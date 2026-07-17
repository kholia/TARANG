#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace meshtastic_relay
{

constexpr size_t MAX_FRAME_SIZE = 255;
constexpr size_t HEADER_SIZE = 16;
constexpr uint8_t HOP_LIMIT_MASK = 0x07;
constexpr uint8_t HOP_START_MASK = 0xE0;
constexpr uint8_t HOP_START_SHIFT = 5;
constexpr uint8_t NO_NEXT_HOP_PREFERENCE = 0;

struct __attribute__((packed)) PacketHeader {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel;
    uint8_t nextHop;
    uint8_t relayNode;
};

static_assert(sizeof(PacketHeader) == HEADER_SIZE, "Meshtastic radio header must remain 16 bytes");

enum class Decision : uint8_t {
    RELAY,
    TOO_SHORT,
    TOO_LONG,
    INVALID_SENDER,
    INVALID_PACKET_ID,
    FROM_US,
    TO_US,
    NO_HOPS_LEFT,
    OTHER_NEXT_HOP,
};

inline PacketHeader readHeader(const uint8_t *frame)
{
    PacketHeader header{};
    memcpy(&header, frame, sizeof(header));
    return header;
}

inline void writeHeader(uint8_t *frame, const PacketHeader &header)
{
    memcpy(frame, &header, sizeof(header));
}

inline uint8_t hopLimit(const PacketHeader &header)
{
    return header.flags & HOP_LIMIT_MASK;
}

inline uint8_t hopStart(const PacketHeader &header)
{
    return (header.flags & HOP_START_MASK) >> HOP_START_SHIFT;
}

inline uint8_t effectiveNextHop(const PacketHeader &header)
{
    return hopStart(header) == 0 ? NO_NEXT_HOP_PREFERENCE : header.nextHop;
}

inline Decision inspect(const uint8_t *frame, size_t length, uint32_t ourNode)
{
    if (length < HEADER_SIZE)
        return Decision::TOO_SHORT;
    if (length > MAX_FRAME_SIZE)
        return Decision::TOO_LONG;

    const PacketHeader header = readHeader(frame);
    if (header.from == 0)
        return Decision::INVALID_SENDER;
    if (header.id == 0)
        return Decision::INVALID_PACKET_ID;
    if (header.from == ourNode)
        return Decision::FROM_US;
    if (header.to == ourNode)
        return Decision::TO_US;
    if (hopLimit(header) == 0)
        return Decision::NO_HOPS_LEFT;

    const uint8_t nextHop = effectiveNextHop(header);
    if (nextHop != NO_NEXT_HOP_PREFERENCE && nextHop != static_cast<uint8_t>(ourNode))
        return Decision::OTHER_NEXT_HOP;
    return Decision::RELAY;
}

inline bool prepareForRelay(uint8_t *frame, size_t length, uint32_t ourNode)
{
    if (inspect(frame, length, ourNode) != Decision::RELAY)
        return false;

    PacketHeader header = readHeader(frame);
    const uint8_t hops = hopLimit(header) - 1;
    header.flags = (header.flags & static_cast<uint8_t>(~HOP_LIMIT_MASK)) | hops;
    header.nextHop = NO_NEXT_HOP_PREFERENCE;
    header.relayNode = static_cast<uint8_t>(ourNode);
    writeHeader(frame, header);
    return true;
}

} // namespace meshtastic_relay
