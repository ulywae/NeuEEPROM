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
- CRC8 integrity check
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
neuEEPROM.begin(512); // size in bytes
```

---

### 2. Register slots

```cpp
neuEEPROM.registerSlot(ID_CONFIG, sizeof(Config));
```

---

### 3. Write data

```cpp
neuEEPROM.put(ID_CONFIG, config);
neuEEPROM.commit();
```

---

### 4. Read data

```cpp
Config config;
neuEEPROM.get(ID_CONFIG, config);
```

---

## Auto Commit (Optional)

```cpp
neuEEPROM.setAutoCommit(5000); // commit every 5 seconds

void loop() {
    neuEEPROM.update();
}
```

---

## Verify Data Integrity

```cpp
if (!neuEEPROM.verify()) {
    neuEEPROM.wipe(); // reset if corrupted
}
```

---

## Wipe Storage

```cpp
neuEEPROM.wipe();
```

---

## Debug Tools

```cpp
neuEEPROM.hexDump();
neuEEPROM.debugSlots();
```

---

## Slot System

Instead of using manual offsets, NeuEEPROM uses logical IDs:

```cpp
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
- CRC8 is used for lightweight integrity checking

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
