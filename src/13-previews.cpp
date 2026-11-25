// ---------------------------------------------------------------------------
// Thumbnail preview glow (multi-window flyouts)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// WindowsUdk.UI.Shell / Taskbar.View interfaces (Explorer's own bindings)
// ---------------------------------------------------------------------------
// IIDs and method order from windowsudk.winmd; Taskbar.* IIDs are guid_v
// constants from Taskbar.View.dll symbols. Same declarations as
// taskbar-active-window-highlight (EphremTil17, GPL-3.0). A changed interface
// changes its IID, the QueryInterface fails and the card stays unresolved.

constexpr GUID IID_Wudk_ITaskItemThumbnailView = {
    0xF093D5DE, 0xB516, 0x55A2, {0xB1, 0x37, 0x4E, 0x12, 0x5E, 0x37, 0xCA, 0xF4}};
constexpr GUID IID_Wudk_ITaskItemThumbnailViewModel = {
    0xA5D30F97, 0x5BC4, 0x58AE, {0xB4, 0x48, 0xB1, 0x54, 0x8A, 0x1A, 0x92, 0x9D}};
constexpr GUID IID_Wudk_ITaskItemThumbnail = {
    0xD7DC08AE, 0x776E, 0x5871, {0xBC, 0xEE, 0x25, 0x0D, 0xBC, 0x94, 0xD0, 0x37}};

// Leading members only; declared order matches windowsudk.winmd.
// (WudkITaskItemAbi is declared with the task-group interfaces above.)
struct WudkITaskItemThumbnailAbi : ::IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AccessibleName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ParentItem(WudkITaskItemAbi** value) = 0;
};

// Interface getters of Taskbar.View.dll types (optional symbols). Called with
// the interface pointer obtained by QueryInterface for that interface's IID.
using WudkGetObject_t = int32_t(WINAPI*)(void* pThis, void** value);
WudkGetObject_t TaskItemThumbnailView_get_ThumbnailSource;
WudkGetObject_t TaskItemThumbnailViewModel_get_TaskItemThumbnail;

// Strongest resolve: the window Explorer itself bound to this card.
// card -> ThumbnailSource (view model) -> TaskItemThumbnail -> ParentItem
// -> WindowId. Per card, no ctor timing, no index; for UWP the WindowId is
// the ApplicationFrameWindow (shadow log 2026-11-15), matching focus.
// Why a card did not resolve (diagnostics; 0 = resolved).
enum class CardResolveFail : int {
    None = 0,
    Unavailable,   // getters not found
    NoView,        // QI ITaskItemThumbnailView
    NoSource,      // ThumbnailSource
    NoViewModel,   // QI ITaskItemThumbnailViewModel
    NoThumbnail,   // TaskItemThumbnail
    NoThumbnailQi, // QI ITaskItemThumbnail
    NoParentItem,  // ParentItem null / failed
    NoWindowId,    // get_WindowId failed
    NotAWindow,    // WindowId is not a live HWND
    Exception,
};

PCWSTR CardResolveFailName(CardResolveFail f) {
    switch (f) {
        case CardResolveFail::None: return L"ok";
        case CardResolveFail::Unavailable: return L"unavailable";
        case CardResolveFail::NoView: return L"no-view";
        case CardResolveFail::NoSource: return L"no-source";
        case CardResolveFail::NoViewModel: return L"no-viewmodel";
        case CardResolveFail::NoThumbnail: return L"no-thumbnail";
        case CardResolveFail::NoThumbnailQi: return L"no-thumbnail-qi";
        case CardResolveFail::NoParentItem: return L"no-parent-item";
        case CardResolveFail::NoWindowId: return L"no-window-id";
        case CardResolveFail::NotAWindow: return L"not-a-window";
        case CardResolveFail::Exception: return L"exception";
    }
    return L"?";
}

