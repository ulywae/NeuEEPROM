# NeuEEPROM

<p align="center">
  <strong>Lightweight, safe, and slot-based EEPROM system for ESP32 & ESP8266</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-ESP8266-blue.svg">
  <img src="https://img.shields.io/badge/Arduino-00979D?logo=arduino&logoColor=white">
  <img src="https://img.shields.io/badge/License-MIT-green.svg">
  <img src="https://img.shields.io/badge/PlatformIO-ready-orange.svg">
</p>

NeuEEPROM makes persistent storage simple — no manual offsets, no corruption fear, no flash spam.  
It handles safety, alignment, and integrity internally.

---

## Features

| Category | Features |
|----------|----------|
| **Memory Management** | Smart Slot Management, Shadow RAM Engine, 4-byte auto-alignment |
| **Data Integrity** | Atomic Swap Transaction, Self-Healing CRC, Automatic wipe on corruption |
| **Hardware Protection** | Anti-Spam Lockdown, Rate Limiting, Flash Health Meter, Hardware Odometer |
| **Security** | Basic XOR Obfuscation (replaceable with AES/ChaCha20) |
| **Diagnostics** | Event-Driven Error Callbacks, Heap Monitors, Fragmentation Tracker |
| **Utilities** | Data Suitcase (Export/Import), Master Clear Window, Hex Dump |

---

## Philosophy

> **Simple outside, controlled inside.**

You just `put` and `get` — the library handles alignment, wear protection, and power-fail safety.

---

## Getting Started

### 1. Initialize

```cpp
#include <NeuEEPROM.h>

void setup() {
    // Optional: set obfuscation key before begin
    uint8_t key[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    neuEEPROM.setEncryption(key, sizeof(key));

    neuEEPROM.autoFormatting(false); // default = true
    neuEEPROM.begin(512);
    neuEEPROM.registerSlot(1, sizeof(MySettings));
}
```

### 2. Register Slots

```cpp
neuEEPROM.registerSlot(ID_CONFIG, sizeof(Config));
```

### 3. Write Data

```cpp
neuEEPROM.put(ID_CONFIG, config);
neuEEPROM.commit(); // flush to flash
```

### 4. Read Data

```cpp
Config config;
if (neuEEPROM.get(ID_CONFIG, config)) {
    // data loaded
}
```

### 5. Auto-Commit (Optional)

```cpp
void loop() {
    neuEEPROM.update(); // call this in loop()
}
```

---

## Auto-Padding for Buffer Alignment

Automatically pads buffer size to the nearest 4‑byte boundary.

What Size
Struct size 83 bytes
Buffer size 84 bytes (auto‑padded)
File size in flash 84 + 4 (counter) + 1 (CRC) = 89 bytes

Example:

```cpp
struct DeviceConfig {
    char ssid[32];
    char password[32];
    char ip[16];
    uint16_t port;
    uint8_t deviceId;
}; // sizeof = 83 bytes

neuEEPROM.begin(sizeof(DeviceConfig)); 
// Buffer → 84 bytes | File → 89 bytes
```

---

## Error Handling System

```cpp
void myErrorHandler(uint8_t code, uint8_t id) {
    switch(code) {
        case neuEEPROM.ERR_FLASH_SPAM:
            digitalWrite(LED_BUILTIN, HIGH);
            break;
        case neuEEPROM.ERR_CRC_FAIL:
            Serial.println("Data corrupted, auto-repair triggered");
            break;
    }
}

void setup() {
    neuEEPROM.onError(myErrorHandler);
}
```

---

### Error Codes

Code Meaning:

· ERR_NOT_REGISTERED Slot not registered before put/get

· ERR_SIZE_MISMATCH Data size doesn't match registered slot

· ERR_BUFFER_OVERFLOW Shadow RAM full

· ERR_MALLOC_FAIL System heap exhausted (critical)

· ERR_FLASH_LOCKED Operations blocked due to redundant writes

· ERR_FLASH_SPAM Write spam detected — lockdown activated

· ERR_CRC_FAIL Data integrity check failed

· ERR_ATOMIC_SWAP File system error during safe-write

· ERR_HEALTH_LOW Flash nearing end of life (<10% health)

---

## Debug Tools

```cpp
neuEEPROM.hexDump();                         // hex + ASCII preview
neuEEPROM.debugSlots();                      // slot map + free list
Serial.printf("Health: %.2f%%\n", neuEEPROM.getHealth());
Serial.printf("Write cycles: %d\n", neuEEPROM.getWriteCount());
Serial.printf("Library heap: %d bytes\n", neuEEPROM.getLibraryHeapUsage());
Serial.printf("Free heap: %d bytes\n", neuEEPROM.getSystemFreeHeap());
```

---

## Data Suitcase (Export/Import)

```cpp
neuEEPROM.exportData(Serial);        // backup to PC
neuEEPROM.importData(Serial, 5000);  // restore from PC
```

Works with any Stream (Serial, WiFi, SD).

---

## Security Note

NeuEEPROM includes a lightweight XOR cipher for basic obfuscation — prevents accidental reading of plaintext data (e.g., opening the .bin file in a text editor).

NOT cryptographically secure. 
For sensitive data, replace NeuCipher with AES-128 or ChaCha20.

---

## Why NeuEEPROM?

Problem Solution:

· Manual offset calculations Smart Slot Management

· Flash wear from frequent writes Rate Limiting + Anti-Spam Lockdown

· Data corruption on power loss Atomic Swap Transaction

· Silent data corruption Self-Healing CRC + Auto-wipe

· Difficult debugging Hex Dump + Slot Map + Error Callbacks

· No flash lifespan visibility Health Meter + Write Odometer

---

## Design Goals

· Deterministic behavior

· Minimal overhead

· Hardware-friendly

· Safe by default

---

## Requirements

· ESP32 or ESP8266 Arduino core

· LittleFS enabled

· Arduino framework or PlatformIO

---

## Contributing

Issues, PRs, and suggestions welcome — especially:

· Alternative cipher implementations (AES, ChaCha20)

· Wear leveling improvements

· Platform expansion (RP2040, ESP32-S3)

---

## License

MIT — free for personal and commercial use.

---

## Final Note

NeuEEPROM is built from real-world problems — designed to prevent them from happening again.

If you've ever lost data to a power outage or killed a flash chip with an accidental infinite loop, this library is for you.

---

### Made with for ESP32 & ESP8266

```
