#include <gtest/gtest.h>
#include <P2P/Client.h>


// TEST(TCP_Test, DataTransferTest_SimplePackage) {
//     std::atomic<std::array<int, 2>> ports;
//     std::atomic<asio::ip::address> address{};
//
//     std::thread clientThread([&]() {
//         P2P::Client client;
//
//         while (address.load() == asio::ip::address{}) {
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
//     });
//
//     std::thread serverThread([&]() {
//         P2P::Client server;
//
//         server.SeekLocalConnection();
//
//
//     });
//
//     serverThread.join();
// }