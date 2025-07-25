#include <gtest/gtest.h>
#include <P2P/Client.h>

TEST(TCP_Test, ConnectionTest) {
    P2P::Client client1, client2;

    client1.SetClientRole(P2P::ClientRole::Client);
    client1.SetClientMode(P2P::ClientMode::TCP_Client);

    client2.SetClientRole(P2P::ClientRole::Server);
    client2.SetClientMode(P2P::ClientMode::TCP_Client);

    client2.Connect("127.0.0.1", {50000, 50001});
    client1.Connect("127.0.0.1", {50000, 50001});

    while (client1.GetConnectionState() != ConnectionState::CONNECTED && client2.GetConnectionState() != ConnectionState::CONNECTED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    client1.Disconnect();
    client2.Disconnect();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

}