#ifndef P2P_CONNECTION_PARENT_H
#define P2P_CONNECTION_PARENT_H

#include <AsioCommon.h>
#include <Package.h>
#include <tracy/Tracy.hpp>

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

template <PackageType T>
class ConnectionParent {
public:
    virtual ~ConnectionParent() = default;
    virtual void Start(IPAddress address, std::array<uint16_t, 2> ports, std::function<void()> callback) = 0;
    virtual void Seek(IPAddress address, std::array<uint16_t, 2> ports, std::function<void()> connectionSeekCallback, std::function<void()> callback) = 0;
    NO_DISCARD virtual ConnectionState GetConnectionState() const = 0;
    virtual void Send(std::unique_ptr<Package<T>>&& package) = 0;
    virtual void RequestFile(const std::string& requestedFilePath, const std::string& fileName) = 0;
    virtual void Disconnect() = 0;
    virtual void DestroyContext() = 0;

    NO_DISCARD virtual std::array<uint16_t, 2> GetPorts() const = 0;
    NO_DISCARD virtual IPAddress GetAddress() const = 0;
};

template <PackageType T>
struct PackageIn {
    std::unique_ptr<Package<T>>          package;
    std::shared_ptr<ConnectionParent<T>> connection;

    PackageIn() noexcept
        : package(nullptr), connection(nullptr) {}

    PackageIn(PackageIn&& other) noexcept
        : package(std::move(other.package)),
          connection(std::move(other.connection)) {}

    PackageIn& operator=(PackageIn&& other) noexcept {
        if (this != &other) {
            package = std::move(other.package);
            connection = std::move(other.connection);
        }
        return *this;
    }

    PackageIn(const PackageIn&) = delete;
    PackageIn& operator=(const PackageIn&) = delete;
};


#endif //P2P_CONNECTION_PARENT_H
