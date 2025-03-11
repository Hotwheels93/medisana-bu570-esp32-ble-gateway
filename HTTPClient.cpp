#include "HTTPClient.h"

APIClient::APIClient(const char* host) : host(host) {
    client.setInsecure();
    client.setTimeout(5);
}

bool APIClient::post(const char* endpoint, const String& data, String& response) {
    // Vorherige Verbindungen sauber beenden
    client.stop();
    delay(100);

    Serial.printf("\n[HTTP] POST to %s (Heap: %d)\n", host, ESP.getFreeHeap());
    
    if (!client.connect(host, 443)) {
        Serial.println("[HTTP] Connection failed");
        return false;
    }

    // Request senden
    client.print("POST ");
    client.print(endpoint);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(data.length());
    client.println();
    client.println(data);

    // Response lesen
    return readResponse(response);
}

bool APIClient::get(const char* endpoint, String& response) {
    // Vorherige Verbindungen sauber beenden
    client.stop();
    delay(100);

    Serial.printf("\n[HTTP] GET from %s (Heap: %d)\n", host, ESP.getFreeHeap());
    
    if (!client.connect(host, 443)) {
        Serial.println("[HTTP] Connection failed");
        return false;
    }

    // Request senden
    client.print("GET ");
    client.print(endpoint);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();

    // Response lesen
    return readResponse(response);
}

bool APIClient::readResponse(String& response) {
    // Auf Antwort warten
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 5000) {
            Serial.println("[HTTP] Response timeout");
            client.stop();
            return false;
        }
        delay(10);
    }

    // Headers überspringen
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
        if (millis() - timeout > 5000) {
            Serial.println("[HTTP] Headers timeout");
            client.stop();
            return false;
        }
    }

    // Response body lesen
    response = "";
    response.reserve(512); // Kleinerer Buffer
    timeout = millis();

    int bytesRead = 0;
    while (client.available() && bytesRead < 512 && (millis() - timeout < 5000)) {
        char c = client.read();
        response += c;
        bytesRead++;
        if (bytesRead % 100 == 0) {
            delay(1); // Kleine Pause zur Vermeidung von Watchdog-Resets
        }
    }

    client.stop();
    Serial.printf("[HTTP] Response received (%d bytes, Heap: %d)\n", 
                 response.length(), ESP.getFreeHeap());
    return response.length() > 0;
}