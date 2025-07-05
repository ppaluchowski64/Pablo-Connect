#include <DebugLog.h>
#include <iostream>
#include <SSL/CertificateManager.h>
#include <SSL/Package.h>
#include <asio.hpp>

enum class packType : uint16_t {
    message
};

int main() {
    if (!CertificateManager::IsCertificateValid("certificate/")) {
        Debug::Log("Generating new ssl certificate");
        CertificateManager::GenerateCertificate("certificate/");
    }

    std::cin.get();
}
