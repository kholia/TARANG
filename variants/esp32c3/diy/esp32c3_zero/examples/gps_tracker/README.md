# Private GPS over the open TARANG mesh

This standalone example reads a UART GPS and sends a private application payload inside an otherwise open,
relayable TARANG/Meshtastic packet. Nearby TARANG nodes can forward the packet without knowing the location or
the application key, much like shared public infrastructure carrying private user traffic.

The inner payload uses AES-256-CBC with PKCS#7 padding and Encrypt-then-MAC using HMAC-SHA256. PBKDF2-HMAC-SHA256
derives independent 32-byte encryption and authentication keys from an arbitrary-length passphrase. A fresh
hardware-random IV is generated for every transmission.

Plain AES-CBC does not detect tampering; the HMAC is required. This is a custom `PRIVATE_APP` payload, so stock
Meshtastic clients will relay it but will not display it as a normal position. A receiver must implement the same
payload format and verify the HMAC before decrypting.

Location data is sensitive. Obtain consent before tracking a person or their property, and share the passphrase
only with intended recipients.

## Configure the application key

Edit these constants near the top of `src/main.cpp`:

- `PSK_PASSPHRASE` accepts a short or long passphrase.
- `PBKDF2_SALT` must be identical at every sender and receiver and should uniquely identify the deployment.
- `PBKDF2_ITERATIONS` controls the password-guessing cost.

A short human password remains guessable even with PBKDF2. Use a long, randomly generated passphrase in a real
deployment. The example never prints the passphrase or derived keys.

No NVS is used. The node number is derived from the factory eFuse MAC, while packet IDs and IVs come from the
ESP32-C3 hardware random generator.

## GPS connection

| GPS pin | ESP32-C3-Zero pin |
| ------- | ----------------- |
| TX      | GPIO20 (GPS RX)   |
| RX      | GPIO21 (GPS TX)   |
| GND     | GND               |
| VCC     | Appropriate supply for the GPS module |

The example expects 9600-baud NMEA data. Ensure the GPS module uses 3.3 V logic levels.

## Radio and transmission

The Ra-02 uses the wiring in the parent board README. Radio settings are 433.920 MHz, 125 kHz bandwidth, SF11,
CR 4/5, sync word `0x2B`, and 10 dBm. A valid fix is sent immediately and then at most once every five minutes.
The outer channel is TARANG's open `LongFast` channel; only the inner GPS payload is private.

## Build and upload

From the firmware repository root:

```sh
uvx --python 3.12 --from platformio \
  pio run -d variants/esp32c3/diy/esp32c3_zero/examples/gps_tracker
```

To upload, append the upload target and port:

```sh
uvx --python 3.12 --from platformio \
  pio run -d variants/esp32c3/diy/esp32c3_zero/examples/gps_tracker \
  -t upload --upload-port /dev/ttyACM0
```

## Inner payload format

The protobuf `Data.payload` contains:

| Bytes | Meaning |
| ----- | ------- |
| 1 | Format version (`1`) |
| 16 | Random CBC IV |
| 32 | AES-CBC ciphertext of the padded 20-byte GPS record |
| 32 | HMAC-SHA256 over version, IV, and ciphertext |

The plaintext GPS record is little-endian: version, UTC Unix time, latitude and longitude in `1e-7` degrees,
altitude in centimetres, satellite count, and HDOP multiplied by 100.
