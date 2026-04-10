#include "NeuEEPROM.h"

NeuEEPROM::NeuEEPROM() {}

NeuEEPROM::~NeuEEPROM()
{
    if (_buffer)
        free(_buffer);
    SlotNode *current = _head;
    while (current)
    {
        SlotNode *next = current->next;
        free(current);
        current = next;
    }
}

/**
 * [_calculateChecksum] Byte-by-byte XOR logic for data integrity.
 * @return 1-byte checksum of all buffers.
 */
uint8_t NeuEEPROM::_calculateChecksum(uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i)
        crc ^= data[i];
    return crc;
}

/**
 * [_findSlot] Searches for a slot by ID.
 * @return Pointer to the slot if found, otherwise nullptr.
 */
NeuEEPROM::SlotNode *NeuEEPROM::_findSlot(uint8_t id)
{
    SlotNode *current = _head;
    while (current)
    {
        if (current->id == id)
            return current;
        current = current->next;
    }
    return nullptr;
}

/**
 * [begin] Initial setup: RAM allocation, Mount FS, & Validation of old data.
 * @return bool: True if RAM is ready & FS mounted successfully.
 */
bool NeuEEPROM::begin(size_t size, const char *path)
{
    _size = size;
    _path = path;

    // 1. RAM Management: Prevent memory leaks
    if (_buffer)
        free(_buffer);

    _buffer = (uint8_t *)malloc(_size);

    if (!_buffer)
        return false;

    memset(_buffer, 0xFF, _size);

    // 2. Mount Filesystem (Cross-Platform ESP32/ESP8266)
#if defined(ESP32)
    if (!LittleFS.begin(_autoFormat))
    {
        _reportError(ERR_FS_MOUNT); // Filesystem mount failed
        return false;
    }
#elif defined(ESP8266)
    if (!LittleFS.begin() && _autoFormat)
    {
        LittleFS.format();
        if (!LittleFS.begin())
        {
            _reportError(ERR_FS_MOUNT); // Filesystem mount failed
            return false;
        }
    }
#endif

    // 3. CLEANUP: Remove junk files if any
    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", _path);

    if (LittleFS.exists(tempPath))
        LittleFS.remove(tempPath);

    // 4. LOAD DATA: Retrieve original data from Flash
    if (LittleFS.exists(_path))
    {
        File f = LittleFS.open(_path, "r");
        if (f && f.size() == _size + 1) // Validation size: Data + 1 byte Checksum
        {
            f.read(_buffer, _size);
            uint8_t savedCrc = f.read();

            // Check Integrity with Checksum
            if (savedCrc == _calculateChecksum(_buffer, _size))
            {
                f.close();
                _dirty = false;
                return true;
            }

            _reportError(ERR_CRC_FAIL);
        }

        if (f)
            f.close();

        // If data is corrupt or the size is incorrect, perform a reset (Wipe)
        wipe();
    }
    return true;
}

/**
 * [registerSlot] Register a slot for data storage.
 * @return bool: True if the slot was registered successfully.
 */
bool NeuEEPROM::registerSlot(uint8_t id, size_t size)
{
    if (size == 0 || _findSlot(id))
        return false;

    uint16_t aligned = (size + 3) & ~3;
    if (_nextOffset + aligned > _size)
    {
        _reportError(ERR_BUFFER_OVERFLOW, id); // Buffer overflow
        return false;
    }

    SlotNode *newNode = (SlotNode *)malloc(sizeof(SlotNode));
    if (!newNode)
    {
        _reportError(ERR_MALLOC_FAIL, id); // Memory allocation failed
        return false;
    }

    newNode->id = id;
    newNode->offset = _nextOffset;
    newNode->size = (uint16_t)size;
    newNode->next = _head;
    _head = newNode;

    _nextOffset += aligned;
    return true;
}

/**
 * [commit] ATOMIC SWAP: Write the temp file first then rename it to original (Safe Write).
 * @return bool: True if the commit was successful.
 */
