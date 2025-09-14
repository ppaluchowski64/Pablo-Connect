#ifndef SCANNER_H
#define SCANNER_H

#include <asio/awaitable.hpp>
#include <AsioCommon.h>
#include <AwaitableFlag.h>
#include <vector>

struct DeviceInfo {

};

class LanDeviceScanner {
public:
    LanDeviceScanner();

    static void BeginScan();
    static void EndScan();
    static std::vector<DeviceInfo> GetDiscoveredDevices();

private:
    static asio::awaitable<void> Co_JoinMulticastGroup();
    static asio::awaitable<void> Co_LeaveMulticastGroup();
    static asio::awaitable<void> Co_SendProbes();
    static asio::awaitable<void> Co_ReceiveResponses();

    enum class DeviceScannerPackageType : uint16_t {
        None = 0
    };

    static LanDeviceScanner* s_instance;

    IOContext m_context;

    AwaitableFlag m_awaitableFlag;
    IOWorkGuard m_workGuard;

    UDPSocket m_senderSocket;
    UDPSocket m_receiverSocket;

    std::thread m_contextThread;
    bool m_isScanning{false};

    std::vector<DeviceInfo> m_discoveredDevices;
};

#endif //SCANNER_H
