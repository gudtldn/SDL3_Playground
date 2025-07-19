﻿module;
#include <SDL3/SDL.h>
module Playground.App;
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

        if (SDL_GetWindowFlags(GetMainWindow()) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

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
}

void App::Render() const
{
    // Command Buffer 가져오기
    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);

    // Swapchain Texture 가져오기 (화면에 그릴 캔버스 역할)
    SDL_GPUTexture* swapchain_texture;
    SDL_AcquireGPUSwapchainTexture(command_buffer, GetMainWindow(), &swapchain_texture, nullptr, nullptr);

    // Rendering
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    if (swapchain_texture && !is_minimized)
    {
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

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
            // Render ImGui
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
        }
        SDL_EndGPURenderPass(render_pass);
    }

    // Update and Render additional Platform Windows
    const ImGuiIO& IO = ImGui::GetIO();
    if (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Command Buffer 제출
    SDL_SubmitGPUCommandBuffer(command_buffer);
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
