#include <gtest/gtest.h>
#include <P2P/Client.h>

TEST(TCP_Test, ConnectionTest) {
    P2P::Client client1, client2;

    client1.SetClientMode(P2P::ClientMode::TCP_Client);
    client2.SetClientMode(P2P::ClientMode::TCP_Client);

    client1.SeekLocalConnection();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2.Connect(client1.GetConnectionAddress(), client1.GetConnectionPorts());

    while (client1.GetConnectionState() != ConnectionState::CONNECTED && client2.GetConnectionState() != ConnectionState::CONNECTED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    client1.Disconnect();
    client2.Disconnect();
}