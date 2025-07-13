module Playground.App;
import <cassert>;
import <SDL3/SDL.h>;


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
    SDL_Quit();
}

void App::ProcessPlatformEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            RequestQuit();
        }
    }
}

void App::Update(float delta_time)
{
}

void App::Render() const
{
}
