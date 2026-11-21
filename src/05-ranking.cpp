// ---------------------------------------------------------------------------
// Ranking
// ---------------------------------------------------------------------------

// True if a running (or just-closing) taskbar button carries this app's
// Explorer AppId and still lists a live window of it. Ranks without one keep
// their history but take no slot. observedRunning is the last UI IsRunning
// snapshot; it can stay set for a button Explorer parked without a final
// state update, so a live window is required too (Fable review 2.1).
// Grace covers Alt-Tab flicker, not a heartbeat. No weak.get() here.
bool AppAppearsOnTaskbar(const std::wstring& key) {
    const std::wstring wantAppId = AppIdFromAppKey(key);
    if (wantAppId.empty()) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_buttonStateMutex);
    for (const auto& [cacheKey, e] : g_buttonStates) {
        (void)cacheKey;
        const bool recentlyRunning =
            e.lastRunningTick != 0 &&
            now - e.lastRunningTick < kIsRunningGraceMs;
        if (e.appIdUpper != wantAppId) {
            continue;
        }
        if (recentlyRunning) {
            return true;
        }
        if (e.observedRunning) {
            for (const auto& w : e.groupWindows) {
                if (HwndMatchesStoredPid(w.hwnd, w.pid)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Requires g_stateMutex. Drops dead, recycled and decayed window records,
// then folds the others into their app's record by the window's current
// Explorer AppId (newest confirmation wins). A window without a known AppId
// keeps its record and is folded in once a button lists it.
void DeriveAppsFromWindowsLocked(DesktopRecencyState& desk,
                                 ULONGLONG decayMs,
                                 ULONGLONG now) {
    for (auto it = desk.windowAppFocus.begin();
         it != desk.windowAppFocus.end();) {
        HWND hwnd = it->first;
        WindowAppFocus& w = it->second;
        if (!IsWindow(hwnd) || !HwndMatchesStoredPid(hwnd, w.pid) ||
            IsTickDecayed(w.tick, decayMs, now)) {
            it = desk.windowAppFocus.erase(it);
            continue;
        }
        const std::wstring appId = LookupWindowAppId(hwnd);
        const std::wstring key =
            appId.empty() ? std::wstring{} : std::wstring(L"APPID:") + appId;
        if (key != w.key && !key.empty()) {
            LOG_DEFERRED(L"Window %p (%s) belongs to %s", hwnd,
                         w.displayName.c_str(), key.c_str());
        }
        w.key = key;
        if (!key.empty()) {
            AppFocusInfo& app = desk.appFocusMap[key];
            if (app.key.empty() || w.tick >= app.lastConfirmedFocusTick) {
                app.key = key;
                app.displayName = w.displayName;
                if (!w.title.empty()) {
                    app.lastWindowTitle = w.title;
                }
                app.appIdUpper = w.appIdUpper;
                app.pathUpper = w.pathUpper;
                app.lastHwnd = hwnd;
                app.lastPid = w.pid;
                app.lastConfirmedFocusTick = w.tick;
            }
        }
        ++it;
    }
}

void RecomputeRanksForDesktopLocked(DesktopRecencyState& desk) {
    desk.rankedApps.clear();

    auto settings = SettingsSnap();
    if (g_unloading.load()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs = DecayMsFromMinutes(settings->decayMinutes);
    DeriveAppsFromWindowsLocked(desk, decayMs, now);

    std::vector<AppFocusInfo> candidates;
    candidates.reserve(desk.appFocusMap.size());

    for (auto it = desk.appFocusMap.begin(); it != desk.appFocusMap.end();) {
        auto& info = it->second;
        if (info.lastConfirmedFocusTick == 0) {
            ++it;
            continue;
        }
        if (IsTickDecayed(info.lastConfirmedFocusTick, decayMs, now)) {
            // Key: two apps can share a display name (Total Commander and
            // its Lister windows are both TOTALCMD64.EXE).
            const ULONGLONG idleTenths =
                (now - info.lastConfirmedFocusTick) / 6000;
            LOG_DEFERRED(L"Decayed: %s key=%s (last use %llu.%llu min ago)",
                         info.displayName.c_str(), info.key.c_str(),
                         static_cast<unsigned long long>(idleTenths / 10),
                         static_cast<unsigned long long>(idleTenths % 10));
            it = desk.appFocusMap.erase(it);
            continue;
        }
        // No running button with this AppId (closed, pinned-only, tray-only
        // window): keep history but do not occupy a visible slot. Logged on
        // change, which also explains a ladder that empties without a decay
        // (lock or desktop switch clears the running grace until the next
        // full pass re-reads IsRunning).
        const bool hidden = !AppAppearsOnTaskbar(info.key);
        if (hidden != info.hiddenNoButton) {
            info.hiddenNoButton = hidden;
            if (hidden) {
                LOG_DEFERRED(L"Hidden (no running button): %s key=%s",
                             info.displayName.c_str(), info.key.c_str());
            } else {
                LOG_DEFERRED(L"Shown again (running button): %s",
                             info.displayName.c_str());
            }
        }
        if (hidden) {
            ++it;
            continue;
        }
        candidates.push_back(info);
        ++it;
    }

    // Per-window slots (window records were pruned by the derive step).
    {
        std::unordered_set<HWND, HwndHash> uncombined;
        {
            std::lock_guard<std::mutex> lock(g_uncombinedMutex);
            for (auto& [thread, windows] : g_uncombinedWindowsByThread) {
                (void)thread;
                for (auto it = windows.begin(); it != windows.end();) {
                    it = IsWindow(*it) ? std::next(it) : windows.erase(it);
                }
                uncombined.insert(windows.begin(), windows.end());
            }
        }
        if (!uncombined.empty()) {
            // An app is split when any of its confirmed windows has its own
            // button; each confirmed window then is its own unit.
            std::vector<AppFocusInfo> expanded;
            for (auto& app : candidates) {
                bool split = false;
                for (const auto& [hwnd, w] : desk.windowAppFocus) {
                    if (w.key == app.key && uncombined.count(hwnd)) {
                        split = true;
                        break;
                    }
                }
                if (!split) {
                    expanded.push_back(std::move(app));
                    continue;
                }
                for (const auto& [hwnd, w] : desk.windowAppFocus) {
                    if (w.key != app.key) {
                        continue;
                    }
                    AppFocusInfo unit = app;
                    unit.perWindow = true;
                    unit.lastHwnd = hwnd;
                    unit.lastPid = w.pid;
                    unit.lastConfirmedFocusTick = w.tick;
                    if (!w.title.empty()) {
                        unit.lastWindowTitle = w.title;
                    }
                    expanded.push_back(std::move(unit));
                }
            }
            candidates = std::move(expanded);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const AppFocusInfo& a, const AppFocusInfo& b) {
                  return a.lastConfirmedFocusTick > b.lastConfirmedFocusTick;
              });

    const int limit = (std::max)(0, settings->highlightCount);
    if (static_cast<int>(candidates.size()) > limit) {
        candidates.resize(static_cast<size_t>(limit));
    }

    desk.rankedApps = std::move(candidates);
}

