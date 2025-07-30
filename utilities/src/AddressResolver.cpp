#include <AddressResolver.h>

bool AddressResolver::IsAddressPublic(const IPAddress& address) {
    if (address.is_v4()) {
        return IsAddressPublic(address.to_v4());
    }

    if (address.is_v6()) {
        return IsAddressPublic(address.to_v6());
    }

    return false;
}

bool AddressResolver::IsAddressPublic(const asio::ip::address_v6& address) {
    return (address.to_bytes()[0] & 0xE0) == 0x20;
}

bool AddressResolver::IsAddressPublic(const asio::ip::address_v4& address) {
    if (IsAddressPrivate(address)) {
        return false;
    }

    const uint32_t ip = address.to_uint();
    const uint8_t b1 = (ip >> 24) & 0xFF;
    const uint8_t b2 = (ip >> 16) & 0xFF;

    if (b1 == 127)
        return false;

    if (b1 == 169 && b2 == 254)
        return false;

    if (b1 == 100 && (b2 >= 64 && b2 <= 127))
        return false;

    if (b1 == 192 && b2 == 0 && ((ip >> 8) & 0xFF) == 2)
        return false;
    if (b1 == 198 && b2 == 51 && ((ip >> 8) & 0xFF) == 100)
        return false;
    if (b1 == 203 && b2 == 0 && ((ip >> 8) & 0xFF) == 113)
        return false;

    if (b1 >= 224 && b1 <= 239)
        return false;

    if (b1 >= 240)
        return false;

    if (address == asio::ip::address_v4::broadcast() || address == asio::ip::address_v4::any())
        return false;

    return true;
}

bool AddressResolver::IsAddressPrivate(const IPAddress& address) {
    if (address.is_v4()) {
        return IsAddressPrivate(address.to_v4());
    }

    if (address.is_v6()) {
        return IsAddressPrivate(address.to_v6());
    }

    return false;
}

bool AddressResolver::IsAddressPrivate(const asio::ip::address_v6& address) {
    return (address.to_bytes()[0] & 0xFE) == 0xFC;
}

bool AddressResolver::IsAddressPrivate(const asio::ip::address_v4& address) {
    const uint32_t ip = address.to_uint();
    const uint8_t b1 = (ip >> 24) & 0xFF;
    const uint8_t b2 = (ip >> 16) & 0xFF;

    if (b1 == 10)
        return true;
    if (b1 == 172 && (b2 >= 16 && b2 <= 31))
        return true;
    if (b1 == 192 && b2 == 168)
        return true;

    return false;
}

IPAddress AddressResolver::GetPrivateIPv4() {
    try {
        asio::io_context ioContext;
        asio::ip::tcp::resolver resolver(ioContext);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v4() && IsAddressPrivate(entryAddress)) {
                return entryAddress;
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    Debug::LogError("No address found");
    return {};
}

IPAddress AddressResolver::GetPrivateIPv6() {
    try {
        asio::io_context ctx;
        asio::ip::tcp::resolver resolver(ctx);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v6() && IsAddressPrivate(entryAddress)) {
                return entryAddress;
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    Debug::LogError("No address found");
    return {};
}