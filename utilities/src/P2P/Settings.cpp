#include <P2P/Settings.h>

std::mutex P2PSettings::m_mutex{};
std::filesystem::path P2PSettings::m_fileDownloadDirectory{};

void P2PSettings::SetFileDownloadDirectory(const std::filesystem::path& directory) {
    std::lock_guard lock(m_mutex);
    m_fileDownloadDirectory = directory;
}

std::filesystem::path P2PSettings::GetFileDownloadDirectory() {
    return m_fileDownloadDirectory;
}

