// paths.cpp -- see paths.h.
#include "paths.h"

#include <climits>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

bool IsAbsolute(const std::string &p)
{
#ifdef _WIN32
    return (p.size() >= 2 && p[1] == ':') || (!p.empty() && (p[0] == '\\' || p[0] == '/'));
#else
    return !p.empty() && p[0] == '/';
#endif
}

std::string DirName(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

bool IsDir(const std::string &path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool IsFile(const std::string &path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

std::string JoinPath(const std::string &a, const std::string &b)
{
    if (a.empty() || IsAbsolute(b))
        return b;
    char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

std::string CurrentDir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    return _getcwd(buf, sizeof buf) ? std::string(buf) : std::string(".");
#else
    char buf[PATH_MAX];
    return getcwd(buf, sizeof buf) ? std::string(buf) : std::string(".");
#endif
}

std::string ExeDir(const char *argv0)
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
    if (n > 0 && n < sizeof buf)
        return DirName(std::string(buf, n));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1);
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        char real[PATH_MAX];
        if (realpath(buf.data(), real))
            return DirName(real);
    }
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0)
        return DirName(std::string(buf, (size_t)n));
#endif
    return argv0 ? DirName(argv0) : std::string();
}

std::string ResolveDataDir(const std::string &dir, const char *argv0)
{
    if (IsAbsolute(dir) || IsDir(dir))
        return dir;
    std::string beside = JoinPath(ExeDir(argv0), dir);
    return IsDir(beside) ? beside : dir;
}