bool NeuEEPROM::commit(uint32_t maxIntervalMs, uint8_t maxWrites)
{
    // 1. CHECK REDUNDANCE: If the data is the same as in Flash, skip the write to preserve flash life
    if (!_dirty)
    {
        if (++_sameDataCount >= 5)
        {
            _isLocked = true;
            _reportError(ERR_FLASH_LOCKED);
        }
        return true;
    }

    // 2. CHECK LOCK STATUS: If it was previously locked
    if (_isLocked)
    {
        _reportError(ERR_FLASH_LOCKED);
        return false;
    }

    // 3. RATE LIMITER LOGIC: Prevents fast write spam
    uint32_t now = millis();
    if (now - _lastCheckTime < maxIntervalMs)
    {
        if (++_writeCount > maxWrites)
        {
            _isLocked = true;
            _reportError(ERR_FLASH_SPAM);
            return false;
        }
    }
    else
    {
        // Reset the write after each time window expires
        _writeCount = 1;
        _lastCheckTime = now;
    }

    // 4. ATOMIC SWAP: Processes writes to a temporary file
    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", _path);

    File f = LittleFS.open(tempPath, "w");
    if (!f)
    {
        _reportError(ERR_ATOMIC_SWAP);
        return false;
    }

    f.write(_buffer, _size);
    f.write(_calculateChecksum(_buffer, _size));
    f.close();

    // 5. SWAP FILE: Remove the old one and replace it with the new one
    LittleFS.remove(_path);
    if (LittleFS.rename(tempPath, _path))
    {
        _dirty = false;            // Reset dirty flag
        _dirtyTimer = 0;           // Reset timer for auto-commit
        _sameDataCount = 0;        // Reset redundant write counter
        _lastCheckTime = millis(); // Update the last successful time
        return true;
    }

    _reportError(ERR_ATOMIC_SWAP);
    return false;
}

/**
 * [verify] Compares the contents of Shadow RAM vs Physical Files in Flash.
 * @return bool: True if RAM and Flash are 100% in sync (Including Checksum).
 */
bool NeuEEPROM::verify()
{
    if (!_buffer || !LittleFS.exists(_path))
        return false; // File not found

    File f = LittleFS.open(_path, "r");

    // Check file validity
    if (!f || f.size() != _size + 1)
    {
        if (f)
            f.close();

        _reportError(ERR_CRC_FAIL); // File size mismatch, corrupted indicates
        return false;
    }

    // Compare data RAM vs Flash
    uint8_t temp[64];
    size_t bytesRead = 0;

    while (bytesRead < _size)
    {
        size_t toRead = std::min((size_t)sizeof(temp), _size - bytesRead);
        f.read(temp, toRead);

        if (memcmp(temp, _buffer + bytesRead, toRead) != 0)
        {
            f.close();
            return false; // Data mismatch? RAM and Flash are not in sync
        }

        bytesRead += toRead;
    }

    bool ok = (f.read() == _calculateChecksum(_buffer, _size));
    f.close();

    if (!ok) // Checksum in Flash mismatch
        _reportError(ERR_CRC_FAIL);

    return ok;
}

/**
 * [wipe] Reset Shadow RAM to 0xFF & erase the physical files in Flash.
 * @return bool: True if the reset was successful with no residue.
 */
bool NeuEEPROM::wipe()
{
    if (_buffer)
        memset(_buffer, 0xFF, _size);

    _dirty = true; // Mark for the next commit to write clean data

    if (LittleFS.exists(_path))
        return LittleFS.remove(_path);

    return true;
}

/**
 * [update] The main engine for managing auto-commit and rate limiting.
 */
void NeuEEPROM::update()
{
    if (!_dirty || _autoCommitMs == 0 || _isLocked)
        return;

    if (millis() - _dirtyTimer >= _autoCommitMs)
    {
        if (commit())
            _dirtyTimer = 0;
    }
}

/**
 * [hexDump] Prints the contents of the EEPROM in a hex format for debugging.
 * Pro-tip: Left line = Offset, Middle = Hex, Right = ASCII.
 */
void NeuEEPROM::hexDump(size_t bytesPerLine)
{
    if (!_buffer)
        return;

    Serial.println(F("\n=== NeuEEPROM HexDump ==="));

    for (size_t i = 0; i < _size; i += bytesPerLine)
    {
        Serial.printf("%04X: ", (int)i);
        for (size_t j = 0; j < bytesPerLine; j++)
        {
            if (i + j < _size)
                Serial.printf("%02X ", _buffer[i + j]);
            else
                Serial.print(F("   "));
        }
        Serial.print(F("| "));
        for (size_t j = 0; j < bytesPerLine; j++)
        {
            if (i + j < _size)
            {
                char c = _buffer[i + j];
                // Only display readable characters (Printable ASCII)
                Serial.print((c >= 32 && c <= 126) ? c : '.');
            }
        }
        Serial.println();
    }
}

/**
 * [debugSlots] Prints the slot map for debugging purposes.
 */
void NeuEEPROM::debugSlots()
{
    Serial.println(F("=== SLOT MAP ==="));
    SlotNode *c = _head;
    while (c)
    {
        Serial.printf("ID %d -> Offset: %d, Size: %d\n", c->id, c->offset, c->size);
        c = c->next;
    }
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuEEPROM neuEEPROM;
#endif
