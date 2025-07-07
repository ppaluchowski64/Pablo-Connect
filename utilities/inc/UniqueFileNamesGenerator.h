#ifndef UNIQUE_FILE_NAMES_GENERATOR_H
#define UNIQUE_FILE_NAMES_GENERATOR_H

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

class UniqueFileNamesGenerator {
public:
    UniqueFileNamesGenerator(const std::filesystem::path& path, const std::string& prefix, const std::string& suffix) {
        m_prefix = prefix;
        m_suffix = suffix;

        const std::filesystem::path filePath = path / ".counter.conf";

        std::filesystem::create_directories(path);

        if (!std::filesystem::exists(filePath)) {
            std::ofstream createFile(filePath, std::ios::binary);
            createFile << '0';
#ifdef _WIN32
            SetFileAttributesA(filePath.string().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
        }

        m_uniqueNamesFile.open(filePath, std::ios::in | std::ios::out | std::ios::binary);

        if (!m_uniqueNamesFile) {
            Debug::LogError("Failed to open counter file");
        }
    }

    std::string GetUniqueName() {
        m_uniqueNamesFile.seekg(0, std::ios::end);
        size_t size = m_uniqueNamesFile.tellg();
        m_uniqueNamesFile.seekg(0);

        std::string content;
        if (size > 0) {
            content.resize(size);
            m_uniqueNamesFile.read(&content[0], size);
        } else {
            content = "0";
        }

        std::string result = m_prefix + content + m_suffix;

        int i = static_cast<int>(content.size()) - 1;
        for (; i >= 0; --i) {
            if (content[i] < '9') {
                content[i]++;
                break;
            }

            content[i] = '0';
        }

        if (i == -1) {
            content.insert(content.begin(), '1');
        }

        m_uniqueNamesFile.seekp(0);
        m_uniqueNamesFile << content;
        m_uniqueNamesFile.flush();

        return result;
    }

private:
    std::fstream m_uniqueNamesFile;
    std::string  m_prefix;
    std::string  m_suffix;


};

#endif //UNIQUE_FILE_NAMES_GENERATOR_H
