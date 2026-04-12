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
        ERR_ATOMIC_SWAP,     // Atomic swap failed
        ERR_HEALTH_LOW,      // Health check
    };

    typedef void (*ErrorCallback)(uint8_t errorCode, uint8_t id);
    void onError(ErrorCallback cb) { _errorCallback = cb; }

    /**
     * @description Enable/Disable autoformatting
     * Enable autoformat if mount fails, set before begin if used
     * @param enable
     */
    void autoFormatting(bool enable) { _autoFormat = enable; }
    bool begin(size_t size = 512, const char *path = "/neu_eeprom.bin"); // Initialize NeuEEPROM
    bool registerSlot(uint8_t id, size_t size);                          // Register a slot with a unique ID and a specific size (Optional, for internal management)

    /**
     * @description Save data to Shadow RAM, mark dirty if changes occur
     * @param id Slot ID
     * @param data Data
     */
    template <typename T>
    void put(uint8_t id, const T &data)
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

    /**
     * @description Load data from Shadow RAM
     * @param id Slot ID
     * @param data Data
     */
    template <typename T>
    bool get(uint8_t id, T &data)
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

    /**
     * @description Set auto-commit interval
     * Set how long to wait from dirty data until auto-commit (ms), default is 1000ms.
     * If ms = 0, auto-commit is disabled.
     * @param ms
     */
    void setAutoCommit(uint32_t ms) { _autoCommitMs = ms; }
    bool commit(uint32_t maxIntervalMs = 1000, uint8_t maxWrites = 10);
    bool verify();
    bool wipe();
    void update();

    bool isDirty() const { return _dirty; }
    bool isLocked() const { return _isLocked; }
    void hexDump(size_t bytesPerLine = 16);
    void debugSlots();
    uint32_t getWriteCount() const { return _totalWriteCycles; }
    float getHealth();

    /**
     * [getLibraryHeapUsage] Gets the total RAM (bytes) allocated by this library.
     * Includes the main data buffer and slot metadata structures.
     */
    size_t getLibraryHeapUsage() const
    {
        size_t total = _size; // Main buffer (Shadow RAM)

        // Calculate the memory used by each SlotNode (metadata)
        SlotNode *current = _head;
        while (current)
        {
            total += sizeof(SlotNode);
            current = current->next;
        }
        return total;
    }

    /**
     * [getSystemFreeHeap] Gets the remaining global RAM available on the ESP32/ESP8266.
     */
    uint32_t getSystemFreeHeap() const
    {
        return ESP.getFreeHeap();
    }

#if defined(ESP8266)
    /**
     * [getMaxBlock8266] ESP8266 specific to check RAM fragmentation.
     */
    uint32_t getMaxBlock8266() const
    {
        return ESP.getMaxFreeBlockSize();
    }
#endif

    /**
     * [setEncryption] Set the encryption key.
     * Call this before begin() if you want to use encryption.
     * @param key Encryption key
     * @param len Key length
     */
    void setEncryption(const uint8_t *key, size_t len)
    {
        _encKey = key;
        _encKeyLen = len;
    }
    bool masterClear();

    void exportData(Stream &dest);
    bool importData(Stream &src, uint32_t timeoutMs = 5000);

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
    bool _autoFormat = true;

    const uint8_t *_encKey = nullptr;
    size_t _encKeyLen = 0;
    uint32_t _startTime = 0;

    uint8_t _writeCount = 0;
    uint8_t _sameDataCount = 0;
    uint32_t _lastCheckTime = 0;
    uint32_t _dirtyTimer = 0;
    uint32_t _autoCommitMs = 1000;

    uint8_t _calculateChecksum(uint8_t *data, size_t len);
    SlotNode *_findSlot(uint8_t id);

    ErrorCallback _errorCallback = nullptr;

    // Error reporting
    void _reportError(uint8_t code, uint8_t id = 0)
    {
        if (_errorCallback)
            _errorCallback(code, id);
    }

    uint32_t _totalWriteCycles = 0; // Total number of write cycles
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuEEPROM neuEEPROM;
#endif

#endif
