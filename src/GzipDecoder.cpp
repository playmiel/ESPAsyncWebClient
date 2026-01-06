#include "GzipDecoder.h"

#if !ASYNC_HTTP_ENABLE_GZIP_DECODE

#include <string.h>

GzipDecoder::GzipDecoder()
    : _state(State::kError), _headerStage(HeaderStage::kFixed10), _error("Gzip decode disabled"), _fixedLen(0),
      _flags(0), _extraLenRead(0), _extraRemaining(0), _needName(false), _needComment(false), _needHcrc(false),
      _trailerLen(0), _crc32(0), _outSize(0), _dict(nullptr), _dictOfs(0), _decomp(nullptr) {
    memset(_fixed, 0, sizeof(_fixed));
    memset(_extraLenBytes, 0, sizeof(_extraLenBytes));
    memset(_trailer, 0, sizeof(_trailer));
}

GzipDecoder::~GzipDecoder() {}

void GzipDecoder::reset() {}

bool GzipDecoder::begin() {
    return false;
}

GzipDecoder::Result GzipDecoder::write(const uint8_t* in, size_t inLen, size_t* inConsumed, const uint8_t** outPtr,
                                       size_t* outLen, bool hasMoreInput) {
    (void)in;
    (void)inLen;
    (void)hasMoreInput;
    if (inConsumed)
        *inConsumed = 0;
    if (outPtr)
        *outPtr = nullptr;
    if (outLen)
        *outLen = 0;
    _error = "Gzip decode disabled (build with -DASYNC_HTTP_ENABLE_GZIP_DECODE=1)";
    return Result::kError;
}

GzipDecoder::Result GzipDecoder::finish(const uint8_t** outPtr, size_t* outLen) {
    if (outPtr)
        *outPtr = nullptr;
    if (outLen)
        *outLen = 0;
    _error = "Gzip decode disabled (build with -DASYNC_HTTP_ENABLE_GZIP_DECODE=1)";
    return Result::kError;
}

bool GzipDecoder::isDone() const {
    return false;
}

const char* GzipDecoder::lastError() const {
    return _error ? _error : "";
}

#else

#include <stdlib.h>
#include <string.h>

#include "third_party/miniz/miniz_tinfl.h"

static constexpr size_t kGzipFixedHeaderSize = 10;
static constexpr size_t kGzipTrailerSize = 8;
static constexpr size_t kTinflDictSize = 32768;

static constexpr uint8_t kGzipId1 = 0x1f;
static constexpr uint8_t kGzipId2 = 0x8b;
static constexpr uint8_t kGzipCmDeflate = 0x08;

static constexpr uint8_t kGzipFlagHcrc = 0x02;
static constexpr uint8_t kGzipFlagExtra = 0x04;
static constexpr uint8_t kGzipFlagName = 0x08;
static constexpr uint8_t kGzipFlagComment = 0x10;

