#ifndef PACKAGE_H
#define PACKAGE_H

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

#include <DebugLog.h>
#include <type_traits>
#include <string>
#include <AsioCommon.h>
#include <boost/endian/conversion.hpp>
#include <tracy/Tracy.hpp>

#include <fmt/ostream.h>

enum class PackageFlag : uint8_t {
    NONE               = 0,
    FILE_REQUEST       = 1 << 1,
    FILE_RECEIVE_INFO  = 1 << 2
};

inline uint8_t operator&(uint8_t l, PackageFlag r) {
    return l & static_cast<uint8_t>(r);
}

inline uint8_t operator&(PackageFlag l, uint8_t r) {
    return static_cast<uint8_t>(l) & r;
}

inline uint8_t operator|(uint8_t l, PackageFlag r) {
    return l | static_cast<uint8_t>(r);
}

inline uint8_t operator|(PackageFlag l, uint8_t r) {
    return static_cast<uint8_t>(l) | r;
}

inline uint8_t operator|(PackageFlag l, PackageFlag r) {
    return static_cast<uint8_t>(l) | static_cast<uint8_t>(r);
}

struct PackageHeader {
    PackageTypeInt type{};
    PackageSizeInt size{};
    uint8_t        flags{};

    void FromNativeToBigEndian() {
        boost::endian::native_to_big_inplace(type);
        boost::endian::native_to_big_inplace(size);
        boost::endian::native_to_big_inplace(flags);
    }

    void FromBigEndianToNative() {
        boost::endian::big_to_native_inplace(type);
        boost::endian::big_to_native_inplace(size);
        boost::endian::big_to_native_inplace(flags);
    }
};

inline std::ostream& operator<<(std::ostream& os, const PackageHeader& object) {
    os << "Type: " << static_cast<size_t>(object.type) << ", Size: " << static_cast<size_t>(object.size) << ", Flags: " << static_cast<size_t>(object.flags);
    return os;
}

template <>
struct fmt::formatter<PackageHeader> : fmt::ostream_formatter {};

template <PackageType T>
class Package final {
public:
    Package() {
        m_header = {0, 0};
        m_rawBody = nullptr;
    };

    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

    Package(Package&& other) noexcept
        : m_header(other.m_header), m_rawBody(other.m_rawBody), m_readOffset(other.m_readOffset) {
        other.m_rawBody = nullptr;
    }

    Package& operator=(Package&& other) noexcept {
        ZoneScoped;

        if (this != &other) {
            delete[] m_rawBody;

            m_header = other.m_header;
            m_rawBody = other.m_rawBody;
            m_readOffset = other.m_readOffset;

            other.m_rawBody = nullptr;
        }
        return *this;
    }

    ~Package() {
        delete[] m_rawBody;
    }

    explicit Package(const PackageHeader header) : m_header(header) {
        m_rawBody = new uint8_t[m_header.size];
    }

    NO_DISCARD PackageHeader& GetHeader() {
        return m_header;
    }

    NO_DISCARD PackageHeader GetHeaderCopy() const {
        return m_header;
    }

    NO_DISCARD uint8_t* GetRawBody() const {
        return m_rawBody;
    }

