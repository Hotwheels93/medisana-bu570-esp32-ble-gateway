// WiFiManager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <functional>


class WiFiManager {
public:
    WiFiManager();
    void begin();
    bool connect();
    void handleClient();
    bool isConnected();
    void pauseBLE(bool pause);
    String getLocalIP();
    // Callback for BLE control
    void setBlePauseCallback(std::function<void(bool)> callback) {
        blePauseCallback = callback;
    }

private:
    WebServer server;
    bool shouldSaveConfig;
    bool portalRunning;
    unsigned long lastWiFiCheck;
    const unsigned long WIFI_CHECK_INTERVAL = 30000; // 30 seconds
    const int MAX_RETRY_ATTEMPTS = 3;
    std::function<void(bool)> blePauseCallback;

    
    // Configuration methods
    bool loadConfig();
    void saveConfig();
    void setupWebServer();
    void startPortal();
    void stopPortal();
    
    // Web handlers
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleNotFound();
    
    // Helper methods
    String getNetworkList();
    String getHTMLContent();
};

#endif // WIFI_MANAGER_H