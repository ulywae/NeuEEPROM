#include "NeuEEPROM.h"

// [Constructor] Initialize variables to prevent them from running wild in memory
NeuEEPROM::NeuEEPROM() : _buffer(nullptr), _size(0), _path(nullptr), _dirty(false) {}

// [Destructor] Ensure Shadow RAM is cleared when an object is destroyed (Anti-Leak)
NeuEEPROM::~NeuEEPROM()
{
    if (_buffer)
        free(_buffer);
}

/**
* [_calculateChecksum] Byte-by-byte XOR logic for data integrity.
* @return 1-byte checksum of all buffers.
*/
uint8_t NeuEEPROM::_calculateChecksum(uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];

        for (uint8_t b = 0; b < 8; b++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }

    return crc;
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
        Serial.println("[NeuEEPROM] Mount Failed.");
        return false;
    }
#elif defined(ESP8266)
    bool success = LittleFS.begin();
    if (!success && _autoFormat)
    {
        Serial.println("[NeuEEPROM] Mount Failed. Formatting...");
        LittleFS.format();
        success = LittleFS.begin();
    }
    if (!success)
        return false;
#endif

    // 3. CLEANUP: Remove junk files if any
    const char *tempPath = "/neu_eeprom.tmp";
    if (LittleFS.exists(tempPath))
    {
        LittleFS.remove(tempPath);
        Serial.println("[NeuEEPROM] Cleanup: Temporary file deleted.");
    }

    // 4. LOAD DATA: Retrieve original data from Flash
    if (LittleFS.exists(_path))
    {
        File f = LittleFS.open(_path, "r");
        if (f)
        {
            // Validation size: Data + 1 byte Checksum
            if (f.size() == _size + 1)
            {
                f.read(_buffer, _size);
                uint8_t savedCrc = f.read();
                f.close();

                // Check Integrity with Checksum
                if (savedCrc == _calculateChecksum(_buffer, _size))
                {
                    Serial.println("[NeuEEPROM] Integrity OK. Data Loaded.");
                    _dirty = false;
                    return true;
                }
                Serial.println("[NeuEEPROM] CRC Error! Data Corrupt.");
            }
            else
            {
                Serial.println("[NeuEEPROM] Size Mismatch! Data ignored.");
                f.close();
            }

            // If data is corrupt or the size is incorrect, perform a reset (Wipe)
            wipe();
        }
    }
    else
        Serial.println("[NeuEEPROM] No existing data. Starting fresh.");

    return true;
}

/**
* [registerSlot] Register a slot for data storage.
* @return bool: True if the slot was registered successfully. 
*/
bool NeuEEPROM::registerSlot(uint8_t id, size_t size)
{
    if (size == 0)
        return false;

    if (_slotUsed[id])
        return false;

    uint16_t aligned = (size + 3) & ~3;
    if (_nextOffset + aligned > _size)
        return false;

    _slots[id] = {_nextOffset, aligned};

    // align 4 byte
    _nextOffset += (size + 3) & ~3;

    _slotUsed[id] = true;
    return true;
}

