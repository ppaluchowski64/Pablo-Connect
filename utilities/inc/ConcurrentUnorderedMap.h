#ifndef CONCURRENT_UNORDERED_MAP_H
#define CONCURRENT_UNORDERED_MAP_H

#include <unordered_map>
#include <mutex>
#include <optional>

template<typename Key, typename Value, typename Hash = std::hash<Key>>
class ConcurrentUnorderedMap final {
public:
    ConcurrentUnorderedMap() = default;
    ConcurrentUnorderedMap(const ConcurrentUnorderedMap&) = delete;
    ConcurrentUnorderedMap& operator=(const ConcurrentUnorderedMap&) = delete;

    void InsertOrAssign(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map[key] = value;
    }

    void InsertOrAssign(Key&& key, const Value& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map[key] = value;
    }

    void InsertOrAssign(const Key& key, Value&& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map[key] = value;
    }

    void InsertOrAssign(Key&& key, Value&& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map[key] = value;
    }

    void Erase(const Key& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map.erase(key);
    }

    void Erase(Key&& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map.erase(key);
    }

    std::optional<Value> Get(const Key& key) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_map.contains(key)) {
            return m_map.at(key);
        }

        return std::nullopt;
    }

    std::optional<Value> Get(Key&& key) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_map.contains(key)) {
            return m_map.at(key);
        }

        return std::nullopt;
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.size();
    }

    bool Contains(const Key& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.contains(key);
    }

    bool Contains(Key&& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.contains(key);
    }

private:
    std::unordered_map<Key, Value, Hash> m_map;
    std::mutex m_mutex;
};

#endif //CONCURRENT_UNORDERED_MAP_H
