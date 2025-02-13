// Copyright 2021 Emmett Lalish
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "meshIO.h"

#include <algorithm>

#include "assimp/Exporter.hpp"
#include "assimp/Importer.hpp"
#include "assimp/pbrmaterial.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

namespace manifold {

Mesh ImportMesh(const std::string& filename) {
  std::string ext = filename.substr(filename.find_last_of(".") + 1);
  const bool isYup = ext == "glb" || ext == "gltf";

  Assimp::Importer importer;
  importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS,                    //
                              aiComponent_NORMALS |                      //
                                  aiComponent_TANGENTS_AND_BITANGENTS |  //
                                  aiComponent_COLORS |                   //
                                  aiComponent_TEXCOORDS |                //
                                  aiComponent_BONEWEIGHTS |              //
                                  aiComponent_ANIMATIONS |               //
                                  aiComponent_TEXTURES |                 //
                                  aiComponent_LIGHTS |                   //
                                  aiComponent_CAMERAS |                  //
                                  aiComponent_MATERIALS);
  importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                              aiPrimitiveType_POINT | aiPrimitiveType_LINE);
  const aiScene* scene =
      importer.ReadFile(filename,                             //
                        aiProcess_JoinIdenticalVertices |     //
                            aiProcess_Triangulate |           //
                            aiProcess_RemoveComponent |       //
                            aiProcess_PreTransformVertices |  //
                            aiProcess_SortByPType |           //
                            aiProcess_OptimizeMeshes);

  ALWAYS_ASSERT(scene, userErr, importer.GetErrorString());

  Mesh mesh_out;
  for (int i = 0; i < scene->mNumMeshes; ++i) {
    const aiMesh* mesh_i = scene->mMeshes[i];
    for (int j = 0; j < mesh_i->mNumVertices; ++j) {
      const aiVector3D vert = mesh_i->mVertices[j];
      mesh_out.vertPos.push_back(isYup ? glm::vec3(vert.z, vert.x, vert.y)
                                       : glm::vec3(vert.x, vert.y, vert.z));
    }
    for (int j = 0; j < mesh_i->mNumFaces; ++j) {
      const aiFace face = mesh_i->mFaces[j];
      ALWAYS_ASSERT(face.mNumIndices == 3, userErr,
                    "Non-triangular face in " + filename);
      mesh_out.triVerts.emplace_back(face.mIndices[0], face.mIndices[1],
                                     face.mIndices[2]);
    }
  }
  return mesh_out;
}

void ExportMesh(const std::string& filename, const Mesh& mesh,
                const ExportOptions& options) {
  if (mesh.triVerts.size() == 0) {
    std::cout << filename << " was not saved because the input mesh was empty."
              << std::endl;
    return;
  }

  std::string ext = filename.substr(filename.find_last_of(".") + 1);
  const bool isYup = ext == "glb" || ext == "gltf";
  if (ext == "glb") ext = "glb2";
  if (ext == "gltf") ext = "gltf2";

  aiScene* scene = new aiScene();

  scene->mNumMaterials = 1;
  scene->mMaterials = new aiMaterial*[scene->mNumMaterials];
  scene->mMaterials[0] = new aiMaterial();

  aiMaterial* material = scene->mMaterials[0];
  material->AddProperty(&options.mat.roughness, 1,
                        AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR);
  material->AddProperty(&options.mat.metalness, 1,
                        AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR);
  const glm::vec4& color = options.mat.color;
  aiColor4D baseColor(color.r, color.g, color.b, color.a);
  material->AddProperty(&baseColor, 1, AI_MATKEY_COLOR_DIFFUSE);

  scene->mNumMeshes = 1;
  scene->mMeshes = new aiMesh*[scene->mNumMeshes];
  scene->mMeshes[0] = new aiMesh();
  scene->mMeshes[0]->mMaterialIndex = 0;

  scene->mRootNode = new aiNode();
  scene->mRootNode->mNumMeshes = 1;
  scene->mRootNode->mMeshes = new uint[scene->mRootNode->mNumMeshes];
  scene->mRootNode->mMeshes[0] = 0;

  aiMesh* mesh_out = scene->mMeshes[0];
  mesh_out->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

  mesh_out->mNumVertices = mesh.vertPos.size();
  mesh_out->mVertices = new aiVector3D[mesh_out->mNumVertices];
  if (!options.faceted) {
    ALWAYS_ASSERT(
        mesh.vertNormal.size() == mesh.vertPos.size(), userErr,
        "vertNormal must be the same length as vertPos when faceted is false.");
    mesh_out->mNormals = new aiVector3D[mesh_out->mNumVertices];
  }
  if (!options.mat.vertColor.empty()) {
    ALWAYS_ASSERT(mesh.vertPos.size() == options.mat.vertColor.size(), userErr,
                  "If present, vertColor must be the same length as vertPos.");
    mesh_out->mColors[0] = new aiColor4D[mesh_out->mNumVertices];
  }

  for (int i = 0; i < mesh_out->mNumVertices; ++i) {
    const glm::vec3& v = mesh.vertPos[i];
    mesh_out->mVertices[i] =
        isYup ? aiVector3D(v.y, v.z, v.x) : aiVector3D(v.x, v.y, v.z);
    if (!options.faceted) {
      const glm::vec3& n = mesh.vertNormal[i];
      mesh_out->mNormals[i] =
          isYup ? aiVector3D(n.y, n.z, n.x) : aiVector3D(n.x, n.y, n.z);
    }
    if (!options.mat.vertColor.empty()) {
      const glm::vec4& c = options.mat.vertColor[i];
      mesh_out->mColors[0][i] = aiColor4D(c.r, c.g, c.b, c.a);
    }
  }

  mesh_out->mNumFaces = mesh.triVerts.size();
  mesh_out->mFaces = new aiFace[mesh_out->mNumFaces];

  for (int i = 0; i < mesh_out->mNumFaces; ++i) {
    aiFace& face = mesh_out->mFaces[i];
    face.mNumIndices = 3;
    face.mIndices = new uint[face.mNumIndices];
    for (int j : {0, 1, 2}) face.mIndices[j] = mesh.triVerts[i][j];
  }

  Assimp::Exporter exporter;

  // int n = exporter.GetExportFormatCount();
  // for (int i = 0; i < n; ++i) {
  //   auto desc = exporter.GetExportFormatDescription(i);
  //   std::cout << i << ", id = " << desc->id << ", " << desc->description
  //             << std::endl;
  // }

  auto result = exporter.Export(scene, ext, filename);

  delete scene;

  ALWAYS_ASSERT(result == AI_SUCCESS, userErr, exporter.GetErrorString());
}

}  // namespace manifold