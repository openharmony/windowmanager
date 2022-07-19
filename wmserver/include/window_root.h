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
#ifndef OHOS_ROSEN_WINDOW_ROOT_H
#define OHOS_ROSEN_WINDOW_ROOT_H

#include <refbase.h>
#include <iremote_object.h>
#include <transaction/rs_interfaces.h>

#include "agent_death_recipient.h"
#include "display_manager_service_inner.h"
#include "window_node_container.h"
#include "zidl/window_manager_agent_interface.h"

namespace OHOS {
namespace Rosen {
enum class Event : uint32_t {
    REMOTE_DIED,
};

class WindowRoot : public RefBase {
using Callback = std::function<void (Event event, const sptr<IRemoteObject>& remoteObject)>;

public:
    explicit WindowRoot(Callback callback) : callback_(callback) {}
    ~WindowRoot() = default;

    sptr<WindowNodeContainer> GetOrCreateWindowNodeContainer(DisplayId displayId);
    sptr<WindowNodeContainer> GetWindowNodeContainer(DisplayId displayId);
    sptr<WindowNodeContainer> CreateWindowNodeContainer(sptr<DisplayInfo> displayInfo);
    sptr<WindowNode> GetWindowNode(uint32_t windowId) const;

    WMError SaveWindow(const sptr<WindowNode>& node);
    void AddDeathRecipient(sptr<WindowNode> node);
    sptr<WindowNode> FindWindowNodeWithToken(const sptr<IRemoteObject>& token) const;
    WMError AddWindowNode(uint32_t parentId, sptr<WindowNode>& node, bool fromStartingWin = false);
    WMError RemoveWindowNode(uint32_t windowId);
    WMError DestroyWindow(uint32_t windowId, bool onlySelf);
    WMError UpdateWindowNode(uint32_t windowId, WindowUpdateReason reason);
    bool isVerticalDisplay(sptr<WindowNode>& node) const;
    bool IsForbidDockSliceMove(DisplayId displayId) const;
    bool IsDockSliceInExitSplitModeArea(DisplayId displayId) const;
    void ExitSplitMode(DisplayId displayId);
    void NotifyWindowVisibilityChange(std::shared_ptr<RSOcclusionData> occlusionData);
    void AddSurfaceNodeIdWindowNodePair(uint64_t surfaceNodeId, sptr<WindowNode> node);

    WMError RequestFocus(uint32_t windowId);
    WMError RequestActiveWindow(uint32_t windowId);
    WMError MinimizeStructuredAppWindowsExceptSelf(sptr<WindowNode>& node);
    AvoidArea GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType);
    WMError SetWindowMode(sptr<WindowNode>& node, WindowMode dstMode);
    std::shared_ptr<RSSurfaceNode> GetSurfaceNodeByAbilityToken(const sptr<IRemoteObject>& abilityToken) const;
    WMError GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId);
    void MinimizeAllAppWindows(DisplayId displayId);
    WMError ToggleShownStateForAllAppWindows();
    WMError SetWindowLayoutMode(DisplayId displayId, WindowLayoutMode mode);

