#include <NeuEEPROM.h>

/**
 * NeuEEPROM v2.0.0: Backup & Restore Example
 * This sketch demonstrates how to Export and Import your data
 * using the Stream interface (Serial).
 *
 * Pro-Tips:
 * 1. Binary Mode: Remind users that exportData sends RAW binary.
 *    If they use a terminal like Serial Monitor, some bytes might not display correctly.
 *    They should use a terminal that supports Binary/Hex transfer (like Tera Term, CoolTerm, or a custom Python script).
 *
 * 2. Encryption Sync: The data is exported in its encrypted state.
 *    To restore it, the receiving device must have the exact same setEncryption key active before calling importData.
 *
 * 3. The "5-Second" Rule: If they want to use masterClear() as well,
 *    remind them that importData is a safer way to "overwrite" settings than clearing them entirely.
 */

#define ID_CONFIG 1

struct MySettings
{
    int deviceId;
    char status[16];
};

// Simple encryption key
uint8_t mySecretKey[] = {0xDE, 0xAD, 0xBE, 0xEF};

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // 1. Setup Encryption (Optional but recommended)
    neuEEPROM.setEncryption(mySecretKey, sizeof(mySecretKey));

    // 2. Initialize
    if (!neuEEPROM.begin(512))
    {
        Serial.println(F("Failed to init NeuEEPROM"));
        while (1)
            ;
    }

    neuEEPROM.registerSlot(ID_CONFIG, sizeof(MySettings));

    Serial.println(F("\n--- NeuEEPROM Backup & Restore Menu ---"));
    Serial.println(F("Press 'E' to Export (Backup to Serial)"));
    Serial.println(F("Press 'I' to Import (Restore from Serial)"));
    Serial.println(F("Press 'D' to show HexDump"));
    Serial.println(F("---------------------------------------\n"));
}

void loop()
{
    if (Serial.available())
    {
        char cmd = Serial.read();

        if (cmd == 'E' || cmd == 'e')
        {
            Serial.println(F("\n[STARTING EXPORT]"));
            Serial.println(F("--- BEGIN BINARY DATA ---"));

            // Export binary data directly to Serial
            neuEEPROM.exportData(Serial);

            Serial.println(F("\n--- END BINARY DATA ---"));
            Serial.println(F("[EXPORT COMPLETE]"));
        }

        else if (cmd == 'I' || cmd == 'i')
        {
            Serial.println(F("\n[READY TO IMPORT]"));
            Serial.println(F("Please paste/send your binary file now..."));

            // Import from Serial with 10 seconds timeout
            if (neuEEPROM.importData(Serial, 10000))
            {
                Serial.println(F("[IMPORT SUCCESS] Data re-synced to RAM."));
            }
            else
            {
                Serial.println(F("[IMPORT FAILED] Timeout or Invalid Data."));
            }
        }

        else if (cmd == 'D' || cmd == 'd')
        {
            neuEEPROM.hexDump();
        }
    }
}
