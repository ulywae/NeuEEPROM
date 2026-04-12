#include <Preferences.h>
#include "NeuEEPROM.h"

Preferences prefs;
const uint8_t myKey[] = {0x01, 0x02, 0x03, 0x04};

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println(F("\n=== BENCHMARK: NeuEEPROM vs Preferences ==="));

    // 1. NeuEEPROM Preparation
    neuEEPROM.setEncryption(myKey, sizeof(myKey));
    neuEEPROM.begin(512);
    neuEEPROM.registerSlot(1, sizeof(int));

    // 2. Preferences Preparation
    prefs.begin("test_bench", false);

    // --- TEST 1: WRITE SPEED (LATENCY) ---
    uint32_t start, end;
    int testData = 12345;

    Serial.println(F("\n[Test 1: Write Latency]"));

    // Benchmark Preferences
    start = micros();
    prefs.putInt("val", testData);
    end = micros();
    Serial.print(F("Preferences putInt: ")); Serial.print(end - start); Serial.println(F(" us"));

    // Benchmark NeuEEPROM Put (to RAM)
    start = micros();
    neuEEPROM.put(1, testData); 
    end = micros();
    Serial.print(F("NeuEEPROM put (RAM): ")); Serial.print(end - start); Serial.println(F(" us (Fastest!)"));

    // Benchmark NeuEEPROM Commit (to Flash)
    start = micros();
    neuEEPROM.commit();
    end = micros();
    Serial.print(F("NeuEEPROM commit (Flash): ")); Serial.print(end - start); Serial.println(F(" us (Atomic Swap)"));

    // --- TEST 2: RAM USAGE ---
    Serial.println(F("\n[Test 2: RAM Usage]"));
    Serial.print(F("RAM used NeuEEPROM: ")); 
    Serial.print(neuEEPROM.getLibraryHeapUsage()); Serial.println(F(" bytes"));
    
    Serial.print(F("Global RAM Remaining: ")); 
    Serial.print(ESP.getFreeHeap()); Serial.println(F(" bytes"));

    prefs.end();
}

void loop() {}
