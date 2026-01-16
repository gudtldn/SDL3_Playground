#pragma once

#include "Mesh.h"
#include <filesystem>
#include <memory>

class AssetLoader
{
public:
    static std::shared_ptr<StaticMesh> LoadStaticMesh(const std::filesystem::path& path);
};
