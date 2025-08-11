module;
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
module Playground.App;
import SimpleEngine.Core;
import SimpleEngine.Utility;
import Playground.Utility;
import <imgui.h>;
import <imgui_impl_sdl3.h>;
import <imgui_impl_sdlgpu3.h>;
import <cassert>;


double App::CurrentTime = 0.0;
double App::LastTime = 0.0;
double App::DeltaTime = 1.0 / 60.0;
double App::FixedDeltaTime = 1.0 / 60.0;
uint64 App::TotalElapsedTime = 0;

uint32 App::TargetFps = 240;
double App::TargetFrameTime = 1.0 / static_cast<double>(TargetFps);

App* App::Instance = nullptr;


struct Vertex
{
    float position[3];  // POSITION 시맨틱
    float color[4];     // COLOR 시맨틱
};

// constexpr Vertex vertices[] = {
//     // 위쪽 - 빨간색
//     {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
//     // 왼쪽 아래 - 초록색
//     {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
//     // 오른쪽 아래 - 파란색
//     {{ 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
// };

// constexpr uint16 indices[] = {0, 1, 2};

constexpr Vertex vertices[] = {
    {{ -0.5f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
    {{ 0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }},
    {{ -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }},
    {{ 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
};

constexpr uint16 indices[] = { 0, 1, 2, 2, 1, 3 };


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

    // create shader
    const std::filesystem::path root = std::filesystem::current_path().parent_path();
    SDL_GPUShader* vertex_shader = playground::utility::shader_utils::CompileHLSL(
        gpu_device,
        root / "Shaders/Default.vert.hlsl",
        std::nullopt,
        std::nullopt,
        0,
        0,
        0,
        0
    );
    SDL_GPUShader* fragment_shader = playground::utility::shader_utils::CompileHLSL(
        gpu_device,
        root / "Shaders/Default.frag.hlsl",
        std::nullopt,
        std::nullopt,
        0,
        0,
        0,
        0
    );

    // 버텍스 입력 설정
    SDL_GPUVertexBufferDescription vertex_buffer_desc = {
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    };

    SDL_GPUVertexAttribute vertex_attributes[2] = {
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

    SDL_GPUColorTargetDescription color_target_desc = {
        .format = SDL_GetGPUSwapchainTextureFormat(gpu_device, window),
    };

    // 파이프라인 초기화
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = &vertex_buffer_desc,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertex_attributes,
            .num_vertex_attributes = 2
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .color_target_descriptions = &color_target_desc,
            .num_color_targets = 1,
        },
    };
    pipeline_info.rasterizer_state = {
        .fill_mode = SDL_GPU_FILLMODE_FILL,
        .cull_mode = SDL_GPU_CULLMODE_NONE,  // 양면 렌더링
        .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
    },

    pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &pipeline_info);

    SDL_ReleaseGPUShader(gpu_device, vertex_shader);
    SDL_ReleaseGPUShader(gpu_device, fragment_shader);

    // 버텍스 버퍼
    SDL_GPUBufferCreateInfo vertex_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(vertices)
    };
    vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &vertex_buffer_info);

    // 인덱스 버퍼
    SDL_GPUBufferCreateInfo index_buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(indices)
    };
    index_buffer = SDL_CreateGPUBuffer(gpu_device, &index_buffer_info);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(vertices) + sizeof(indices)
    };
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);

    void* transfer_data = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);
    std::memcpy(transfer_data, vertices, sizeof(vertices));
    std::memcpy(static_cast<uint8*>(transfer_data) + sizeof(vertices), indices, sizeof(indices));
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
            .size = sizeof(vertices)
        };

        SDL_UploadToGPUBuffer(copy_pass, &vertex_location, &vertex_region, false);

        SDL_GPUTransferBufferLocation index_location = {
            .transfer_buffer = transfer_buffer,
            .offset = sizeof(vertices)
        };

        SDL_GPUBufferRegion index_region = {
            .buffer = index_buffer,
            .offset = 0,
            .size = sizeof(indices)
        };

        SDL_UploadToGPUBuffer(copy_pass, &index_location, &index_region, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmd);
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
        const double frame_start = static_cast<double>(SDL_GetPerformanceCounter()) / performance_frequency;

        // Calculate Delta Time
        LastTime = CurrentTime;
        CurrentTime = frame_start;
        DeltaTime = CurrentTime - LastTime;
        TotalElapsedTime += static_cast<uint64>(DeltaTime * 1000.0);

        ProcessPlatformEvents();

        Update(static_cast<float>(DeltaTime));

        Render();


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
    SDL_WaitForGPUIdle(gpu_device);

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

    // GPU Device Release
    SDL_DestroyGPUDevice(gpu_device);
    gpu_device = nullptr;

    SDL_ShaderCross_Quit();
    SDL_Quit();
}

void App::ProcessPlatformEvents()
{
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
    // Start the Dear ImGui frame
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Begin("asdasd");
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

    ImGui::Begin("Test");
    {
        ImGui::Text("FPS: %.3f", ImGui::GetIO().Framerate);
        ImGui::Text("FPS: %.3f", 1 / delta_time);
    }
    ImGui::End();
}

void App::Render() const
{
    for (const auto& [window_id, window] : windows)
    {
        // Command Buffer 가져오기
        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);

        // Swapchain Texture 가져오기 (화면에 그릴 캔버스 역할)
        SDL_GPUTexture* swapchain_texture;
        // SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr);
        SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr);

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
                SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

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
                SDL_DrawGPUIndexedPrimitives(render_pass, std::size(indices), 1, 0, 0, 0);
                // 또는 인덱스 없이 그리기
                // SDL_DrawGPUPrimitives(render_pass, std::size(vertices), 1, 0, 0);

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
