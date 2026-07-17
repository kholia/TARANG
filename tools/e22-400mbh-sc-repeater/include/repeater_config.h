#pragma once

#ifndef ENABLE_DEBUG
#define ENABLE_DEBUG 0
#endif

// Define for receive-only test builds. No frame can reach the radio transmit path.
#define REPEATER_RX_ONLY 1

// Set this to the exact channel frequency printed by a Meshtastic node on boot.
// Zero is deliberately receive/transmit-disabled: frequency legality is
// regional.
#ifndef REPEATER_FREQUENCY_MHZ
#define REPEATER_FREQUENCY_MHZ 433.920F
#endif

// This drives the SX1268 inside the E22 module, whose external PA adds gain.
// CDEBYTE maps 10 dBm chip drive to approximately 24 dBm module output.
#ifndef REPEATER_RADIO_DRIVE_DBM
#define REPEATER_RADIO_DRIVE_DBM 10
#endif

// TARANG PHY. All nodes on the radio channel must use the same settings.
#ifndef REPEATER_BANDWIDTH_KHZ
#define REPEATER_BANDWIDTH_KHZ 125.0F
#endif

#ifndef REPEATER_SPREADING_FACTOR
#define REPEATER_SPREADING_FACTOR 11
#endif

#ifndef REPEATER_CODING_RATE
#define REPEATER_CODING_RATE 5
#endif

#ifndef REPEATER_PREAMBLE_LENGTH
#define REPEATER_PREAMBLE_LENGTH 16
#endif

#if ENABLE_DEBUG
#define REPEATER_RADIO_SYNC_WORD 0x14
#define REPEATER_RADIO_PREAMBLE_LENGTH 8
#else
#define REPEATER_RADIO_SYNC_WORD 0x2B
#define REPEATER_RADIO_PREAMBLE_LENGTH REPEATER_PREAMBLE_LENGTH
#endif

// A zero-PSK channel hash is the XOR of this exact, case-sensitive name.
#ifndef REPEATER_CHANNEL_NAME
#define REPEATER_CHANNEL_NAME "LongFast"
#endif

#ifndef REPEATER_ORIGIN_HOP_LIMIT
#define REPEATER_ORIGIN_HOP_LIMIT 3
#endif

#ifndef REPEATER_TCXO_VOLTAGE
#define REPEATER_TCXO_VOLTAGE 3.3F
#endif

#ifndef REPEATER_USB_CDC_LOGGING
#define REPEATER_USB_CDC_LOGGING 1
#endif

#ifndef REPEATER_UART_LOGGING
#define REPEATER_UART_LOGGING 0
#endif

#if REPEATER_USB_CDC_LOGGING && REPEATER_UART_LOGGING
#error "Select only one repeater console"
#endif

#ifndef REPEATER_UART_BAUD
#define REPEATER_UART_BAUD 115200
#endif

// Emit complete RX and relayed TX frames as hexadecimal for external decoders.
#ifndef REPEATER_CONSOLE_DUMP_FRAMES
#define REPEATER_CONSOLE_DUMP_FRAMES 1
#endif
