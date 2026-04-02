# Ring Buffer Worker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Isoler le traitement HTTP (`handleData`, `handleDisconnect`, `handleTransportError`) hors de `tcpip_thread` via un ring buffer + worker FreeRTOS unique, éliminant la corruption PSRAM.

**Architecture:** Les callbacks AsyncTCP (qui tournent dans `tcpip_thread`) copient les événements (data/disconnect/error) dans un `WorkerBuffer` protégé par mutex. Une tâche FreeRTOS dédiée consomme ce buffer et appelle les handlers HTTP. Le `WorkerBuffer` alloue dynamiquement depuis PSRAM (fallback DRAM), avec limite automatique doublée à 95% de remplissage.

**Tech Stack:** ESP32 / FreeRTOS, AsyncTCP, C++11, `heap_caps_malloc(MALLOC_CAP_SPIRAM)`

---

## Fichiers touchés

| Action | Fichier | Responsabilité |
|--------|---------|----------------|
| Créer | `src/WorkerBuffer.h` | Interface + types `WorkerItem` |
| Créer | `src/WorkerBuffer.cpp` | Implémentation buffer, mutex, semaphore |
| Modifier | `src/AsyncHttpClient.h` | Ajouter `_workerBuffer`, `_workerTaskHandle`, `_workerLoop` |
| Modifier | `src/AsyncHttpClient.cpp` | Constructeur/destructeur + `executeRequest` + `_workerLoop` |

`AsyncTransport.h`, `TcpTransport.cpp`, `TlsTransport.cpp`, `ConnectionPool`, API publique : **inchangés**.

---

## Task 1 : Créer `WorkerBuffer.h`

**Files:**
- Create: `src/WorkerBuffer.h`

- [ ] **Step 1 : Écrire `src/WorkerBuffer.h`**

```cpp
#ifndef WORKER_BUFFER_H
#define WORKER_BUFFER_H

#ifdef ARDUINO_ARCH_ESP32

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "HttpCommon.h"

#ifndef ASYNC_HTTP_RING_BUFFER_SIZE
#define ASYNC_HTTP_RING_BUFFER_SIZE 8192
#endif

#ifndef ASYNC_HTTP_RING_BUFFER_MAX
#define ASYNC_HTTP_RING_BUFFER_MAX 65536
#endif

struct WorkerItem {
    enum class Type { Data, Disconnect, Error };
    Type type;
    std::shared_ptr<void> ctx;  // type-erased shared_ptr<RequestContext>
    uint8_t* data    = nullptr; // heap_caps_malloc'd — only for Type::Data
    size_t   len     = 0;       // only for Type::Data
    HttpClientError errorCode = CONNECTION_FAILED; // only for Type::Error
    char errorMsg[64]{};        // only for Type::Error
};

class WorkerBuffer {
  public:
    explicit WorkerBuffer(size_t limitBytes = ASYNC_HTTP_RING_BUFFER_SIZE,
                          size_t maxBytes   = ASYNC_HTTP_RING_BUFFER_MAX);
    ~WorkerBuffer();

    // Called from tcpip_thread.
    // Returns false if buffer is at max capacity (caller should close transport).
    bool pushData(std::shared_ptr<void> ctx, const char* data, size_t len);
    void pushDisconnect(std::shared_ptr<void> ctx);
    void pushError(std::shared_ptr<void> ctx, HttpClientError code, const char* msg);

    // Called from worker task.
    // Returns false if queue is empty.
    bool pop(WorkerItem& out);

    // Blocks worker task until an item is available.
    void waitForItem();

    bool isEmpty() const;

  private:
    void enqueue(WorkerItem&& item);

    std::deque<WorkerItem> _queue;
    size_t _totalBytes  = 0;  // sum of data bytes currently queued
    size_t _limitBytes  = 0;  // current soft limit (doubles at 95%)
    size_t _maxBytes    = 0;  // hard ceiling
    SemaphoreHandle_t _mutex     = nullptr;
    SemaphoreHandle_t _semaphore = nullptr;
};

#endif // ARDUINO_ARCH_ESP32
#endif // WORKER_BUFFER_H
```

- [ ] **Step 2 : Commit**

```bash
git add src/WorkerBuffer.h
git commit -m "feat: add WorkerBuffer header (ring buffer worker)"
```

---

## Task 2 : Implémenter `WorkerBuffer.cpp`

**Files:**
- Create: `src/WorkerBuffer.cpp`

- [ ] **Step 1 : Écrire `src/WorkerBuffer.cpp`**