HWND ResolveHwndFromCardViewModel(FrameworkElement const& card,
                                  CardResolveFail* fail = nullptr,
                                  UINT64* rawWindowId = nullptr) {
    auto failed = [&](CardResolveFail f) -> HWND {
        if (fail) {
            *fail = f;
        }
        return nullptr;
    };
    if (fail) {
        *fail = CardResolveFail::None;
    }
    if (rawWindowId) {
        *rawWindowId = 0;
    }
    if (!card || !TaskItemThumbnailView_get_ThumbnailSource ||
        !TaskItemThumbnailViewModel_get_TaskItemThumbnail) {
        return failed(CardResolveFail::Unavailable);
    }
    try {
        auto* unknown = static_cast<::IUnknown*>(winrt::get_abi(card));
        winrt::com_ptr<::IUnknown> view;
        if (FAILED(unknown->QueryInterface(IID_Wudk_ITaskItemThumbnailView,
                                           view.put_void()))) {
            return failed(CardResolveFail::NoView);
        }
        winrt::com_ptr<::IUnknown> source;
        if (TaskItemThumbnailView_get_ThumbnailSource(view.get(),
                                                      source.put_void()) < 0 ||
            !source) {
            return failed(CardResolveFail::NoSource);
        }
        winrt::com_ptr<::IUnknown> viewModel;
        if (FAILED(source->QueryInterface(IID_Wudk_ITaskItemThumbnailViewModel,
                                          viewModel.put_void()))) {
            return failed(CardResolveFail::NoViewModel);
        }
        winrt::com_ptr<::IUnknown> thumbnailObject;
        if (TaskItemThumbnailViewModel_get_TaskItemThumbnail(
                viewModel.get(), thumbnailObject.put_void()) < 0 ||
            !thumbnailObject) {
            return failed(CardResolveFail::NoThumbnail);
        }
        winrt::com_ptr<WudkITaskItemThumbnailAbi> thumbnail;
        if (FAILED(thumbnailObject->QueryInterface(IID_Wudk_ITaskItemThumbnail,
                                                   thumbnail.put_void()))) {
            return failed(CardResolveFail::NoThumbnailQi);
        }
        winrt::com_ptr<WudkITaskItemAbi> item;
        if (FAILED(thumbnail->get_ParentItem(item.put())) || !item) {
            return failed(CardResolveFail::NoParentItem);
        }
        UINT64 windowId = 0;
        if (FAILED(item->get_WindowId(&windowId))) {
            return failed(CardResolveFail::NoWindowId);
        }
        if (rawWindowId) {
            *rawWindowId = windowId;
        }
        HWND hwnd = reinterpret_cast<HWND>(windowId);
        return IsWindow(hwnd) ? hwnd : failed(CardResolveFail::NotAWindow);
    } catch (...) {
        return failed(CardResolveFail::Exception);
    }
}

void CollectThumbnailViewsUnder(FrameworkElement root,
                                std::vector<FrameworkElement>& out) {
    if (!root) {
        return;
    }
    try {
        if (winrt::get_class_name(root) == L"Taskbar.TaskItemThumbnailView") {
            out.push_back(root);
        }
        int n = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                CollectThumbnailViewsUnder(child, out);
            }
        }
    } catch (...) {
    }
}

FrameworkElement FindAncestorByClassName(FrameworkElement element,
                                         PCWSTR className) {
    FrameworkElement cur = element;
    for (int guard = 0; guard < 32 && cur; ++guard) {
        try {
            if (winrt::get_class_name(cur) == className) {
                return cur;
            }
            cur = Media::VisualTreeHelper::GetParent(cur)
                      .try_as<FrameworkElement>();
        } catch (...) {
            break;
        }
    }
    return nullptr;
}

