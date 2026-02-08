#include "App.h"

#include <cassert>
#include <filesystem>
#include <format>
#include <ranges>

#include "Graphics/Compiler/Provider.h"
#include "SimpleEngine/Asset/Pipeline/AssetImporter.h"
#include "SimpleEngine/Asset/Pipeline/Factories/StaticMeshFactory.h"
#include "SimpleEngine/Asset/Pipeline/Translators/AssimpTranslator.h"
#include "SimpleEngine/Asset/Types/MeshTypes.h"
#include "SimpleEngine/Core/HAL/FileDialog.h"
#include "SimpleEngine/Core/Math/Math.h"
#include "SimpleEngine/Core/Math/Ray.h"
#include "SimpleEngine/ECS/Query.h"
#include "SimpleEngine/ECS/Components/TransformComponent.h"
#include "SimpleEngine/Graphics/MeshPrimitives.h"
#include "SimpleEngine/Graphics/Manager/PSOManager.h"
#include "SimpleEngine/Graphics/Memory/GpuResourceManager.h"
#include "SimpleEngine/Utility/StringUtils.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "SDL3/SDL.h"
#include "SDL3_shadercross/SDL_shadercross.h"
#include "tracy/Tracy.hpp"

#pragma warning(disable: 4996) // deprecated warning

using namespace se;
using namespace se::ecs;
using namespace se::graphics;

static se::Ray ScreenToWorldRay(float mouse_x, float mouse_y, float window_w, float window_h, const Matrix4x4& view_mat, const Matrix4x4& proj_mat)
{
    // NDC: x [-1, 1], y [1, -1]
    const float ndc_x = (2.0f * mouse_x) / window_w - 1.0f;
    const float ndc_y = 1.0f - (2.0f * mouse_y) / window_h;

    const Matrix4x4 inv_vp = (view_mat * proj_mat).Inverse();

    const Vector4 near_clip(ndc_x, ndc_y, 0.0f, 1.0f);
    const Vector4 far_clip(ndc_x, ndc_y, 1.0f, 1.0f);

    Vector4 near_world = near_clip * inv_vp;
    Vector4 far_world = far_clip * inv_vp;

    near_world /= near_world.w;
    far_world /= far_world.w;

    const Vector3 ray_origin = Vector3(near_world.x, near_world.y, near_world.z);
    const Vector3 ray_dir = (Vector3(far_world.x, far_world.y, far_world.z) - ray_origin).GetNormalized();

    return se::Ray(ray_origin, ray_dir);
}

double App::CurrentTime = 0.0;
double App::LastTime = 0.0;
double App::DeltaTime = 1.0 / 60.0;
double App::FixedDeltaTime = 1.0 / 60.0;
uint64 App::TotalElapsedTime = 0;

uint32 App::TargetFps = 24000;
double App::TargetFrameTime = 1.0 / static_cast<double>(TargetFps);

App* App::Instance = nullptr;


struct Camera
{
    Quaternion rotation = Quaternion::Identity();
    Vector3 position = Vector3{ 0, -8, 6 };

    double camera_speed = 10.0f;
    double sensitivity = 0.2f;
    Degree<double> fov = 90_deg;
};

struct MeshComponent
{
    std::shared_ptr<LoadedMesh> mesh;
};

static Camera my_camera;

static SDL_Window* focused_window = nullptr;


App::App()
{
    assert(!Instance);
    Instance = this;
}

App::~App()
{
    Instance = nullptr;
}

void App::Initialize()
{
    ZoneScoped;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
    SDL_ShaderCross_Init();

    asset_importer = std::make_unique<asset::AssetImporter>();
    asset_importer->RegisterTranslator<asset::AssimpTranslator>();
    asset_importer->RegisterFactory<asset::StaticMeshFactory>();

    /* GPU Device 초기화 */
    // 지원할 셰이더 포맷들 설정
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);

