# TARANG - The Public Mesh Network

TARANG is a temporary fork of the open-source Meshtastic firmware using the standard `LongFast` modem preset and
public PSK. Its well-known PSK provides interoperability, not privacy; do not use TARANG for private or sensitive
information.

TARANG targets India's licence-exempt 433.05-434.79 MHz short-range/ISM band, subject to a maximum +10 dBm ERP
(10 mW) and 10% duty cycle.

## Get connected

### In under five minutes

1. Buy a [Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/) **433 MHz**
   (ESP32-S3 + SX1262).
2. Attach its 433 MHz antenna before powering it.
3. Flash the `heltec-v3` firmware using the browser or build instructions below.
4. Install the official [Meshtastic Android app](https://play.google.com/store/apps/details?id=com.geeksville.mesh),
   enable Bluetooth, and select the node. The default pairing PIN is `123456`.
5. Scan this QR code in Meshtastic to apply the TARANG channel and radio settings:

![Meshtastic ChannelSet QR Code](tarang-channel-qr.png)

The QR uses Meshtastic's public `LongFast` PSK index `1`. It provides interoperability, not privacy.

### Add a private channel (Android)

Keep TARANG as the primary channel, then create a private secondary channel:

1. On the first node, open **Settings > Channels** and add the next available channel.
2. Set its role to **Secondary**, choose a shared name, and generate a random 256-bit PSK. Leave MQTT uplink and
   downlink disabled.
3. Save, then share the resulting QR code or link only with trusted participants.
4. Import it on every private node; do not generate a different random PSK on each node.

All nodes still use TARANG's frequency and PHY settings. See the
[Meshtastic channel documentation](https://meshtastic.org/docs/configuration/radio/channels/).

#### Private location tracker

On the `private tracker node` in the Android app:

1. Under **Settings > Channels**, set **Position precision** to **Disabled/0** on TARANG and every channel before
   the private secondary channel.
2. Set the private channel's position precision to **Precise/32 bits**; keep MQTT uplink and downlink disabled.
3. Under **Settings > Device**, select the **Tracker** role.
4. Under **Settings > Position**, enable GPS and choose a broadcast interval or Smart Position, then save.

Firmware sends automatic position updates on the first channel with non-zero precision, so these settings keep the
location off the public TARANG channel. Monitoring nodes only need the same private channel name and PSK.

### Lowest-cost DIY option

Buy an [Ai-Thinker Ra-02](https://docs.ai-thinker.com/en/Ra-02/) (SX1278), ESP32-C3 Super Mini, U.FL/I-PEX-to-SMA
female pigtail, and 433 MHz SMA antenna. Wire the Ra-02 over 3.3 V SPI with RESET and DIO0, then use an
Ra-02/SX1278 (`USE_RF95`) Super Mini target.

### Flash firmware from a browser

1. Open the [ESPBoards web flasher](https://www.espboards.dev/tools/program/) in Chrome, Edge, or Opera and connect
   the board.
2. Select `builds/<environment>/*.factory.bin` at address `0x0`, then click **Program**.
3. Reconnect after flashing. Do not use **Erase flash** unless you intend to delete all saved device state.

## TARANG radio settings

All nodes must use these settings:

| Setting | Value |
| --- | --- |
| Region | **IN_433** |
| Frequency | **433.920 MHz** (fixed override) |
| Modem preset | **LongFast** |
| Bandwidth | **250 kHz** |
| Spreading factor | **SF11** |
| Coding rate | **4/5** |
| Sync word | **0x2B** |
| Preamble | **16 symbols** |
| Header mode | **Explicit** |
| IQ | **Normal (not inverted)** |
| CRC | **Enabled** |
| Low-data-rate optimization | **Disabled** |
| Maximum radiated power | **+10 dBm ERP (10 mW)** |

The 433.920 MHz override disables automatic channel selection. Confirm the boot log reports 433.920 MHz, 250 kHz,
SF11, and CR 4/5.

## Operating in India

Under the [2022 WPC exemption rules](433-Band-Rules.pdf), compliant low-power devices may operate from 433.05 to
434.79 MHz without an individual spectrum licence. Operation is limited to 10 mW ERP and a maximum 10% duty cycle
on a shared, non-interference and non-protection basis. The rules also require equipment type approval and
compliance with EN 300 220.

- Hardware, including its RF frontend and antenna, must support 433 MHz.
- The +10 dBm ERP limit includes radio output, external PA, feed-line loss, and antenna gain.
- Operators are responsible for ensuring their complete installation complies with the rules.

## Why 433 MHz?

India's dense cities put concrete walls, closely packed buildings, trees, vehicles, and narrow streets between
radios. Compared with higher LoRa frequencies, 433 MHz generally has lower path loss and better diffraction around
obstacles, giving the signal more practical "punching power" without increasing transmit power. Actual range still
depends on antenna quality and placement, local interference, terrain, and compliance with the 10 mW ERP limit.

## Build firmware (optional)

Install [`uv` (includes `uvx`)](https://docs.astral.sh/uv/getting-started/installation/), then run from the repository
root:

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh
```

```sh
# ESP32-C3 Super Mini
uvx --python 3.12 --from platformio pio run -e esp32c3_super_mini

# ESP32-C3-Zero
uvx --python 3.12 --from platformio pio run -e esp32c3_zero
uvx --python 3.12 --from platformio pio run -e esp32c3_zero --target upload

# ESP32-S3 example (Heltec V3)
uvx --python 3.12 --from platformio pio run -e heltec-v3
```

For another ESP32-S3 board, replace `heltec-v3` with the environment name from its
`variants/esp32s3/*/platformio.ini`. Add `-t upload --upload-port <port>` to a build command to flash the connected
board. TARANG settings from `userPrefs.jsonc` are applied automatically.

## Recommended antennas

- Ebyte TX433-BLG-40 433 MHz 4.5 dBi
- Diamond SG7900 / X-30 / X-50
- Ebyte TX433-JWG-7 433 MHz 2.5 dBi

## Upstream workflow

This repository stays current by rebasing TARANG changes onto the latest
[`meshtastic/firmware`](https://github.com/meshtastic/firmware) `develop` branch. Use fetch-and-rebase rather than
merge commits when synchronizing with upstream.

## Resources

- [TARANG channel URL](tarang-channel-url.txt)
- [Machine-readable PHY settings](tarang-phy.json)
- [433 MHz band rules - India](433-Band-Rules.pdf)
- [Meshtastic firmware](https://github.com/meshtastic/firmware)
- [REYAX LoRa at 433 MHz in India](https://gsasindia.com/blog/reyax-lora-getting-started-433mhz-india)
