#ifndef P2P_TLS_CONNECTION_H
#define P2P_TLS_CONNECTION_H

#include <AwaitableFlag.h>
#include <P2P/ConnectionParent.h>
#include <P2P/Settings.h>
#include <concurrentqueue.h>

#include <utility>

template <PackageType T>
class TLSConnection final : public ConnectionParent<T>, public std::enable_shared_from_this<TLSConnection<T>> {
public:
    TLSConnection() = delete;
    TLSConnection(IOContext& sharedContext, std::shared_ptr<SSLContext> sharedSSLContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue)
        : m_context(sharedContext), m_sslContext(std::move(sharedSSLContext)), m_socket(m_context, *m_sslContext), m_fileStreamSocket(m_context, *m_sslContext),
          m_sendMessageAwaitableFlag(m_context.get_executor()), m_sendFileAwaitableFlag(m_context.get_executor()), m_receiveFileAwaitableFlag(m_context.get_executor()),
          m_inQueue(sharedMessageQueue)
    { }

    static NO_DISCARD std::shared_ptr<SSLContext> CreateSSLContext(const std::filesystem::path& path, const bool isServer) {
        auto ctx = std::make_shared<SSLContext>(isServer ? SSLContext::tlsv13_server : SSLContext::tlsv13_client);
        const std::string keyPath = (path / "privateKey.key").string();
        const std::string certPath = (path / "certificate.crt").string();

        ctx->set_options(
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

    static NO_DISCARD std::shared_ptr<TLSConnection<T>> Create(IOContext& sharedContext, std::shared_ptr<SSLContext> sharedSSLContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue) {
        return std::make_shared<TLSConnection<T>>(sharedContext, sharedSSLContext, sharedMessageQueue);
    }

    void Start(const TCPEndpoint& connectionEndpoint, const TCPEndpoint& fileStreamEndpoint, const ConnectionCallbackData callbackData) override {
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<TLSConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoStart(connection, connectionEndpoint, fileStreamEndpoint, callbackData), asio::detached);
    }

    void Seek(TCPAcceptor& connectionAcceptor, TCPAcceptor& fileStreamAcceptor, const ConnectionCallbackData callbackData) override {
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<TLSConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoSeek(connection, connectionAcceptor, fileStreamAcceptor, callbackData), asio::detached);
    }

    NO_DISCARD ConnectionState GetConnectionState() const override {
        return m_connectionState.load(std::memory_order_acquire);
    }

    void Send(std::unique_ptr<Package<T>>&& package) override {
        static thread_local moodycamel::ProducerToken token(m_outQueue);

        m_outQueue.enqueue(token, std::move(package));
        m_sendMessageAwaitableFlag.Signal();
    }

    void RequestFile(const std::string& requestedFilePath, const std::string& fileName) override {
        std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(static_cast<T>(0), fileName, requestedFilePath);
        package->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_REQUEST);
        Send(std::move(package));
    }

    void Disconnect() override {
        std::shared_ptr<TLSConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoDisconnect(connection), asio::detached);
    }

