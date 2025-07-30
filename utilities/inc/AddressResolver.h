#ifndef ADDRESS_RESOLVER_H
#define ADDRESS_RESOLVER_H

#include <P2P/Common.h>
#include <DebugLog.h>

class AddressResolver final {
public:
    static bool IsAddressPublic(const asio::ip::address_v6& address);
    static bool IsAddressPublic(const asio::ip::address_v4& address);
    static bool IsAddressPublic(const IPAddress& address);

    static bool IsAddressPrivate(const asio::ip::address_v6& address);
    static bool IsAddressPrivate(const asio::ip::address_v4& address);
    static bool IsAddressPrivate(const IPAddress& address);

    static IPAddress GetPrivateIPv4();
    static IPAddress GetPrivateIPv6();
};

#endif //ADDRESS_RESOLVER_H
