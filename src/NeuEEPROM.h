#ifndef NEU_EEPROM_H
#define NEU_EEPROM_H

#include <Arduino.h>
#include <LittleFS.h>
#include <type_traits>

class NeuEEPROM
{
private:
    uint8_t *_buffer = nullptr;  // Shadow RAM to store temporary data before commit
    size_t _size = 0;            // Shadow RAM size
    const char *_path = nullptr; // File path in LittleFS to store EEPROM data
    bool _dirty = false;         // Flag to indicate whether there are uncommitted changes to Flash
    bool _autoFormat = false;    // Enable auto-format if mount fails

    // Slot system
    struct Slot
    {
        uint16_t offset;
        uint16_t size;
    };

    Slot _slots[256];              // List of Slots
    bool _slotUsed[256] = {false}; // List of used Slots
    uint16_t _nextOffset = 0;      // Offset for the next slot

    // Security Guard Variables
    uint32_t _lastCheckTime = 0; // Last time a commit or verification was successful
    uint8_t _writeCount = 0;     // Counts the number of times a commit was made
    bool _isLocked = false;      // True if blocked
    uint8_t _sameDataCount = 0;  // Counts the number of times the same data was committed
    uint32_t _dirtyTimer = 0;    // Last time the data was dirty
    uint32_t _autoCommitMs = 0;  // 0 means the feature is disabled

    uint8_t _calculateChecksum(uint8_t *data, size_t len);

public:
    NeuEEPROM();
    ~NeuEEPROM();

    bool begin(size_t size = 512, const char *path = "/neu_eeprom.bin"); // Initialize NeuEEPROM
    void autoFormatting(bool enable) { _autoFormat = enable; }           // Enable autoformat if mount fails
    bool registerSlot(uint8_t id, size_t size);                          // Register a slot with a unique ID and a specific size (Optional, for internal management)

    template <typename T>
    void put(uint8_t id, const T &data) // Save data to Shadow RAM, mark dirty if changes occur
    {
        // Check: Is this data type safe to copy raw (Plain Old Data)?
        static_assert(std::is_trivially_copyable<T>::value, "The data type is too complex for the EEPROM!");

        if (!_slotUsed[id])
            return;

        auto &slot = _slots[id];
        if (slot.size != sizeof(T))
            return;

        if (memcmp(_buffer + slot.offset, &data, sizeof(T)) != 0)
        {
            memcpy(_buffer + slot.offset, &data, sizeof(T));
            _dirty = true;
        }
    }

    template <typename T>
    bool get(uint8_t id, T &data) // Read data from Shadow RAM
    {
        // Check: Make sure the destination data type is also compatible
        static_assert(std::is_trivially_copyable<T>::value, "ERROR: The target data type is incompatible!");

        if (!_slotUsed[id])
            return false;

        auto &slot = _slots[id];
        if (slot.size != sizeof(T))
            return false;

        memcpy(&data, _buffer + slot.offset, sizeof(T));
        return true;
    }

    bool commit(uint32_t maxIntervalMs = 100, uint8_t maxWrites = 10); // Write data to Flash with a security mechanism
    bool wipe();                                                       // Reset Shadow RAM and erase files in Flash
    bool verify();                                                     // Check integrity between RAM and Flash
    bool isDirty() const { return _dirty; }                            // Check if there are any changes
    bool isLocked() const { return _isLocked; }                        // Check if commit is locked due to rate limit

    // Set how long to wait from dirty data until auto-commit
    void setAutoCommit(uint32_t ms) { _autoCommitMs = ms; } // If ms = 0, auto-commit is disabled
    void update();                                          // Engine function to manage auto-commit and rate limiting

    void hexDump(size_t bytesPerLine = 16); // print "Hex Mode"
    void debugSlots();                      // Print registered slot information (for debugging)
};

extern NeuEEPROM neuEEPROM;

#endif