    template <StdLayoutOrVecOrString T0>
    NO_DISCARD T0 GetValue() {
        ZoneScoped;
        using T1 = std::decay_t<T0>;
        T1 element{};

        if constexpr (std::is_same_v<T1, std::string>) {
            PackageSizeInt stringSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            std::memcpy(&stringSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            m_readOffset += sizeof(PackageSizeInt);

            if (m_readOffset + stringSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            element.resize(stringSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, stringSize);
            m_readOffset += stringSize;

            return std::move(element);
        } else if constexpr (is_std_layout_vector<T1>::value) {
            PackageSizeInt vectorSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            std::memcpy(&vectorSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            m_readOffset += sizeof(PackageSizeInt);
            const PackageSizeInt dataSize = vectorSize * sizeof(typename T1::value_type);

            if (m_readOffset + dataSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            element.resize(vectorSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, dataSize);
            m_readOffset += dataSize;

            return std::move(element);
        } else {
            const PackageSizeInt size = sizeof(T1);

            if (m_readOffset + size > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            std::memcpy(&element, m_rawBody + m_readOffset, size);
            m_readOffset += size;

            return std::move(element);
        }
    }

    template <StdLayoutOrVecOrString T0>
    void GetValue(T0& element) {
        ZoneScoped;
        using T1 = std::decay_t<T0>;

        if constexpr (std::is_same_v<T1, std::string>) {
            PackageSizeInt stringSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&stringSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            boost::endian::big_to_native_inplace(stringSize);
            m_readOffset += sizeof(PackageSizeInt);

            if (m_readOffset + stringSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            element.resize(stringSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, stringSize);
            m_readOffset += stringSize;
        } else if constexpr (is_std_layout_vector<T1>::value) {
            PackageSizeInt vectorSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&vectorSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            boost::endian::big_to_native_inplace(vectorSize);
            m_readOffset += sizeof(PackageSizeInt);
            const PackageSizeInt dataSize = vectorSize * sizeof(typename T1::value_type);

            if (m_readOffset + dataSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            element.resize(vectorSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, dataSize);

            for (auto& item : element) {
                boost::endian::big_to_native_inplace(item);
            }

            m_readOffset += dataSize;
        } else {
            const PackageSizeInt size = sizeof(T1);

            if (m_readOffset + size > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&element, m_rawBody + m_readOffset, size);
            boost::endian::big_to_native_inplace(element);
            m_readOffset += size;
        }
    }

    template <StdLayoutOrVecOrString... Args>
    static Package Create(T type, Args&&... args) {
        ZoneScoped;
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0,
            0
        };

        (CalculateElementSize(args, header), ...);
        Package newPackage(header);
        PackageSizeInt offset = 0;
        (InsertElementToBody(args, newPackage, offset), ...);

        return newPackage;
    }

    template <StdLayoutOrVecOrString... Args>
    static std::unique_ptr<Package> CreateUnique(T type, Args&&... args) {
        ZoneScoped;
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0,
            0
        };

        (CalculateElementSize(args, header), ...);
        auto newPackage = std::make_unique<Package>(header);
        PackageSizeInt offset = 0;
        (InsertElementToBody(args, *newPackage, offset), ...);

        return newPackage;
    }

private:
    template <typename T0>
    static void InsertElementToBody(T0& arg, Package& package, PackageSizeInt& offset) {
        ZoneScoped;
        using T1 = std::decay_t<T0>;

        if constexpr (std::is_same_v<T1, std::string>) {
            auto size = static_cast<PackageSizeInt>(arg.size());
            boost::endian::native_to_big_inplace(size);
            std::memcpy(package.m_rawBody + offset, &size, sizeof(PackageSizeInt));
            std::memcpy(package.m_rawBody + offset + sizeof(PackageSizeInt), arg.data(), arg.size());
            offset += sizeof(PackageSizeInt) + arg.size();
        } else if constexpr (is_std_layout_vector<T1>::value) {
            auto size = static_cast<PackageSizeInt>(arg.size());
            boost::endian::native_to_big_inplace(size);

            for (auto& element : arg) {
                boost::endian::native_to_big_inplace(element);
            }

            std::memcpy(package.m_rawBody + offset, &size, sizeof(PackageSizeInt));
            std::memcpy(package.m_rawBody + offset + sizeof(PackageSizeInt), arg.data(), arg.size() * sizeof(typename T1::value_type));
            offset += sizeof(PackageSizeInt) + arg.size() * sizeof(typename T1::value_type);
        } else {
            boost::endian::native_to_big_inplace(arg);
            std::memcpy(package.m_rawBody + offset, &arg, sizeof(T1));
            offset += sizeof(T1);
        }
    }

    template <typename T0>
    static void CalculateElementSize(const T0& arg, PackageHeader& packageHeader) {
        ZoneScoped;
        using T1 = std::decay_t<T0>;
        if constexpr (std::is_same_v<T1, std::string>) {
            packageHeader.size += arg.size() + sizeof(PackageSizeInt);
        } else if constexpr (is_std_layout_vector<T1>::value) {
            packageHeader.size += arg.size() * sizeof(typename T1::value_type) + sizeof(PackageSizeInt);
        } else {
            packageHeader.size += sizeof(T1);
        }
    }

    PackageHeader  m_header{};
    uint8_t*       m_rawBody{nullptr};
    PackageSizeInt m_readOffset{0};

};

#endif //PACKAGE_H