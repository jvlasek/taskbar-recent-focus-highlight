// ==WindhawkMod==
// @id              load-failure-probe
// @name            Load failure probe (test only)
// @description     Test mod: fails Wh_ModInit and/or stalls Wh_ModUninit, to see how Windhawk reacts
// @version         0.1.0
// @author          test
// @include         notepad.exe
// @compilerOptions -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Load failure probe

Test-only mod. Targets **notepad.exe** only, so Explorer is never touched.
Open Notepad after enabling, then read the log.

- **Fail init** (default on): `Wh_ModInit` returns FALSE. Question: does the
  Windhawk UI report it, and does the toggle flip back?
- **Uninit delay**: `Wh_ModUninit` sleeps this many ms before returning.
  Use it with Fail init off: enable, open Notepad, then disable the mod (or
  re-enable it) and watch what the UI does while the uninit is stalled.
  0 = return at once.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- failInit: true
  $name: Fail init
  $description: Wh_ModInit returns FALSE
- uninitDelayMs: 0
  $name: Uninit delay (ms)
  $description: Wh_ModUninit sleeps this long before returning (try 30000)
*/
// ==/WindhawkModSettings==

#include <windows.h>

BOOL Wh_ModInit() {
    const BOOL fail = Wh_GetIntSetting(L"failInit");
    Wh_Log(L"Wh_ModInit in pid %u: failInit=%d", GetCurrentProcessId(), fail);
    if (fail) {
        Wh_Log(L"Wh_ModInit returning FALSE on purpose");
        return FALSE;
    }
    return TRUE;
}

void Wh_ModUninit() {
    const int delay = Wh_GetIntSetting(L"uninitDelayMs");
    Wh_Log(L"Wh_ModUninit start, sleeping %d ms", delay);
    if (delay > 0) {
        Sleep(static_cast<DWORD>(delay));
    }
    Wh_Log(L"Wh_ModUninit done");
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"Wh_ModSettingsChanged (no-op; disable/enable to re-run init)");
}
