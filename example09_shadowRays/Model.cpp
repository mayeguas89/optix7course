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

#include "Model.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "3rdParty/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "3rdParty/stb_image.h"

//std
#include <set>

namespace std
{
inline bool operator<(const tinyobj::index_t& a, const tinyobj::index_t& b)
{
  if (a.vertex_index < b.vertex_index)
    return true;
  if (a.vertex_index > b.vertex_index)
    return false;

  if (a.normal_index < b.normal_index)
    return true;
  if (a.normal_index > b.normal_index)
    return false;

  if (a.texcoord_index < b.texcoord_index)
    return true;
  if (a.texcoord_index > b.texcoord_index)
    return false;

  return false;
}
}

/*! \namespace osc - Optix Siggraph Course */
namespace osc
{

//! add aligned cube with front-lower-left corner and size
void TriangleMesh::addCube(const vec3f& center, const vec3f& size)
{
  PING;
  affine3f xfm;
  xfm.p = center - 0.5f * size;
  xfm.l.vx = vec3f(size.x, 0.f, 0.f);
  xfm.l.vy = vec3f(0.f, size.y, 0.f);
  xfm.l.vz = vec3f(0.f, 0.f, size.z);
  addUnitCube(xfm);
}

/*! add a unit cube (subject to given xfm matrix) to the current
      triangleMesh */
void TriangleMesh::addUnitCube(const affine3f& xfm)
{
  int firstVertexID = (int)vertex.size();
  vertex.push_back(xfmPoint(xfm, vec3f(0.f, 0.f, 0.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(1.f, 0.f, 0.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(0.f, 1.f, 0.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(1.f, 1.f, 0.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(0.f, 0.f, 1.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(1.f, 0.f, 1.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(0.f, 1.f, 1.f)));
  vertex.push_back(xfmPoint(xfm, vec3f(1.f, 1.f, 1.f)));

  int indices[] = {0, 1, 3, 2, 3, 0, 5, 7, 6, 5, 6, 4, 0, 4, 5, 0, 5, 1,
                   2, 3, 7, 2, 7, 6, 1, 5, 7, 1, 7, 3, 4, 0, 2, 4, 2, 6};
  for (int i = 0; i < 12; i++)
    index.push_back(firstVertexID + vec3i(indices[3 * i + 0], indices[3 * i + 1], indices[3 * i + 2]));
}

/*! find vertex with given position, normal, texcoord, and return
      its vertex ID, or, if it doesn't exit, add it to the mesh, and
      its just-created index */
int addVertex(TriangleMesh* mesh,
              tinyobj::attrib_t& attributes,
              const tinyobj::index_t& idx,
              std::map<tinyobj::index_t, int>& knownVertices)
{
  if (knownVertices.find(idx) != knownVertices.end())
    return knownVertices[idx];

  const vec3f* vertex_array = (const vec3f*)attributes.vertices.data();
  const vec3f* normal_array = (const vec3f*)attributes.normals.data();
  const vec2f* texcoord_array = (const vec2f*)attributes.texcoords.data();

  int newID = (int)mesh->vertex.size();
  knownVertices[idx] = newID;

  mesh->vertex.push_back(vertex_array[idx.vertex_index]);
  if (idx.normal_index >= 0)
  {
    while (mesh->normal.size() < mesh->vertex.size())
      mesh->normal.push_back(normal_array[idx.normal_index]);
  }
  if (idx.texcoord_index >= 0)
  {
    while (mesh->texcoord.size() < mesh->vertex.size())
      mesh->texcoord.push_back(texcoord_array[idx.texcoord_index]);
  }

  // just for sanity's sake:
  if (mesh->texcoord.size() > 0)
    mesh->texcoord.resize(mesh->vertex.size());
  // just for sanity's sake:
  if (mesh->normal.size() > 0)
    mesh->normal.resize(mesh->vertex.size());

  return newID;
}

/*! load a texture (if not already loaded), and return its ID in the
      model's textures[] vector. Textures that could not get loaded
      return -1 */
int loadTexture(Model* model,
                std::map<std::string, int>& knownTextures,
                const std::string& inFileName,
                const std::string& modelPath)
{
  if (inFileName == "")
    return -1;

  if (knownTextures.find(inFileName) != knownTextures.end())
    return knownTextures[inFileName];

  std::string fileName = inFileName;
  // first, fix backspaces:
  for (auto& c: fileName)
    if (c == '\\')
      c = '/';
  fileName = modelPath + "/" + fileName;

  vec2i res;
  int comp;
  unsigned char* image = stbi_load(fileName.c_str(), &res.x, &res.y, &comp, STBI_rgb_alpha);
  int textureID = -1;
  if (image)
  {
    textureID = (int)model->textures.size();
    Texture* texture = new Texture;
    texture->resolution = res;
    texture->pixel = (uint32_t*)image;

    /* iw - actually, it seems that stbi loads the pictures
         mirrored along the y axis - mirror them here */
    for (int y = 0; y < res.y / 2; y++)
    {
      uint32_t* line_y = texture->pixel + y * res.x;
      uint32_t* mirrored_y = texture->pixel + (res.y - 1 - y) * res.x;
      int mirror_y = res.y - 1 - y;
      for (int x = 0; x < res.x; x++)
      {
        std::swap(line_y[x], mirrored_y[x]);
      }
    }

    model->textures.push_back(texture);
  }
  else
  {
    std::cout << GDT_TERMINAL_RED << "Could not load texture from " << fileName << "!" << GDT_TERMINAL_DEFAULT
              << std::endl;
  }

  knownTextures[inFileName] = textureID;
  return textureID;
}

Model* loadOBJ(const std::string& objFile)
{
  Model* model = new Model;

  const std::string modelDir = objFile.substr(0, objFile.rfind('/') + 1);

  tinyobj::attrib_t attributes;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err = "";

  bool readOK = tinyobj::LoadObj(&attributes,
                                 &shapes,
                                 &materials,
                                 &err,
                                 &err,
                                 objFile.c_str(),
                                 modelDir.c_str(),
                                 /* triangulate */ true);
  if (!readOK)
  {
    throw std::runtime_error("Could not read OBJ model from " + objFile + " : " + err);
  }

  // if (materials.empty())
  //   throw std::runtime_error("could not parse materials ...");

  std::cout << "Done loading obj file - found " << shapes.size() << " shapes with " << materials.size()
            << " materials" << std::endl;
  std::map<std::string, int> knownTextures;
  for (int shapeID = 0; shapeID < (int)shapes.size(); shapeID++)
  {
    tinyobj::shape_t& shape = shapes[shapeID];

    std::set<int> materialIDs;
    for (auto faceMatID: shape.mesh.material_ids)
      materialIDs.insert(faceMatID);

    for (int materialID: materialIDs)
    {
      std::map<tinyobj::index_t, int> knownVertices;
      TriangleMesh* mesh = new TriangleMesh;

      for (int faceID = 0; faceID < shape.mesh.material_ids.size(); faceID++)
      {
        if (shape.mesh.material_ids[faceID] != materialID)
          continue;
        tinyobj::index_t idx0 = shape.mesh.indices[3 * faceID + 0];
        tinyobj::index_t idx1 = shape.mesh.indices[3 * faceID + 1];
        tinyobj::index_t idx2 = shape.mesh.indices[3 * faceID + 2];

        vec3i idx(addVertex(mesh, attributes, idx0, knownVertices),
                  addVertex(mesh, attributes, idx1, knownVertices),
                  addVertex(mesh, attributes, idx2, knownVertices));
        mesh->index.push_back(idx);
        if (materialID != -1)
          mesh->diffuse = (const vec3f&)materials[materialID].diffuse;
        else
          mesh->diffuse = gdt::randomColor(rand());
        // mesh->diffuseTextureID =
        //   loadTexture(model, knownTextures, materials[materialID].diffuse_texname, modelDir);
      }

      if (mesh->vertex.empty())
        delete mesh;
      else
        model->meshes.push_back(mesh);
    }
  }

  // of course, you should be using tbb::parallel_for for stuff
  // like this:
  for (auto mesh: model->meshes)
    for (auto vtx: mesh->vertex)
      model->bounds.extend(vtx);

  std::cout << "created a total of " << model->meshes.size() << " meshes" << std::endl;
  return model;
}
}
