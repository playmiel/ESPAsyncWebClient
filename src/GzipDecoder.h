#ifndef GZIP_DECODER_H
#define GZIP_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifndef ASYNC_HTTP_ENABLE_GZIP_DECODE
#define ASYNC_HTTP_ENABLE_GZIP_DECODE 0
#endif

class GzipDecoder {
  public:
    enum class Result {
        kOk,
        kNeedMoreInput,
        kDone,
        kError,
    };

    GzipDecoder();
    ~GzipDecoder();

    void reset();
    bool begin();

    Result write(const uint8_t* in, size_t inLen, size_t* inConsumed, const uint8_t** outPtr, size_t* outLen,
                 bool hasMoreInput);
    Result finish(const uint8_t** outPtr, size_t* outLen);

    bool isDone() const;
    const char* lastError() const;

  private:
    enum class State {
        kHeader,
        kInflate,
        kTrailer,
        kDone,
        kError,
    };

    enum class HeaderStage {
        kFixed10,
        kExtraLen,
        kExtraData,
        kName,
        kComment,
        kHcrc,
        kDone,
    };

    bool initInflater();
    Result consumeHeader(const uint8_t* in, size_t inLen, size_t* inConsumed);
    Result consumeTrailer(const uint8_t* in, size_t inLen, size_t* inConsumed);
    void setError(const char* msg);

    State _state;
    HeaderStage _headerStage;
    const char* _error;

    uint8_t _fixed[10];
    size_t _fixedLen;
    uint8_t _flags;
    uint8_t _extraLenBytes[2];
    size_t _extraLenRead;
    uint16_t _extraRemaining;
    bool _needName;
    bool _needComment;
    bool _needHcrc;

    uint8_t _trailer[8];
    size_t _trailerLen;

    uint32_t _crc32;
    uint32_t _outSize;

    // Deflate (tinfl) state
    void* _dict;
    size_t _dictOfs;
    void* _decomp; // tinfl_decompressor
};

#endif // GZIP_DECODER_H
