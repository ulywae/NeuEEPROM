#ifndef NEU_CIPHER_H
#define NEU_CIPHER_H

#include <Arduino.h>

/**
 * WARNING: This is a simple XOR cipher for basic obfuscation, NOT cryptographically secure.
 * - Prevents accidental reading of plaintext data (e.g., opening .bin file in editor)
 * - Does NOT protect against determined adversaries with known-plaintext attacks
 * - For security-sensitive data, replace this class with AES-128 or ChaCha20
 */

class NeuCipher
{
public:
    /**
     * [process] XORs data with the array key.
     * This algorithm is reversible (the process is the same for encryption and decryption).
     */
    static void process(uint8_t *data, size_t dataLen, const uint8_t *key, size_t keyLen)
    {
        if (!data || !key || keyLen == 0)
            return;

        // XOR each byte of data with the rotating byte key (modulo)
        for (size_t i = 0; i < dataLen; i++)
            data[i] ^= key[i % keyLen];
    }
};

#endif
