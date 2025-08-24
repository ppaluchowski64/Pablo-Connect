#include <P2P/Client.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <AddressResolver.h>
#include <array>

/*
* TO FIX
* Number higher than 1 may lead to occasional asio errors (TLS mostly, more rarely TCP)
*/
static constexpr int ASIO_THREAD_COUNT = 1;

namespace P2P {

    Client::Client(): m_clientMode(ClientMode::TLS_Client), m_contextWorkGuard(m_context.get_executor()) {
        ZoneScoped;

        HandleIncomingPackages();

        for (int i = 0; i < ASIO_THREAD_COUNT; i++) {
            m_threadPool.emplace_back([this]() {
               m_context.run();
            });
        }
    }

    Client::~Client() {
        ZoneScoped;

        m_contextWorkGuard.reset();
        m_destroyThreads = true;

        DestroyContext();

        for (auto& thread : m_threadPool) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // void Client::SeekConnection(const ConnectionCallbackData callbackData) {
    //     ZoneScoped;
    //     if (m_clientMode == ClientMode::TCP_Client) {
    //         CreateTCPConnection();
    //     } else {
    //         CreateTLSConnection(true);
    //     }
    //
    //     m_connection->Seek(callbackData);
    // }

    void Client::SeekLocalConnection(const std::function<void(void)> connectionSeekCallback, const std::function<void(void)> callback) {
        ZoneScoped;

        const IPAddress ipAddress = AddressResolver::GetPrivateIPv4();

        if (ipAddress == IPAddress{}) {
            return;
        }

        if (m_clientMode == ClientMode::TCP_Client) {
            CreateTCPConnection();
        } else {
            CreateTLSConnection(true);
        }

        m_connection->Seek(ipAddress, {0, 0}, connectionSeekCallback, callback);
    }


    void Client::Connect(const IPAddress address, const std::array<uint16_t, 2> ports, const std::function<void(void)> callback) {
        ZoneScoped;
        if (m_clientMode == ClientMode::TCP_Client) {
            CreateTCPConnection();
        } else {
            CreateTLSConnection(false);
        }

        m_connection->Start(address, ports, callback);
    }

    void Client::Disconnect() const {
        ZoneScoped;
        if (m_connection == nullptr || m_connection->GetConnectionState() != ConnectionState::CONNECTED) {
            return;
        }

        m_connection->Disconnect();
    }

    void Client::Send(std::unique_ptr<Package<MessageType>>&& message) const {
        ZoneScoped;
        if (m_connection == nullptr || m_connection->GetConnectionState() != ConnectionState::CONNECTED) {
            return;
        }

        m_connection->Send(std::move(message));
    }

    void Client::RequestFile(const std::string& requestedFilePath, const std::string& fileName) const {
        ZoneScoped;
        if (m_connection == nullptr || m_connection->GetConnectionState() != ConnectionState::CONNECTED) {
            return;
        }

        m_connection->RequestFile(requestedFilePath, fileName);
    }

    void Client::SetClientMode(const ClientMode mode) {
        ZoneScoped;
        m_clientMode = mode;
    }

    ClientMode Client::GetClientMode() const {
        ZoneScoped;
        return m_clientMode;
    }

    ConnectionState Client::GetConnectionState() const {
        ZoneScoped;
        if (m_connection == nullptr || m_connection->GetConnectionState() != ConnectionState::CONNECTED) {
            return ConnectionState::DISCONNECTED;
        }

        return m_connection->GetConnectionState();
    }

    IPAddress Client::GetConnectionAddress() const {
        ZoneScoped;
        return m_connection->GetAddress();
    }

    std::array<uint16_t, 2> Client::GetConnectionPorts() const {
        ZoneScoped;
        return m_connection->GetPorts();
    }

    void Client::AddHandler(MessageType type, std::function<void(std::unique_ptr<PackageIn<MessageType>>)> handler) {
        ZoneScoped;
        m_handlers[static_cast<size_t>(type)] = handler;
    }

    void Client::CreateTLSConnection(const bool isServer) {
        ZoneScoped;
        static const std::filesystem::path certificatePath = "./certificates/";

        if (!TLS::CertificateManager::IsCertificateValid(certificatePath)) {
            TLS::CertificateManager::GenerateCertificate(certificatePath);
        }

        if (m_sslContext == nullptr) {
            m_sslContext = TLSConnection<MessageType>::CreateSSLContext(certificatePath, isServer);
        }

        m_connection = TLSConnection<MessageType>::Create(m_context, m_sslContext, m_packagesIn);

    }

    void Client::CreateTCPConnection() {
        ZoneScoped;
        m_connection = TCPConnection<MessageType>::Create(m_context, m_packagesIn);
    }

    void Client::HandleIncomingPackages() {
        constexpr int executeThreadCount = 1;
        for (int i = 0; i < executeThreadCount; ++i) {
            m_threadPool.emplace_back([this]() {
                moodycamel::ConsumerToken token(m_packagesIn);
                while (!m_destroyThreads) {
                     while (m_connection != nullptr && m_connection->GetConnectionState() == ConnectionState::CONNECTED) {
                        if (std::unique_ptr<PackageIn<MessageType>> package; m_packagesIn.try_dequeue(token, package)) {\
                            m_handlers[package->Package->GetHeader().type](std::move(package));
                        }

                        if (m_packagesIn.size_approx() == 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                     }

                     std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
        }
    }

    void Client::DestroyContext() {
        ZoneScoped;
        if (m_connection == nullptr || m_connection->GetConnectionState() != ConnectionState::CONNECTED) {
            return;
        }

        m_connection->DestroyContext();
    }
}
