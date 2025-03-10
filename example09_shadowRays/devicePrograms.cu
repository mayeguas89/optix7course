// ======================================================================== //
// Copyright 2018-2019 Ingo Wald                                            //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "LaunchParams.h"

#include <cuda_runtime.h>
#include <optix_device.h>

using namespace osc;

namespace osc
{

/*! launch parameters in constant memory, filled in by optix upon
      optixLaunch (this gets filled in from the buffer we pass to
      optixLaunch) */
extern "C" __constant__ LaunchParams optixLaunchParams;

static __forceinline__ __device__ void* unpackPointer(uint32_t i0, uint32_t i1)
{
  const uint64_t uptr = static_cast<uint64_t>(i0) << 32 | i1;
  void* ptr = reinterpret_cast<void*>(uptr);
  return ptr;
}

static __forceinline__ __device__ void packPointer(void* ptr, uint32_t& i0, uint32_t& i1)
{
  const uint64_t uptr = reinterpret_cast<uint64_t>(ptr);
  i0 = uptr >> 32;
  i1 = uptr & 0x00000000ffffffff;
}

template<typename T>
static __forceinline__ __device__ T* getPRD()
{
  const uint32_t u0 = optixGetPayload_0();
  const uint32_t u1 = optixGetPayload_1();
  return reinterpret_cast<T*>(unpackPointer(u0, u1));
}

//------------------------------------------------------------------------------
// closest hit and anyhit programs for radiance-type rays.
//
// Note eventually we will have to create one pair of those for each
// ray type and each geometry type we want to render; but this
// simple example doesn't use any actual geometries yet, so we only
// create a single, dummy, set of them (we do have to have at least
// one group of them to set up the SBT)
//------------------------------------------------------------------------------

extern "C" __global__ void __closesthit__shadow()
{
  /* not going to be used ... */
}

extern "C" __global__ void __closesthit__radiance()
{
  const TriangleMeshSBTData& sbtData = *(const TriangleMeshSBTData*)optixGetSbtDataPointer();

  // ------------------------------------------------------------------
  // gather some basic hit information
  // ------------------------------------------------------------------
  const int primID = optixGetPrimitiveIndex();
  const vec3i index = sbtData.index[primID];
  const float u = optixGetTriangleBarycentrics().x;
  const float v = optixGetTriangleBarycentrics().y;

  // ------------------------------------------------------------------
  // compute normal, using either shading normal (if avail), or
  // geometry normal (fallback)
  // ------------------------------------------------------------------
  const vec3f& A = sbtData.vertex[index.x];
  const vec3f& B = sbtData.vertex[index.y];
  const vec3f& C = sbtData.vertex[index.z];
  vec3f Ng = cross(B - A, C - A);
  vec3f Ns =
    (sbtData.normal) ?
      ((1.f - u - v) * sbtData.normal[index.x] + u * sbtData.normal[index.y] + v * sbtData.normal[index.z]) :
      Ng;

  // ------------------------------------------------------------------
  // face-forward and normalize normals
  // ------------------------------------------------------------------
  const vec3f rayDir = optixGetWorldRayDirection();

  if (dot(rayDir, Ng) > 0.f)
    Ng = -Ng;
  Ng = normalize(Ng);

  if (dot(Ng, Ns) < 0.f)
    Ns -= 2.f * dot(Ng, Ns) * Ng;
  Ns = normalize(Ns);

  // ------------------------------------------------------------------
  // compute diffuse material color, including diffuse texture, if
  // available
  // ------------------------------------------------------------------
  vec3f diffuseColor = sbtData.color;

  // ------------------------------------------------------------------
  // Checker Pattern
  // ------------------------------------------------------------------
  const float pattern = 10.f;
  auto p = (1.f - u - v) * sbtData.vertex[index.x] + u * sbtData.vertex[index.y] + v * sbtData.vertex[index.z];
  float sines = gdt::sin(pattern * p.x) * gdt::sin(pattern * p.y) * gdt::sin(pattern * p.z);
  diffuseColor = vec3f{0.f, 1.f, 0.f};
  if (sines < 0.f)
    diffuseColor = vec3f{1.f, 0.f, 0.f};

  if (sbtData.hasTexture && sbtData.texcoord)
  {
    const vec2f tc =
      (1.f - u - v) * sbtData.texcoord[index.x] + u * sbtData.texcoord[index.y] + v * sbtData.texcoord[index.z];

    vec4f fromTexture = tex2D<float4>(sbtData.texture, tc.x, tc.y);
    diffuseColor *= (vec3f)fromTexture;
  }

  // ------------------------------------------------------------------
  // compute shadow
  // ------------------------------------------------------------------
  const vec3f surfPos =
    (1.f - u - v) * sbtData.vertex[index.x] + u * sbtData.vertex[index.y] + v * sbtData.vertex[index.z];
  const vec3f lightPos(-907.108f, 2205.875f, -400.0267f);
  const vec3f lightDir = lightPos - surfPos;

  // trace shadow ray:
  vec3f lightVisibility = 0.f;
  // the values we store the PRD pointer in:
  uint32_t u0, u1;
  packPointer(&lightVisibility, u0, u1);
  optixTrace(optixLaunchParams.traversable,
             surfPos + 1e-3f * Ng,
             lightDir,
             1e-3f,       // tmin
             1.f - 1e-3f, // tmax
             0.0f,        // rayTime
             OptixVisibilityMask(255),
             // For shadow rays: skip any/closest hit shaders and terminate on first
             // intersection with anything. The miss shader is used to mark if the
             // light was visible.
             OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT
               | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
             SHADOW_RAY_TYPE, // SBT offset
             RAY_TYPE_COUNT,  // SBT stride
             SHADOW_RAY_TYPE, // missSBTIndex
             u0,
             u1);

  // ------------------------------------------------------------------
  // final shading: a bit of ambient, a bit of directional ambient,
  // and directional component based on shadowing
  // ------------------------------------------------------------------
  const float cosDN = 0.1f + .8f * fabsf(dot(rayDir, Ns));

  vec3f& prd = *(vec3f*)getPRD<vec3f>();
  prd = (.1f + (.2f + .8f * lightVisibility) * cosDN) * diffuseColor;
}

extern "C" __global__ void __anyhit__radiance()
{ /*! for this simple example, this will remain empty */
}

extern "C" __global__ void __anyhit__shadow()
{ /*! not going to be used */
}

//------------------------------------------------------------------------------
// miss program that gets called for any ray that did not have a
// valid intersection
//
// as with the anyhit/closest hit programs, in this example we only
// need to have _some_ dummy function to set up a valid SBT
// ------------------------------------------------------------------------------

extern "C" __global__ void __miss__radiance()
{
  vec3f& prd = *(vec3f*)getPRD<vec3f>();
  // set to constant white as background color
  prd = optixLaunchParams.bgColor;
}

extern "C" __global__ void __miss__shadow()
{
  // we didn't hit anything, so the light is visible
  vec3f& prd = *(vec3f*)getPRD<vec3f>();
  prd = vec3f(1.f);
}

//------------------------------------------------------------------------------
// ray gen program - the actual rendering happens in here
//------------------------------------------------------------------------------
extern "C" __global__ void __raygen__renderFrame()
{
  // compute a test pattern based on pixel ID
  const int ix = optixGetLaunchIndex().x;
  const int iy = optixGetLaunchIndex().y;

  const auto& camera = optixLaunchParams.camera;

  // our per-ray data for this example. what we initialize it to
  // won't matter, since this value will be overwritten by either
  // the miss or hit program, anyway
  vec3f pixelColorPRD = vec3f(0.f);

  // the values we store the PRD pointer in:
  uint32_t u0, u1;
  packPointer(&pixelColorPRD, u0, u1);

  // normalized screen plane position, in [0,1]^2
  const vec2f screen(vec2f(ix + .5f, iy + .5f) / vec2f(optixLaunchParams.frame.size));

  // generate ray direction
  vec3f rayDir =
    normalize(camera.direction + (screen.x - 0.5f) * camera.horizontal + (screen.y - 0.5f) * camera.vertical);

  optixTrace(optixLaunchParams.traversable,
             camera.position,
             rayDir,
             0.f,                           // tmin
             1e20f,                         // tmax
             0.0f,                          // rayTime
             OptixVisibilityMask(255),
             OPTIX_RAY_FLAG_DISABLE_ANYHIT, //OPTIX_RAY_FLAG_NONE,
             RADIANCE_RAY_TYPE,             // SBT offset
             RAY_TYPE_COUNT,                // SBT stride
             RADIANCE_RAY_TYPE,             // missSBTIndex
             u0,
             u1);

  const int r = int(255.99f * pixelColorPRD.x);
  const int g = int(255.99f * pixelColorPRD.y);
  const int b = int(255.99f * pixelColorPRD.z);

  // convert to 32-bit rgba value (we explicitly set alpha to 0xff
  // to make stb_image_write happy ...
  const uint32_t rgba = 0xff000000 | (r << 0) | (g << 8) | (b << 16);

  // and write to frame buffer ...
  const uint32_t fbIndex = ix + iy * optixLaunchParams.frame.size.x;
  optixLaunchParams.frame.colorBuffer[fbIndex] = rgba;
}

} // ::osc
