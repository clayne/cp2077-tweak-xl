#include "Application.hpp"
#include "App/Environment.hpp"
#include "App/Migration.hpp"
#include "App/Project.hpp"
#include "App/Stats/StatService.hpp"
#include "App/Tweaks/TweakService.hpp"
#include "Core/Foundation/RuntimeProvider.hpp"
#include "Support/MinHook/MinHookProvider.hpp"
#include "Support/RED4ext/RED4extProvider.hpp"
#include "Support/RedLib/RedLibProvider.hpp"
#include "Support/Spdlog/SpdlogProvider.hpp"

App::Application::Application(HMODULE aHandle, const RED4ext::Sdk* aSdk)
{
    Register<Core::RuntimeProvider>(aHandle)
        ->SetBaseImagePathDepth(2);

    Register<Support::MinHookProvider>();
    Register<Support::SpdlogProvider>()
        ->AppendTimestampToLogName()
        ->CreateRecentLogSymlink();
    Register<Support::RED4extProvider>(aHandle, aSdk)
        ->EnableAddressLibrary()
        ->RegisterScripts(Env::PluginScriptsDir());
    Register<Support::RedLibProvider>();

    Register<App::TweakService>(Env::GameVer(), Env::GameDir(), Env::TweaksDir(),
                                Env::InheritanceMapPath(), Env::ExtraFlatsPath(),
                                Env::RedModSourcesDir());
    Register<App::StatService>();
}

void App::Application::OnStarting()
{
    LogInfo("{} {} is starting...", Project::Name, Project::Version.to_string());

    Migration::CleanUp(Env::LegacyScriptsDir());
}
