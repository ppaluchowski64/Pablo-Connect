#include <SSL/Connection.h>
#include <SSL/CertificateManager.h>
#include <iostream>
#include <thread>
#include <TSDeque.h>
#include <asio.hpp>
#include <asio/ssl.hpp>

enum class packType : uint16_t {
    Hello
};

using namespace std;
typedef Connection<packType> MyConnection; // Replace packType with your enum/type

int main() {
    try {
        asio::io_context ioContext;
        // Paths to your SSL keys/certs
        filesystem::path sslPath = "certificates/";

        if (!CertificateManager::IsCertificateValid(sslPath)) {
            CertificateManager::GenerateCertificate(sslPath);
        }

        auto sslServerCtx = MyConnection::CreateSSLContext(sslPath, true);
        auto sslClientCtx = MyConnection::CreateSSLContext(sslPath, false);

        // Inbound deque shared between server and client handlers
        ts::deque<PackageIn<packType>> inDeque;

        // Storage for connections to keep them alive
        vector<shared_ptr<MyConnection>> connections;
        connections.reserve(2);

        // Server setup
        asio::ip::tcp::acceptor acceptor(ioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 12345));
        MyConnection::Seek(ioContext, sslServerCtx, inDeque, acceptor,
            [&](shared_ptr<MyConnection> conn) {
                cout << "Server: New connection ID " << conn->GetConnectionID() << endl;
                connections.push_back(conn);
            }
        );

        // Client setup
        asio::ip::tcp::resolver resolver(ioContext);
        auto endpoints = resolver.resolve("127.0.0.1", "12345");
        MyConnection::Start(ioContext, sslClientCtx, inDeque, *endpoints.begin(),
            [&](shared_ptr<MyConnection> conn) {
                cout << "Client: Connected with ID " << conn->GetConnectionID() << endl;
                connections.push_back(conn);

                // Send a test message (replace with real package type and args)
                conn->Send(packType::Hello, PackageFlag::NONE, string("Hello from client!"));
            }
        );

        // Run context in background thread
        thread ioThread([&]() { ioContext.run(); });

        // Simple loop to process incoming packages
        while (true) {
            if (!inDeque.empty()) {
                auto pkgIn = inDeque.pop_front();
                auto& pkg = pkgIn.package;
                auto& conn = pkgIn.connection;

                // Example: read a string
                string msg = pkg->GetValue<string>();
                // Echo back
                conn->Send(packType::Hello, PackageFlag::NONE, string("Echo: " + msg + msg + msg + msg + msg + msg + msg + msg + msg));
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }

        ioThread.join();
    } catch (exception& e) {
        cerr << "Exception: " << e.what() << endl;
    }
    return 0;
}
