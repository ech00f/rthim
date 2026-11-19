#include "paths.hpp"

#include <filesystem>
#include <system_error>

namespace paths {
namespace {

std::string g_root = ".";

} // namespace

void init(const std::string &assetRoot) {
    std::error_code ec;
    std::filesystem::path base =
        std::filesystem::absolute(std::filesystem::path(assetRoot), ec);
    if (ec) {
        base = assetRoot;
    }
    // assetRoot обычно <...>/assets (в build/ или рядом с бинарником).
    std::filesystem::path p = base.parent_path();
    if (p.empty()) {
        p = std::filesystem::current_path(ec);
    }

    auto isDevRoot = [](const std::filesystem::path &d) {
        std::error_code e2;
        return std::filesystem::exists(d / "CMakeLists.txt", e2) ||
               std::filesystem::is_directory(d / "src", e2) ||
               std::filesystem::is_directory(d / ".git", e2);
    };

    // Из build/assets поднимаемся до корня исходников, а не остаёмся в build/.
    std::filesystem::path root = p;
    if (!isDevRoot(root)) {
        std::filesystem::path up = p.parent_path();
        if (!up.empty() && isDevRoot(up)) {
            root = up;
        }
    }

    g_root = root.string();
    for (const char *sub : {"songs", "songs/custom", ".cache"}) {
        std::filesystem::create_directories(root / sub, ec);
    }
}

const std::string &root() { return g_root; }

std::string songs() { return (std::filesystem::path(g_root) / "songs").string(); }

std::string customSongs() {
    return (std::filesystem::path(g_root) / "songs" / "custom").string();
}

std::string cache() {
    return (std::filesystem::path(g_root) / ".cache").string();
}

std::string settingsFile() {
    return (std::filesystem::path(g_root) / "settings.cfg").string();
}

} // namespace paths