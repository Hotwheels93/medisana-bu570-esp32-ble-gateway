#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

class APIClient {
public:
    APIClient(const char* host);
    bool post(const char* endpoint, const String& data, String& response);
    bool get(const char* endpoint, String& response);
    
private:
    const char* host;
    WiFiClientSecure client;
    bool readResponse(String& response);
};

#endif