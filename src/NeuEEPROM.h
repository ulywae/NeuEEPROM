#ifndef NEU_EEPROM_H
#define NEU_EEPROM_H

#include <Arduino.h>
#include <LittleFS.h>
#include <type_traits>

class NeuEEPROM
{
private:
    uint8_t *_buffer = nullptr;  // Shadow RAM untuk menyimpan data sementara sebelum commit
    size_t _size = 0;            // Ukuran Shadow RAM
    const char *_path = nullptr; // Path file di LittleFS untuk menyimpan data EEPROM
    bool _dirty = false;         // Flag untuk menandai apakah ada perubahan yang belum di-commit ke Flash
    bool _autoFormat = false;    // Aktifkan format otomatis jika mount gagal

    // Slot system
    struct Slot
    {
        uint16_t offset;
        uint16_t size;
    };

    Slot _slots[256];              // Daftar Slot
    bool _slotUsed[256] = {false}; // Daftar Slot yang digunakan
    uint16_t _nextOffset = 0;      // Offset untuk slot selanjutnya

    // Security Guard Variables
    uint32_t _lastCheckTime = 0; // Waktu terakhir commit atau verifikasi berhasil
    uint8_t _writeCount = 0;     // Menghitung berapa kali commit
    bool _isLocked = false;      // True jika terblokir
    uint8_t _sameDataCount = 0;  // Menghitung berapa kali commit data yang sama
    uint32_t _dirtyTimer = 0;    // Waktu terakhir data dirty
    uint32_t _autoCommitMs = 0;  // 0 berarti fitur mati

    uint8_t _calculateChecksum(uint8_t *data, size_t len);

public:
    NeuEEPROM();
    ~NeuEEPROM();

    bool begin(size_t size = 512, const char *path = "/neu_eeprom.bin"); // Inisialisasi NeuEEPROM
    void autoFormatting(bool enable) { _autoFormat = enable; }           // Aktifkan format otomatis jika mount gagal
    bool registerSlot(uint8_t id, size_t size);                          // Daftarkan slot dengan ID unik dan ukuran tertentu (Opsional, untuk manajemen internal)

    template <typename T>
    void put(uint8_t id, const T &data) // Menyimpan data ke Shadow RAM, tandai dirty jika ada perubahan
    {
        // Cek: Apakah tipe data ini aman untuk di-copy mentah (Plain Old Data)?
        static_assert(std::is_trivially_copyable<T>::value, "Tipe data terlalu kompleks untuk EEPROM!");

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
    bool get(uint8_t id, T &data) // Membaca data dari Shadow RAM
    {
        // Cek: Pastikan tipe data tujuan juga kompatibel
        static_assert(std::is_trivially_copyable<T>::value, "ERROR: Tipe data tujuan tidak kompatibel!");

        if (!_slotUsed[id])
            return false;

        auto &slot = _slots[id];
        if (slot.size != sizeof(T))
            return false;

        memcpy(&data, _buffer + slot.offset, sizeof(T));
        return true;
    }

    bool commit(uint32_t maxIntervalMs = 100, uint8_t maxWrites = 10); // Tulis data ke Flash dengan mekanisme keamanan
    bool wipe();                                                       // Reset Shadow RAM dan hapus file di Flash
    bool verify();                                                     // Cek integritas antara RAM vs Flash
    bool isDirty() const { return _dirty; }                            // Cek apakah ada perubahan
    bool isLocked() const { return _isLocked; }                        // Cek apakah commit terkunci karena rate limit

    // Set berapa lama nunggu sejak data dirty sampai commit otomatis
    void setAutoCommit(uint32_t ms) { _autoCommitMs = ms; } // Jika ms = 0, fitur auto-commit dimatikan
    void update();                                          // Fungsi engine untuk mengelola auto-commit dan rate limiting

    void hexDump(size_t bytesPerLine = 16); // print "Hex Mode"
    void debugSlots();                      // Print informasi slot yang terdaftar (untuk debugging)
};

extern NeuEEPROM neuEEPROM;

#endif