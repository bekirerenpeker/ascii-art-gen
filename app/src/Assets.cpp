#include "Assets.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Assets {

namespace {

std::filesystem::path executableDir()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};

    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
#else
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};

    return self.parent_path();
#endif
}

std::filesystem::path locate()
{
    const std::filesystem::path exe = executableDir();

    // Installed layout first, then a copy sitting next to the binary, then the
    // source tree. A directory only counts if it actually holds the help pages,
    // so a stale prefix cannot shadow a working one.
    const std::filesystem::path candidates[] = {
        exe.empty() ? std::filesystem::path {} : exe.parent_path() / "share" / "asciigen",
        exe.empty() ? std::filesystem::path {} : exe / "assets",
        std::filesystem::path(SOURCE_DIR) / "assets",
    };

    std::error_code ec;
    for (const std::filesystem::path& candidate : candidates) {
        if (candidate.empty()) continue;
        if (std::filesystem::is_directory(candidate / "help", ec)) return candidate;
    }

    return {};
}

}   // namespace

const std::filesystem::path& dir()
{
    static const std::filesystem::path instance = locate();
    return instance;
}

std::filesystem::path help(const std::string& page)
{
    if (dir().empty()) return {};
    return dir() / "help" / (page + ".txt");
}

std::filesystem::path font(const std::string& name)
{
    if (dir().empty()) return {};
    return dir() / "fonts" / name;
}

}   // namespace Assets
