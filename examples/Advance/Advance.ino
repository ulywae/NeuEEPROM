#include <Arduino.h>
#include "NeuEEPROM.h"

// Slot IDs
#define ID_WIFI 0
#define ID_USER 1

struct Wifi
{
    char ssid[32];
    char password[32];
};

struct User
{
    uint8_t level;
    uint32_t score;
};

Wifi wifiData;
User userData;

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ;

    // Init EEPROM
    if (!neuEEPROM.begin(512))
    {
        Serial.println("EEPROM Init Failed!");
        return;
    }

    // Register slots
    neuEEPROM.registerSlot(ID_WIFI, sizeof(Wifi));
    neuEEPROM.registerSlot(ID_USER, sizeof(User));

    // Enable auto-commit every 5 seconds
    neuEEPROM.setAutoCommit(5000);

    // Initial data
    strcpy(wifiData.ssid, "NeuNetwork");
    strcpy(wifiData.password, "SuperSecret");
    userData.level = 10;
    userData.score = 98765;

    neuEEPROM.put(ID_WIFI, wifiData);
    neuEEPROM.put(ID_USER, userData);

    Serial.println("Initial data written to RAM (Shadow)!");
}

void loop()
{
    // Auto-commit engine
    neuEEPROM.update();

    // Example: Read back periodically
    static uint32_t t = millis();
    if (millis() - t > 7000)
    { // every 7s
        Wifi rWifi;
        User rUser;
        if (neuEEPROM.get(ID_WIFI, rWifi))
        {
            Serial.printf("[Read] SSID: %s, PASS: %s\n", rWifi.ssid, rWifi.password);
        }
        if (neuEEPROM.get(ID_USER, rUser))
        {
            Serial.printf("[Read] Level: %d, Score: %u\n", rUser.level, rUser.score);
        }

        // Verify integrity
        if (neuEEPROM.verify())
        {
            Serial.println("[Verify] Data OK");
        }
        else
        {
            Serial.println("[Verify] Data MISMATCH!");
        }

        t = millis();
    }

    // Example: Wipe after 20s
    if (millis() > 20000 && !neuEEPROM.isLocked())
    {
        Serial.println("[Wipe] Reset EEPROM!");
        neuEEPROM.wipe();
        neuEEPROM.hexDump();
        while (true)
            ; // Stop here after wipe for demo
    }
}