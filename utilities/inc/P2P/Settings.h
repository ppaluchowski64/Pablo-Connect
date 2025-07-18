#ifndef P2P_SETTINGS_H
#define P2P_SETTINGS_H

#include <filesystem>
#include <mutex>

class P2PSettings {
public:
    static void SetFileDownloadDirectory(const std::filesystem::path& directory);
    static std::filesystem::path GetFileDownloadDirectory();

private:
    static std::mutex            m_mutex;
    static std::filesystem::path m_fileDownloadDirectory;

};

#endif //P2P_SETTINGS_H
