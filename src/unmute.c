/* Keeps the game's sound on after a debug start with an automation route.
 *
 * Starting a chapter from Blockouts with any automation route (MonteCarlo,
 * PerformanceMetrics, ...) left the game silent until it was restarted.
 * Measured 2026-09-30:
 *
 *   - Chapters contain a sync step (SyncMP_Gameflow): the two online players'
 *     games meet there. On entry it shows the Wolf Pack wait-sync screen,
 *     and on leaving it closes it again. The screen mutes the game when shown
 *     (GameplayStaticsSMG026.MuteAudio(true), zeroing the music, effects and
 *     voice volumes) and unmutes when hidden. Offline the step passes at once,
 *     so the mute lasts a frame or two.
 *   - A bot run leaves the step without closing the screen, so the unmute never
 *     comes. The mute sits on the game instance, which lives for the whole
 *     session, so it survived returning to the main menu.
 *
 * The fix: the game's mute switch (on the game instance, exe+0x14ee600) is
 * hooked. When a call to MUTE comes from the MuteAudio Blueprint function (the
 * only thing that calls it is that wait-sync screen) and the session is not
 * online (the engine's own check behind IsMultiplayerSession), the call is not
 * passed on. Offline that mute serves no purpose; online it is left alone, so
 * real Wolf Pack play still mutes while waiting for the other player. Every
 * other mute and every unmute is passed through unchanged.
 *
 * Each of the three places is checked byte for byte first; anything else (a
 * game update) is refused and logged, and the game runs unchanged. */
#include <windows.h>
#include <string.h>
#include "MinHook.h"

void qdu_log(const char *fmt, ...);

#define MUTE_RVA     0x14ee600   /* game instance: void SetMuted(this, bool) */
#define CALLSITE_RVA 0x157c23c   /* call SetMuted, inside the MuteAudio Blueprint function */
#define MP_RVA       0x0e0b940   /* bool IsMultiplayerSession(const UObject *worldContext) */

/* push rdi / sub rsp,0x90 / mov rdi,rcx / cmp [rcx+0x828],dl */
static const BYTE k_mute_sig[18] = { 0x40,0x57, 0x48,0x81,0xec,0x90,0x00,0x00,0x00, 0x48,0x8b,0xf9,
                                     0x38,0x91,0x28,0x08,0x00,0x00 };
/* sub rsp,0x28 / mov r8d,1 / mov rdx,rcx / call GetWorldFromContextObject / test rax,rax / je */
static const BYTE k_mp_head[14] = { 0x48,0x83,0xec,0x28, 0x41,0xb8,0x01,0x00,0x00,0x00, 0x48,0x8b,0xd1, 0xe8 };
static const BYTE k_mp_tail[5] = { 0x48,0x85,0xc0, 0x74,0x5a };
/* after the call: mov rbx,[rsp+0x30] */
static const BYTE k_call_after[5] = { 0x48,0x8b,0x5c,0x24,0x30 };

typedef void (*pfn_setmuted)(void *, unsigned char);
typedef unsigned char (*pfn_ismp)(void *);
static pfn_setmuted o_setmuted;
static pfn_ismp g_ismp;
static void *g_waitsync_ret;
static volatile LONG g_skipped, g_passed_online;

static int readable(const BYTE *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
           (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)) &&
           (const BYTE *)mbi.BaseAddress + mbi.RegionSize >= p + n;
}

/* 1 all three places are as expected; -1 the mute switch, -2 the call site,
 * -3 the online check differ (or are unreadable). */
__declspec(dllexport) int qdu_unmute_check(const BYTE *base)
{
    const BYTE *m = base + MUTE_RVA, *c = base + CALLSITE_RVA, *p = base + MP_RVA;
    if (!readable(m, sizeof k_mute_sig) || memcmp(m, k_mute_sig, sizeof k_mute_sig)) return -1;
    if (!readable(c, 10) || c[0] != 0xe8) return -2;
    LONG rel;
    memcpy(&rel, c + 1, 4);
    if (c + 5 + rel != m || memcmp(c + 5, k_call_after, sizeof k_call_after)) return -2;
    if (!readable(p, 23) || memcmp(p, k_mp_head, sizeof k_mp_head) || memcmp(p + 18, k_mp_tail, sizeof k_mp_tail))
        return -3;
    return 1;
}

/* The decision, on its own so it can be tested: pass the call on unless it is a
 * MUTE from the wait-sync screen's path in an offline session. */
__declspec(dllexport) int qdu_unmute_should_skip(int mute, const void *ret, const void *waitsync_ret, int online)
{
    return mute && ret == waitsync_ret && !online;
}

static void d_setmuted(void *gi, unsigned char mute)
{
    void *ret = __builtin_return_address(0);
    if (mute && ret == g_waitsync_ret && gi) {
        int online = g_ismp(gi) != 0;
        if (qdu_unmute_should_skip(1, ret, g_waitsync_ret, online)) {
            if (InterlockedIncrement(&g_skipped) <= 10)
                qdu_log("Kept the sound on: the Wolf Pack wait-sync screen asked to mute, and this is not an online session.");
            return;
        }
        if (InterlockedIncrement(&g_passed_online) <= 10)
            qdu_log("Wolf Pack wait-sync mute let through: this is an online session.");
    }
    o_setmuted(gi, mute);
}

/* 1 installed; -1..-3 as qdu_unmute_check (refused); -4 the hook failed. */
__declspec(dllexport) int qdu_unmute_install(BYTE *base)
{
    int r = qdu_unmute_check(base);
    if (r != 1) return r;
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return -4;
    g_ismp = (pfn_ismp)(void *)(base + MP_RVA);
    g_waitsync_ret = base + CALLSITE_RVA + 5;
    void *target = base + MUTE_RVA;
    if (MH_CreateHook(target, (void *)d_setmuted, (void **)&o_setmuted) != MH_OK) return -4;
    if (MH_EnableHook(target) != MH_OK) return -4;
    return 1;
}

__declspec(dllexport) LONG qdu_unmute_skipped(void) { return g_skipped; }