static uint32_t readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t* crc32Table() {
    static bool inited = false;
    static uint32_t table[256];
    if (inited)
        return table;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (uint32_t k = 0; k < 8; ++k) {
            if (c & 1U)
                c = 0xEDB88320U ^ (c >> 1);
            else
                c >>= 1;
        }
        table[i] = c;
    }
    inited = true;
    return table;
}

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    uint32_t c = crc;
    uint32_t* t = crc32Table();
    for (size_t i = 0; i < len; ++i) {
        c = t[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c;
}

GzipDecoder::GzipDecoder()
    : _state(State::kHeader), _headerStage(HeaderStage::kFixed10), _error(nullptr), _fixedLen(0), _flags(0),
      _extraLenRead(0), _extraRemaining(0), _needName(false), _needComment(false), _needHcrc(false), _trailerLen(0),
      _crc32(0), _outSize(0), _dict(nullptr), _dictOfs(0), _decomp(nullptr) {
    memset(_fixed, 0, sizeof(_fixed));
    memset(_extraLenBytes, 0, sizeof(_extraLenBytes));
    memset(_trailer, 0, sizeof(_trailer));
}

GzipDecoder::~GzipDecoder() {
    reset();
}

void GzipDecoder::reset() {
    if (_dict) {
        free(_dict);
        _dict = nullptr;
    }
    if (_decomp) {
        free(_decomp);
        _decomp = nullptr;
    }

    _state = State::kHeader;
    _headerStage = HeaderStage::kFixed10;
    _error = nullptr;

    _fixedLen = 0;
    _flags = 0;
    _extraLenRead = 0;
    _extraRemaining = 0;
    _needName = false;
    _needComment = false;
    _needHcrc = false;

    _trailerLen = 0;
    _dictOfs = 0;
    _crc32 = 0xFFFFFFFFU;
    _outSize = 0;
}

bool GzipDecoder::begin() {
    reset();
    return true;
}

bool GzipDecoder::isDone() const {
    return _state == State::kDone;
}

const char* GzipDecoder::lastError() const {
    return _error ? _error : "";
}

void GzipDecoder::setError(const char* msg) {
    _error = msg ? msg : "gzip decode error";
    _state = State::kError;
}

bool GzipDecoder::initInflater() {
    if (_state == State::kError)
        return false;
    if (_decomp)
        return true;

    _dict = malloc(kTinflDictSize);
    if (!_dict) {
        setError("Out of memory (gzip dict)");
        return false;
    }
    memset(_dict, 0, kTinflDictSize);

    _decomp = malloc(sizeof(tinfl_decompressor));
    if (!_decomp) {
        setError("Out of memory (tinfl)");
        return false;
    }
    tinfl_init(static_cast<tinfl_decompressor*>(_decomp));
    _dictOfs = 0;
    return true;
}

GzipDecoder::Result GzipDecoder::consumeHeader(const uint8_t* in, size_t inLen, size_t* inConsumed) {
    if (!inConsumed)
        return Result::kError;
    *inConsumed = 0;

    while (*inConsumed < inLen && _state == State::kHeader) {
        switch (_headerStage) {
        case HeaderStage::kFixed10: {
            size_t remaining = inLen - *inConsumed;
            size_t need = kGzipFixedHeaderSize - _fixedLen;
            size_t take = remaining < need ? remaining : need;
            if (take > 0) {
                memcpy(_fixed + _fixedLen, in + *inConsumed, take);
                _fixedLen += take;
                *inConsumed += take;
            }
            if (_fixedLen < kGzipFixedHeaderSize) {
                return Result::kNeedMoreInput;
            }

            if (_fixed[0] != kGzipId1 || _fixed[1] != kGzipId2) {
                setError("Not a gzip stream");
                return Result::kError;
            }
            if (_fixed[2] != kGzipCmDeflate) {
                setError("Unsupported gzip compression method");
                return Result::kError;
            }

            _flags = _fixed[3];
            _needName = (_flags & kGzipFlagName) != 0;
            _needComment = (_flags & kGzipFlagComment) != 0;
            _needHcrc = (_flags & kGzipFlagHcrc) != 0;
            _headerStage = (_flags & kGzipFlagExtra) ? HeaderStage::kExtraLen : HeaderStage::kName;
            break;
        }
        case HeaderStage::kExtraLen: {
            while (*inConsumed < inLen && _extraLenRead < 2) {
                _extraLenBytes[_extraLenRead++] = in[(*inConsumed)++];
            }
            if (_extraLenRead < 2)
                return Result::kNeedMoreInput;
            _extraRemaining = (uint16_t)_extraLenBytes[0] | ((uint16_t)_extraLenBytes[1] << 8);
            _headerStage = HeaderStage::kExtraData;
            break;
        }
        case HeaderStage::kExtraData: {
            if (_extraRemaining == 0) {
                _headerStage = HeaderStage::kName;
                break;
            }
            size_t remaining = inLen - *inConsumed;
            size_t take = remaining < (size_t)_extraRemaining ? remaining : (size_t)_extraRemaining;
            _extraRemaining -= (uint16_t)take;
            *inConsumed += take;
            if (_extraRemaining > 0)
                return Result::kNeedMoreInput;
            _headerStage = HeaderStage::kName;
            break;
        }
        case HeaderStage::kName: {
            if (!_needName) {
                _headerStage = HeaderStage::kComment;
                break;
            }
            while (*inConsumed < inLen) {
                uint8_t c = in[(*inConsumed)++];
                if (c == 0) {
                    _needName = false;
                    break;
                }
            }
            if (_needName)
                return Result::kNeedMoreInput;
            _headerStage = HeaderStage::kComment;
            break;
        }
        case HeaderStage::kComment: {
            if (!_needComment) {
                _headerStage = HeaderStage::kHcrc;
                break;
            }
            while (*inConsumed < inLen) {
                uint8_t c = in[(*inConsumed)++];
                if (c == 0) {
                    _needComment = false;
                    break;
                }
            }
            if (_needComment)
                return Result::kNeedMoreInput;
            _headerStage = HeaderStage::kHcrc;
            break;
        }
        case HeaderStage::kHcrc: {
            if (!_needHcrc) {
                _headerStage = HeaderStage::kDone;
                break;
            }
            size_t remaining = inLen - *inConsumed;
            if (remaining < 2)
                return Result::kNeedMoreInput;
            *inConsumed += 2; // skip header CRC
            _needHcrc = false;
            _headerStage = HeaderStage::kDone;
            break;
        }
        case HeaderStage::kDone:
        default:
            _state = State::kInflate;
            if (!initInflater())
                return Result::kError;
            return Result::kOk;
        }
    }

    return (_state == State::kInflate) ? Result::kOk : Result::kNeedMoreInput;
}

GzipDecoder::Result GzipDecoder::consumeTrailer(const uint8_t* in, size_t inLen, size_t* inConsumed) {
    if (!inConsumed)
        return Result::kError;
    *inConsumed = 0;

    while (*inConsumed < inLen && _state == State::kTrailer) {
        size_t remaining = inLen - *inConsumed;
        size_t need = kGzipTrailerSize - _trailerLen;
        size_t take = remaining < need ? remaining : need;
        if (take > 0) {
            memcpy(_trailer + _trailerLen, in + *inConsumed, take);
            _trailerLen += take;
            *inConsumed += take;
        }
        if (_trailerLen < kGzipTrailerSize)
            return Result::kNeedMoreInput;
        uint32_t expectedCrc = readLe32(_trailer);
        uint32_t expectedISize = readLe32(_trailer + 4);
        uint32_t gotCrc = _crc32 ^ 0xFFFFFFFFU;
        uint32_t gotISize = _outSize;

        if (expectedCrc != gotCrc) {
            setError("Gzip CRC32 mismatch");
            return Result::kError;
        }
        if (expectedISize != gotISize) {
            setError("Gzip ISIZE mismatch");
            return Result::kError;
        }

        _state = State::kDone;
        return Result::kDone;
    }

    return (_state == State::kDone) ? Result::kDone : Result::kNeedMoreInput;
}

GzipDecoder::Result GzipDecoder::write(const uint8_t* in, size_t inLen, size_t* inConsumed, const uint8_t** outPtr,
                                       size_t* outLen, bool hasMoreInput) {
    if (!inConsumed || !outPtr || !outLen)
        return Result::kError;
    *inConsumed = 0;
    *outPtr = nullptr;
    *outLen = 0;

    if (_state == State::kError)
        return Result::kError;
    if (_state == State::kDone)
        return Result::kDone;

    size_t totalConsumed = 0;
    if (_state == State::kHeader) {
        size_t localConsumed = 0;
        Result hr = consumeHeader(in, inLen, &localConsumed);
        totalConsumed += localConsumed;
        *inConsumed = totalConsumed;
        if (hr == Result::kError || hr == Result::kDone)
            return hr;
        if (_state != State::kInflate)
            return hr;
        if (totalConsumed >= inLen)
            return Result::kOk;
        // fallthrough to inflate with remaining input
    }

    if (_state == State::kTrailer) {
        size_t localConsumed = 0;
        Result tr = consumeTrailer(in, inLen, &localConsumed);
        totalConsumed += localConsumed;
        *inConsumed = totalConsumed;
        return tr;
    }

    if (_state != State::kInflate) {
        setError("Invalid gzip state");
        return Result::kError;
    }

    if (!_decomp || !_dict) {
        setError("Inflater not initialized");
        return Result::kError;
    }

    tinfl_decompressor* decomp = static_cast<tinfl_decompressor*>(_decomp);
    mz_uint8* dict = static_cast<mz_uint8*>(_dict);

    const mz_uint8* inPtr = reinterpret_cast<const mz_uint8*>(in ? (in + totalConsumed) : nullptr);
    size_t srcBufSize = (inLen >= totalConsumed) ? (inLen - totalConsumed) : 0;
    size_t dstBufSize = kTinflDictSize - _dictOfs;
    int flags = hasMoreInput ? TINFL_FLAG_HAS_MORE_INPUT : 0;

    tinfl_status status = tinfl_decompress(decomp, inPtr, &srcBufSize, dict, dict + _dictOfs, &dstBufSize, flags);

    totalConsumed += srcBufSize;
    *inConsumed = totalConsumed;
    if (dstBufSize > 0) {
        const uint8_t* produced = reinterpret_cast<const uint8_t*>(dict + _dictOfs);
        *outPtr = produced;
        *outLen = dstBufSize;
        _crc32 = crc32Update(_crc32, produced, dstBufSize);
        _outSize += (uint32_t)dstBufSize;
        _dictOfs = (_dictOfs + dstBufSize) & (kTinflDictSize - 1);
    }

    if (status < 0) {
        setError("Deflate inflate failed");
        return Result::kError;
    }

    if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
        return Result::kNeedMoreInput;
    }
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        return Result::kOk;
    }
    if (status == TINFL_STATUS_DONE) {
        _state = State::kTrailer;
        if (totalConsumed < inLen) {
            size_t trailerConsumed = 0;
            Result tr = consumeTrailer(in + totalConsumed, inLen - totalConsumed, &trailerConsumed);
            totalConsumed += trailerConsumed;
            *inConsumed = totalConsumed;
            if (tr == Result::kDone)
                return Result::kDone;
            if (tr == Result::kError)
                return Result::kError;
        }
        return Result::kOk;
    }

    setError("Unexpected inflate status");
    return Result::kError;
}

GzipDecoder::Result GzipDecoder::finish(const uint8_t** outPtr, size_t* outLen) {
    size_t consumed = 0;
    Result r = write(nullptr, 0, &consumed, outPtr, outLen, false);

    if (r == Result::kNeedMoreInput) {
        if (_state == State::kHeader) {
            setError("Truncated gzip header");
            return Result::kError;
        }
        if (_state == State::kTrailer) {
            setError("Truncated gzip trailer");
            return Result::kError;
        }
        setError("Truncated gzip stream");
        return Result::kError;
    }

    return r;
}

#endif // ASYNC_HTTP_ENABLE_GZIP_DECODE
