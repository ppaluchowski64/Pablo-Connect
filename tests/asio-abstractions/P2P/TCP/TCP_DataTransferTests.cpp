#include <gtest/gtest.h>
#include <../../../../utilities/p2p/inc/Client.h>

#include <thread>
#include <future>
#include <chrono>

static std::vector<std::future<void>> leaked_futures;

TEST(TCP_Test, DataTransferTest_SimplePackage) {
    auto future = std::async(std::launch::async, [] {
        std::array<uint16_t, 2> ports{};
        asio::ip::address address{};

        std::atomic<bool> ready{false};
        std::atomic<int> clientMessageReceived{0};
        std::atomic<int> serverMessageReceived{0};

        std::thread clientThread([&]() {
            P2P::Client client;
            client.SetClientMode(P2P::ClientMode::TCP_Client);

            client.AddHandler(P2P::MessageType::message, [&](std::unique_ptr<PackageIn<P2P::MessageType>> package) {
                std::string value;
                package->Package->GetValue(value);
                ASSERT_EQ(std::string("echo test"), value);
                ++clientMessageReceived;
            });

            while (!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            client.Connect(address, ports, [&]() {
                client.Send(P2P::MessageType::message, std::string("message test"));
                client.Send(P2P::MessageType::echo, std::string("echo test"));
            });

            while (serverMessageReceived.load() < 2 || clientMessageReceived.load() < 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::thread serverThread([&]() {
            P2P::Client server;
            server.SetClientMode(P2P::ClientMode::TCP_Client);

            server.AddHandler(P2P::MessageType::message, [&](std::unique_ptr<PackageIn<P2P::MessageType>> package) {
                std::string value;
                package->Package->GetValue(value);
                ++serverMessageReceived;
            });

            server.AddHandler(P2P::MessageType::echo, [&](std::unique_ptr<PackageIn<P2P::MessageType>> package) {
                std::string value;
                package->Package->GetValue(value);
                std::unique_ptr<Package<P2P::MessageType>> packageCopy = Package<P2P::MessageType>::CreateUnique(P2P::MessageType::message, std::move(value));
                server.Send(std::move(packageCopy));
                ++serverMessageReceived;
            });

            server.SeekLocalConnection([&]() {
                ports = server.GetConnectionPorts();
                address = server.GetConnectionAddress();

                ready.store(true);
            });

            while (!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            while (serverMessageReceived.load() < 2 || clientMessageReceived.load() < 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        serverThread.join();
        clientThread.join();

    });

    if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
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