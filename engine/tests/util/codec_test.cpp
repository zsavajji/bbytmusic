#include "check.h"
#include "util/base64.h"
#include "util/ebml.h"
#include "util/sha1.h"
#include "util/sha256.h"

using namespace bbm;

static std::string dec(std::string_view s, bool *ok = nullptr) {
    std::vector<uint8_t> v;
    bool r = base64_decode(s, v);
    if (ok) *ok = r;
    return std::string(v.begin(), v.end());
}

int main() {
    CHECK_EQ(sha1_hex(""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK_EQ(sha1_hex("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHECK_EQ(sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));  // two blocks
    CHECK_EQ(sha1_hex(std::string(1000000, 'a')), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));

    CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));  // two blocks
    CHECK_EQ(sha256_hex("kJQP7kiw5Fk").substr(0, 4), std::string("f1d9"));

    CHECK_EQ(dec("Zm9vYmFy"), std::string("foobar"));
    CHECK_EQ(dec("Zm9vYg=="), std::string("foob"));
    CHECK_EQ(dec("Zm9vYg"), std::string("foob"));     // no padding
    CHECK_EQ(dec("Zm9vYmE"), std::string("fooba"));
    CHECK_EQ(dec(""), std::string());
    CHECK_EQ(dec("-_-_"), dec("+/+/"));               // both alphabets
    bool ok;
    dec("Zm9v!mFy", &ok); CHECK(!ok);
    dec("Z", &ok); CHECK(!ok);
    dec("Zm=vYg", &ok); CHECK(!ok);
    const uint8_t bin[] = {0xfb, 0xff, 0xfe, 0x00};
    CHECK_EQ(base64_encode(bin, 4), std::string("+//+AA=="));
    CHECK_EQ(base64url_encode(bin, 4), std::string("-__-AA"));
    CHECK_EQ(base64url_encode(bin, 4, true), std::string("-__-AA=="));
    CHECK_EQ(base64_encode(nullptr, 0), std::string());

    // EBML: Segment id (4 bytes), 1-, 2- and 8-byte sizes, unknown size, truncation.
    const uint8_t ids[] = {0x18, 0x53, 0x80, 0x67, 0x81, 0x40, 0x05, 0x01, 0, 0, 0, 0, 0, 0, 7, 0xFF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    size_t pos = 0;
    uint32_t id;
    uint64_t size;
    bool unk;
    CHECK(ebml_read_id(ids, pos, id));
    CHECK_EQ(id, uint32_t(0x18538067));
    CHECK_EQ(pos, size_t(4));
    CHECK(ebml_read_size(ids, pos, size, unk));
    CHECK(size == 1 && !unk);
    CHECK(ebml_read_size(ids, pos, size, unk));
    CHECK(size == 5 && !unk);
    CHECK(ebml_read_size(ids, pos, size, unk));
    CHECK(size == 7 && !unk);
    CHECK(ebml_read_size(ids, pos, size, unk));
    CHECK(unk);
    CHECK(ebml_read_size(ids, pos, size, unk));
    CHECK(unk);
    CHECK(!ebml_read_size(ids, pos, size, unk));      // at the end
    for (size_t len = 0; len < 4; len++) {            // truncated id
        size_t p = 0;
        CHECK(!ebml_read_id(std::span<const uint8_t>(ids, len), p, id));
    }
    const uint8_t cut[] = {0x01, 0, 0};               // 8-byte size, 3 bytes present
    pos = 0;
    CHECK(!ebml_read_size(cut, pos, size, unk));
    const uint8_t zero[] = {0};
    pos = 0;
    CHECK(!ebml_read_id(zero, pos, id));
    CHECK(!ebml_read_size(zero, pos, size, unk));
    return check_done();
}
