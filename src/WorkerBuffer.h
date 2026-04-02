#ifndef WORKER_BUFFER_H
#define WORKER_BUFFER_H

#ifdef ARDUINO_ARCH_ESP32

#include <cstddef>
#include <cstdint>
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
    static constexpr size_t kErrorMsgMaxLen = 64;
    char errorMsg[kErrorMsgMaxLen]{};  // only for Type::Error

    WorkerItem() = default;
    WorkerItem(const WorkerItem&) = delete;
    WorkerItem& operator=(const WorkerItem&) = delete;
    WorkerItem(WorkerItem&&) = default;
    WorkerItem& operator=(WorkerItem&&) = default;
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
