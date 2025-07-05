#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <TSDeque.h>
#include <DebugLog.h>
#include <SSL/Package.h>
#include <SSL/CertificateManager.h>

typedef asio::io_context IOContext;
typedef asio::ssl::context SSLContext;
typedef asio::ip::tcp::socket TCPSocket;
typedef asio::ssl::stream<asio::ip::tcp::socket> SSLSocket;
typedef asio::ip::tcp::endpoint Endpoint;
typedef asio::ip::tcp::acceptor Acceptor;

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

    static std::shared_ptr<Connection> Create(IOContext& iocontext, SSLContext& sslcontext, ts::deque<PackageIn<T>>& inDeque, Endpoint endpoint) {
        std::shared_ptr<Connection<T>> connection = std::make_shared<Connection<T>>(iocontext, sslcontext, inDeque);
        connection->m_socket = TCPSocket(iocontext);
        connection->m_connection = 1;
        connection->Connect(endpoint);

        return connection;
    }

    void Send(std::unique_ptr<Package<T>>&& package) {
        m_inDeque.push_back(std::move(package));
    }

private:
    Connection(IOContext& iocontext, SSLContext& sslcontext, ts::deque<PackageIn<T>>& inDeque)
    : m_context(iocontext), m_sslContext(sslcontext), m_inDeque(inDeque) { }

    void Connect(Endpoint endpoint) {
        auto self = this->shared_from_this();
        asio::async_connect(m_socket, std::initializer_list<Endpoint>({endpoint}), [self](const asio::error_code& errorCode, const asio::ip::tcp::endpoint& endpoint) {
            Debug::LogError(errorCode.message());
            return;
        });

        Debug::Log("Connected {" + m_socket.remote_endpoint().address().to_string() + ":" + std::to_string(m_socket.remote_endpoint().port()) + "}");
        self->m_connection = s_currentConnectionID++;
    }

    IOContext&  m_context;
    SSLContext& m_sslContext;
    TCPSocket&  m_socket;
    SSLSocket   m_sslSocket;
    uint16_t    m_connectionID;

    ts::deque<std::unique_ptr<Package<T>>> m_outDeque;
    ts::deque<PackageIn<T>>&               m_inDeque;

    static uint16_t s_currentConnectionID;
};

template <PackageType T>
uint16_t Connection<T>::s_currentConnectionID = 2;

#endif //CONNECTION_H
