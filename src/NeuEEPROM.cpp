#include "NeuEEPROM.h"

// [Constructor] Inisialisasi awal variabel agar tidak "liar" di memori
NeuEEPROM::NeuEEPROM() : _buffer(nullptr), _size(0), _path(nullptr), _dirty(false) {}

// [Destructor] Pastikan Shadow RAM dibersihkan saat objek hancur (Anti-Leak)
NeuEEPROM::~NeuEEPROM()
{
    if (_buffer)
        free(_buffer);
}

/**
 * [_calculateChecksum] Logika XOR per byte untuk integritas data.
 * @return 1-byte checksum hasil akumulasi seluruh buffer.
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
                crc = (crc << 1) ^ 0x07; // polynomial CRC-8
            else
                crc <<= 1;
        }
    }

    return crc;
}

/**
 * [begin] Setup awal: Alokasi RAM, Mount FS, & Validasi data lama.
 * @return bool: True jika RAM siap & FS berhasil dimount.
 */
bool NeuEEPROM::begin(size_t size, const char *path)
{
    _size = size;
    _path = path;

    // 1. Managemen RAM: Cegah kebocoran memori
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

    // 3. CLEANUP: Hapus file sampah jika ada
    const char *tempPath = "/neu_eeprom.tmp";
    if (LittleFS.exists(tempPath))
    {
        LittleFS.remove(tempPath);
        Serial.println("[NeuEEPROM] Cleanup: Temporary file deleted.");
    }

    // 4. LOAD DATA: Ambil data asli dari Flash
    if (LittleFS.exists(_path))
    {
        File f = LittleFS.open(_path, "r");
        if (f)
        {
            // Validasi ukuran: Data + 1 byte Checksum
            if (f.size() == _size + 1)
            {
                f.read(_buffer, _size);
                uint8_t savedCrc = f.read();
                f.close();

                // Cek Integritas dengan Checksum
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

            // Jika data korup atau ukuran salah, lakukan reset (Wipe)
            wipe();
        }
    }
    else
        Serial.println("[NeuEEPROM] No existing data. Starting fresh.");

    return true;
}

/**
 * [registerSlot] Daftarkan slot untuk penyimpanan data.
 * @return bool: True jika slot berhasil didaftarkan.
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
 * [commit] ATOMIC SWAP: Tulis file temp dulu baru rename ke asli (Safe Write).
 * @return bool: True jika commit berhasil.
 */
bool NeuEEPROM::commit(uint32_t maxIntervalMs, uint8_t maxWrites)
{
    // 1. CEK REDUNDANSI: Jika data sama dengan di Flash, jangan nyiksa hardware
    if (!_dirty)
    {
        _sameDataCount++;
        if (_sameDataCount >= 5)
        {
            _isLocked = true;
            Serial.println(F("[NeuEEPROM] LOCKDOWN: Persistent commit calls with NO changes!"));
            return false;
        }
        return true; // Abaikan diam-diam
    }

    // 2. CEK STATUS LOCK: Jika sudah terblokir sebelumnya
    if (_isLocked)
    {
        Serial.println(F("[NeuEEPROM] CRITICAL: System is Locked! Fix your logic and Reboot."));
        return false;
    }

    // 3. LOGIKA RATE LIMITER: Mencegah spam penulisan cepat
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
        // Reset jatah tulis setiap jendela waktu berakhir
        _writeCount = 1;
        _lastCheckTime = now;
    }

    // 4. ATOMIC SWAP: Proses tulis ke file sementara
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

    // 5. SWAP FILE: Hapus yang lama, ganti dengan yang baru
    if (LittleFS.exists(_path))
        LittleFS.remove(_path);

    if (LittleFS.rename(tempPath, _path))
    {
        // RESET SEMUA FLAG SETELAH SUKSES
        _dirty = false;       // Reset flag dirty
        _dirtyTimer = 0;      // Reset timer
        _sameDataCount = 0;   // Reset jatah redundant
        _writeCount = 0;      // Reset jatah tulis
        _lastCheckTime = now; // Update waktu sukses terakhir

        LittleFS.remove(tempPath); // Pastikan file temp bersih
        Serial.println(F("[NeuEEPROM] Safe Commit Success. RAM & Flash Synced."));
        return true;
    }

    Serial.println(F("[NeuEEPROM] Error: Rename failed. Data not saved."));
    return false;
}

/**
 * [wipe] Reset Shadow RAM ke 0xFF & hapus file fisik di Flash.
 * @return bool: True jika "reset" berhasil tanpa sisa.
 */
bool NeuEEPROM::wipe()
{
    if (_buffer)
        memset(_buffer, 0xFF, _size);
    _dirty = true; // Tandai agar commit selanjutnya menulis data bersih
    if (LittleFS.exists(_path))
        return LittleFS.remove(_path);
    return true;
}

/**
 * [hexDump] Tampilan "Hacker Mode" untuk intip isi Shadow RAM.
 * Pro-tip: Baris kiri = Offset, Tengah = Hex, Kanan = ASCII.
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
                // Hanya tampilkan karakter yang bisa dibaca (Printable ASCII)
                Serial.print((c >= 32 && c <= 126) ? c : '.');
            }
        }
        Serial.println();
    }
}

/**
 * [debugSlots] Print informasi slot yang terdaftar (untuk debugging).
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
 * [verify] Membandingkan isi Shadow RAM vs File fisik di Flash (Byte by Byte).
 * @return bool: True jika RAM dan Flash sinkron 100% (Termasuk Checksum).
 */
bool NeuEEPROM::verify()
{
    if (!_buffer || !LittleFS.exists(_path))
        return false;

    File f = LittleFS.open(_path, "r");
    if (!f)
        return false;

    // 1. Cek ukuran file dulu (Data + 1 byte Checksum)
    if (f.size() != _size + 1)
    {
        f.close();
        return false;
    }

    // 2. Baca data dari Flash dan bandingkan langsung dengan RAM
    bool match = true;
    for (size_t i = 0; i < _size; i++)
    {
        if (f.read() != _buffer[i])
        {
            match = false;
            break;
        }
    }

    // 3. Cek apakah Checksum-nya juga masih valid
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
 * [update] Engine utama untuk mengelola commit otomatis dan rate limiting.
 */
void NeuEEPROM::update()
{
    if (!_dirty || _autoCommitMs == 0 || _isLocked)
        return;

    // Jika baru saja jadi dirty, catat waktunya
    if (_dirtyTimer == 0)
    {
        _dirtyTimer = millis();
        return;
    }

    // Jika sudah "mengendap" selama autoCommitMs, maka eksekusi!
    if (millis() - _dirtyTimer >= _autoCommitMs)
    {
        Serial.println(F("[NeuEEPROM] Engine: Auto-committing changes..."));
        if (commit())
            _dirtyTimer = 0; // Reset timer setelah sukses
    }
}

// Instance Singleton (Siap dipanggil di mana saja)
NeuEEPROM neuEEPROM;
