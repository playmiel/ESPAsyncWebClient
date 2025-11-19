#include "AsyncTransport.h"
#include <Arduino.h>
#include <AsyncTCP.h>
#include <vector>
#include <cstring>

class AsyncTcpTransport : public AsyncTransport {
  public:
    AsyncTcpTransport();
    ~AsyncTcpTransport() override;

    void setConnectHandler(ConnectHandler handler, void* arg) override {
        _connectHandler = handler;
        _connectArg = arg;
    }
    void setDataHandler(DataHandler handler, void* arg) override {
        _dataHandler = handler;
        _dataArg = arg;
    }
    void setDisconnectHandler(DisconnectHandler handler, void* arg) override {
        _disconnectHandler = handler;
        _disconnectArg = arg;
    }
    void setErrorHandler(ErrorHandler handler, void* arg) override {
        _errorHandler = handler;
        _errorArg = arg;
    }
    void setTimeout(uint32_t timeoutMs) override {
#if ASYNC_TCP_HAS_TIMEOUT
        if (_client)
            _client->setTimeout(timeoutMs);
#else
        (void)timeoutMs;
#endif
    }
    void setTimeoutHandler(TimeoutHandler handler, void* arg) override {
#if ASYNC_TCP_HAS_TIMEOUT
        _timeoutHandler = handler;
        _timeoutArg = arg;
        if (_client) {
            _client->onTimeout([](void* arg, AsyncClient* c, uint32_t time) {
                (void)c;
                auto self = static_cast<AsyncTcpTransport*>(arg);
                if (self->_timeoutHandler)
                    self->_timeoutHandler(self->_timeoutArg, self, time);
            },
                               this);
        }
#else
        (void)handler;
        (void)arg;
#endif
    }
    bool connect(const char* host, uint16_t port) override {
        if (!_client)
            return false;
        return _client->connect(host, port);
    }
    size_t write(const char* data, size_t len) override {
        if (!_client)
            return 0;
        return _client->write(data, len);
    }
    bool canSend() const override {
        return _client && _client->canSend();
    }
    void close(bool now = false) override {
        (void)now;
        if (_client)
            _client->close();
    }
    bool isSecure() const override {
        return false;
    }
    bool isHandshaking() const override {
        return false;
    }
    uint32_t getHandshakeStartMs() const override {
        return 0;
    }
    uint32_t getHandshakeTimeoutMs() const override {
        return 0;
    }

  private:
    static void handleConnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_connectHandler)
            self->_connectHandler(self->_connectArg, self);
    }
    static void handleDataThunk(void* arg, AsyncClient* client, void* data, size_t len) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_dataHandler)
            self->_dataHandler(self->_dataArg, self, data, len);
    }
    static void handleDisconnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_disconnectHandler)
            self->_disconnectHandler(self->_disconnectArg, self);
    }
    static void handleErrorThunk(void* arg, AsyncClient* client, int8_t error) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_errorHandler)
            self->_errorHandler(self->_errorArg, self, CONNECTION_FAILED, "Network error");
        (void)error;
    }

    AsyncClient* _client;
    ConnectHandler _connectHandler = nullptr;
    DataHandler _dataHandler = nullptr;
    DisconnectHandler _disconnectHandler = nullptr;
    ErrorHandler _errorHandler = nullptr;
    TimeoutHandler _timeoutHandler = nullptr;
    void* _connectArg = nullptr;
    void* _dataArg = nullptr;
    void* _disconnectArg = nullptr;
    void* _errorArg = nullptr;
    void* _timeoutArg = nullptr;
};

AsyncTcpTransport::AsyncTcpTransport() {
    _client = new AsyncClient();
    _client->onConnect(handleConnectThunk, this);
    _client->onData(handleDataThunk, this);
    _client->onDisconnect(handleDisconnectThunk, this);
    _client->onError(handleErrorThunk, this);
}

AsyncTcpTransport::~AsyncTcpTransport() {
    if (_client) {
        _client->onConnect(nullptr, nullptr);
        _client->onData(nullptr, nullptr);
        _client->onDisconnect(nullptr, nullptr);
        _client->onError(nullptr, nullptr);
        _client->onTimeout(nullptr, nullptr);
        _client->close();
        delete _client;
        _client = nullptr;
    }
}

