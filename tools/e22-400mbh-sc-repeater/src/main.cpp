#include "repeater_config.h"
#include <Arduino.h>

#include <RadioLib.h>
#include <SPI.h>
#include <stdarg.h>
#include <stdio.h>
#include <stm32f1xx_hal.h>
#if REPEATER_USB_CDC_LOGGING
#include <USBSerial.h>
#endif

#include "meshtastic_relay.h"
#include "meshtastic_text.h"

namespace
{

constexpr uint32_t SEEN_RETENTION_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t PENDING_CAPACITY = 4;
constexpr uint8_t SEEN_CAPACITY = 32;
constexpr uint32_t SLOT_TIME_MS = 29;
constexpr uint8_t MAX_CAD_DEFERRALS = 8;
constexpr uint32_t BROADCAST_NODE = 0xFFFFFFFFUL;

constexpr uint32_t RADIO_DIO1 = PA3;
constexpr uint32_t RADIO_CS = PA4;
constexpr uint32_t RADIO_RESET = PB0;
constexpr uint32_t RADIO_BUSY = PB1;
constexpr uint32_t RADIO_TX_ENABLE = PB12;
constexpr uint32_t RADIO_RX_ENABLE = PB13;
constexpr uint32_t TX_LED = PA15;
constexpr uint32_t RX_LED = PB6;
constexpr uint32_t LOG_UART_TX = PA9;
constexpr uint32_t LOG_UART_RX = PA10;

SX1268 radio = new Module(RADIO_CS, RADIO_DIO1, RADIO_RESET, RADIO_BUSY);
#if REPEATER_USB_CDC_LOGGING
Stream &logPort = SerialUSB;
#elif REPEATER_UART_LOGGING
HardwareSerial logUart(LOG_UART_RX, LOG_UART_TX);
Stream &logPort = logUart;
#endif

struct PendingFrame {
    bool used = false;
    bool originated = false;
    uint8_t length = 0;
    uint8_t hopsReceived = 0;
    uint8_t cadDeferrals = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint32_t dueAt = 0;
    uint8_t bytes[meshtastic_relay::MAX_FRAME_SIZE]{};
};

struct SeenPacket {
    bool used = false;
    uint8_t bestHops = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint32_t lastSeenAt = 0;
};

PendingFrame pending[PENDING_CAPACITY];
SeenPacket seen[SEEN_CAPACITY];
volatile bool receivedInterrupt = false;
uint32_t ourNode = 0;
#if REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING
char consoleLine[meshtastic_text::MAX_TEXT_SIZE + 6]{};
size_t consoleLineLength = 0;
#endif
#if REPEATER_USB_CDC_LOGGING
bool usbConsoleConnected = false;
#endif

void onReceive()
{
    receivedInterrupt = true;
}

void logMessage(const char *format, ...)
{
#if REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING
    static char message[192];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    logPort.print('[');
    logPort.print(millis());
    logPort.print(F("] "));
    logPort.println(message);
#else
    (void)format;
#endif
}

void dumpFrame(const char *direction, const uint8_t *frame, size_t length)
{
#if (REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING) && REPEATER_CONSOLE_DUMP_FRAMES
    logPort.print('[');
    logPort.print(millis());
    logPort.print(F("] "));
    logPort.print(direction);
    logPort.print(F(" len="));
    logPort.print(length);
    logPort.print(F(" hex="));
    for (size_t i = 0; i < length; ++i) {
        if (frame[i] < 0x10)
            logPort.print('0');
        logPort.print(frame[i], HEX);
    }
    logPort.println();
#else
    (void)direction;
    (void)frame;
    (void)length;
#endif
}

void logRawLora(const uint8_t *frame, size_t length, float snr, int16_t rssi)
{
#if ENABLE_DEBUG && (REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING)
    logPort.print('[');
    logPort.print(millis());
    logPort.print(F("] RX_LORA len="));
    logPort.print(length);
    logPort.print(F(" snr_qdb="));
    logPort.print(static_cast<int>(snr * 4.0F));
    logPort.print(F(" rssi="));
    logPort.print(rssi);
    logPort.print(F(" ascii="));
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = frame[i];
        logPort.write(byte >= 0x20 && byte <= 0x7E ? byte : '.');
    }
    logPort.println();
#else
    (void)frame;
    (void)length;
    (void)snr;
    (void)rssi;
#endif
}

