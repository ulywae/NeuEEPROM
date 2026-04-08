#include <Arduino.h>
#include "NeuEEPROM.h"

struct Wifi
{
    char ssid[32];
    char pass[32];
};
#define ID_WIFI 0

Wifi wifiData = {"NeuNet", "12345678"};

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ;

    // Init & register slot
    neuEEPROM.begin(128);
    neuEEPROM.registerSlot(ID_WIFI, sizeof(Wifi));

    // Write & commit
    neuEEPROM.put(ID_WIFI, wifiData);
    neuEEPROM.commit();

    // Read back
    Wifi r;
    if (neuEEPROM.get(ID_WIFI, r))
        Serial.printf("SSID: %s, PASS: %s\n", r.ssid, r.pass);

    // Verify RAM vs Flash
    Serial.println(neuEEPROM.verify() ? "[OK] Data synced" : "[FAIL] Mismatch");
}

void loop()
{
    // Engine auto-commit (optional)
    neuEEPROM.update();
}