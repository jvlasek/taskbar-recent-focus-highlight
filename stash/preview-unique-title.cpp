// Stash only — not a Windhawk mod, not compiled.
//
// Unique-title preview fallback extracted from
// taskbar-recent-focus-highlight.wh.cpp (through v0.9.24).
//
// Why it existed: optional TaskItemThumbnail ctor / Thumbnails.GetAt /
// OnApplyTemplate symbols. On builds where those miss, a multi-window flyout
// had no HWND except by matching card title to confirmed windows.
//
// Why it was removed: on current Win11, pass 1 (DataContext ↔ ctor map) and
// pass 2 (repeater GetAt) fill every card. Pass 3 was never exercised as the
// primary path. Catalog review treated title-guess as fail-open.
//
// Wire-in (RefreshThumbnailFlyout_UIThread, after pass 1/2, before ranking):
//   - CopyRecentWindowsForPreview()
//   - PickFlyoutCardTitles(siblings)
//   - if any scored[i].hwnd is null: unique-title loop below
//   - optional pass 4: same-PID tick copy when HWND is set but tick is 0
//
// Types from the mod: WindowFocusInfo { hwnd, processKey, windowTitle,
// lastConfirmedTick, ... }. Helpers: ToUpper, FileNameFromPath, GetWindowTitle.

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------------------
// Title normalize / score
// ---------------------------------------------------------------------------

std::wstring AlnumUpper(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t ch : s) {
        if (ch >= L'a' && ch <= L'z') {
            out.push_back(static_cast<wchar_t>(ch - L'a' + L'A'));
        } else if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            out.push_back(ch);
        }
    }
    return out;
}

// Taskbar names look like "App - 2 running windows pinned" — strip that noise.
// Do NOT cut at the first " - ": Lister titles are
// "Lister - [C:\\path\\file.txt] - 3 running windows and 1 group".
std::wstring NormalizeAutomationName(std::wstring name) {
    auto isRunningCountSuffix = [](std::wstring_view tail) -> bool {
        size_t i = 0;
        while (i < tail.size() && tail[i] == L' ') {
            ++i;
        }
        if (i >= tail.size() || tail[i] < L'0' || tail[i] > L'9') {
            return false;
        }
        while (i < tail.size() && tail[i] >= L'0' && tail[i] <= L'9') {
            ++i;
        }
        if (i >= tail.size() || tail[i] != L' ') {
            return false;
        }
        ++i;
        constexpr wchar_t kRun[] = L"running";
        if (i + 7 > tail.size()) {
            return false;
        }
        for (int k = 0; k < 7; ++k) {
            wchar_t c = tail[i + static_cast<size_t>(k)];
            if (c >= L'A' && c <= L'Z') {
                c = static_cast<wchar_t>(c - L'A' + L'a');
            }
            if (c != kRun[k]) {
                return false;
            }
        }
        return true;
    };

    size_t cut = std::wstring::npos;
    for (size_t search = 0; search + 3 < name.size();) {
        const auto pos = name.find(L" - ", search);
        if (pos == std::wstring::npos) {
            break;
        }
        if (isRunningCountSuffix(std::wstring_view(name).substr(pos + 3))) {
            cut = pos;
            break;
        }
        search = pos + 3;
    }
    if (cut != std::wstring::npos) {
        name.resize(cut);
    }
    constexpr wchar_t kPinned[] = L" pinned";
    if (name.size() > 7) {
        auto off = name.size() - 7;
        if (_wcsicmp(name.c_str() + off, kPinned) == 0) {
            name.resize(off);
        }
    }
    while (!name.empty() && name.back() == L' ') {
        name.pop_back();
    }
    return name;
}

// True for Lister-style "[c:\temp\file.txt]" / "[book.epub]", not Calibre's
// format tag "[EPUB]" (same on every book — must not be an identity key).
bool LooksLikeFilePath(const std::wstring& s) {
    if (s.empty()) {
        return false;
    }
    if (s.find(L'\\') != std::wstring::npos ||
        s.find(L'/') != std::wstring::npos) {
        return true;
    }
    if (s.size() >= 2 && ((s[0] >= L'A' && s[0] <= L'Z') ||
                          (s[0] >= L'a' && s[0] <= L'z')) &&
        s[1] == L':') {
        return true;
    }
    const auto dot = s.rfind(L'.');
    return dot != std::wstring::npos && dot > 0 && dot + 1 < s.size();
}

std::wstring ExtractBracketedPath(const std::wstring& s) {
    const auto open = s.find(L'[');
    const auto close = s.rfind(L']');
    if (open == std::wstring::npos || close == std::wstring::npos ||
        close <= open + 1) {
        return {};
    }
    std::wstring inner = s.substr(open + 1, close - open - 1);
    if (!LooksLikeFilePath(inner)) {
        return {};
    }
    return inner;
}

