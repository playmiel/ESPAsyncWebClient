# Design : AsyncTCP + Ring Buffer Worker

**Date :** 2026-04-02
**Contexte :** Corruption PSRAM lors de flux lourds entre AsyncWebServer, SSH sockets et AsyncWebClient — tous partagent `tcpip_thread`. Le parsing HTTP et les allocations PSRAM se font actuellement dans les callbacks AsyncTCP, directement dans `tcpip_thread`.

---

## Problème

`handleData` (parsing HTTP complet, allocations PSRAM) est appelé depuis le callback `onData` d'AsyncTCP, qui s'exécute dans `tcpip_thread`. Quand AsyncWebServer, SSH et AsyncWebClient tournent simultanément, leurs callbacks entrent en compétition sur les ressources lwIP et PSRAM → corruption silencieuse.

---

## Solution

Insérer un **ring buffer + worker task** entre les callbacks AsyncTCP et le traitement HTTP. `tcpip_thread` ne fait plus que copier les bytes dans le buffer et signaler le worker. Tout le traitement lourd se fait hors `tcpip_thread`.

---

## Architecture

```
tcpip_thread (AsyncTCP onData callback)
  └→ écrit frame [context_ptr (4B) | len (2B) | data (len B)] dans ring buffer
     si ≥95% plein → tente de grandir (×2, jusqu'à ASYNC_HTTP_RING_BUFFER_MAX)
     si max atteint → pause TCP ACK (back-pressure lwIP)
     xSemaphoreGive(workerSemaphore)

Worker task (tâche FreeRTOS unique, tskNO_AFFINITY, priorité 2)
  └→ xSemaphoreTake(workerSemaphore, portMAX_DELAY)
     lit frame du ring buffer
     si TCP ACK était en pause → le reprend
     appelle handleData(context, data, len)   ← code existant inchangé
```

---

## Composants

### Nouveau : `src/WorkerBuffer.h` / `src/WorkerBuffer.cpp`

Ring buffer circulaire thread-safe.

**Format d'une frame :**
```
| shared_ptr<RequestContext> (8B) | len : 2 bytes | data : len bytes |
```
Le `shared_ptr` maintient le context en vie pendant le transit dans le buffer — pas de dangling pointer possible.

**Mémoire :**
- Allocation primaire : PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`)
- Fallback : DRAM interne si PSRAM absente ou allocation échouée

**Croissance dynamique :**
- Seuil : 95% de remplissage
- Stratégie : réallocation ×2, copie circulaire → linéaire
- Max : `ASYNC_HTTP_RING_BUFFER_MAX` (défaut 64KB, configurable au compile-time)
- Si max atteint : retourne `false` → déclenche back-pressure

**Synchronisation :**
- `SemaphoreHandle_t _mutex` (mutex FreeRTOS) — protège read/write
- `SemaphoreHandle_t _semaphore` (binaire) — réveille le worker

**Interface publique :**
```cpp
class WorkerBuffer {
public:
    explicit WorkerBuffer(size_t initialSize = ASYNC_HTTP_RING_BUFFER_SIZE);
    ~WorkerBuffer();

    // Écrit une frame depuis tcpip_thread. Retourne false si buffer plein (back-pressure).
    bool push(std::shared_ptr<RequestContext> ctx, const char* data, size_t len);

    // Lit la prochaine frame. Retourne false si vide.
    bool pop(std::shared_ptr<RequestContext>& ctxOut, std::vector<uint8_t>& dataOut);

    // Attend qu'une frame soit disponible (bloquant, pour le worker task).
    bool waitForData(TickType_t timeout = portMAX_DELAY);

    bool isEmpty() const;
    size_t capacity() const;
    size_t used() const;
};
```

### Modifié : `src/AsyncHttpClient.h`

Ajouts :
```cpp
#include "WorkerBuffer.h"

// Dans la section private :
WorkerBuffer _workerBuffer;
TaskHandle_t _workerTaskHandle = nullptr;

static void _workerTaskThunk(void* param);
void _workerLoop();
```

### Modifié : `src/AsyncHttpClient.cpp`

**Dans `executeRequest` :** le `setDataHandler` n'appelle plus `handleData` directement :
```cpp
context->transport->setDataHandler(
    [](void*, AsyncTransport* t, void* data, size_t len) {
        // tcpip_thread : juste copier dans le buffer
        if (!_workerBuffer.push(ctxShared, static_cast<char*>(data), len)) {
            // Buffer plein au max → back-pressure : pause ACK
            t->pauseAck();
        }
    }, nullptr);
```

**Worker task :**
```cpp
void AsyncHttpClient::_workerLoop() {
    while (true) {
        _workerBuffer.waitForData();
        std::shared_ptr<RequestContext> ctx;
        std::vector<uint8_t> data;
        while (_workerBuffer.pop(ctx, data)) {
            if (ctx && !ctx->cancelled.load()) {
                handleData(ctx.get(), (char*)data.data(), data.size());
                // Reprendre ACK si était en pause
                if (ctx->transport) ctx->transport->resumeAck();
            }
        }
    }
}
```

**Dans le constructeur :**
```cpp
xTaskCreatePinnedToCore(_workerTaskThunk, "AsyncHttpWorker",
                        8192, this, 2, &_workerTaskHandle, tskNO_AFFINITY);
```

### Inchangé

- `TcpTransport.cpp` / `TlsTransport.cpp`
- `ConnectionPool`, `RedirectHandler`, `AsyncCookieJar`
- `HttpRequest`, `HttpResponse`, `HttpHelpers`
- API publique complète (`get`, `post`, `request`, callbacks...)

---

## Paramètres compile-time

| Macro | Défaut | Description |
|-------|--------|-------------|
| `ASYNC_HTTP_RING_BUFFER_SIZE` | `8192` | Taille initiale du ring buffer (bytes) |
| `ASYNC_HTTP_RING_BUFFER_MAX` | `65536` | Taille maximale avant back-pressure |

---

## Back-pressure

Quand le ring buffer atteint sa taille max :
- Le callback `onData` retourne `false`
- `AsyncTcpTransport` appelle `_client->stop()` → lwIP ne ACK plus les segments reçus
- Le sender TCP ralentit naturellement (fenêtre TCP = 0)
- Quand le worker vide le buffer, `_client->start()` reprend les ACK

Nécessite d'exposer `pauseAck()` / `resumeAck()` dans `AsyncTransport` (2 méthodes virtuelles supplémentaires avec implémentation no-op par défaut).

---

## Garanties

- `tcpip_thread` ne fait jamais d'allocation PSRAM lourde
- `handleData` s'exécute toujours hors `tcpip_thread`
- N connexions parallèles supportées (AsyncTCP inchangé)
- API publique 100% compatible avec l'existant
- Tests natifs (PC) non affectés (`#ifdef ARDUINO_ARCH_ESP32`)
