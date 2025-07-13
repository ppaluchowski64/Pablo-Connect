#ifndef TLS_CONNECTION_H
#define TLS_CONNECTION_H

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
#include <TCP/Package.h>
#include <asio/buffer.hpp>
#include <AwaitableFlag.h>
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

namespace TLS {
    constexpr uint32_t PACKAGES_WARN_THRESHOLD = 10000;
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
        : m_context(ioContext), m_sslContext(sslContext), m_sslSocket(ioContext, *sslContext), m_sslFileStreamSocket(ioContext, *sslContext), m_connectionState(ConnectionState::DISCONNECTED), m_sendMessageFlag(ioContext.get_executor())
        , m_sendFileFlag(ioContext.get_executor()), m_fileReceiveFlag(ioContext.get_executor()), m_inDeque(inDeque) { }

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
            m_sendMessageFlag.Signal();
        }

        template <StdLayoutOrVecOrString... Args>
        void Send(T type, PackageFlag flag, Args... args) {
            std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(type, args...);
            package->GetHeader().flags = static_cast<uint8_t>(flag);
            m_outDeque.push_back(std::move(package));
            m_sendMessageFlag.Signal();
        }

        void RequestFile(const std::string& path, const std::string& filename) {
            std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(static_cast<T>(0), filename, path);
            package->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_REQUEST);
            m_outDeque.push_back(std::move(package));
            m_sendMessageFlag.Signal();
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
                connection->m_connectionState = ConnectionState::CONNECTED;

                asio::co_spawn(connection->m_context, coReceiveMessage(connection), asio::detached);
                asio::co_spawn(connection->m_context, coReceiveFile(connection), asio::detached);
                asio::co_spawn(connection->m_context, coSendFile(connection), asio::detached);
                asio::co_spawn(connection->m_context, coSendMessage(connection), asio::detached);

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
                connection->m_connectionState = ConnectionState::CONNECTED;

                asio::co_spawn(connection->m_context, coReceiveMessage(connection), asio::detached);
                asio::co_spawn(connection->m_context, coReceiveFile(connection), asio::detached);
                asio::co_spawn(connection->m_context, coSendFile(connection), asio::detached);
                asio::co_spawn(connection->m_context, coSendMessage(connection), asio::detached);

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

            connection->m_fileReceiveFlag.Signal();
            connection->m_sendMessageFlag.Signal();
            connection->m_sendFileFlag.Signal();
        }

        static asio::awaitable<void> coReceiveMessage(std::shared_ptr<Connection<T>> connection) {
            try {
                while (connection->m_connectionState == ConnectionState::CONNECTED) {
                    PackageHeader header{};
                    asio::mutable_buffer headerBuffer(&header, sizeof(header));

                    co_await asio::async_read(connection->m_sslSocket, headerBuffer, asio::use_awaitable);

                    std::unique_ptr<Package<T>> package = std::make_unique<Package<T>>(header);
                    asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

                    co_await asio::async_read(connection->m_sslSocket, packageBuffer, asio::use_awaitable);

                    if ((header.flags & PackageFlag::FILE_RECEIVE_INFO) != 0) {
                        connection->m_fileInfoDeque.push_back(std::move(package));
                        connection->m_fileReceiveFlag.Signal();
                        continue;
                    }

                    if ((header.flags & PackageFlag::FILE_REQUEST) != 0) {
                        connection->m_fileRequestDeque.push_back(std::move(package));
                        connection->m_sendFileFlag.Signal();
                    }

                    PackageIn<T> packageIn {
                        std::move(package),
                        connection
                    };

                    connection->m_inDeque.push_back(std::move(packageIn));
                }
            } catch (const std::system_error& error) {
                if (error.code() == asio::error::eof || error.code() == asio::ssl::error::stream_truncated) {
                    Debug::Log("Connection closed cleanly by peer.");
                } else {
                    Debug::LogError(error.what());
                }

                connection->Disconnect();
                co_return;
            }
        }

        static asio::awaitable<void> coReceiveFile(std::shared_ptr<Connection<T>> connection) {
            try {
                if (connection->m_connectionState != ConnectionState::CONNECTED) co_return;
                std::vector<char> dataBuffer(FILE_BUFFER_SIZE);
                co_await connection->m_fileReceiveFlag.Wait();

                while (connection->m_connectionState == ConnectionState::CONNECTED) {
                    while (!connection->m_fileInfoDeque.empty()) {
                        std::unique_ptr<Package<T>> package = connection->m_fileInfoDeque.pop_front();

                        std::string filename;
                        PackageSizeInt size;

                        if ((package->GetHeader().flags & PackageFlag::FILE_NAME_INCLUDED) != 0) {
                            package->GetValue(filename);
                        } else {
                            filename = UniqueFileNamesGenerator::GetUniqueName();
                        }

                        package->GetValue(size);
                        std::ofstream fileStream(UniqueFileNamesGenerator::GetFilePath() / filename, std::ios::binary | std::ios::trunc);

                        if (!fileStream.is_open()) {
                            Debug::LogError("Could not open file");
                            connection->Disconnect();
                            co_return;
                        }

                        while (size > 0) {
                            const PackageSizeInt readSize = std::min(size, FILE_BUFFER_SIZE);
                            size -= readSize;

                            asio::mutable_buffer buffer(dataBuffer.data(), readSize);
                            co_await asio::async_read(connection->m_sslFileStreamSocket, buffer, asio::use_awaitable);
                            fileStream.write(dataBuffer.data(), readSize);
                        }

                        fileStream.close();
                    }

                    connection->m_fileReceiveFlag.Reset();
                    co_await connection->m_fileReceiveFlag.Wait();
                }
            } catch (const std::system_error& error) {
                if (error.code() == asio::error::eof || error.code() == asio::ssl::error::stream_truncated) {
                    Debug::Log("Connection closed cleanly by peer.");
                } else {
                    Debug::LogError(error.what());
                }

                connection->Disconnect();
                co_return;
            }
        }

        static asio::awaitable<void> coSendMessage(std::shared_ptr<Connection<T>> connection) {
            try {
                if (connection->m_connectionState != ConnectionState::CONNECTED) co_return;
                co_await connection->m_sendMessageFlag.Wait();

                while (connection->m_connectionState == ConnectionState::CONNECTED) {
                    while (!connection->m_outDeque.empty()) {
                        std::unique_ptr<Package<T>> package = connection->m_outDeque.pop_front();
                        PackageHeader header = package->GetHeader();

                        std::vector<asio::const_buffer> buffers = {
                            asio::const_buffer(&header, sizeof(header)),
                            asio::const_buffer(package->GetRawBody(), header.size)
                        };

                        co_await asio::async_write(connection->m_sslSocket, buffers, asio::use_awaitable);
                    }

                    connection->m_sendMessageFlag.Reset();
                    co_await connection->m_sendMessageFlag.Wait();
                }
            } catch (const std::system_error& error) {
                Debug::LogError(error.what());
                connection->Disconnect();
                co_return;
            }
        }

        static asio::awaitable<void> coSendFile(std::shared_ptr<Connection<T>> connection) {
            try {
                if (connection->m_connectionState != ConnectionState::CONNECTED) co_return;
                std::vector<char> fileBuffer(FILE_BUFFER_SIZE);
                co_await connection->m_sendFileFlag.Wait();

                while (connection->m_connectionState == ConnectionState::CONNECTED) {
                    while (!connection->m_fileRequestDeque.empty()) {
                        std::unique_ptr<Package<T>> package = connection->m_fileRequestDeque.pop_front();

                        std::string filename;
                        std::string path;

                        package->GetValue(filename);
                        package->GetValue(path);

                        std::filesystem::path filePath(path);

                        if (!std::filesystem::exists(filePath)) {
                            Debug::LogError("File path doesnt exist");
                            connection->Disconnect();
                            co_return;
                        }

                        PackageSizeInt size = std::filesystem::file_size(filePath);

                        {
                            std::unique_ptr<Package<T>> fileInfo = Package<T>::CreateUnique(static_cast<T>(0), filename, size);
                            fileInfo->GetHeader().flags = PackageFlag::FILE_NAME_INCLUDED | PackageFlag::FILE_RECEIVE_INFO;
                            connection->Send(std::move(fileInfo));
                        }

                        std::ifstream fileStream(filePath, std::ios::binary | std::ios::in);

                        if (!fileStream.is_open()) {
                            Debug::LogError("Could not open file");
                            connection->Disconnect();
                            co_return;
                        }

                        const PackageSizeInt fullSize = size;

                        while (size > 0) {
                            const PackageSizeInt readSize = std::min(size, FILE_BUFFER_SIZE);
                            size -= readSize;
                            fileStream.read(fileBuffer.data(), readSize);

                            asio::const_buffer buffer(fileBuffer.data(), readSize);
                            co_await asio::async_write(connection->m_sslFileStreamSocket, buffer, asio::use_awaitable);
                        }

                        fileStream.close();
                    }

                    connection->m_sendFileFlag.Reset();
                    co_await connection->m_sendFileFlag.Wait();
                }
            } catch (const std::system_error& error) {
                Debug::LogError(error.what());
                connection->Disconnect();
                co_return;
            }
        }

        IOContext&                  m_context;
        std::shared_ptr<SSLContext> m_sslContext;
        SSLSocket                   m_sslSocket;
        SSLSocket                   m_sslFileStreamSocket;
        ConnectionState             m_connectionState;
        uint16_t                    m_connectionID{};
        AwaitableFlag               m_sendMessageFlag;
        AwaitableFlag               m_sendFileFlag;
        AwaitableFlag               m_fileReceiveFlag;

        ts::deque<std::unique_ptr<Package<T>>> m_fileInfoDeque;
        ts::deque<std::unique_ptr<Package<T>>> m_fileRequestDeque;
        ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
        ts::deque<PackageIn<T>>&               m_inDeque;

        static uint16_t s_currentConnectionID;
    };

    template <PackageType T>
    uint16_t Connection<T>::s_currentConnectionID = 1;
}
#endif //TLS_CONNECTION_H