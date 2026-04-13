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
  * @brief Calculates the checksum value for data integrity verification
  *
  * Performs a simple XOR operation across all bytes in the buffer.
  * The result is used to validate whether the data is intact and
  * has not been corrupted or modified.
  *
  * @param data Pointer to the data buffer
  * @param len Length of the data in bytes
  * @return 1-byte checksum result
  */
uint8_t NeuEEPROM::_calculateChecksum(uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i)
        crc ^= data[i];
    return crc;
}

/**
  * @brief Searches for a slot in the linked list by its ID
  *
  * Traverses the slot list and looks for an entry matching the given ID.
  * Used internally by get/put operations to locate the correct memory position.
  *
  * @param id Slot ID number to find
  * @return Pointer to the SlotNode if found, nullptr if not exists
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
  * @brief Initializes the NeuEEPROM system and prepares all resources
  *
  * This function allocates the main Shadow RAM buffer, mounts the file system,
  * and loads existing data from Flash. It automatically handles first-time
  * initialization, file creation, and recovery from corrupted data.
  *
  * Memory size is automatically adjusted to 4-byte alignment for
  * optimal hardware performance and safety.
  *
  * @param size Total storage size required (default: 512 bytes)
  * @param path File path in LittleFS where data is stored (default: "/neu_eeprom.bin")
  * @return True if system ready and healthy, false if initialization failed
  */
