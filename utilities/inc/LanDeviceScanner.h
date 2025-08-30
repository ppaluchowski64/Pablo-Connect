#ifndef LAN_DEVICE_SCANNER_H
#define LAN_DEVICE_SCANNER_H

#include <AsioCommon.h>
#include <P2P/Package.h>
#include <AwaitableFlag.h>
#include <string>

constexpr static int MULTICAST_PORT = 30052;
const static std::string MULTICAST_ADDRESS = "239.255.0.1";

enum class DeviceScannerPackageType : uint16_t {
    DevicePulse,
    DeviceEndPulse
};

struct DeviceInfo {
    uint32_t    id;
    std::string name;
    IPAddress   address;
    uint16_t    port;
};

class LanDeviceScanner  {
public:
    LanDeviceScanner();

    static void Join();
    static void Leave();
    static std::vector<DeviceInfo> GetDevices();

private:
    asio::awaitable<void> CoSender();
    asio::awaitable<void> CoReceiver();

    static LanDeviceScanner* s_instance;

    IOContext m_context;
    UDPSocket m_socket;

    AwaitableFlag     m_runningSignal;
    std::atomic<bool> m_running;

    asio::executor_work_guard<asio::io_context::executor_type> m_workGuard;

    std::vector<DeviceInfo>                m_devices;
    std::unordered_map<uint32_t, uint32_t> m_lastDeviceResponse;

    uint32_t m_currentDeviceID{0};
};

#endif //LAN_DEVICE_SCANNER_H
