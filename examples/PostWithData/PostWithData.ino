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
    
    // Make a POST request with data
    String postData = "name=ESP32&value=123&sensor=temperature";
    
    client.post("http://httpbin.org/post", postData.c_str(),
        [](AsyncHttpResponse* response) {
            Serial.println("POST Success!");
            Serial.printf("Status: %d\n", response->getStatusCode());
            Serial.printf("Content-Type: %s\n", response->getHeader("Content-Type").c_str());
            Serial.printf("Body length: %d\n", response->getBody().length());
            
            // Print first 500 characters of response
            String body = response->getBody();
            if (body.length() > 500) {
                body = body.substring(0, 500) + "...";
            }
            Serial.printf("Body: %s\n", body.c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("POST Error: %d - %s\n", static_cast<int>(error), message);
        }
    );
}

void loop() {
    client.loop();
    delay(1000);
}