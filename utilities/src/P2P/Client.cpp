#include <P2P/Client.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <array>


namespace P2P {
    void Client::Connect(std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) {
        ZoneScoped;

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            ConnectTCP(std::move(address), ports, callbackData);
            break;
        case ClientMode::TLS_Client:
            ConnectTLS(std::move(address), ports, callbackData);
            break;
        default:
            break;
        }
    }

    void Client::Disconnect() const {
        ZoneScoped;

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            if (!IsTCPConnectionValid()) {
                return;
            }

            m_tcpConnection->Disconnect();
            break;
        case ClientMode::TLS_Client:
            if (!IsTLSConnectionValid()) {
                return;
            }

            m_tlsConnection->Disconnect();
            break;
        default:
            break;
        }
    }

    void Client::Send(std::unique_ptr<Package<MessageType>>&& message) const {
        ZoneScoped;

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            if (!IsTCPConnectionValid()) {
                Debug::LogError("TCP connection invalid");
                return;
            }

            m_tcpConnection->Send(std::move(message));
            break;
        case ClientMode::TLS_Client:
            if (!IsTLSConnectionValid()) {
                Debug::LogError("TLS connection invalid");
                return;
            }

            m_tlsConnection->Send(std::move(message));
            break;
        default:
            break;
        }
    }

    void Client::RequestFile(const std::string& requestedFilePath, const std::string& fileName) const {
        ZoneScoped;

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            if (!IsTCPConnectionValid()) {
                Debug::LogError("TCP connection invalid");
                return;
            }

            m_tcpConnection->RequestFile(requestedFilePath, fileName);
            break;
        case ClientMode::TLS_Client:
            if (!IsTLSConnectionValid()) {
                Debug::LogError("TLS connection invalid");
                return;
            }

            m_tlsConnection->RequestFile(requestedFilePath, fileName);
            break;
        default:
            break;
        }
    }

    NO_DISCARD constexpr ClientMode Client::GetClientMode() const {
        ZoneScoped;
        return m_clientMode;
    }

    void Client::SetClientMode(const ClientMode mode) {
        ZoneScoped;
        m_clientMode = mode;
    }

    void Client::SetClientRole(const ClientRole role) {
        m_clientRole = role;
    }

    constexpr ClientRole Client::GetClientRole() const {
        return m_clientRole;
    }

    constexpr ConnectionMode Client::GetConnectionMode() const {
        ZoneScoped;
        return m_connectionMode;
    }

    constexpr void Client::SetConnectionMode(const ConnectionMode mode) {
        ZoneScoped;
        m_connectionMode = mode;
    }

    ConnectionState Client::GetConnectionState() const {
        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            if (m_tcpConnection == nullptr) {
                return ConnectionState::DISCONNECTED;
            }

            return m_tcpConnection->GetConnectionState();
        case ClientMode::TLS_Client:
            if (m_tlsConnection == nullptr) {
                return ConnectionState::DISCONNECTED;
            }

            return m_tlsConnection->GetConnectionState();
        default:
            return ConnectionState::DISCONNECTED;
        }
    }

    void Client::AddHandler(MessageType type, const HandlerFunc func) {
        ZoneScoped;
        m_handlers[static_cast<size_t>(type)] = func;
    }

    Client::Client() : m_packagesIn(100000), m_clientMode(ClientMode::TLS_Client), m_clientRole(ClientRole::Client), m_contextWorkGuard(asio::make_work_guard(m_context)){
        ZoneScoped;

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                tracy::SetThreadName("asio::io_context thread ");
                m_context.run();
            });
        }
    }

    Client::~Client() {
        ZoneScoped;

        Disconnect();
        m_context.stop();

        for (auto& thread : m_threadPool) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void Client::CreateTLSConnection() {
        ZoneScoped;
        static const std::filesystem::path certificatePath = "./certificates/";

        if (!TLS::CertificateManager::IsCertificateValid(certificatePath)) {
            TLS::CertificateManager::GenerateCertificate(certificatePath);
        }

        if (m_sslContext == nullptr) {
            const bool isServer = m_clientRole == ClientRole::Server;
            m_sslContext = TLSConnection<MessageType>::CreateSSLContext(certificatePath, isServer);
        }

        m_tlsConnection = TLSConnection<MessageType>::Create(m_context, m_sslContext, m_packagesIn);
    }

    void Client::CreateTCPConnection() {
        ZoneScoped;
        m_tcpConnection = TCPConnection<MessageType>::Create(m_context, m_packagesIn);
    }

    void Client::ConnectTCP(std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) {
        ZoneScoped;
        if (m_tcpConnection == nullptr) {
            CreateTCPConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tcpConnection->Start(std::move(address), ports, callbackData);
            break;
        case ClientRole::Server:
            m_tcpConnection->Seek(std::move(address), ports, callbackData);
            break;
        }

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                tracy::SetThreadName("TCP package handler worker ");
                moodycamel::ConsumerToken token(m_packagesIn);

                while (m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                while (m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTED) {
                    if (std::unique_ptr<PackageIn<MessageType>> packageIn; m_packagesIn.try_dequeue(packageIn)) {
                        m_handlers[packageIn->Package->GetHeader().type](std::move(packageIn->Package));
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });
        }
    }

    void Client::ConnectTLS(std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) {
        ZoneScoped;
        if (m_tlsConnection == nullptr) {
            CreateTLSConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tlsConnection->Start(std::move(address), ports, callbackData);
            break;
        case ClientRole::Server:
            m_tlsConnection->Seek(std::move(address), ports, callbackData);
            break;
        }

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                tracy::SetThreadName("TLS package handler worker ");
                moodycamel::ConsumerToken token(m_packagesIn);

                while (m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTING) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                while (m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTED) {
                    if (std::unique_ptr<PackageIn<MessageType>> packageIn; m_packagesIn.try_dequeue(packageIn)) {
                        m_handlers[packageIn->Package->GetHeader().type](std::move(packageIn->Package));
                    }  else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });
        }
    }

    bool Client::IsTCPConnectionValid() const {
        ZoneScoped;
        if (m_tcpConnection == nullptr) {
            return false;
        }

        return m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTED;
    }

    bool Client::IsTLSConnectionValid() const {
        ZoneScoped;
        if (m_tlsConnection == nullptr) {
            return false;
        }

        return m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTED;
    }
}
