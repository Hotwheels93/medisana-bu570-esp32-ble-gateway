/*
mh2 BLE Gateway 
Author: Martin Hocquel-Hans
Date: 2025-02-17
Description: Implementation of a BLE Client with WiFi Management and API Communication
*/

#include "esp_coexist.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <esp_gap_ble_api.h>
#include "WiFiManager.h"
#include "HTTPClient.h"


WiFiManager wifiManager;
APIClient apiClient("sensors.mh2-dev.com");

static BLEScan* pBLEScan = nullptr;
static bool blePaused = false;
static int lastRSSI = 0;

// Service UUIDs
static BLEUUID BLOOD_PRESSURE_SERVICE_UUID((uint16_t)0x1810);

// Characteristic UUIDs
static BLEUUID BLOOD_PRESSURE_MEASUREMENT_UUID((uint16_t)0x2A35);
static BLEUUID INTERMEDIATE_CUFF_PRESSURE_UUID((uint16_t)0x2A36);
static BLEUUID BLOOD_PRESSURE_FEATURE_UUID((uint16_t)0x2A49);
static BLEUUID CLIENT_CHARACTERISTIC_CONFIG_UUID((uint16_t)0x2902);

// BLE Status Variables
static BLEAddress* pServerAddress;
static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pBPMeasurementCharacteristic = nullptr;
static BLERemoteCharacteristic* pIntermediateCuffCharacteristic = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr;
static boolean doConnect = false;
static boolean connected = false;
static boolean indications_enabled = false;
static boolean notifications_enabled = false;

static void bpMeasurementCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, 
                                uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("\nBLOOD PRESSURE MEASUREMENT RECEIVED\n");
    Serial.printf("Signal Strength (RSSI): %d dBm\n", lastRSSI);
    Serial.print("Raw data: ");
    for(int i = 0; i < length; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();
    
    uint8_t flags = pData[0];
    uint16_t systolic = (pData[1] | (pData[2] << 8));
    uint16_t diastolic = (pData[3] | (pData[4] << 8));
    uint16_t map = (pData[5] | (pData[6] << 8));
    
    // Create JSON for API
    DynamicJsonDocument doc(1024);
    doc["systolic"] = systolic;
    doc["diastolic"] = diastolic;
    doc["map"] = map;
    doc["rssi"] = lastRSSI;
    
    // Add timestamp if present
    int index = 7;
    if (flags & 0x02) {
        uint16_t year = pData[index] | (pData[index + 1] << 8);
        uint8_t month = pData[index + 2];
        uint8_t day = pData[index + 3];
        uint8_t hours = pData[index + 4];
        uint8_t minutes = pData[index + 5];
        uint8_t seconds = pData[index + 6];
        
        char timestamp[25];
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                year, month, day, hours, minutes, seconds);
        doc["timestamp"] = timestamp;
        index += 7;
    }
    
    // Add pulse if present
    if (flags & 0x04) {
        uint16_t pulse = (pData[index] | (pData[index + 1] << 8));
        doc["pulse"] = pulse;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    // Send to API if WiFi is connected
    if (wifiManager.isConnected()) {
        String response;
    if (apiClient.post("/index.php?endpoint=measurements", jsonString, response)) {
            Serial.println("Measurement sent to API successfully");
            Serial.println("API Response: " + response);
        } else {
            Serial.println("Failed to send measurement to API");
        }
    } else {
        Serial.println("WiFi not connected, measurement not sent to API");
    }
    
    // Console output
    Serial.printf("\n=== Blood Pressure Reading ===\n");
    Serial.printf("Systolic: %d mmHg\n", systolic);
    Serial.printf("Diastolic: %d mmHg\n", diastolic);
    Serial.printf("Mean Arterial Pressure: %d mmHg\n", map);
    
    if (flags & 0x02) {
        uint16_t year = pData[index-7] | (pData[index-6] << 8);
        uint8_t month = pData[index-5];
        uint8_t day = pData[index-4];
        uint8_t hours = pData[index-3];
        uint8_t minutes = pData[index-2];
        uint8_t seconds = pData[index-1];
        Serial.printf("Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hours, minutes, seconds);
    }
    
    if (flags & 0x04) {
        uint16_t pulse = (pData[index] | (pData[index + 1] << 8));
        Serial.printf("Pulse: %d bpm\n", pulse);
    }
    
    Serial.println("===========================");
}

static void intermediateCuffCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                                   uint8_t* pData, size_t length, bool isNotify) {
    uint16_t pressure = (pData[1] | (pData[2] << 8));
    
    // Create JSON for API
    DynamicJsonDocument doc(256);
    doc["cuff_pressure"] = pressure;
    doc["rssi"] = lastRSSI;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    // Send to API if WiFi is connected
    if (wifiManager.isConnected()) {
        String response;
    if (apiClient.post("/index.php?endpoint=intermediate", jsonString, response)) {
            Serial.println("Intermediate pressure sent to API successfully");
        }
    }
    
    Serial.printf("Current Cuff Pressure: %d mmHg\n", pressure);
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        connected = true;
        Serial.println("Connected to BLE device");
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        indications_enabled = false;
        notifications_enabled = false;
        Serial.println("Disconnected from BLE device");
    }
};

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.getServiceUUID().equals(BLOOD_PRESSURE_SERVICE_UUID)) {
            lastRSSI = advertisedDevice.getRSSI();
            advertisedDevice.getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            doConnect = true;
        }
    }
};

