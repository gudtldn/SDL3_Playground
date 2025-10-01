#include "API/x64/LPP_API_x64_CPP.h"
#include "tracy/Tracy.hpp"

import std;
import Playground.App;
import SE.Types;

import SE.Core;
import <Windows.h>;


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

    lpp::LppDefaultAgent lpp_agent = lpp::LppCreateDefaultAgent(nullptr, L"../ThirdParty/LivePP");

    if (!lpp::LppIsValidDefaultAgent(&lpp_agent))
    {
        return 1;
    }

    lpp_agent.EnableModule(
        lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr
    );

    {
        using namespace se::core::logging;
        LogBackendManager::Get().AddBackend<backends::ConsoleBackend>();
        LogSettings::SetForceColor(true);
    }

    {
        App app;
        app.Initialize();
        app.Run();
        app.Release();
    }

    lpp::LppDestroyDefaultAgent(&lpp_agent);

    return 0;
}