```cpp
#ifdef ARDUINO_ARCH_ESP32

#include "WorkerBuffer.h"
#include <esp_heap_caps.h>
#include <Arduino.h>

WorkerBuffer::WorkerBuffer(size_t limitBytes, size_t maxBytes)
    : _limitBytes(limitBytes), _maxBytes(maxBytes) {
    _mutex     = xSemaphoreCreateMutex();
    _semaphore = xSemaphoreCreateBinary();
}

WorkerBuffer::~WorkerBuffer() {
    // Free any remaining data buffers
    for (auto& item : _queue) {
        if (item.data) {
            heap_caps_free(item.data);
            item.data = nullptr;
        }
    }
    if (_mutex)     vSemaphoreDelete(_mutex);
    if (_semaphore) vSemaphoreDelete(_semaphore);
}

bool WorkerBuffer::pushData(std::shared_ptr<void> ctx, const char* data, size_t len) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Auto-grow limit at 95%
    if (_totalBytes + len > _limitBytes * 95 / 100 && _limitBytes < _maxBytes) {
        _limitBytes = (_limitBytes * 2 <= _maxBytes) ? _limitBytes * 2 : _maxBytes;
    }

    // Hard ceiling reached
    if (_totalBytes + len > _maxBytes) {
        xSemaphoreGive(_mutex);
        return false;
    }

    // Allocate from PSRAM, fallback to DRAM
    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf)
        buf = static_cast<uint8_t*>(malloc(len));
    if (!buf) {
        xSemaphoreGive(_mutex);
        return false;
    }
    memcpy(buf, data, len);

    WorkerItem item;
    item.type = WorkerItem::Type::Data;
    item.ctx  = std::move(ctx);
    item.data = buf;
    item.len  = len;
    _totalBytes += len;
    _queue.push_back(std::move(item));

    xSemaphoreGive(_mutex);
    xSemaphoreGive(_semaphore);
    return true;
}

void WorkerBuffer::pushDisconnect(std::shared_ptr<void> ctx) {
    WorkerItem item;
    item.type = WorkerItem::Type::Disconnect;
    item.ctx  = std::move(ctx);
    enqueue(std::move(item));
}

void WorkerBuffer::pushError(std::shared_ptr<void> ctx, HttpClientError code, const char* msg) {
    WorkerItem item;
    item.type      = WorkerItem::Type::Error;
    item.ctx       = std::move(ctx);
    item.errorCode = code;
    if (msg) {
        strncpy(item.errorMsg, msg, sizeof(item.errorMsg) - 1);
        item.errorMsg[sizeof(item.errorMsg) - 1] = '\0';
    }
    enqueue(std::move(item));
}

void WorkerBuffer::enqueue(WorkerItem&& item) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _queue.push_back(std::move(item));
    xSemaphoreGive(_mutex);
    xSemaphoreGive(_semaphore);
}

bool WorkerBuffer::pop(WorkerItem& out) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_queue.empty()) {
        xSemaphoreGive(_mutex);
        return false;
    }
    out = std::move(_queue.front());
    _queue.pop_front();
    if (out.type == WorkerItem::Type::Data)
        _totalBytes -= out.len;
    xSemaphoreGive(_mutex);
    return true;
}

void WorkerBuffer::waitForItem() {
    xSemaphoreTake(_semaphore, portMAX_DELAY);
}

bool WorkerBuffer::isEmpty() const {
    // Called only from worker task (single reader) — no lock needed
    return _queue.empty();
}

#endif // ARDUINO_ARCH_ESP32
```

- [ ] **Step 2 : Commit**

```bash
git add src/WorkerBuffer.cpp
git commit -m "feat: implement WorkerBuffer (PSRAM-backed, dynamic limit)"
```

---

## Task 3 : Modifier `AsyncHttpClient.h`

**Files:**
- Modify: `src/AsyncHttpClient.h`

- [ ] **Step 1 : Ajouter l'include et les membres**

Dans `src/AsyncHttpClient.h`, après la ligne `#include <AsyncTCP.h>` (ligne 21), ajouter :

```cpp
#ifdef ARDUINO_ARCH_ESP32
#include "WorkerBuffer.h"
#endif
```

Dans la section `private:` (après `std::unique_ptr<RedirectHandler> _redirectHandler;`), ajouter :

```cpp
#ifdef ARDUINO_ARCH_ESP32
    WorkerBuffer _workerBuffer;
    TaskHandle_t _workerTaskHandle = nullptr;
    static void _workerTaskThunk(void* param);
    void _workerLoop();
#endif
```

