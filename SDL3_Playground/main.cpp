#include "App.h"
#include "SimpleEngine/Core/Logging/LogBackendManager.h"
#include "SimpleEngine/Core/Logging/LogSettings.h"
#include "SimpleEngine/Core/Logging/Backends/ConsoleBackend.h"


int main()
{
    {
        using namespace se::core::logging;
        LogBackendManager::Get().AddBackend<ConsoleBackend>();
        LogSettings::SetForceColor(true);
    }

    {
        App app;
        app.Initialize();
        app.Run();
        app.Release();
    }
}
