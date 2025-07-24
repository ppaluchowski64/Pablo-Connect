#include <iostream>
#include <atomic>
#include <chrono>
#include <P2P/Client.h>
#define TRACY_IMPL
#include <tracy/Tracy.hpp>

typedef std::unique_ptr<Package<P2P::MessageType>> PackageObj;


void MessageH(PackageObj package) {
    int text;

    package->GetValue(text);
    Debug::Log(text);
}

int main() {
    ZoneScoped;

    try {
        const IPAddress ip = asio::ip::make_address_v4("127.0.0.1");
        constexpr uint16_t serverConnectionPort = 50000;
        constexpr uint16_t serverFileStreamPort = 50001;

        P2P::Client client(P2P::ClientRole::Client, ip, serverConnectionPort, serverFileStreamPort);
        P2P::Client server(P2P::ClientRole::Server, ip, serverConnectionPort, serverFileStreamPort);

        client.SetClientMode(P2P::ClientMode::TCP_Client);
        server.SetClientMode(P2P::ClientMode::TCP_Client);

        client.AddHandler(P2P::MessageType::message, MessageH);
        server.AddHandler(P2P::MessageType::message, MessageH);

        server.Connect();
        client.Connect();

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        while (true) {
            server.Send(P2P::MessageType::message, (int)5);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}