#include <P2P/Client.h>

namespace P2P {
    constexpr void Client::WaitForConnection() {
        switch (m_clientMode) {
            case ClientMode::TCP_Client:
                CreateTCPConnection();
                break;
            case ClientMode::TLS_Client:
                CreateTLSConnection();
                break;
        }
    }

    NO_DISCARD constexpr ClientMode Client::GetClientMode() const {
        return m_clientMode;
    }

    constexpr void Client::SetClientMode(const ClientMode mode) {
        m_clientMode = mode;
    }

    constexpr ConnectionMode Client::GetConnectionMode() const {
        return m_connectionMode;
    }

    constexpr void Client::SetConnectionMode(const ConnectionMode mode) {
        m_connectionMode = mode;
    }

    constexpr void Client::AddHandler(MessageType type, const HandlerFunc func) {
        m_handlers[static_cast<size_t>(type)] = func;
    }

    void Client::CreateTLSConnection() {
        static constexpr std::filesystem::path certificatePath = "./certificates/";

        if (TLS::CertificateManager::IsCertificateValid(certificatePath)) {
            TLS::CertificateManager::GenerateCertificate(certificatePath);
        }

        if (m_sslContext == nullptr) {
            constexpr bool isServer = m_clientRole == ClientRole::Server;
            m_sslContext = TLS::Connection<MessageType>::CreateSSLContext(certificatePath, isServer);
        }

        m_tlsConnection = TLS::Connection<MessageType>::Create(m_context, m_sslContext, m_tlsPackagesIn);
    }

    void Client::CreateTCPConnection() {
        m_tcpConnection = TCP::Connection<MessageType>::Create(m_context, m_tcpPackagesIn);
    }
}
