#ifndef CERTIFICATE_MANAGER_H
#define CERTIFICATE_MANAGER_H

#define _CRT_SECURE_NO_WARNINGS

#include <filesystem>

class CertificateManager {
public:
    static void GenerateCertificate(const std::filesystem::path& path);
    static bool IsCertificateValid(const std::filesystem::path& path);

private:
    static std::string GetOpenSSLError();

};

#endif //CERTIFICATE_MANAGER_H
