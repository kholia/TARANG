#include <Arduino.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <cstring>

namespace
{
constexpr char PSK_PASSPHRASE[] = "change-this-demo-passphrase";
constexpr char PBKDF2_SALT[] = "tarang-gps-v1:change-this-deployment";
constexpr uint32_t PBKDF2_ITERATIONS = 100000;

constexpr uint8_t PIN_LORA_SCK = 4;
constexpr uint8_t PIN_LORA_MISO = 5;
constexpr uint8_t PIN_LORA_MOSI = 6;
constexpr uint8_t PIN_LORA_CS = 7;
constexpr uint8_t PIN_LORA_RESET = 8;
constexpr uint8_t PIN_LORA_DIO0 = 3;
constexpr uint8_t PIN_GPS_RX = 20;
constexpr uint8_t PIN_GPS_TX = 21;

constexpr uint32_t SEND_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t BROADCAST_NODE = UINT32_MAX;
constexpr uint32_t PRIVATE_APP = 256;
constexpr uint8_t HOP_LIMIT = 3;
constexpr uint8_t FORMAT_VERSION = 1;
constexpr size_t GPS_RECORD_SIZE = 20;
constexpr size_t CIPHERTEXT_SIZE = 32;
constexpr size_t INNER_PAYLOAD_SIZE = 1 + 16 + CIPHERTEXT_SIZE + 32;
constexpr char CHANNEL_NAME[] = "LongFast";

SX1278 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO0, PIN_LORA_RESET, RADIOLIB_NC);
TinyGPSPlus gps;
uint8_t encryptionKey[32];
uint8_t authenticationKey[32];
uint32_t nodeNumber;
uint32_t lastSendMs;
bool hasSent;

void putU16LE(uint8_t *output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

void putU32LE(uint8_t *output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

size_t appendVarint(uint8_t *output, uint32_t value)
{
    size_t length = 0;
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        output[length++] = byte | (value == 0 ? 0 : 0x80);
    } while (value != 0);
    return length;
}

uint8_t channelHash()
{
    uint8_t hash = 0;
    for (const char *character = CHANNEL_NAME; *character != '\0'; ++character) {
        hash ^= static_cast<uint8_t>(*character);
    }
    return hash;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097LL + static_cast<int64_t>(dayOfEra) - 719468;
}

uint32_t gpsUnixTime()
{
    if (!gps.date.isValid() || !gps.time.isValid()) {
        return 0;
    }

    const int64_t days = daysFromCivil(gps.date.year(), gps.date.month(), gps.date.day());
    const int64_t seconds = days * 86400LL + gps.time.hour() * 3600LL + gps.time.minute() * 60LL + gps.time.second();
    return seconds > 0 && seconds <= UINT32_MAX ? static_cast<uint32_t>(seconds) : 0;
}

bool deriveKeys()
{
    uint8_t derived[64];
    const int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char *>(PSK_PASSPHRASE), strlen(PSK_PASSPHRASE),
        reinterpret_cast<const unsigned char *>(PBKDF2_SALT), strlen(PBKDF2_SALT), PBKDF2_ITERATIONS, sizeof(derived),
        derived);
    if (result != 0) {
        return false;
    }

    memcpy(encryptionKey, derived, sizeof(encryptionKey));
    memcpy(authenticationKey, derived + sizeof(encryptionKey), sizeof(authenticationKey));
    mbedtls_platform_zeroize(derived, sizeof(derived));
    return true;
}

