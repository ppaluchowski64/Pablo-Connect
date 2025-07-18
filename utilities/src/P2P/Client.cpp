#include <P2P/Client.h>
#include <iostream>


namespace P2P {
    void Client::Connect(const ConnectionCallbackData callbackData) {
        if (m_clientRole == ClientRole::Server) EnableAcceptors();
        else                                    DisableAcceptors();

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            ConnectTCP(callbackData);
            break;
        case ClientMode::TLS_Client:
            ConnectTLS(callbackData);
            break;
        default:
            break;
        }
    }

    void Client::Disconnect() {
        if (m_clientRole == ClientRole::Server) DisableAcceptors();

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
        return m_clientMode;
    }

    void Client::SetClientMode(const ClientMode mode) {
        m_clientMode = mode;

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            CreateTCPConnection();
            break;
        case ClientMode::TLS_Client:
            CreateTLSConnection();
            break;
        default:
            break;
        }
    }

    constexpr ConnectionMode Client::GetConnectionMode() const {
        return m_connectionMode;
    }

    constexpr void Client::SetConnectionMode(const ConnectionMode mode) {
        m_connectionMode = mode;
    }

    void Client::AddHandler(MessageType type, const HandlerFunc func) {
        m_handlers[static_cast<size_t>(type)] = func;
    }

    Client::Client(const ClientRole role, const IPAddress& address, const uint16_t serverPort, const uint16_t fileStreamPort) :
    m_connectionEndpoint(TCPEndpoint(address, serverPort)), m_fileStreamEndpoint(TCPEndpoint(address, fileStreamPort)),
        m_connectionAcceptor(m_context), m_fileStreamAcceptor(m_context),
        m_serverPort(serverPort), m_serverFileStreamPort(fileStreamPort), m_clientRole(role), m_contextWorkGuard(asio::make_work_guard(m_context)),
        m_packagesIn(100000) {

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                m_context.run();
            });
        }
    }

    Client::~Client() {
        m_context.stop();
        Disconnect();

        for (auto& thread : m_threadPool) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void Client::CreateTLSConnection() {
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
        m_tcpConnection = TCPConnection<MessageType>::Create(m_context, m_packagesIn);
    }

    void Client::ConnectTCP(const ConnectionCallbackData callbackData) {
        if (m_tcpConnection == nullptr) {
            CreateTCPConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tcpConnection->Start(m_connectionEndpoint, m_fileStreamEndpoint, callbackData);
            break;
        case ClientRole::Server:
            m_tcpConnection->Seek(m_connectionAcceptor, m_fileStreamAcceptor, callbackData);
            break;
        }

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                moodycamel::ConsumerToken token(m_packagesIn);

                while (m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTING) {}
                while (m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTED) {
                    if (std::unique_ptr<PackageIn<MessageType>> packageIn; m_packagesIn.try_dequeue(packageIn)) {
                        m_handlers[packageIn->Package->GetHeader().type](std::move(packageIn->Package));
                    }
                }
            });
        }
    }

    void Client::ConnectTLS(const ConnectionCallbackData callbackData) {
        if (m_tlsConnection == nullptr) {
            CreateTLSConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tlsConnection->Start(m_connectionEndpoint, m_fileStreamEndpoint, callbackData);
            break;
        case ClientRole::Server:
            m_tlsConnection->Seek(m_connectionAcceptor, m_fileStreamAcceptor, callbackData);
            break;
        }

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                moodycamel::ConsumerToken token(m_packagesIn);

                while (m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTING) {}
                while (m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTED) {
                    if (std::unique_ptr<PackageIn<MessageType>> packageIn; m_packagesIn.try_dequeue(packageIn)) {
                        m_handlers[packageIn->Package->GetHeader().type](std::move(packageIn->Package));
                    }
                }
            });
        }
    }

    bool Client::IsTCPConnectionValid() const {
        if (m_tcpConnection == nullptr) {
            return false;
        }

        return m_tcpConnection->GetConnectionState() == ConnectionState::CONNECTED;
    }

    bool Client::IsTLSConnectionValid() const {
        if (m_tlsConnection == nullptr) {
            return false;
        }

        return m_tlsConnection->GetConnectionState() == ConnectionState::CONNECTED;
    }

    void Client::EnableAcceptors() {
        m_connectionAcceptor.open(asio::ip::tcp::v4());
        m_connectionAcceptor.set_option(asio::socket_base::reuse_address(true));
        m_connectionAcceptor.bind(m_connectionEndpoint);
        m_connectionAcceptor.listen();

        m_fileStreamAcceptor.open(asio::ip::tcp::v4());
        m_fileStreamAcceptor.set_option(asio::socket_base::reuse_address(true));
        m_fileStreamAcceptor.bind(m_fileStreamEndpoint);
        m_fileStreamAcceptor.listen();
    }

    void Client::DisableAcceptors() {
        if (m_connectionAcceptor.is_open()) {
            m_connectionAcceptor.close();
        }

        if (m_fileStreamAcceptor.is_open()) {
            m_fileStreamAcceptor.close();
        }
    }
}
