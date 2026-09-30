/* Quarry Debug UI: shows The Quarry's shipped developer debug buttons.
 *
 * Loaded as XAPOFX1_5.dll next to the game exe (the game imports one function
 * from it, CreateFX, which is passed through to the real DLL in System32).
 *
 * The game's menus ask ShouldShowDebugUI() (a native Blueprint function) whether
 * to show their debug buttons. In the retail exe its body is compiled out: the
 * thunk ignores everything and stores a constant false as the result,
 *
 *   0x1415ec241  mov rax,[rbx+0x20] ... mov rbx,[rsp+0x30]
 *   0x1415ec258  mov byte ptr [rsi], 0      ; c6 06 00  <- the result
 *   0x1415ec25b  mov rsi,[rsp+0x40] / add rsp,0x20 / pop rdi / ret
 *
 * so no setting, ini or command-line switch can turn it on. This mod changes
 * that one immediate from 0 to 1.
 *
 * The linker merged two editor-only functions into the same code
 * (JumpToBookmarkInLevelEditor, RunAssetExportTask); they return true too.
 * Nothing in the shipped game content calls either.
 *
 * The 37 bytes checked occur once in the exe. Anything else there (a game
 * update) is refused and logged, and the game runs unchanged. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

#define QDU_VERSION "1.0.0"

static const BYTE k_sig[37] = {
    0x48,0x8b,0x43,0x20, 0x48,0x85,0xc0, 0x40,0x0f,0x95,0xc7, 0x48,0x03,0xf8,
    0x48,0x89,0x7b,0x20, 0x48,0x8b,0x5c,0x24,0x30,
    0xc6,0x06,0x00,                              /* mov byte ptr [rsi], 0 */
    0x48,0x8b,0x74,0x24,0x40, 0x48,0x83,0xc4,0x20, 0x5f, 0xc3 };
#define SIG_RVA   0x15ec241
#define VALUE_OFF 25                             /* the 0 in c6 06 00 */

static WCHAR g_dir[MAX_PATH];

static void qlog(const char *fmt, ...)
{
    WCHAR path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%lsQuarryDebugUI.log", g_dir);
    FILE *f = _wfopen(path, L"a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* 1 patched, 0 already patched, -1 bytes differ (refused), -2 not writable */
__declspec(dllexport) int qdu_patch_at(BYTE *p)
{
    BYTE on[sizeof k_sig];
    memcpy(on, k_sig, sizeof on);
    on[VALUE_OFF] = 1;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) ||
        (mbi.Protect & PAGE_GUARD) || (BYTE *)mbi.BaseAddress + mbi.RegionSize < p + sizeof k_sig)
        return -1;
    if (!memcmp(p, on, sizeof on)) return 0;
    if (memcmp(p, k_sig, sizeof k_sig)) return -1;
    DWORD old;
    if (!VirtualProtect(p + VALUE_OFF, 1, PAGE_EXECUTE_READWRITE, &old)) return -2;
    p[VALUE_OFF] = 1;
    VirtualProtect(p + VALUE_OFF, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p + VALUE_OFF, 1);
    return 1;
}

/* ---- the real XAPOFX function, passed through ---- */
static HMODULE g_real;
static INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_real(PINIT_ONCE o, void *p, void **c)
{
    (void)o; (void)p; (void)c;
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n && n < MAX_PATH - 20) {
        wcscat(path, L"\\XAPOFX1_5.dll");
        g_real = LoadLibraryW(path);
    }
    return TRUE;
}

typedef HRESULT (__cdecl *pfn_createfx)(REFCLSID, void **, const void *, UINT32);

/* XAPOFX.h declares CreateFX with the default (cdecl) convention; on x64 there
 * is only one. */
HRESULT __cdecl CreateFX(REFCLSID clsid, void **effect, const void *init, UINT32 init_size)
{
    static pfn_createfx f;
    if (!f) {
        InitOnceExecuteOnce(&g_real_once, load_real, NULL, NULL);
        if (g_real) f = (pfn_createfx)(void *)GetProcAddress(g_real, "CreateFX");
    }
    return f ? f(clsid, effect, init, init_size) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, void *r)
{
    (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    DWORD n = GetModuleFileNameW(h, g_dir, MAX_PATH);
    WCHAR *slash = n ? wcsrchr(g_dir, L'\\') : NULL;
    if (slash) slash[1] = 0;
    else g_dir[0] = 0;
    WCHAR log[MAX_PATH];
    _snwprintf(log, MAX_PATH, L"%lsQuarryDebugUI.log", g_dir);
    DeleteFileW(log);

    /* The exe is mapped and unpacked before its imports load, so the code is
     * already there. Only memcmp and VirtualProtect run here: both are safe
     * under the loader lock. */
    BYTE *p = (BYTE *)GetModuleHandleW(NULL) + SIG_RVA;
    int res = qdu_patch_at(p);
    qlog("Quarry Debug UI %s.", QDU_VERSION);
    if (res == 1) qlog("Debug UI enabled: ShouldShowDebugUI now returns true.");
    else if (res == 0) qlog("Debug UI was already enabled.");
    else if (res == -1) qlog("Debug UI NOT enabled: this game version is not the one this mod knows. The game is unchanged.");
    else qlog("Debug UI NOT enabled: could not change the game's code.");
    return TRUE;
}
