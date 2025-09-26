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

    // Set global default headers
    client.setHeader("X-API-Key", "your-api-key-here");
    client.setHeader("Authorization", "Bearer your-token-here");
    client.setUserAgent("ESP32-CustomClient/1.0");
    client.setTimeout(15000); // 15 seconds timeout

    // Make a request that will include the custom headers
    client.get(
        "http://httpbin.org/headers",
        [](AsyncHttpResponse* response) {
            Serial.println("Request with custom headers successful!");
            Serial.printf("Status: %d\n", response->getStatusCode());

            // Print response headers
            Serial.println("\nResponse Headers:");
            const auto& headers = response->getHeaders();
            for (const auto& header : headers) {
                Serial.printf("  %s: %s\n", header.name.c_str(), header.value.c_str());
            }

            // Print the body which should show our request headers
            Serial.printf("\nResponse Body:\n%s\n", response->getBody().c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        });

    delay(5000); // Wait 5 seconds

    // Make another request with additional headers using the advanced API
    AsyncHttpRequest* customRequest = new AsyncHttpRequest(HTTP_POST, "http://httpbin.org/post");
    customRequest->setHeader("Content-Type", "application/json");
    customRequest->setHeader("X-Custom-Header", "CustomValue123");
    customRequest->setHeader("Accept", "application/json");
    customRequest->setBody("{\"message\":\"Hello from ESP32\",\"timestamp\":" + String(millis()) + "}");

    client.request(
        customRequest,
        [](AsyncHttpResponse* response) {
            Serial.println("\nCustom JSON POST request successful!");
            Serial.printf("Status: %d\n", response->getStatusCode());
            Serial.printf("Content-Type: %s\n", response->getHeader("Content-Type").c_str());

            // Print first 800 characters of response
            String body = response->getBody();
            if (body.length() > 800) {
                body = body.substring(0, 800) + "...";
            }
            Serial.printf("Response: %s\n", body.c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Custom request error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // ESP32 fallback mode: the library auto-ticks timeouts via a FreeRTOS task.
    // If you define -DASYNC_HTTP_DISABLE_AUTOLOOP, call client.loop() periodically here.
    // client.loop();
#endif
}