int ScoreTitleToAutomationName(const std::wstring& windowTitle,
                               const std::wstring& automationName) {
    if (windowTitle.empty() || automationName.empty()) {
        return 0;
    }
    std::wstring t = AlnumUpper(NormalizeAutomationName(windowTitle));
    std::wstring a = AlnumUpper(NormalizeAutomationName(automationName));
    if (t.empty() || a.empty()) {
        return 0;
    }
    if (t == a) {
        return 98;
    }
    if (t.size() >= 4 && a.find(t) != std::wstring::npos) {
        return 93;
    }
    if (a.size() >= 4 && t.find(a) != std::wstring::npos) {
        return 91;
    }
    const std::wstring tPath =
        /* ToUpper */ ExtractBracketedPath(NormalizeAutomationName(windowTitle));
    const std::wstring aPath =
        /* ToUpper */ ExtractBracketedPath(
            NormalizeAutomationName(automationName));
    if (!tPath.empty() && !aPath.empty() && tPath != aPath) {
        return 0;
    }
    size_t pref = 0;
    while (pref < t.size() && pref < a.size() && t[pref] == a[pref]) {
        ++pref;
    }
    if (pref >= 6) {
        if (t.size() > pref && a.size() > pref) {
            return 0;
        }
        return 85;
    }
    return 0;
}

std::wstring TitleMatchKey(const std::wstring& raw) {
    std::wstring n = NormalizeAutomationName(raw);
    std::wstring path = ExtractBracketedPath(n);
    if (!path.empty()) {
        return path;
    }
    return AlnumUpper(n);
}

bool TitleKeysAreDistinct(const std::vector<std::wstring>& titles) {
    if (titles.size() < 2) {
        return false;
    }
    std::unordered_set<std::wstring> seen;
    for (const auto& t : titles) {
        std::wstring key = TitleMatchKey(t);
        if (key.empty() || !seen.insert(key).second) {
            return false;
        }
    }
    return true;
}

struct WindowFocusInfo {
    HWND hwnd = nullptr;
    ULONGLONG lastConfirmedTick = 0;
    std::wstring processKey;
    std::wstring windowTitle;
};

// Title → HWND only for unique assignment (each HWND at most once).
// Filter `recent` to this flyout's processKey before calling.
// Needs ToUpper / FileNameFromPath / GetWindowTitle from the mod.
HWND MatchTitleToUnusedRecent(const std::wstring& autoName,
                              const std::vector<WindowFocusInfo>& recent,
                              const std::unordered_set<HWND>& used) {
    if (autoName.empty()) {
        return nullptr;
    }
    const std::wstring cardPath = ExtractBracketedPath(autoName);
    const std::wstring cardFile =
        cardPath.empty() ? std::wstring{} : cardPath;

    int bestScore = 0;
    HWND bestHwnd = nullptr;
    ULONGLONG bestTick = 0;
    for (const auto& info : recent) {
        if (!info.hwnd || used.count(info.hwnd)) {
            continue;
        }
        int s = ScoreTitleToAutomationName(info.windowTitle, autoName);
        if (IsWindow(info.hwnd)) {
            s = (std::max)(s, ScoreTitleToAutomationName(info.windowTitle,
                                                         autoName));
            if (!cardPath.empty()) {
                const std::wstring infoPath =
                    ExtractBracketedPath(info.windowTitle);
                if (infoPath == cardPath) {
                    s = (std::max)(s, 100);
                }
            }
        }
        if (s > bestScore ||
            (s == bestScore && s >= 70 && info.lastConfirmedTick > bestTick)) {
            bestScore = s;
            bestHwnd = info.hwnd;
            bestTick = info.lastConfirmedTick;
        }
    }
    return bestScore >= 70 ? bestHwnd : nullptr;
}

#if 0
// --- Pass 3 call site (RefreshThumbnailFlyout_UIThread) ---
//
// bool anyHole = false;
// for (const auto& s : scored) {
//     if (!s.hwnd) { anyHole = true; break; }
// }
// if (anyHole) {
//     std::wstring flyoutKey;
//     for (const auto& s : scored) {
//         if (!s.hwnd || (s.how != TaskItem && s.how != Repeater)) continue;
//         for (const auto& info : recent) {
//             if (info.hwnd == s.hwnd && !info.processKey.empty()) {
//                 flyoutKey = info.processKey;
//                 break;
//             }
//         }
//         if (!flyoutKey.empty()) break;
//     }
//     if (TitleKeysAreDistinct(cardTitles) && !flyoutKey.empty()) {
//         std::vector<WindowFocusInfo> recentSameApp;
//         for (const auto& info : recent) {
//             if (info.processKey == flyoutKey) recentSameApp.push_back(info);
//         }
//         for (size_t i = 0; i < siblings.size(); ++i) {
//             if (scored[i].hwnd) continue;
//             HWND h = MatchTitleToUnusedRecent(cardTitles[i], recentSameApp,
//                                               usedHwnds);
//             if (h) {
//                 stampRecency(i, h);
//                 scored[i].how = ResolveHow::Title;
//                 usedHwnds.insert(h);
//             }
//         }
//     }
// }
//
// --- Pass 4 (tick copy, not HWND assign) ---
// ITaskItem HWND vs FOREGROUND HWND (Lister, tab proxies). Same PID, title
// score >= 96 → copy lastConfirmedTick onto the card that already has an HWND.
//
// for (size_t i = 0; i < scored.size(); ++i) {
//     if (!scored[i].hwnd || scored[i].tick > 0) continue;
//     DWORD cardPid = 0;
//     GetWindowThreadProcessId(scored[i].hwnd, &cardPid);
//     ... ScoreTitleToAutomationName vs recent same-PID ...
//     if (bestScore >= 96) scored[i].tick = bestTick;
// }
#endif