const char *decisionName(meshtastic_relay::Decision decision)
{
    using meshtastic_relay::Decision;
    switch (decision) {
    case Decision::RELAY:
        return "relay";
    case Decision::TOO_SHORT:
        return "too_short";
    case Decision::TOO_LONG:
        return "too_long";
    case Decision::INVALID_SENDER:
        return "invalid_sender";
    case Decision::INVALID_PACKET_ID:
        return "invalid_id";
    case Decision::FROM_US:
        return "from_us";
    case Decision::TO_US:
        return "to_us";
    case Decision::NO_HOPS_LEFT:
        return "no_hops";
    case Decision::OTHER_NEXT_HOP:
        return "other_next_hop";
    }
    return "unknown";
}

bool timeReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t makeNodeNumber()
{
    uint32_t value = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^ 0x9E3779B9UL;
    value ^= value >> 16;
    value *= 0x7FEB352DUL;
    value ^= value >> 15;
    if (value == 0)
        value = 1;
    if (static_cast<uint8_t>(value) == 0)
        value |= 1;
    return value;
}

uint8_t channelHash()
{
    uint8_t hash = 0;
    for (const char *p = REPEATER_CHANNEL_NAME; *p != '\0'; ++p)
        hash ^= static_cast<uint8_t>(*p);
    return hash;
}

[[noreturn]] void fatalBlink(const char *reason, int16_t state = RADIOLIB_ERR_NONE)
{
    logMessage("FATAL reason=%s state=%d", reason, state);
#if REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING
    logPort.flush();
#endif
    while (true) {
        digitalWrite(TX_LED, LOW);
        digitalWrite(RX_LED, HIGH);
        delay(150);
        digitalWrite(TX_LED, HIGH);
        digitalWrite(RX_LED, LOW);
        delay(150);
    }
}

uint8_t contentionWindow(float snr)
{
    if (snr < -20.0F)
        snr = -20.0F;
    if (snr > 10.0F)
        snr = 10.0F;
    return 3 + static_cast<uint8_t>(((snr + 20.0F) * 5.0F) / 30.0F);
}

uint32_t relayDelay(float snr)
{
    const uint8_t window = contentionWindow(snr);
    return static_cast<uint32_t>(random(0, 2 * window)) * SLOT_TIME_MS;
}

PendingFrame *findPending(uint32_t from, uint32_t id)
{
    for (auto &entry : pending) {
        if (entry.used && entry.from == from && entry.id == id)
            return &entry;
    }
    return nullptr;
}

PendingFrame *freePending()
{
    for (auto &entry : pending) {
        if (!entry.used)
            return &entry;
    }
    return nullptr;
}

SeenPacket *findSeen(uint32_t from, uint32_t id)
{
    for (auto &entry : seen) {
        if (entry.used && entry.from == from && entry.id == id)
            return &entry;
    }
    return nullptr;
}

SeenPacket *allocateSeen(uint32_t now)
{
    SeenPacket *oldest = &seen[0];
    for (auto &entry : seen) {
        if (!entry.used || now - entry.lastSeenAt > SEEN_RETENTION_MS)
            return &entry;
        if (now - entry.lastSeenAt > now - oldest->lastSeenAt)
            oldest = &entry;
    }
    return oldest;
}

bool shouldSchedule(const meshtastic_relay::PacketHeader &header, uint32_t now)
{
    SeenPacket *entry = findSeen(header.from, header.id);
    const uint8_t hops = meshtastic_relay::hopLimit(header);
    const bool originalRetry = meshtastic_relay::hopStart(header) > 0 && meshtastic_relay::hopStart(header) == hops;

    if (entry == nullptr) {
        entry = allocateSeen(now);
        *entry = {true, hops, header.from, header.id, now};
        return true;
    }

    entry->lastSeenAt = now;
    if (hops > entry->bestHops) {
        entry->bestHops = hops;
        return true;
    }
    return originalRetry && findPending(header.from, header.id) == nullptr;
}

