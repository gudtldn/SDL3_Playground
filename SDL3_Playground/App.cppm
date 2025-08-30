module;
#include <SDL3/SDL.h>
export module Playground.App;
export import SimpleEngine.Types;
export import SimpleEngine.Rendering;

import SimpleEngine.Core;
import std;


export class App
{
public:
    App();
    virtual ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    static App& Get() { return *Instance; }

    virtual void Initialize();
    virtual void Run();
    virtual void Release();

protected:
    void ProcessPlatformEvents();
    void Update(float delta_time);
    void Render() const;

public:
    bool IsRunning() const { return is_running; }

    // Application 종료 관련
    void RequestQuit() { quit_requested = true; }
    bool IsQuitRequested() const { return quit_requested; }

public:
    static double GetCurrentTime() { return CurrentTime; }
    static double GetLastTime() { return LastTime; }
    static double GetDeltaTime() { return DeltaTime; }
    static double GetFixedDeltaTime() { return FixedDeltaTime; }
    static uint64 GetTotalElapsedTime() { return TotalElapsedTime; }

    static uint32 GetTargetFps() { return TargetFps; }

    static void SetTargetFps(uint32 new_fps)
    {
        TargetFps = new_fps;
        TargetFrameTime = 1.0 / static_cast<double>(TargetFps);
    }

    SDL_Window* GetWindow(SDL_WindowID window_id) const { return windows.at(window_id); }
    SDL_Window* GetMainWindow() const { return GetWindow(main_window_id); }

    SDL_WindowID CreateWindow(const char* title, int32 x, int32 y, int32 width, int32 height, uint32 flags);
    void DestroyWindow(SDL_WindowID window_id);
    void DestroyWindow(SDL_Window* window);

    SDL_GPUDevice* GetGPUDevice() const { return gpu_device; }

private:
    static App* Instance;

    // 아래 시간들의 단위는 초단위
    static double CurrentTime;      // 현재 프레임 시작 시간
    static double LastTime;         // 이전 프레임 시작 시간
    static double DeltaTime;        // CurrentTime - LastTime
    static double FixedDeltaTime;   // 물리 계산용 DeltaTime
    static uint64 TotalElapsedTime; // 총 경과 시간 ms

    static uint32 TargetFps;       // 목표 FPS
    static double TargetFrameTime; // 목표 FPS 시간

    // Loop 제어 변수
    bool is_running = false;
    bool quit_requested = false;

private:
    std::unique_ptr<se::rendering::manager::ShaderManager> shader_manager;
    mutable se::core::ecs::World world;

private:
    SDL_WindowID main_window_id = 0;
    std::unordered_map<SDL_WindowID, SDL_Window*> windows;

    SDL_GPUDevice* gpu_device = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;

    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;
};
