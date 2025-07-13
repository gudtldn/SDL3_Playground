export module Playground.App;
import std;
import <cstdint>;
import <cstddef>;

export
{
    // 정수형
    using int8 = std::int8_t;
    using uint8 = std::uint8_t;
    using int16 = std::int16_t;
    using uint16 = std::uint16_t;
    using int32 = std::int32_t;
    using uint32 = std::uint32_t;
    using int64 = std::int64_t;
    using uint64 = std::uint64_t;

    // 문자형 (UTF-8 기본)
    using char8 = char8_t;
    using char16 = char16_t;
    using char32 = char32_t;

    // 크기 및 포인터 정수형
    using size_t = std::size_t;
    using intptr = std::intptr_t;
    using uintptr = std::uintptr_t;
}

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
};
