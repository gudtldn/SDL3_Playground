#include "AssetLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

using namespace se;
using namespace se::core;

std::shared_ptr<StaticMesh> AssetLoader::LoadStaticMesh(const std::filesystem::path& path)
{
    Assimp::Importer importer;
    // aiProcess_PreTransformVertices: Combine the scene hierarchy into a single mesh for simplicity if desired,
    // but preserving hierarchy is usually better. For a single StaticMesh struct, we might want to flatten.
    // Let's not flatten yet, but we will merge meshes into the single StaticMesh buffers.
    const aiScene* scene = importer.ReadFile(path.string(),
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        // Handle error (log it)
        std::cerr << "Assimp Error: " << importer.GetErrorString() << std::endl;
        return nullptr;
    }

    auto mesh = std::make_shared<StaticMesh>();
    mesh->name = path.filename().string().c_str();

    // Process all meshes in the scene and merge them into our StaticMesh
    // Note: This ignores the node hierarchy transform!
    // Ideally we should traverse the node graph and bake transforms if we want a single static mesh representing the whole scene frozen.
    // For now, let's just merge all meshes.
    
    uint32 vertex_offset = 0;
    uint32 index_offset = 0;

    mesh->vertices.Reserve(scene->mNumMeshes * 1000); // Heuristic reserve
    mesh->indices.Reserve(scene->mNumMeshes * 3000);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* ai_mesh = scene->mMeshes[i];
        MeshSection section;
        section.material_index = ai_mesh->mMaterialIndex;
        section.index_start = index_offset;
        section.index_count = ai_mesh->mNumFaces * 3;

        // Vertices
        for (unsigned int v = 0; v < ai_mesh->mNumVertices; ++v)
        {
            Vertex vertex;
            vertex.position = Vector3f(ai_mesh->mVertices[v].x, ai_mesh->mVertices[v].y, ai_mesh->mVertices[v].z);
            
            if (ai_mesh->HasNormals())
            {
                vertex.normal = Vector3f(ai_mesh->mNormals[v].x, ai_mesh->mNormals[v].y, ai_mesh->mNormals[v].z);
            }
            
            if (ai_mesh->HasTangentsAndBitangents())
            {
                vertex.tangent = Vector3f(ai_mesh->mTangents[v].x, ai_mesh->mTangents[v].y, ai_mesh->mTangents[v].z);
            }
            
            if (ai_mesh->mTextureCoords[0])
            {
                vertex.tex_coord = Vector2f(ai_mesh->mTextureCoords[0][v].x, ai_mesh->mTextureCoords[0][v].y);
            }
            else
            {
                vertex.tex_coord = Vector2f(0.0f, 0.0f);
            }

            mesh->vertices.Push(vertex);
        }

        // Indices
        for (unsigned int f = 0; f < ai_mesh->mNumFaces; ++f)
        {
            aiFace face = ai_mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                mesh->indices.Push(face.mIndices[j] + vertex_offset);
            }
        }

        mesh->sections.Push(section);

        vertex_offset += ai_mesh->mNumVertices;
        index_offset += section.index_count;
    }

    // Load Materials (Basic placeholder)
    for (unsigned int m = 0; m < scene->mNumMaterials; ++m)
    {
        Material material;
        // Load textures logic here... for now just push empty material
        mesh->materials.Push(material);
    }

    return mesh;
}
