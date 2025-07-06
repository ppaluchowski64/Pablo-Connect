#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <utility>
#include <TSDeque.h>
#include <DebugLog.h>
#include <asio/ssl/context_base.hpp>
#include <SSL/Package.h>
#include <asio/buffer.hpp>
#include <SSL/CertificateManager.h>

typedef asio::io_context IOContext;
typedef asio::ssl::context SSLContext;
typedef asio::ip::tcp::socket TCPSocket;
typedef asio::ssl::stream<asio::ip::tcp::socket> SSLSocket;
typedef asio::ip::tcp::endpoint Endpoint;
typedef asio::ip::tcp::acceptor Acceptor;
typedef asio::ssl::context::method SSLMethod;
typedef asio::ssl::stream_base SSLStreamBase;

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
    Connection(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque)
    : m_context(ioContext), m_sslContext(sslContext), m_sslSocket(ioContext, sslContext), m_inDeque(inDeque) { }

    Connection() = delete;

    static void Start(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque, Endpoint endpoint, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);

        Debug::Log("Connection started");

        asio::async_connect(connection->m_sslSocket.lowest_layer(), std::initializer_list<Endpoint>({std::move(endpoint)}), [connection, callback](const asio::error_code& errorCode, const asio::ip::tcp::endpoint&) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            Debug::Log("ht");

            connection->m_sslSocket.async_handshake(SSLStreamBase::client, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                    return;
                }

                Debug::Log("Accepted connection to " + connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" + std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()));
                connection-> m_connectionID = s_currentConnectionID++;
                connection->ReceiveMessage();
                callback(connection);
            });
        });
    }

    static void Seek(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque, Acceptor& acceptor, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);

        Debug::Log("Seeking connection");

        acceptor.async_accept(connection->m_sslSocket.lowest_layer(), [connection, callback](const asio::error_code& errorCode) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            Debug::Log("ht");

            connection->m_sslSocket.async_handshake(SSLStreamBase::server, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
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

        if (!m_sending) {
            SendMessage();
        }
    }

    uint16_t GetConnectionID() const {
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

        m_sslSocket.async_write_some(buffers, [connection](const asio::error_code& errorCode, const size_t) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            if (connection->m_outDeque.empty()) {
                connection->m_sending = false;
                return;
            }

            connection->SendMessage();
        });
    }

    void ReceiveMessage() {
        std::shared_ptr<Connection<T>> connection = this->shared_from_this();
        m_packageIn = std::make_unique<Package<T>>(PackageHeader());
        asio::mutable_buffer headerBuf(&m_packageIn->GetHeader(), sizeof(PackageHeader));

        asio::async_read(m_sslSocket, headerBuf, [connection](const asio::error_code& errorCode, const size_t bytes) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            PackageHeader header = connection->m_packageIn->GetHeader();
            connection->m_packageIn = std::make_unique<Package<T>>(header);
            asio::mutable_buffer bodyBuf(connection->m_packageIn->GetRawBody(), header.size);

            asio::async_read(connection->m_sslSocket, bodyBuf, [connection](const asio::error_code& errorCode, const size_t bytes) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                    return;
                }

                PackageIn packageIn {
                    std::move(connection->m_packageIn),
                    connection
                };

                connection->m_inDeque.push_back(std::move(packageIn));
                connection->ReceiveMessage();
            });
        });
    }

    IOContext&  m_context;
    SSLContext& m_sslContext;
    SSLSocket   m_sslSocket;
    uint16_t    m_connectionID{};
    bool        m_sending{false};

    std::unique_ptr<Package<T>> m_packageOut;
    std::unique_ptr<Package<T>> m_packageIn;

    ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
    ts::deque<PackageIn<T>>&               m_inDeque;

    static uint16_t s_currentConnectionID;
};

template <PackageType T>
uint16_t Connection<T>::s_currentConnectionID = 2;

#endif //CONNECTION_H
