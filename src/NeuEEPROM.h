#ifndef NEU_EEPROM_H
#define NEU_EEPROM_H

#include <Arduino.h>
#include <LittleFS.h>
#include <type_traits>

class NeuEEPROM
{
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

public:
    NeuEEPROM();
    ~NeuEEPROM();

    bool begin(size_t size = 512, const char *path = "/neu_eeprom.bin"); // Initialize NeuEEPROM
    void autoFormatting(bool enable) { _autoFormat = enable; }           // Enable autoformat if mount fails
    bool registerSlot(uint8_t id, size_t size);                          // Register a slot with a unique ID and a specific size (Optional, for internal management)

    template <typename T>
    void put(uint8_t id, const T &data) // Save data to Shadow RAM, mark dirty if changes occur
    {
        static_assert(std::is_trivially_copyable<T>::value, "Data type too complex!");
        SlotNode *slot = _findSlot(id);
        if (!slot || slot->size != sizeof(T))
            return;

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
        if (!slot || slot->size != sizeof(T))
            return false;

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
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuEEPROM neuEEPROM;
#endif

#endif
