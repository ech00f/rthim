#pragma once

#include <string>

// Единые пути игры. Корень — папка проекта (там, где лежат assets/ и songs/),
// даже если бинарник запущен из build/.
namespace paths {

void init(const std::string &assetRoot);

const std::string &root();
std::string songs();
std::string customSongs();
std::string cache();
std::string settingsFile();

} // namespace paths