#ifdef _DEBUG
    // 디버그 모드 설정
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
#endif

    // dx12로 설정
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "direct3d12");

    // GPU Device 생성
    gpu_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    if (!gpu_device)
    {
        [[maybe_unused]] const char* msg = SDL_GetError();
        SDL_AssertBreakpoint();
    }

    /* 윈도우 초기화 */
    const float main_display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    constexpr int32 width = 1600;
    constexpr int32 height = 900;

    main_window_id = CreateWindow(
        "SDL3 Playground",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int32>(width * main_display_scale),
        static_cast<int32>(height * main_display_scale),
        SDL_WINDOW_RESIZABLE
    );

    SDL_Window* window = GetWindow(main_window_id);
    windows.insert({ main_window_id, window });

    // Swapchain 설정
    SDL_SetGPUSwapchainParameters(
        gpu_device,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_MAILBOX
    );


    // ImGui 초기화
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_display_scale);
    style.FontScaleDpi = main_display_scale;
    IO.ConfigDpiScaleFonts = true;
    IO.ConfigDpiScaleViewports = true;

    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {
        .Device = gpu_device,
        .ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window),
        .MSAASamples = SDL_GPU_SAMPLECOUNT_1,
    };
    ImGui_ImplSDLGPU3_Init(&init_info);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    pso_manager = std::make_unique<PSOManager>(gpu_device);
    pso_manager->SetShaderCacheProvider<editor::CompilingShaderProvider>();

    // GPU Resource Manager 초기화
    gpu_resource_manager = std::make_unique<GpuResourceManager>(gpu_device);

    // 셰이더 컴파일때 사용하는 솔루션 경로
    const std::filesystem::path root = PROJECT_ROOT_DIR;

    // 버텍스 입력 설정
    SDL_GPUVertexBufferDescription vertex_buffer_desc[] = {
        {
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
        }
    };

    SDL_GPUVertexAttribute vertex_attributes[] = {
        {
            .location = 0, // POSITION
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = offsetof(Vertex, position)
        },
        {
            .location = 1, // NORMAL
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = offsetof(Vertex, normal)
        },
        {
            .location = 2, // TEXCOORD
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(Vertex, tex_coord)
        },
        {
            .location = 3, // TANGENT
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = offsetof(Vertex, tangent)
        },
    };

    SDL_GPUColorTargetDescription color_target_desc[] = {
        { .format = SDL_GetGPUSwapchainTextureFormat(gpu_device, window) }
    };

    // 파이프라인 생성
    pipeline = pso_manager->GetOrCreateGraphicsPipeline({
        .vertex_shader_request = {
            .source_path = root / "Shaders/Default.vert.hlsl",
        },
        .fragment_shader_request = {
            .source_path = root / "Shaders/Default.frag.hlsl",
        },
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertex_buffer_desc,
            .num_vertex_buffers = std::size(vertex_buffer_desc),
            .vertex_attributes = vertex_attributes,
            .num_vertex_attributes = std::size(vertex_attributes),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
        },
        .multisample_state = {},
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = color_target_desc,
            .num_color_targets = std::size(color_target_desc),
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            .has_depth_stencil_target = true,
        },
    });

    if (!pipeline)
    {
        SDL_AssertBreakpoint();
    }

    // 선 렌더링용 파이프라인 생성
    line_pipeline = pso_manager->GetOrCreateGraphicsPipeline({
        .vertex_shader_request = {
            .source_path = root / "Shaders/Default.vert.hlsl",
        },
        .fragment_shader_request = {
            .source_path = root / "Shaders/Default.frag.hlsl",
        },
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertex_buffer_desc,
            .num_vertex_buffers = std::size(vertex_buffer_desc),
            .vertex_attributes = vertex_attributes,
            .num_vertex_attributes = std::size(vertex_attributes),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST, // 선 리스트 사용
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
        },
        .multisample_state = {},
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test = true,
            .enable_depth_write = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = color_target_desc,
            .num_color_targets = std::size(color_target_desc),
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            .has_depth_stencil_target = true,
        },
    });

    // 뎁스 텍스처 생성
    constexpr SDL_GPUTextureCreateInfo texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    depth_texture = SDL_CreateGPUTexture(gpu_device, &texture_info);

    // 단위 큐브(0~1) 정점 데이터 (선 렌더링용)
    Vertex unit_cube_vertices[] = {
        {{0,0,0}}, {{1,0,0}}, {{1,1,0}}, {{0,1,0}},
        {{0,0,1}}, {{1,0,1}}, {{1,1,1}}, {{0,1,1}}
    };
    uint32 unit_cube_indices[] = {
        0,1, 1,2, 2,3, 3,0, // Bottom
        4,5, 5,6, 6,7, 7,4, // Top
        0,4, 1,5, 2,6, 3,7  // Vertical
    };

    // 버퍼 생성 및 데이터 업로드
    SDL_GPUBufferCreateInfo vbuf_info = { .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(unit_cube_vertices) };
    SDL_GPUBufferCreateInfo ibuf_info = { .usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = sizeof(unit_cube_indices) };
    debug_unit_cube_vbuf = SDL_CreateGPUBuffer(gpu_device, &vbuf_info);
    debug_unit_cube_ibuf = SDL_CreateGPUBuffer(gpu_device, &ibuf_info);

    SDL_GPUTransferBufferCreateInfo transfer_info = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(unit_cube_vertices) + sizeof(unit_cube_indices) };
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    
    void* map = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);
    SDL_memcpy(map, unit_cube_vertices, sizeof(unit_cube_vertices));
    SDL_memcpy((uint8*)map + sizeof(unit_cube_vertices), unit_cube_indices, sizeof(unit_cube_indices));
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src_v = { .transfer_buffer = transfer_buffer, .offset = 0 };
    SDL_GPUBufferRegion dst_v = { .buffer = debug_unit_cube_vbuf, .offset = 0, .size = sizeof(unit_cube_vertices) };
    SDL_UploadToGPUBuffer(copy_pass, &src_v, &dst_v, false);

    SDL_GPUTransferBufferLocation src_i = { .transfer_buffer = transfer_buffer, .offset = sizeof(unit_cube_vertices) };
    SDL_GPUBufferRegion dst_i = { .buffer = debug_unit_cube_ibuf, .offset = 0, .size = sizeof(unit_cube_indices) };
    SDL_UploadToGPUBuffer(copy_pass, &src_i, &dst_i, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);
}