void scheduleFrame(const uint8_t *data, uint8_t length, float snr)
{
    const auto header = meshtastic_relay::readHeader(data);
    const uint32_t now = millis();
    PendingFrame *entry = findPending(header.from, header.id);

    if (entry != nullptr && meshtastic_relay::hopLimit(header) <= entry->hopsReceived) {
        logMessage("DROP duplicate from=%08lX id=%08lX hops=%u", static_cast<unsigned long>(header.from),
                   static_cast<unsigned long>(header.id), meshtastic_relay::hopLimit(header));
        return;
    }
    if (entry != nullptr) {
        logMessage("QUEUE upgrade from=%08lX id=%08lX hops=%u->%u", static_cast<unsigned long>(header.from),
                   static_cast<unsigned long>(header.id), entry->hopsReceived, meshtastic_relay::hopLimit(header));
        SeenPacket *seenEntry = findSeen(header.from, header.id);
        if (seenEntry != nullptr) {
            seenEntry->bestHops = meshtastic_relay::hopLimit(header);
            seenEntry->lastSeenAt = now;
        }
    }
    if (entry == nullptr && !shouldSchedule(header, now)) {
        logMessage("DROP seen from=%08lX id=%08lX hops=%u", static_cast<unsigned long>(header.from),
                   static_cast<unsigned long>(header.id), meshtastic_relay::hopLimit(header));
        return;
    }
    if (entry == nullptr)
        entry = freePending();
    if (entry == nullptr) {
        logMessage("DROP queue_full from=%08lX id=%08lX", static_cast<unsigned long>(header.from),
                   static_cast<unsigned long>(header.id));
        return;
    }

    entry->used = true;
    entry->originated = false;
    entry->length = length;
    entry->hopsReceived = meshtastic_relay::hopLimit(header);
    entry->cadDeferrals = 0;
    entry->from = header.from;
    entry->id = header.id;
    entry->dueAt = now + relayDelay(snr);
    memcpy(entry->bytes, data, length);
    meshtastic_relay::prepareForRelay(entry->bytes, length, ourNode);
    logMessage("QUEUE relay from=%08lX id=%08lX in_ms=%lu out_hops=%u", static_cast<unsigned long>(header.from),
               static_cast<unsigned long>(header.id), static_cast<unsigned long>(entry->dueAt - now),
               meshtastic_relay::hopLimit(meshtastic_relay::readHeader(entry->bytes)));
}

void rememberOriginated(uint32_t packetId, uint8_t hops, uint32_t now)
{
    SeenPacket *entry = allocateSeen(now);
    *entry = {true, hops, ourNode, packetId, now};
}

void originateText(const uint8_t *text, size_t textLength)
{
    PendingFrame *entry = freePending();
    if (entry == nullptr) {
        logMessage("SEND rejected reason=queue_full");
        return;
    }

    uint32_t packetId = static_cast<uint32_t>(random(1, 0x7FFFFFFF));
    if (packetId == 0)
        packetId = 1;
    const meshtastic_relay::PacketHeader header = {
        BROADCAST_NODE,
        ourNode,
        packetId,
        static_cast<uint8_t>((REPEATER_ORIGIN_HOP_LIMIT << meshtastic_relay::HOP_START_SHIFT) | REPEATER_ORIGIN_HOP_LIMIT),
        channelHash(),
        meshtastic_relay::NO_NEXT_HOP_PREFERENCE,
        static_cast<uint8_t>(ourNode),
    };
    meshtastic_relay::writeHeader(entry->bytes, header);
    const size_t encodedLength = meshtastic_text::encode(text, textLength, entry->bytes + meshtastic_relay::HEADER_SIZE,
                                                         meshtastic_relay::MAX_FRAME_SIZE - meshtastic_relay::HEADER_SIZE);
    if (encodedLength == 0) {
        logMessage("SEND rejected reason=invalid_text length=%u", static_cast<unsigned>(textLength));
        return;
    }

    const uint32_t now = millis();
    entry->used = true;
    entry->originated = true;
    entry->length = meshtastic_relay::HEADER_SIZE + encodedLength;
    entry->hopsReceived = REPEATER_ORIGIN_HOP_LIMIT;
    entry->cadDeferrals = 0;
    entry->from = ourNode;
    entry->id = packetId;
    entry->dueAt = now + static_cast<uint32_t>(random(0, 8)) * SLOT_TIME_MS;
    rememberOriginated(packetId, REPEATER_ORIGIN_HOP_LIMIT, now);
    logMessage("QUEUE origin id=%08lX length=%u in_ms=%lu", static_cast<unsigned long>(packetId),
               static_cast<unsigned>(textLength), static_cast<unsigned long>(entry->dueAt - now));
}

