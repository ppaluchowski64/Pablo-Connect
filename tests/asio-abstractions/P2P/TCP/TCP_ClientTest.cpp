#include <gtest/gtest.h>
#include <Client.h>

static std::vector<std::future<void>> leaked_futures;

TEST(TCP_Test, ConnectionTest) {
    auto future = std::async(std::launch::async, [] {
        P2P::Client client1, client2;

        client1.SetClientMode(P2P::ClientMode::TCP_Client);
        client2.SetClientMode(P2P::ClientMode::TCP_Client);

        std::atomic<bool> ptr = false;

        client1.SeekLocalConnection([&ptr]() {
            ptr.store(true);
        });

        while (!ptr.load()) {}

        client2.Connect(client1.GetConnectionAddress(), client1.GetConnectionPorts());

        while (client1.GetConnectionState() != ConnectionState::CONNECTED && client2.GetConnectionState() != ConnectionState::CONNECTED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        client1.Disconnect();
        client2.Disconnect();
    });

#ifdef TIMEOUT_TIME___S
    if (future.wait_for(std::chrono::seconds(TIMEOUT_TIME___S)) == std::future_status::timeout) {
#else
    if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
#endif
        leaked_futures.emplace_back(std::move(future));
        FAIL() << "Test timed out!";
    } else {
        try {
            future.get();
        } catch (const std::exception& e) {
            FAIL() << "Test failed with an exception: " << e.what();
        }
    }
}