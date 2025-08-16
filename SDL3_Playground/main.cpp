import std;
import Playground.App;
import SimpleEngine.Types;

import SimpleEngine.Core;
import <Windows.h>;

App app;


int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    {
        using namespace se::core::logging;
        LogBackendManager::Get().AddBackend<backends::ConsoleBackend>();
        LogSettings::SetForceColor(true);
    }

    app.Initialize();
    app.Run();
    app.Release();

    return 0;
}