void logDecodedText(const meshtastic_relay::PacketHeader &header, const uint8_t *payload, size_t payloadLength)
{
    if (header.channel != channelHash())
        return;
    meshtastic_text::DecodedText text;
    if (!meshtastic_text::decode(payload, payloadLength, &text))
        return;

#if REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING
    logPort.print('[');
    logPort.print(millis());
    logPort.print(F("] TEXT from="));
    logPort.print(header.from, HEX);
    logPort.print(F(" value="));
    for (size_t i = 0; i < text.length; ++i) {
        const uint8_t byte = text.bytes[i];
        logPort.write(byte == '\r' || byte == '\n' || byte < 0x20 ? '.' : byte);
    }
    logPort.println();
#else
    (void)header;
#endif
}

void handleConsoleCommand(const char *line, size_t length)
{
    if (length == 4 && memcmp(line, "help", 4) == 0) {
        logMessage("COMMANDS send <text> | status | help");
    } else if (length == 6 && memcmp(line, "status", 6) == 0) {
        uint8_t queued = 0;
        for (const auto &entry : pending)
            queued += entry.used;
        logMessage("STATUS node=%08lX channel_hash=%02X queued=%u", static_cast<unsigned long>(ourNode), channelHash(), queued);
    } else if (length > 5 && memcmp(line, "send ", 5) == 0) {
        originateText(reinterpret_cast<const uint8_t *>(line + 5), length - 5);
    } else {
        logMessage("ERROR unknown_command; use help");
    }
}

void pollConsole()
{
#if REPEATER_USB_CDC_LOGGING || REPEATER_UART_LOGGING
#if REPEATER_USB_CDC_LOGGING
    const bool connected = SerialUSB.dtr();
    if (connected && !usbConsoleConnected)
        logMessage("CONSOLE connected node=%08lX channel_hash=%02X", static_cast<unsigned long>(ourNode), channelHash());
    usbConsoleConnected = connected;
#endif
    while (logPort.available() > 0) {
        const int value = logPort.read();
        if (value < 0)
            return;
        const char c = static_cast<char>(value);
        if (c == '\r')
            continue;
        if (c == '\n') {
            if (consoleLineLength > 0)
                handleConsoleCommand(consoleLine, consoleLineLength);
            consoleLineLength = 0;
        } else if ((c == '\b' || c == 0x7F) && consoleLineLength > 0) {
            --consoleLineLength;
        } else if (consoleLineLength < sizeof(consoleLine) - 1) {
            consoleLine[consoleLineLength++] = c;
        } else {
            consoleLineLength = 0;
            logMessage("ERROR command_too_long");
        }
    }
#endif
}

void resumeReceive()
{
    receivedInterrupt = false;
    radio.setDio1Action(onReceive);
    const int16_t state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE)
        fatalBlink("start_receive", state);
}

