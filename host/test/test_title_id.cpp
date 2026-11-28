// Running program ids -> application id (games inside a collection, include/title_id.h).
#include "title_id.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    using namespace titleid;
    // KINGDOM HEARTS HD 1.5+2.5 ReMIX: launcher ...000, each game ...001, ...002...
    CHECK(applicationId(0x010090C022978000ULL) == 0x010090C022978000ULL);
    CHECK(applicationId(0x010090C022978001ULL) == 0x010090C022978000ULL);
    CHECK(applicationId(0x010090C022978005ULL) == 0x010090C022978000ULL);
    CHECK(programIndex(0x010090C022978003ULL) == 3);
    CHECK(programIndex(0x01007EF00011E000ULL) == 0);
    // single-program games stay as they are
    CHECK(applicationId(0x01007EF00011E000ULL) == 0x01007EF00011E000ULL);
    // not applications: hbmenu as an application, forwarders, system programs
    CHECK(applicationId(0x01ED3E3AECBF3000ULL) == 0x01ED3E3AECBF3000ULL);
    CHECK(applicationId(0x0500000000001234ULL) == 0x0500000000001234ULL);
    CHECK(applicationId(0x0100000000001000ULL) == 0x0100000000001000ULL);
    CHECK(applicationId(0) == 0);
    if (fails) return 1;
    puts("title_id: all tests passed");
    return 0;
}
