#pragma once

#include "App/Tweaks/Batch/TweakChangelog.hpp"
#include "App/Tweaks/Batch/TweakChangeset.hpp"
#include "App/Tweaks/Declarative/TweakReader.hpp"
#include "App/Tweaks/TweakContext.hpp"
#include "Core/Logging/LoggingAgent.hpp"
#include "Red/TweakDB/Manager.hpp"

namespace App
{
class TweakImporter : Core::LoggingAgent
{
public:
    TweakImporter(Core::SharedPtr<Red::TweakDBManager> aManager, Core::SharedPtr<App::TweakContext> aContext);

    void ImportTweaks(const Core::Vector<std::filesystem::path>& aImportPaths,
                      const Core::SharedPtr<App::TweakChangelog>& aChangelog = nullptr,
                      bool aDryRun = false);

private:
    bool Read(const Core::SharedPtr<App::TweakChangeset>& aChangeset,
              const std::filesystem::path& aPath,
              const std::filesystem::path& aDir);
    bool Apply(const Core::SharedPtr<App::TweakChangeset>& aChangeset,
               const Core::SharedPtr<App::TweakChangelog>& aChangelog);

    static bool IsFirstPriority(const std::filesystem::path& aPath);
    static bool IsLastPriority(const std::filesystem::path& aPath);

    Core::SharedPtr<Red::TweakDBManager> m_manager;
    Core::SharedPtr<App::TweakContext> m_context;
};
}
