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

    ~Package() {
        delete[] m_rawBody;
    }

    template <StdLayoutOrVecOrString... Args>
    static Package CreateFromData(T type, const Args&... args) {
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0
        };
        (CalculateElementSize(args, header), ...);
        Package newPackage(header);
        PackageSizeInt offset = 0;
        ((InsertElementToBody(args, newPackage, offset)), ...);

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