private:
    static asio::awaitable<void> CoStart(std::shared_ptr<TLSConnection<T>> connection, const TCPEndpoint& connectionEndpoint, const TCPEndpoint& fileStreamEndpoint, const ConnectionCallbackData callbackData) {
        try {
            connection->SetConnectionState(ConnectionState::CONNECTING);
            std::initializer_list<TCPEndpoint> connectionEndpoints({connectionEndpoint});
            std::initializer_list<TCPEndpoint> fileStreamEndpoints({fileStreamEndpoint});

            co_await asio::async_connect(connection->m_socket.lowest_layer(), connectionEndpoints, asio::use_awaitable);
            co_await connection->m_socket.async_handshake(SSLStreamBase::client, asio::use_awaitable);
            co_await asio::async_connect(connection->m_fileStreamSocket.lowest_layer(), fileStreamEndpoints, asio::use_awaitable);
            co_await connection->m_fileStreamSocket.async_handshake(SSLStreamBase::client, asio::use_awaitable);

            Debug::Log("Accepted TLS connection to {}:{}, {}:{}",
                  connection->m_socket.lowest_layer().remote_endpoint().address().to_string(),
                  connection->m_socket.lowest_layer().remote_endpoint().port(),
                  connection->m_fileStreamSocket.lowest_layer().remote_endpoint().address().to_string(),
                  connection->m_fileStreamSocket.lowest_layer().remote_endpoint().port());

            connection->SetConnectionState(ConnectionState::CONNECTED);

            asio::co_spawn(connection->m_context, CoReceiveMessage(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoReceiveFile(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoSendFile(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoSendMessage(connection), asio::detached);

            if (callbackData.callback != nullptr) {
                callbackData.callback(callbackData.data);
            }

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
        }
    }

    static asio::awaitable<void> CoSeek(std::shared_ptr<TLSConnection<T>> connection, TCPAcceptor& connectionAcceptor, TCPAcceptor& fileStreamAcceptor, const ConnectionCallbackData callbackData) {
        try {
            connection->SetConnectionState(ConnectionState::CONNECTING);

            co_await connectionAcceptor.async_accept(connection->m_socket.lowest_layer(), asio::use_awaitable);
            co_await connection->m_socket.async_handshake(SSLStreamBase::server, asio::use_awaitable);
            co_await fileStreamAcceptor.async_accept(connection->m_fileStreamSocket.lowest_layer(), asio::use_awaitable);
            co_await connection->m_fileStreamSocket.async_handshake(SSLStreamBase::server, asio::use_awaitable);

            Debug::Log("Accepted TLS connection to {}:{}, {}:{}",
                  connection->m_socket.lowest_layer().remote_endpoint().address().to_string(),
                  connection->m_socket.lowest_layer().remote_endpoint().port(),
                  connection->m_fileStreamSocket.lowest_layer().remote_endpoint().address().to_string(),
                  connection->m_fileStreamSocket.lowest_layer().remote_endpoint().port());

            connection->SetConnectionState(ConnectionState::CONNECTED);

            asio::co_spawn(connection->m_context, CoReceiveMessage(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoReceiveFile(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoSendFile(connection), asio::detached);
            asio::co_spawn(connection->m_context, CoSendMessage(connection), asio::detached);

            if (callbackData.callback != nullptr) {
                callbackData.callback(callbackData.data);
            }

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
        }
    }

    static asio::awaitable<void> CoDisconnect(std::shared_ptr<TLSConnection<T>> connection) {
        try {
            if (connection->GetConnectionState() == ConnectionState::DISCONNECTED) {
                co_return;
            }

            if (!connection->m_socket.lowest_layer().is_open() && !connection->m_fileStreamSocket.lowest_layer().is_open()) {
                connection->SetConnectionState(ConnectionState::DISCONNECTED);
                co_return;
            }

            connection->SetConnectionState(ConnectionState::DISCONNECTING);

            if (connection->m_socket.lowest_layer().is_open()) {
                co_await connection->m_socket.async_shutdown(asio::use_awaitable);
            }

            if (connection->m_fileStreamSocket.lowest_layer().is_open()) {
                co_await connection->m_fileStreamSocket.async_shutdown(asio::use_awaitable);
            }

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
        }

        asio::error_code errorCode;

        if (connection->m_socket.lowest_layer().is_open()) {
            connection->m_socket.lowest_layer().close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }

        if (connection->m_fileStreamSocket.lowest_layer().is_open()) {
            connection->m_fileStreamSocket.lowest_layer().close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }

        connection->SetConnectionState(ConnectionState::DISCONNECTED);
        connection->m_receiveFileAwaitableFlag.Signal();
        connection->m_sendMessageAwaitableFlag.Signal();
        connection->m_sendFileAwaitableFlag.Signal();
    }

    static asio::awaitable<void> CoReceiveMessage(std::shared_ptr<TLSConnection<T>> connection) {
        try {
            PackageHeader header{};
            moodycamel::ProducerToken inQueueToken(connection->m_inQueue);
            moodycamel::ProducerToken fileRequestToken(connection->m_fileRequestQueue);
            moodycamel::ProducerToken fileInfoToken(connection->m_fileInfoQueue);

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                asio::mutable_buffer headerBuffer(&header, sizeof(header));

                co_await asio::async_read(connection->m_socket, headerBuffer, asio::use_awaitable);

                std::unique_ptr<Package<T>> package = std::make_unique<Package<T>>(header);
                asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

                co_await asio::async_read(connection->m_socket, packageBuffer, asio::use_awaitable);

                if ((header.flags & PackageFlag::FILE_RECEIVE_INFO) != 0) {
                    connection->m_fileInfoQueue.enqueue(fileInfoToken, std::move(package));
                    connection->m_receiveFileAwaitableFlag.Signal();
                    continue;
                }

                if ((header.flags & PackageFlag::FILE_REQUEST) != 0) {
                    connection->m_fileRequestQueue.enqueue(fileRequestToken, std::move(package));
                    connection->m_sendFileAwaitableFlag.Signal();
                }

                std::unique_ptr<PackageIn<T>> packageIn = std::make_unique<PackageIn<T>>();
                packageIn->Package = std::move(package);
                packageIn->Connection = connection;

                connection->m_inQueue.enqueue(inQueueToken, std::move(packageIn));
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

        static asio::awaitable<void> CoReceiveFile(std::shared_ptr<TLSConnection<T>> connection) {
        try {
            moodycamel::ConsumerToken fileInfoToken(connection->m_fileInfoQueue);

            if (connection->GetConnectionState() == ConnectionState::CONNECTED) co_return;
            std::vector<char> dataBuffer(FILE_BUFFER_SIZE);
            co_await connection->m_receiveFileAwaitableFlag.Wait();

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                if (std::unique_ptr<Package<T>> package; connection->m_fileInfoQueue.try_dequeue(fileInfoToken, package)) {
                    std::string filename;
                    PackageSizeInt size;

                    package->GetValue(filename);
                    package->GetValue(size);

                    std::ofstream fileStream(P2PSettings::GetFileDownloadDirectory() / filename, std::ios::binary | std::ios::trunc);

                    if (!fileStream.is_open()) {
                        Debug::LogError("Could not open file");
                        connection->Disconnect();
                        co_return;
                    }

                    while (size > 0) {
                        const PackageSizeInt readSize = std::min(size, FILE_BUFFER_SIZE);
                        size -= readSize;

                        asio::mutable_buffer buffer(dataBuffer.data(), readSize);
                        co_await asio::async_read(connection->m_fileStreamSocket, buffer, asio::use_awaitable);
                        fileStream.write(dataBuffer.data(), readSize);
                    }

                    fileStream.close();
                } else {
                    connection->m_receiveFileAwaitableFlag.Reset();
                    co_await connection->m_receiveFileAwaitableFlag.Wait();
                }
            }
        } catch (const std::system_error& error) {
            if (error.code() == asio::error::eof) {
                Debug::Log("Connection closed cleanly by peer.");
            } else {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoSendMessage(std::shared_ptr<TLSConnection<T>> connection) {
        try {
            moodycamel::ConsumerToken outQueueToken(connection->m_outQueue);

            if (connection->GetConnectionState() != ConnectionState::CONNECTED) co_return;
            co_await connection->m_sendMessageAwaitableFlag.Wait();

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                if (std::unique_ptr<Package<T>> package; connection->m_outQueue.try_dequeue(outQueueToken, package)) {
                    const PackageHeader header = package->GetHeader();

                    std::vector<asio::const_buffer> buffers = {
                        asio::const_buffer(&header, sizeof(header)),
                        asio::const_buffer(package->GetRawBody(), header.size)
                    };

                    co_await asio::async_write(connection->m_socket, buffers, asio::use_awaitable);
                } else {
                    connection->m_sendMessageAwaitableFlag.Reset();
                    co_await connection->m_sendMessageAwaitableFlag.Wait();
                }
            }
        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoSendFile(std::shared_ptr<TLSConnection<T>> connection) {
        try {
            moodycamel::ConsumerToken fileRequestToken(connection->m_fileRequestQueue);

            if (connection->GetConnectionState() != ConnectionState::CONNECTED) co_return;
            std::vector<char> fileBuffer(FILE_BUFFER_SIZE);

            co_await connection->m_sendFileAwaitableFlag.Wait();

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                if (std::unique_ptr<Package<T>> package; connection->m_fileRequestQueue.try_dequeue(fileRequestToken, package)) {
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
                        fileInfo->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_RECEIVE_INFO);
                        connection->Send(std::move(fileInfo));
                    }

                    std::ifstream fileStream(filePath, std::ios::binary | std::ios::in);

                    if (!fileStream.is_open()) {
                        Debug::LogError("Could not open file");
                        connection->Disconnect();
                        co_return;
                    }

                    while (size > 0) {
                        const PackageSizeInt readSize = std::min(size, FILE_BUFFER_SIZE);
                        size -= readSize;
                        fileStream.read(fileBuffer.data(), readSize);

                        asio::const_buffer buffer(fileBuffer.data(), readSize);
                        co_await asio::async_write(connection->m_fileStreamSocket, buffer, asio::use_awaitable);
                    }

                    fileStream.close();
                } else {
                    connection->m_sendFileAwaitableFlag.Reset();
                    co_await connection->m_sendFileAwaitableFlag.Wait();
                }
            }
        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
            co_return;
        }
    }

    void SetConnectionState(const ConnectionState state) {
        m_connectionState.store(state, std::memory_order_release);
    }

    IOContext&                  m_context;
    std::shared_ptr<SSLContext> m_sslContext;
    SSLSocket                   m_socket;
    SSLSocket                   m_fileStreamSocket;

    AwaitableFlag m_sendMessageAwaitableFlag;
    AwaitableFlag m_sendFileAwaitableFlag;
    AwaitableFlag m_receiveFileAwaitableFlag;

    std::atomic<ConnectionState> m_connectionState;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_outQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_fileRequestQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_fileInfoQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& m_inQueue;
};

#endif //P2P_TLS_CONNECTION_H
