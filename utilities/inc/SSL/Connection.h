#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <utility>
#include <TSDeque.h>
#include <DebugLog.h>
#include <memory>
#include <filesystem>
#include <asio/ssl/context_base.hpp>
#include <SSL/Package.h>
#include <asio/buffer.hpp>
#include <UniqueFileNamesGenerator.h>

typedef asio::io_context IOContext;
typedef asio::ssl::context SSLContext;
typedef asio::ip::tcp::socket TCPSocket;
typedef asio::ssl::stream<asio::ip::tcp::socket> SSLSocket;
typedef asio::ip::tcp::endpoint Endpoint;
typedef asio::ip::tcp::acceptor Acceptor;
typedef asio::ssl::context::method SSLMethod;
typedef asio::ssl::stream_base SSLStreamBase;

constexpr uint32_t PACKAGES_WARN_THRESHOLD = 100;

template <PackageType T>
class Connection;

template <PackageType T>
class PackageIn final {
public:
    std::unique_ptr<Package<T>> package;
    std::shared_ptr<Connection<T>> connection;
};

template <PackageType T>
class Connection final : public std::enable_shared_from_this<Connection<T>> {
public:
    Connection(IOContext& ioContext, const std::shared_ptr<SSLContext>& sslContext, ts::deque<PackageIn<T>>& inDeque)
    : m_context(ioContext), m_sslContext(sslContext), m_sslSocket(ioContext, *sslContext), m_inDeque(inDeque) { }

    Connection() = delete;

    NO_DISCARD static std::shared_ptr<SSLContext> CreateSSLContext(const std::filesystem::path& path, const bool isServer) {
        std::shared_ptr<SSLContext> ctx = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);
        const std::string keyPath = (path / "privateKey.key").string();
        const std::string certPath = (path / "certificate.crt").string();

        ctx->set_options(
            SSLContext::default_workarounds |
            SSLContext::default_workarounds |
            SSLContext::no_sslv2 |
            SSLContext::no_sslv3 |
            SSLContext::no_tlsv1 |
            SSLContext::no_tlsv1_1
        );

        ctx->use_certificate_chain_file(certPath);
        ctx->use_private_key_file(keyPath, SSLContext::pem);
        ctx->set_verify_mode(asio::ssl::verify_none);

        return ctx;
    }

    static void Start(IOContext& ioContext, std::shared_ptr<SSLContext> sslContext, ts::deque<PackageIn<T>>& inDeque, Endpoint endpoint, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);

        asio::async_connect(connection->m_sslSocket.lowest_layer(), std::initializer_list<Endpoint>({std::move(endpoint)}), [connection, callback](const asio::error_code& errorCode, const asio::ip::tcp::endpoint&) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                connection->Disconnect();
                return;
            }

            connection->m_sslSocket.async_handshake(SSLStreamBase::client, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                    connection->Disconnect();
                    return;
                }

                Debug::Log("Accepted connection to " + connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" + std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()));
                connection-> m_connectionID = s_currentConnectionID++;
                connection->ReceiveMessage();
                callback(connection);
            });
        });
    }

    static void Seek(IOContext& ioContext, std::shared_ptr<SSLContext> sslContext, ts::deque<PackageIn<T>>& inDeque, Acceptor& acceptor, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);

        acceptor.async_accept(connection->m_sslSocket.lowest_layer(), [connection, callback](const asio::error_code& errorCode) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                connection->Disconnect();
                return;
            }

            connection->m_sslSocket.async_handshake(SSLStreamBase::server, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                    connection->Disconnect();
                    return;
                }

                Debug::Log("Accepted connection from " + connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" + std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()));
                connection->m_connectionID = s_currentConnectionID++;
                connection->ReceiveMessage();
                callback(connection);
            });
        });
    }

    void Send(std::unique_ptr<Package<T>>&& package) {
        m_outDeque.push_back(std::move(package));

        if (const size_t dequeSize = m_outDeque.size(); dequeSize > PACKAGES_WARN_THRESHOLD) {
            Debug::LogWarning("Output queue size is growing: " + std::to_string(dequeSize));
        }

        if (!m_sending) {
            m_sending = true;
            SendMessage();
        }
    }

    template <StdLayoutOrVecOrString... Args>
    void Send(T type, PackageFlag flag, Args... args) {
        std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(type, args...);
        package->GetHeader().flags = static_cast<uint8_t>(flag);

        m_outDeque.push_back(std::move(package));
        if (!m_sending) {
            m_sending = true;
            SendMessage();
        }
    }


    // template <StdLayoutOrVecOrString... Args>
    // void SendRequest(T type, std::function<void(std::unique_ptr<Package<T>>)> callback, Args... args) {
    //     uint32_t requestID = m_currentRequestID++;
    //
    //     std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(type, requestID, args);
    //     m_requestCallbacks[requestID] = std::move(callback);
    //     package->GetHeader().flags = PackageFlag::REQUEST;
    //
    //     m_outDeque.push_back(std::move(package));
    //
    //     if (!m_sending) {
    //         m_sending = true;
    //         SendMessage();
    //     }
    // }

    void Disconnect() {
        if (!m_sslSocket.lowest_layer().is_open()) {
            return;
        }

        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        m_sslSocket.async_shutdown([connection](const asio::error_code& errorCode) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
            }

            asio::error_code ec;
            connection->m_sslSocket.lowest_layer().close(ec);

            if (ec) {
                Debug::LogError(ec.message());
            }
        });
    }

    NO_DISCARD uint16_t GetConnectionID() const {
        return m_connectionID;
    }

