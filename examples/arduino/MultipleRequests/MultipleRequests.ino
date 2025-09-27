#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;
int requestCount = 0;
int responseCount = 0;

void onResponse(AsyncHttpResponse* response, const String& requestName) {
    responseCount++;
    Serial.printf("[%s] Response %d received!\n", requestName.c_str(), responseCount);
    Serial.printf("[%s] Status: %d %s\n", requestName.c_str(), response->getStatusCode(),
                  response->getStatusText().c_str());
}

void onError(HttpClientError error, const char* message, const String& requestName) {
    responseCount++;
    Serial.printf("[%s] Error %d: %s\n", requestName.c_str(), (int)error, httpClientErrorToString(error));
}

void setup() {
    Serial.begin(115200);

    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    requestCount++;
    client.get(
        "http://httpbin.org/get", [](AsyncHttpResponse* response) { onResponse(response, "GET"); },
        [](HttpClientError error, const char* message) { onError(error, message, "GET"); });

    requestCount++;
    client.post(
        "http://httpbin.org/post", "data=test", [](AsyncHttpResponse* response) { onResponse(response, "POST"); },
        [](HttpClientError error, const char* message) { onError(error, message, "POST"); });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // Call client.loop() periodically unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP (ESP32 only).
    client.loop();
#endif
}
