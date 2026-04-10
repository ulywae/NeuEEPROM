# NeuEEPROM

**Lightweight, safe, and slot-based EEPROM system for ESP32 & ESP8266**

NeuEEPROM is designed to make persistent storage simple to use, while handling safety, alignment, and integrity internally.

---

## Features

- Slot-based storage (no manual offset)
- 4-byte aligned memory access
- Safe commit (temp file + rename)
- Auto commit engine (non-blocking)
- Anti write-spam protection
- Dirty state tracking
- XOR Integrity Check
- Data verification support
- Easy wipe/reset system
- Debug tools (`hexDump`, `debugSlots`)

---

## Philosophy

> Simple outside, controlled inside.

NeuEEPROM removes complexity from the user while enforcing safety and consistency internally.

---

## Getting Started

### 1. Initialize

```cpp
// Enable automatic formatting of the filesystem if mounting fails.
// Useful for brand new chips or recovering from filesystem corruption.
// Default "false" if not set.
neuEEPROM.autoFormatting(true);

// Initialize the library with a 512-byte Shadow RAM buffer.
// This will mount the FS, cleanup junk, and load existing data from Flash.
neuEEPROM.begin(512);

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

## 🛠 Debug Tools

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
