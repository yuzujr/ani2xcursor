#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>

namespace ani2xcursor::utils {

// Read little-endian integers from byte span with bounds checking
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data), pos_(0) {}

    [[nodiscard]] size_t position() const {
        return pos_;
    }
    [[nodiscard]] size_t remaining() const {
        return data_.size() - pos_;
    }
    [[nodiscard]] bool eof() const {
        return pos_ >= data_.size();
    }

    void seek(size_t pos) {
        if (pos > data_.size()) {
            throw std::out_of_range("ByteReader::seek: position out of range");
        }
        pos_ = pos;
    }

    void skip(size_t count) {
        if (pos_ + count > data_.size()) {
            throw std::out_of_range("ByteReader::skip: would exceed buffer");
        }
        pos_ += count;
    }

    [[nodiscard]] uint8_t read_u8() {
        check_remaining(1);
        return data_[pos_++];
    }

    [[nodiscard]] uint16_t read_u16_le() {
        check_remaining(2);
        uint16_t val =
            static_cast<uint16_t>(data_[pos_]) | (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return val;
    }

    [[nodiscard]] uint32_t read_u32_le() {
        check_remaining(4);
        uint32_t val = static_cast<uint32_t>(data_[pos_]) |
                       (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                       (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                       (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return val;
    }

    [[nodiscard]] int32_t read_i32_le() {
        return static_cast<int32_t>(read_u32_le());
    }

    [[nodiscard]] std::string_view read_fourcc() {
        check_remaining(4);
        std::string_view result(reinterpret_cast<const char*>(data_.data() + pos_), 4);
        pos_ += 4;
        return result;
    }

    [[nodiscard]] std::span<const uint8_t> read_bytes(size_t count) {
        check_remaining(count);
        auto result = data_.subspan(pos_, count);
        pos_ += count;
        return result;
    }

    [[nodiscard]] std::span<const uint8_t> peek_bytes(size_t count) const {
        if (pos_ + count > data_.size()) {
            throw std::out_of_range("ByteReader::peek_bytes: insufficient data");
        }
        return data_.subspan(pos_, count);
    }

    [[nodiscard]] std::span<const uint8_t> subspan(size_t offset, size_t count) const {
        if (offset + count > data_.size()) {
            throw std::out_of_range("ByteReader::subspan: out of range");
        }
        return data_.subspan(offset, count);
    }

    [[nodiscard]] std::span<const uint8_t> data() const {
        return data_;
    }

private:
    void check_remaining(size_t need) const {
        if (pos_ + need > data_.size()) {
            throw std::out_of_range("ByteReader: insufficient data");
        }
    }

    std::span<const uint8_t> data_;
    size_t pos_;
};

// Helper to read little-endian from raw pointer
inline uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t read_i32_le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32_le(p));
}

}  // namespace ani2xcursor::utils
