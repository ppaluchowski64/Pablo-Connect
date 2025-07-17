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
    virtual void RequestFile(const std::string& requestedFilePath, std::string& fileName) = 0;
    virtual void Disconnect() = 0;
};

template <PackageType T>
struct PackageIn {
    std::shared_ptr<ConnectionParent<T>> Connection;
    std::unique_ptr<Package<T>>          Package;
};

#endif //P2P_CONNECTION_PARENT_H
