#include <iostream>
#include <P2P/Client.h>



int main() {
    try {
        const IPAddress ip = asio::ip::make_address_v4("127.0.0.1");
        constexpr uint16_t serverConnectionPort = 50423;
        constexpr uint16_t serverFileStreamPort = 50424;

        P2P::Client client(P2P::ClientRole::Client, ip, serverConnectionPort, serverFileStreamPort);
        P2P::Client server(P2P::ClientRole::Server, ip, serverConnectionPort, serverFileStreamPort);

        client.SetClientMode(P2P::ClientMode::TCP_Client);
        server.SetClientMode(P2P::ClientMode::TCP_Client);

        server.Connect();
        client.Connect();

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}