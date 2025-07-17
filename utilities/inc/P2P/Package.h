#ifndef PACKAGE_H
#define PACKAGE_H

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

#include <DebugLog.h>
#include <type_traits>
#include <string>
#include <TCP_TLS_COMMON/Common.h>

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
};

template <PackageType T>
class Package final {
public:
    Package() = delete;
    explicit Package(const PackageHeader header) : m_header(header) {
        m_rawBody = new uint8_t[m_header.size];
    }

    NO_DISCARD PackageHeader& GetHeader() {
        return m_header;
    }

    NO_DISCARD uint8_t* GetRawBody() const {
        return m_rawBody;
    }

    template <StdLayoutOrVecOrString T0>
    NO_DISCARD T0 GetValue() {
        if constexpr (std::is_same_v<T0, std::string>) {
            T0 element{};
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
        } else if constexpr (is_std_layout_vector<T0>::value) {
            T0 element{};
            PackageSizeInt vectorSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            std::memcpy(&vectorSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            m_readOffset += sizeof(PackageSizeInt);
            PackageSizeInt dataSize = vectorSize * sizeof(typename T0::value_type);

            if (m_readOffset + dataSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
                return element;
            }

            element.resize(vectorSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, dataSize);
            m_readOffset += dataSize;

            return std::move(element);
        } else {
            const PackageSizeInt size = sizeof(T0);
            T0 element{};

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
        if constexpr (std::is_same_v<T0, std::string>) {
            PackageSizeInt stringSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&stringSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            m_readOffset += sizeof(PackageSizeInt);

            if (m_readOffset + stringSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            element.resize(stringSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, stringSize);
            m_readOffset += stringSize;
        } else if constexpr (is_std_layout_vector<T0>::value) {
            PackageSizeInt vectorSize;

            if (m_readOffset + sizeof(PackageSizeInt) > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&vectorSize, m_rawBody + m_readOffset, sizeof(PackageSizeInt));
            m_readOffset += sizeof(PackageSizeInt);
            const PackageSizeInt dataSize = vectorSize * sizeof(typename T0::value_type);

            if (m_readOffset + dataSize > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            element.resize(vectorSize);
            std::memcpy(element.data(), m_rawBody + m_readOffset, dataSize);
            m_readOffset += dataSize;
        } else {
            const PackageSizeInt size = sizeof(T0);

            if (m_readOffset + size > m_header.size) {
                Debug::LogError("m_readOffset out of body scope");
            }

            std::memcpy(&element, m_rawBody + m_readOffset, size);
            m_readOffset += size;
        }
    }

    ~Package() {
        delete[] m_rawBody;
    }

    template <StdLayoutOrVecOrString... Args>
    static Package Create(T type, const Args&... args) {
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0
        };
        (CalculateElementSize(args, header), ...);
        Package newPackage(header);
        PackageSizeInt offset = 0;
        (InsertElementToBody(args, newPackage, offset), ...);

        return newPackage;
    }

    template <StdLayoutOrVecOrString... Args>
    static std::unique_ptr<Package> CreateUnique(T type, const Args&... args) {
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0
        };
        (CalculateElementSize(args, header), ...);
        std::unique_ptr<Package> newPackage = std::make_unique<Package>(header);
        PackageSizeInt offset = 0;
        (InsertElementToBody(args, *newPackage, offset), ...);

        return std::move(newPackage);
    }

private:
    template <typename T0>
    static void InsertElementToBody(const T0& arg, Package& package, PackageSizeInt& offset) {
        using T = std::decay_t<T0>;

        if constexpr (std::is_same_v<T, std::string>) {
            const auto size = static_cast<PackageSizeInt>(arg.size());
            std::memcpy(package.m_rawBody + offset, &size, sizeof(PackageSizeInt));
            std::memcpy(package.m_rawBody + offset + sizeof(PackageSizeInt), arg.data(), arg.size());
            offset += sizeof(PackageSizeInt) + arg.size();
        } else if constexpr (is_std_layout_vector<T>::value) {
            const auto size = static_cast<PackageSizeInt>(arg.size());
            std::memcpy(package.m_rawBody + offset, &size, sizeof(PackageSizeInt));
            std::memcpy(package.m_rawBody + offset + sizeof(PackageSizeInt), arg.data(), arg.size() * sizeof(typename T::value_type));
            offset += sizeof(PackageSizeInt) + arg.size() * sizeof(typename T::value_type);
        } else {
            std::memcpy(package.m_rawBody + offset, &arg, sizeof(T));
            offset += sizeof(T);
        }
    }

    template <typename T0>
    static void CalculateElementSize(const T0& arg, PackageHeader& packageHeader) {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, std::string>) {
            packageHeader.size += arg.size() + sizeof(PackageSizeInt);
        } else if constexpr (is_std_layout_vector<T>::value) {
            packageHeader.size += arg.size() * sizeof(typename T::value_type) + sizeof(PackageSizeInt);
        } else {
            packageHeader.size += sizeof(T);
        }
    }

    PackageHeader  m_header{};
    uint8_t*       m_rawBody{nullptr};
    PackageSizeInt m_readOffset{0};

};

#endif //PACKAGE_H