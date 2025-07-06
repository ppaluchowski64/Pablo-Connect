#include <DebugLog.h>
#include <iostream>
#include <functional>
#include <vector>
#include <algorithm>
#include <SSL/CertificateManager.h>
#include <SSL/Package.h>
#include <TSVector.h>
#include <thread>
#include <SSL/Connection.h>

enum class packType : uint16_t {
    message
};

ts::vector<std::shared_ptr<Connection<packType>>> connections;

inline SSLContext CreateSSLContext(
    const std::string& certFile,
    const std::string& keyFile,
    const bool server
) {
    auto ctx = SSLContext(server ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);

    ctx.set_options(
        SSLContext::default_workarounds |
        SSLContext::default_workarounds |
        SSLContext::no_sslv2 |
        SSLContext::no_sslv3 |
        SSLContext::no_tlsv1 |
        SSLContext::no_tlsv1_1
    );

    ctx.use_certificate_chain_file(certFile);
    ctx.use_private_key_file(keyFile, SSLContext::pem);
    ctx.set_verify_mode(asio::ssl::verify_none);

    return std::move(ctx);
}

void Callback(std::shared_ptr<Connection<packType>> connection) {
    connections.push_back(connection);
}


int main(int argc, char** argv) {
    if (!CertificateManager::IsCertificateValid("certificate/")) {
        Debug::Log("Generating new ssl certificate");
        CertificateManager::GenerateCertificate("certificate/");
    }

    if (argc > 1) {
        if (std::string(argv[1]) == "server") {
            Debug::Log("Starting server");
            Endpoint endpoint(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 20000));

            IOContext context;
            auto work = asio::make_work_guard(context);
            std::thread thread([&]() {context.run();});
            SSLContext sslContext = CreateSSLContext("certificate/certificate.crt", "certificate/privateKey.key", true);
            ts::deque<PackageIn<packType>> packages;
            Acceptor acceptor(context, endpoint);

            Connection<packType>::Seek(context, sslContext, packages, acceptor, Callback);

            while (true) {
                while (!packages.empty()) {
                    PackageIn<packType> package = packages.pop_front();
                    std::string value;
                    package.package->GetValue<std::string>(value);

                    Debug::Log("Package from [" + std::to_string(package.connection->GetConnectionID()) + "]: " + value);

                    std::string message = "hello";
                    std::unique_ptr<Package<packType>> pack = Package<packType>::CreateUniquePtr(packType::message, message);
                    package.connection->Send(std::move(pack));
                }
            }
        }

        if (std::string(argv[1]) == "client") {
            Debug::Log("Starting client");
            IOContext context;
            auto work = asio::make_work_guard(context);
            std::thread thread([&]() {context.run();});
            SSLContext sslContext = CreateSSLContext("certificate/certificate.crt", "certificate/privateKey.key", false);
            ts::deque<PackageIn<packType>> packages;
            Endpoint endpoint(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 20000));

            Connection<packType>::Start(context, sslContext, packages,endpoint, Callback);

            while (true) {
                while (!packages.empty()) {
                    PackageIn<packType> package = packages.pop_front();
                    std::string value;
                    package.package->GetValue<std::string>(value);

                    Debug::Log("Package from [" + std::to_string(package.connection->GetConnectionID()) + "]: " + value);
                }

                if (connections.size() == 0) continue;

                std::string message;
                std::cin >> message;

                std::vector<std::shared_ptr<Connection<packType>>> sn = connections.snapshot();
                std::unique_ptr<Package<packType>> pack = Package<packType>::CreateUniquePtr(packType::message, message);
                sn[0]->Send(std::move(pack));
            }
        }
    }

    std::cin.get();
}