bool connectToServer() {
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY = 1000; // 1 second

    Serial.print("Forming a connection to ");
    Serial.println(pServerAddress->toString().c_str());
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    // Set preferred connection parameters
    esp_ble_conn_update_params_t connParams;
    connParams.min_int = 0x10;  // 20ms
    connParams.max_int = 0x20;  // 40ms
    connParams.latency = 0;
    connParams.timeout = 400;    // 4 seconds
    
    // Connect to the remote BLE Server
    if (!pClient->connect(*pServerAddress)) {
        Serial.println("Failed to connect to server");
        return false;
    }
    
    // Set preferred connection parameters
    esp_ble_gap_update_conn_params(&connParams);
    
    Serial.println("Connected to server");
    
    // Get the Blood Pressure service
    BLERemoteService* pRemoteService = pClient->getService(BLOOD_PRESSURE_SERVICE_UUID);
    if (pRemoteService == nullptr) {
        Serial.println("Failed to find Blood Pressure service");
        return false;
    }
    Serial.println("Found Blood Pressure service");
    
    // Get Blood Pressure Measurement characteristic
    pBPMeasurementCharacteristic = pRemoteService->getCharacteristic(BLOOD_PRESSURE_MEASUREMENT_UUID);
    if (pBPMeasurementCharacteristic == nullptr) {
        Serial.println("Failed to find Blood Pressure Measurement characteristic");
        return false;
    }
    
    // Enable indications for Blood Pressure Measurement
    if(pBPMeasurementCharacteristic->canIndicate()) {
        // First disable both notifications and indications
        const uint8_t bothOff[] = {0x0, 0x0};
        BLERemoteDescriptor* pDesc = pBPMeasurementCharacteristic->getDescriptor(CLIENT_CHARACTERISTIC_CONFIG_UUID);
        if (pDesc != nullptr) {
            pDesc->writeValue((uint8_t*)bothOff, 2, true);
            delay(100);  // Give cpu some time to process
            
            // Now enable indications
            const uint8_t indicationOn[] = {0x2, 0x0};
            pDesc->writeValue((uint8_t*)indicationOn, 2, true);
            delay(100); 
            
            // Verify the write
            std::string descValue = pDesc->readValue();
            if (descValue.length() >= 2 && descValue[0] == indicationOn[0] && descValue[1] == indicationOn[1]) {
                Serial.println("Indication setting verified");
                indications_enabled = true;
            } else {
                Serial.println("Failed to verify indication setting");
            }
            
            pBPMeasurementCharacteristic->registerForNotify(bpMeasurementCallback, false);  // false = indications
            Serial.println("Enabled Blood Pressure Measurement indications");
        }
    }
    
    // Get and enable Intermediate Cuff Pressure characteristic
    pIntermediateCuffCharacteristic = pRemoteService->getCharacteristic(INTERMEDIATE_CUFF_PRESSURE_UUID);
    if (pIntermediateCuffCharacteristic != nullptr && pIntermediateCuffCharacteristic->canNotify()) {
        const uint8_t notificationOn[] = {0x1, 0x0};
        BLERemoteDescriptor* pDesc = pIntermediateCuffCharacteristic->getDescriptor(CLIENT_CHARACTERISTIC_CONFIG_UUID);
        if (pDesc != nullptr) {
            pDesc->writeValue((uint8_t*)notificationOn, 2, true);
            pIntermediateCuffCharacteristic->registerForNotify(intermediateCuffCallback);
            notifications_enabled = true;
            Serial.println("Enabled Intermediate Cuff Pressure notifications");
        }
    }
    
    // Read Blood Pressure Feature
    BLERemoteCharacteristic* pFeatureCharacteristic = 
        pRemoteService->getCharacteristic(BLOOD_PRESSURE_FEATURE_UUID);
    if (pFeatureCharacteristic != nullptr && pFeatureCharacteristic->canRead()) {
        std::string value = pFeatureCharacteristic->readValue();
        if (value.length() >= 2) {
            uint16_t features = (uint8_t)value[0] | ((uint8_t)value[1] << 8);
            Serial.printf("Blood Pressure Feature: 0x%04X\n", features);
        }
    }

    // Set up Current Time Service
    BLERemoteService* pTimeService = pClient->getService(BLEUUID((uint16_t)0x1805));
    if (pTimeService != nullptr) {
        BLERemoteCharacteristic* pTimeChar = pTimeService->getCharacteristic(BLEUUID((uint16_t)0x2A2B));
        if (pTimeChar != nullptr && pTimeChar->canWrite()) {
            // Get current time
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            
            // Format time according to BLE Current Time characteristic format
            uint8_t timeData[10];
            timeData[0] = (timeinfo.tm_year + 1900) & 0xFF;        // Year LSB
            timeData[1] = ((timeinfo.tm_year + 1900) >> 8) & 0xFF; // Year MSB
            timeData[2] = timeinfo.tm_mon + 1;                     // Month
            timeData[3] = timeinfo.tm_mday;                        // Day
            timeData[4] = timeinfo.tm_hour;                        // Hours
            timeData[5] = timeinfo.tm_min;                         // Minutes
            timeData[6] = timeinfo.tm_sec;                         // Seconds
            timeData[7] = timeinfo.tm_wday;                        // Day of Week
            timeData[8] = 0;                                       // Fractions256
            timeData[9] = 0;                                       // Adjust Reason

            pTimeChar->writeValue(timeData, 10, true);
            Serial.println("Current time set on device");
        }
    }
    
    connected = true;
    return true;
}

