#include "NeuEEPROM.h"
#include "NeuCipher.h"

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
    _startTime = millis();
    _totalWriteCycles = 0;

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
        if (f && f.size() == _size + 4 + 1) // Validation size: Data + 4 bytes Header + 1 byte Checksum
        {
            f.read(_buffer, _size);                   // Baca data (masih terenkripsi)
            f.read((uint8_t *)&_totalWriteCycles, 4); // Baca counter
            uint8_t savedCrc = f.read();              // Baca CRC

            // [BARU] Dekripsi data sebelum cek integritas
            if (_encKey && _encKeyLen > 0)
                NeuCipher::process(_buffer, _size, _encKey, _encKeyLen);

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
 * [masterClear] Master Clear: Erase all data & reset the total write cycles.
 * @return bool: True if the master clear was successful.
 * WARNING: This function will erase all data and reset the total write cycles.
 * Can only be called within the first 5 seconds after startup.
 */
bool NeuEEPROM::masterClear()
{
    // Check if it has been more than 5 seconds (5000 ms)
    if (millis() - _startTime > 5000)
    {
        Serial.println(F("\nMaster Clear rejected: Time limit has passed 5 seconds."));
        return false;
    }

    Serial.println(F("\nMaster Clear approved: Deleting data..."));
    _totalWriteCycles = 0;

    if (LittleFS.exists(_path))
        LittleFS.remove(_path);

    return wipe();
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
    // 1. & 2. CHECK REDUNDANCE & LOCK STATUS
    if (!_dirty)
    {
        if (++_sameDataCount >= 10)
        {
            _isLocked = true;
            _reportError(ERR_FLASH_LOCKED);
        }
        return true;
    }
    if (_isLocked)
    {
        _reportError(ERR_FLASH_LOCKED);
        return false;
    }

    // 3. RATE LIMITER LOGIC
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
        _writeCount = 1;
        _lastCheckTime = now;
    }

    // 4. PREPARE DATA & ENCRYPTION
    uint8_t finalCrc = _calculateChecksum(_buffer, _size);
    size_t expectedSize = _size + 4 + 1; // Total size to be written
    size_t actualWritten = 0;

    if (_encKey && _encKeyLen > 0)
        NeuCipher::process(_buffer, _size, _encKey, _encKeyLen);

    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", _path);

    File f = LittleFS.open(tempPath, "w");
    if (!f)
    {
        if (_encKey && _encKeyLen > 0)
            NeuCipher::process(_buffer, _size, _encKey, _encKeyLen);
        _reportError(ERR_ATOMIC_SWAP);
        return false;
    }

    // Count the accumulated bytes actually written.
    actualWritten += f.write(_buffer, _size);

    _totalWriteCycles++;
    actualWritten += f.write((uint8_t *)&_totalWriteCycles, 4);

    if (_totalWriteCycles >= 90000)
        _reportError(ERR_HEALTH_LOW, 0);

    actualWritten += f.write(finalCrc);
    f.close();

    // REVERSE DECRYPTION
    if (_encKey && _encKeyLen > 0)
        NeuCipher::process(_buffer, _size, _encKey, _encKeyLen);

    // [VALIDATION] Check if all bytes are written correctly
    if (actualWritten != expectedSize)
    {
        LittleFS.remove(tempPath); // Delete corrupted/damaged files
        _reportError(ERR_ATOMIC_SWAP);
        return false;
    }

    // 5. SWAP FILE
    LittleFS.remove(_path);
    if (LittleFS.rename(tempPath, _path))
    {
        _dirty = false;
        _dirtyTimer = 0;
        _sameDataCount = 0;
        _lastCheckTime = millis();
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
    if (!f || f.size() != _size + 4 + 1)
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

        if (_encKey && _encKeyLen > 0)
        {
            // We use bytesRead offset to sync with key rotation
            for (size_t i = 0; i < toRead; i++)
                temp[i] ^= _encKey[(bytesRead + i) % _encKeyLen];
        }

        if (memcmp(temp, _buffer + bytesRead, toRead) != 0)
        {
            f.close();
            return false; // Data mismatch? RAM and Flash are not in sync
        }

        bytesRead += toRead;
    }

    f.seek(_size + 4); // Seek to CRC
    bool ok = (f.read() == _calculateChecksum(_buffer, _size));
    f.close();

    if (!ok) // Checksum in Flash mismatch
        _reportError(ERR_CRC_FAIL);

    return ok;
}

/**
 * [wipe] Reset Shadow RAM to 0xFF & erase the physical files in Flash.
 * @return bool: True if the reset was successful with no residue.
 * WARNING: This function will erase all data and reset the total write cycles.
 */
bool NeuEEPROM::wipe()
{
    // 1. Reset Shadow RAM
    _totalWriteCycles = 0;
    if (_buffer)
        memset(_buffer, 0xFF, _size);

    _dirty = false;

    // 2. Data + 4 byte Counter + 1 byte CRC (Create if not exists)
    File f = LittleFS.open(_path, "w");
    if (!f)
    {
        _reportError(ERR_FS_MOUNT);
        return false;
    }

    // Write data 0xFF to flash
    f.write(_buffer, _size);

    // Write counter
    uint32_t zeroCounter = 0;
    f.write((uint8_t *)&zeroCounter, 4);

    // Write CRC
    uint8_t initialCrc = _calculateChecksum(_buffer, _size);
    f.write(initialCrc);

    f.close();
    return true;
}

/**
 * [update] The main engine for managing auto-commit and rate limiting.
 * @note This function should be called in the loop() function.
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

/**
 * [getHealth] Calculates the health of the flash chip based on the number of write cycles.
 * @return float: 0.0 to 100.0
 */
float NeuEEPROM::getHealth()
{
    const uint32_t MAX_CYCLES = 100000; // Maximum number of write cycles

    if (_totalWriteCycles >= MAX_CYCLES)
        return 0.0f; // Flash chip is dead

    // Calculate health
    float health = 100.0f - (((float)_totalWriteCycles / MAX_CYCLES) * 100.0f);

    return (health < 0) ? 0.0f : health;
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuEEPROM neuEEPROM;
#endif
