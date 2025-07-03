#include <DebugLog.h>
#include <iostream>
#include <SSL/CertificateManager.h>

int main() {
    if (!CertificateManager::IsCertificateValid(".")) {
        Debug::Log("Generating new ssl certificate");
        CertificateManager::GenerateCertificate(".");
    }

    std::cin.get();
}
