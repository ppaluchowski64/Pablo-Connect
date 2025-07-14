#ifndef CLIENT_H
#define CLIENT_H

#include <TCP_TLS_COMMON/Common.h>
#include <TCP_TLS_COMMON/Package.h>
#include <TCP/Connection.h>
#include <TLS/Connection.h>
#include <TLS/CertificateManager.h>
#include <UniqueFileNamesGenerator.h>

namespace P2P {
    enum class MessageType : uint16_t {
        message,
        echo,
        COUNT
    };

    using HandlerFunc = void(*)(std::unique_ptr<Package<MessageType>>);

    enum class ClientMode : uint8_t {
        TCP_Client,
        TLS_Client
    };

    enum class ClientRole : uint8_t {
        Client,
        Server
    };

    enum class ConnectionMode : uint8_t {
        LocalNetwork,
        GlobalNetwork
    };

    class Client {
    public:
        constexpr void WaitForConnection();
        NO_DISCARD constexpr ClientMode GetClientMode() const;
        constexpr void SetClientMode(ClientMode mode);
        NO_DISCARD constexpr ConnectionMode GetConnectionMode() const;
        constexpr void SetConnectionMode(ConnectionMode mode);
        constexpr void AddHandler(MessageType type, HandlerFunc func);


    private:
        void CreateTLSConnection();
        void CreateTCPConnection();

        IOContext                   m_context;
        std::shared_ptr<SSLContext> m_sslContext{nullptr};

        std::shared_ptr<TCP::Connection<MessageType>> m_tcpConnection{nullptr};
        std::shared_ptr<TLS::Connection<MessageType>> m_tlsConnection{nullptr};

        ts::deque<TLS::PackageIn<MessageType>> m_tlsPackagesIn;
        ts::deque<TCP::PackageIn<MessageType>> m_tcpPackagesIn;

        ClientMode  m_clientMode        = ClientMode::TLS_Client;
        ConnectionMode m_connectionMode = ConnectionMode::LocalNetwork;
        ClientRole m_clientRole         = ClientRole::Client;

        HandlerFunc m_handlers[MessageType::COUNT] = {nullptr};

    };
}

#endif //CLIENT_H
