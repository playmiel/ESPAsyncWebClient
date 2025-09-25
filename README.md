# ESPAsyncWebClient

[![Build Examples](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml)
[![Library Tests](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/playmiel/library/ESPAsyncWebClient.svg)](https://registry.platformio.org/libraries/playmiel/ESPAsyncWebClient)

An asynchronous HTTP client library for ESP32 microcontrollers, built on top of AsyncTCP. This library provides a simple and efficient way to make HTTP requests without blocking your main program execution.

> ⚠️ **Avertissement HTTPS**: La connexion TLS/HTTPS réelle n'est pas encore implémentée. Les URLs `https://` sont simplement reconnues pour parser le port (443) et le flag `secure`, mais aucune négociation TLS n'est effectuée. N'utilisez pas cette bibliothèque pour transmettre des données sensibles tant que le support TLS n'est pas ajouté.

## Features

- ✅ **Asynchronous HTTP requests** - Non-blocking HTTP operations
- ✅ **Multiple HTTP methods** - GET, POST, PUT, DELETE support
- ✅ **Custom headers** - Set global and per-request headers
- ✅ **Callback-based responses** - Success and error callbacks
- ✅ **ESP32 only** – (ESP8266 retiré depuis 1.0.1)
- ✅ **Simple API** - Easy to use with minimal setup
- ✅ **Configurable timeouts** - Set custom timeout values
- ✅ **Multiple simultaneous requests** - Handle multiple requests concurrently
- ⚠️ **Basic chunked transfer decoding** - Simple implementation (no trailers)

> ⚠ Limitations: HTTPS n'est pas encore implémenté (les URLs https:// retournent une erreur), la gestion chunked est volontairement minimale (pas de trailers ni décompression), et de très grandes réponses sont entièrement mises en mémoire (pas de streaming progressif pour l'instant).

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

> IMPORTANT: Le pointeur `AsyncHttpResponse*` passé au callback succès n'est valide QUE pendant l'exécution du callback. Ne le stockez pas, ne gardez pas de références vers le `String` du body ou les headers après retour. Copiez les données si nécessaire.

### Streaming du corps (expérimental)

Vous pouvez enregistrer un callback global via `client.onBodyChunk([](const char* data, size_t len, bool final){ ... });`.

Paramètres:

- `data`, `len`: segment reçu (si `final==true` et aucune donnée supplémentaire, `data` peut être `nullptr` et `len==0`)
- `final`: true lorsque la réponse est totalement reçue

Notes:

- Appelé pour chaque segment (chunk HTTP ou portion brute non-chunked)
- Le corps complet est toujours assemblé dans `AsyncHttpResponse` (optimisation future possible pour éviter double stockage)
- Le callback `final` est invoqué juste avant le callback succès
- Ne faites pas d'opérations bloquantes dans ce callback


### Content-Length et corps tronqués / excédentaires

Si l'en-tête `Content-Length` est présent, la réponse est considérée complète dès que ce nombre d'octets est reçu. Des octets supplémentaires éventuellement envoyés par un serveur mal configuré seront ignorés. Si le serveur ferme la connexion sans `Content-Length`, le corps reçu jusqu'à la fermeture est retourné.

### Transfer-Encoding: chunked

Une implémentation simple du décodage chunked est fournie. Limitations:

- Pas de prise en charge des trailers (ils sont ignorés)
- Pas de validation avancée (CRC, extensions de chunk, etc.)
- En cas d'erreur de décodage : erreur `CHUNKED_DECODE_FAILED`

### HTTPS

Les URLs `https://` retournent actuellement l'erreur `HTTPS_NOT_SUPPORTED`. Pour ajouter TLS, il faudra remplacer ou encapsuler `AsyncClient` par une variante sécurisée.

## Thread Safety

- The library is designed for single-threaded use (Arduino main loop)
- Callbacks are executed in the context of the network event loop
- Keep callback functions lightweight and non-blocking

## Dependencies

- **ESP32**: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)
- **Arduino Core**: ESP32 (v2.0+)

> **Note**: ESP8266 was mentioned in early docs but is no longer supported as of 1.0.1. The code exclusively targets AsyncTCP (ESP32).

## Plateformes supportées

- Cible actuelle : **ESP32** (plateforme PlatformIO `espressif32`).
- ESP8266 : retiré (absence d'inclusion conditionnelle `ESPAsyncTCP.h`, pas de code de compatibilité, tests uniquement ESP32).
- Si une réintégration multi‑plateforme est souhaitée plus tard : introduire des `#ifdef` sur core architecture + adapter l'abstraction client.

## Limitations actuelles

- Pas de **TLS/HTTPS** (voir avertissement en tête). Champ `_secure` uniquement informatif.
- Pas de **décodage chunked** (`Transfer-Encoding: chunked` ignoré : le corps sera concaténé brut jusqu'à fermeture ou `Content-Length`).
- Pas de **streaming incrémental** utilisateur : tout le corps est accumulé en mémoire (`String`). Risque de fragmentation / OOM pour grosses réponses.
- Pas de **redirections automatiques** (3xx non suivis, à gérer manuellement si besoin).
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

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

- Create an issue on GitHub for bug reports or feature requests
- Check the examples directory for usage patterns
- Review the API documentation above for detailed information

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and changes.
