// SHA-256 used by the in-app updater (FIPS 180-4 test vectors).
#include "sha256.h"
#include <cstdio>
#include <string>
int main() {
    std::string m(1000000, 'a');
    struct { std::string in, out; } t[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {m, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"}};
    int fails = 0;
    for (auto& v : t) if (sha256Hex(v.in.data(), v.in.size()) != v.out) { printf("FAIL sha256 of %zu bytes\n", v.in.size()); fails++; }
    printf("sha256: %s\n", fails ? "FAILED" : "ok");
    return fails;
}