void App::Run()
{
    is_running = true;

    double performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    if (performance_frequency <= 0.0)
    {
        performance_frequency = 1000.0;
    }

    CurrentTime = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;

    while (is_running && !quit_requested)
    {
        {
            ZoneScopedN("FrameLoop");

            const double frame_start = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;

            // Calculate Delta Time
            LastTime = CurrentTime;
            CurrentTime = frame_start;
            DeltaTime = CurrentTime - LastTime;
            TotalElapsedTime += static_cast<uint64>(DeltaTime * 1000.0);

            ProcessPlatformEvents();

            Update(static_cast<float>(DeltaTime));

            Render();
        }

        {
            ZoneScopedN("FrameSleep");

            double frame_duration;
            do
            {
                SDL_Delay(0);
                const double frame_end = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;
                frame_duration = frame_end - CurrentTime;
            } while (frame_duration < TargetFrameTime);
        }
        FrameMark;
    }
}

void App::Release()
{
    ZoneScoped;

    SDL_WaitForGPUIdle(gpu_device);

    gpu_resource_manager.reset();
    pso_manager.reset();

    // ImGui Release
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // Windows Release
    for (SDL_Window* window : windows | std::views::reverse | std::views::values)
    {
        SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
        SDL_DestroyWindow(window);
    }
    windows.clear();

    SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);

    // GPU Device Release
    SDL_DestroyGPUDevice(gpu_device);
    gpu_device = nullptr;

    asset_importer.reset();

    SDL_ShaderCross_Quit();
    SDL_Quit();
}

void App::ProcessPlatformEvents()
{
    ZoneScoped;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        {
            RequestQuit();
            break;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            if (event.window.windowID == main_window_id)
            {
                RequestQuit();
                break;
            }
            DestroyWindow(event.window.windowID);
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        {
            if (SDL_Window* window = SDL_GetWindowFromID(event.window.windowID))
            {
                focused_window = window;
            }
            break;
        }
        default:
            break;
        }
    }
}

