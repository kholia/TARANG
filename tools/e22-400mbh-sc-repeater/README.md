# Minimal plaintext Meshtastic endpoint/repeater for E22-400MBH-SC

This is a fixed-configuration, plaintext LoRa endpoint and repeater. It receives a Meshtastic radio frame, checks the
clear 16-byte routing header, decrements the hop limit, updates the relay byte, and retransmits the payload unchanged. It
can also send and display zero-PSK Meshtastic text broadcasts through its console. It therefore needs no AES, Curve25519, node
database, phone API, screen, filesystem, or settings service; the small text-message protobuf envelope is encoded and
decoded directly.

## Deliberate limitations

- It does not appear in the Meshtastic node list or originate NodeInfo, telemetry, acknowledgements, or admin packets.
- It sends and displays plaintext `TEXT_MESSAGE_APP` broadcasts. It still repeats other valid frames opaquely.
- It has no Bluetooth, Meshtastic USB protocol, screen, or over-the-air update path. A USB CDC command/log console is
  enabled by default.
- It implements router-like flooding, duplicate suppression, hop-limit upgrades, reliable-origin retries, randomized
  SNR-weighted relay delay, and CAD. It has no learned next-hop table, so a packet explicitly addressed to this relay byte
  returns to flooding after this hop.
- The compile-time TARANG modem settings are 433.920 MHz, 125 kHz, SF11, CR 4/5, sync word `0x2b`, a 16-symbol
  preamble, explicit header, normal IQ, CRC enabled, and low-data-rate optimization enabled.

## Configure before flashing

Edit `include/repeater_config.h` and set `REPEATER_FREQUENCY_MHZ` to the exact frequency used by the existing mesh.
The TARANG default is `433.920` MHz. Meshtastic derives the actual frequency from region, modem preset, and channel
slot/name, so confirm that each node reports this exact frequency during radio initialization.

Also choose `REPEATER_RADIO_DRIVE_DBM` conservatively and lawfully. This value is the SX1268 drive level before the
E22-400M30S external PA; the vendor firmware maps 10 dBm drive to roughly 24 dBm module output and 20 dBm drive to
roughly 30 dBm module output. Antenna gain, duty cycle, frequency, and permitted EIRP depend on local regulation.

### Post-experiment deployment note

Do not carry the experimental PA settings into a wider deployment. After testing, use the lowest SX1268
drive power and spreading factor that still provide the required link reliability. Choose bandwidth from measured airtime
and channel occupancy rather than simply selecting the lowest number: reducing LoRa bandwidth lengthens each symbol and
can increase airtime. Account for the external PA, feed-line loss, and antenna gain when calculating radiated power, and
add or verify an airtime/duty-cycle limiter before unattended deployment.

