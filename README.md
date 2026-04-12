# NeuEEPROM

**Lightweight, safe, and slot-based EEPROM system for ESP32 & ESP8266**

NeuEEPROM is designed to make persistent storage simple to use, while handling safety, alignment, and integrity internally.

---

## Features (v2.0.0 "The Security Update")

- **Smart Slot Management**: No more manual byte counting or memory overlap.
- **Hardware Odometer**: Tracks total write cycles to monitor your Flash chip's lifespan.
- **Flash Health Meter**: Get real-time health percentage of your hardware.
- **Zero-Knowledge Encryption**: Integrated XOR cipher to secure data on the physical chip.
- **Data Suitcase (Export/Import)**: Backup and restore settings via any Stream (Serial/WiFi/SD).
- **Event-Driven Diagnostics**: Professional callback system for real-time error reporting.
- **Anti-Spam Lockdown**: Military-grade protection against accidental infinite write loops.
- **Atomic Swap Transaction**: Power-failure safe writes using `.tmp` and `rename` logic.
- **Self-Healing**: Automatic CRC integrity check and auto-wipe for corrupted data.
- **Shadow RAM Engine**: Lightning-fast `put`/`get` operations with dirty-state tracking.
- **System Monitors**: Built-in Heap Usage and fragmentation trackers (ESP8266 specific).
- **Master Clear Window**: 5-second safety period for total factory resets.

---

## Philosophy

> Simple outside, controlled inside.

NeuEEPROM removes complexity from the user while enforcing safety and consistency internally.

---

## Getting Started

### 1. Initialize

```cpp

#include <NeuEEPROM.h>

void setup() {
    // Optional: Set encryption key before begin
  uint8_t key[] = {0x12, 0x34, 0x56};
  neuEEPROM.setEncryption(key, sizeof(key));

    // Enable automatic formatting of the filesystem if mounting fails.
    // Useful for brand new chips or recovering from filesystem corruption.
    // Default "true" if not set.
    neuEEPROM.autoFormatting(false);

  // Initialize with 512 bytes of Shadow RAM
  neuEEPROM.begin(512);

  // Register your data slot
  neuEEPROM.registerSlot(1, sizeof(MySettings));
}

```

---

### 2. Register slots

```cpp
// Register a storage slot with a unique ID and the size of your data structure.
// This automatically calculates the memory offset and ensures 4-byte alignment.
neuEEPROM.registerSlot(ID_CONFIG, sizeof(Config));
```

---

### 3. Write data

```cpp
// Update the data in Shadow RAM for the specified slot.
// It only marks the data as "dirty" if the new value differs from the current one.
neuEEPROM.put(ID_CONFIG, config);

// Physically write all "dirty" data from RAM to Flash storage.
// Includes safety checks for rate-limiting and atomic file swapping.
neuEEPROM.commit();
```

---

### Pro-Tip

If you are using the Auto-Commit feature, you can also add a comment for update():

```cpp
// Place this in your loop() to handle auto-commit and rate-limiting logic.
// It checks if the "settling time" has passed before writing to Flash.
neuEEPROM.update();
```

---

### 4. Read data

```cpp
// Retrieve data from Shadow RAM and copy it into your local variable.
// Returns "true" if the slot exists and the data size matches perfectly.
Config config;
neuEEPROM.get(ID_CONFIG, config);
```

---

## Auto Commit (Optional)

```cpp
// Set a "settling time" of 5000ms (5 seconds).
// Data will only be written to Flash after it remains unchanged for this duration.
neuEEPROM.setAutoCommit(5000);

void loop() {
    // The main engine that manages the background tasks.
    // Handles automatic commits, rate-limiting, and security locks.
    neuEEPROM.update();
}
```

---

## Verify Data Integrity

```cpp
// Compare RAM content with physical Flash storage byte-by-byte.
// If data is corrupted (CRC mismatch), perform a full wipe to recover.
if (!neuEEPROM.verify()) {
    neuEEPROM.wipe();
}
```

---

## Wipe Storage

```cpp
// Manually reset the Shadow RAM to 0xFF and erase the physical file from Flash.
neuEEPROM.wipe();
```

---

## New "Event-Driven" Error System:

