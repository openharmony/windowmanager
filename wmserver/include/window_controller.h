/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OHOS_ROSEN_WINDOW_CONTROLLER_H
#define OHOS_ROSEN_WINDOW_CONTROLLER_H

#include <refbase.h>
#include <rs_iwindow_animation_controller.h>

#include "accessibility_connection.h"
#include "input_window_monitor.h"
#include "zidl/window_manager_agent_interface.h"
#include "window_root.h"
#include "window_transition_info.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
class WindowController : public RefBase {
public:
    WindowController(sptr<WindowRoot>& root, sptr<InputWindowMonitor> inputWindowMonitor) : windowRoot_(root),
        inputWindowMonitor_(inputWindowMonitor), accessibilityConnection_(new AccessibilityConnection(windowRoot_)) {}
    ~WindowController() = default;

    WMError CreateWindow(sptr<IWindow>& window, sptr<WindowProperty>& property,
        const std::shared_ptr<RSSurfaceNode>& surfaceNode,
        uint32_t& windowId, sptr<IRemoteObject> token, int32_t pid, int32_t uid);
    WMError AddWindowNode(sptr<WindowProperty>& property);
    WMError RemoveWindowNode(uint32_t windowId);
    WMError NotifyWindowTransition(sptr<WindowTransitionInfo>& fromInfo, sptr<WindowTransitionInfo>& toInfo);
    WMError GetFocusWindowInfo(sptr<IRemoteObject>& abilityToken);
    WMError DestroyWindow(uint32_t windowId, bool onlySelf);
    WMError RequestFocus(uint32_t windowId);
    WMError SetAlpha(uint32_t windowId, float alpha);
    AvoidArea GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType) const;
    WMError GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId);
    void NotifyDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type);
    WMError NotifyServerReadyToMoveOrDrag(uint32_t windowId, sptr<MoveDragProperty>& moveDragProperty);
    WMError ProcessPointDown(uint32_t windowId);
    WMError ProcessPointUp(uint32_t windowId);
    void MinimizeAllAppWindows(DisplayId displayId);
    WMError ToggleShownStateForAllAppWindows();
    WMError SetWindowLayoutMode(WindowLayoutMode mode);
    WMError UpdateProperty(sptr<WindowProperty>& property, PropertyChangeAction action);
    void NotifySystemBarTints();
    WMError SetWindowAnimationController(const sptr<RSIWindowAnimationController>& controller);
    WMError GetModeChangeHotZones(DisplayId displayId,
        ModeChangeHotZones& hotZones, const ModeChangeHotZonesConfig& config);
    void StartingWindow(sptr<WindowTransitionInfo> info, sptr<Media::PixelMap> pixelMap,
        uint32_t bkgColor, bool isColdStart);
    void CancelStartingWindow(sptr<IRemoteObject> abilityToken);
    void MinimizeWindowsByLauncher(std::vector<uint32_t>& windowIds, bool isAnimated,
        sptr<RSIWindowAnimationFinishedCallback>& finishCallback);
    Orientation GetWindowPreferredOrientation(DisplayId displayId);
    void OnScreenshot(DisplayId displayId);
    WMError GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo) const;
    WMError BindDialogTarget(uint32_t& windowId, sptr<IRemoteObject> targetToken);
    WMError InterceptInputEventToServer(uint32_t windowId);
    WMError RecoverInputEventToClient(uint32_t windowId);
    WMError NotifyWindowClientPointUp(uint32_t windowId, const std::shared_ptr<MMI::PointerEvent>& pointerEvent);

private:
    uint32_t GenWindowId();
    void FlushWindowInfo(uint32_t windowId);
    void FlushWindowInfoWithDisplayId(DisplayId displayId);
    void UpdateWindowAnimation(const sptr<WindowNode>& node);
    void ProcessDisplayChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type);
    void StopBootAnimationIfNeed(WindowType type) const;
    void RecordBootAnimationEvent() const;
    WMError SetWindowType(uint32_t windowId, WindowType type);
    WMError SetWindowFlags(uint32_t windowId, uint32_t flags);
    WMError SetSystemBarProperty(uint32_t windowId, WindowType type, const SystemBarProperty& property);
    WMError ResizeRect(uint32_t windowId, const Rect& rect, WindowSizeChangeReason reason);
    WMError SetWindowMode(uint32_t windowId, WindowMode dstMode);
    void ResizeSystemBarPropertySizeIfNeed(const sptr<WindowNode>& node);
    void ResizeSoftInputCallingWindowIfNeed(const sptr<WindowNode>& node);
    void RestoreCallingWindowSizeIfNeed();
    void HandleTurnScreenOn(const sptr<WindowNode>& node);
    void ProcessSystemBarChange(const sptr<DisplayInfo>& displayInfo);
    WMError UpdateTouchHotAreas(const sptr<WindowNode>& node, const std::vector<Rect>& rects);
    WMError UpdateTransform(uint32_t windowId);
    void NotifyTouchOutside(const sptr<WindowNode>& node);
    uint32_t GetEmbedNodeId(const std::vector<sptr<WindowNode>>& windowNodes, const sptr<WindowNode>& node);
    void NotifyWindowPropertyChanged(const sptr<WindowNode>& node);
    WMError GetFocusWindowNode(DisplayId displayId, sptr<WindowNode>& windowNode);

    sptr<WindowRoot> windowRoot_;
    sptr<InputWindowMonitor> inputWindowMonitor_;
    sptr<AccessibilityConnection> accessibilityConnection_;
    std::atomic<uint32_t> windowId_ { INVALID_WINDOW_ID };
    // Remove 'sysBarWinId_' after SystemUI resize 'systembar', systemBar only exist on default display currently
    std::unordered_map<WindowType, uint32_t> sysBarWinId_ {
        { WindowType::WINDOW_TYPE_STATUS_BAR,     INVALID_WINDOW_ID },
        { WindowType::WINDOW_TYPE_NAVIGATION_BAR, INVALID_WINDOW_ID },
    };
    bool isScreenLocked_ { false };
    Rect callingWindowRestoringRect_ { 0, 0, 0, 0 };
    uint32_t callingWindowId_ = 0u;
};
} // Rosen
} // OHOS
#endif // OHOS_ROSEN_WINDOW_CONTROLLER_H
