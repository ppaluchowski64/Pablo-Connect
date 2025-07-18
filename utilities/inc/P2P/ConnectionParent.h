#ifndef P2P_CONNECTION_PARENT_H
#define P2P_CONNECTION_PARENT_H

#include <P2P/Common.h>
#include <P2P/Package.h>

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

template <PackageType T>
class ConnectionParent {
public:
    virtual ~ConnectionParent() = default;
    virtual void Start(const TCPEndpoint& connectionEndpoint, const TCPEndpoint& fileStreamEndpoint, ConnectionCallbackData callbackData) = 0;
    virtual void Seek(TCPAcceptor& connectionAcceptor, TCPAcceptor& fileStreamAcceptor, ConnectionCallbackData callbackData) = 0;
    NO_DISCARD virtual ConnectionState GetConnectionState() const = 0;
    virtual void Send(std::unique_ptr<Package<T>>&& package) = 0;
    virtual void RequestFile(const std::string& requestedFilePath, const std::string& fileName) = 0;
    virtual void Disconnect() = 0;
};

template <PackageType T>
struct PackageIn {
    std::unique_ptr<Package<T>>          Package;
    std::shared_ptr<ConnectionParent<T>> Connection;

    PackageIn() noexcept
        : Package(nullptr), Connection(nullptr) {}

    PackageIn(PackageIn&& other) noexcept
        : Package(std::move(other.Package)),
          Connection(std::move(other.Connection)) {}

    PackageIn& operator=(PackageIn&& other) noexcept {
        if (this != &other) {
            Package = std::move(other.Package);
            Connection = std::move(other.Connection);
        }
        return *this;
    }

    PackageIn(const PackageIn&) = delete;
    PackageIn& operator=(const PackageIn&) = delete;
};


#endif //P2P_CONNECTION_PARENT_H
