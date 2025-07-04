# ESPAsyncWebClient

[![Build Examples](https://github.com/yourusername/ESPAsyncWebClient/actions/workflows/build.yml/badge.svg)](https://github.com/yourusername/ESPAsyncWebClient/actions/workflows/build.yml)
[![Library Tests](https://github.com/yourusername/ESPAsyncWebClient/actions/workflows/test.yml/badge.svg)](https://github.com/yourusername/ESPAsyncWebClient/actions/workflows/test.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/yourusername/library/ESPAsyncWebClient.svg)](https://registry.platformio.org/libraries/yourusername/ESPAsyncWebClient)

An asynchronous HTTP client library for ESP32 microcontrollers, built on top of AsyncTCP. This library provides a simple and efficient way to make HTTP requests without blocking your main program execution.

## Features

- ✅ **Asynchronous HTTP requests** - Non-blocking HTTP operations
- ✅ **Multiple HTTP methods** - GET, POST, PUT, DELETE support
- ✅ **Custom headers** - Set global and per-request headers
- ✅ **Callback-based responses** - Success and error callbacks
- ✅ **ESP32 - Works on both platforms
- ✅ **Simple API** - Easy to use with minimal setup
- ✅ **Configurable timeouts** - Set custom timeout values
- ✅ **Multiple simultaneous requests** - Handle multiple requests concurrently

## Installation

### PlatformIO (Recommended)

Add to your `platformio.ini`:

```ini
lib_deps = 
    https://github.com/ESP32Async/AsyncTCP.git
    https://github.com/playmiel/ESPAsyncWebClient.git
```

### Arduino IDE

1. Download this repository as ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Install the dependencies:
   - For ESP32: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)


## Quick Start

```cpp
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }
    
    // Make a simple GET request
    client.get("http://httpbin.org/get", 
        [](AsyncHttpResponse* response) {
            Serial.printf("Success! Status: %d\n", response->getStatusCode());
            Serial.printf("Body: %s\n", response->getBody().c_str());
        },
        [](int error, const char* message) {
            Serial.printf("Error: %d - %s\n", error, message);
        }
    );
}

void loop() {
    delay(1000);
}
```

## API Reference

### AsyncHttpClient Class

#### HTTP Methods

```cpp
// GET request
void get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// POST request with data
void post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// PUT request with data
void put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// DELETE request
void del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// Advanced request (custom method, headers, etc.)
void request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
```

#### Configuration Methods

```cpp
// Set global default header
void setHeader(const char* name, const char* value);

// Set request timeout (milliseconds)
void setTimeout(uint32_t timeout);

// Set User-Agent string
void setUserAgent(const char* userAgent);
```

#### Callback Types

```cpp
typedef std::function<void(AsyncHttpResponse*)> SuccessCallback;
typedef std::function<void(int, const char*)> ErrorCallback;
```

### AsyncHttpResponse Class

```cpp
// Response status
int getStatusCode() const;
const String& getStatusText() const;

// Response headers
const String& getHeader(const String& name) const;
const std::vector<HttpHeader>& getHeaders() const;

// Response body
const String& getBody() const;
size_t getContentLength() const;

// Status helpers
bool isSuccess() const;    // 2xx status codes
bool isRedirect() const;   // 3xx status codes
bool isError() const;      // 4xx+ status codes
```

### AsyncHttpRequest Class (Advanced Usage)

```cpp
// Create custom request
AsyncHttpRequest request(HTTP_POST, "http://example.com/api");

// Set headers
request.setHeader("Content-Type", "application/json");
request.setHeader("Authorization", "Bearer token");

// Set body
request.setBody("{\"key\":\"value\"}");

// Set timeout
request.setTimeout(10000);

// Execute
client.request(&request, onSuccess, onError);
```

## Examples

### Simple GET Request

```cpp
client.get("http://api.example.com/data", 
    [](AsyncHttpResponse* response) {
        if (response->isSuccess()) {
            Serial.println("Data received:");
            Serial.println(response->getBody());
        }
    }
);
```

### POST with JSON Data

```cpp
client.setHeader("Content-Type", "application/json");
String jsonData = "{\"sensor\":\"temperature\",\"value\":25.5}";

client.post("http://api.example.com/sensor", jsonData.c_str(),
    [](AsyncHttpResponse* response) {
        Serial.printf("Posted data, status: %d\n", response->getStatusCode());
    }
);
```

### Multiple Simultaneous Requests

```cpp
// These requests will be made concurrently
client.get("http://api1.example.com/data", onSuccess1);
client.get("http://api2.example.com/data", onSuccess2);
client.post("http://api3.example.com/data", "payload", onSuccess3);
```

### Custom Headers

```cpp
// Set global headers (applied to all requests)
client.setHeader("X-API-Key", "your-api-key");
client.setUserAgent("MyDevice/1.0");

// Or set per-request headers
AsyncHttpRequest* request = new AsyncHttpRequest(HTTP_GET, "http://example.com");
request->setHeader("Authorization", "Bearer token");
client.request(request, onSuccess);
```

## Error Handling

Error codes passed to error callbacks:

- `-1`: Failed to initiate connection
- `-2`: Failed to parse response headers  
- `-3`: Connection closed before headers received
- `-4`: Request timeout
- `>0`: AsyncTCP error codes

```cpp
client.get("http://example.com", onSuccess,
    [](int error, const char* message) {
        switch(error) {
            case -1:
                Serial.println("Connection failed");
                break;
            case -4:
                Serial.println("Request timed out");
                break;
            default:
                Serial.printf("Network error: %d - %s\n", error, message);
        }
    }
);
```

## Configuration

### Global Settings

```cpp
// Set default timeout for all requests (10 seconds)
client.setTimeout(10000);

// Set default User-Agent
client.setUserAgent("ESP32-IoT-Device/1.0");

// Set default headers applied to all requests
client.setHeader("X-Device-ID", "esp32-001");
client.setHeader("Accept", "application/json");
```

### Per-Request Settings

```cpp
AsyncHttpRequest* request = new AsyncHttpRequest(HTTP_POST, url);
request->setTimeout(30000);  // 30 second timeout for this request
request->setHeader("Content-Type", "application/xml");
request->setBody(xmlData);
```

## Memory Management

- The library automatically manages memory for standard requests
- For advanced `AsyncHttpRequest` objects, the library takes ownership and will delete them
- Response objects are automatically cleaned up after callbacks complete
- No manual memory management required for typical usage

## Thread Safety

- The library is designed for single-threaded use (Arduino main loop)
- Callbacks are executed in the context of the network event loop
- Keep callback functions lightweight and non-blocking

## Dependencies

- **ESP32**: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP) 
- **Arduino Core**: ESP32 (v2.0+) 

> **Note**: This library uses the maintained ESP32Async fork of AsyncTCP, which is more up-to-date and better maintained than the original repository.

## Testing

### Dependency Testing

To test compatibility with different versions of AsyncTCP, use the provided test script:

```bash
./test_dependencies.sh
```

This script tests compilation with:
- AsyncTCP ESP32Async/main (development)
- AsyncTCP ESP32Async stable

### Manual Testing

You can also test individual environments:

```bash
# Test with development AsyncTCP
pio run -e esp32dev_asynctcp_dev

# Test with stable AsyncTCP
pio run -e test_asynctcp_stable

# Basic compilation test
pio run -e compile_test
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

- Create an issue on GitHub for bug reports or feature requests
- Check the examples directory for usage patterns
- Review the API documentation above for detailed information

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and changes.