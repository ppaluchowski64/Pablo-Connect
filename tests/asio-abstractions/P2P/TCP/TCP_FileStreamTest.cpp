#include <gtest/gtest.h>
#include <P2P/Client.h>

#include <thread>
#include <future>
#include <chrono>

#include <fstream>

static std::vector<std::future<void>> leaked_futures;

TEST(TCP_Test, FileStreamTest_SmallFile) {
    std::fstream fileStream;
    const std::string data(1024, 'a');

    if (std::filesystem::exists("test.txt")) {
        std::filesystem::remove("test.txt");
    }

    if (std::filesystem::exists("test_result.txt")) {
        std::filesystem::remove("test_result.txt");
    }

    fileStream.open("test.txt", std::ios::out | std::ios::binary);
    fileStream.write(data.c_str(), data.size());
    fileStream.close();

    auto future = std::async(std::launch::async, [] {
        std::array<uint16_t, 2> ports{};
        asio::ip::address address{};

        std::atomic<bool> ready{false};

        std::thread clientThread([&]() {
            P2P::Client client;
            client.SetClientMode(P2P::ClientMode::TCP_Client);

            while (!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            client.Connect(address, ports, [&]() {
                client.RequestFile("./test.txt", "test_result.txt");
            });

            while (!std::filesystem::exists("test_result.txt") || std::filesystem::file_size("test_result.txt") != 1024) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });

        std::thread serverThread([&]() {
            P2P::Client server;
            server.SetClientMode(P2P::ClientMode::TCP_Client);

            server.SeekLocalConnection([&]() {
                ports = server.GetConnectionPorts();
                address = server.GetConnectionAddress();

                ready.store(true);
            });

            while (!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            while (!std::filesystem::exists("test_result.txt") || std::filesystem::file_size("test_result.txt") != 1024) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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