void App::Update(float delta_time)
{
    ZoneScoped;

    static int32 selected_entity = -1;
    static int32 selected_component = 0;

    // Camera Input
    float x_delta, y_delta;
    SDL_MouseButtonFlags m_buttons = SDL_GetRelativeMouseState(&x_delta, &y_delta);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Picking logic
    if (!ImGui::GetIO().WantCaptureMouse && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)))
    {
        float mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);

        int w, h;
        SDL_GetWindowSize(GetMainWindow(), &w, &h);

        Matrix4x4 view_mat = math::TransformUtility::MakeViewMatrix(
            my_camera.position, my_camera.position + my_camera.rotation.GetForwardVector(), Vector3::UnitZ()
        );
        Matrix4x4 projection_mat = math::TransformUtility::MakePerspectiveMatrix(
            Radian{ my_camera.fov },
            static_cast<double>(w) / h,
            0.1, 10000.0
        );

        se::Ray ray = ScreenToWorldRay(mouse_x, mouse_y, static_cast<float>(w), static_cast<float>(h), view_mat, projection_mat);

        double closest_dist = std::numeric_limits<double>::max();
        Entity closest_entity = Entity{};

        for (auto [entity, transform, mesh_comp] : world.QueryEntities<Entity, const TransformComponent&, const MeshComponent&>())
        {
            if (!mesh_comp.mesh || !mesh_comp.mesh->mesh_data) continue;

            Matrix4x4 model = math::TransformUtility::MakeModelMatrix(
                transform.position, transform.rotation, transform.scale
            );
            Matrix4x4 inv_model = model.Inverse();

            // Transform Ray to local space
            Vector4 local_origin_v4 = Vector4(ray.origin.x, ray.origin.y, ray.origin.z, 1.0f) * inv_model;
            Vector3 local_origin = Vector3(local_origin_v4.x, local_origin_v4.y, local_origin_v4.z) / local_origin_v4.w;

            Vector4 local_dir_v4 = Vector4(ray.direction.x, ray.direction.y, ray.direction.z, 0.0f) * inv_model;
            Vector3 local_dir = Vector3(local_dir_v4.x, local_dir_v4.y, local_dir_v4.z).GetNormalized();

            se::Ray local_ray(local_origin, local_dir);

            const AABBf& mesh_bounds = mesh_comp.mesh->mesh_data->bounds;
            const AABB bounds_d(
                Vector3(mesh_bounds.min.x, mesh_bounds.min.y, mesh_bounds.min.z),
                Vector3(mesh_bounds.max.x, mesh_bounds.max.y, mesh_bounds.max.z)
            );

            double dist;
            if (local_ray.Intersects(bounds_d, dist))
            {
                // To be precise, calculate world space distance
                Vector3 hit_local = local_ray.GetPoint(dist);
                Vector4 hit_world_v4 = Vector4(hit_local.x, hit_local.y, hit_local.z, 1.0f) * model;
                Vector3 hit_world = Vector3(hit_world_v4.x, hit_world_v4.y, hit_world_v4.z) / hit_world_v4.w;

                double world_dist = (hit_world - ray.origin).Length();

                if (world_dist < closest_dist)
                {
                    closest_dist = world_dist;
                    closest_entity = entity;
                }
            }
        }

        if (closest_entity.IsValid())
        {
            Array<Entity> entities = world.GetAliveEntities();
            for (int i = 0; i < static_cast<int>(entities.Len()); ++i)
            {
                if (entities[i] == closest_entity)
                {
                    selected_entity = i;
                    break;
                }
            }
        }
    }

    if (m_buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))
    {
        SDL_SetWindowRelativeMouseMode(focused_window, true);

        {
            const Quaternion yaw_q = Quaternion::FromAxisAngle(Vector3::UnitZ(), math::DegToRad(-x_delta * my_camera.sensitivity));
            const Quaternion pitch_q = Quaternion::FromAxisAngle(
                my_camera.rotation.GetRightVector(),
                math::DegToRad(-y_delta * my_camera.sensitivity)
            );
            my_camera.rotation = yaw_q * pitch_q * my_camera.rotation;
        }

        if (keys[SDL_SCANCODE_W])
        {
            my_camera.position += my_camera.rotation.GetForwardVector() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_S])
        {
            my_camera.position -= my_camera.rotation.GetForwardVector() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_D])
        {
            my_camera.position += my_camera.rotation.GetRightVector() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_A])
        {
            my_camera.position -= my_camera.rotation.GetRightVector() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_E])
        {
            my_camera.position += Vector3::UnitZ() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_Q])
        {
            my_camera.position -= Vector3::UnitZ() * my_camera.camera_speed * delta_time;
        }
    }
    else
    {
        SDL_SetWindowRelativeMouseMode(focused_window, false);
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Begin("Import Asset");
    {
        if (ImGui::Button("Load Mesh"))
        {
            FileDialog::OpenFile([this](const Path& path)
            {
                auto assets = asset_importer->Import(path);
                if (assets.HasError())
                {
                    return;
                }

                for (const auto& asset : *assets)
                {
                    if (auto mesh = std::dynamic_pointer_cast<asset::StaticMesh>(asset))
                    {
                        auto loaded_mesh = std::make_shared<LoadedMesh>();
                        loaded_mesh->id = asset::AssetId(Guid::NewGuid());
                        loaded_mesh->name = path.FileName().ValueOr("Unknown"); // Use filename as name
                        loaded_mesh->mesh_data = mesh;

                        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
                        if (gpu_resource_manager->UploadMesh(
                            cmd, loaded_mesh->id,
                            mesh->vertices.Data(), static_cast<uint32>(mesh->vertices.Len() * sizeof(Vertex)),
                            mesh->indices.Data(), static_cast<uint32>(mesh->indices.Len() * sizeof(uint32))
                        ))
                        {
                            loaded_meshes.Push(loaded_mesh);

                            // Automatically spawn an entity with this mesh
                            world.SpawnEntity()
                                 .AddComponent<TransformComponent>()
                                 .AddComponent<MeshComponent>(loaded_mesh);

                            SDL_SubmitGPUCommandBuffer(cmd);
                        }
                        else
                        {
                             // Failed to upload
                            SDL_CancelGPUCommandBuffer(cmd);
                        }
                    }
                }
            });
        }
    }
    ImGui::End();

    ImGui::Begin("Window Pannal");
    {
        static FixedArray<char, 256> window_title = {};
        static int32 window_x = 100;
        static int32 window_y = 100;
        static int32 window_width = 1280;
        static int32 window_height = 720;

        ImGui::InputText(
            "Window Title",
            window_title.Data(),
            window_title.Len(),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        ImGui::InputInt("Window X", &window_x);
        ImGui::InputInt("Window Y", &window_y);
        ImGui::InputInt("Window Width", &window_width);
        ImGui::InputInt("Window Height", &window_height);

        if (ImGui::Button("New Window"))
        {
            CreateWindow(
                window_title.Data(),
                window_x,
                window_y,
                window_width,
                window_height,
                SDL_WINDOW_RESIZABLE
            );
        }
    }
    ImGui::End();

    ImGui::Begin("World");
    {
        ImGui::Text("FPS: %.3f", ImGui::GetIO().Framerate);
        ImGui::Text("FPS: %.3f", 1 / delta_time);

        static Array component_names {
             "TransformComponent", "MeshComponent"
        };
        Array<Entity> entities = world.GetAliveEntities();

        ImGui::SeparatorText("Entity Pannal");
        static int count = 0;
        ImGui::InputInt("##Count", &count);
        ImGui::SameLine();
        if (ImGui::Button("Create Entities"))
        {
            for (int i = 0; i < count; ++i)
            {
                world.SpawnEntity()
                     .AddComponent<TransformComponent>();
            }
        }
        if (ImGui::Button("Create Entity"))
        {
            world.SpawnEntity()
                 .AddComponent<TransformComponent>();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Entity") || keys[SDL_SCANCODE_DELETE])
        {
            if (selected_entity >= 0 && selected_entity < entities.Len())
            {
                world.DestroyEntity(entities[selected_entity]);
                selected_entity = -1;
            }
        }

        ImGui::Combo("##AddComponentCombo", &selected_component, component_names.Data(), static_cast<int>(component_names.Len()));
        ImGui::SameLine();
        if (ImGui::Button("Add Component"))
        {
            if (selected_entity >= 0 && selected_entity < entities.Len())
            {
                if (selected_component == 0)
                {
                    world.AddComponent<TransformComponent>(entities[selected_entity]);
                }
                else if (selected_component == 1)
                {
                    // Use the first loaded mesh if available
                    std::shared_ptr<LoadedMesh> default_mesh = loaded_meshes.IsEmpty() ? nullptr : loaded_meshes[0];
                    world.AddComponent<MeshComponent>(entities[selected_entity], default_mesh);
                }
            }
        }

        Array<String> entity_names;
        Array<const char*> temp_entity_names;

        entity_names.Reserve(entities.Len());
        temp_entity_names.Reserve(entity_names.Len());
        std::ranges::for_each(entities, [&entity_names, &temp_entity_names](Entity entity)
        {
            String name = String::Format("Entity {}, Gen: {}", entity.GetId(), entity.GetGeneration());
            temp_entity_names.Push(name.CStr());
            entity_names.Push(std::move(name));
        });

        ImGui::SeparatorText("Entity List");
        ImGui::Text("Entity Count: %d", static_cast<int>(entities.Len()));
        ImGui::ListBox(
            "##EntityList",
            &selected_entity,
            temp_entity_names.Data(),
            static_cast<int>(entity_names.Len()),
            10
        );

        ImGui::SeparatorText("Entity Property");
        if (selected_entity >= 0 && selected_entity < entities.Len())
        {
            const Entity entity = entities[selected_entity];
            ImGui::TextUnformatted(std::format("Selected Entity ID: {}", entity.GetId()).c_str());

            if (Optional<TransformComponent&> transform_comp_opt = world.TryGetComponent<TransformComponent>(entity))
            {
                auto& [quat, position, scale] = transform_comp_opt.Value();
                ImGui::DragScalarN("Position", ImGuiDataType_Double, &position.x, 3, 1.0f);

                const Rotator old_rotator = quat.ToRotator();

                Degree<double> refl[] = { old_rotator.pitch, old_rotator.roll, old_rotator.yaw };
                ImGui::DragScalarN("Rotation", ImGuiDataType_Double, &refl[0].value, 3, 1.0f);

                static bool local_rotation = false;
                ImGui::Checkbox("Local Rotation", &local_rotation);

                {
                    Vector3 axis_x, axis_y, axis_z;
                    if (local_rotation)
                    {
                        axis_x = quat.GetRightVector();
                        axis_y = quat.GetForwardVector();
                        axis_z = quat.GetUpVector();
                    }
                    else
                    {
                        axis_x = Vector3::UnitX();
                        axis_y = Vector3::UnitY();
                        axis_z = Vector3::UnitZ();
                    }
                    const Quaternion pitch_q = Quaternion::FromAxisAngle(axis_x, Radian{ refl[0] - old_rotator.pitch });
                    const Quaternion roll_q = Quaternion::FromAxisAngle(axis_y, Radian{ refl[1] - old_rotator.roll });
                    const Quaternion yaw_q = Quaternion::FromAxisAngle(axis_z, Radian{ refl[2] - old_rotator.yaw });

                    quat = (pitch_q * roll_q * yaw_q * quat).GetNormalized();
                }

                constexpr double min_value = 0.0;
                ImGui::DragScalarN("Scale", ImGuiDataType_Double, &scale.x, 3, 1.0f, &min_value);
            }

            if (Optional<MeshComponent&> mesh_comp_opt = world.TryGetComponent<MeshComponent>(entity))
            {
                MeshComponent& mesh_comp = mesh_comp_opt.Value();
                if (mesh_comp.mesh)
                {
                    ImGui::Text("Mesh: %s", mesh_comp.mesh->name.CStr());
                }
                else
                {
                    ImGui::Text("Mesh: None");
                }
            }
        }
        else
        {
            ImGui::Text("No Selected Entity");
        }
    }
    ImGui::End();

    ImGui::Begin("Camera");
    {
        if (ImGui::Button("Reset Camera"))
        {
            my_camera.position = Vector3::Zero();
            my_camera.rotation = Quaternion::Identity();
        }
        {
            constexpr double min_value = -1000.0, max_value = 1000.0;
            ImGui::DragScalarN("Position", ImGuiDataType_Double, &my_camera.position.x, 3, 1.0f, &min_value, &max_value);
        }

        {
            Rotator rotator = my_camera.rotation.ToRotator();
            auto& [pitch, yaw, roll] = rotator;
            Degree<double> refl[] = { pitch, roll, yaw };

            constexpr double min_value = -180.0, max_value = 180.0;
            ImGui::DragScalarN("Rotation", ImGuiDataType_Double, &refl[0].value, 3, 1.0f, &min_value, &max_value);
            my_camera.rotation = Rotator{ refl[0], refl[2], refl[1] }.ToQuaternion();
        }

        {
            constexpr double min_value = 0.001, max_value = 1000.0;
            ImGui::DragScalarN("Camera Speed", ImGuiDataType_Double, &my_camera.camera_speed, 1, 1.0f, &min_value, &max_value);
        }
        {
            constexpr double min_value = 0.001, max_value = 10.0;
            ImGui::SliderScalarN("Sensitivity", ImGuiDataType_Double, &my_camera.sensitivity, 1, &min_value, &max_value);
        }
    }
    ImGui::End();
}

void App::Render() const
{
    ZoneScoped;

    for (const auto& [window_id, window] : windows)
    {
        // Command Buffer 가져오기
        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);

        // Swapchain Texture 가져오기 (화면에 그릴 캔버스 역할)
        SDL_GPUTexture* swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr);

        // Rendering
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

        if (swapchain_texture && !is_minimized)
        {
            if (window_id == main_window_id)
            {
                ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
            }

            constexpr SDL_FColor clear_color = { 0.25f, 0.25f, 0.25f, 1.0f };

            SDL_GPUColorTargetInfo target_info = {};
            target_info.texture = swapchain_texture;
            target_info.clear_color = clear_color;
            target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            target_info.store_op = SDL_GPU_STOREOP_STORE;
            target_info.mip_level = 0;
            target_info.layer_or_depth_plane = 0;
            target_info.cycle = false;

            SDL_GPUDepthStencilTargetInfo depth_stencil_target_info = {
                .texture = depth_texture,
                .clear_depth = 1.0f,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
                .cycle = false,
            };

            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, &depth_stencil_target_info);
            {
                SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

                Matrix4x4 vp_mat;
                {
                    Matrix4x4 view_mat = math::TransformUtility::MakeViewMatrix(
                        my_camera.position, my_camera.position + my_camera.rotation.GetForwardVector(), Vector3::UnitZ()
                    );
                    Matrix4x4 projection_mat = math::TransformUtility::MakePerspectiveMatrix(
                        Radian{ my_camera.fov },
                        static_cast<double>(draw_data->DisplaySize.x / draw_data->DisplaySize.y),
                        0.1, 10000.0
                    );

                    vp_mat = view_mat * projection_mat;
                }

                auto render_mesh = [&](const Matrix4x4& model, const std::shared_ptr<LoadedMesh>& mesh)
                {
                    if (!mesh || !mesh->mesh_data) return;

                    const GpuBufferSlice& slice = gpu_resource_manager->GetSlice(mesh->id);
                    if (!slice.IsValid()) return;

                    Matrix4x4 mvp = model * vp_mat;
                    Matrix4x4f mvpf;
                    for (int i = 0; i < 16; ++i)
                    {
                        const double* value = mvp.GetData() + i;
                        *(mvpf.GetData() + i) = static_cast<float>(*value);
                    }

                    SDL_PushGPUVertexUniformData(command_buffer, 0, &mvpf, sizeof(mvpf));

                    // Vertex Buffer 바인딩
                    const SDL_GPUBufferBinding vertex_binding = {
                        .buffer = slice.buffer,
                        .offset = slice.offset
                    };
                    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

                    // Index Buffer 바인딩
                    const SDL_GPUBufferBinding index_binding = {
                        .buffer = slice.buffer,
                        .offset = slice.index_offset
                    };
                    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                    // Draw Sections
                    for (const auto& section : mesh->mesh_data->sections)
                    {
                        SDL_DrawGPUIndexedPrimitives(render_pass, section.index_count, 1, section.index_start, 0, 0);
                    }
                };

                // 임시 코드
                static bool is_first = true;
                if (is_first)
                {
                    world.AddSystem<UpdatePhase>([&](Query<const TransformComponent&, const MeshComponent&> query)
                    {
                        for (const auto& [transform_comp, mesh_comp] : query)
                        {
                            Matrix4x4 model = math::TransformUtility::MakeModelMatrix(
                                transform_comp.position, transform_comp.rotation, transform_comp.scale
                            );
                            render_mesh(model, mesh_comp.mesh);
                        }
                    });
                    is_first = false;
                }

                // 임시 코드
                world.RunPhase<UpdatePhase>();

                // --- AABB 직접 렌더링 구현 ---
                SDL_BindGPUGraphicsPipeline(render_pass, line_pipeline);
                
                const SDL_GPUBufferBinding v_binding = { .buffer = debug_unit_cube_vbuf, .offset = 0 };
                SDL_BindGPUVertexBuffers(render_pass, 0, &v_binding, 1);
                const SDL_GPUBufferBinding i_binding = { .buffer = debug_unit_cube_ibuf, .offset = 0 };
                SDL_BindGPUIndexBuffer(render_pass, &i_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                for (auto [transform, mesh_comp] : world.QueryEntities<const TransformComponent&, const MeshComponent&>())
                {
                    if (!mesh_comp.mesh || !mesh_comp.mesh->mesh_data) continue;

                    const AABBf& bounds = mesh_comp.mesh->mesh_data->bounds;
                    
                    // AABB를 위한 로컬 변환 행렬 (Unit Cube [0,1] -> AABB [min, max])
                    Vector3f size = bounds.GetSize();
                    Vector3f min = bounds.min;
                    
                    // Scale * Translation
                    Matrix4x4 aabb_local = math::TransformUtility::MakeFromScale(Vector3(size.x, size.y, size.z)) * 
                                          math::TransformUtility::MakeFromTranslation(Vector3(min.x, min.y, min.z));

                    // Entity Model Matrix 적용
                    Matrix4x4 model = math::TransformUtility::MakeModelMatrix(
                        transform.position, transform.rotation, transform.scale
                    );
                    
                    Matrix4x4 mvp = aabb_local * model * vp_mat;
                    Matrix4x4f mvpf;
                    for (int i = 0; i < 16; ++i) mvpf.GetData()[i] = (float)mvp.GetData()[i];

                    SDL_PushGPUVertexUniformData(command_buffer, 0, &mvpf, sizeof(mvpf));
                    SDL_DrawGPUIndexedPrimitives(render_pass, 24, 1, 0, 0, 0);
                }

                // Render ImGui
                if (window_id == main_window_id)
                {
                    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
                }
            }
            SDL_EndGPURenderPass(render_pass);
        }

        // Command Buffer 제출
        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    // Update and Render additional Platform Windows
    const ImGuiIO& IO = ImGui::GetIO();
    if (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    pso_manager->EndFrame();
}

SDL_WindowID App::CreateWindow(const char* title, int32 x, int32 y, int32 width, int32 height, uint32 flags)
{
    SDL_Window* window = SDL_CreateWindow(title, width, height, flags);
    SDL_SetWindowPosition(window, x, y);

    SDL_WindowID window_id = SDL_GetWindowID(window);
    windows.insert({ window_id, window });

    SDL_ClaimWindowForGPUDevice(gpu_device, window);
    SDL_SetGPUSwapchainParameters(
        gpu_device,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_MAILBOX
    );

    return window_id;
}

void App::DestroyWindow(SDL_WindowID window_id)
{
    SDL_Window* window = GetWindow(window_id);
    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyWindow(window);
    windows.erase(window_id);
}

void App::DestroyWindow(SDL_Window* window)
{
    DestroyWindow(SDL_GetWindowID(window));
}