- [ ] **Step 2 : Commit**

```bash
git add src/AsyncHttpClient.h
git commit -m "feat: add WorkerBuffer and worker task to AsyncHttpClient header"
```

---

## Task 4 : Démarrer/arrêter la worker task dans `AsyncHttpClient.cpp`

**Files:**
- Modify: `src/AsyncHttpClient.cpp`

- [ ] **Step 1 : Démarrer la task dans le constructeur**

Dans `AsyncHttpClient::AsyncHttpClient()`, après la ligne `_redirectHandler.reset(new RedirectHandler(this));` (ligne 29), ajouter :

```cpp
#ifdef ARDUINO_ARCH_ESP32
    xTaskCreatePinnedToCore(
        _workerTaskThunk,   // entry
        "AsyncHttpWorker",  // name
        8192,               // stack words
        this,               // param
        2,                  // priority
        &_workerTaskHandle, // handle out
        tskNO_AFFINITY      // any core
    );
#endif
```

- [ ] **Step 2 : Arrêter la task dans le destructeur**

Dans `AsyncHttpClient::~AsyncHttpClient()`, au tout début du corps (avant le bloc `#if !ASYNC_TCP_HAS_TIMEOUT`), ajouter :

```cpp
#ifdef ARDUINO_ARCH_ESP32
    if (_workerTaskHandle) {
        TaskHandle_t h = _workerTaskHandle;
        _workerTaskHandle = nullptr;
        vTaskDelete(h);
    }
#endif
```

- [ ] **Step 3 : Implémenter `_workerTaskThunk` et `_workerLoop`**

Ajouter après le bloc `_autoLoopTaskThunk` (après la ligne 91) :

```cpp
#ifdef ARDUINO_ARCH_ESP32
void AsyncHttpClient::_workerTaskThunk(void* param) {
    static_cast<AsyncHttpClient*>(param)->_workerLoop();
}

void AsyncHttpClient::_workerLoop() {
    while (true) {
        _workerBuffer.waitForItem();
        WorkerItem item;
        while (_workerBuffer.pop(item)) {
            auto ctx = std::static_pointer_cast<RequestContext>(item.ctx);
            if (!ctx || ctx->cancelled.load()) {
                // Free data buffer if present
                if (item.data) {
                    heap_caps_free(item.data);
                    item.data = nullptr;
                }
                continue;
            }
            switch (item.type) {
                case WorkerItem::Type::Data:
                    handleData(ctx.get(), reinterpret_cast<char*>(item.data), item.len);
                    // heap_caps_free works for both heap_caps_malloc and malloc on ESP32
                    heap_caps_free(item.data);
                    item.data = nullptr;
                    break;
                case WorkerItem::Type::Disconnect:
                    handleDisconnect(ctx.get());
                    break;
                case WorkerItem::Type::Error:
                    handleTransportError(ctx.get(), item.errorCode, item.errorMsg);
                    break;
            }
        }
    }
}
#endif // ARDUINO_ARCH_ESP32
```

- [ ] **Step 4 : Ajouter `#include <esp_heap_caps.h>` en tête de fichier**

Dans `AsyncHttpClient.cpp`, après `#include "RedirectHandler.h"` (ligne 14) :

```cpp
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif
```

- [ ] **Step 5 : Commit**

```bash
git add src/AsyncHttpClient.cpp
git commit -m "feat: add worker task lifecycle and _workerLoop to AsyncHttpClient"
```

---

## Task 5 : Rerouter les callbacks dans `executeRequest`

**Files:**
- Modify: `src/AsyncHttpClient.cpp`

C'est le changement central : les trois lambdas `setDataHandler`, `setDisconnectHandler`, `setErrorHandler` (lignes 450-473) ne doivent plus appeler les handlers directement — elles poussent dans le `WorkerBuffer`.

- [ ] **Step 1 : Remplacer le lambda `setDataHandler` (ligne 450-457)**

Remplacer :
```cpp
    context->transport->setDataHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t, void* data, size_t len) {
            (void)t;
            if (ctxShared->cancelled.load())
                return;
            handleData(ctxShared.get(), static_cast<char*>(data), len);
        },
        nullptr);
```

Par :
```cpp
    context->transport->setDataHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t, void* data, size_t len) {
            if (ctxShared->cancelled.load())
                return;
#ifdef ARDUINO_ARCH_ESP32
            if (!_workerBuffer.pushData(ctxShared, static_cast<char*>(data), len)) {
                // Buffer at max capacity — close transport to trigger disconnect/error path
                if (t) t->close();
            }
#else
            (void)t;
            handleData(ctxShared.get(), static_cast<char*>(data), len);
#endif
        },
        nullptr);
```