void handleReceive()
{
    receivedInterrupt = false;
    const size_t length = radio.getPacketLength();
    if (length == 0 || length > meshtastic_relay::MAX_FRAME_SIZE) {
        logMessage("DROP radio_length len=%u", static_cast<unsigned>(length));
        resumeReceive();
        return;
    }
#if !ENABLE_DEBUG
    if (length < meshtastic_relay::HEADER_SIZE) {
        logMessage("DROP radio_length len=%u", static_cast<unsigned>(length));
        resumeReceive();
        return;
    }
#endif

    uint8_t frame[meshtastic_relay::MAX_FRAME_SIZE];
    const int16_t state = radio.readData(frame, length);
    const float snr = radio.getSNR();
    const int16_t rssi = static_cast<int16_t>(radio.getRSSI());
    resumeReceive();

    if (state != RADIOLIB_ERR_NONE) {
        logMessage("DROP radio_read state=%d len=%u", state, static_cast<unsigned>(length));
        return;
    }

#if ENABLE_DEBUG
    digitalWrite(RX_LED, LOW);
    logRawLora(frame, length, snr, rssi);
    dumpFrame("RX_RAW", frame, length);
    digitalWrite(RX_LED, HIGH);
    return;
#else
    const auto header = meshtastic_relay::readHeader(frame);
    const auto decision = meshtastic_relay::inspect(frame, length, ourNode);
    digitalWrite(RX_LED, LOW);
    logMessage("RX from=%08lX to=%08lX id=%08lX len=%u hops=%u/%u ch=%02X next=%02X relay=%02X snr_qdb=%d rssi=%d action=%s",
               static_cast<unsigned long>(header.from), static_cast<unsigned long>(header.to),
               static_cast<unsigned long>(header.id), static_cast<unsigned>(length), meshtastic_relay::hopLimit(header),
               meshtastic_relay::hopStart(header), header.channel, header.nextHop, header.relayNode, static_cast<int>(snr * 4.0F),
               rssi, decisionName(decision));
    dumpFrame("RX_RAW", frame, length);
    logDecodedText(header, frame + meshtastic_relay::HEADER_SIZE, length - meshtastic_relay::HEADER_SIZE);
    if (decision == meshtastic_relay::Decision::RELAY)
        scheduleFrame(frame, static_cast<uint8_t>(length), snr);
    digitalWrite(RX_LED, HIGH);
#endif
}

void transmitDueFrame()
{
    const uint32_t now = millis();
    PendingFrame *entry = nullptr;
    for (auto &candidate : pending) {
        if (candidate.used && timeReached(now, candidate.dueAt)) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr)
        return;

#ifdef REPEATER_RX_ONLY
    logMessage("DROP tx_disabled kind=%s from=%08lX id=%08lX", entry->originated ? "origin" : "relay",
               static_cast<unsigned long>(entry->from), static_cast<unsigned long>(entry->id));
    entry->used = false;
#else
    radio.clearDio1Action();
    const int16_t cad = radio.scanChannel();
    if (cad != RADIOLIB_CHANNEL_FREE) {
        if (entry->cadDeferrals < MAX_CAD_DEFERRALS) {
            ++entry->cadDeferrals;
            entry->dueAt = millis() + static_cast<uint32_t>(random(1, 9)) * SLOT_TIME_MS;
            logMessage("CAD defer from=%08lX id=%08lX state=%d attempt=%u", static_cast<unsigned long>(entry->from),
                       static_cast<unsigned long>(entry->id), cad, entry->cadDeferrals);
        } else {
            logMessage("DROP cad_busy from=%08lX id=%08lX state=%d", static_cast<unsigned long>(entry->from),
                       static_cast<unsigned long>(entry->id), cad);
            entry->used = false;
        }
        resumeReceive();
        return;
    }

    digitalWrite(TX_LED, LOW);
    const int16_t state = radio.transmit(entry->bytes, entry->length);
    digitalWrite(TX_LED, HIGH);
    const auto header = meshtastic_relay::readHeader(entry->bytes);
    logMessage("TX kind=%s from=%08lX to=%08lX id=%08lX len=%u hops=%u state=%d", entry->originated ? "origin" : "relay",
               static_cast<unsigned long>(header.from), static_cast<unsigned long>(header.to),
               static_cast<unsigned long>(header.id), entry->length, meshtastic_relay::hopLimit(header), state);
    dumpFrame("TX_RAW", entry->bytes, entry->length);
    entry->used = false;
    resumeReceive();
#endif
}

} // namespace