/** 
* [commit] ATOMIC SWAP: Write the temp file first then rename it to original (Safe Write). 
* @return bool: True if the commit was successful.
*/
bool NeuEEPROM::commit(uint32_t maxIntervalMs, uint8_t maxWrites)
{
    // 1. CHECK REDUNDANCE: If the data is the same as in Flash, don't torture the hardware
    if (!_dirty)
    {
        _sameDataCount++;
        if (_sameDataCount >= 5)
        {
            _isLocked = true;
            Serial.println(F("[NeuEEPROM] LOCKDOWN: Persistent commit calls with NO changes!"));
            return false;
        }
        return true; // Silently ignore
    }

    // 2. CHECK LOCK STATUS: If it was previously locked
    if (_isLocked)
    {
        Serial.println(F("[NeuEEPROM] CRITICAL: System is Locked! Fix your logic and Reboot."));
        return false;
    }

    // 3. RATE LIMITER LOGIC: Prevents fast write spam
    uint32_t now = millis();
    if (now - _lastCheckTime < maxIntervalMs)
    {
        _writeCount++;
        if (_writeCount > maxWrites)
        {
            _isLocked = true;
            Serial.println(F("[NeuEEPROM] SECURITY: Write SPAM detected! Flash Protection Active."));
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
    const char *tempPath = "/neu_eeprom.tmp";
    File f = LittleFS.open(tempPath, "w");
    if (!f)
    {
        Serial.println(F("[NeuEEPROM] Error: Could not create temp file."));
        return false;
    }

    f.write(_buffer, _size);
    f.write(_calculateChecksum(_buffer, _size));
    f.close();

    // 5. SWAP FILE: Remove the old one and replace it with the new one
    if (LittleFS.exists(_path))
        LittleFS.remove(_path);

    if (LittleFS.rename(tempPath, _path))
    {
        // RESET ALL FLAGS AFTER SUCCESS
        _dirty = false;       // Reset the dirty flag
        _dirtyTimer = 0;      // Reset the timer
        _sameDataCount = 0;   // Reset the redundant
        _writeCount = 0;      // Reset the write
        _lastCheckTime = now; // Update the last successful time

        LittleFS.remove(tempPath); // Ensure the temp file is clean
        Serial.println(F("[NeuEEPROM] Safe Commit Success. RAM & Flash Synced."));
        return true;
    }

    Serial.println(F("[NeuEEPROM] Error: Rename failed. Data not saved."));
    return false;
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
* [hexDump] "Hacker Mode" view to peek inside Shadow RAM.
* Pro-tip: Left line = Offset, Middle = Hex, Right = ASCII.
*/
void NeuEEPROM::hexDump(size_t bytesPerLine)
{
    if (!_buffer)
        return;

    Serial.println("\n=== NeuEEPROM HexDump (NEU System) ===");

    for (size_t i = 0; i < _size; i += bytesPerLine)
    {
        char buf[10];
        sprintf(buf, "%04X: ", (int)i);
        Serial.print(buf);
        for (size_t j = 0; j < bytesPerLine; j++)
        {
            if (i + j < _size)
            {
                sprintf(buf, "%02X ", _buffer[i + j]);
                Serial.print(buf);
            }
            else
                Serial.print("   ");
        }
        Serial.print("| ");
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
* [debugSlots] Print registered slot information (for debugging).
*/
void NeuEEPROM::debugSlots()
{
    Serial.println("=== SLOT MAP ===");
    for (int i = 0; i < 256; i++)
    {
        if (_slotUsed[i])
        {
            Serial.printf("ID %d -> offset: %d, size: %d\n",
                          i, _slots[i].offset, _slots[i].size);
        }
    }
}

/** 
* [verify] Compares the contents of Shadow RAM vs Physical Files in Flash (Byte by Byte). 
* @return bool: True if RAM and Flash are 100% in sync (Including Checksum). 
*/
bool NeuEEPROM::verify()
{
    if (!_buffer || !LittleFS.exists(_path))
        return false;

    File f = LittleFS.open(_path, "r");
    if (!f)
        return false;

    // 1. Check the file size first (Data + 1 byte Checksum)
    if (f.size() != _size + 1)
    {
        f.close();
        return false;
    }

    // 2. Read data from Flash and compare it directly to RAM
    bool match = true;
    for (size_t i = 0; i < _size; i++)
    {
        if (f.read() != _buffer[i])
        {
            match = false;
            break;
        }
    }

    // 3. Check if the Checksum is still valid
    if (match)
    {
        uint8_t savedCrc = f.read();
        if (savedCrc != _calculateChecksum(_buffer, _size))
            match = false;
    }

    f.close();

    if (match)
        Serial.println("[NeuEEPROM] Verification Passed. RAM & Flash are in Sync.");
    else
        Serial.println("[NeuEEPROM] Verification Failed! Inconsistency detected.");

    return match;
}

/**
* [update] The main engine for managing auto-commit and rate limiting.
*/
void NeuEEPROM::update()
{
    if (!_dirty || _autoCommitMs == 0 || _isLocked)
        return;

    // If it has just become dirty, record the time.
    if (_dirtyTimer == 0)
    {
        _dirtyTimer = millis();
        return;
    }

    // If it has "settled" for autoCommitMs, then execute!
    if (millis() - _dirtyTimer >= _autoCommitMs)
    {
        Serial.println(F("[NeuEEPROM] Engine: Auto-committing changes..."));
        if (commit())
            _dirtyTimer = 0; // Reset timer after success
    }
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuEEPROM neuEEPROM;
#endif
