#ifndef P2P_TCP_CONNECTION_H
#define P2P_TCP_CONNECTION_H

#include <AwaitableFlag.h>
#include <ConnectionParent.h>
#include <Settings.h>
#include <concurrentqueue.h>
#include <ConcurrentUnorderedMap.h>
#include <array>
#include <deque>

template <PackageType T>
class TCPConnection final : public ConnectionParent<T>, public std::enable_shared_from_this<TCPConnection<T>> {
public:
    TCPConnection() = delete;
    TCPConnection(IOContext& sharedContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue) :
        m_context(sharedContext), m_socket(sharedContext), m_fileStreamSocket(sharedContext), m_resolver(m_context),
        m_sendMessageAwaitableFlag(sharedContext.get_executor()), m_sendFileAwaitableFlag(sharedContext.get_executor()),
        m_receiveFileAwaitableFlag(sharedContext.get_executor()), m_connectionState(ConnectionState::DISCONNECTED), m_inQueue(sharedMessageQueue), m_ports({0, 0}) { }

    NO_DISCARD static std::shared_ptr<TCPConnection<T>> Create(IOContext& sharedContext, moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& sharedMessageQueue) {
        return std::make_shared<TCPConnection<T>>(sharedContext, sharedMessageQueue);
    }

    void Start(const IPAddress address, const std::array<uint16_t, 2> ports, const std::function<void()> callback) override {
        ZoneScoped;
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        m_address = address;
        m_ports = ports;

        std::shared_ptr<TCPConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoStart(connection, callback), asio::detached);
    }

    void Seek(const IPAddress address, const std::array<uint16_t, 2> ports, const std::function<void()> connectionSeekCallback, const std::function<void()> callback) override {
        ZoneScoped;
        if (GetConnectionState() != ConnectionState::DISCONNECTED) {
            Debug::LogError("Connection already started");
            return;
        }

        m_address = address;
        m_ports = ports;

        std::shared_ptr<TCPConnection<T>> connection = this->shared_from_this();
        asio::co_spawn(m_context, CoSeek(connection, connectionSeekCallback, callback), asio::detached);
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

        size_t requestID = m_fileCurrentID.fetch_add(1);
        m_fileNameMap.InsertOrAssign(requestID, std::string(fileName));

        std::unique_ptr<Package<T>> package = Package<T>::CreateUnique(static_cast<T>(0), std::move(requestID), std::string(requestedFilePath));
        package->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_REQUEST);
        Send(std::move(package));
    }

    void Disconnect() override {
        ZoneScoped;
        if (!m_socket.is_open() && !m_fileStreamSocket.is_open()) {
            SetConnectionState(ConnectionState::DISCONNECTED);
            m_context.stop();
            return;
        }

        CloseSocket(m_socket);
        CloseSocket(m_fileStreamSocket);

        SetConnectionState(ConnectionState::DISCONNECTED);

        m_receiveFileAwaitableFlag.Signal();
        m_sendMessageAwaitableFlag.Signal();
        m_sendFileAwaitableFlag.Signal();
    }

    void DestroyContext() override {
        ZoneScoped;
        if (!m_socket.is_open() && !m_fileStreamSocket.is_open()) {
            SetConnectionState(ConnectionState::DISCONNECTED);
            m_context.stop();
            return;
        }

        CloseSocket(m_socket);
        CloseSocket(m_fileStreamSocket);

        SetConnectionState(ConnectionState::DISCONNECTED);

        m_receiveFileAwaitableFlag.Signal();
        m_sendMessageAwaitableFlag.Signal();
        m_sendFileAwaitableFlag.Signal();

        m_context.stop();
    }

    NO_DISCARD IPAddress GetAddress() const override {
        return m_address;
    }

    NO_DISCARD std::array<uint16_t, 2> GetPorts() const override {
        return m_ports;
    }

