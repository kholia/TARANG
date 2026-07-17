#pragma once

#include <stddef.h>
#include <stdint.h>

namespace meshtastic_text
{

constexpr uint32_t TEXT_MESSAGE_APP = 1;
constexpr size_t MAX_TEXT_SIZE = 220;

struct DecodedText {
    const uint8_t *bytes = nullptr;
    size_t length = 0;
};

inline size_t encodeVarint(uint32_t value, uint8_t *output, size_t capacity)
{
    size_t used = 0;
    do {
        if (used == capacity)
            return 0;
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0)
            byte |= 0x80;
        output[used++] = byte;
    } while (value != 0);
    return used;
}

inline bool decodeVarint(const uint8_t *input, size_t length, size_t *offset, uint32_t *value)
{
    uint32_t result = 0;
    for (uint8_t shift = 0; shift < 35; shift += 7) {
        if (*offset >= length)
            return false;
        const uint8_t byte = input[(*offset)++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

inline size_t encode(const uint8_t *text, size_t textLength, uint8_t *output, size_t capacity)
{
    if (textLength == 0 || textLength > MAX_TEXT_SIZE || capacity < 4)
        return 0;

    size_t offset = 0;
    output[offset++] = 0x08;
    output[offset++] = TEXT_MESSAGE_APP;
    output[offset++] = 0x12;
    const size_t lengthBytes = encodeVarint(textLength, output + offset, capacity - offset);
    if (lengthBytes == 0 || offset + lengthBytes + textLength > capacity)
        return 0;
    offset += lengthBytes;
    for (size_t i = 0; i < textLength; ++i)
        output[offset++] = text[i];
    return offset;
}

inline bool decode(const uint8_t *data, size_t length, DecodedText *text)
{
    size_t offset = 0;
    uint32_t portnum = 0;
    bool havePort = false;
    text->bytes = nullptr;
    text->length = 0;

    while (offset < length) {
        uint32_t key = 0;
        if (!decodeVarint(data, length, &offset, &key) || key == 0)
            return false;
        const uint32_t field = key >> 3;
        const uint8_t wireType = key & 0x07;

        if (wireType == 0) {
            uint32_t value = 0;
            if (!decodeVarint(data, length, &offset, &value))
                return false;
            if (field == 1) {
                portnum = value;
                havePort = true;
            }
        } else if (wireType == 1) {
            if (length - offset < 8)
                return false;
            offset += 8;
        } else if (wireType == 2) {
            uint32_t fieldLength = 0;
            if (!decodeVarint(data, length, &offset, &fieldLength) || fieldLength > length - offset)
                return false;
            if (field == 2) {
                text->bytes = data + offset;
                text->length = fieldLength;
            }
            offset += fieldLength;
        } else if (wireType == 5) {
            if (length - offset < 4)
                return false;
            offset += 4;
        } else {
            return false;
        }
    }

    return havePort && portnum == TEXT_MESSAGE_APP && text->bytes != nullptr && text->length > 0;
}

} // namespace meshtastic_text
