#ifndef TLS_CERTIFICATE_MANAGER_H
#define TLS_CERTIFICATE_MANAGER_H

#define _CRT_SECURE_NO_WARNINGS

#include <filesystem>

namespace TLS {
    class CertificateManager {
    public:
        static void GenerateCertificate(const std::filesystem::path& path);
        static bool IsCertificateValid(const std::filesystem::path& path);

    private:
        static std::string GetOpenSSLError();

    };
}

#endif //TLS_CERTIFICATE_MANAGER_H
