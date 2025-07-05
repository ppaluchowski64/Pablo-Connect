#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <utility>
#include <TSDeque.h>
#include <DebugLog.h>
#include <asio/ssl/context_base.hpp>
#include <SSL/Package.h>
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
    std::unique_ptr<Package<T>> package;
    std::shared_ptr<Connection<T>> connection;
};

template <PackageType T>
class Connection final : public std::enable_shared_from_this<Connection<T>> {
public:
    Connection() = delete;

    static void Start(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque, Endpoint endpoint, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);
        asio::async_connect(connection->m_sslSocket.lowest_layer(), std::initializer_list<Endpoint>({std::move(endpoint)}), [connection, callback](const asio::error_code& errorCode, const asio::ip::tcp::endpoint&) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            connection->m_sllSocket->async_handshake(SSLStreamBase::client, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                    return;
                }

                Debug::Log("Accepted connection to " + connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" + std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()));
                connection->m_connection = s_currentConnectionID++;
                callback(connection);
            });
        });
    }

    static void Seek(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque, Acceptor& acceptor, std::function<void(std::shared_ptr<Connection<T>>)> callback) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(ioContext, sslContext, inDeque);
        acceptor.async_accept(connection->m_sslSocket.lowest_layer(), [connection, callback](const asio::error_code& errorCode) {
            if (errorCode) {
                Debug::LogError(errorCode.message());
                return;
            }

            connection->m_sslSocket.async_handshake(SSLStreamBase::server, [connection, callback](const asio::error_code& errorCode) {
                if (errorCode) {
                    Debug::LogError(errorCode.message());
                }

                Debug::Log("Accepted connection from " + connection->m_sslSocket.lowest_layer().remote_endpoint().address().to_string() + ":" + std::to_string(connection->m_sslSocket.lowest_layer().remote_endpoint().port()));
                connection->m_connection = s_currentConnectionID++;
                callback(connection);
            });
        });
    }

    void Send(std::unique_ptr<Package<T>>&& package) {
        m_inDeque.push_back(std::move(package));
    }

private:
    Connection(IOContext& ioContext, SSLContext& sslContext, ts::deque<PackageIn<T>>& inDeque)
    : m_context(ioContext), m_sslContext(sslContext), m_sslSocket(ioContext, sslContext), m_inDeque(inDeque) { }


    IOContext&  m_context;
    SSLContext& m_sslContext;
    SSLSocket   m_sslSocket;
    uint16_t    m_connectionID{};

    ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
    ts::deque<PackageIn<T>>&               m_inDeque;

    static uint16_t s_currentConnectionID;
};

template <PackageType T>
uint16_t Connection<T>::s_currentConnectionID = 2;

#endif //CONNECTION_H
