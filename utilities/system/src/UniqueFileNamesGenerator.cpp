#include <UniqueFileNamesGenerator.h>

std::filesystem::path UniqueFileNamesGenerator::s_path{};
std::fstream          UniqueFileNamesGenerator::s_uniqueNamesFile{};
std::string           UniqueFileNamesGenerator::s_prefix{};
std::string           UniqueFileNamesGenerator::s_suffix{};
std::mutex            UniqueFileNamesGenerator::s_mutex{};

void UniqueFileNamesGenerator::SetFilePath(std::filesystem::path& path) {
    std::lock_guard lock(s_mutex);

    if (s_path != path) {
        s_path = path.string();

        if (s_uniqueNamesFile.is_open()) {
            s_uniqueNamesFile.close();
        }

        const std::string filePath = (path / ".counter.conf").string();
        std::filesystem::create_directories(s_path);
        s_uniqueNamesFile.open(filePath, std::ios::in | std::ios::out | std::ios::binary);

#ifdef _WIN32
        SetFileAttributesA(filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

        s_uniqueNamesFile << '0';
    }
}

void UniqueFileNamesGenerator::SetFilePrefix(const std::string& prefix) {
    std::lock_guard lock(s_mutex);
    s_prefix = prefix;
}

void UniqueFileNamesGenerator::SetFileSuffix(const std::string& suffix) {
    std::lock_guard lock(s_mutex);
    s_suffix = suffix;
}

std::filesystem::path UniqueFileNamesGenerator::GetFilePath() {
    std::lock_guard lock(s_mutex);
    return s_path;
}

std::string UniqueFileNamesGenerator::GetFilePrefix() {
    std::lock_guard lock(s_mutex);
    return s_prefix;
}

std::string UniqueFileNamesGenerator::GetFileSuffix() {
    std::lock_guard lock(s_mutex);
    return s_suffix;
}

std::string UniqueFileNamesGenerator::GetUniqueName() {
    std::lock_guard lock(s_mutex);

    s_uniqueNamesFile.seekg(0, std::ios::end);
    const std::streamsize size = s_uniqueNamesFile.tellg();
    s_uniqueNamesFile.seekg(0);

    std::string content;
    if (size > 0) {
        content.resize(size);
        s_uniqueNamesFile.read(&content[0], size);
    } else {
        content = "0";
    }

    std::string result = (s_path / s_prefix).string() + content + s_suffix;

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

    s_uniqueNamesFile.seekp(0);
    s_uniqueNamesFile << content;
    s_uniqueNamesFile.flush();

    return result;
}

void UniqueFileNamesGenerator::GetUniqueName(std::string& value) {
    std::lock_guard lock(s_mutex);

    s_uniqueNamesFile.seekg(0, std::ios::end);
    const size_t size = s_uniqueNamesFile.tellg();
    s_uniqueNamesFile.seekg(0);

    std::string content;
    if (size > 0) {
        content.resize(size);
        s_uniqueNamesFile.read(&content[0], size);
    } else {
        content = "0";
    }

    value = (s_path / s_prefix).string() + content + s_suffix;

    int i = content.size() - 1;
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

    s_uniqueNamesFile.seekp(0);
    s_uniqueNamesFile << content;
    s_uniqueNamesFile.flush();
}