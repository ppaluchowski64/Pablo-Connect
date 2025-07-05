#ifndef PACKAGE_H
#define PACKAGE_H

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

#include <type_traits>
#include <string>
#include <vector>

typedef uint16_t PackageSizeInt;
typedef uint16_t PackageTypeInt;

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

struct PackageHeader {
    PackageTypeInt type;
    PackageSizeInt size;
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

    NO_DISCARD const uint8_t* GetRawBody() const {
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

private:
    template <typename T0>
    static void InsertElementToBody(const T0& arg, Package& package, uint16_t& offset) {
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