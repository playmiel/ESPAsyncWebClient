#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    
    // Make a simple GET request
    client.get("http://httpbin.org/get", 
        [](AsyncHttpResponse* response) {
            Serial.println("Success!");
            Serial.printf("Status: %d\n", response->getStatusCode());
            Serial.printf("Body: %s\n", response->getBody().c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %d - %s\n", static_cast<int>(error), message);
        }
    );
}

void loop() {
    client.loop();
    delay(1000);
}