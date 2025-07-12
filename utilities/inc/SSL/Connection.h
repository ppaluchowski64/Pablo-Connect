#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/awaitable.hpp>

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
typedef asio::ip::address IPAddress;

constexpr uint32_t PACKAGES_WARN_THRESHOLD = 100;

constexpr uint16_t SSL_CONNECTION_PORT = 50000;
constexpr uint16_t SSL_FILE_STREAM_PORT = 50001;

template <PackageType T>
class Connection;

template <PackageType T>
class PackageIn final {
public:
    std::unique_ptr<Package<T>> package;
    std::shared_ptr<Connection<T>> connection;
};

enum class ConnectionState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

template <PackageType T>
class Connection final : public std::enable_shared_from_this<Connection<T>> {
public:
    Connection(IOContext& ioContext, const std::shared_ptr<SSLContext>& sslContext, ts::deque<PackageIn<T>>& inDeque)
    : m_context(ioContext), m_sslContext(sslContext), m_sslSocket(ioContext, *sslContext), m_sslFileStreamSocket(ioContext, *sslContext), m_connectionState(ConnectionState::DISCONNECTED), m_inDeque(inDeque) { }

    Connection() = delete;

    ~Connection() {
        Disconnect();
    }

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

    NO_DISCARD static std::shared_ptr<Connection> Create(IOContext& ioContext, std::shared_ptr<SSLContext> sslContext, ts::deque<PackageIn<T>>& inDeque) {
        return std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);
    }

    void Seek(Acceptor& connectionAcceptor, Acceptor& fileStreamAcceptor, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        if (m_connectionState != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, coSeek(connection, connectionAcceptor, fileStreamAcceptor, callback), asio::detached);
    }

    void Start(const Endpoint& connectionEndpoint, const Endpoint& fileStreamEndpoint, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        if (m_connectionState != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, coStart(connection, connectionEndpoint, fileStreamEndpoint, callback), asio::detached);
    }

    void Start(const IPAddress& address, const uint16_t connectionPort, const uint16_t fileStreamPort, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        if (m_connectionState != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, coStart(connection, Endpoint(address, connectionPort), Endpoint(address, fileStreamPort), callback), asio::detached);
    }

    NO_DISCARD ConnectionState GetConnectionState() {
        if (m_connectionState == ConnectionState::CONNECTED) {
            if (!m_sslSocket.lowest_layer().is_open() || !m_sslFileStreamSocket.lowest_layer().is_open()) {
                m_connectionState = ConnectionState::DISCONNECTED;
                m_connectionID    = 0;
            }
        }

        return m_connectionState;
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

    void Disconnect() {
        if (!m_sslSocket.lowest_layer().is_open() && !m_sslFileStreamSocket.lowest_layer().is_open()) {
            m_connectionState = ConnectionState::DISCONNECTED;
            return;
        }

        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, coDisconnect(connection), asio::detached);
    }

    NO_DISCARD uint16_t GetConnectionID() const {
        return m_connectionID;
    }

