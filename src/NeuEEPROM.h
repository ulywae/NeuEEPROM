/**
 * ---------------------------------------------------------------------------------
 *  NeuEEPROM - High Performance Virtual EEPROM for ESP32/ESP8266
 * ---------------------------------------------------------------------------------
 *  Description:
 *  A lightweight, RAM-cached virtual EEPROM library built on top of LittleFS.
 *  Designed for speed, flash longevity, and data integrity.
 *
 *  Key Features:
 *  - Shadow RAM: Ultra-fast O(1) access (No Flash read on get/put).
 *  - Atomic Swap: Power-failure resilient writes using .tmp swap logic.
 *  - Flash Protection: Built-in rate limiter and anti-spam protection.
 *  - ID-Based: Memory-efficient storage using uint8_t IDs instead of string keys.
 *  - Integrity: Byte-by-byte XOR checksum for data validation.
 *
 *  Author: [Neufa/ulywae]
 *  Version: 1.2.0
 *  License: MIT
 *  Repository: https://github.com/ulywae/NeuEEPROM
 * ---------------------------------------------------------------------------------
 *  NOTE: This library is designed to be "Disciplined". 
 *  If you trigger the Flash Spam Protection, a hardware restart is required.
 * ---------------------------------------------------------------------------------
 */


#ifndef NEU_EEPROM_H
#define NEU_EEPROM_H

#include <Arduino.h>
#include <LittleFS.h>
#include <type_traits>

class NeuEEPROM
{
public:
    NeuEEPROM();
    ~NeuEEPROM();

    // ERROR CODE
    enum NeuError
    {
        ERR_NONE = 0,
        ERR_NOT_REGISTERED,  // Slot unregistered
        ERR_SIZE_MISMATCH,   // Size mismatch
        ERR_BUFFER_OVERFLOW, // Buffer overflow
        ERR_MALLOC_FAIL,     // Memory allocation failed
        ERR_FLASH_LOCKED,    // Flash locked
        ERR_FLASH_SPAM,      // Spam indicate
        ERR_FS_MOUNT,        // LittleFS mount failed
        ERR_CRC_FAIL,        // CRC mismatch / file corrupted
        ERR_ATOMIC_SWAP      // Atomic swap failed
    };

    typedef void (*ErrorCallback)(uint8_t errorCode, uint8_t id);
    void onError(ErrorCallback cb) { _errorCallback = cb; }

    bool begin(size_t size = 512, const char *path = "/neu_eeprom.bin"); // Initialize NeuEEPROM
    void autoFormatting(bool enable) { _autoFormat = enable; }           // Enable autoformat if mount fails, set before begin
    bool registerSlot(uint8_t id, size_t size);                          // Register a slot with a unique ID and a specific size (Optional, for internal management)

    template <typename T>
    void put(uint8_t id, const T &data) // Save data to Shadow RAM, mark dirty if changes occur
    {
        static_assert(std::is_trivially_copyable<T>::value, "Data type too complex!");

        SlotNode *slot = _findSlot(id);

        if (!slot)
        {
            _reportError(ERR_NOT_REGISTERED, id);
            return;
        }

        if (slot->size != sizeof(T))
        {
            _reportError(ERR_SIZE_MISMATCH, id);
            return;
        }

        if (memcmp(_buffer + slot->offset, &data, sizeof(T)) != 0)
        {
            memcpy(_buffer + slot->offset, &data, sizeof(T));
            _dirty = true;

            if (_dirtyTimer == 0)
                _dirtyTimer = millis();
        }
    }

    template <typename T>
    bool get(uint8_t id, T &data) // Load data from Shadow RAM
    {
        static_assert(std::is_trivially_copyable<T>::value, "Data type incompatible!");

        SlotNode *slot = _findSlot(id);
        if (!slot)
        {
            _reportError(ERR_NOT_REGISTERED, id);
            return false;
        }

        if (slot->size != sizeof(T))
        {
            _reportError(ERR_SIZE_MISMATCH, id);
            return false;
        }

        memcpy(&data, _buffer + slot->offset, sizeof(T));
        return true;
    }

    bool commit(uint32_t maxIntervalMs = 100, uint8_t maxWrites = 10);
    bool verify();
    bool wipe();
    void update();

    // Set how long to wait from dirty data until auto-commit (ms).
    // If ms = 0, auto-commit is disabled.
    void setAutoCommit(uint32_t ms) { _autoCommitMs = ms; }

    bool isDirty() const { return _dirty; }
    bool isLocked() const { return _isLocked; }
    void hexDump(size_t bytesPerLine = 16);
    void debugSlots();

private:
    struct SlotNode
    {
        uint8_t id;
        uint16_t offset;
        uint16_t size;
        SlotNode *next;
    };

    uint8_t *_buffer = nullptr;
    size_t _size = 0;
    const char *_path = nullptr;
    SlotNode *_head = nullptr;

    uint16_t _nextOffset = 0;
    bool _dirty = false;
    bool _isLocked = false;
    bool _autoFormat = false;

    uint8_t _writeCount = 0;
    uint8_t _sameDataCount = 0;
    uint32_t _lastCheckTime = 0;
    uint32_t _dirtyTimer = 0;
    uint32_t _autoCommitMs = 0;

    uint8_t _calculateChecksum(uint8_t *data, size_t len);
    SlotNode *_findSlot(uint8_t id);

    ErrorCallback _errorCallback = nullptr;

    // Error reporting
    void _reportError(uint8_t code, uint8_t id = 0)
    {
        if (_errorCallback)
            _errorCallback(code, id);
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuEEPROM neuEEPROM;
#endif

#endif
