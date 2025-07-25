#ifndef P2P_TCP_CONNECTION_H
#define P2P_TCP_CONNECTION_H

#include <AwaitableFlag.h>
#include <P2P/ConnectionParent.h>
#include <P2P/Settings.h>
#include <concurrentqueue.h>
#include <array>

template <PackageType T>
class TCPConnection final : public ConnectionParent<T>, public std::enable_shared_from_this<TCPConnection<T>> {
public:
    TCPConnection() = delete;
    TCPConnection(IOContext& sharedContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue) :
        m_context(sharedContext), m_socket(sharedContext), m_fileStreamSocket(sharedContext), m_resolver(m_context),
        m_sendMessageAwaitableFlag(sharedContext.get_executor()), m_sendFileAwaitableFlag(sharedContext.get_executor()),
        m_receiveFileAwaitableFlag(sharedContext.get_executor()), m_connectionState(ConnectionState::DISCONNECTED), m_inQueue(sharedMessageQueue) { }

    static NO_DISCARD std::shared_ptr<TCPConnection<T>> Create(IOContext& sharedContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue) {
        return std::make_shared<TCPConnection<T>>(sharedContext, sharedMessageQueue);
    }

    void Start(std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) override {
        ZoneScoped;
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<TCPConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoStart(connection, std::move(address), ports, callbackData), asio::detached);
    }

    void Seek(std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) override {
        ZoneScoped;
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        std::shared_ptr<TCPConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoSeek(connection, std::move(address), std::move(ports), callbackData), asio::detached);
    }

    NO_DISCARD ConnectionState GetConnectionState() const override {
        ZoneScoped;
        return m_connectionState.load(std::memory_order_acquire);
    }

    void Send(std::unique_ptr<Package<T>>&& package) override {
        ZoneScoped;
        static thread_local moodycamel::ProducerToken token(m_outQueue);

        m_outQueue.enqueue(token, std::move(package));
        m_sendMessageAwaitableFlag.Signal();
    }

    void RequestFile(const std::string& requestedFilePath, const std::string& fileName) override {
        ZoneScoped;
        std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(static_cast<T>(0), std::string(fileName), std::string(requestedFilePath));
        package->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_REQUEST);
        Send(std::move(package));
    }

    void Disconnect() override {
        ZoneScoped;
        if (!m_socket.is_open() && !m_fileStreamSocket.is_open()) {
            SetConnectionState(ConnectionState::DISCONNECTED);
            return;
        }

        SetConnectionState(ConnectionState::DISCONNECTED);
        asio::error_code errorCode;

        if (m_socket.is_open()) {
            m_socket.close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }

        if (m_fileStreamSocket.is_open()) {
            m_fileStreamSocket.close(errorCode);

            if (errorCode) {
                Debug::LogError(errorCode.message());
            }
        }

        SetConnectionState(ConnectionState::DISCONNECTED);

        m_receiveFileAwaitableFlag.Signal();
        m_sendMessageAwaitableFlag.Signal();
        m_sendFileAwaitableFlag.Signal();
    }

private:
    static asio::awaitable<void> CoStart(std::shared_ptr<TCPConnection<T>> connection, std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) {
        try {
            connection->SetConnectionState(ConnectionState::CONNECTING);

            auto connectionEndpoints = co_await connection->m_resolver.async_resolve(address, std::to_string(ports[0]));
            auto fileStreamEndpoints = co_await connection->m_resolver.async_resolve(address, std::to_string(ports[1]));

            if (connectionEndpoints.empty() || fileStreamEndpoints.empty()) {
                Debug::LogError("Failed to resolve one or more endpoints.");
                connection->Disconnect();
                co_return;
            }

            co_await asio::async_connect(connection->m_socket, connectionEndpoints, asio::use_awaitable);
            co_await asio::async_connect(connection->m_fileStreamSocket, fileStreamEndpoints, asio::use_awaitable);

            Debug::Log("Accepted TCP connection to {}:{}, {}:{}",
                  connection->m_socket.remote_endpoint().address().to_string(),
                  std::to_string(connection->m_socket.remote_endpoint().port()),
                  connection->m_fileStreamSocket.remote_endpoint().address().to_string(),
                  std::to_string(connection->m_fileStreamSocket.remote_endpoint().port()));

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

    static asio::awaitable<void> CoSeek(std::shared_ptr<TCPConnection<T>> connection, std::string&& address, const std::array<uint16_t, 2> ports, const ConnectionCallbackData callbackData) {
        try {
            connection->SetConnectionState(ConnectionState::CONNECTING);

            auto connectionEndpoints = co_await connection->m_resolver.async_resolve(address, std::to_string(ports[0]));
            auto fileStreamEndpoints = co_await connection->m_resolver.async_resolve(address, std::to_string(ports[1]));

            if (connectionEndpoints.empty() || fileStreamEndpoints.empty()) {
                Debug::LogError("Failed to resolve one or more endpoints.");
                connection->Disconnect();
                co_return;
            }

            TCPAcceptor connectionAcceptor(connection->m_context, *connectionEndpoints.begin());
            TCPAcceptor fileStreamAcceptor(connection->m_context, *fileStreamEndpoints.begin());

            co_await connectionAcceptor.async_accept(connection->m_socket, asio::use_awaitable);
            co_await fileStreamAcceptor.async_accept(connection->m_fileStreamSocket, asio::use_awaitable);

            Debug::Log("Accepted TCP connection to {}:{}, {}:{}",
                  connection->m_socket.remote_endpoint().address().to_string(),
                  std::to_string(connection->m_socket.remote_endpoint().port()),
                  connection->m_fileStreamSocket.remote_endpoint().address().to_string(),
                  std::to_string(connection->m_fileStreamSocket.remote_endpoint().port()));

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

    static asio::awaitable<void> CoReceiveMessage(std::shared_ptr<TCPConnection<T>> connection) {
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
            if (error.code() == asio::error::eof || error.code() == asio::error::connection_reset) {
                Debug::Log("Connection closed cleanly by peer.");
            } else if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoReceiveFile(std::shared_ptr<TCPConnection<T>> connection) {
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
            if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoSendMessage(std::shared_ptr<TCPConnection<T>> connection) {
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
            if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoSendFile(std::shared_ptr<TCPConnection<T>> connection) {
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
                        std::unique_ptr<Package<T>> fileInfo = Package<T>::CreateUnique(static_cast<T>(0), std::move(filename), std::move(size));
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
            if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    void SetConnectionState(const ConnectionState state) {
        ZoneScoped;
        m_connectionState.store(state, std::memory_order_release);
    }

    IOContext&  m_context;
    TCPSocket   m_socket;
    TCPSocket   m_fileStreamSocket;
    TCPResolver m_resolver;

    AwaitableFlag m_sendMessageAwaitableFlag;
    AwaitableFlag m_sendFileAwaitableFlag;
    AwaitableFlag m_receiveFileAwaitableFlag;

    std::atomic<ConnectionState> m_connectionState;

    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_outQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_fileRequestQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<Package<T>>>    m_fileInfoQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& m_inQueue;


};

#endif //P2P_TCP_CONNECTION_H