#include "App.h"

#include <cassert>
#include <filesystem>
#include <format>
#include <ranges>

#include "SimpleEngine/Core/Math/Math.h"
#include "SimpleEngine/Core/Memory/MemoryResource/OsMemoryResource.h"
#include "SimpleEngine/Geometry/Circle.h"
#include "SimpleEngine/Geometry/Corn.h"
#include "SimpleEngine/Geometry/Cube.h"
#include "SimpleEngine/Geometry/Cylinder.h"
#include "SimpleEngine/Geometry/Plane.h"
#include "SimpleEngine/Geometry/Torus.h"
#include "SimpleEngine/Geometry/Vertex.h"
#include "SimpleEngine/Rendering/Manager/PSOManager.h"
#include "SimpleEngine/World/Components/TransformComponent.h"
#include "SimpleEngine/World/Query.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "Rendering/Compiler/Provider.h"
#include "SDL3/SDL.h"
#include "SDL3_shadercross/SDL_shadercross.h"
#include "tracy/Tracy.hpp"

#pragma warning(disable: 4996) // deprecated warning

using namespace se::core;
using namespace se::world;
using namespace se::rendering;

double App::CurrentTime = 0.0;
double App::LastTime = 0.0;
double App::DeltaTime = 1.0 / 60.0;
double App::FixedDeltaTime = 1.0 / 60.0;
uint64 App::TotalElapsedTime = 0;

uint32 App::TargetFps = 24000;
double App::TargetFrameTime = 1.0 / static_cast<double>(TargetFps);

App* App::Instance = nullptr;

static se::core::memory::memory_resource::OsMemoryResource os_memory_resource;
static std::pmr::memory_resource* original_resource = nullptr;


struct Camera
{
    Vector3 position = Vector3{ 0, -8, 6 };
    Quaternion rotation = Quaternion::Identity();

    double camera_speed = 10.0f;
    double sensitivity = 0.2f;
    Degree<double> fov = 90_deg;
};

enum class MeshTypes : uint8
{
    Circle,
    Corn,
    Cube,
    Cylinder,
    Plane,
    Torus,
};

struct MeshComponent
{
    MeshTypes type = MeshTypes::Cube;
};

struct MeshInfo
{
    uint32 index_count = 0;
    uint32 base_vertex = 0;
    uint32 index_buffer_offset = 0;
};

static std::map<MeshTypes, MeshInfo> mesh_infos;

static Camera my_camera;


App::App()
{
    assert(!Instance);
    Instance = this;

    original_resource = std::pmr::set_default_resource(&os_memory_resource);
}

App::~App()
{
    std::pmr::set_default_resource(original_resource);

    Instance = nullptr;
}