- [ ] **Step 2 : Remplacer le lambda `setDisconnectHandler` (ligne 458-465)**

Remplacer :
```cpp
    context->transport->setDisconnectHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t) {
            (void)t;
            if (ctxShared->cancelled.load())
                return;
            handleDisconnect(ctxShared.get());
        },
        nullptr);
```

Par :
```cpp
    context->transport->setDisconnectHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t) {
            (void)t;
            if (ctxShared->cancelled.load())
                return;
#ifdef ARDUINO_ARCH_ESP32
            _workerBuffer.pushDisconnect(ctxShared);
#else
            handleDisconnect(ctxShared.get());
#endif
        },
        nullptr);
```

- [ ] **Step 3 : Remplacer le lambda `setErrorHandler` (ligne 466-473)**

Remplacer :
```cpp
    context->transport->setErrorHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t, HttpClientError error, const char* message) {
            (void)t;
            if (ctxShared->cancelled.load())
                return;
            handleTransportError(ctxShared.get(), error, message);
        },
        nullptr);
```

Par :
```cpp
    context->transport->setErrorHandler(
        [this, ctxShared](void* /*arg*/, AsyncTransport* t, HttpClientError error, const char* message) {
            (void)t;
            if (ctxShared->cancelled.load())
                return;
#ifdef ARDUINO_ARCH_ESP32
            _workerBuffer.pushError(ctxShared, error, message);
#else
            handleTransportError(ctxShared.get(), error, message);
#endif
        },
        nullptr);
```

- [ ] **Step 4 : Commit**

```bash
git add src/AsyncHttpClient.cpp
git commit -m "feat: route data/disconnect/error callbacks through WorkerBuffer"
```

---

## Task 6 : Ajouter `WorkerBuffer.cpp` au build system

**Files:**
- Modify: `library.json` (ou `CMakeLists.txt` si présent)

- [ ] **Step 1 : Vérifier que `library.json` inclut les sources**

```bash
cat library.json | grep -A5 '"sources"'
```

Si `library.json` utilise `"srcDir": "src"` (auto-discovery), aucune modification nécessaire — `WorkerBuffer.cpp` est détecté automatiquement.

Si une liste explicite de sources existe, ajouter `"src/WorkerBuffer.cpp"`.

- [ ] **Step 2 : Vérifier la compilation**

```bash
cd examples/platformio/SimpleGet && pio run -e esp32dev 2>&1 | tail -20
```

Résultat attendu : `SUCCESS` sans erreur de compilation.

- [ ] **Step 3 : Commit si library.json modifié**

```bash
git add library.json
git commit -m "build: add WorkerBuffer.cpp to build"
```

---

## Task 7 : Test sur device

Il n'y a pas de test natif pour la partie FreeRTOS du WorkerBuffer. Le test se fait sur device avec un exemple existant + monitoring.

- [ ] **Step 1 : Flasher l'exemple MultipleRequests**

```bash
cd examples/platformio/MultipleRequests && pio run -e esp32dev --target upload
```

- [ ] **Step 2 : Monitorer la sortie série**

```bash
pio device monitor --baud 115200
```

Résultat attendu :
- Requêtes HTTP complétées avec succès
- Aucun `abort()` / crash / heap corruption dans les logs

- [ ] **Step 3 : Test de charge — lancer simultanément AsyncWebServer + AsyncHttpClient**

Dans ton sketch de test existant (celui qui causait la corruption PSRAM), relancer le même scénario. Résultat attendu :
- Pas de corruption PSRAM
- `handleData` n'apparaît plus dans les stack traces liés à `tcpip_thread`

- [ ] **Step 4 : Commit du résultat**

```bash
git add -A
git commit -m "test: validate ring buffer worker on device — no PSRAM corruption"
```

---

## Résumé des commits attendus

1. `feat: add WorkerBuffer header (ring buffer worker)`
2. `feat: implement WorkerBuffer (PSRAM-backed, dynamic limit)`
3. `feat: add WorkerBuffer and worker task to AsyncHttpClient header`
4. `feat: add worker task lifecycle and _workerLoop to AsyncHttpClient`
5. `feat: route data/disconnect/error callbacks through WorkerBuffer`
6. `build: add WorkerBuffer.cpp to build` (si nécessaire)
7. `test: validate ring buffer worker on device — no PSRAM corruption`