#if defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/version.h>

class AsyncTlsTransport : public AsyncTransport {
  public:
    explicit AsyncTlsTransport(const AsyncHttpTLSConfig& config);
    ~AsyncTlsTransport() override;

    void setConnectHandler(ConnectHandler handler, void* arg) override {
        _connectHandler = handler;
        _connectArg = arg;
    }
    void setDataHandler(DataHandler handler, void* arg) override {
        _dataHandler = handler;
        _dataArg = arg;
    }
    void setDisconnectHandler(DisconnectHandler handler, void* arg) override {
        _disconnectHandler = handler;
        _disconnectArg = arg;
    }
    void setErrorHandler(ErrorHandler handler, void* arg) override {
        _errorHandler = handler;
        _errorArg = arg;
    }
    void setTimeout(uint32_t timeoutMs) override {
#if ASYNC_TCP_HAS_TIMEOUT
        if (_client)
            _client->setTimeout(timeoutMs);
#else
        (void)timeoutMs;
#endif
    }
    void setTimeoutHandler(TimeoutHandler handler, void* arg) override {
#if ASYNC_TCP_HAS_TIMEOUT
        _timeoutHandler = handler;
        _timeoutArg = arg;
        if (_client) {
            _client->onTimeout([](void* arg, AsyncClient* c, uint32_t t) {
                (void)c;
                auto self = static_cast<AsyncTlsTransport*>(arg);
                if (self->_timeoutHandler)
                    self->_timeoutHandler(self->_timeoutArg, self, t);
            },
                               this);
        }
#else
        (void)handler;
        (void)arg;
#endif
    }
    bool connect(const char* host, uint16_t port) override;
    size_t write(const char* data, size_t len) override;
    bool canSend() const override;
    void close(bool now = false) override;
    bool isSecure() const override {
        return true;
    }
    bool isHandshaking() const override {
        return _state == State::Handshaking;
    }
    uint32_t getHandshakeStartMs() const override {
        return _handshakeStartMs;
    }
    uint32_t getHandshakeTimeoutMs() const override {
        return _config.handshakeTimeoutMs;
    }

  private:
    enum class State { Idle, TcpConnecting, Handshaking, Established, Closed, Failed };

    static void handleTcpConnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        static_cast<AsyncTlsTransport*>(arg)->handleTcpConnect();
    }
    static void handleTcpDataThunk(void* arg, AsyncClient* client, void* data, size_t len) {
        (void)client;
        static_cast<AsyncTlsTransport*>(arg)->handleTcpData(data, len);
    }
    static void handleTcpDisconnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        static_cast<AsyncTlsTransport*>(arg)->handleTcpDisconnect();
    }
    static void handleTcpErrorThunk(void* arg, AsyncClient* client, int8_t error) {
        (void)client;
        static_cast<AsyncTlsTransport*>(arg)->handleTcpError(error);
    }
    static int sslSend(void* ctx, const unsigned char* buf, size_t len);
    static int sslRecv(void* ctx, unsigned char* buf, size_t len);

    bool setupSsl();
    void handleTcpConnect();
    void handleTcpData(void* data, size_t len);
    void handleTcpDisconnect();
    void handleTcpError(int8_t err);
    void continueHandshake();
    void flushApplicationData();
    void fail(HttpClientError code, const char* message);
    void fail(HttpClientError code, const char* message, int detail);
    bool verifyPeerCertificate();
    bool verifyFingerprint();
    void resetBuffers();
    void shutdownClient();

    AsyncClient* _client;
    AsyncHttpTLSConfig _config;
    ConnectHandler _connectHandler = nullptr;
    DataHandler _dataHandler = nullptr;
    DisconnectHandler _disconnectHandler = nullptr;
    ErrorHandler _errorHandler = nullptr;
    TimeoutHandler _timeoutHandler = nullptr;
    void* _connectArg = nullptr;
    void* _dataArg = nullptr;
    void* _disconnectArg = nullptr;
    void* _errorArg = nullptr;
    void* _timeoutArg = nullptr;
    String _host;
    uint16_t _port = 0;
    State _state = State::Idle;
    uint32_t _handshakeStartMs = 0;

    std::vector<uint8_t> _encryptedBuffer;
    size_t _encryptedOffset = 0;
    std::vector<uint8_t> _fingerprintBytes;

    mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _sslConfig;
    mbedtls_x509_crt _caCert;
    mbedtls_x509_crt _clientCert;
    mbedtls_pk_context _clientKey;
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctrDrbg;
    bool _sslReady = false;
};

