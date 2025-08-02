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
        TCP_Client,
        TLS_Client
    };

    class Client {
    public:
        Client();
        ~Client();

        //void SeekConnection(ConnectionCallbackData callbackData = {nullptr, nullptr});
        void SeekLocalConnection(ConnectionSeekCallbackData connectionSeekCallbackData = {nullptr, nullptr}, ConnectionCallbackData callbackData = {nullptr, nullptr});
        void Connect(IPAddress address, std::array<uint16_t, 2> ports, ConnectionCallbackData callbackData = {nullptr, nullptr});
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

        NO_DISCARD ClientMode GetClientMode() const;
        NO_DISCARD ConnectionState GetConnectionState() const;
        NO_DISCARD IPAddress GetConnectionAddress() const;
        NO_DISCARD std::array<uint16_t, 2> GetConnectionPorts() const;

        void AddHandler(MessageType type, HandlerFunc func);


    private:
        void CreateTLSConnection(bool isServer);
        void CreateTCPConnection();

        IOContext                   m_context;
        std::shared_ptr<SSLContext> m_sslContext{nullptr};

        std::shared_ptr<ConnectionParent<MessageType>> m_connection{nullptr};
        moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<MessageType>>> m_packagesIn;

        ClientMode  m_clientMode;

        asio::executor_work_guard<asio::io_context::executor_type> m_contextWorkGuard;
        std::vector<std::thread> m_threadPool;

        HandlerFunc m_handlers[static_cast<uint64_t>(MessageType::COUNT)] = {nullptr};

    };
}

#endif //CLIENT_H
