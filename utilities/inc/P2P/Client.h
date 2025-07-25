#ifndef CLIENT_H
#define CLIENT_H

#include <P2P/Common.h>
#include <P2P/Package.h>
#include <P2P/ConnectionParent.h>
#include <P2P/TCPConnection.h>
#include <P2P/TLSConnection.h>
#include <TLS/CertificateManager.h>
#include <UniqueFileNamesGenerator.h>
#include <tracy/Tracy.hpp>

#include <concurrentqueue.h>

namespace P2P {
    enum class MessageType : uint16_t {
        message,
        echo,
        COUNT
    };

    using HandlerFunc = void(*)(std::unique_ptr<Package<MessageType>>);

    enum class ClientMode : uint8_t {
        None,
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
        Client();
        ~Client();

        void Connect(std::string&& address, std::array<uint16_t, 2> ports, ConnectionCallbackData callbackData = {nullptr, nullptr});
        void Disconnect() const;

        void Send(std::unique_ptr<Package<MessageType>>&& message) const;
        template<StdLayoutOrVecOrString... Args>
        void Send(MessageType type, Args&&... args) {
            ZoneScoped;
            auto package = Package<MessageType>::CreateUnique(type, std::forward<Args>(args)...);
            Send(std::move(package));
        }
        void RequestFile(const std::string& requestedFilePath, const std::string& fileName) const;

        void SetClientMode(ClientMode mode);
        void SetClientRole(ClientRole role);
        constexpr void SetConnectionMode(ConnectionMode mode);


        NO_DISCARD constexpr ConnectionMode GetConnectionMode() const;
        NO_DISCARD constexpr ClientRole GetClientRole() const;
        NO_DISCARD constexpr ClientMode GetClientMode() const;
        NO_DISCARD ConnectionState GetConnectionState() const;

        void AddHandler(MessageType type, HandlerFunc func);


    private:
        constexpr static void DefaultCallback() {}
        void CreateTLSConnection();
        void CreateTCPConnection();
        void ConnectTCP(std::string&& address, std::array<uint16_t, 2> ports, ConnectionCallbackData callbackData);
        void ConnectTLS(std::string&& address, std::array<uint16_t, 2> ports, ConnectionCallbackData callbackData);
        bool IsTCPConnectionValid() const;
        bool IsTLSConnectionValid() const;

        IOContext                   m_context;
        std::shared_ptr<SSLContext> m_sslContext{nullptr};

        std::shared_ptr<TCPConnection<MessageType>> m_tcpConnection{nullptr};
        std::shared_ptr<TLSConnection<MessageType>> m_tlsConnection{nullptr};
        moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<MessageType>>> m_packagesIn;

        ClientMode  m_clientMode        = ClientMode::None;
        ConnectionMode m_connectionMode = ConnectionMode::LocalNetwork;
        ClientRole m_clientRole         = ClientRole::Client;

        asio::executor_work_guard<asio::io_context::executor_type> m_contextWorkGuard;
        std::vector<std::thread> m_threadPool;

        HandlerFunc m_handlers[static_cast<uint64_t>(MessageType::COUNT)] = {nullptr};

    };
}

#endif //CLIENT_H
