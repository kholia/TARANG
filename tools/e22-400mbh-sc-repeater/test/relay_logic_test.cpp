#include "meshtastic_relay.h"
#include "meshtastic_text.h"

#include <assert.h>
#include <string.h>

using namespace meshtastic_relay;

namespace
{

constexpr uint32_t OUR_NODE = 0xAABBCC42;

void writeTestFrame(uint8_t *frame, uint8_t hopLimit, uint8_t hopStart, uint8_t nextHop = 0)
{
    memset(frame, 0xA5, 32);
    const PacketHeader header = {
        0xFFFFFFFF, 0x11223344, 0x12345678, static_cast<uint8_t>((hopStart << HOP_START_SHIFT) | hopLimit | 0x18),
        0x7A,       nextHop,    0x11,
    };
    writeHeader(frame, header);
}

void testOpaqueRelayPreservesPayloadAndFlags()
{
    uint8_t frame[32];
    writeTestFrame(frame, 3, 3);
    uint8_t payload[16];
    memcpy(payload, frame + HEADER_SIZE, sizeof(payload));

    assert(prepareForRelay(frame, sizeof(frame), OUR_NODE));
    const PacketHeader header = readHeader(frame);
    assert(hopLimit(header) == 2);
    assert(hopStart(header) == 3);
    assert((header.flags & 0x18) == 0x18);
    assert(header.channel == 0x7A);
    assert(header.nextHop == 0);
    assert(header.relayNode == 0x42);
    assert(memcmp(payload, frame + HEADER_SIZE, sizeof(payload)) == 0);
}

void testHopAndIdentityFiltering()
{
    uint8_t frame[32];
    writeTestFrame(frame, 0, 3);
    assert(inspect(frame, sizeof(frame), OUR_NODE) == Decision::NO_HOPS_LEFT);

    writeTestFrame(frame, 2, 3, 0x99);
    assert(inspect(frame, sizeof(frame), OUR_NODE) == Decision::OTHER_NEXT_HOP);

    writeTestFrame(frame, 2, 3, 0x42);
    assert(inspect(frame, sizeof(frame), OUR_NODE) == Decision::RELAY);

    PacketHeader header = readHeader(frame);
    header.from = OUR_NODE;
    writeHeader(frame, header);
    assert(inspect(frame, sizeof(frame), OUR_NODE) == Decision::FROM_US);
}

void testLegacyHeaderIgnoresNextHop()
{
    uint8_t frame[32];
    writeTestFrame(frame, 2, 0, 0x99);
    assert(inspect(frame, sizeof(frame), OUR_NODE) == Decision::RELAY);
    assert(prepareForRelay(frame, sizeof(frame), OUR_NODE));
    assert(readHeader(frame).nextHop == 0);
}

void testPlaintextDataRoundTrip()
{
    const uint8_t input[] = "public mesh traffic";
    uint8_t encoded[64];
    const size_t encodedLength = meshtastic_text::encode(input, sizeof(input) - 1, encoded, sizeof(encoded));
    assert(encodedLength == sizeof(input) + 3);
    assert(encoded[0] == 0x08);
    assert(encoded[1] == meshtastic_text::TEXT_MESSAGE_APP);
    assert(encoded[2] == 0x12);

    meshtastic_text::DecodedText decoded;
    assert(meshtastic_text::decode(encoded, encodedLength, &decoded));
    assert(decoded.length == sizeof(input) - 1);
    assert(memcmp(decoded.bytes, input, decoded.length) == 0);
}

void testPlaintextLongLengthAndUnknownField()
{
    uint8_t input[meshtastic_text::MAX_TEXT_SIZE];
    memset(input, 'x', sizeof(input));
    uint8_t encoded[240];
    size_t encodedLength = meshtastic_text::encode(input, sizeof(input), encoded, sizeof(encoded));
    assert(encodedLength == sizeof(input) + 5);
    encoded[encodedLength++] = 0x48;
    encoded[encodedLength++] = 0x00;

    meshtastic_text::DecodedText decoded;
    assert(meshtastic_text::decode(encoded, encodedLength, &decoded));
    assert(decoded.length == sizeof(input));
    assert(memcmp(decoded.bytes, input, decoded.length) == 0);
}

} // namespace

int main()
{
    testOpaqueRelayPreservesPayloadAndFlags();
    testHopAndIdentityFiltering();
    testLegacyHeaderIgnoresNextHop();
    testPlaintextDataRoundTrip();
    testPlaintextLongLengthAndUnknownField();
    return 0;
}