std::vector<FrameworkElement> CollectSiblingThumbnailViews(
    FrameworkElement thumbView) {
    std::vector<FrameworkElement> out;
    if (!thumbView) {
        return out;
    }

    // Prefer known list hosts from the XAML thumbnail flyout.
    static const PCWSTR kHosts[] = {
        L"Taskbar.TaskItemThumbnailList",
        L"Taskbar.TaskItemThumbnailScrollableList",
        L"Taskbar.FlyoutFrame",
    };
    FrameworkElement host = nullptr;
    for (auto name : kHosts) {
        host = FindAncestorByClassName(thumbView, name);
        if (host) {
            break;
        }
    }
    if (!host) {
        // Walk up a few parents and collect from the highest with 2+ thumbs.
        FrameworkElement cur = thumbView;
        for (int i = 0; i < 8 && cur; ++i) {
            try {
                cur = Media::VisualTreeHelper::GetParent(cur)
                          .try_as<FrameworkElement>();
            } catch (...) {
                cur = nullptr;
            }
            if (cur) {
                host = cur;
            }
        }
    }
    if (host) {
        CollectThumbnailViewsUnder(host, out);
    }
    if (out.empty()) {
        out.push_back(thumbView);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Flyout ItemsRepeater: repaint when a card is removed
// ---------------------------------------------------------------------------
// Closing a window from the flyout removes its card without rebinding the
// others (no put_ThumbnailSource, no OnApplyTemplate), so the remaining
// ranks kept their old paint until focus changed or the flyout reopened
// (capture manual-20261115-152151, 15:23:03 -> 15:23:20). ElementClearing
// fires when the repeater removes or recycles a card. Subscribed once per
// repeater on its UI thread; revoked there at unload.

namespace muxc = winrt::Microsoft::UI::Xaml::Controls;

void SchedulePreviewFlyoutRefresh(FrameworkElement dispatcherAnchor);

struct RepeaterClearingWatch {
    winrt::weak_ref<muxc::ItemsRepeater> repeater;
    winrt::event_token token{};
};
std::mutex g_repeaterWatchMutex;
std::unordered_map<void*, RepeaterClearingWatch> g_repeaterWatches;

void WatchRepeaterElementClearing_UIThread(muxc::ItemsRepeater const& repeater) {
    // Unload revokes the watch on its dispatcher: subscribe only on one the
    // drain knows (review-lessons "no SizeChanged without a drainable
    // dispatcher"; Neon review 1.4.1).
    if (!repeater || g_unloading.load() ||
        !RememberUiDispatcher(repeater.try_as<FrameworkElement>())) {
        return;
    }
    void* id = InspectableIdentity(repeater);
    if (!id) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        // A dead repeater's address can be reused by a new one: drop it.
        for (auto it = g_repeaterWatches.begin(); it != g_repeaterWatches.end();) {
            bool dead = true;
            try {
                dead = !it->second.repeater.get();
            } catch (...) {
            }
            it = dead ? g_repeaterWatches.erase(it) : std::next(it);
        }
        if (g_repeaterWatches.count(id)) {
            return;
        }
    }
    winrt::event_token token{};
    try {
        token = repeater.ElementClearing(
            [](muxc::ItemsRepeater const& sender,
               muxc::ItemsRepeaterElementClearingEventArgs const& args) {
                if (g_unloading.load() ||
                    !SettingsSnap()->previewHighlightEnabled) {
                    return;
                }
                try {
                    FrameworkElement anchor = nullptr;
                    if (auto el = args.Element()) {
                        anchor = el.try_as<FrameworkElement>();
                    }
                    if (!anchor) {
                        anchor = sender.try_as<FrameworkElement>();
                    }
                    // Low priority, after layout: the card set has changed by
                    // then. Coalesced with other refresh requests.
                    SchedulePreviewFlyoutRefresh(anchor);
                } catch (...) {
                }
            });
    } catch (...) {
        return;
    }
    if (!token) {
        return;
    }
    bool raced = false;
    {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        raced = !g_repeaterWatches
                     .emplace(id, RepeaterClearingWatch{winrt::make_weak(repeater),
                                                        token})
                     .second;
    }
    // Registration is not logged (205 lines a day); a watch left over at
    // unload is (Wh_ModUninit, "ElementClearing watches not revoked").
    if (raced) {
        try {
            repeater.ElementClearing(token);
        } catch (...) {
        }
    }
}

// Unload: revoke the watches owned by this dispatcher; keep the others for
// their own dispatcher's pass.
void RevokeRepeaterWatchesOnThisDispatcher() {
    std::unordered_map<void*, RepeaterClearingWatch> taken;
    {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        taken.swap(g_repeaterWatches);
    }
    std::unordered_map<void*, RepeaterClearingWatch> keep;
    for (auto& [id, watch] : taken) {
        muxc::ItemsRepeater repeater{nullptr};
        try {
            repeater = watch.repeater.get();
        } catch (...) {
        }
        if (!repeater) {
            continue;  // gone; nothing to revoke
        }
        bool mine = false;
        try {
            auto dispatcher = repeater.Dispatcher();
            mine = dispatcher && dispatcher.HasThreadAccess();
        } catch (...) {
        }
        if (!mine) {
            keep.emplace(id, std::move(watch));
            continue;
        }
        try {
            repeater.ElementClearing(watch.token);
        } catch (...) {
            Wh_Log(L"ERROR: ElementClearing revoke failed");
        }
    }
    if (!keep.empty()) {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        for (auto& [id, watch] : keep) {
            g_repeaterWatches.emplace(id, std::move(watch));
        }
    }
}

// The flyout's realized cards in repeater order, which is the flyout's
// visual order (unlike PositionInSet, which the automation peer may not
// refresh after a thumbnail reorder).
std::vector<FrameworkElement> CollectRepeaterThumbnailViews(
    FrameworkElement thumbView) {
    std::vector<FrameworkElement> out;
    auto repeaterEl = FindAncestorByClassName(
        thumbView, L"Microsoft.UI.Xaml.Controls.ItemsRepeater");
    if (!repeaterEl) {
        return out;
    }
    auto repeater =
        repeaterEl.try_as<winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater>();
    if (!repeater) {
        return out;
    }
    WatchRepeaterElementClearing_UIThread(repeater);
    try {
        auto itemsSourceView = repeater.ItemsSourceView();
        const int count = itemsSourceView ? itemsSourceView.Count() : 0;
        for (int i = 0; i < count; ++i) {
            auto element = repeater.TryGetElement(i);
            if (!element) {
                continue;
            }
            auto child = element.try_as<FrameworkElement>();
            if (!child) {
                continue;
            }
            if (winrt::get_class_name(child) ==
                L"Taskbar.TaskItemThumbnailView") {
                out.push_back(child);
            }
        }
    } catch (...) {
    }
    return out;
}

void TrackThumbView_UIThread(FrameworkElement thumbView) {
    // Untracked without a drainable dispatcher (Neon review 1.4.1).
    if (!thumbView || !RememberUiDispatcher(thumbView)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
    for (auto& weak : g_trackedThumbViews) {
        try {
            if (weak.get() == thumbView) {
                return;
            }
        } catch (...) {
        }
    }
    g_trackedThumbViews.push_back(winrt::make_weak(thumbView));
    if (g_trackedThumbViews.size() > 64) {
        g_trackedThumbViews.erase(
            std::remove_if(
                g_trackedThumbViews.begin(), g_trackedThumbViews.end(),
                [](winrt::weak_ref<FrameworkElement>& weak) {
                    try {
                        return !weak.get();
                    } catch (...) {
                        return true;
                    }
                }),
            g_trackedThumbViews.end());
    }
}

void RemoveNamedDescendant(FrameworkElement root, PCWSTR name) {
    if (!root || !name) {
        return;
    }
    for (int guard = 0; guard < 4; ++guard) {
        auto existing = FindDescendantByName(root, name);
        if (!existing) {
            return;
        }
        try {
            if (auto parent = Media::VisualTreeHelper::GetParent(existing)
                                  .try_as<Controls::Panel>()) {
                uint32_t idx = 0;
                if (parent.Children().IndexOf(existing, idx)) {
                    parent.Children().RemoveAt(idx);
                    continue;
                }
            }
        } catch (...) {
        }
        break;
    }
}

// Find a Panel to host overlays (root Grid under TaskItemThumbnailView).
Controls::Panel GetThumbnailHostPanel(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    try {
        if (auto p = thumbView.try_as<Controls::Panel>()) {
            return p;
        }
        int n = Media::VisualTreeHelper::GetChildrenCount(thumbView);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(thumbView, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }
            if (winrt::get_class_name(child) ==
                L"Windows.UI.Xaml.Controls.Grid") {
                if (auto p = child.try_as<Controls::Panel>()) {
                    return p;
                }
            }
            if (auto p = child.try_as<Controls::Panel>()) {
                return p;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

FrameworkElement FindThumbnailTitleElement(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    static const PCWSTR kNames[] = {
        L"DisplayNameTextBlock",
        L"DisplayName",
        L"TitleTextBlock",
        L"Title",
    };
    for (auto name : kNames) {
        if (auto el = FindDescendantByName(thumbView, name)) {
            if (winrt::get_class_name(el) == L"Windows.UI.Xaml.Controls.TextBlock" ||
                el.try_as<Controls::TextBlock>()) {
                return el;
            }
            if (auto tb = FindDescendantByName(el, L"DisplayNameTextBlock")) {
                return tb;
            }
            return el;
        }
    }
    return nullptr;
}

// Snap-group card in the same flyout as the individual windows. Must not be
// treated as a window thumbnail. Language-independent: the group card hosts
// an IconsRepeater with 2+ icon children and has no window HWND.
bool IsSnapGroupThumbnailView(FrameworkElement view) {
    if (!view) {
        return false;
    }
    if (auto repeater = FindDescendantByName(view, L"IconsRepeater")) {
        try {
            if (Media::VisualTreeHelper::GetChildrenCount(repeater) >= 2) {
                // A real window card that gained a second icon still has an
                // HWND (from its view model). A snap-group card does not.
                if (ResolveHwndFromCardViewModel(view)) {
                    return false;
                }
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

FrameworkElement FindThumbnailBackgroundBorder(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    if (auto b = FindDescendantByName(thumbView, L"BackgroundBorder")) {
        return b;
    }
    // First Border under the root grid (styler targets this).
    try {
        auto panel = GetThumbnailHostPanel(thumbView);
        if (!panel) {
            return nullptr;
        }
        auto fe = panel.as<FrameworkElement>();
        int n = Media::VisualTreeHelper::GetChildrenCount(fe);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(fe, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }
            auto cn = winrt::get_class_name(child);
            // Never our own card overlay (Neon review 1.7.6).
            if (cn == L"Windows.UI.Xaml.Controls.Border" &&
                child.Name() != kThumbCardOverlayName) {
                return child;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

