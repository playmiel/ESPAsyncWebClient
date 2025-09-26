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
    Serial.printf("[%s] Body length: %d\n", requestName.c_str(), response->getBody().length());

    if (responseCount >= requestCount) {
        Serial.println("All requests completed!");
    }
}

void onError(HttpClientError error, const char* message, const String& requestName) {
    responseCount++;
    Serial.printf("[%s] Error %d: %s\n", requestName.c_str(), (int)error, httpClientErrorToString(error));

    if (responseCount >= requestCount) {
        Serial.println("All requests completed!");
    }
}

void setup() {
    Serial.begin(115200);

    // Connect to WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    Serial.println("Starting multiple simultaneous requests...");

    // Request 1: GET request
    requestCount++;
    client.get(
        "http://httpbin.org/get", [](AsyncHttpResponse* response) { onResponse(response, "GET"); },
        [](HttpClientError error, const char* message) { onError(error, message, "GET"); });

    // Request 2: POST request
    requestCount++;
    client.post(
        "http://httpbin.org/post", "data=test", [](AsyncHttpResponse* response) { onResponse(response, "POST"); },
        [](HttpClientError error, const char* message) { onError(error, message, "POST"); });

    // Request 3: Another GET to different endpoint
    requestCount++;
    client.get(
        "http://httpbin.org/headers", [](AsyncHttpResponse* response) { onResponse(response, "HEADERS"); },
        [](HttpClientError error, const char* message) { onError(error, message, "HEADERS"); });

    // Request 4: DELETE request
    requestCount++;
    client.del(
        "http://httpbin.org/delete", [](AsyncHttpResponse* response) { onResponse(response, "DELETE"); },
        [](HttpClientError error, const char* message) { onError(error, message, "DELETE"); });

    Serial.printf("Initiated %d simultaneous requests\n", requestCount);
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // ESP32 fallback mode: the library auto-ticks timeouts via a FreeRTOS task.
    // If you define -DASYNC_HTTP_DISABLE_AUTOLOOP, call client.loop() periodically here.
    // client.loop();
#endif
}
