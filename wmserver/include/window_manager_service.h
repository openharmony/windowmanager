/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#ifndef OHOS_WINDOW_MANAGER_SERVICE_H
#define OHOS_WINDOW_MANAGER_SERVICE_H

#include <vector>
#include <map>

#include <input_window_monitor.h>
#include <nocopyable.h>
#include <system_ability.h>
#include <window_manager_service_handler_stub.h>
#include <transaction/rs_interfaces.h>
#include "atomic_map.h"
#include "display_change_listener.h"
#include "drag_controller.h"
#include "freeze_controller.h"
#include "singleton_delegator.h"
#include "wm_single_instance.h"
#include "window_common_event.h"
#include "window_controller.h"
#include "zidl/window_manager_stub.h"
#include "window_dumper.h"
#include "window_root.h"
#include "window_task_looper.h"
#include "snapshot_controller.h"

namespace OHOS {
namespace Rosen {
class DisplayChangeListener : public IDisplayChangeListener {
public:
    virtual void OnDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type) override;
    virtual void OnGetWindowPreferredOrientation(DisplayId displayId, Orientation &orientation) override;
    virtual void OnScreenshot(DisplayId displayId) override;
};

class WindowManagerServiceHandler : public AAFwk::WindowManagerServiceHandlerStub {
public:
    virtual void NotifyWindowTransition(
        sptr<AAFwk::AbilityTransitionInfo> from, sptr<AAFwk::AbilityTransitionInfo> to) override;
    int32_t GetFocusWindow(sptr<IRemoteObject>& abilityToken) override;
    virtual void StartingWindow(
        sptr<AAFwk::AbilityTransitionInfo> info, sptr<Media::PixelMap> pixelMap, uint32_t bgColor) override;
    virtual void StartingWindow(sptr<AAFwk::AbilityTransitionInfo> info, sptr<Media::PixelMap> pixelMap) override;
    virtual void CancelStartingWindow(sptr<IRemoteObject> abilityToken) override;
};

class RSUIDirector;
class WindowManagerService : public SystemAbility, public WindowManagerStub {
friend class DisplayChangeListener;
friend class WindowManagerServiceHandler;
DECLARE_SYSTEM_ABILITY(WindowManagerService);
WM_DECLARE_SINGLE_INSTANCE_BASE(WindowManagerService);

public:
    void OnStart() override;
    void OnStop() override;
    void OnAddSystemAbility(int32_t systemAbilityId, const std::string &deviceId) override;
    int Dump(int fd, const std::vector<std::u16string>& args) override;

    WMError CreateWindow(sptr<IWindow>& window, sptr<WindowProperty>& property,
        const std::shared_ptr<RSSurfaceNode>& surfaceNode,
        uint32_t& windowId, sptr<IRemoteObject> token) override;
    WMError AddWindow(sptr<WindowProperty>& property) override;
    WMError RemoveWindow(uint32_t windowId) override;
    WMError NotifyWindowTransition(sptr<WindowTransitionInfo>& from, sptr<WindowTransitionInfo>& to,
        bool isFromClient = false) override;
    WMError DestroyWindow(uint32_t windowId, bool onlySelf = false) override;
    WMError RequestFocus(uint32_t windowId) override;
    WMError SetWindowBackgroundBlur(uint32_t windowId, WindowBlurLevel level) override;
    AvoidArea GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType) override;
    void ProcessPointDown(uint32_t windowId, bool isStartDrag) override;
    void ProcessPointUp(uint32_t windowId) override;
    WMError GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId) override;
    void MinimizeAllAppWindows(DisplayId displayId) override;
    WMError ToggleShownStateForAllAppWindows() override;
    WMError SetWindowLayoutMode(WindowLayoutMode mode) override;
    WMError UpdateProperty(sptr<WindowProperty>& windowProperty, PropertyChangeAction action) override;
    WMError GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo) override;
    WMError HandleAddWindow(sptr<WindowProperty>& property);
    WMError HandleRemoveWindow(uint32_t windowId);

    void RegisterWindowManagerAgent(WindowManagerAgentType type,
        const sptr<IWindowManagerAgent>& windowManagerAgent) override;
    void UnregisterWindowManagerAgent(WindowManagerAgentType type,
        const sptr<IWindowManagerAgent>& windowManagerAgent) override;

    WMError SetWindowAnimationController(const sptr<RSIWindowAnimationController>& controller) override;
    WMError GetSystemConfig(SystemConfig& systemConfig) override;
    WMError GetModeChangeHotZones(DisplayId displayId, ModeChangeHotZones& hotZones) override;
    WMError UpdateAvoidAreaListener(uint32_t windowId, bool haveAvoidAreaListener) override;
    void StartingWindow(sptr<WindowTransitionInfo> info, sptr<Media::PixelMap> pixelMap,
        bool isColdStart, uint32_t bkgColor = 0xffffffff);
    void CancelStartingWindow(sptr<IRemoteObject> abilityToken);
    void MinimizeWindowsByLauncher(std::vector<uint32_t> windowIds, bool isAnimated,
        sptr<RSIWindowAnimationFinishedCallback>& finishCallback) override;
    void GetWindowPreferredOrientation(DisplayId displayId, Orientation &orientation);
    void OnAccountSwitched() const;
    WMError UpdateRsTree(uint32_t windowId, bool isAdd) override;
    void OnScreenshot(DisplayId displayId);
protected:
    WindowManagerService();
    virtual ~WindowManagerService() = default;

private:
    bool Init();
    void RegisterSnapshotHandler();
    void RegisterWindowManagerServiceHandler();
    void RegisterWindowVisibilityChangeCallback();
    void WindowVisibilityChangeCallback(std::shared_ptr<RSOcclusionData> occlusionData);
    void OnWindowEvent(Event event, const sptr<IRemoteObject>& remoteObject);
    void NotifyDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type);
    WMError GetFocusWindowInfo(sptr<IRemoteObject>& abilityToken);
    void ConfigureWindowManagerService();

    static inline SingletonDelegator<WindowManagerService> delegator;
    AtomicMap<uint32_t, uint32_t> accessTokenIdMaps_;
    sptr<WindowRoot> windowRoot_;
    sptr<WindowController> windowController_;
    sptr<InputWindowMonitor> inputWindowMonitor_;
    sptr<SnapshotController> snapshotController_;
    sptr<WindowManagerServiceHandler> wmsHandler_;
    sptr<DragController> dragController_;
    sptr<FreezeController> freezeDisplayController_;
    sptr<WindowDumper> windowDumper_;
    SystemConfig systemConfig_;
    ModeChangeHotZonesConfig hotZonesConfig_ { false, 0, 0, 0 };
    std::unique_ptr<WindowTaskLooper> wmsTaskLooper_;
    std::shared_ptr<WindowCommonEvent> windowCommonEvent_;
    RSInterfaces& rsInterface_;
    bool startingOpen_ = true;
    std::shared_ptr<RSUIDirector> rsUiDirector_;
};
} // namespace Rosen
} // namespace OHOS
#endif // OHOS_WINDOW_MANAGER_SERVICE_H