private:
    void SendMessage() {
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        m_packageOut = m_outDeque.pop_front();
        PackageHeader header = m_packageOut->GetHeader();

        std::vector<asio::const_buffer> buffers = {
            asio::buffer(&header, sizeof(PackageHeader)),
            asio::buffer(m_packageOut->GetRawBody(), header.size)
        };

        asio::async_write(m_sslSocket, buffers, [connection](const asio::error_code& errorCode, const size_t) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                connection->Disconnect();
                return;
            }

            if (connection->m_outDeque.empty()) {
                connection->m_sending = false;
                return;
            }

            connection->SendMessage();
        });
    }

    void HandleFullReceive() {
        PackageHeader header = m_packageIn->GetHeader();
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        asio::mutable_buffer buffer(m_packageIn->GetRawBody() + m_receivedBytes, header.size);

        Debug::Log("Received package: " + std::to_string(header.size));

        asio::async_read(m_sslSocket, buffer, [connection](const asio::error_code& errorCode, const size_t bytes) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                connection->Disconnect();
                return;
            }

            PackageIn<T> package;
            package.package = std::move(connection->m_packageIn);
            package.connection = connection;

            connection->m_inDeque.push_back(std::move(package));
            connection->ReceiveMessage();
        });
    }



    void ReceiveMessage() {
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        m_packageIn = std::make_unique<Package<T>>(PackageHeader());
        m_receivedBytes = 0;
        asio::mutable_buffer headerBuf(&m_packageIn->GetHeader(), sizeof(PackageHeader));

        asio::async_read(m_sslSocket, headerBuf, [connection](const asio::error_code& errorCode, const size_t) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                connection->Disconnect();
                return;
            }

            PackageHeader header = connection->m_packageIn->GetHeader();
            connection->m_packageIn = std::make_unique<Package<T>>(header);
            connection->HandleFullReceive();
        });
    }

    IOContext&                  m_context;
    std::shared_ptr<SSLContext> m_sslContext;
    SSLSocket                   m_sslSocket;
    uint16_t                    m_connectionID{};
    uint32_t                    m_receivedBytes{0};
    //uint32_t                    m_currentRequestID{0};
    bool                        m_sending{false};
    std::unique_ptr<Package<T>> m_packageOut;
    std::unique_ptr<Package<T>> m_packageIn;

    //std::unordered_map<uint32_t, std::function<void(std::unique_ptr<Package<T>>)>> m_requestCallbacks;

    ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
    ts::deque<PackageIn<T>>&               m_inDeque;

    static uint16_t                 s_currentConnectionID;
    static UniqueFileNamesGenerator s_fileNamesGenerator;
    static std::once_flag           s_onceFlag;
};

template <PackageType T>
uint16_t Connection<T>::s_currentConnectionID = 2;

#endif //CONNECTION_H
