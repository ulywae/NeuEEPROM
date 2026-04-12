#include <NeuEEPROM.h>

/**
 * NeuEEPROM: Event-Driven Error Handling Example
 * This sketch demonstrates the "Paranoid" diagnostic system.
 */

// Define Slot IDs
#define ID_WIFI_SETTINGS 1
#define ID_DEVICE_CONFIG 2

struct DeviceConfig {
  int brightness;
  bool autoMode;
};

// 1. ERROR CALLBACK FUNCTION
// This function acts like a "System Observer." It triggers whenever the 
// library detects suspicious activity or hardware failures.
void onSystemError(uint8_t errorCode, uint8_t id) {
  Serial.println(F("\n[NeuEEPROM Diagnostic Report]"));
  
  switch (errorCode) {
    case neuEEPROM.ERR_NOT_REGISTERED:
      Serial.printf("Status: Access denied! Slot ID %d is not registered.\n", id);
      break;

    case neuEEPROM.ERR_SIZE_MISMATCH:
      Serial.printf("Status: Data size mismatch for ID %d. Check your data types!\n", id);
      break;

    case neuEEPROM.ERR_FLASH_SPAM:
      Serial.println(F("Status: CRITICAL! Write spam detected. Flash protection ACTIVE (Locked)."));
      break;

    case neuEEPROM.ERR_BUFFER_OVERFLOW:
      Serial.printf("Status: Memory full! Cannot register more data in Slot ID %d.\n", id);
      break;

    case neuEEPROM.ERR_CRC_FAIL:
      Serial.println(F("Status: Data corruption detected! System initiated self-healing (Wipe)."));
      break;

    case neuEEPROM.ERR_MALLOC_FAIL:
      Serial.println(F("Status: System RAM exhausted! Heap allocation failed."));
      break;

    default:
      Serial.printf("Status: Unknown error code %d occurred.\n", errorCode);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 2. REGISTER CALLBACK BEFORE BEGIN
  // Hook the observer to the library engine
  neuEEPROM.onError(onSystemError);

  // Initialize library
  if (!neuEEPROM.begin(512)) {
    Serial.println(F("Critical: Hardware initialization failed!"));
    while(1);
  }

  // 3. REGISTER SLOTS
  neuEEPROM.registerSlot(ID_DEVICE_CONFIG, sizeof(DeviceConfig));

  Serial.println(F("System is online. Testing 'Paranoid Mode'..."));
}

void loop() {
  // --- SIMULATION 1: Accessing an unregistered ID ---
  // This will trigger ERR_NOT_REGISTERED via Callback
  float nonExistentData = 45.5;
  neuEEPROM.put(99, nonExistentData); 

  // --- SIMULATION 2: Data type size mismatch ---
  // If we try to put a 'long' into a slot made for 'DeviceConfig'
  // This will trigger ERR_SIZE_MISMATCH via Callback
  long wrongType = 123456789;
  neuEEPROM.put(ID_DEVICE_CONFIG, wrongType); 

  // Wait 10 seconds to avoid flooding the Serial Monitor
  delay(10000); 
}