static int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static std::vector<uint8_t> parseFingerprintString(const String& text) {
    std::vector<uint8_t> bytes;
    int accum = -1;
    for (size_t i = 0; i < text.length(); ++i) {
        char ch = text.charAt(i);
        if (ch == ':' || ch == ' ' || ch == '-') {
            continue;
        }
        int v = hexValue(ch);
        if (v < 0) {
            bytes.clear();
            break;
        }
        if (accum == -1) {
            accum = v;
        } else {
            bytes.push_back(static_cast<uint8_t>((accum << 4) | v));
            accum = -1;
        }
    }
    if (accum != -1) {
        // Odd number of nibbles -> invalid
        bytes.clear();
    }
    return bytes;
}

AsyncTlsTransport::AsyncTlsTransport(const AsyncHttpTLSConfig& config) : _client(new AsyncClient()), _config(config) {
    _client->onConnect(handleTcpConnectThunk, this);
    _client->onData(handleTcpDataThunk, this);
    _client->onDisconnect(handleTcpDisconnectThunk, this);
    _client->onError(handleTcpErrorThunk, this);
    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_sslConfig);
    mbedtls_x509_crt_init(&_caCert);
    mbedtls_x509_crt_init(&_clientCert);
    mbedtls_pk_init(&_clientKey);
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_ctrDrbg);
    _fingerprintBytes = parseFingerprintString(_config.fingerprint);
}

AsyncTlsTransport::~AsyncTlsTransport() {
    shutdownClient();
    mbedtls_ssl_free(&_ssl);
    mbedtls_ssl_config_free(&_sslConfig);
    mbedtls_x509_crt_free(&_caCert);
    mbedtls_x509_crt_free(&_clientCert);
    mbedtls_pk_free(&_clientKey);
    mbedtls_ctr_drbg_free(&_ctrDrbg);
    mbedtls_entropy_free(&_entropy);
}

void AsyncTlsTransport::shutdownClient() {
    if (_client) {
        _client->onConnect(nullptr, nullptr);
        _client->onData(nullptr, nullptr);
        _client->onDisconnect(nullptr, nullptr);
        _client->onError(nullptr, nullptr);
        _client->onTimeout(nullptr, nullptr);
        _client->close();
        delete _client;
        _client = nullptr;
    }
}

bool AsyncTlsTransport::connect(const char* host, uint16_t port) {
    if (!_client)
        return false;
    _host = host;
    _port = port;
    _handshakeStartMs = millis();
    _state = State::TcpConnecting;
    return _client->connect(host, port);
}

void AsyncTlsTransport::handleTcpConnect() {
    _state = State::Handshaking;
    _handshakeStartMs = millis();
    resetBuffers();
    if (!setupSsl()) {
        fail(TLS_HANDSHAKE_FAILED, "Failed to initialize TLS");
        return;
    }
    continueHandshake();
}

void AsyncTlsTransport::resetBuffers() {
    _encryptedBuffer.clear();
    _encryptedOffset = 0;
}

