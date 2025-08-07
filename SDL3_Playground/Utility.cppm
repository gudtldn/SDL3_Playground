module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
export module Playground.Utility;

import SimpleEngine.Types;
import SimpleEngine.Utility;
import std;


export namespace playground::utility::shader_utils
{
SDL_GPUShader* LoadShader(
    SDL_GPUDevice* device,
    const std::filesystem::path& shader_path,
    uint32 sampler_count,
    uint32 uniform_buffer_count,
    uint32 storage_buffer_count,
    uint32 storage_texture_count
)
{
    if (auto data = se::utility::file_utils::ReadFromBinary(shader_path))
    {
        const std::u8string path = shader_path.generic_u8string();

        // 파일 확장자로 stage 구분
        SDL_GPUShaderStage stage;
        if (path.find(u8".vert") != std::string::npos)
        {
            stage = SDL_GPU_SHADERSTAGE_VERTEX;
        }
        else if (path.find(u8".frag") != std::string::npos)
        {
            stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        }
        else
        {
            return nullptr;
        }

        const SDL_GPUShaderFormat backend_formats = SDL_GetGPUShaderFormats(device);
        SDL_GPUShaderFormat format;
        const char* entrypoint;

        if (backend_formats & SDL_GPU_SHADERFORMAT_SPIRV)
        {
            format = SDL_GPU_SHADERFORMAT_SPIRV;
            entrypoint = "main";
        } else if (backend_formats & SDL_GPU_SHADERFORMAT_MSL)
        {
            format = SDL_GPU_SHADERFORMAT_MSL;
            entrypoint = "main0";
        } else if (backend_formats & SDL_GPU_SHADERFORMAT_DXIL)
        {
            format = SDL_GPU_SHADERFORMAT_DXIL;
            entrypoint = "main";
        } else
        {
            return nullptr;
        }

        const SDL_GPUShaderCreateInfo info = {
            .code_size = data->size(),
            .code = data->data(),
            .entrypoint = entrypoint,
            .format = format,
            .stage = stage,
            .num_samplers = sampler_count,
            .num_storage_textures = storage_texture_count,
            .num_storage_buffers = storage_buffer_count,
            .num_uniform_buffers = uniform_buffer_count,
        };

        if (SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info))
        {
            return shader;
        }
    }
    return nullptr;
}
}