void App::Initialize()
{
    ZoneScoped;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
    SDL_ShaderCross_Init();

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
    pso_manager->SetShaderCacheProvider<se::editor::rendering::CompilingShaderProvider>();

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
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, // POSITION
            .offset = offsetof(Vertex, position)
        },
        {
            .location = 1, // COLOR0
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, // COLOR0
            .offset = offsetof(Vertex, color)
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

    // 모든 메시 데이터를 하나의 버퍼에 업로드
    constexpr size_t total_vertex_size = sizeof(circle_vertices) + sizeof(corn_vertices) + sizeof(cube_vertices) + sizeof(cylinder_vertices) + sizeof(
        plane_vertices) + sizeof(torus_vertices);
    constexpr size_t total_index_size = sizeof(circle_indices) + sizeof(corn_indices) + sizeof(cube_indices) + sizeof(cylinder_indices) + sizeof(
        plane_indices) + sizeof(torus_indices);

    // 버텍스 버퍼
    SDL_GPUBufferCreateInfo vertex_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = total_vertex_size
    };
    vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &vertex_buffer_info);

    // 인덱스 버퍼
    SDL_GPUBufferCreateInfo index_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = total_index_size
    };
    index_buffer = SDL_CreateGPUBuffer(gpu_device, &index_buffer_info);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = total_vertex_size + total_index_size
    };
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    void* transfer_data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);

    // 데이터 복사 및 정보 저장
    uint8* vertex_dst = static_cast<uint8*>(transfer_data);
    uint8* index_dst = vertex_dst + total_vertex_size;
    uint32 current_vertex_offset = 0;
    uint32 current_index_offset = 0;
    uint32 base_vertex = 0;

    auto upload_mesh = [&](MeshTypes type, const auto& vertices, const auto& indices)
    {
        const size_t vertex_size = sizeof(vertices);
        const size_t index_size = sizeof(indices);

        std::memcpy(vertex_dst + current_vertex_offset, vertices, vertex_size);
        std::memcpy(index_dst + current_index_offset, indices, index_size);

        mesh_infos[type] = {
            .index_count = static_cast<uint32>(std::size(indices)),
            .base_vertex = base_vertex,
            .index_buffer_offset = current_index_offset
        };

        current_vertex_offset += vertex_size;
        current_index_offset += index_size;
        base_vertex += static_cast<uint32>(std::size(vertices));
    };

    upload_mesh(MeshTypes::Circle, circle_vertices, circle_indices);
    upload_mesh(MeshTypes::Corn, corn_vertices, corn_indices);
    upload_mesh(MeshTypes::Cube, cube_vertices, cube_indices);
    upload_mesh(MeshTypes::Cylinder, cylinder_vertices, cylinder_indices);
    upload_mesh(MeshTypes::Plane, plane_vertices, plane_indices);
    upload_mesh(MeshTypes::Torus, torus_vertices, torus_indices);

    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    SDL_GPUCommandBuffer* upload_cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmd);
    {
        SDL_GPUTransferBufferLocation vertex_location = { .transfer_buffer = transfer_buffer, .offset = 0 };
        SDL_GPUBufferRegion vertex_region = { .buffer = vertex_buffer, .offset = 0, .size = total_vertex_size };
        SDL_UploadToGPUBuffer(copy_pass, &vertex_location, &vertex_region, false);

        SDL_GPUTransferBufferLocation index_location = { .transfer_buffer = transfer_buffer, .offset = total_vertex_size };
        SDL_GPUBufferRegion index_region = { .buffer = index_buffer, .offset = 0, .size = total_index_size };
        SDL_UploadToGPUBuffer(copy_pass, &index_location, &index_region, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmd);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);

    world.SpawnEntity()
         .AddComponent<TransformComponent>()
         .AddComponent<MeshComponent>(); // 기본으로 MeshComponent도 추가
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

    SDL_ReleaseGPUBuffer(gpu_device, vertex_buffer);
    SDL_ReleaseGPUBuffer(gpu_device, index_buffer);
    SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);

    // GPU Device Release
    SDL_DestroyGPUDevice(gpu_device);
    gpu_device = nullptr;

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
        default:
            break;
        }
    }
}