private:
    static asio::awaitable<void> coStart(std::shared_ptr<Connection<T>> connection, const Endpoint connectionEndpoint, const Endpoint fileStreamEndpoint, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        try {
            connection->m_connectionState = ConnectionState::CONNECTING;
            std::initializer_list<Endpoint> connectionEndpoints({connectionEndpoint});
            std::initializer_list<Endpoint> fileStreamEndpoints({fileStreamEndpoint});

            co_await asio::async_connect(connection->m_sslSocket.lowest_layer(), connectionEndpoints, asio::use_awaitable);
            co_await connection->m_sslSocket.async_handshake(SSLStreamBase::client, asio::use_awaitable);
            co_await asio::async_connect(connection->m_sslFileStreamSocket.lowest_layer(), fileStreamEndpoints, asio::use_awaitable);
            co_await connection->m_sslFileStreamSocket.async_handshake(SSLStreamBase::client, asio::use_awaitable);

            Debug::Log("Accepted connection to " +
                  connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" +
                  std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()) + " , " +
                  connection->m_sslFileStreamSocket.lowest_layer().remote_endpoint().address().to_string() + ":" +
                  std::to_string(connection->m_sslFileStreamSocket.lowest_layer().remote_endpoint().port()));

            connection->m_connectionID = s_currentConnectionID++;
            connection->ReceiveMessage();
            connection->m_connectionState = ConnectionState::CONNECTED;
            callback(connection);

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
        }
    }

    static asio::awaitable<void> coSeek(std::shared_ptr<Connection<T>> connection, Acceptor& connectionAcceptor, Acceptor& fileStreamAcceptor, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        try {
            connection->m_connectionState = ConnectionState::CONNECTING;
            co_await connectionAcceptor.async_accept(connection->m_sslSocket.lowest_layer(), asio::use_awaitable);
            co_await connection->m_sslSocket.async_handshake(SSLStreamBase::server, asio::use_awaitable);
            co_await fileStreamAcceptor.async_accept(connection->m_sslFileStreamSocket.lowest_layer(), asio::use_awaitable);
            co_await connection->m_sslFileStreamSocket.async_handshake(SSLStreamBase::server, asio::use_awaitable);

            Debug::Log("Accepted connection to " +
                connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" +
                std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()) + " , " +
                connection->m_sslFileStreamSocket.lowest_layer().remote_endpoint().address().to_string() + ":" +
                std::to_string(connection->m_sslFileStreamSocket.lowest_layer().remote_endpoint().port()));

            connection->m_connectionID = s_currentConnectionID++;
            connection->ReceiveMessage();
            connection->m_connectionState = ConnectionState::CONNECTED;
            callback(connection);

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
        }
    }

    static asio::awaitable<void> coDisconnect(std::shared_ptr<Connection<T>> connection) {
        connection->m_connectionState = ConnectionState::DISCONNECTING;

        try {
            if (connection->m_sslSocket.lowest_layer().is_open()) {
                co_await connection->m_sslSocket.async_shutdown(asio::use_awaitable);
            }
            if (connection->m_sslFileStreamSocket.lowest_layer().is_open()) {
                co_await connection->m_sslFileStreamSocket.async_shutdown(asio::use_awaitable);
            }

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
        }

        std::error_code errorCode;
        if (connection->m_sslSocket.lowest_layer().is_open()) {
            connection->m_sslSocket.lowest_layer().close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }
        if (connection->m_sslFileStreamSocket.lowest_layer().is_open()) {
            connection->m_sslFileStreamSocket.lowest_layer().close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }

        connection->m_connectionState = ConnectionState::DISCONNECTED;
        connection->m_connectionID    = 0;
    }


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

        asio::async_read(m_sslSocket, buffer, [connection](const asio::error_code& errorCode, const size_t bytes) {
            if (errorCode) {
                if (errorCode == asio::error::eof || errorCode == asio::ssl::error::stream_truncated) {
                    Debug::Log("Connection closed cleanly by peer.");
                } else {
                    Debug::LogError(errorCode.message());
                }

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

    void HandleFileReceive() {
        PackageHeader header = m_packageIn->GetHeader();
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        PackageSizeInt bytesLeft = header.size;
        std::string filename;

        if ((header.flags & PackageFlag::FILE_NAME_INCLUDED) == 0) {
            filename = UniqueFileNamesGenerator::GetUniqueName();
        } else {
            PackageSizeInt nameSize;
            asio::mutable_buffer sizeBuffer(&nameSize, sizeof(PackageSizeInt));
            asio::read(m_sslSocket, sizeBuffer);

            if (nameSize > MAX_FILE_NAME_SIZE) {
                Debug::LogError("File name too long");
                Disconnect();
                return;
            }

            filename.resize(nameSize);
            asio::mutable_buffer nameBuffer(filename.data(), nameSize);
            asio::read(m_sslSocket, nameBuffer);

            bytesLeft -= sizeof(PackageSizeInt);
            bytesLeft -= nameSize;
        }

        while (bytesLeft > 0) {
            PackageSizeInt bufferSize = std::min(bytesLeft, FILE_BUFFER_SIZE);
            std::unique_ptr<char[]> rawBuffer = std::make_unique<char[]>(bufferSize);
            asio::mutable_buffer buffer(rawBuffer.get(), bufferSize);

            asio::read(m_sslSocket, buffer);

        }
    }

    void ReceiveMessage() {
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        m_packageIn = std::make_unique<Package<T>>(PackageHeader());
        m_receivedBytes = 0;
        asio::mutable_buffer headerBuf(&m_packageIn->GetHeader(), sizeof(PackageHeader));

        asio::async_read(m_sslSocket, headerBuf, [connection](const asio::error_code& errorCode, const size_t) {
            if (errorCode) {
                if (errorCode == asio::error::eof || errorCode == asio::ssl::error::stream_truncated) {
                    Debug::Log("Connection closed cleanly by peer.");
                } else {
                    Debug::LogError(errorCode.message());
                }

                connection->Disconnect();
                return;
            }

            PackageHeader header = connection->m_packageIn->GetHeader();
            connection->m_packageIn = std::make_unique<Package<T>>(header);

            if ((header.flags & PackageFlag::FILE) != 0) {
                connection->HandleFileReceive();
                return;
            }

            connection->HandleFullReceive();
        });
    }

    IOContext&                  m_context;
    std::shared_ptr<SSLContext> m_sslContext;
    SSLSocket                   m_sslSocket;
    SSLSocket                   m_sslFileStreamSocket;
    ConnectionState             m_connectionState;
    uint16_t                    m_connectionID{};
    uint32_t                    m_receivedBytes{0};
    bool                        m_sending{false};
    std::unique_ptr<Package<T>> m_packageOut;
    std::unique_ptr<Package<T>> m_packageIn;

    ts::deque<std::unique_ptr<Package<T>>> m_fileInfoDeque;
    ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
    ts::deque<PackageIn<T>>&               m_inDeque;

    static uint16_t                 s_currentConnectionID;
    static UniqueFileNamesGenerator s_fileNamesGenerator;
    static std::once_flag           s_onceFlag;
};

template <PackageType T>
uint16_t Connection<T>::s_currentConnectionID = 1;

#endif //CONNECTION_H