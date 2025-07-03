#include <DebugLog.h>
#include <iostream>
#include <SSL/CertificateManager.h>
#include <SSL/Package.h>
#include <asio.hpp>

enum class packType : uint16_t {
    message
};

int main() {
    if (!CertificateManager::IsCertificateValid(".")) {
        Debug::Log("Generating new ssl certificate");
        CertificateManager::GenerateCertificate(".");
    }

    int iv = 5;
    float fv = 3.4;
    std::string ss = "Hello world!";
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    Package<packType> package = Package<packType>::CreateFromData(packType::message, iv, fv, ss, v);

    std::cin.get();
}
