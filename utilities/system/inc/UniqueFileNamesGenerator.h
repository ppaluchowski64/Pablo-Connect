#ifndef UNIQUE_FILE_NAMES_GENERATOR_H
#define UNIQUE_FILE_NAMES_GENERATOR_H

#include <filesystem>
#include <mutex>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

class UniqueFileNamesGenerator {
public:
    static void SetFilePath(std::filesystem::path& path);
    static void SetFilePrefix(const std::string& prefix);
    static void SetFileSuffix(const std::string& suffix);

    NO_DISCARD static std::filesystem::path GetFilePath();
    NO_DISCARD static std::string GetFilePrefix();
    NO_DISCARD static std::string GetFileSuffix();

    static void GetUniqueName(std::string& value);
    NO_DISCARD static std::string GetUniqueName();

private:
    static std::filesystem::path s_path;
    static std::fstream          s_uniqueNamesFile;
    static std::string           s_prefix;
    static std::string           s_suffix;
    static std::mutex            s_mutex;
};

#endif //UNIQUE_FILE_NAMES_GENERATOR_H
