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
        strncpy(item.errorMsg, msg, WorkerItem::kErrorMsgMaxLen - 1);
        item.errorMsg[WorkerItem::kErrorMsgMaxLen - 1] = '\0';
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

#endif // ARDUINO_ARCH_ESP32