bool AsyncTlsTransport::setupSsl() {
    const char* pers = "ESPAsyncWebClientTLS";
    int rc = mbedtls_ctr_drbg_seed(&_ctrDrbg, mbedtls_entropy_func, &_entropy, (const unsigned char*)pers, strlen(pers));
    if (rc != 0)
        return false;
    rc = mbedtls_ssl_config_defaults(&_sslConfig, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0)
        return false;
    mbedtls_ssl_conf_rng(&_sslConfig, mbedtls_ctr_drbg_random, &_ctrDrbg);
    mbedtls_ssl_conf_authmode(&_sslConfig, _config.insecure ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_REQUIRED);
    if (_config.caCert.length() > 0) {
        rc = mbedtls_x509_crt_parse(&_caCert, (const unsigned char*)_config.caCert.c_str(),
                                    _config.caCert.length() + 1);
        if (rc < 0)
            return false;
        mbedtls_ssl_conf_ca_chain(&_sslConfig, &_caCert, nullptr);
    }
    if (_config.clientCert.length() > 0 && _config.clientPrivateKey.length() > 0) {
        rc = mbedtls_x509_crt_parse(&_clientCert, (const unsigned char*)_config.clientCert.c_str(),
                                    _config.clientCert.length() + 1);
        if (rc < 0)
            return false;
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x03000000)
        rc = mbedtls_pk_parse_key(&_clientKey, (const unsigned char*)_config.clientPrivateKey.c_str(),
                                  _config.clientPrivateKey.length() + 1, nullptr, 0, mbedtls_ctr_drbg_random, &_ctrDrbg);
#else
        rc = mbedtls_pk_parse_key(&_clientKey, (const unsigned char*)_config.clientPrivateKey.c_str(),
                                  _config.clientPrivateKey.length() + 1, nullptr, 0);
#endif
        if (rc < 0)
            return false;
        rc = mbedtls_ssl_conf_own_cert(&_sslConfig, &_clientCert, &_clientKey);
        if (rc != 0)
            return false;
    }
    mbedtls_ssl_setup(&_ssl, &_sslConfig);
    if (_host.length() > 0)
        mbedtls_ssl_set_hostname(&_ssl, _host.c_str());
    mbedtls_ssl_set_bio(&_ssl, this, sslSend, sslRecv, nullptr);
    _sslReady = true;
    return true;
}

void AsyncTlsTransport::continueHandshake() {
    if (!_sslReady || _state != State::Handshaking)
        return;
    while (true) {
        int rc = mbedtls_ssl_handshake(&_ssl);
        if (rc == 0) {
            if (!verifyPeerCertificate())
                return;
            _state = State::Established;
            if (_connectHandler)
                _connectHandler(_connectArg, this);
            flushApplicationData();
            return;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            break;
        fail(TLS_HANDSHAKE_FAILED, "TLS handshake failed", rc);
        break;
    }
}

bool AsyncTlsTransport::verifyPeerCertificate() {
    if (_config.insecure)
        return true;
    uint32_t res = mbedtls_ssl_get_verify_result(&_ssl);
    if (res != 0) {
        fail(TLS_CERT_INVALID, "TLS certificate validation failed");
        return false;
    }
    if (!_fingerprintBytes.empty() && !verifyFingerprint()) {
        fail(TLS_FINGERPRINT_MISMATCH, "TLS fingerprint mismatch");
        return false;
    }
    return true;
}

bool AsyncTlsTransport::verifyFingerprint() {
    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(&_ssl);
    if (!peer)
        return false;
    uint8_t hash[32];
#if defined(MBEDTLS_VERSION_MAJOR) && (MBEDTLS_VERSION_MAJOR >= 3)
    if (mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0) != 0)
        return false;
#else
    mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0);
#endif
    if (_fingerprintBytes.size() != sizeof(hash))
        return false;
    return std::memcmp(hash, _fingerprintBytes.data(), sizeof(hash)) == 0;
}

void AsyncTlsTransport::handleTcpData(void* data, size_t len) {
    if (_state == State::Closed || _state == State::Failed)
        return;
    size_t current = _encryptedBuffer.size();
    _encryptedBuffer.resize(current + len);
    std::memcpy(_encryptedBuffer.data() + current, data, len);
    if (_state == State::Handshaking)
        continueHandshake();
    else if (_state == State::Established)
        flushApplicationData();
}