- **Added Error Callbacks**: You can now register a callback function using `onError()`. The library will "report" to your main code whenever something goes wrong.
- **Detailed Error Codes (Enum)**:
  - `ERR_NOT_REGISTERED`: Attempted to put/get data to a non-existent slot.
  - `ERR_SIZE_MISMATCH`: Data type size doesn't match the registered slot.
  - `ERR_BUFFER_OVERFLOW`: Allocated Shadow RAM is full.
  - `ERR_MALLOC_FAIL`: System Heap memory is exhausted (Critical).
  - `ERR_FLASH_LOCKED`: Operations blocked due to persistent redundant writes.
  - `ERR_FLASH_SPAM`: Hardware protection triggered due to write spamming.
  - `ERR_CRC_FAIL`: Data integrity check failed during load or verify.
  - `ERR_ATOMIC_SWAP`: File system failure during safe-write process.

### Improvements & Optimizations:

- **Integrity Check**: Enhanced `verify()` and `begin()` logic to ensure 100% data consistency.
- **Defensive Programming**: Added extra cleanup layers for temporary files to prevent junk accumulation.
- **Improved 4-Byte Alignment**: Stricter memory padding for better stability on ESP32/ESP8266.
- **Zero-String Footprint**: All diagnostic codes are handled via Enums, keeping the library lightweight and RAM-efficient.

### Example Usage:

```cpp
void myErrorHandler(uint8_t code, uint8_t id) {
    if (code == neuEEPROM.ERR_FLASH_SPAM) {
        // Trigger alarm or LED if someone is spamming the flash!
    }
}

void setup() {
    neuEEPROM.onError(myErrorHandler);
    neuEEPROM.begin(512);
}
```

---

## Debug Tools

```cpp
// Print a professional Hexadecimal view of the entire Shadow RAM.
// Pro-tip: Useful for inspecting raw data and printable ASCII characters.
neuEEPROM.hexDump();

// Visualize the internal memory map.
// Shows registered IDs, their memory offsets, and their actual sizes.
neuEEPROM.debugSlots();
```

---

## Slot System

Instead of using manual offsets, NeuEEPROM uses logical IDs:

```cpp
// Define storage blocks with unique IDs.
// Must be called before put/get to reserve space and ensure 4-byte alignment.
neuEEPROM.registerSlot(ID_WIFI, sizeof(Wifi));
neuEEPROM.registerSlot(ID_CONFIG, sizeof(Config));
```

Each slot is:

- Automatically aligned (4-byte)
- Safely mapped in memory
- Protected from overlap

---

## Monitoring Hardware Health

```cpp
Serial.printf("Flash Health: %.2f%%\n", neuEEPROM.getHealth());
Serial.printf("Total Writes: %d cycles\n", neuEEPROM.getWriteCount());
```

---

## Why NeuEEPROM?

Traditional EEPROM usage can lead to:

- Incorrect offset calculations
- Frequent flash writes (wear)
- Data corruption on power loss
- Hard-to-debug issues

NeuEEPROM solves these with:

- Structured storage
- Write protection & rate limiting
- Atomic commit system
- Built-in integrity checks

- **Hardware Protection**: Built-in Write-Spam protection and **Lockdown** mode to prevent killing your Flash chip from accidental infinite loops.
- **Shadow RAM Speed**: Uses a fast RAM buffer for `get` and `put` operations. Write to Flash only when needed.
- **Zero-Knowledge Encryption**: Integrated XOR cipher to keep your data encrypted on the chip but plain in RAM.
- **Flash Odometer**: Tracks total write cycles and provides a **Health Meter** for your hardware.
- **Atomic Swap**: Uses temporary files and CRC checks during writes to ensure power failures never corrupt your data.
- **Portable Data**: Export and Import your entire storage as a `.bin` suitcase via any `Stream` (Serial, WiFi, SD Card).

---

## Design Goals

- Deterministic behavior
- Minimal overhead
- Hardware-friendly
- Safe by default

---

## Notes

- Designed for ESP32 & ESP8266
- Uses 4-byte alignment for stability and performance
- XOR is used for lightweight integrity checking

---

## Contributing

Feel free to fork, improve, or suggest features.

---

## License

MIT License

---

## Final Note

NeuEEPROM is built from real-world problems —
designed to prevent them from happening again.
