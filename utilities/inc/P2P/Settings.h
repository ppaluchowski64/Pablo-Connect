#ifndef P2P_SETTINGS_H
#define P2P_SETTINGS_H

#include <filesystem>
#include <mutex>

class P2PSettings {
public:
    static void SetFileDownloadDirectory(const std::filesystem::path& directory) {
        std::lock_guard lock(m_mutex);
        m_fileDownloadDirectory = directory;
    }

    static std::filesystem::path GetFileDownloadDirectory() {
        return m_fileDownloadDirectory;
    }

private:
    static std::mutex            m_mutex;
    static std::filesystem::path m_fileDownloadDirectory;

};

#endif //P2P_SETTINGS_H
