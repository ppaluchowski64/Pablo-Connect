#ifndef TCP_TLS_COMMON_H
#define TCP_TLS_COMMON_H

#include <type_traits>
#include <string>
#include <vector>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <DebugLog.h>

typedef uint32_t PackageSizeInt;
typedef uint16_t PackageTypeInt;

typedef asio::io_context IOContext;
typedef asio::ssl::context SSLContext;
typedef asio::ip::tcp::socket TCPSocket;
typedef asio::ip::tcp::resolver TCPResolver;
typedef asio::ssl::stream<asio::ip::tcp::socket> SSLSocket;
typedef asio::ip::tcp::endpoint TCPEndpoint;
typedef asio::ip::tcp::acceptor TCPAcceptor;
typedef asio::ssl::context::method SSLMethod;
typedef asio::ssl::stream_base SSLStreamBase;
typedef asio::ip::address IPAddress;

constexpr PackageSizeInt MAX_NON_FILE_PACKAGE_SIZE = 1024 * 32;
constexpr PackageSizeInt MAX_FULL_PACKAGE_SIZE = 1024 * 64;
constexpr PackageSizeInt MAX_FILE_NAME_SIZE = 255;
constexpr PackageSizeInt FILE_BUFFER_SIZE = 128 * 1024;
constexpr uint32_t PACKAGES_WARN_THRESHOLD = 10000;
constexpr uint16_t SSL_CONNECTION_PORT = 50000;
constexpr uint16_t SSL_FILE_STREAM_PORT = 50001;


template <typename T>
concept PackageType = std::is_same_v<std::underlying_type_t<T>, PackageTypeInt>;

template <typename T>
concept StandardLayaut = std::is_standard_layout_v<T>;

template<typename T>
struct is_std_layout_vector : std::false_type {};

template<typename U>
struct is_std_layout_vector<std::vector<U>>
    : std::bool_constant<std::is_standard_layout_v<U>> {};

template<typename T>
concept StdLayoutOrVecOrString =
    std::is_standard_layout_v<T> ||
    is_std_layout_vector<T>::value ||
    std::is_same_v<T, std::string>;

enum class ConnectionState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

#endif //TCP_TLS_COMMON_H
