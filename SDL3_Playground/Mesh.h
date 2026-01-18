#pragma once

#include "SimpleEngine/Asset/AssetId.h"
#include "SimpleEngine/Core/Container/Array.h"
#include "SimpleEngine/Core/Math/Math.h"

using namespace se;
using namespace se::core;

struct Material
{
    asset::AssetId albedo_texture_id;
    asset::AssetId normal_texture_id;
};

enum class MeshType
{
    Static,
    Skeletal
};

struct Mesh
{
    virtual ~Mesh() = default;

    MeshType type;
    asset::AssetId id;
    String name;
};

struct alignas(16) Vertex
{
    Vector3f position;
    Vector3f normal;
    Vector2f tex_coord;
    Vector4f tangent;
};

struct SkinVertex
{
    uint32 bone_indices[4];
    float bone_weights[4];
};

struct MeshSection
{
    uint32 material_index;
    uint32 index_start;
    uint32 index_count;
};

struct StaticMesh : public Mesh
{
    StaticMesh() { type = MeshType::Static; }

    Array<Vertex> vertices;
    Array<uint32> indices;
    Array<MeshSection> sections;
    Array<Material> materials;
};

struct SkeletalMesh : public Mesh
{
    SkeletalMesh() { type = MeshType::Skeletal; }

    Array<Vertex> vertices;
    Array<SkinVertex> skin_vertices;
    Array<uint32> indices;
    Array<MeshSection> sections;
    Array<Material> materials;
};