bool NeuEEPROM::begin(size_t size, const char *path)
{
    if (size % 4 != 0)
        size += (4 - (size % 4));

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

    memset(_buffer, 0x00, _size);

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
            f.read(_buffer, _size);                   // Read data (encrypt)
            f.read((uint8_t *)&_totalWriteCycles, 4); // Read count
            uint8_t savedCrc = f.read();              // Read CRC

            // Decrypt data if use
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
  * @brief Permanently erases all data and resets the entire system state
  *
  * This function performs a complete factory reset by deleting the storage file
  * and reinitializing the memory with clean blank data. It also resets the
  * write cycle counter and health monitor to zero.
  *
  * @warning This operation is irreversible and will DELETE ALL DATA saved in Flash!
  *
  * @note For safety and security reasons, this function ONLY works if called
  *       within the first 5 seconds after power-up or reboot. After this
  *       time window, the operation will be automatically rejected.
  *
  * @return True if data was successfully wiped, false if timed out or failed
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
  * @brief Registers a new data slot with unique ID and specified size
  *
  * Allocates a reserved memory area within the buffer to hold a specific data structure.
  * The system searches the Free List and assigns the first suitable empty space.
  * If no existing space fits, it will extend the allocation from the end of the pool.
  *
  * Memory allocation is automatically aligned to 4-byte boundary
  * for optimal hardware performance and data alignment.
  *
  * @param id Unique identifier number for this slot (1..255)
  * @param size Size of the data in bytes
  * @return True if registration successful, false if out of memory or ID exists
  */
bool NeuEEPROM::registerSlot(uint8_t id, size_t size)
{
    if (size == 0 || _findSlot(id))
        return false;

    uint16_t aligned = (size + 3) & ~3;

    // Find empty space that is big enough
    FreeNode *prev = nullptr;
    FreeNode *curr = _freeHead;
    while (curr)
    {
        if (aligned <= curr->size)
        {
            // Use the free space
            SlotNode *newNode = (SlotNode *)malloc(sizeof(SlotNode));
            if (!newNode)
            {
                _reportError(ERR_MALLOC_FAIL, id);
                return false;
            }

            newNode->id = id;
            newNode->offset = curr->offset;
            newNode->size = (uint16_t)size;
            newNode->next = _head;
            _head = newNode;

            // Remove the free space
            if (prev)
                prev->next = curr->next;
            else
                _freeHead = curr->next;
            free(curr);

            return true;
        }
        prev = curr;
        curr = curr->next;
    }

    // No free space, create a new slot
    if (_nextOffset + aligned > _size)
    {
        _reportError(ERR_BUFFER_OVERFLOW, id);
        return false;
    }

    SlotNode *newNode = (SlotNode *)malloc(sizeof(SlotNode));
    if (!newNode)
    {
        _reportError(ERR_MALLOC_FAIL, id);
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
  * @brief Removes a registered slot and frees its memory space
  *
  * This function deletes the slot entry from the linked list and
  * adds its occupied area back into the Free List pool.
  * The memory space becomes available again for new slot allocations.
  *
  * After removal, it is recommended to call @ref compactFreeList()
  * to merge adjacent empty areas for optimal memory usage.
  *
  * @param id Unique ID number of the slot to be removed
  * @return True if slot removed successfully, false if slot not found
  */
bool NeuEEPROM::removeSlot(uint8_t id)
{
    SlotNode *prev = nullptr;
    SlotNode *curr = _head;

    while (curr)
    {
        if (curr->id == id)
        {
            // Add to free list
            FreeNode *f = (FreeNode *)malloc(sizeof(FreeNode));
            if (!f)
            {
                _reportError(ERR_MALLOC_FAIL, id);
                return false;
            }
            f->offset = curr->offset;
            f->size = (curr->size + 3) & ~3; // align
            f->next = _freeHead;
            _freeHead = f;

            // Remove from slot list
            if (prev)
                prev->next = curr->next;
            else
                _head = curr->next;
            free(curr);

            _dirty = true; // Mark dirty
            return true;
        }
        prev = curr;
        curr = curr->next;
    }

    _reportError(ERR_NOT_REGISTERED, id);
    return false;
}

/**
* @brief Merges contiguous blocks of free memory into one large block
*
* This function performs two important steps:
* 1. Sorts the list of free space by offset address
* 2. Checks for and merges adjacent free blocks
* into one larger contiguous area.
*
* This is to maintain efficient memory management, prevent fragmentation,
* and maximize the available free space for new slot allocations.
*/
void NeuEEPROM::_mergeFreeList() {
    if (!_freeHead) return;

    // Sort free list by offset (bubble sort sederhana)
    bool swapped;
    do {
        swapped = false;
        FreeNode *curr = _freeHead;
        while (curr && curr->next) {
            if (curr->offset > curr->next->offset) {
                uint16_t tmpOff = curr->offset;
                uint16_t tmpSize = curr->size;
                curr->offset = curr->next->offset;
                curr->size = curr->next->size;
                curr->next->offset = tmpOff;
                curr->next->size = tmpSize;
                swapped = true;
            }
            curr = curr->next;
        }
    } while (swapped);

    // Merge adjacent
    FreeNode *curr = _freeHead;
    while (curr && curr->next) {
        if (curr->offset + curr->size == curr->next->offset) {
            curr->size += curr->next->size;
            FreeNode *toDel = curr->next;
            curr->next = toDel->next;
            free(toDel);
        } else {
            curr = curr->next;
        }
    }
}

/**
  * @brief Saves changes from Shadow RAM to physical Flash permanently
  *
  * This function performs a safe and intelligent write operation using the
  * Atomic Swap method. Data is first written to a temporary file (.tmp),
  * validated, and then replaces the original file only if everything is correct.
  *
  * It also includes built-in protection mechanisms:
  * - Skip writing if data has not changed (@ref Smart Write)
  * - Rate limiting to prevent flash abuse
  * - Automatic Lockdown if spam is detected
  * - Health monitoring to track flash wear level
  *
  * @param maxIntervalMs Minimum time gap required between writes (default: 1000ms)
  * @param maxWrites Maximum allowed write attempts within the interval (default: 10)
  * @return True if commit successful, False if error or locked
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

    if (_totalWriteCycles > 100000)
        _totalWriteCycles = 100000;

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
  * @brief Checks if Shadow RAM data matches the physical data in Flash
  *
  * This function performs a byte-to-byte comparison between the data
  * currently held in RAM and the data physically stored on the file system.
  * It also validates the integrity using the Checksum/CRC value.
  *
  * If encryption is enabled, data will be automatically decoded
  * during the comparison process.
  *
  * @return True if data is 100% identical and valid, false if mismatch or corrupt
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
  * @brief Resets all data to empty state and creates a fresh blank file
  *
  * This function clears the Shadow RAM buffer with zeros and
  * writes a new clean file directly to Flash. The write cycle counter
  * is reset, and the checksum is recalculated for the empty state.
  *
  * It effectively restores the storage to its initial factory condition
  * without removing the slot configuration structure.
  *
  * @warning: THIS OPERATION WILL PERMANENTLY ERASE ALL DATA!
  *          All saved values, settings, and history will be lost immediately.
  *          Use with extreme caution!
  *
  * @return True if wipe operation successful, false if file system error
  */
bool NeuEEPROM::wipe()
{
    // 1. Reset Shadow RAM
    _totalWriteCycles = 0;
    if (_buffer)
        memset(_buffer, 0x00, _size);

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
  * @brief Main background task handler for automatic operations
  *
  * This function should be called regularly inside the loop() function.
  * It handles the Auto-Commit logic by checking if there are pending
  * changes and whether the configured delay time has passed.
  *
  * It also serves as the heartbeat for the rate limiter and
  * protection systems to work correctly.
  *
  * @note Must be called periodically in loop() for auto-save to work
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
  * @brief Prints the complete memory contents in Hex and ASCII format
  *
  * Displays a visual representation of the Shadow RAM buffer.
  * The output is divided into three columns:
  *  - Left: Memory address offset
  *  - Middle: Byte values in Hexadecimal
  *  - Right: Human-readable ASCII characters
  *
  * Free memory areas are specially marked as "--" for easy identification.
  * This is extremely useful for debugging, reverse engineering,
  * or checking raw data integrity.
  *
  * @param bytesPerLine Number of bytes to display per row (default: 16)
  */
void NeuEEPROM::hexDump(size_t bytesPerLine)
{
    if (!_buffer)
        return;

    Serial.println(F("\n=== NeuEEPROM HexDump ==="));

    for (size_t i = 0; i < _size; i += bytesPerLine)
    {
        Serial.printf("%04X: ", (int)i);

        // Hex
        for (size_t j = 0; j < bytesPerLine; j++)
        {
            if (i + j < _size)
            {
                bool isFree = false;
                FreeNode *f = _freeHead;
                while (f)
                {
                    if ((i + j) >= f->offset && (i + j) < f->offset + f->size)
                    {
                        isFree = true;
                        break;
                    }
                    f = f->next;
                }

                if (isFree)
                    Serial.print(F("-- ")); // special symbol for free space
                else
                    Serial.printf("%02X ", _buffer[i + j]);
            }
            else
                Serial.print(F("   "));
        }

        Serial.print(F("| "));

        // ASCII
        for (size_t j = 0; j < bytesPerLine; j++)
        {
            if (i + j < _size)
            {
                bool isFree = false;
                FreeNode *f = _freeHead;
                while (f)
                {
                    if ((i + j) >= f->offset && (i + j) < f->offset + f->size)
                    {
                        isFree = true;
                        break;
                    }
                    f = f->next;
                }

                if (isFree)
                    Serial.print('-'); // special symbol for free space
                else
                {
                    char c = _buffer[i + j];
                    Serial.print((c >= 32 && c <= 126) ? c : '.');
                }
            }
        }
        Serial.println();
    }
}

/**
  * @brief Displays the complete memory allocation map for debugging
  *
  * Prints the list of all registered slots showing their IDs,
  * memory offsets, and sizes. It also displays the Free List
  * showing which areas are currently empty and available.
  *
  * This is extremely helpful to visualize how memory is fragmented,
  * check alignment, and verify the internal structure of the storage.
  *
  * Output is printed directly to the Serial Monitor.
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

    Serial.println(F("=== FREE LIST ==="));
    FreeNode *f = _freeHead;
    if (!f)
        Serial.println(F("(no free slots)"));

    while (f)
    {
        Serial.printf("Free -> Offset: %d, Size: %d\n", f->offset, f->size);
        f = f->next;
    }
}

/**
  * @brief Gets the remaining lifespan percentage of the Flash memory
  *
  * Calculates the health status based on how many times the data
  * has been written since the first use. Flash memory has a limited
  * number of write cycles (~100,000 times).
  *
  * The value ranges from 100.0% (brand new / fresh) down to 0.0%
  * when the maximum limit is reached.
  *
  * @return Health percentage from 0.0% to 100.0%
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

/**
  * @brief Exports the entire storage file as raw binary data
  *
  * Streams the complete physical file content (including data,
  * write counter, and checksum) to any output Stream object.
  *
  * This can be used to create a full backup, transfer settings
  * to another device, or save the image via Serial/Network.
  *
  * @param dest Destination stream (Serial, File, Client, etc.)
  */
void NeuEEPROM::exportData(Stream &dest)
{
    if (!LittleFS.exists(_path))
        return;

    File f = LittleFS.open(_path, "r");
    if (!f)
        return;

    uint8_t buffer[64]; // Use a small buffer to save RAM

    while (f.available())
    {
        size_t n = f.read(buffer, sizeof(buffer));
        dest.write(buffer, n);
    }

    f.close();
}

/**
 * @brief Receives binary data from a Stream and restores it to Flash.
 * Re-initializes the library after a successful import.
 * Always ensure the same encryption key is set on the destination device before calling importData,
 * otherwise the data will fail the CRC check and be wiped for safety.
 *
 * @param src The source Stream (e.g., Serial or WiFiClient)
 * @param timeoutMs Maximum time to wait for data (default 5000ms)
 */
bool NeuEEPROM::importData(Stream &src, uint32_t timeoutMs)
{
    size_t expectedLen = _size + 4 + 1; // Size standar NeuEEPROM v1.3+

    // Use temporary files for security
    char tempPath[64];
    snprintf(tempPath, sizeof(tempPath), "%s.imp", _path);

    File f = LittleFS.open(tempPath, "w");
    if (!f)
    {
        _reportError(ERR_ATOMIC_SWAP);
        return false;
    }

    size_t totalReceived = 0;
    uint32_t startWait = millis();

    // Data receiving loop with Timeout
    while (totalReceived < expectedLen)
    {
        if (src.available())
        {
            f.write(src.read());
            totalReceived++;
            startWait = millis(); // Reset timeout every time data is entered
        }
        else
        {
            if (millis() - startWait > timeoutMs)
            {
                f.close();
                LittleFS.remove(tempPath);
                return false; // Failed due to data disconnection
            }
            delay(1); // Give breath to the system
        }
    }
    f.close();

    // Validate newly entered files
    if (totalReceived == expectedLen)
    {
        LittleFS.remove(_path);
        if (LittleFS.rename(tempPath, _path))
        {
            // Load new data into Shadow RAM
            return begin(_size, _path);
        }
    }

    LittleFS.remove(tempPath);
    return false;
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuEEPROM neuEEPROM;
#endif
