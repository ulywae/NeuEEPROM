#include <Arduino.h>
#include "NeuEEPROM.h"

// ID slot
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

    // Init EEPROM 512 bytes
    if (!neuEEPROM.begin(512))
    {
        Serial.println("EEPROM Init Failed!");
        return;
    }

    // Register slots
    neuEEPROM.registerSlot(ID_WIFI, sizeof(Wifi));
    neuEEPROM.registerSlot(ID_USER, sizeof(User));

    // Set data
    strcpy(wifiData.ssid, "NeuNetwork");
    strcpy(wifiData.password, "12345678");
    userData.level = 5;
    userData.score = 12345;

    // Write to EEPROM (Shadow RAM)
    neuEEPROM.put(ID_WIFI, wifiData);
    neuEEPROM.put(ID_USER, userData);

    // Commit to flash
    if (neuEEPROM.commit())
    {
        Serial.println("Data committed successfully!");
    }

    // Read back
    Wifi rWifi;
    User rUser;
    if (neuEEPROM.get(ID_WIFI, rWifi))
    {
        Serial.printf("SSID: %s, PASS: %s\n", rWifi.ssid, rWifi.password);
    }
    if (neuEEPROM.get(ID_USER, rUser))
    {
        Serial.printf("Level: %d, Score: %u\n", rUser.level, rUser.score);
    }

    // Hex dump (optional)
    neuEEPROM.hexDump();
}

void loop()
{
    // Nothing here
}