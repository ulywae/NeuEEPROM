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

NeuEEPROM is designed to make persistent storage simple to use, while handling safety, alignment, and integrity internally — no more manual offset calculations or corrupted data.

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

NeuEEPROM removes complexity from the user while enforcing safety and consistency internally. You just `put` and `get` — the library handles alignment, wear protection, and power-fail safety.

---

## Getting Started

### 1. Initialize

```cpp
#include <NeuEEPROM.h>

void setup() {
    // Optional: Set basic obfuscation key before begin
    uint8_t key[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    neuEEPROM.setEncryption(key, sizeof(key));

    // Disable auto-formatting if you want manual control (default = true)
    neuEEPROM.autoFormatting(false);

    // Initialize with 512 bytes of Shadow RAM
    neuEEPROM.begin(512);

    // Register your data slot
    neuEEPROM.registerSlot(1, sizeof(MySettings));
}
```

### 2. Register Slots

```cpp
// Register a storage slot with a unique ID and your data structure size
// Automatically calculates memory offset and ensures 4-byte alignment
neuEEPROM.registerSlot(ID_CONFIG, sizeof(Config));
```

### 3. Write Data

```cpp
// Update Shadow RAM (only marks as "dirty" if data actually changed)
neuEEPROM.put(ID_CONFIG, config);

// Physically commit to Flash with safety checks
neuEEPROM.commit();
```

### 4. Read Data

```cpp
// Retrieve from Shadow RAM into your variable
Config config;
if (neuEEPROM.get(ID_CONFIG, config)) {
    // Data loaded successfully
}
```

### 5. Auto-Commit (Optional)

```cpp
void loop() {
    // Place this in loop() for auto-commit and rate-limiting
    neuEEPROM.update();
}
```

---

## Auto-Padding for Buffer Alignment

NeuEEPROM automatically pads buffer size to the nearest 4‑byte boundary — no more alignment headaches.

What Size
Struct size Raw size (e.g., 83 bytes)
Buffer size Auto-padded to 4‑byte alignment (84 bytes)
File size in flash Buffer + 4 bytes (write counter) + 1 byte (CRC) = 89 bytes

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

Register a callback to catch issues in real-time:

```cpp
void myErrorHandler(uint8_t code, uint8_t id) {
    switch(code) {
        case neuEEPROM.ERR_FLASH_SPAM:
            digitalWrite(LED_BUILTIN, HIGH);  // Alert!
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

## Error Codes

Code Meaning:

**ERR_NOT_REGISTERED Slot not registered before put/get
**ERR_SIZE_MISMATCH Data size doesn't match registered slot
**ERR_BUFFER_OVERFLOW Shadow RAM full
**ERR_MALLOC_FAIL System heap exhausted (critical)
**ERR_FLASH_LOCKED Operations blocked due to redundant writes
**ERR_FLASH_SPAM Write spam detected — lockdown activated
**ERR_CRC_FAIL Data integrity check failed
**ERR_ATOMIC_SWAP File system error during safe-write
**ERR_HEALTH_LOW Flash nearing end of life (<10% health)

---

## Debug Tools

```cpp
// Professional hex dump with ASCII preview
neuEEPROM.hexDump();

// Visualize memory map: ID → Offset → Size
neuEEPROM.debugSlots();

// Monitor flash health
Serial.printf("Health: %.2f%%\n", neuEEPROM.getHealth());
Serial.printf("Write cycles: %d\n", neuEEPROM.getWriteCount());

// RAM usage
Serial.printf("Library heap: %d bytes\n", neuEEPROM.getLibraryHeapUsage());
Serial.printf("Free heap: %d bytes\n", neuEEPROM.getSystemFreeHeap());
```

---

## Data Suitcase (Export/Import)

Backup and restore full storage via any Stream (Serial, WiFi, SD):

```cpp
// Export to Serial (save to PC)
neuEEPROM.exportData(Serial);

// Import from Serial
neuEEPROM.importData(Serial, 5000);
```

---

## Security Note

NeuEEPROM includes a lightweight XOR cipher for basic obfuscation — it prevents accidental reading of plaintext data (e.g., opening the .bin file in a text editor).

This is NOT cryptographically secure. For sensitive data (passwords, API keys, personal info), replace NeuCipher with AES-128 or ChaCha20.

---

## Why NeuEEPROM?

Problem NeuEEPROM Solution:
**Manual offset calculations Smart Slot Management
**Flash wear from frequent writes Rate Limiting + Anti-Spam Lockdown
**Data corruption on power loss Atomic Swap Transaction
**Silent data corruption Self-Healing CRC + Auto-wipe
**Difficult debugging Hex Dump + Slot Map + Error Callbacks
**No flash lifespan visibility Health Meter + Write Odometer

---

## Design Goals

**Deterministic behavior
**Minimal overhead
**Hardware-friendly
**Safe by default

---

## Requirements

**ESP32 or ESP8266 Arduino core
**LittleFS enabled in board settings
**Arduino framework or PlatformIO

---

## Contributing

Issues, PRs, and suggestions welcome — especially:

**Alternative cipher implementations (AES, ChaCha20)
**Wear leveling improvements
**Platform expansion (RP2040, ESP32-S3)

---

## License

MIT License — Free for personal and commercial use.

---

## Final Note

NeuEEPROM is built from real-world problems — designed to prevent them from happening again.

If you've ever lost data to a power outage or killed a flash chip with an accidental infinite loop, this library is for you.

---

## Made with for ESP32 & ESP8266

