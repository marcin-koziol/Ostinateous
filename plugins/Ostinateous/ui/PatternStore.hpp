/*
 * Ostinateous - user pattern library (save/load/list/delete) plus the MIDI
 * export "Clips folder" path helper. Mirrors the Sideous family's own
 * ui/PresetStore.hpp exactly - same plain-text-file-per-item approach, same
 * POSIX mkdir/opendir/readdir (not std::filesystem: libc++'s std::filesystem
 * symbols are annotated unavailable below a macOS 10.15 deployment target,
 * and DPF/PawPaw's macOS-universal build targets older than that for host
 * compatibility - confirmed the same failure across every sideous-* plugin
 * that used it) - just storing a serialized Pattern instead of param values.
 */

#pragma once

#include "../dsp/Pattern.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#endif

namespace ostinateous {
namespace ui {

inline std::string patternsDirectory()
{
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    return appData ? std::string(appData) + "\\ostinateous\\patterns" : ".\\patterns";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/ostinateous/patterns" : "./patterns";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return std::string(xdg) + "/ostinateous/patterns";
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share/ostinateous/patterns" : "./patterns";
#endif
}

// default "Save to Clips Folder" target - a shared, cross-application
// convention (Ardour's own Preferences -> Triggering page watches exactly
// this kind of folder), deliberately NOT under an ostinateous-specific
// directory the way patternsDirectory() is.
inline std::string clipsDirectory()
{
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    return appData ? std::string(appData) + "\\sounds\\clips" : ".\\clips";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/sounds/clips" : "./clips";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return std::string(xdg) + "/sounds/clips";
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share/sounds/clips" : "./clips";
#endif
}

inline std::string homeDirectory()
{
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    return appData ? std::string(appData) : ".";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : ".";
#endif
}

inline bool directoryExists(const std::string& path) noexcept
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ~/.local/share/ostinateous (or platform equivalent) - where ostinateous's own
// small config lives, as opposed to clipsDirectory() which is a shared,
// cross-application convention (Ardour's own Clips folder)
inline std::string configDirectory()
{
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    return appData ? std::string(appData) + "\\ostinateous" : ".\\ostinateous";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/ostinateous" : "./ostinateous";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return std::string(xdg) + "/ostinateous";
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share/ostinateous" : "./ostinateous";
#endif
}

inline std::string sanitizeName(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char c : raw)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || (unsigned char)c < 0x20)
            out += '_';
        else
            out += c;
    }

    const size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return "Pattern";
    const size_t end = out.find_last_not_of(' ');
    return out.substr(start, end - start + 1);
}

// mkdir, one path component at a time (POSIX mkdir() doesn't create missing
// parents) - failures (e.g. a component already existing) are silently
// ignored, same as the std::filesystem version this replaced elsewhere.
inline void createDirectories(const std::string& path)
{
    for (size_t pos = 0; pos < path.size(); )
    {
        size_t next = path.find_first_of("/\\", pos);
        if (next == std::string::npos)
            next = path.size();
        const std::string partial = path.substr(0, next);
        if (!partial.empty())
        {
#if defined(_WIN32)
            _mkdir(partial.c_str());
#else
            mkdir(partial.c_str(), 0755);
#endif
        }
        pos = next + 1;
    }
}

inline bool hasSuffix(const std::string& s, const std::string& suffix) noexcept
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// clipsDirectory() is only a sensible default if Ardour (or whatever else
// watches it) actually created it - defaulting to it unconditionally would
// point at a folder that doesn't exist yet on a fresh setup, and the
// per-platform fallback in clipsDirectory()/configDirectory() for a missing
// HOME env var is a bare relative path ("./..."), which can silently
// resolve under whatever a plugin host/bridge process's working directory
// happens to be (e.g. /tmp) rather than anywhere the user would expect.
inline std::string defaultExportFolder()
{
    const std::string clips = clipsDirectory();
    if (directoryExists(clips))
        return clips;
    return homeDirectory();
}

// the "Export MIDI" target folder - a user preference, not tied to any one
// pattern, so it lives in its own tiny one-line file rather than DPF plugin
// state (which is per-instance/session, not a global setting that should
// follow the user across every project). Defaults to defaultExportFolder()
// until the user picks something else via the UI's Folder button.
inline std::string loadExportFolder()
{
    std::ifstream in(configDirectory() + "/export_folder.txt");
    if (in.is_open())
    {
        std::string line;
        if (std::getline(in, line) && !line.empty())
            return line;
    }
    return defaultExportFolder();
}

inline bool saveExportFolder(const std::string& path)
{
    createDirectories(configDirectory());
    std::ofstream out(configDirectory() + "/export_folder.txt");
    if (!out.is_open())
        return false;
    out << path << "\n";
    return true;
}

static constexpr const char* kPatternExt = ".ostpattern";

inline std::vector<std::string> listPatterns()
{
    std::vector<std::string> names;
    DIR* dir = opendir(patternsDirectory().c_str());
    if (dir == nullptr)
        return names;

    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (hasSuffix(name, kPatternExt))
            names.push_back(name.substr(0, name.size() - std::strlen(kPatternExt)));
    }
    closedir(dir);

    std::sort(names.begin(), names.end());
    return names;
}

inline bool savePattern(const std::string& name, const Pattern& pattern)
{
    createDirectories(patternsDirectory());

    std::ofstream out(patternsDirectory() + "/" + name + kPatternExt);
    if (!out.is_open())
        return false;

    out << pattern.serialize();
    return true;
}

inline bool loadPattern(const std::string& name, Pattern& outPattern)
{
    std::ifstream in(patternsDirectory() + "/" + name + kPatternExt);
    if (!in.is_open())
        return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    outPattern = Pattern::deserialize(ss.str().c_str());
    return true;
}

inline bool deletePattern(const std::string& name)
{
    return std::remove((patternsDirectory() + "/" + name + kPatternExt).c_str()) == 0;
}

} // namespace ui
} // namespace ostinateous
