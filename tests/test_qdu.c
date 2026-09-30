/* Offline tests for the Quarry Debug UI proxy.
 * usage: test_qdu.exe <path to built XAPOFX1_5.dll> [<game exe>]
 * Exit code = number of failed checks. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
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
    typedef int (*pfn_ucheck)(const BYTE *);
    pfn_ucheck ucheck = (pfn_ucheck)(void *)GetProcAddress(m, "qdu_unmute_check");
    if (argc >= 3) {
        HMODULE exe = LoadLibraryExA(argv[2], NULL, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(exe != NULL, "the game exe maps for reading");
        if (exe) {
            BYTE *at = (BYTE *)exe + 0x15ec241;
            CHECK(!memcmp(at, k_retail, sizeof k_retail), "the signature matches the installed game at RVA 0x15ec241");
            CHECK(ucheck && ucheck((BYTE *)exe) == 1, "the sound fix's three places match the installed game");
            FreeLibrary(exe);
        }
    }

    /* 8. The sound fix, end to end, on a stand-in for the game's code: the
     *    three places built at their RVAs in executable memory, hooked through
     *    the DLL's own install path. */
    {
        enum { MUTE = 0x14ee600, CALLSITE = 0x157c23c, MP = 0x0e0b940, SIZE = 0x1600000 };
        typedef int (*pfn_uinst)(BYTE *);
        typedef int (*pfn_skip)(int, const void *, const void *, int);
        typedef LONG (*pfn_count)(void);
        pfn_uinst uinst = (pfn_uinst)(void *)GetProcAddress(m, "qdu_unmute_install");
        pfn_skip uskip = (pfn_skip)(void *)GetProcAddress(m, "qdu_unmute_should_skip");
        pfn_count ucount = (pfn_count)(void *)GetProcAddress(m, "qdu_unmute_skipped");
        CHECK(ucheck && uinst && uskip && ucount, "the sound fix's functions are exported");
        Sleep(300);   /* the DLL's own install thread (refused on this exe) has finished */

        static volatile ULONG_PTR world;   /* what the stand-in "get world" returns: 0 = offline */
        BYTE *img = VirtualAlloc(NULL, SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memset(img, 0xcc, SIZE);
        /* the mute switch: its real 18 opening bytes, then set [rcx+0x828] = dl and return */
        static const BYTE mute[] = { 0x40,0x57, 0x48,0x81,0xec,0x90,0,0,0, 0x48,0x8b,0xf9, 0x38,0x91,0x28,0x08,0,0,
                                     0x88,0x91,0x28,0x08,0,0, 0x48,0x81,0xc4,0x90,0,0,0, 0x5f, 0xc3 };
        memcpy(img + MUTE, mute, sizeof mute);
        /* the caller: push rbx / sub rsp,0x30 / call mute / mov rbx,[rsp+0x30] (the real
         * bytes after the call) / add rsp,0x38 / ret -- rcx and dl pass straight through */
        BYTE *cs = img + CALLSITE;
        memcpy(cs - 5, "\x53\x48\x83\xec\x30", 5);
        cs[0] = 0xe8;
        LONG rel = (LONG)((img + MUTE) - (cs + 5));
        memcpy(cs + 1, &rel, 4);
        memcpy(cs + 5, "\x48\x8b\x5c\x24\x30\x48\x83\xc4\x38\xc3", 10);
        /* the online check: its real bytes, calling a stub that returns `world` */
        BYTE *mp = img + MP, *stub = img + 0x100;
        static const BYTE head[] = { 0x48,0x83,0xec,0x28, 0x41,0xb8,1,0,0,0, 0x48,0x8b,0xd1, 0xe8 };
        memcpy(mp, head, sizeof head);
        rel = (LONG)(stub - (mp + 18));
        memcpy(mp + 14, &rel, 4);
        memcpy(mp + 18, "\x48\x85\xc0\x74\x5a", 5);            /* test rax,rax / je +0x5a */
        memcpy(mp + 23, "\xb0\x01\x48\x83\xc4\x28\xc3", 7);    /* world found: al = 1 (online) */
        memcpy(mp + 23 + 0x5a, "\x48\x83\xc4\x28\xc3", 5);     /* no world: al = 0 (offline) */
        ULONG_PTR wa = (ULONG_PTR)&world;
        stub[0] = 0x48; stub[1] = 0xb8; memcpy(stub + 2, &wa, 8);   /* mov rax, &world */
        memcpy(stub + 10, "\x48\x8b\x00\xc3", 4);                  /* mov rax,[rax] / ret */

        CHECK(ucheck(img) == 1, "the stand-in passes the byte checks");
        int each = 1;
        BYTE *spots[3] = { img + MUTE + 12, cs + 6, mp + 20 };
        int want[3] = { -1, -2, -3 };
        for (int i = 0; i < 3; i++) {
            spots[i][0] ^= 1;
            int got = ucheck(img);
            if (got != want[i]) { printf("      changed place %d: returned %d\n", i, got); each = 0; }
            spots[i][0] ^= 1;
        }
        LONG saved;
        memcpy(&saved, cs + 1, 4);
        LONG wrong = saved + 16;
        memcpy(cs + 1, &wrong, 4);
        if (ucheck(img) != -2) { printf("      a call to somewhere else was accepted\n"); each = 0; }
        memcpy(cs + 1, &saved, 4);
        CHECK(each, "a changed byte in any of the three places, or a call elsewhere, is refused");

        typedef void (*pfn_set)(void *, unsigned char);
        pfn_set via_screen = (pfn_set)(void *)(cs - 5), direct = (pfn_set)(void *)(img + MUTE);
        BYTE *gi = calloc(1, 0x1000);
        CHECK(uinst(img) == 1, "the sound fix installs on the stand-in");
        world = 0;
        via_screen(gi, 1);
        CHECK(gi[0x828] == 0 && ucount() == 1, "offline: the wait-sync screen's mute is not passed on");
        direct(gi, 1);
        CHECK(gi[0x828] == 1, "any other mute is passed on");
        via_screen(gi, 0);
        CHECK(gi[0x828] == 0, "an unmute from the screen is passed on");
        world = 0x1234;
        via_screen(gi, 1);
        CHECK(gi[0x828] == 1 && ucount() == 1, "online: the wait-sync screen's mute is passed on");

        const void *a = (void *)0x1000, *b = (void *)0x2000;
        CHECK(uskip(1, a, a, 0) && !uskip(1, a, a, 1) && !uskip(0, a, a, 0) && !uskip(1, b, a, 0),
              "decision: skip only a mute, from the screen's path, offline");
    }

    printf(g_fail ? "%d check(s) FAILED\n" : "all checks passed\n", g_fail);
    return g_fail;
}