void App::Update(float delta_time)
{
    ZoneScoped;

    // Camera Input
    float x_delta, y_delta;
    SDL_MouseButtonFlags m_buttons = SDL_GetRelativeMouseState(&x_delta, &y_delta);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    if (m_buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))
    {
        {
            using namespace se::math;

            const Quaternion yaw_q = Quaternion::FromAxisAngle(Vector3::UnitZ(), MathUtility::DegreesToRadians(-x_delta * my_camera.sensitivity));
            const Quaternion pitch_q = Quaternion::FromAxisAngle(
                my_camera.rotation.GetRightVector(),
                MathUtility::DegreesToRadians(-y_delta * my_camera.sensitivity)
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

    // Start the Dear ImGui frame
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Begin("Window Pannal");
    {
        static std::array<char, 256> window_title = {};
        static int32 window_x = 100;
        static int32 window_y = 100;
        static int32 window_width = 1280;
        static int32 window_height = 720;

        ImGui::InputText(
            "Window Title",
            window_title.data(),
            window_title.size(),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        ImGui::InputInt("Window X", &window_x);
        ImGui::InputInt("Window Y", &window_y);
        ImGui::InputInt("Window Width", &window_width);
        ImGui::InputInt("Window Height", &window_height);

        if (ImGui::Button("New Window"))
        {
            CreateWindow(
                window_title.data(),
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

        static int32 selected_entity = -1;
        static int32 selected_component = 0;
        static se::vector<const char*> component_names
        {
            { "TransformComponent", "MeshComponent" }, std::pmr::get_default_resource()
        };
        se::vector<se::world::Entity> entities = world.GetAliveEntities();

        ImGui::SeparatorText("Entity Pannal");
        static int count = 0;
        ImGui::InputInt("##Count", &count);
        ImGui::SameLine();
        if (ImGui::Button("Create Entities"))
        {
            for (int i = 0; i < count; ++i)
            {
                world.SpawnEntity()
                     .AddComponent<TransformComponent>()
                     .AddComponent<MeshComponent>();
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
            if (selected_entity >= 0 && selected_entity < entities.size())
            {
                world.DestroyEntity(entities[selected_entity]);
                selected_entity = -1;
            }
        }

        ImGui::Combo("##AddComponentCombo", &selected_component, component_names.data(), static_cast<int>(component_names.size()));
        ImGui::SameLine();
        if (ImGui::Button("Add Component"))
        {
            if (selected_entity >= 0 && selected_entity < entities.size())
            {
                if (selected_component == 0)
                {
                    world.AddComponent<TransformComponent>(entities[selected_entity]);
                }
                else if (selected_component == 1)
                {
                    world.AddComponent<MeshComponent>(entities[selected_entity]);
                }
            }
        }

        //se::vector<std::string> entity_names;
        //entity_names.reserve(entities.size());
        //std::ranges::for_each(entities, [&entity_names](se::core::ecs::Entity entity)
        //{
        //    entity_names.push_back(std::format("Entity {}, Gen: {}", entity.GetId(), entity.GetGeneration()));
        //});

        //se::vector<const char*> temp_entity_names;
        //temp_entity_names.reserve(entity_names.size());
        //std::ranges::for_each(entity_names, [&temp_entity_names](const std::string& name)
        //{
        //    temp_entity_names.push_back(name.c_str());
        //});

        ImGui::SeparatorText("Entity List");
        ImGui::Text("Entity Count: %d", static_cast<int>(entities.size()));
        //ImGui::ListBox(
        //    "##EntityList",
        //    &selected_entity,
        //    temp_entity_names.data(),
        //    static_cast<int>(entity_names.size()),
        //    10
        //);

        ImGui::SeparatorText("Entity Property");
        if (selected_entity >= 0 && selected_entity < entities.size())
        {
            const se::world::Entity entity = entities[selected_entity];
            ImGui::Text(std::format("Selected Entity ID: {}", entity.GetId()).c_str());

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
                    using namespace se::math;

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

                int idx = static_cast<int>(mesh_comp.type);
                const char* mesh_names[] = { "Circle", "Corn", "Cube", "Cylinder", "Plane", "Torus", };
                ImGui::Combo("Mesh", &idx, mesh_names, std::size(mesh_names));
                mesh_comp.type = static_cast<MeshTypes>(idx);
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
            ImGui::SliderScalarN("Sensitivity", ImGuiDataType_Double, &my_camera.sensitivity, 3, &min_value, &max_value);
        }
        {
            constexpr double min_value = 0.0, max_value = 180.0;
            ImGui::SliderScalarN("FOV", ImGuiDataType_Double, &my_camera.fov.value, 1, &min_value, &max_value);
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
        // SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr);
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
                auto render_primitive = [&](const Matrix4x4& mvp, MeshTypes mesh_type)
                {
                    Matrix4x4f mvpf;
                    for (int i = 0; i < 16; ++i)
                    {
                        const double* value = mvp.GetData() + i;
                        *(mvpf.GetData() + i) = static_cast<float>(*value);
                    }

                    SDL_PushGPUVertexUniformData(command_buffer, 0, &mvpf, sizeof(mvpf));

                    const auto& mesh_info = mesh_infos[mesh_type];

                    // Vertex Buffer 바인딩
                    const SDL_GPUBufferBinding vertex_binding = {
                        .buffer = vertex_buffer,
                        .offset = 0 // Vertex buffer offset is handled by base_vertex in DrawGPUIndexedPrimitives
                    };
                    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

                    // Index Buffer 바인딩
                    const SDL_GPUBufferBinding index_binding = {
                        .buffer = index_buffer,
                        .offset = mesh_info.index_buffer_offset
                    };
                    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

                    // 삼각형 그리기 (인덱스 사용하는 경우)
                    SDL_DrawGPUIndexedPrimitives(render_pass, mesh_info.index_count, 1, 0, static_cast<int32_t>(mesh_info.base_vertex), 0);
                };

                SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

                Matrix4x4 vp_mat;
                {
                    using namespace se::math;

                    Matrix4x4 view_mat = TransformUtility::MakeViewMatrix(
                        my_camera.position, my_camera.position + my_camera.rotation.GetForwardVector(), Vector3::UnitZ()
                    );
                    Matrix4x4 projection_mat = TransformUtility::MakePerspectiveMatrix(
                        Radian{ my_camera.fov },
                        static_cast<double>(draw_data->DisplaySize.x / draw_data->DisplaySize.y),
                        0.1, 10000.0
                    );

                    vp_mat = view_mat * projection_mat;
                }

                // 임시 코드
                static bool is_first = true;
                if (is_first)
                {
                    world.AddSystem<schedules::Update>([&](Query<const TransformComponent&, const MeshComponent&> query)
                    {
                        using namespace se::math;
                        for (const auto& [transform_comp, mesh_comp] : query)
                        {
                            Matrix4x4 model = TransformUtility::MakeModelMatrix(
                                transform_comp.position, transform_comp.rotation, transform_comp.scale
                            );
                            render_primitive(model * vp_mat, mesh_comp.type);
                        }
                    });
                    is_first = false;
                }

                // 임시 코드
                world.RunSchedule<schedules::Update>();

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