    void ProcessWindowStateChange(WindowState state, WindowStateChangeReason reason);
    void ProcessDisplayChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type);
    void ProcessDisplayDestroy(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap);
    void ProcessDisplayCreate(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap);

    void NotifySystemBarTints();
    WMError RaiseZOrderForAppWindow(sptr<WindowNode>& node);
    void FocusFaultDetection() const;
    float GetVirtualPixelRatio(DisplayId displayId) const;
    Rect GetDisplayGroupRect(DisplayId displayId) const;
    WMError UpdateSizeChangeReason(uint32_t windowId, WindowSizeChangeReason reason);
    void SetBrightness(uint32_t windowId, float brightness);
    void HandleKeepScreenOn(uint32_t windowId, bool requireLock);
    void UpdateFocusableProperty(uint32_t windowId);
    WMError GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo);
    void SetMaxAppWindowNumber(int windowNum);
    WMError GetModeChangeHotZones(DisplayId displayId,
        ModeChangeHotZones& hotZones, const ModeChangeHotZonesConfig& config);
    std::vector<DisplayId> GetAllDisplayIds() const;
    uint32_t GetTotalWindowNum() const;
    uint32_t GetWindowIdByObject(const sptr<IRemoteObject>& remoteObject);
    sptr<WindowNode> GetWindowForDumpAceHelpInfo() const;
    void DestroyLeakStartingWindow();
    void SetSplitRatios(const std::vector<float>& splitRatioNumbers);
    void SetExitSplitRatios(const std::vector<float>& exitSplitRatios);
    void MinimizeTargetWindows(std::vector<uint32_t>& windowIds);
    void RemoveSingleUserWindowNodes();
    WMError UpdateRsTree(uint32_t windowId, bool isAdd);
private:
    void OnRemoteDied(const sptr<IRemoteObject>& remoteObject);
    WMError DestroyWindowInner(sptr<WindowNode>& node);
    void UpdateFocusWindowWithWindowRemoved(const sptr<WindowNode>& node,
        const sptr<WindowNodeContainer>& container) const;
    sptr<WindowNode> UpdateActiveWindowWithWindowRemoved(const sptr<WindowNode>& node,
        const sptr<WindowNodeContainer>& container) const;
    void UpdateBrightnessWithWindowRemoved(uint32_t windowId, const sptr<WindowNodeContainer>& container) const;
    std::string GenAllWindowsLogInfo() const;
    bool CheckDisplayInfo(const sptr<DisplayInfo>& display);
    ScreenId GetScreenGroupId(DisplayId displayId, bool& isRecordedDisplay);
    void ProcessExpandDisplayCreate(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
        std::map<DisplayId, Rect>& displayRectMap);
    std::map<DisplayId, sptr<DisplayInfo>> GetAllDisplayInfos(const std::vector<DisplayId>& displayIdVec);
    std::map<DisplayId, Rect> GetAllDisplayRectsByDMS(sptr<DisplayInfo> displayInfo);
    std::map<DisplayId, Rect> GetAllDisplayRectsByDisplayInfo(
        const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap);
    void MoveNotShowingWindowToDefaultDisplay(DisplayId defaultDisplayId, DisplayId displayId);
    WMError PostProcessAddWindowNode(sptr<WindowNode>& node, sptr<WindowNode>& parentNode,
        sptr<WindowNodeContainer>& container);
    std::vector<std::pair<uint64_t, bool>> GetWindowVisibilityChangeInfo(
        std::shared_ptr<RSOcclusionData> occlusionData);
    bool NeedToStopAddingNode(sptr<WindowNode>& node, const sptr<WindowNodeContainer>& container);

    std::map<uint32_t, sptr<WindowNode>> windowNodeMap_;
    std::map<sptr<IRemoteObject>, uint32_t> windowIdMap_;
    std::map<uint64_t, sptr<WindowNode>> surfaceIdWindowNodeMap_;
    std::shared_ptr<RSOcclusionData> lastOcclusionData_ = std::make_shared<RSOcclusionData>();
    std::map<ScreenId, sptr<WindowNodeContainer>> windowNodeContainerMap_;
    std::map<ScreenId, std::vector<DisplayId>> displayIdMap_;

    bool needCheckFocusWindow = false;

    std::map<WindowManagerAgentType, std::vector<sptr<IWindowManagerAgent>>> windowManagerAgents_;

    sptr<AgentDeathRecipient> windowDeath_ = new AgentDeathRecipient(std::bind(&WindowRoot::OnRemoteDied,
        this, std::placeholders::_1));
    Callback callback_;
    uint32_t maxAppWindowNumber_ = 100;
    SplitRatioConfig splitRatioConfig_ = {0.1, 0.9, {}};
};
}
}
#endif // OHOS_ROSEN_WINDOW_ROOT_H