void setup()
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
    pinMode(TX_LED, OUTPUT);
    pinMode(RX_LED, OUTPUT);
    digitalWrite(TX_LED, HIGH);
    digitalWrite(RX_LED, HIGH);
#if REPEATER_USB_CDC_LOGGING
    SerialUSB.begin();
#elif REPEATER_UART_LOGGING
    logUart.begin(REPEATER_UART_BAUD);
#endif
#if REPEATER_USB_CDC_LOGGING
    logMessage("BOOT minimal-meshtastic-repeater console=usb_cdc");
#elif REPEATER_UART_LOGGING
    logMessage("BOOT minimal-meshtastic-repeater console=uart tx=PA9 baud=%lu", static_cast<unsigned long>(REPEATER_UART_BAUD));
#endif

    if (REPEATER_FREQUENCY_MHZ < 410.0F || REPEATER_FREQUENCY_MHZ > 493.0F)
        fatalBlink("invalid_frequency");
    if (REPEATER_RADIO_DRIVE_DBM < -9 || REPEATER_RADIO_DRIVE_DBM > 22)
        fatalBlink("invalid_drive_power");
    if (REPEATER_ORIGIN_HOP_LIMIT < 1 || REPEATER_ORIGIN_HOP_LIMIT > meshtastic_relay::HOP_LIMIT_MASK)
        fatalBlink("invalid_origin_hop_limit");

    ourNode = makeNodeNumber();
    randomSeed(ourNode ^ micros());
    logMessage("CONFIG node=%08lX freq_hz=%lu bw_hz=%lu sf=%u cr=4/%u sync=%02X preamble=%u debug=%u drive_dbm=%d "
               "channel=%s hash=%02X",
               static_cast<unsigned long>(ourNode), static_cast<unsigned long>(REPEATER_FREQUENCY_MHZ * 1000000.0F),
               static_cast<unsigned long>(REPEATER_BANDWIDTH_KHZ * 1000.0F), REPEATER_SPREADING_FACTOR, REPEATER_CODING_RATE,
               REPEATER_RADIO_SYNC_WORD, REPEATER_RADIO_PREAMBLE_LENGTH, ENABLE_DEBUG, REPEATER_RADIO_DRIVE_DBM,
               REPEATER_CHANNEL_NAME, channelHash());
    SPI.setMISO(PA_6);
    SPI.setMOSI(PA_7);
    SPI.setSCLK(PA_5);
    SPI.begin();

    radio.setRfSwitchPins(RADIO_RX_ENABLE, RADIO_TX_ENABLE);
    const int16_t state = radio.begin(REPEATER_FREQUENCY_MHZ, REPEATER_BANDWIDTH_KHZ, REPEATER_SPREADING_FACTOR,
                                      REPEATER_CODING_RATE, REPEATER_RADIO_SYNC_WORD, REPEATER_RADIO_DRIVE_DBM,
                                      REPEATER_RADIO_PREAMBLE_LENGTH, REPEATER_TCXO_VOLTAGE, false);
    if (state != RADIOLIB_ERR_NONE)
        fatalBlink("radio_begin", state);
    int16_t configState = RADIOLIB_ERR_NONE;
    configState = radio.forceLDRO(true);
    if (configState != RADIOLIB_ERR_NONE)
        fatalBlink("radio_ldro", configState);
    configState = radio.setCurrentLimit(140.0F);
    if (configState != RADIOLIB_ERR_NONE)
        fatalBlink("radio_current_limit", configState);
    configState = radio.setDio2AsRfSwitch(false);
    if (configState != RADIOLIB_ERR_NONE)
        fatalBlink("radio_dio2_switch", configState);
    configState = radio.setCRC(RADIOLIB_SX126X_LORA_CRC_ON);
    if (configState != RADIOLIB_ERR_NONE)
        fatalBlink("radio_crc", configState);

    resumeReceive();
    logMessage("READY receiving");
}

void loop()
{
    if (receivedInterrupt) {
        handleReceive();
        return;
    }
    pollConsole();
    transmitDueFrame();
}
