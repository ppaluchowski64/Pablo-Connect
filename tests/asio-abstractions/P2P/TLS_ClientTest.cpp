#include <gtest/gtest.h>
#include <P2P/Client.h>

static void CallbackConnectionTest(void* data) {
    static_cast<std::atomic<bool>*>(data)->store(true);
}

TEST(TLS_Test, ConnectionTest) {
    P2P::Client client1, client2;

    client1.SetClientMode(P2P::ClientMode::TLS_Client);
    client2.SetClientMode(P2P::ClientMode::TLS_Client);

    std::atomic<bool> ptr = false;

    ConnectionSeekCallbackData data;
    data.callback = CallbackConnectionTest;
    data.data = &ptr;

    client1.SeekLocalConnection(data);

    while (!ptr.load()) {}

    client2.Connect(client1.GetConnectionAddress(), client1.GetConnectionPorts());

    while (client1.GetConnectionState() != ConnectionState::CONNECTED && client2.GetConnectionState() != ConnectionState::CONNECTED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    client1.Disconnect();
    client2.Disconnect();
}