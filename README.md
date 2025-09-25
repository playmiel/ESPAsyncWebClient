# ESPAsyncWebClient

[![Build Examples](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml)
[![Library Tests](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/playmiel/library/ESPAsyncWebClient.svg)](https://registry.platformio.org/libraries/playmiel/ESPAsyncWebClient)

An asynchronous HTTP client library for ESP32 microcontrollers, built on top of AsyncTCP. This library provides a simple and efficient way to make HTTP requests without blocking your main program execution.

> ⚠️ **HTTPS Warning**: Real TLS/HTTPS is NOT implemented yet. `https://` URLs are rejected with `HTTPS_NOT_SUPPORTED`. Do not use this library for sensitive data until TLS support is added.

## Features

- ✅ **Asynchronous HTTP requests** - Non-blocking HTTP operations
- ✅ **Multiple HTTP methods** - GET, POST, PUT, DELETE support
- ✅ **Custom headers** - Set global and per-request headers
- ✅ **Callback-based responses** - Success and error callbacks
- ✅ **ESP32 only** – (ESP8266 support removed since 1.0.1)
- ✅ **Simple API** - Easy to use with minimal setup
- ✅ **Configurable timeouts** - Set custom timeout values
- ✅ **Multiple simultaneous requests** - Handle multiple requests concurrently
- ⚠️ **Basic chunked transfer decoding** - Simple implementation (no trailers)

> ⚠ Limitations: HTTPS not implemented; chunked decoding is minimal (no trailers); full body is buffered in memory (no zero-copy streaming yet).

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
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        }
    );
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    client.loop();
#endif
    delay(1000);
}
```

If your AsyncTCP library does not provide native timeout support (`setTimeout`),
remember to call `client.loop()` regularly to handle manual timeout checks.

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
typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
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

- `CONNECTION_FAILED (-1)`: Failed to initiate connection
- `HEADER_PARSE_FAILED (-2)`: Failed to parse response headers
- `CONNECTION_CLOSED (-3)`: Connection closed before headers received
- `REQUEST_TIMEOUT (-4)`: Request timeout
- `HTTPS_NOT_SUPPORTED (-5)`: HTTPS not implemented yet
- `CHUNKED_DECODE_FAILED (-6)`: Failed to decode chunked body
- `>0`: AsyncTCP error codes

```cpp
client.get("http://example.com", onSuccess,
    [](HttpClientError error, const char* message) {
        switch(error) {
            case CONNECTION_FAILED:
                Serial.println("Connection failed");
                break;
            case REQUEST_TIMEOUT:
                Serial.println("Request timed out");
                break;
            default:
                Serial.printf("Network error: %s (%d)\n", httpClientErrorToString(error), (int)error);
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

> IMPORTANT: The `AsyncHttpResponse*` pointer passed to the success callback is ONLY valid during that callback. Do not store it or references to its internal `String` objects. Copy what you need.

### Body Streaming (experimental)

Register a global streaming callback via:

```cpp
client.onBodyChunk([](const char* data, size_t len, bool final) {
    // data may be nullptr & len==0 when final==true and no trailing bytes
});
```

Parameters:

- `data`, `len`: received segment (for chunked: decoded chunk payload; for non-chunked: raw slice). When `final==true` and no extra bytes, `data` can be `nullptr`.
- `final`: true when the whole response body is complete.

Notes:

- Invoked for every segment (chunk or contiguous data block)
- The full body is still accumulated internally (future option may allow disabling accumulation)
- `final` is invoked just before the success callback
- Keep it lightweight (avoid blocking operations)


### Content-Length and over / under delivery

If `Content-Length` is present, the response is considered complete once that many bytes have been received. Extra bytes (if a misbehaving server sends more) are ignored. Without `Content-Length`, completion is determined by connection close.

### Transfer-Encoding: chunked

Minimal chunked decoding is implemented.

Limitations:

- No trailer support (ignored if present)
- No advanced validation (extensions, checksums)
- On parse failure you get `CHUNKED_DECODE_FAILED`

### HTTPS

`https://` URLs return `HTTPS_NOT_SUPPORTED`. To add TLS later, wrap or replace `AsyncClient` with a secure implementation.

## Thread Safety

- The library is designed for single-threaded use (Arduino main loop)
- Callbacks are executed in the context of the network event loop
- Keep callback functions lightweight and non-blocking

## Dependencies

- **ESP32**: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)
- **Arduino Core**: ESP32 (v2.0+)

> **Note**: ESP8266 was mentioned in early docs but is no longer supported as of 1.0.1. The code exclusively targets AsyncTCP (ESP32).

## Supported Platforms

- Current target: **ESP32** only
- ESP8266: removed (no conditional code path retained)

## Current Limitations (summary)

- No TLS (HTTPS rejected)
- Chunked: minimal (no trailers)
- Full in-memory buffering (even with streaming hook)
- No automatic redirects (3xx not followed)
- Pas de **keep-alive prolongé** : en-tête par défaut `Connection: close`; aucune réutilisation de connexion.
- Timeout manuel requis si la version AsyncTCP utilisée ne fournit pas `setTimeout` (mettre `client.loop()` dans `loop()`).
- Aucune gestion spécifique des encodages de contenu (gzip/deflate ignorés si envoyés).

## Cycle de vie des objets / Ownership

1. `AsyncHttpClient::makeRequest()` crée un `AsyncHttpRequest` dynamique (ou vous passez le vôtre à `request()`).
2. `request()` alloue un `RequestContext`, un `AsyncHttpResponse` et un `AsyncClient`.
3. Connexion ouverte → envoi de la requête HTTP construite (`buildHttpRequest()`).
4. Réception: tampon de headers jusqu'à `\r\n\r\n`, puis accumulation body.
5. Sur succès complet : callback succès appelé avec un pointeur `AsyncHttpResponse*` (valable uniquement pendant le callback).
6. Sur erreur ou après callback succès : `cleanup()` détruit `AsyncClient`, `AsyncHttpRequest`, `AsyncHttpResponse`, `RequestContext`.
7. Ne **pas** conserver de pointeur / référence après retour du callback (dangling pointer garanti).

Pour fournir un corps très volumineux ou un streaming, il faudra insérer un hook dans `handleData` après `headersComplete` avant `appendBody`.

## Codes d'erreur

Erreurs négatives définies (enum `HttpClientError`):

| Code | Nom                  | Signification |
|------|----------------------|---------------|
| -1   | `CONNECTION_FAILED`  | Échec d'initiation de la connexion TCP |
| -2   | `HEADER_PARSE_FAILED`| Format de réponse invalide avant fin d'en-têtes |
| -3   | `CONNECTION_CLOSED`  | Connexion fermée avant réception headers complets |
| -4   | `REQUEST_TIMEOUT`    | Délai dépassé (timeout natif ou boucle manuelle) |

Codes positifs : valeurs directes retournées par AsyncTCP (erreurs réseau bas niveau) transmises inchangées; utiliser le code numérique et un logging réseau approprié.

Exemple de mapping dans un callback :

```cpp
client.get("http://example.com", 
  [](AsyncHttpResponse* r) {
      Serial.printf("OK %d %s\n", r->getStatusCode(), r->getStatusText().c_str());
  },
  [](HttpClientError e, const char* msg) {
      switch (e) {
          case CONNECTION_FAILED: Serial.println("TCP connect failed"); break;
          case HEADER_PARSE_FAILED: Serial.println("Bad HTTP header"); break;
          case CONNECTION_CLOSED: Serial.println("Closed early"); break;
          case REQUEST_TIMEOUT: Serial.println("Timeout"); break;
          default: Serial.printf("AsyncTCP error pass-through: %d\n", (int)e); break;
      }
  }
);
```

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

- Added: HEAD, PATCH

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

- Streaming request body (no-copy) via setBodyStream
- Global and per-request body chunk callbacks
- Basic Auth helper (request->setBasicAuth)
- Query param builder (addQueryParam/finalizeQueryParams)
- Optional Accept-Encoding: gzip (no automatic decompression yet)
- Separate connect timeout and total timeout
- Optional request queue limiting parallel connections (setMaxParallel)

- Create an issue on GitHub for bug reports or feature requests
- Check the examples directory for usage patterns
- Review the API documentation above for detailed information

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and changes.