void AsyncTlsTransport::flushApplicationData() {
    if (_state != State::Established || !_dataHandler)
        return;
    uint8_t temp[512];
    while (true) {
        int rc = mbedtls_ssl_read(&_ssl, temp, sizeof(temp));
        if (rc > 0) {
            _dataHandler(_dataArg, this, temp, rc);
            continue;
        }
        if (rc == 0) {
            handleTcpDisconnect();
            break;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            break;
        fail(TLS_HANDSHAKE_FAILED, "TLS channel read failed", rc);
        break;
    }
}

void AsyncTlsTransport::handleTcpDisconnect() {
    if (_state == State::Closed || _state == State::Failed)
        return;
    _state = State::Closed;
    if (_disconnectHandler)
        _disconnectHandler(_disconnectArg, this);
}

void AsyncTlsTransport::handleTcpError(int8_t err) {
    (void)err;
    fail(CONNECTION_FAILED, "TLS transport error");
}

size_t AsyncTlsTransport::write(const char* data, size_t len) {
    if (_state != State::Established)
        return 0;
    size_t total = 0;
    while (total < len) {
        int rc = mbedtls_ssl_write(&_ssl, (const unsigned char*)data + total, len - total);
        if (rc > 0) {
            total += rc;
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        fail(TLS_HANDSHAKE_FAILED, "TLS write failed", rc);
        break;
    }
    return total;
}

bool AsyncTlsTransport::canSend() const {
    return _client && _client->canSend() && _state == State::Established;
}

void AsyncTlsTransport::close(bool now) {
    (void)now;
    if (_state == State::Established) {
        mbedtls_ssl_close_notify(&_ssl);
    }
    if (_client)
        _client->close();
    _state = State::Closed;
}

int AsyncTlsTransport::sslSend(void* ctx, const unsigned char* buf, size_t len) {
    auto self = static_cast<AsyncTlsTransport*>(ctx);
    if (!self->_client)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    if (!self->_client->canSend())
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    size_t sent = self->_client->write((const char*)buf, len);
    if (sent == 0)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return static_cast<int>(sent);
}

int AsyncTlsTransport::sslRecv(void* ctx, unsigned char* buf, size_t len) {
    auto self = static_cast<AsyncTlsTransport*>(ctx);
    size_t available = 0;
    if (self->_encryptedBuffer.size() > self->_encryptedOffset)
        available = self->_encryptedBuffer.size() - self->_encryptedOffset;
    if (available == 0)
        return MBEDTLS_ERR_SSL_WANT_READ;
    size_t take = len < available ? len : available;
    std::memcpy(buf, self->_encryptedBuffer.data() + self->_encryptedOffset, take);
    self->_encryptedOffset += take;
    if (self->_encryptedOffset >= self->_encryptedBuffer.size()) {
        self->_encryptedBuffer.clear();
        self->_encryptedOffset = 0;
    }
    return static_cast<int>(take);
}

void AsyncTlsTransport::fail(HttpClientError code, const char* message) {
    fail(code, message, 0);
}

void AsyncTlsTransport::fail(HttpClientError code, const char* message, int detail) {
    (void)detail;
    if (_state == State::Failed || _state == State::Closed)
        return;
    _state = State::Failed;
    if (_errorHandler)
        _errorHandler(_errorArg, this, code, message);
    if (_client)
        _client->close();
}

#else
class AsyncTlsTransport : public AsyncTransport {
  public:
    explicit AsyncTlsTransport(const AsyncHttpTLSConfig& config) {
        (void)config;
    }
    ~AsyncTlsTransport() override {}
    void setConnectHandler(ConnectHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setDataHandler(DataHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setDisconnectHandler(DisconnectHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setErrorHandler(ErrorHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setTimeout(uint32_t timeoutMs) override {
        (void)timeoutMs;
    }
    void setTimeoutHandler(TimeoutHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    bool connect(const char* host, uint16_t port) override {
        (void)host;
        (void)port;
        return false;
    }
    size_t write(const char* data, size_t len) override {
        (void)data;
        (void)len;
        return 0;
    }
    bool canSend() const override {
        return false;
    }
    void close(bool now = false) override {
        (void)now;
    }
    bool isSecure() const override {
        return true;
    }
    bool isHandshaking() const override {
        return false;
    }
    uint32_t getHandshakeStartMs() const override {
        return 0;
    }
    uint32_t getHandshakeTimeoutMs() const override {
        return 0;
    }
};
#endif

AsyncTransport* createTcpTransport() {
    return new AsyncTcpTransport();
}

AsyncTransport* createTlsTransport(const AsyncHttpTLSConfig& config) {
#if defined(ARDUINO_ARCH_ESP32)
    return new AsyncTlsTransport(config);
#else
    (void)config;
    return nullptr;
#endif
}