The 433.05-434.79 MHz ISM/SRD allocation is shared and conditionally license-exempt in some jurisdictions, not a
universally unrestricted public band. Check the applicable frequency, occupied-bandwidth, ERP/EIRP, duty-cycle, equipment
approval, and non-interference requirements. For example, India's 2022 exemption includes a 10 mW ERP, no-more-than-10%
duty-cycle case; other operating cases have different limits. The present external-PA settings must not be assumed to
comply. See the [WPC 433.05-434.79 MHz Exemption Rules, 2022](https://www.eservices.dot.gov.in/sites/default/files/circular-notifications/the-use-of-low-power-radio-frequency-devices-in-the-frequency-band-433.05-to-434.79-mhz-exemption-from-license-rules-2022.pdf).

## Build and flash

From this directory:

```sh
make
make upload
```

`make` builds a minimal local image containing Python and the pinned PlatformIO Core, then runs the firmware build inside
it. PlatformIO packages, dependencies, and output persist in the ignored `.pio-docker` directory. The first build needs
network access; subsequent builds reuse the cache. Use `make build-local` to bypass Docker with a host PlatformIO
installation. The toolchain is pinned to PlatformIO Core 6.1.19, ST STM32 platform 19.7.0, and STM32duino 2.12.0.

`make upload` builds in Docker and flashes that exact binary on the host with `st-flash`. This keeps USB/ST-Link access
outside Docker, which is especially useful on macOS. Install the ST-Link tools first (`brew install stlink` on macOS or
your distribution's `stlink-tools` package on Linux). `make upload-local` remains available when host PlatformIO is
preferred.

The PlatformIO environment uses ST-Link because that is the dependable programming path for a blank STM32F103C8T6. TX
LED is PA15, RX LED is PB6, and both are active-low. Alternating LEDs indicate an invalid configuration or radio
initialization failure.

Other useful targets are `make clean`, `make test`, `make format`, and `make check`. Open the Type-C console with the
device path reported by the host, for example `make monitor PORT=/dev/cu.usbmodem1234`. Override tools or the image tag
when needed, such as `make PIO=~/.platformio/penv/bin/pio` or `make PIO_IMAGE=my-registry/e22-pio:6.1.19`.

## Type-C USB console

The existing Type-C connector appears as a USB CDC serial port. No USB-UART adapter is needed. On macOS it normally
appears as `/dev/cu.usbmodem*`; Linux commonly uses `/dev/ttyACM*`. Terminal programs may request 115200 baud, but USB
CDC does not use a physical baud rate.

The relay never waits for a terminal, so radio operation starts even when no host is attached. Opening the serial port
prints a `CONSOLE connected` line; use `status` to retrieve current state. Very early boot messages can be missed.

The board routes USB D-/D+ to PA11/PA12 and controls its USB pull-up with PB5. The firmware uses the board's 8 MHz crystal
to generate the required 48 MHz USB clock and toggles PB5 during startup so the host reliably re-enumerates it.

Commands are newline-terminated:

```text
send hello, public mesh
status
help
```

`send` constructs a clear Meshtastic `Data` envelope with `portnum=TEXT_MESSAGE_APP`, broadcasts it, and gives it the
configured initial hop limit. Received plaintext text appears as a `TEXT` log line.

Every accepted radio frame produces a compact routing line. With `REPEATER_CONSOLE_DUMP_FRAMES=1`, the complete received
and relayed frame is also printed as `RX_RAW` or `TX_RAW` hexadecimal. On a zero-PSK Meshtastic channel, bytes after the
first 16 bytes are the clear protobuf `Data` payload and can be passed directly to an external decoder. `snr_qdb` is SNR
in quarter-decibel units.

Set `REPEATER_USB_CDC_LOGGING=0` in `include/repeater_config.h` to compile console handling out, or set
`REPEATER_CONSOLE_DUMP_FRAMES=0` to retain status lines without full frame dumps. For a hardware-UART fallback, disable
USB logging and set `REPEATER_UART_LOGGING=1`; USART1 uses PA9 TX, PA10 RX, and `REPEATER_UART_BAUD`.

For receive-only testing, build and flash the `e22-400mbh-sc-rx-only` environment. It defines `REPEATER_RX_ONLY`, so the
firmware continues to receive, inspect, and log frames, but compile-time guards remove the CAD and radio-transmit path;
queued relay and console-originated frames are dropped when due.

`REPEATER_CHANNEL_NAME` must exactly match the zero-PSK primary channel name on the stock Meshtastic nodes. For the
default `LONG_FAST` channel this is `LongFast`; the plaintext channel hash is the XOR of those name bytes. A blank
secondary-channel PSK inherits the primary key in stock Meshtastic, so configure the primary channel itself with no PSK.

With `ENABLE_DEBUG=1`, the radio listens for the E22 demo's raw LoRa packets using sync word `0x14`, an 8-symbol preamble,
and forced LDRO. Each packet is printed as `RX_LORA` plus `RX_RAW` on USB CDC and is never relayed. Set `ENABLE_DEBUG=0`
to restore Meshtastic sync word `0x2b` and normal repeater behavior.

## Test the wire-header logic on a host

```sh
make test
```
