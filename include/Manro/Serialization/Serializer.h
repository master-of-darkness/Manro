#pragma once

#include <Manro/Core/Types.h>
#include <vector>
#include <cstring>
#include <type_traits>

namespace Engine {
    class BinaryArchive {
    public:
        BinaryArchive() = default;

        BinaryArchive(const std::vector<u8> &data) : m_Buffer(data), m_ReadOffset(0) {
        }

        template<typename T>
            requires std::is_trivially_copyable_v<T>
        void Write(const T &data) {
            size_t size = sizeof(T);
            size_t current = m_Buffer.size();
            m_Buffer.resize(current + size);
            std::memcpy(m_Buffer.data() + current, &data, size);
        }

        template<typename T>
            requires std::is_trivially_copyable_v<T>
        void Write(const std::vector<T> &data) {
            u32 count = static_cast<u32>(data.size());
            Write(count);

            size_t size = count * sizeof(T);
            size_t current = m_Buffer.size();
            m_Buffer.resize(current + size);
            std::memcpy(m_Buffer.data() + current, data.data(), size);
        }

        template<typename T>
            requires std::is_trivially_copyable_v<T>
        void Read(T &outData) {
            size_t size = sizeof(T);
            std::memcpy(&outData, m_Buffer.data() + m_ReadOffset, size);
            m_ReadOffset += size;
        }

        template<typename T>
            requires std::is_trivially_copyable_v<T>
        void Read(std::vector<T> &outData) {
            u32 count = 0;
            Read(count);
            outData.resize(count);

            size_t size = count * sizeof(T);
            std::memcpy(outData.data(), m_Buffer.data() + m_ReadOffset, size);
            m_ReadOffset += size;
        }

        const std::vector<u8> &GetBuffer() const { return m_Buffer; }

        void Reset() {
            m_Buffer.clear();
            m_ReadOffset = 0;
        }

    private:
        std::vector<u8> m_Buffer;
        size_t m_ReadOffset{0};
    };

#define SERIALIZE(...) \
    template<typename Archive> \
    void Serialize(Archive& archive) { \
        archive.Write(__VA_ARGS__); \
    } \
    template<typename Archive> \
    void Deserialize(Archive& archive) { \
        archive.Read(__VA_ARGS__); \
    }
} // namespace Engine
