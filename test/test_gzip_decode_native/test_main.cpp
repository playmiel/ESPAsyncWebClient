#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "GzipDecoder.h"

static const uint8_t kGzipHello[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51,
    0x48, 0xaf, 0xca, 0x2c, 0x50, 0xe4, 0x02, 0x00, 0x05, 0x14, 0xa6, 0xf3, 0x0d, 0x00, 0x00, 0x00,
};

static std::string decodeGzipInChunks(const std::vector<uint8_t>& gz, size_t chunkSize) {
    GzipDecoder dec;
    TEST_ASSERT_TRUE(dec.begin());

    std::string out;
    size_t offset = 0;
    while (offset < gz.size()) {
        size_t n = std::min(chunkSize, gz.size() - offset);
        size_t inner = 0;
        while (inner < n) {
            const uint8_t* outPtr = nullptr;
            size_t outLen = 0;
            size_t consumed = 0;
            GzipDecoder::Result r = dec.write(gz.data() + offset + inner, n - inner, &consumed, &outPtr, &outLen, true);
            if (outLen > 0) {
                out.append(reinterpret_cast<const char*>(outPtr), outLen);
            }
            TEST_ASSERT_NOT_EQUAL_MESSAGE(GzipDecoder::Result::kError, r, dec.lastError());
            TEST_ASSERT_TRUE_MESSAGE(consumed > 0 || outLen > 0, "decoder stalled");
            inner += consumed;
            if (r == GzipDecoder::Result::kNeedMoreInput && inner >= n) {
                break;
            }
        }
        offset += n;
    }

    for (;;) {
        const uint8_t* outPtr = nullptr;
        size_t outLen = 0;
        GzipDecoder::Result r = dec.finish(&outPtr, &outLen);
        if (outLen > 0) {
            out.append(reinterpret_cast<const char*>(outPtr), outLen);
        }
        if (r == GzipDecoder::Result::kDone)
            break;
        TEST_ASSERT_EQUAL_MESSAGE(GzipDecoder::Result::kOk, r, dec.lastError());
    }

    TEST_ASSERT_TRUE(dec.isDone());
    return out;
}

static void test_gzip_decode_single_chunk() {
    const std::vector<uint8_t> gz(kGzipHello, kGzipHello + sizeof(kGzipHello));
    std::string out = decodeGzipInChunks(gz, gz.size());
    TEST_ASSERT_EQUAL_STRING("Hello, gzip!\n", out.c_str());
}

static void test_gzip_decode_byte_by_byte() {
    const std::vector<uint8_t> gz(kGzipHello, kGzipHello + sizeof(kGzipHello));
    std::string out = decodeGzipInChunks(gz, 1);
    TEST_ASSERT_EQUAL_STRING("Hello, gzip!\n", out.c_str());
}

static void test_gzip_header_with_fname() {
    std::vector<uint8_t> gz(kGzipHello, kGzipHello + sizeof(kGzipHello));
    // Set FNAME flag and insert a null-terminated filename after the fixed 10-byte header.
    gz[3] |= 0x08;
    const char name[] = "x.txt";
    gz.insert(gz.begin() + 10, name, name + sizeof(name)); // includes trailing NUL

    std::string out = decodeGzipInChunks(gz, 3);
    TEST_ASSERT_EQUAL_STRING("Hello, gzip!\n", out.c_str());
}

static void test_truncated_gzip_fails() {
    const std::vector<uint8_t> gz(kGzipHello, kGzipHello + sizeof(kGzipHello));
    TEST_ASSERT_TRUE(gz.size() > 8);

    GzipDecoder dec;
    TEST_ASSERT_TRUE(dec.begin());

    const std::vector<uint8_t> truncated(gz.begin(), gz.end() - 3);
    size_t offset = 0;
    while (offset < truncated.size()) {
        const uint8_t* outPtr = nullptr;
        size_t outLen = 0;
        size_t consumed = 0;
        GzipDecoder::Result r =
            dec.write(truncated.data() + offset, truncated.size() - offset, &consumed, &outPtr, &outLen, true);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(GzipDecoder::Result::kError, r, dec.lastError());
        TEST_ASSERT_TRUE(consumed > 0 || outLen > 0);
        offset += consumed;
        if (r == GzipDecoder::Result::kNeedMoreInput)
            break;
    }

    const uint8_t* outPtr = nullptr;
    size_t outLen = 0;
    GzipDecoder::Result r = dec.finish(&outPtr, &outLen);
    TEST_ASSERT_EQUAL(GzipDecoder::Result::kError, r);
    TEST_ASSERT_TRUE(strlen(dec.lastError()) > 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_gzip_decode_single_chunk);
    RUN_TEST(test_gzip_decode_byte_by_byte);
    RUN_TEST(test_gzip_header_with_fname);
    RUN_TEST(test_truncated_gzip_fails);
    return UNITY_END();
}