private:
    static void CloseSocket(TCPSocket& socket) {
        if (!socket.is_open()) {
            return;
        }

        asio::error_code ec;
        socket.close(ec);
        if (ec && ec != asio::error::bad_descriptor && ec != asio::error::not_socket && ec != asio::error::connection_reset && ec != asio::error::not_connected) {
            Debug::LogError("Close error: " + ec.message());
       }
    }



    static asio::awaitable<void> CoStart(std::shared_ptr<TCPConnection<T>> connection, const std::function<void()> callback) {
        while (true) {
            try {
                connection->SetConnectionState(ConnectionState::CONNECTING);

                std::initializer_list<TCPEndpoint> connectionEndpoints = {TCPEndpoint(connection->m_address, connection->m_ports[0])};
                std::initializer_list<TCPEndpoint> fileStreamEndpoints = {TCPEndpoint(connection->m_address, connection->m_ports[1])};

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

                callback();
            } catch (const std::system_error& error) {
                const asio::error_code errorCode = error.code();

                if (errorCode == asio::error::eof ||
                    errorCode == asio::error::connection_reset ||
                    errorCode == asio::error::connection_aborted ||
                    errorCode == asio::error::connection_refused ||
                    errorCode == asio::error::host_unreachable ||
                    errorCode == asio::error::network_unreachable ||
                    errorCode == asio::error::timed_out ||
                    errorCode == asio::error::broken_pipe) {
                    continue;
                }

                Debug::LogError(errorCode.message());
                connection->Disconnect();
            }

            break;
        }
    }

    static asio::awaitable<void> CoSeek(std::shared_ptr<TCPConnection<T>> connection, const std::function<void()> connectionSeekCallback, const std::function<void()> callback) {
        try {
            connection->SetConnectionState(ConnectionState::CONNECTING);

            std::initializer_list<TCPEndpoint> connectionEndpoints = {TCPEndpoint(connection->m_address, connection->m_ports[0])};
            std::initializer_list<TCPEndpoint> fileStreamEndpoints = {TCPEndpoint(connection->m_address, connection->m_ports[1])};

            TCPAcceptor connectionAcceptor(connection->m_context, *connectionEndpoints.begin());
            TCPAcceptor fileStreamAcceptor(connection->m_context, *fileStreamEndpoints.begin());

            connection->m_address = connectionAcceptor.local_endpoint().address();
            connection->m_ports   = {connectionAcceptor.local_endpoint().port(), fileStreamAcceptor.local_endpoint().port()};

            connectionSeekCallback();

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

            callback();

        } catch (const std::system_error& error) {
            Debug::LogError(error.what());
            connection->Disconnect();
        }
    }

    static asio::awaitable<void> CoReceiveMessage(std::shared_ptr<TCPConnection<T>> connection) {
        try {
            PackageHeader header{};
            moodycamel::ProducerToken inQueueToken(connection->m_inQueue);

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                asio::mutable_buffer headerBuffer(&header, sizeof(PackageHeader));
                co_await asio::async_read(connection->m_socket, headerBuffer, asio::use_awaitable);

                header.FromBigEndianToNative();

                std::unique_ptr<Package<T>> package = std::make_unique<Package<T>>(header);
                asio::mutable_buffer packageBuffer(package->GetRawBody(), header.size);

                co_await asio::async_read(connection->m_socket, packageBuffer, asio::use_awaitable);

                if ((header.flags & PackageFlag::FILE_RECEIVE_INFO) != 0) {
                    connection->m_fileInfoQueue.push_back(std::move(package));
                    connection->m_receiveFileAwaitableFlag.Signal();
                    continue;
                }

                if ((header.flags & PackageFlag::FILE_REQUEST) != 0) {
                    connection->m_fileRequestQueue.push_back(std::move(package));
                    connection->m_sendFileAwaitableFlag.Signal();
                    continue;
                }

                std::unique_ptr<PackageIn<T>> packageIn = std::make_unique<PackageIn<T>>();
                packageIn->Package = std::move(package);
                packageIn->Connection = connection;

                connection->m_inQueue.enqueue(inQueueToken, std::move(packageIn));
            }
        } catch (const asio::error_code& errorCode) {
            if (errorCode == asio::error::eof || errorCode == asio::error::connection_reset || errorCode == asio::error::operation_aborted || errorCode == asio::error::connection_aborted || errorCode == asio::error::broken_pipe)  {
                Debug::Log("Connection closed cleanly by peer.");
            } else if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(errorCode.message());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoReceiveFile(std::shared_ptr<TCPConnection<T>> connection) {
        try {
            if (connection->GetConnectionState() != ConnectionState::CONNECTED) co_return;

            std::vector<char> dataBuffer(FILE_BUFFER_SIZE);
            co_await connection->m_receiveFileAwaitableFlag.Wait();

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                if (!connection->m_fileInfoQueue.empty()) {
                    std::unique_ptr<Package<T>> package = std::move(connection->m_fileInfoQueue.front());
                    connection->m_fileInfoQueue.pop_front();
                    size_t requestID;
                    PackageSizeInt size;

                    package->GetValue(requestID);
                    package->GetValue(size);

                    if (!connection->m_fileNameMap.Contains(requestID)) {
                        Debug::LogError("File ID do not exist");
                        connection->Disconnect();
                        co_return;
                    }

                    std::string filename = connection->m_fileNameMap.Get(requestID).value();
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
        } catch (const asio::error_code& errorCode) {
            if (errorCode == asio::error::eof || errorCode == asio::error::connection_reset || errorCode == asio::error::operation_aborted || errorCode == asio::error::connection_aborted || errorCode == asio::error::broken_pipe)  {
                Debug::Log("Connection closed cleanly by peer.");
            } else if (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                Debug::LogError(errorCode.message());
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
                    PackageHeader header = package->GetHeader();

                    std::vector<asio::const_buffer> buffers = {
                        asio::const_buffer(&header, sizeof(header)),
                        asio::const_buffer(package->GetRawBody(), header.size)
                    };

                    header.FromNativeToBigEndian();
                    co_await asio::async_write(connection->m_socket, buffers, asio::use_awaitable);
                } else {
                    connection->m_sendMessageAwaitableFlag.Reset();
                    co_await connection->m_sendMessageAwaitableFlag.Wait();
                }
            }
        } catch (const std::system_error& error) {
            if (connection->GetConnectionState() == ConnectionState::CONNECTED && error.code() != asio::error::operation_aborted && error.code() != asio::error::connection_aborted) {
                Debug::LogError(error.what());
            }

            connection->Disconnect();
            co_return;
        }
    }

    static asio::awaitable<void> CoSendFile(std::shared_ptr<TCPConnection<T>> connection) {
        try {
            if (connection->GetConnectionState() != ConnectionState::CONNECTED) co_return;
            std::vector<char> fileBuffer(FILE_BUFFER_SIZE);

            co_await connection->m_sendFileAwaitableFlag.Wait();

            while (connection->GetConnectionState() == ConnectionState::CONNECTED) {
                if (!connection->m_fileRequestQueue.empty()) {
                    std::unique_ptr<Package<T>> package = std::move(connection->m_fileRequestQueue.front());
                    connection->m_fileRequestQueue.pop_front();

                    size_t      requestID;
                    std::string path;

                    package->GetValue(requestID);
                    package->GetValue(path);

                    std::filesystem::path filePath(path);

                    if (!std::filesystem::exists(filePath)) {
                        Debug::LogError("File path doesnt exist");
                        connection->Disconnect();
                        co_return;
                    }

                    std::ifstream fileStream(filePath, std::ios::binary | std::ios::in);
                    if (!fileStream.is_open()) {
                        Debug::LogError("Could not open file");
                        connection->Disconnect();
                        co_return;
                    }

                    PackageSizeInt size = std::filesystem::file_size(filePath);
                    {
                        std::unique_ptr<Package<T>> fileInfo = Package<T>::CreateUnique(static_cast<T>(0), size_t{requestID}, PackageSizeInt{size});
                        fileInfo->GetHeader().flags = static_cast<uint8_t>(PackageFlag::FILE_RECEIVE_INFO);
                        connection->Send(std::move(fileInfo));
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
            if (connection->GetConnectionState() == ConnectionState::CONNECTED && error.code() != asio::error::operation_aborted && error.code() != asio::error::connection_aborted) {
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
    moodycamel::ConcurrentQueue<std::unique_ptr<PackageIn<T>>>& m_inQueue;

    std::deque<std::unique_ptr<Package<T>>> m_fileRequestQueue;
    std::deque<std::unique_ptr<Package<T>>> m_fileInfoQueue;

    ConcurrentUnorderedMap<size_t, std::string> m_fileNameMap;
    std::atomic<size_t>                         m_fileCurrentID{0};

    IPAddress               m_address;
    std::array<uint16_t, 2> m_ports;
};

#endif //P2P_TCP_CONNECTION_H