bool encryptGpsPayload(uint8_t output[INNER_PAYLOAD_SIZE])
{
    uint8_t plaintext[CIPHERTEXT_SIZE] = {};
    plaintext[0] = FORMAT_VERSION;
    putU32LE(plaintext + 1, gpsUnixTime());
    putU32LE(plaintext + 5, static_cast<uint32_t>(llround(gps.location.lat() * 1e7)));
    putU32LE(plaintext + 9, static_cast<uint32_t>(llround(gps.location.lng() * 1e7)));
    putU32LE(plaintext + 13, static_cast<uint32_t>(llround(gps.altitude.meters() * 100.0)));
    plaintext[17] = gps.satellites.isValid() ? min<uint32_t>(gps.satellites.value(), 255) : 0;
    putU16LE(plaintext + 18, gps.hdop.isValid() ? min<uint32_t>(gps.hdop.value(), UINT16_MAX) : UINT16_MAX);
    memset(plaintext + GPS_RECORD_SIZE, CIPHERTEXT_SIZE - GPS_RECORD_SIZE, CIPHERTEXT_SIZE - GPS_RECORD_SIZE);

    output[0] = FORMAT_VERSION;
    esp_fill_random(output + 1, 16);
    uint8_t workingIv[16];
    memcpy(workingIv, output + 1, sizeof(workingIv));

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int result = mbedtls_aes_setkey_enc(&aes, encryptionKey, 256);
    if (result == 0) {
        result = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, CIPHERTEXT_SIZE, workingIv, plaintext, output + 17);
    }
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(workingIv, sizeof(workingIv));
    if (result != 0) {
        return false;
    }

    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return sha256 != nullptr &&
           mbedtls_md_hmac(sha256, authenticationKey, sizeof(authenticationKey), output, 1 + 16 + CIPHERTEXT_SIZE,
                           output + 1 + 16 + CIPHERTEXT_SIZE) == 0;
}

size_t makeDataProtobuf(const uint8_t *innerPayload, size_t innerLength, uint8_t *output)
{
    size_t offset = 0;
    output[offset++] = 0x08;
    offset += appendVarint(output + offset, PRIVATE_APP);
    output[offset++] = 0x12;
    offset += appendVarint(output + offset, innerLength);
    memcpy(output + offset, innerPayload, innerLength);
    return offset + innerLength;
}

bool sendFix()
{
    uint8_t innerPayload[INNER_PAYLOAD_SIZE];
    if (!encryptGpsPayload(innerPayload)) {
        Serial.println("Encryption failed");
        return false;
    }

    uint8_t dataPayload[96];
    const size_t dataLength = makeDataProtobuf(innerPayload, sizeof(innerPayload), dataPayload);
    uint8_t packet[16 + sizeof(dataPayload)];
    const uint32_t packetId = esp_random();
    putU32LE(packet, BROADCAST_NODE);
    putU32LE(packet + 4, nodeNumber);
    putU32LE(packet + 8, packetId);
    packet[12] = HOP_LIMIT | (HOP_LIMIT << 5);
    packet[13] = channelHash();
    packet[14] = 0;
    packet[15] = 0;
    memcpy(packet + 16, dataPayload, dataLength);

    const int result = radio.transmit(packet, 16 + dataLength);
    mbedtls_platform_zeroize(innerPayload, sizeof(innerPayload));
    if (result != RADIOLIB_ERR_NONE) {
        Serial.printf("Radio transmit failed: %d\n", result);
        return false;
    }

    Serial.printf("Private fix sent: %.6f, %.6f (%u satellites)\n", gps.location.lat(), gps.location.lng(),
                  gps.satellites.value());
    return true;
}
} // namespace

void setup()
{
    Serial.begin(115200);
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    delay(1000);

    if (!deriveKeys()) {
        Serial.println("PBKDF2 failed");
        while (true) {
            delay(1000);
        }
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    nodeNumber = static_cast<uint32_t>(mac[2]) << 24 | static_cast<uint32_t>(mac[3]) << 16 |
                 static_cast<uint32_t>(mac[4]) << 8 | mac[5];
    if (nodeNumber == 0 || nodeNumber == BROADCAST_NODE) {
        nodeNumber ^= 0x80000000;
    }

    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
    int result = radio.begin(433.920, 125.0, 11, 5, 0x2b, 10, 16);
    if (result == RADIOLIB_ERR_NONE) {
        result = radio.setCRC(true);
    }
    if (result == RADIOLIB_ERR_NONE) {
        result = radio.invertIQ(false);
    }
    if (result != RADIOLIB_ERR_NONE) {
        Serial.printf("Ra-02 initialization failed: %d\n", result);
        while (true) {
            delay(1000);
        }
    }

    Serial.printf("GPS tracker ready as node !%08x\n", nodeNumber);
}

void loop()
{
    while (Serial1.available()) {
        gps.encode(Serial1.read());
    }

    const uint32_t now = millis();
    const bool hasRecentFix = gps.location.isValid() && gps.location.age() < 10000;
    if (hasRecentFix && (!hasSent || static_cast<uint32_t>(now - lastSendMs) >= SEND_INTERVAL_MS)) {
        if (sendFix()) {
            lastSendMs = now;
            hasSent = true;
        }
    }
    delay(10);
}
