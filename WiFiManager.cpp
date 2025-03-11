// WiFiManager.cpp
#include "WiFiManager.h"

WiFiManager::WiFiManager() : server(80), shouldSaveConfig(false), portalRunning(false), lastWiFiCheck(0) {}

void WiFiManager::begin() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return;
    }
    
    if (!loadConfig()) {
        Serial.println("Failed to load config, starting configuration portal");
        startPortal();
    } else {
        connect();
    }
}

bool WiFiManager::connect() {
    if (WiFi.status() == WL_CONNECTED) return true;
    
    DynamicJsonDocument doc(1024);
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) return false;
    
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    if (error) return false;
    
    const char* ssid = doc["ssid"];
    const char* password = doc["password"];
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < MAX_RETRY_ATTEMPTS) {
        delay(1000);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        startPortal();
        return false;
    }
    
    Serial.println("\nConnected to WiFi");
    Serial.println(WiFi.localIP());
    return true;
}

void WiFiManager::handleClient() {
    if (portalRunning) {
        // BLE scan/connect pause to avoid ble/wifi conflicts due to same freq. 2.4ghz
        if (blePauseCallback) {
            blePauseCallback(true);
        }
        server.handleClient();
    } else if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi connection lost, attempting reconnect...");
            if (blePauseCallback) {
              blePauseCallback(true);
            }
            connect();
            if (blePauseCallback) {
              blePauseCallback(false);
            }

        }
    }
}

bool WiFiManager::loadConfig() {
    if (!SPIFFS.exists("/config.json")) return false;
    
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) return false;
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    return !error && doc.containsKey("ssid") && doc.containsKey("password");
}

void WiFiManager::saveConfig() {
    DynamicJsonDocument doc(1024);
    doc["ssid"] = server.arg("ssid");
    doc["password"] = server.arg("password");
    
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
        serializeJson(doc, configFile);
        configFile.close();
        shouldSaveConfig = true;
    }
}

void WiFiManager::startPortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BLE-Gateway-Config");
    
    setupWebServer();
    server.begin();
    portalRunning = true;
    
    Serial.println("Configuration portal started");
    Serial.println(WiFi.softAPIP());
}

void WiFiManager::stopPortal() {
    server.stop();
    WiFi.softAPdisconnect(true);
    portalRunning = false;
}

void WiFiManager::setupWebServer() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/scan", HTTP_GET, [this]() { handleScan(); });
    server.on("/save", HTTP_POST, [this]() { handleSave(); });
    server.onNotFound([this]() { handleNotFound(); });
}

void WiFiManager::handleRoot() {
    server.send(200, "text/html", getHTMLContent());
}

void WiFiManager::handleScan() {
    server.send(200, "application/json", getNetworkList());
}

void WiFiManager::handleSave() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        saveConfig();
        server.send(200, "text/plain", "Configuration saved. Rebooting...");
        delay(1000);
        stopPortal();
        ESP.restart();
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

void WiFiManager::handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

String WiFiManager::getNetworkList() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray networks = doc.createNestedArray("networks");
    
    for (int i = 0; i < n; ++i) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String WiFiManager::getHTMLContent() {
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "    <style>\n"
           "        body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }\n"
           "        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
           "        h1 { color: #333; margin-top: 0; }\n"
           "        .form-group { margin-bottom: 15px; }\n"
           "        label { display: block; margin-bottom: 5px; color: #666; }\n"
           "        input[type=\"text\"], input[type=\"password\"] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
           "        button { background: #2196F3; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }\n"
           "        button:hover { background: #1976D2; }\n"
           "        #networks { margin-bottom: 15px; }\n"
           "        .network-item { padding: 8px; border: 1px solid #ddd; margin-bottom: 5px; border-radius: 4px; cursor: pointer; }\n"
           "        .network-item:hover { background: #f5f5f5; }\n"
           "    </style>\n"
           "</head>\n"
           "<body>\n"
           "    <div class=\"container\">\n"
           "        <h1>WiFi Configuration</h1>\n"
           "        <div class=\"form-group\">\n"
           "            <button onclick=\"scanNetworks()\">Scan Networks</button>\n"
           "            <div id=\"networks\"></div>\n"
           "        </div>\n"
           "        <form id=\"config-form\">\n"
           "            <div class=\"form-group\">\n"
           "                <label for=\"ssid\">SSID:</label>\n"
           "                <input type=\"text\" id=\"ssid\" name=\"ssid\" required>\n"
           "            </div>\n"
           "            <div class=\"form-group\">\n"
           "                <label for=\"password\">Password:</label>\n"
           "                <input type=\"password\" id=\"password\" name=\"password\" required>\n"
           "            </div>\n"
           "            <button type=\"submit\">Save Configuration</button>\n"
           "        </form>\n"
           "    </div>\n"
           "    <script>\n"
           "        function scanNetworks() {\n"
           "            fetch('/scan')\n"
           "                .then(response => response.json())\n"
           "                .then(data => {\n"
           "                    const networksDiv = document.getElementById('networks');\n"
           "                    networksDiv.innerHTML = '';\n"
           "                    data.networks.forEach(network => {\n"
           "                        const div = document.createElement('div');\n"
           "                        div.className = 'network-item';\n"
           "                        div.textContent = network.ssid + ' (' + network.rssi + ' dBm)';\n"
           "                        div.onclick = function() {\n"
           "                            document.getElementById('ssid').value = network.ssid;\n"
           "                        };\n"
           "                        networksDiv.appendChild(div);\n"
           "                    });\n"
           "                });\n"
           "        }\n"
           "\n"
           "        document.getElementById('config-form').onsubmit = function(e) {\n"
           "            e.preventDefault();\n"
           "            const formData = new FormData(e.target);\n"
           "            fetch('/save', {\n"
           "                method: 'POST',\n"
           "                body: formData\n"
           "            })\n"
           "            .then(response => response.text())\n"
           "            .then(text => alert(text));\n"
           "        };\n"
           "    </script>\n"
           "</body>\n"
           "</html>";
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::pauseBLE(bool pause) {
    if (blePauseCallback) {
        blePauseCallback(pause);
    }
}

String WiFiManager::getLocalIP() {
    return WiFi.localIP().toString();
}