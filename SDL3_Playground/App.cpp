module;
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <tracy/Tracy.hpp>
module Playground.App;

#pragma warning(disable: 4996) // deprecated warning


import SimpleEngine.Editor.Utility;
import SimpleEngine.Editor.Rendering;

import SimpleEngine.Prelude;
import SimpleEngine.Components;
import SimpleEngine.Geometry;

import <imgui.h>;
import <imgui_impl_sdl3.h>;
import <imgui_impl_sdlgpu3.h>;
import <cassert>;


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
    Vector3f position = Vector3f{ 0, -8, 6 };
    Quaternionf rotation = Quaternionf::Identity();

    float camera_speed = 10.0f;
    float sensitivity = 0.2f;
    Degree<float> fov = 90_degf;
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

static Camera my_camera;


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
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    // GPU Device 생성
    gpu_device = SDL_CreateGPUDeviceWithProperties(props);
    const char* val = SDL_GetError();
    SDL_DestroyProperties(props);


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

    pso_manager = std::make_unique<se::rendering::manager::PSOManager>(gpu_device);
    pso_manager->SetShaderCacheProvider<se::editor::rendering::shader_provider::CompilingShaderProvider>();

    // 셰이더 컴파일때 사용하는 솔루션 경로
    const std::filesystem::path root = std::filesystem::current_path().parent_path();

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
        .depth_stencil_state = {},
        .target_info = {
            .color_target_descriptions = color_target_desc,
            .num_color_targets = std::size(color_target_desc),
        },
    });

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
        .depth_stencil_state = {},
        .target_info = {
            .color_target_descriptions = color_target_desc,
            .num_color_targets = std::size(color_target_desc),
        },
    });
    if (!pipeline)
    {
        SDL_AssertBreakpoint();
    }

    // 버텍스 버퍼
    SDL_GPUBufferCreateInfo vertex_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(cube_vertices)
    };
    vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &vertex_buffer_info);

    // 인덱스 버퍼
    SDL_GPUBufferCreateInfo index_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(cube_indices)
    };
    index_buffer = SDL_CreateGPUBuffer(gpu_device, &index_buffer_info);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(cube_vertices) + sizeof(cube_indices)
        // .size = sizeof(circle_vertices) + sizeof(circle_indices)
        // + sizeof(corn_vertices) + sizeof(corn_indices)
        // + sizeof(cube_vertices) + sizeof(cube_indices)
        // + sizeof(cylinder_vertices) + sizeof(cylinder_indices)
        // + sizeof(plane_vertices) + sizeof(plane_indices)
        // + sizeof(torus_vertices) + sizeof(torus_indices)
    };
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);

    void* transfer_data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);
    std::memcpy(transfer_data, cube_vertices, sizeof(cube_vertices));
    std::memcpy(static_cast<uint8*>(transfer_data) + sizeof(cube_vertices), cube_indices, sizeof(cube_indices));
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    SDL_GPUCommandBuffer* upload_cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmd);
    {
        SDL_GPUTransferBufferLocation vertex_location = {
            .transfer_buffer = transfer_buffer,
            .offset = 0
        };

        SDL_GPUBufferRegion vertex_region = {
            .buffer = vertex_buffer,
            .offset = 0,
            .size = sizeof(cube_vertices)
        };

        SDL_UploadToGPUBuffer(copy_pass, &vertex_location, &vertex_region, false);

        SDL_GPUTransferBufferLocation index_location = {
            .transfer_buffer = transfer_buffer,
            .offset = sizeof(cube_vertices)
        };

        SDL_GPUBufferRegion index_region = {
            .buffer = index_buffer,
            .offset = 0,
            .size = sizeof(cube_indices)
        };

        SDL_UploadToGPUBuffer(copy_pass, &index_location, &index_region, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmd);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);

    world.CreateEntity()
         .AddComponent<TransformComponent>();
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
        ZoneScoped;
        const double frame_start = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;

        // Calculate Delta Time
        LastTime = CurrentTime;
        CurrentTime = frame_start;
        DeltaTime = CurrentTime - LastTime;
        TotalElapsedTime += static_cast<uint64>(DeltaTime * 1000.0);


        ProcessPlatformEvents();

        Update(static_cast<float>(DeltaTime));

        Render();


        FrameMark;

        double frame_duration;
        do
        {
            SDL_Delay(0);
            const double frame_end = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;
            frame_duration = frame_end - CurrentTime;
        }
        while (frame_duration < TargetFrameTime);
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

            const Quaternionf yaw_q = Quaternionf::FromAxisAngle(Vector3f::UnitZ(), MathUtility::DegreesToRadians(-x_delta * my_camera.sensitivity));
            const Quaternionf pitch_q = Quaternionf::FromAxisAngle(
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
            my_camera.position += Vector3f::UnitZ() * my_camera.camera_speed * delta_time;
        }
        if (keys[SDL_SCANCODE_Q])
        {
            my_camera.position -= Vector3f::UnitZ() * my_camera.camera_speed * delta_time;
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
        static std::vector component_names
        {
            "TransformComponent", "MeshComponent"
        };
        std::vector<se::core::ecs::Entity> entities = world.GetAliveEntities();

        ImGui::SeparatorText("Entity Pannal");
        static int count = 0;
        ImGui::InputInt("##Count", &count);
        ImGui::SameLine();
        if (ImGui::Button("Create Entities"))
        {
            for (int i = 0; i < count; ++i)
            {
                world.CreateEntity()
                     .AddComponent<TransformComponent>();
            }
        }
        if (ImGui::Button("Create Entity"))
        {
            world.CreateEntity()
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

        std::vector<std::string> entity_names;
        std::ranges::for_each(entities, [&entity_names](se::core::ecs::Entity entity)
        {
            entity_names.push_back(std::format("Entity {}, Gen: {}", entity.GetId(), entity.GetGeneration()));
        });

        std::vector<const char*> temp_entity_names;
        std::ranges::for_each(entity_names, [&temp_entity_names](const std::string& name)
        {
            temp_entity_names.push_back(name.c_str());
        });

        ImGui::SeparatorText("Entity List");
        ImGui::Text("Entity Count: %d", static_cast<int>(entities.size()));
        ImGui::ListBox(
            "##EntityList",
            &selected_entity,
            temp_entity_names.data(),
            static_cast<int>(entity_names.size()),
            10
        );

        ImGui::SeparatorText("Entity Property");
        if (selected_entity >= 0 && selected_entity < entities.size())
        {
            const se::core::ecs::Entity entity = entities[selected_entity];
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
            my_camera.position = Vector3f::Zero();
            my_camera.rotation = Quaternionf::Identity();
        }
        ImGui::DragFloat3("Position", &my_camera.position.x, 1.0f, -1000.0f, 1000.0f);

        Rotatorf rotator = my_camera.rotation.ToRotator();
        auto& [pitch, yaw, roll] = rotator;
        Degree<float> refl[] = { pitch, roll, yaw };

        ImGui::DragFloat3("Rotation", &refl[0].value, 1.0f, -180.0f, 180.0f);
        my_camera.rotation = Rotatorf{ refl[0], refl[2], refl[1] }.ToQuaternion();

        ImGui::SliderFloat("Camera Speed", &my_camera.camera_speed, 0.001f, 1000.0f);
        ImGui::SliderFloat("Sensitivity", &my_camera.sensitivity, 0.001f, 10.0f);
        ImGui::SliderFloat("FOV", &my_camera.fov.value, 0.0f, 180.0f);
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

            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);
            {
                auto render_primitive = [&](const Matrix4x4f& mvp)
                {
                    SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(mvp));

                    // Vertex Buffer 바인딩
                    const SDL_GPUBufferBinding vertex_binding = {
                        .buffer = vertex_buffer,
                        .offset = 0
                    };
                    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

                    // Index Buffer 바인딩
                    const SDL_GPUBufferBinding index_binding = {
                        .buffer = index_buffer,
                        .offset = 0
                    };
                    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

                    // 삼각형 그리기 (인덱스 사용하는 경우)
                    SDL_DrawGPUIndexedPrimitives(render_pass, std::size(cube_indices), 1, 0, 0, 0);
                    // 또는 인덱스 없이 그리기
                    // SDL_DrawGPUPrimitives(render_pass, std::size(cube_vertices), 1, 0, 0);
                };

                SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

                Matrix4x4f vp_mat;
                {
                    using namespace se::math;

                    Matrix4x4f view_mat = TransformUtility::MakeViewMatrix(
                        my_camera.position, my_camera.position + my_camera.rotation.GetForwardVector(), Vector3f::UnitZ()
                    );
                    Matrix4x4f projection_mat = TransformUtility::MakePerspectiveMatrix(
                        Radian{ my_camera.fov }, draw_data->DisplaySize.x / draw_data->DisplaySize.y, 0.1f, 10000.0f
                    );

                    vp_mat = view_mat * projection_mat;
                }

                world.Query<TransformComponent>().ForEach([&](se::core::ecs::Entity _, const TransformComponent& transform_comp)
                {
                    using namespace se::math;

                    Matrix4x4 modeld = TransformUtility::MakeModelMatrix(
                        transform_comp.position, transform_comp.rotation, transform_comp.scale
                    );

                    Matrix4x4f model;
                    for (int i = 0; i < 16; ++i)
                    {
                        const double* value = modeld.GetData() + i;
                        *(model.GetData() + i) = static_cast<float>(*value);
                    }

                    render_primitive(model * vp_mat);
                });

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
