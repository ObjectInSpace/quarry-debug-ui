/* Offline tests for the Quarry Debug UI proxy.
 * usage: test_qdu.exe <path to built XAPOFX1_5.dll> [<game exe>]
 * Exit code = number of failed checks. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); \
                            else { printf("FAIL  %s\n", what); g_fail++; } } while (0)

/* The retail bytes at RVA 0x15ec241 (read from TheQuarry-Win64-Shipping.exe). */
static const BYTE k_retail[37] = {
    0x48,0x8b,0x43,0x20, 0x48,0x85,0xc0, 0x40,0x0f,0x95,0xc7, 0x48,0x03,0xf8,
    0x48,0x89,0x7b,0x20, 0x48,0x8b,0x5c,0x24,0x30, 0xc6,0x06,0x00,
    0x48,0x8b,0x74,0x24,0x40, 0x48,0x83,0xc4,0x20, 0x5f, 0xc3 };

typedef int (*pfn_patch)(BYTE *);
typedef HRESULT (__cdecl *pfn_createfx)(REFCLSID, void **, const void *, UINT32);

/* Code-like page holding the signature at offset 100, protected RX like .text. */
static BYTE *make_page(const BYTE *bytes, size_t n)
{
    BYTE *a = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(a, 0xcc, 4096);
    memcpy(a + 100, bytes, n);
    DWORD old;
    VirtualProtect(a, 4096, PAGE_EXECUTE_READ, &old);
    return a;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_qdu <XAPOFX1_5.dll>\n"); return 99; }
    HMODULE m = LoadLibraryA(argv[1]);
    CHECK(m != NULL, "the proxy loads");
    if (!m) return 99;
    pfn_patch patch = (pfn_patch)(void *)GetProcAddress(m, "qdu_patch_at");
    CHECK(patch != NULL, "qdu_patch_at is exported");
    if (!patch) return 99;

    /* 1. Retail bytes: exactly one byte changes, 0 -> 1, and the page stays RX. */
    BYTE *a = make_page(k_retail, sizeof k_retail);
    int r = patch(a + 100);
    BYTE want[37];
    memcpy(want, k_retail, sizeof want);
    want[25] = 1;
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(a + 100, &mbi, sizeof mbi);
    CHECK(r == 1, "retail bytes are patched (returns 1)");
    CHECK(!memcmp(a + 100, want, sizeof want), "only the result immediate changed, to 1");
    CHECK(a[99] == 0xcc && a[137] == 0xcc, "neighbouring bytes untouched");
    CHECK(mbi.Protect == PAGE_EXECUTE_READ, "page protection restored");

    /* 2. Running twice is harmless. */
    r = patch(a + 100);
    CHECK(r == 0 && !memcmp(a + 100, want, sizeof want), "second run reports already patched");

    /* 3. A different game version: any one differing byte is refused, nothing written.
     *    Every position is tried, so a check that skips part of the signature fails. */
    int refused_all = 1;
    for (int i = 0; i < 37; i++) {
        if (i == 25) continue;                     /* that byte is the value itself */
        BYTE other[37];
        memcpy(other, k_retail, sizeof other);
        other[i] ^= 0x01;
        BYTE *b = make_page(other, sizeof other);
        int rr = patch(b + 100);
        if (rr != -1 || memcmp(b + 100, other, sizeof other)) {
            printf("      position %d: returned %d\n", i, rr);
            refused_all = 0;
        }
        VirtualFree(b, 0, MEM_RELEASE);
    }
    CHECK(refused_all, "a changed byte at any signature position is refused untouched");

    /* 4. The value byte holding something other than 0 or 1 is also refused. */
    BYTE odd[37];
    memcpy(odd, k_retail, sizeof odd);
    odd[25] = 2;
    BYTE *c = make_page(odd, sizeof odd);
    r = patch(c + 100);
    CHECK(r == -1 && c[125] == 2, "an unexpected result value is refused");

    /* 5. Unmapped memory is refused, not a crash. */
    BYTE *d = VirtualAlloc(NULL, 4096, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(patch(d + 100) == -1, "reserved-but-uncommitted memory is refused");
    BYTE *e = make_page(k_retail, sizeof k_retail);
    CHECK(patch(e + 4096 - 20) == -1, "a signature running off the end of the region is refused");

    /* 6. CreateFX reaches the real DLL: an unknown CLSID gets the real DLL's error,
     *    not the proxy's own E_FAIL fallback. */
    pfn_createfx mine = (pfn_createfx)(void *)GetProcAddress(m, "CreateFX");
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat(sys, "\\XAPOFX1_5.dll");
    HMODULE real = LoadLibraryA(sys);
    pfn_createfx theirs = real ? (pfn_createfx)(void *)GetProcAddress(real, "CreateFX") : NULL;
    CHECK(mine && theirs, "CreateFX exported by both");
    if (mine && theirs) {
        static const GUID bogus = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
        void *o1 = (void *)1, *o2 = (void *)1;
        HRESULT h1 = mine(&bogus, &o1, NULL, 0);
        HRESULT h2 = theirs(&bogus, &o2, NULL, 0);
        printf("      proxy 0x%08lx, real 0x%08lx\n", (unsigned long)h1, (unsigned long)h2);
        CHECK(h1 == h2 && h1 != E_FAIL, "CreateFX is passed through to the real DLL");
    }

    /* 7. The signature is the one in the installed game exe (if given). The
     *    exe is mapped as an image, not run, so its bytes sit at their RVAs. */
    if (argc >= 3) {
        HMODULE exe = LoadLibraryExA(argv[2], NULL, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(exe != NULL, "the game exe maps for reading");
        if (exe) {
            BYTE *at = (BYTE *)exe + 0x15ec241;
            CHECK(!memcmp(at, k_retail, sizeof k_retail), "the signature matches the installed game at RVA 0x15ec241");
            FreeLibrary(exe);
        }
    }

    printf(g_fail ? "%d check(s) FAILED\n" : "all checks passed\n", g_fail);
    return g_fail;
}
