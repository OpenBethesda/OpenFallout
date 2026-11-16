#ifndef OPENFALLOUT_COMPONENTS_CRASHCATCHER_CRASHCATCHER_HPP
#define OPENFALLOUT_COMPONENTS_CRASHCATCHER_CRASHCATCHER_HPP

#include <filesystem>

void crashCatcherInstall(int argc, char** argv, const std::filesystem::path& crashLogPath);

#endif
