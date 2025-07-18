#include <iostream>
#include <atomic>
#include <chrono>
#include <P2P/Client.h>

typedef std::unique_ptr<Package<P2P::MessageType>> PackageObj;

P2P::Client* clientPtr;
P2P::Client* serverPtr;

std::atomic<uint64_t> counter = 0;

void MessageH(PackageObj package) {
    ++counter;
}

int main() {
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

        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            client.Send(P2P::MessageType::message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8000));

        uint64_t val = counter.load();

        Debug::Log("Total packages: {}", val);
        Debug::Log("p/s:            {}", static_cast<double>(val) / 10);

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}