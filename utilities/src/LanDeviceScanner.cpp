// #include <LanDeviceScanner.h>
// #include <chrono>
// #include <AddressResolver.h>
//
// LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};
//
// LanDeviceScanner::LanDeviceScanner() : m_socket(m_context), m_runningSignal(m_context.get_executor()), m_workGuard(m_context.get_executor()) {
// }
//
// void LanDeviceScanner::Join() {
//     if (s_instance == nullptr) {
//         s_instance = new LanDeviceScanner();
//     }
//
//     const UDPEndpoint endpoint(asio::ip::udp::v4(), MULTICAST_PORT);
//     const IPAddress multicastAddress = asio::ip::make_address(MULTICAST_ADDRESS);
//
//     s_instance->m_socket.open(endpoint.protocol());
//     s_instance->m_socket.set_option(UDPSocket::reuse_address(true));
//     s_instance->m_socket.bind(endpoint);
//     s_instance->m_socket.set_option(asio::ip::multicast::join_group(multicastAddress));
//
//     s_instance->m_running = true;
//     s_instance->m_runningSignal.Signal();
// }
//
// void LanDeviceScanner::Leave() {
//     if (s_instance == nullptr) {
//         s_instance = new LanDeviceScanner();
//     }
//
//     s_instance->m_socket.cancel();
//     s_instance->m_socket.shutdown(asio::socket_base::shutdown_both);
//     s_instance->m_socket.close();
//
//     s_instance->m_running = false;
//     s_instance->m_runningSignal.Reset();
// }
//
// std::vector<DeviceInfo> LanDeviceScanner::GetDevices() {
//     if (s_instance == nullptr) {
//         s_instance = new LanDeviceScanner();
//     }
//
//     return s_instance->m_devices;
// }
//
// asio::awaitable<void> LanDeviceScanner::CoSender() {
//     try {
//         asio::steady_timer timer(m_context.get_executor());
//
//         while (true) {
//             const Package<DeviceScannerPackageType> package = Package<DeviceScannerPackageType>::Create(
//                 DeviceScannerPackageType::DevicePulse,
//                 std::string("place_holder"),                // Device name
//                 AddressResolver::GetPrivateIPv4().to_v4(),      // Device address
//                 0                                               // Connection port
//             );
//
//             const PackageHeader header = package.GetHeaderCopy();
//             const std::vector<asio::const_buffer> buffers = {
//                 asio::const_buffer(&header, sizeof(header)),
//                 asio::const_buffer(package.GetRawBody(), header.size)
//             };
//
//             while (m_running) {
//                 co_await m_socket.async_send(buffers, asio::use_awaitable);
//
//                 timer.expires_after(std::chrono::milliseconds(500));
//                 co_await timer.async_wait(asio::use_awaitable);
//             }
//
//             co_await m_runningSignal.Wait();
//         }
//     } catch (const asio::error_code& errorCode) {
//         Debug::LogError(errorCode.message());
//         co_return;
//     }
// }
//
// asio::awaitable<void> LanDeviceScanner::CoReceiver() {
//
// }
