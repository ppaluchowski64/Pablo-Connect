#include <Scanner.h>
#include <DebugLog.h>
#include <Package.h>

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_awaitableFlag(m_context.get_executor()), m_workGuard(asio::make_work_guard(m_context.get_executor())), m_senderSocket(m_context), m_receiverSocket(m_context) {
    m_contextThread = std::thread([this]() {
        m_context.run();
    });
}

void LanDeviceScanner::EndScan() {
    if (s_instance == nullptr) {
        return;
    }

    if (!s_instance->m_isScanning) {
        return;
    }

    asio::post(s_instance->m_context, [](){
        asio::co_spawn(s_instance->m_context, Co_LeaveMulticastGroup(), asio::detached);
    });
}

void LanDeviceScanner::BeginScan() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    if (s_instance->m_isScanning) {
        return;
    }

    asio::post(s_instance->m_context, [](){
        asio::co_spawn(s_instance->m_context, Co_JoinMulticastGroup(), asio::detached);
    });
}

std::vector<DeviceInfo> LanDeviceScanner::GetDiscoveredDevices() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    return s_instance->m_discoveredDevices;
}

asio::awaitable<void> LanDeviceScanner::Co_JoinMulticastGroup() {
    try {
        s_instance->m_receiverSocket.open(asio::ip::udp::v4());
        s_instance->m_receiverSocket.set_option(asio::socket_base::reuse_address(true));
        s_instance->m_receiverSocket.bind(UDPEndpoint(asio::ip::udp::v4(), DEVICE_DISCOVERY_MULTICAST_PORT));
        s_instance->m_receiverSocket.set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS));
        s_instance->m_receiverSocket.set_option(asio::ip::multicast::enable_loopback(false));

        s_instance->m_senderSocket.open(asio::ip::udp::v4());
        s_instance->m_senderSocket.set_option(asio::ip::multicast::hops(1));
        s_instance->m_senderSocket.set_option(asio::ip::multicast::enable_loopback(false));

        s_instance->m_isScanning = true;
        asio::co_spawn(s_instance->m_context, Co_SendProbes(), asio::detached);
        asio::co_spawn(s_instance->m_context, Co_ReceiveResponses(), asio::detached);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_LeaveMulticastGroup() {
    try {
        s_instance->m_receiverSocket.set_option(asio::ip::multicast::leave_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS));

        s_instance->m_receiverSocket.cancel();
        s_instance->m_senderSocket.cancel();

        s_instance->m_receiverSocket.close();
        s_instance->m_senderSocket.close();

        s_instance->m_isScanning = false;

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_SendProbes() {
    try {
        Package<DeviceScannerPackageType> package = Package<DeviceScannerPackageType>::Create(DeviceScannerPackageType::None);
        const std::vector<asio::const_buffer> buffers = {
            asio::const_buffer(&package.GetHeader(), sizeof(PackageHeader)),
            asio::const_buffer(package.GetRawBody(), package.GetHeader().size)
        };

        const UDPEndpoint multicastEndpoint(DEVICE_DISCOVERY_MULTICAST_ADDRESS, DEVICE_DISCOVERY_MULTICAST_PORT);

        do {
            co_await s_instance->m_senderSocket.async_send_to(buffers, multicastEndpoint);

            asio::steady_timer timer(s_instance->m_context);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);

        } while (s_instance->m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }
}

asio::awaitable<void> LanDeviceScanner::Co_ReceiveResponses() {
    try {
        PackageHeader header{};

        do {
            asio::mutable_buffer headerBuffer(&header, sizeof(PackageHeader));

            UDPEndpoint senderEndpoint;
            co_await s_instance->m_receiverSocket.async_receive_from(headerBuffer, senderEndpoint);

            Package<DeviceScannerPackageType> package(header);

            asio::mutable_buffer packageBuffer(package.GetRawBody(), header.size);
            co_await s_instance->m_receiverSocket.async_receive_from(packageBuffer, senderEndpoint);

            Debug::Log("Received");

        } while (s_instance->m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }
}


