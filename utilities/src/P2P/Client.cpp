#include <P2P/Client.h>
#include <iostream>

void cc() {

}

namespace P2P {
    void Client::Connect() {
        if (m_clientRole == ClientRole::Server) {
            m_connectionAcceptor.open(asio::ip::tcp::v4());
            m_connectionAcceptor.set_option(asio::socket_base::reuse_address(true));
            m_connectionAcceptor.bind(m_connectionEndpoint);
            m_connectionAcceptor.listen();

            m_fileStreamAcceptor.open(asio::ip::tcp::v4());
            m_fileStreamAcceptor.set_option(asio::socket_base::reuse_address(true));
            m_fileStreamAcceptor.bind(m_fileStreamEndpoint);
            m_fileStreamAcceptor.listen();

        } else {
            if (m_connectionAcceptor.is_open()) {
                m_connectionAcceptor.close();
            }

            if (m_fileStreamAcceptor.is_open()) {
                m_fileStreamAcceptor.close();
            }
        }

        switch (m_clientMode) {
        case ClientMode::TCP_Client:
            ConnectTCP();
            break;
        case ClientMode::TLS_Client:
            ConnectTLS();
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
        m_serverPort(serverPort), m_serverFileStreamPort(fileStreamPort), m_clientRole(role), m_contextWorkGuard(asio::make_work_guard(m_context)){

        for (int i = 0; i < 1; i++) {
            m_threadPool.emplace_back([this]() {
                m_context.run();
            });
        }
    }

    Client::~Client() {
        m_context.stop();

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
            m_sslContext = TLS::Connection<MessageType>::CreateSSLContext(certificatePath, isServer);
        }

        m_tlsConnection = TLS::Connection<MessageType>::Create(m_context, m_sslContext, m_tlsPackagesIn);
    }

    void Client::CreateTCPConnection() {
        m_tcpConnection = TCP::Connection<MessageType>::Create(m_context, m_tcpPackagesIn);
    }

    void Client::ConnectTCP() {
        if (m_tcpConnection == nullptr) {
            CreateTCPConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tcpConnection->Start(m_connectionEndpoint, m_fileStreamEndpoint, cc);
            break;
        case ClientRole::Server:
            m_tcpConnection->Seek(m_connectionAcceptor, m_fileStreamAcceptor, cc);
            break;
        }
    }

    void Client::ConnectTLS() {
        if (m_tlsConnection == nullptr) {
            CreateTLSConnection();
        }

        switch (m_clientRole) {
        case ClientRole::Client:
            m_tlsConnection->Start(m_connectionEndpoint, m_fileStreamEndpoint, cc);
            break;
        case ClientRole::Server:
            m_tlsConnection->Seek(m_connectionAcceptor, m_fileStreamAcceptor, cc);
            break;
        }
    }
}
