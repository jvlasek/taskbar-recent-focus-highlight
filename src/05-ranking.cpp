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

// Requires g_stateMutex. Moves recency recorded under a provisional
// WINDOW: key to the APPID: key of the button that now lists that window
// (newer confirmation wins when the app already has a record).
void AttributeProvisionalAppsLocked(DesktopRecencyState& desk) {
    std::vector<std::pair<std::wstring, std::wstring>> moves;  // old, new
    for (const auto& [key, info] : desk.appFocusMap) {
        if (!IsProvisionalAppKey(key) || !info.lastHwnd ||
            !HwndMatchesStoredPid(info.lastHwnd, info.lastPid)) {
            continue;
        }
        const std::wstring appId = LookupWindowAppId(info.lastHwnd);
        if (!appId.empty()) {
            moves.emplace_back(key, std::wstring(L"APPID:") + appId);
        }
    }
    for (const auto& [oldKey, newKey] : moves) {
        auto it = desk.appFocusMap.find(oldKey);
        if (it == desk.appFocusMap.end()) {
            continue;
        }
        AppFocusInfo moved = std::move(it->second);
        desk.appFocusMap.erase(it);
        moved.key = newKey;
        LOG_DEFERRED(L"Attributed %s window %p to %s", moved.displayName.c_str(),
               moved.lastHwnd, newKey.c_str());
        auto& dst = desk.appFocusMap[newKey];
        if (dst.key.empty() ||
            moved.lastConfirmedFocusTick > dst.lastConfirmedFocusTick) {
            dst = std::move(moved);
        }
        for (auto& [hwnd, w] : desk.windowAppFocus) {
            (void)hwnd;
            if (w.key == oldKey) {
                w.key = newKey;
            }
        }
        for (auto& [hwnd, w] : desk.windowFocusMap) {
            (void)hwnd;
            if (w.processKey == oldKey) {
                w.processKey = newKey;
            }
        }
    }
}

void RecomputeRanksForDesktopLocked(DesktopRecencyState& desk) {
    desk.rankedApps.clear();
    AttributeProvisionalAppsLocked(desk);

    auto settings = SettingsSnap();
    if (g_unloading.load()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs = DecayMsFromMinutes(settings->decayMinutes);

    std::vector<AppFocusInfo> candidates;
    candidates.reserve(desk.appFocusMap.size());

    for (auto it = desk.appFocusMap.begin(); it != desk.appFocusMap.end();) {
        auto& info = it->second;
        if (info.lastConfirmedFocusTick == 0) {
            ++it;
            continue;
        }
        if (IsTickDecayed(info.lastConfirmedFocusTick, decayMs, now)) {
            LOG_DEFERRED(L"Decayed: %s", info.displayName.c_str());
            it = desk.appFocusMap.erase(it);
            continue;
        }
        // No running button with this AppId (closed, pinned-only, tray-only
        // window): keep history but do not occupy a visible slot.
        if (!AppAppearsOnTaskbar(info.key)) {
            ++it;
            continue;
        }
        candidates.push_back(info);
        ++it;
    }

    // Per-window slots. Drop dead/recycled/forgotten windows first.
    for (auto it = desk.windowAppFocus.begin(); it != desk.windowAppFocus.end();) {
        const bool keep = IsWindow(it->first) &&
                          HwndMatchesStoredPid(it->first, it->second.pid) &&
                          desk.appFocusMap.count(it->second.key) &&
                          !IsTickDecayed(it->second.tick, decayMs, now);
        it = keep ? std::next(it) : desk.windowAppFocus.erase(it);
    }
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