void setup() {
    Serial.begin(115200);
    Serial.println("mh2 - BLE Gateway with WiFi Management");
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

    // Initialize WiFi Manager
    wifiManager.begin();
    
    // Initialize BLE
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    BLEDevice::init("");
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK);
    
    Serial.printf("Initial free heap: %d\n", ESP.getFreeHeap());

    // BLE-Pause-Callback setzen
    wifiManager.setBlePauseCallback([](bool pause) {
        blePaused = pause;
        if (pause) {
            if (pBLEScan != nullptr) {
                pBLEScan->stop();
            }
        }
    });

    // Start initial BLE scan
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(5);
}

void loop() {
    static unsigned long lastCheck = 0;
    static unsigned long disconnectTime = 0;
    
    // Handle WiFi management
    wifiManager.handleClient();
    
    // Handle BLE connection
    if (doConnect) {
        if (connectToServer()) {
            Serial.println("Connection to BLE Server: [SUCCESS]");
        } else {
            Serial.println("Connection to BLE Server: [FAIL]");
        }
        doConnect = false;
    }
    
    // Periodic connection check
if (millis() - lastCheck >= 5000) {
    lastCheck = millis();
    if (connected && pClient != nullptr) {
        if (!pClient->isConnected()) {
            Serial.println("BLE Connection lost!");
            connected = false;
            disconnectTime = millis();
        } else {
            // Reduziere die Häufigkeit der API-Status-Checks
            static unsigned long lastApiCheck = 0;
            if (wifiManager.isConnected() && (millis() - lastApiCheck >= 60000)) {  // Alle 30 Sekunden
                lastApiCheck = millis();
                String response;
            if (apiClient.get("/index.php?endpoint=status", response)) {
                    Serial.println("API connection: OK");
                }
            }
            
            // Log connection status
            Serial.println("BLE Connection: OK");
            if (indications_enabled) {
                Serial.println("Indications are enabled");
            }
            if (notifications_enabled) {
                Serial.println("Notifications are enabled");
            }


        }
    }
}
    
    // If disconnected from BLE, wait before scanning again
      if (!connected && !blePaused && (millis() - disconnectTime >= 3000)) {
          Serial.println("Starting BLE scan...");
          pBLEScan->start(5);
      }
    
    delay(100);  // Reduced delay for more responsive checking
}