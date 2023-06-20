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

#include "SampleRenderer.h"

// our helper library for window handling
#include "glfWindow/GLFWindow.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "3rdParty/stb_image_write.h"

#include <GL/gl.h>

/*! \namespace osc - Optix Siggraph Course */
namespace osc
{

struct SampleWindow: public GLFCameraWindow
{
  SampleWindow(const std::string& title, const Model* model, const Camera& camera, const float worldScale):
    GLFCameraWindow(title, camera.from, camera.at, camera.up, worldScale),
    sample(model)
  {
    sample.setCamera(camera);
  }

  virtual void render() override
  {
    if (cameraFrame.modified)
    {
      sample.setCamera(Camera{cameraFrame.get_from(), cameraFrame.get_at(), cameraFrame.get_up()});
      cameraFrame.modified = false;
    }
    sample.render();
  }

  virtual void key(int key, int mods) override
  {
    switch (key)
    {
      case 'w':
      case 'W':
      {
        auto color = gdt::randomColor(rand());
        std::cout << "Changing bg color: " << color << std::endl;
        sample.setBackground(color);
        break;
      }
      case 'f':
      case 'F':
        std::cout << "Entering 'fly' mode" << std::endl;
        if (flyModeManip)
          cameraFrameManip = flyModeManip;
        break;
      case 'i':
      case 'I':
        std::cout << "Entering 'inspect' mode" << std::endl;
        if (inspectModeManip)
          cameraFrameManip = inspectModeManip;
        break;
      default:
        if (cameraFrameManip)
          cameraFrameManip->key(key, mods);
    }
  }

  virtual void draw() override
  {
    sample.downloadPixels(pixels.data());
    if (fbTexture == 0)
      glGenTextures(1, &fbTexture);

    glBindTexture(GL_TEXTURE_2D, fbTexture);
    GLenum texFormat = GL_RGBA;
    GLenum texelType = GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, texFormat, fbSize.x, fbSize.y, 0, GL_RGBA, texelType, pixels.data());

    glDisable(GL_LIGHTING);
    glColor3f(1, 1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, fbTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glDisable(GL_DEPTH_TEST);

    glViewport(0, 0, fbSize.x, fbSize.y);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.f, (float)fbSize.x, 0.f, (float)fbSize.y, -1.f, 1.f);

    glBegin(GL_QUADS);
    {
      glTexCoord2f(0.f, 0.f);
      glVertex3f(0.f, 0.f, 0.f);

      glTexCoord2f(0.f, 1.f);
      glVertex3f(0.f, (float)fbSize.y, 0.f);

      glTexCoord2f(1.f, 1.f);
      glVertex3f((float)fbSize.x, (float)fbSize.y, 0.f);

      glTexCoord2f(1.f, 0.f);
      glVertex3f((float)fbSize.x, 0.f, 0.f);
    }
    glEnd();
  }

  virtual void resize(const vec2i& newSize)
  {
    fbSize = newSize;
    sample.resize(newSize);
    pixels.resize(newSize.x * newSize.y);
  }

  vec2i fbSize;
  GLuint fbTexture{0};
  SampleRenderer sample;
  std::vector<uint32_t> pixels;
};

/*! main entry point to this example - initially optix, print hello
    world, then exit */
extern "C" int main(int ac, char** av)
{
  try
  {
    Model* model = loadOBJ(
#ifdef _WIN32
      // on windows, visual studio creates _two_ levels of build dir
      // (x86/Release)
      "C:/Users/mayeg/Documents/U-TAD/Master/Practicas/Anyverse/dogwood-objs/Dogwood_Summer_Spring_High.mxs.obj"
#else
      // on linux, common practice is to have ONE level of build dir
      // (say, <project>/build/)...
      "../models/sponza.obj"
#endif
    );
    // Camera camera = { /*from*/vec3f(-1293.07f, 154.681f, -0.7304f),
    //                   /* at */model->bounds.center()-vec3f(0,400,0),
    //                   /* up */vec3f(0.f,1.f,0.f) };
    // something approximating the scale of the world, so the
    // camera knows how much to move for any given user interaction:
    Camera camera = {/*from*/ vec3f(0.f, 0.f, -0.7304f),
                     /* at */ model->bounds.center() - vec3f(0, model->bounds.center().y, 0),
                     /* up */ vec3f(0.f, 1.f, 0.f)};
    // something approximating the scale of the world, so the
    // camera knows how much to move for any given user interaction:
    const float worldScale = length(model->bounds.span());

    TriangleMesh floor;
    // 100x100 thin ground plane
    floor.diffuse = vec3f(0.f, 1.f, 0.f);
    floor.addCube(vec3f(0.f, -0.1f, 0.f), vec3f(40.f, .1f, 40.f));
    model->meshes.push_back(&floor);

    Model* model2 = loadOBJ("C:/Users/mayeg/Documents/U-TAD/Master/Practicas/Anyverse/apple_sapling-objs/"
                            "Apple_Sapling_Autumn_High.mxs.obj");
    // std::cout << model2->meshes[0]->vertex.at(0) << std::endl;
    std::for_each(model2->meshes.begin(),
                  model2->meshes.end(),
                  [](auto& mesh)
                  {
                    std::for_each(mesh->vertex.begin(),
                                  mesh->vertex.end(),
                                  [](auto& vertex)
                                  { vertex = xfmPoint(affine3f::translate(vec3f(-5.f, 0.f, -5.f)), vertex); });
                  });
    // std::cout << model2->meshes[0]->vertex.at(0) << std::endl;
    model->meshes.insert(model->meshes.end(), model2->meshes.begin(), model2->meshes.end());

    SampleWindow* window = new SampleWindow("Optix 9 Course Example", model, camera, worldScale);
    window->run();

    auto frame_size = window->sample.getFrameSize();
    std::vector<uint32_t> pixels(frame_size.x * frame_size.y);
    window->sample.downloadPixels(pixels.data());

    const std::string fileName = "example09.bmp";
    stbi_write_bmp(fileName.c_str(), frame_size.x, frame_size.y, 4, pixels.data());
  }
  catch (std::runtime_error& e)
  {
    std::cout << GDT_TERMINAL_RED << "FATAL ERROR: " << e.what() << GDT_TERMINAL_DEFAULT << std::endl;
    std::cout << "Did you forget to copy sponza.obj and sponza.mtl into your optix7course/models directory?"
              << std::endl;
    exit(1);
  }
  return 0;
}

} // ::osc
