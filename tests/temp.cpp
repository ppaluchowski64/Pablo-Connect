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

    Package<packType> package = Package<packType>::Create(packType::message, iv, fv, ss, v);

    int rv = package.GetValue<int>();
    float rf = package.GetValue<float>();
    std::string sf = package.GetValue<std::string>();
    std::vector<int> v2 = package.GetValue<std::vector<int>>();
    int rvd = package.GetValue<int>();

    Debug::Log(std::to_string(rv) + " " + std::to_string(rf) + " " + sf);

    std::string result = "";

    for (const auto &v : v2) {
        result += std::to_string(v) + " ";
    }
    Debug::Log(result);

    std::cin.get();
}
