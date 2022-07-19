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

#include "window_root.h"
#include <ability_manager_client.h>
#include <cinttypes>
#include <display_power_mgr_client.h>
#include <hisysevent.h>
#include <hitrace_meter.h>
#include <transaction/rs_transaction.h>

#include "display_manager_service_inner.h"
#include "window_helper.h"
#include "window_manager_hilog.h"
#include "window_manager_service.h"
#include "window_manager_agent_controller.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowRoot"};
}

uint32_t WindowRoot::GetTotalWindowNum() const
{
    return static_cast<uint32_t>(windowNodeMap_.size());
}

sptr<WindowNode> WindowRoot::GetWindowForDumpAceHelpInfo() const
{
    for (auto& iter : windowNodeMap_) {
        if (iter.second->GetWindowType() == WindowType::WINDOW_TYPE_DESKTOP ||
            iter.second->GetWindowType() == WindowType::WINDOW_TYPE_NAVIGATION_BAR ||
            iter.second->GetWindowType() == WindowType::WINDOW_TYPE_STATUS_BAR ||
            iter.second->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
            return iter.second;
        }
    }
    return nullptr;
}

ScreenId WindowRoot::GetScreenGroupId(DisplayId displayId, bool& isRecordedDisplay)
{
    for (auto iter : displayIdMap_) {
        auto displayIdVec = iter.second;
        if (std::find(displayIdVec.begin(), displayIdVec.end(), displayId) != displayIdVec.end()) {
            isRecordedDisplay = true;
            return iter.first;
        }
    }
    isRecordedDisplay = false;
    WLOGFE("Current display is not be recorded, displayId: %{public}" PRIu64 "", displayId);
    return DisplayManagerServiceInner::GetInstance().GetScreenGroupIdByDisplayId(displayId);
}

sptr<WindowNodeContainer> WindowRoot::GetOrCreateWindowNodeContainer(DisplayId displayId)
{
    auto container = GetWindowNodeContainer(displayId);
    if (container != nullptr) {
        return container;
    }

    // In case of have no container for default display, create container
    WLOGFI("Create container for current display, displayId: %{public}" PRIu64 "", displayId);
    sptr<DisplayInfo> displayInfo = DisplayManagerServiceInner::GetInstance().GetDisplayById(displayId);
    return CreateWindowNodeContainer(displayInfo);
}

sptr<WindowNodeContainer> WindowRoot::GetWindowNodeContainer(DisplayId displayId)
{
    bool isRecordedDisplay;
    sptr<DisplayInfo> displayInfo = DisplayManagerServiceInner::GetInstance().GetDisplayById(displayId);
    ScreenId displayGroupId = GetScreenGroupId(displayId, isRecordedDisplay);
    auto iter = windowNodeContainerMap_.find(displayGroupId);
    if (iter != windowNodeContainerMap_.end()) {
        // if container exist for screenGroup and display is not be recorded, process expand display
        if (!isRecordedDisplay) {
            // add displayId in displayId vector
            displayIdMap_[displayGroupId].push_back(displayId);
            auto displayRectMap = GetAllDisplayRectsByDMS(displayInfo);
            DisplayId defaultDisplayId = DisplayManagerServiceInner::GetInstance().GetDefaultDisplayId();
            ProcessExpandDisplayCreate(defaultDisplayId, displayInfo, displayRectMap);
        }
        return iter->second;
    }
    return nullptr;
}

sptr<WindowNodeContainer> WindowRoot::CreateWindowNodeContainer(sptr<DisplayInfo> displayInfo)
{
    if (displayInfo == nullptr || !CheckDisplayInfo(displayInfo)) {
        WLOGFE("get display failed or get invalid display info");
        return nullptr;
    }

    DisplayId displayId = displayInfo->GetDisplayId();
    ScreenId displayGroupId = displayInfo->GetScreenGroupId();
    WLOGFI("create new container for display, width: %{public}d, height: %{public}d, "
        "displayGroupId:%{public}" PRIu64", displayId:%{public}" PRIu64"", displayInfo->GetWidth(),
        displayInfo->GetHeight(), displayGroupId, displayId);
    sptr<WindowNodeContainer> container = new WindowNodeContainer(displayInfo, displayGroupId);
    windowNodeContainerMap_.insert(std::make_pair(displayGroupId, container));
    std::vector<DisplayId> displayVec = { displayId };
    displayIdMap_.insert(std::make_pair(displayGroupId, displayVec));
    if (container == nullptr) {
        WLOGFE("create container failed, displayId :%{public}" PRIu64 "", displayId);
        return nullptr;
    }
    container->GetLayoutPolicy()->SetSplitRatioConfig(splitRatioConfig_);
    return container;
}

bool WindowRoot::CheckDisplayInfo(const sptr<DisplayInfo>& display)
{
    const int32_t minWidth = 50;
    const int32_t minHeight = 50;
    const int32_t maxWidth = 7680;
    const int32_t maxHeight = 7680; // 8k resolution
    if (display->GetWidth() < minWidth || display->GetWidth() > maxWidth ||
        display->GetHeight() < minHeight || display->GetHeight() > maxHeight) {
        return false;
    }
    return true;
}

sptr<WindowNode> WindowRoot::GetWindowNode(uint32_t windowId) const
{
    auto iter = windowNodeMap_.find(windowId);
    if (iter == windowNodeMap_.end()) {
        return nullptr;
    }
    return iter->second;
}

sptr<WindowNode> WindowRoot::FindWindowNodeWithToken(const sptr<IRemoteObject>& token) const
{
    if (token == nullptr) {
        WLOGFE("token is null");
        return nullptr;
    }
    auto iter = std::find_if(windowNodeMap_.begin(), windowNodeMap_.end(),
        [token](const std::map<uint32_t, sptr<WindowNode>>::value_type& pair) {
            if (!(WindowHelper::IsSubWindow(pair.second->GetWindowType()))) {
                return pair.second->abilityToken_ == token;
            }
            return false;
        });
    if (iter == windowNodeMap_.end()) {
        WLOGFI("cannot find windowNode");
        return nullptr;
    }
    return iter->second;
}

void WindowRoot::AddDeathRecipient(sptr<WindowNode> node)
{
    if (node == nullptr) {
        WLOGFE("AddDeathRecipient failed, node is nullptr");
        return;
    }

    auto remoteObject = node->GetWindowToken()->AsObject();
    windowIdMap_.insert(std::make_pair(remoteObject, node->GetWindowId()));

    if (windowDeath_ == nullptr) {
        WLOGFI("failed to create death Recipient ptr WindowDeathRecipient");
        return;
    }
    if (!remoteObject->AddDeathRecipient(windowDeath_)) {
        WLOGFI("failed to add death recipient");
    }
}

WMError WindowRoot::SaveWindow(const sptr<WindowNode>& node)
{
    if (node == nullptr) {
        WLOGFE("add window failed, node is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }

    WLOGFI("save windowId %{public}u", node->GetWindowId());
    windowNodeMap_.insert(std::make_pair(node->GetWindowId(), node));
    if (node->surfaceNode_ != nullptr) {
        surfaceIdWindowNodeMap_.insert(std::make_pair(node->surfaceNode_->GetId(), node));
    }
    if (node->GetWindowToken()) {
        AddDeathRecipient(node);
    }
    // Register FirstFrame Callback to rs, inform ability to get snapshot
    wptr<WindowNode> weak = node;
    auto firstFrameCompleteCallback = [weak]() {
        auto weakNode = weak.promote();
        if (weakNode == nullptr) {
            WLOGFE("windowNode is nullptr");
            return;
        }
        AAFwk::AbilityManagerClient::GetInstance()->CompleteFirstFrameDrawing(weakNode->abilityToken_);
    };
    if (node->surfaceNode_ && WindowHelper::IsMainWindow(node->GetWindowType())) {
        node->surfaceNode_->SetBufferAvailableCallback(firstFrameCompleteCallback);
    }
    return WMError::WM_OK;
}

WMError WindowRoot::MinimizeStructuredAppWindowsExceptSelf(sptr<WindowNode>& node)
{
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "root:MinimizeStructuredAppWindowsExceptSelf");
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("MinimizeAbility failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    return container->MinimizeStructuredAppWindowsExceptSelf(node);
}

void WindowRoot::MinimizeTargetWindows(std::vector<uint32_t>& windowIds)
{
    for (auto& windowId : windowIds) {
        if (windowNodeMap_.count(windowId) != 0) {
            auto windowNode = windowNodeMap_[windowId];
            if (windowNode->GetWindowType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW) {
                MinimizeApp::AddNeedMinimizeApp(windowNode, MinimizeReason::GESTURE_ANIMATION);
            } else {
                WLOGFE("Minimize window failed id: %{public}u, type: %{public}u",
                    windowNode->GetWindowId(), static_cast<uint32_t>(windowNode->GetWindowType()));
            }
        }
    }
}

bool WindowRoot::IsForbidDockSliceMove(DisplayId displayId) const
{
    auto container = const_cast<WindowRoot*>(this)->GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("can't find container");
        return true;
    }
    return container->IsForbidDockSliceMove(displayId);
}

bool WindowRoot::IsDockSliceInExitSplitModeArea(DisplayId displayId) const
{
    auto container = const_cast<WindowRoot*>(this)->GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("can't find container");
        return false;
    }
    return container->IsDockSliceInExitSplitModeArea(displayId);
}

void WindowRoot::ExitSplitMode(DisplayId displayId)
{
    auto container = GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("can't find container");
        return;
    }
    container->ExitSplitMode(displayId);
}

void WindowRoot::AddSurfaceNodeIdWindowNodePair(uint64_t surfaceNodeId, sptr<WindowNode> node)
{
    surfaceIdWindowNodeMap_.insert(std::make_pair(surfaceNodeId, node));
}

std::vector<std::pair<uint64_t, bool>> WindowRoot::GetWindowVisibilityChangeInfo(
    std::shared_ptr<RSOcclusionData> occlusionData)
{
    std::vector<std::pair<uint64_t, bool>> visibilityChangeInfo;
    VisibleData& currentVisibleWindow = occlusionData->GetVisibleData();
    std::sort(currentVisibleWindow.begin(), currentVisibleWindow.end());
    VisibleData& lastVisibleWindow = lastOcclusionData_->GetVisibleData();
    uint32_t i, j;
    i = j = 0;
    for (; i < lastVisibleWindow.size() && j < currentVisibleWindow.size();) {
        if (lastVisibleWindow[i] < currentVisibleWindow[j]) {
            visibilityChangeInfo.emplace_back(lastVisibleWindow[i], false);
            i++;
        } else if (lastVisibleWindow[i] > currentVisibleWindow[j]) {
            visibilityChangeInfo.emplace_back(currentVisibleWindow[j], true);
            j++;
        } else {
            i++;
            j++;
        }
    }
    for (; i < lastVisibleWindow.size(); ++i) {
        visibilityChangeInfo.emplace_back(lastVisibleWindow[i], false);
    }
    for (; j < currentVisibleWindow.size(); ++j) {
        visibilityChangeInfo.emplace_back(currentVisibleWindow[j], true);
    }
    lastOcclusionData_ = occlusionData;
    return visibilityChangeInfo;
}

void WindowRoot::NotifyWindowVisibilityChange(std::shared_ptr<RSOcclusionData> occlusionData)
{
    std::vector<std::pair<uint64_t, bool>> visibilityChangeInfo = GetWindowVisibilityChangeInfo(occlusionData);
    std::vector<sptr<WindowVisibilityInfo>> windowVisibilityInfos;
    for (const auto& elem : visibilityChangeInfo) {
        uint64_t surfaceId = elem.first;
        bool isVisible = elem.second;
        auto iter = surfaceIdWindowNodeMap_.find(surfaceId);
        if (iter == surfaceIdWindowNodeMap_.end()) {
            continue;
        }
        sptr<WindowNode> node = iter->second;
        if (node == nullptr) {
            continue;
        }
        node->isVisible_ = isVisible;
        windowVisibilityInfos.emplace_back(new WindowVisibilityInfo(node->GetWindowId(), node->GetCallingPid(),
            node->GetCallingUid(), isVisible, node->GetWindowType()));
        WLOGFD("NotifyWindowVisibilityChange: covered status changed window:%{public}u, isVisible:%{public}d",
            node->GetWindowId(), isVisible);
    }
    if (windowVisibilityInfos.size() != 0) {
        WindowManagerAgentController::GetInstance().UpdateWindowVisibilityInfo(windowVisibilityInfos);
    }
}

AvoidArea WindowRoot::GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType)
{
    AvoidArea avoidArea;
    sptr<WindowNode> node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return avoidArea;
    }
    sptr<WindowNodeContainer> container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("add window failed, window container could not be found");
        return avoidArea;
    }
    return container->GetAvoidAreaByType(node, avoidAreaType);
}

void WindowRoot::MinimizeAllAppWindows(DisplayId displayId)
{
    auto container = GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("can't find window node container, failed!");
        return;
    }
    return container->MinimizeAllAppWindows(displayId);
}

WMError WindowRoot::ToggleShownStateForAllAppWindows()
{
    std::vector<DisplayId> displays = DisplayManagerServiceInner::GetInstance().GetAllDisplayIds();
    std::vector<sptr<WindowNodeContainer>> containers;
    bool isAllAppWindowsEmpty = true;
    for (auto displayId : displays) {
        auto container = GetOrCreateWindowNodeContainer(displayId);
        if (container == nullptr) {
            WLOGFE("can't find window node container, failed!");
            continue;
        }
        containers.emplace_back(container);
        isAllAppWindowsEmpty = isAllAppWindowsEmpty && container->IsAppWindowsEmpty();
    }
    WMError res = WMError::WM_OK;
    std::for_each(containers.begin(), containers.end(),
        [this, isAllAppWindowsEmpty, &res] (sptr<WindowNodeContainer> container) {
        auto restoreFunc = [this](uint32_t windowId, WindowMode mode) {
            auto windowNode = GetWindowNode(windowId);
            if (windowNode == nullptr) {
                return false;
            }
            if (!windowNode->GetWindowToken()) {
                return false;
            }
            auto property = windowNode->GetWindowToken()->GetWindowProperty();
            if (property == nullptr) {
                return false;
            }
            if (mode == WindowMode::WINDOW_MODE_SPLIT_PRIMARY ||
                mode == WindowMode::WINDOW_MODE_SPLIT_SECONDARY) {
                property->SetWindowMode(mode);
            }
            windowNode->GetWindowToken()->UpdateWindowState(WindowState::STATE_SHOWN);
            WindowManagerService::GetInstance().HandleAddWindow(property);
            return true;
        };
        WMError tmpRes = tmpRes = container->ToggleShownStateForAllAppWindows(restoreFunc, isAllAppWindowsEmpty);
        res = (res == WMError::WM_OK) ? tmpRes : res;
    });
    return res;
}

void WindowRoot::DestroyLeakStartingWindow()
{
    WLOGFI("DestroyLeakStartingWindow is called");
    std::vector<uint32_t> destroyIds;
    for (auto& iter : windowNodeMap_) {
        if (iter.second->startingWindowShown_ && !iter.second->GetWindowToken()) {
            destroyIds.push_back(iter.second->GetWindowId());
        }
    }
    for (auto& id : destroyIds) {
        WLOGFI("Destroy Window id:%{public}u", id);
        DestroyWindow(id, false);
    }
}

WMError WindowRoot::PostProcessAddWindowNode(sptr<WindowNode>& node, sptr<WindowNode>& parentNode,
    sptr<WindowNodeContainer>& container)
{
    if (WindowHelper::IsSubWindow(node->GetWindowType())) {
        if (parentNode == nullptr) {
            WLOGFE("window type is invalid");
            return WMError::WM_ERROR_INVALID_TYPE;
        }
        sptr<WindowNode> parent = nullptr;
        container->RaiseZOrderForAppWindow(parentNode, parent);
    }
    if (node->GetWindowProperty()->GetFocusable()) {
        container->SetFocusWindow(node->GetWindowId());
        needCheckFocusWindow = true;
    }
    container->SetActiveWindow(node->GetWindowId(), false);

    for (auto& child : node->children_) {
        if (child == nullptr || !child->currentVisibility_) {
            break;
        }
        HandleKeepScreenOn(child->GetWindowId(), child->IsKeepScreenOn());
    }
    HandleKeepScreenOn(node->GetWindowId(), node->IsKeepScreenOn());
    WLOGFI("windowId:%{public}u, name:%{public}s, orientation:%{public}u, type:%{public}u, isMainWindow:%{public}d",
        node->GetWindowId(), node->GetWindowName().c_str(), static_cast<uint32_t>(node->GetRequestedOrientation()),
        node->GetWindowType(), WindowHelper::IsMainWindow(node->GetWindowType()));
    if (WindowHelper::IsRotatableWindow(node->GetWindowType(), node->GetWindowMode())) {
        DisplayManagerServiceInner::GetInstance().
            SetOrientationFromWindow(node->GetDisplayId(), node->GetRequestedOrientation());
    }
    return WMError::WM_OK;
}

bool WindowRoot::NeedToStopAddingNode(sptr<WindowNode>& node, const sptr<WindowNodeContainer>& container)
{
    if (!WindowHelper::IsMainWindow(node->GetWindowType())) {
        return false;
    }
    // intercept the node which doesn't support floating mode at tile mode
    if (WindowHelper::IsInvalidWindowInTileLayoutMode(node->GetModeSupportInfo(), container->GetCurrentLayoutMode())) {
        WLOGFE("window doesn't support floating mode in tile, windowId: %{public}u", node->GetWindowId());
        return true;
    }
    // intercept the node that the tile rect can't be applied to
    WMError res = container->IsTileRectSatisfiedWithSizeLimits(node);
    if (res != WMError::WM_OK) {
        return true;
    }
    return false;
}

WMError WindowRoot::AddWindowNode(uint32_t parentId, sptr<WindowNode>& node, bool fromStartingWin)
{
    if (node == nullptr) {
        WLOGFE("add window failed, node is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }

    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("add window failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }

    if (NeedToStopAddingNode(node, container)) { // true means stop adding
        return WMError::WM_ERROR_INVALID_WINDOW_MODE_OR_SIZE;
    }

    if (node->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN &&
        WindowHelper::IsAppWindow(node->GetWindowType()) && !node->isPlayAnimationShow_) {
        container->NotifyDockWindowStateChanged(node, false);
        WMError res = MinimizeStructuredAppWindowsExceptSelf(node);
        if (res != WMError::WM_OK) {
            WLOGFE("Minimize other structured window failed");
            MinimizeApp::ClearNodesWithReason(MinimizeReason::OTHER_WINDOW);
            return res;
        }
    }
    if (fromStartingWin) {
        WMError res = container->ShowStartingWindow(node);
        if (res != WMError::WM_OK) {
            MinimizeApp::ClearNodesWithReason(MinimizeReason::OTHER_WINDOW);
        }
        return res;
    }
    // limit number of main window
    uint32_t mainWindowNumber = container->GetWindowCountByType(WindowType::WINDOW_TYPE_APP_MAIN_WINDOW);
    if (mainWindowNumber >= maxAppWindowNumber_ && node->GetWindowType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW) {
        container->MinimizeOldestAppWindow();
    }

    auto parentNode = GetWindowNode(parentId);
    WMError res = container->AddWindowNode(node, parentNode);
    if (!WindowHelper::IsSystemWindow(node->GetWindowType())) {
        DestroyLeakStartingWindow();
    }
    if (res != WMError::WM_OK) {
        WLOGFE("AddWindowNode failed with ret: %{public}u", static_cast<uint32_t>(res));
        return res;
    }

    return PostProcessAddWindowNode(node, parentNode, container);
}

WMError WindowRoot::RemoveWindowNode(uint32_t windowId)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("remove window failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    container->DropShowWhenLockedWindowIfNeeded(node);
    UpdateFocusWindowWithWindowRemoved(node, container);
    auto nextOrientationWindow = UpdateActiveWindowWithWindowRemoved(node, container);
    UpdateBrightnessWithWindowRemoved(windowId, container);
    WMError res = container->RemoveWindowNode(node);
    if (res == WMError::WM_OK) {
        for (auto& child : node->children_) {
            if (child == nullptr) {
                break;
            }
            HandleKeepScreenOn(child->GetWindowId(), false);
        }
        HandleKeepScreenOn(windowId, false);
    }
    while (nextOrientationWindow != nullptr && !WindowHelper::IsMainWindow(nextOrientationWindow->GetWindowType())) {
        nextOrientationWindow = nextOrientationWindow->parent_;
    }
    if (nextOrientationWindow != nullptr && WindowHelper::IsRotatableWindow(
        nextOrientationWindow->GetWindowType(), nextOrientationWindow->GetWindowMode())) {
        DisplayManagerServiceInner::GetInstance().SetOrientationFromWindow(nextOrientationWindow->GetDisplayId(),
            nextOrientationWindow->GetRequestedOrientation());
    }
    return res;
}

WMError WindowRoot::UpdateWindowNode(uint32_t windowId, WindowUpdateReason reason)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("update window failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    return container->UpdateWindowNode(node, reason);
}

WMError WindowRoot::UpdateSizeChangeReason(uint32_t windowId, WindowSizeChangeReason reason)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("update window size change reason failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    container->UpdateSizeChangeReason(node, reason);
    return WMError::WM_OK;
}

void WindowRoot::SetBrightness(uint32_t windowId, float brightness)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("set brightness failed, window container could not be found");
        return;
    }
    if (!WindowHelper::IsAppWindow(node->GetWindowType())) {
        WLOGFI("non app window does not support set brightness");
        return;
    }
    if (windowId == container->GetActiveWindow()) {
        if (container->GetDisplayBrightness() != brightness) {
            WLOGFI("set brightness with value: %{public}u", container->ToOverrideBrightness(brightness));
            DisplayPowerMgr::DisplayPowerMgrClient::GetInstance().OverrideBrightness(
                container->ToOverrideBrightness(brightness));
            container->SetDisplayBrightness(brightness);
        }
        container->SetBrightnessWindow(windowId);
    }
}

void WindowRoot::HandleKeepScreenOn(uint32_t windowId, bool requireLock)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("handle keep screen on failed, window container could not be found");
        return;
    }
    container->HandleKeepScreenOn(node, requireLock);
}

void WindowRoot::UpdateFocusableProperty(uint32_t windowId)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("handle focusable failed, window container could not be found");
        return;
    }

    if (windowId != container->GetFocusWindow() || node->GetWindowProperty()->GetFocusable()) {
        return;
    }
    auto nextFocusableWindow = container->GetNextFocusableWindow(windowId);
    if (nextFocusableWindow != nullptr) {
        WLOGFI("adjust focus window, next focus window id: %{public}u", nextFocusableWindow->GetWindowId());
        container->SetFocusWindow(nextFocusableWindow->GetWindowId());
    }
}

WMError WindowRoot::SetWindowMode(sptr<WindowNode>& node, WindowMode dstMode)
{
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("set window mode failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto res = container->SetWindowMode(node, dstMode);
    if (WindowHelper::IsRotatableWindow(node->GetWindowType(), node->GetWindowMode())) {
        DisplayManagerServiceInner::GetInstance().
            SetOrientationFromWindow(node->GetDisplayId(), node->GetRequestedOrientation());
    }
    return res;
}

WMError WindowRoot::DestroyWindow(uint32_t windowId, bool onlySelf)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFW("Window mode is destroyed or not created");
        return WMError::WM_OK;
    }
    WMError res;
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container != nullptr) {
        UpdateFocusWindowWithWindowRemoved(node, container);
        UpdateActiveWindowWithWindowRemoved(node, container);
        UpdateBrightnessWithWindowRemoved(windowId, container);
        HandleKeepScreenOn(windowId, false);
        if (onlySelf) {
            for (auto& child : node->children_) {
                child->parent_ = nullptr;
            }
            res = container->RemoveWindowNode(node);
            if (res != WMError::WM_OK) {
                WLOGFE("RemoveWindowNode failed");
            }
            return DestroyWindowInner(node);
        } else {
            std::vector<uint32_t> windowIds;
            res = container->DestroyWindowNode(node, windowIds);
            for (auto id : windowIds) {
                node = GetWindowNode(id);
                if (node != nullptr) {
                    HandleKeepScreenOn(id, false);
                    DestroyWindowInner(node);
                }
            }
            return res;
        }
    }
    res = DestroyWindowInner(node);
    WLOGFI("destroy window failed, window container could not be found");
    return res;
}

WMError WindowRoot::DestroyWindowInner(sptr<WindowNode>& node)
{
    if (node == nullptr) {
        WLOGFE("window has been destroyed");
        return WMError::WM_ERROR_DESTROYED_OBJECT;
    }

    std::vector<sptr<WindowVisibilityInfo>> windowVisibilityInfos;
    node->isVisible_ = false;
    windowVisibilityInfos.emplace_back(new WindowVisibilityInfo(node->GetWindowId(), node->GetCallingPid(),
        node->GetCallingUid(), false, node->GetWindowType()));
    WLOGFD("NotifyWindowVisibilityChange: covered status changed window:%{public}u, isVisible:%{public}d",
        node->GetWindowId(), node->isVisible_);
    WindowManagerAgentController::GetInstance().UpdateWindowVisibilityInfo(windowVisibilityInfos);

    auto cmpFunc = [node](const std::map<uint64_t, sptr<WindowNode>>::value_type& pair) {
        if (pair.second == nullptr) {
            return false;
        }
        if (pair.second->GetWindowId() == node->GetWindowId()) {
            return true;
        }
        return false;
    };
    auto iter = std::find_if(surfaceIdWindowNodeMap_.begin(), surfaceIdWindowNodeMap_.end(), cmpFunc);
    if (iter != surfaceIdWindowNodeMap_.end()) {
        surfaceIdWindowNodeMap_.erase(iter);
    }

    sptr<IWindow> window = node->GetWindowToken();
    if ((window != nullptr) && (window->AsObject() != nullptr)) {
        if (windowIdMap_.count(window->AsObject()) == 0) {
            WLOGFI("window remote object has been destroyed");
            return WMError::WM_ERROR_DESTROYED_OBJECT;
        }

        if (window->AsObject() != nullptr) {
            window->AsObject()->RemoveDeathRecipient(windowDeath_);
        }
        windowIdMap_.erase(window->AsObject());
    }
    windowNodeMap_.erase(node->GetWindowId());
    WLOGFI("destroy window node use_count:%{public}d", node->GetSptrRefCount());
    return WMError::WM_OK;
}

void WindowRoot::UpdateFocusWindowWithWindowRemoved(const sptr<WindowNode>& node,
    const sptr<WindowNodeContainer>& container) const
{
    if (node == nullptr || container == nullptr) {
        WLOGFE("window is invalid");
        return;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
        WLOGFI("window is divider, do not get next focus window.");
        return;
    }
    uint32_t windowId = node->GetWindowId();
    uint32_t focusedWindowId = container->GetFocusWindow();
    WLOGFI("current window: %{public}u, focus window: %{public}u", windowId, focusedWindowId);
    if (WindowHelper::IsMainWindow(node->GetWindowType())) {
        if (windowId != focusedWindowId) {
            auto iter = std::find_if(node->children_.begin(), node->children_.end(),
                                     [focusedWindowId](sptr<WindowNode> node) {
                                         return node->GetWindowId() == focusedWindowId;
                                     });
            if (iter == node->children_.end()) {
                return;
            }
        }
        if (!node->children_.empty()) {
            auto firstChild = node->children_.front();
            if (firstChild->priority_ < 0) {
                windowId = firstChild->GetWindowId();
            }
        }
    } else {
        if (windowId != focusedWindowId) {
            return;
        }
    }
    auto nextFocusableWindow = container->GetNextFocusableWindow(windowId);
    if (nextFocusableWindow != nullptr) {
        WLOGFI("adjust focus window, next focus window id: %{public}u", nextFocusableWindow->GetWindowId());
        container->SetFocusWindow(nextFocusableWindow->GetWindowId());
    }
}

sptr<WindowNode> WindowRoot::UpdateActiveWindowWithWindowRemoved(const sptr<WindowNode>& node,
    const sptr<WindowNodeContainer>& container) const
{
    if (node == nullptr || container == nullptr) {
        WLOGFE("window is invalid");
        return nullptr;
    }
    uint32_t windowId = node->GetWindowId();
    uint32_t activeWindowId = container->GetActiveWindow();
    WLOGFI("current window: %{public}u, active window: %{public}u", windowId, activeWindowId);
    if (WindowHelper::IsMainWindow(node->GetWindowType())) {
        if (windowId != activeWindowId) {
            auto iter = std::find_if(node->children_.begin(), node->children_.end(),
                                     [activeWindowId](sptr<WindowNode> node) {
                                         return node->GetWindowId() == activeWindowId;
                                     });
            if (iter == node->children_.end()) {
                return nullptr;
            }
        }
        if (!node->children_.empty()) {
            auto firstChild = node->children_.front();
            if (firstChild->priority_ < 0) {
                windowId = firstChild->GetWindowId();
            }
        }
    } else {
        if (windowId != activeWindowId) {
            return nullptr;
        }
    }
    auto nextActiveWindow = container->GetNextActiveWindow(windowId);
    if (nextActiveWindow != nullptr) {
        WLOGFI("adjust active window, next active window id: %{public}u", nextActiveWindow->GetWindowId());
        container->SetActiveWindow(nextActiveWindow->GetWindowId(), true);
    }
    return nextActiveWindow;
}

void WindowRoot::UpdateBrightnessWithWindowRemoved(uint32_t windowId, const sptr<WindowNodeContainer>& container) const
{
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return;
    }
    if (windowId == container->GetBrightnessWindow()) {
        WLOGFI("adjust brightness window with active window: %{public}u", container->GetActiveWindow());
        container->UpdateBrightness(container->GetActiveWindow(), true);
    }
}

bool WindowRoot::isVerticalDisplay(sptr<WindowNode>& node) const
{
    auto container = const_cast<WindowRoot*>(this)->GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("get display direction failed, window container could not be found");
        return false;
    }
    return container->isVerticalDisplay(node->GetDisplayId());
}

WMError WindowRoot::RequestFocus(uint32_t windowId)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (!node->currentVisibility_) {
        WLOGFE("could not request focus before it does not be shown");
        return WMError::WM_ERROR_INVALID_OPERATION;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (node->GetWindowProperty()->GetFocusable()) {
        return container->SetFocusWindow(windowId);
    }
    return WMError::WM_ERROR_INVALID_OPERATION;
}

WMError WindowRoot::RequestActiveWindow(uint32_t windowId)
{
    auto node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto res =  container->SetActiveWindow(windowId, false);
    WLOGFI("windowId:%{public}u, name:%{public}s, orientation:%{public}u, type:%{public}u, isMainWindow:%{public}d",
        windowId, node->GetWindowName().c_str(), static_cast<uint32_t>(node->GetRequestedOrientation()),
        node->GetWindowType(), WindowHelper::IsMainWindow(node->GetWindowType()));
    if (res == WMError::WM_OK &&
        WindowHelper::IsRotatableWindow(node->GetWindowType(), node->GetWindowMode())) {
        DisplayManagerServiceInner::GetInstance().
            SetOrientationFromWindow(node->GetDisplayId(), node->GetRequestedOrientation());
    }
    return res;
}

std::shared_ptr<RSSurfaceNode> WindowRoot::GetSurfaceNodeByAbilityToken(const sptr<IRemoteObject>& abilityToken) const
{
    for (const auto& iter : windowNodeMap_) {
        if (iter.second->abilityToken_ != abilityToken) {
            continue;
        }
        return iter.second->surfaceNode_;
    }
    WLOGFE("could not find required abilityToken!");
    return nullptr;
}

void WindowRoot::ProcessWindowStateChange(WindowState state, WindowStateChangeReason reason)
{
    for (auto& elem : windowNodeContainerMap_) {
        if (elem.second == nullptr) {
            continue;
        }
        elem.second->ProcessWindowStateChange(state, reason);
    }
}

void WindowRoot::NotifySystemBarTints()
{
    WLOGFD("notify current system bar tints");
    for (auto& it : windowNodeContainerMap_) {
        if (it.second != nullptr) {
            it.second->NotifySystemBarTints(displayIdMap_[it.first]);
        }
    }
}

WMError WindowRoot::RaiseZOrderForAppWindow(sptr<WindowNode>& node)
{
    if (node == nullptr) {
        WLOGFW("add window failed, node is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
        auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
        if (container == nullptr) {
            WLOGFW("window container could not be found");
            return WMError::WM_ERROR_NULLPTR;
        }
        container->RaiseSplitRelatedWindowToTop(node);
        return WMError::WM_OK;
    }

    if (!WindowHelper::IsAppWindow(node->GetWindowType())) {
        WLOGFW("window is not app window");
        return WMError::WM_ERROR_INVALID_TYPE;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFW("add window failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }

    auto parentNode = GetWindowNode(node->GetParentId());
    return container->RaiseZOrderForAppWindow(node, parentNode);
}

uint32_t WindowRoot::GetWindowIdByObject(const sptr<IRemoteObject>& remoteObject)
{
    auto iter = windowIdMap_.find(remoteObject);
    return iter == std::end(windowIdMap_) ? INVALID_WINDOW_ID : iter->second;
}

void WindowRoot::OnRemoteDied(const sptr<IRemoteObject>& remoteObject)
{
    callback_(Event::REMOTE_DIED, remoteObject);
}

WMError WindowRoot::GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId)
{
    if (windowNodeMap_.find(mainWinId) == windowNodeMap_.end()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    auto node = windowNodeMap_[mainWinId];
    if (!node->currentVisibility_) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (!node->children_.empty()) {
        auto iter = node->children_.rbegin();
        if (WindowHelper::IsSubWindow((*iter)->GetWindowType())) {
            topWinId = (*iter)->GetWindowId();
            return WMError::WM_OK;
        }
    }
    topWinId = mainWinId;
    return WMError::WM_OK;
}

WMError WindowRoot::SetWindowLayoutMode(DisplayId displayId, WindowLayoutMode mode)
{
    auto container = GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    WMError ret = container->SwitchLayoutPolicy(mode, displayId, true);
    if (ret != WMError::WM_OK) {
        WLOGFW("set window layout mode failed displayId: %{public}" PRIu64 ", ret: %{public}d", displayId, ret);
    }
    return ret;
}

std::vector<DisplayId> WindowRoot::GetAllDisplayIds() const
{
    std::vector<DisplayId> displayIds;
    for (auto& it : windowNodeContainerMap_) {
        if (!it.second) {
            return {};
        }
        std::vector<DisplayId>& displayIdVec = const_cast<WindowRoot*>(this)->displayIdMap_[it.first];
        for (auto displayId : displayIdVec) {
            displayIds.push_back(displayId);
        }
    }
    return displayIds;
}

std::string WindowRoot::GenAllWindowsLogInfo() const
{
    std::ostringstream os;
    WindowNodeOperationFunc func = [&os](sptr<WindowNode> node) {
        if (node == nullptr) {
            WLOGE("WindowNode is nullptr");
            return false;
        }
        os<<"window_name:"<<node->GetWindowName()<<",id:"<<node->GetWindowId()<<
           ",focusable:"<<node->GetWindowProperty()->GetFocusable()<<";";
        return false;
    };

    for (auto& elem : windowNodeContainerMap_) {
        if (elem.second == nullptr) {
            continue;
        }
        std::vector<DisplayId>& displayIdVec = const_cast<WindowRoot*>(this)->displayIdMap_[elem.first];
        for (auto& displayId : displayIdVec) {
            os << "Display " << displayId << ":";
        }
        elem.second->TraverseWindowTree(func, true);
    }
    return os.str();
}

void WindowRoot::FocusFaultDetection() const
{
    if (!needCheckFocusWindow) {
        return;
    }
    bool needReport = true;
    uint32_t focusWinId = INVALID_WINDOW_ID;
    for (auto& elem : windowNodeContainerMap_) {
        if (elem.second == nullptr) {
            continue;
        }
        focusWinId = elem.second->GetFocusWindow();
        if (focusWinId != INVALID_WINDOW_ID) {
            needReport = false;
            sptr<WindowNode> windowNode = GetWindowNode(focusWinId);
            if (windowNode == nullptr || !windowNode->currentVisibility_) {
                needReport = true;
                WLOGFE("The focus windowNode is nullptr or is invisible, focusWinId: %{public}u", focusWinId);
                break;
            }
        }
    }
    if (needReport) {
        std::string windowLog(GenAllWindowsLogInfo());
        WLOGFE("The focus window is faulty, focusWinId:%{public}u, %{public}s", focusWinId, windowLog.c_str());
        int32_t ret = OHOS::HiviewDFX::HiSysEvent::Write(
            OHOS::HiviewDFX::HiSysEvent::Domain::WINDOW_MANAGER,
            "NO_FOCUS_WINDOW",
            OHOS::HiviewDFX::HiSysEvent::EventType::FAULT,
            "PID", getpid(),
            "UID", getuid(),
            "PACKAGE_NAME", "foundation",
            "PROCESS_NAME", "foundation",
            "MSG", windowLog);
        if (ret != 0) {
            WLOGFE("Write HiSysEvent error, ret:%{public}d", ret);
        }
    }
}

void WindowRoot::ProcessExpandDisplayCreate(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    std::map<DisplayId, Rect>& displayRectMap)
{
    if (displayInfo == nullptr || !CheckDisplayInfo(displayInfo)) {
        WLOGFE("get display failed or get invalid display info");
        return;
    }
    DisplayId displayId = displayInfo->GetDisplayId();
    ScreenId displayGroupId = displayInfo->GetScreenGroupId();
    auto container = windowNodeContainerMap_[displayGroupId];
    if (container == nullptr) {
        WLOGFE("window node container is nullptr, displayId :%{public}" PRIu64 "", displayId);
    }

    WLOGFI("[Display Create] before add new display, displayId: %{public}" PRIu64"", displayId);
    container->GetMultiDisplayController()->ProcessDisplayCreate(defaultDisplayId, displayInfo, displayRectMap);
    WLOGFI("[Display Create] Container exist, add new display, displayId: %{public}" PRIu64"", displayId);
}

std::map<DisplayId, sptr<DisplayInfo>> WindowRoot::GetAllDisplayInfos(const std::vector<DisplayId>& displayIdVec)
{
    std::map<DisplayId, sptr<DisplayInfo>> displayInfoMap;
    for (auto& displayId : displayIdVec) {
        const sptr<DisplayInfo> displayInfo = DisplayManagerServiceInner::GetInstance().GetDisplayById(displayId);
        displayInfoMap.insert(std::make_pair(displayId, displayInfo));
        WLOGFI("Get latest displayInfo, displayId: %{public}" PRIu64"", displayId);
    }
    return displayInfoMap;
}

std::map<DisplayId, Rect> WindowRoot::GetAllDisplayRectsByDMS(sptr<DisplayInfo> displayInfo)
{
    std::map<DisplayId, Rect> displayRectMap;

    for (auto& displayId : displayIdMap_[displayInfo->GetScreenGroupId()]) {
        auto info = DisplayManagerServiceInner::GetInstance().GetDisplayById(displayId);
        Rect displayRect = { info->GetOffsetX(), info->GetOffsetY(), info->GetWidth(), info->GetHeight() };
        displayRectMap.insert(std::make_pair(displayId, displayRect));

        WLOGFI("displayId: %{public}" PRIu64", displayRect: [ %{public}d, %{public}d, %{public}d, %{public}d]",
            displayId, displayRect.posX_, displayRect.posY_, displayRect.width_, displayRect.height_);
    }
    return displayRectMap;
}

std::map<DisplayId, Rect> WindowRoot::GetAllDisplayRectsByDisplayInfo(
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap)
{
    std::map<DisplayId, Rect> displayRectMap;

    for (auto& iter : displayInfoMap) {
        auto id = iter.first;
        auto info = iter.second;
        Rect displayRect = { info->GetOffsetX(), info->GetOffsetY(), info->GetWidth(), info->GetHeight() };
        displayRectMap.insert(std::make_pair(id, displayRect));

        WLOGFI("displayId: %{public}" PRIu64", displayRect: [ %{public}d, %{public}d, %{public}d, %{public}d]",
            id, displayRect.posX_, displayRect.posY_, displayRect.width_, displayRect.height_);
    }
    return displayRectMap;
}

void WindowRoot::ProcessDisplayCreate(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap)
{
    DisplayId displayId = (displayInfo == nullptr) ? DISPLAY_ID_INVALID : displayInfo->GetDisplayId();
    ScreenId displayGroupId = (displayInfo == nullptr) ? SCREEN_ID_INVALID : displayInfo->GetScreenGroupId();
    auto iter = windowNodeContainerMap_.find(displayGroupId);
    if (iter == windowNodeContainerMap_.end()) {
        CreateWindowNodeContainer(displayInfo);
        WLOGFI("[Display Create] Create new container for display, displayId: %{public}" PRIu64"", displayId);
    } else {
        auto& displayIdVec = displayIdMap_[displayGroupId];
        if (std::find(displayIdVec.begin(), displayIdVec.end(), displayId) != displayIdVec.end()) {
            WLOGFI("[Display Create] Current display is already exist, displayId: %{public}" PRIu64"", displayId);
            return;
        }
        // add displayId in displayId vector
        displayIdMap_[displayGroupId].push_back(displayId);
        auto displayRectMap = GetAllDisplayRectsByDisplayInfo(displayInfoMap);
        ProcessExpandDisplayCreate(defaultDisplayId, displayInfo, displayRectMap);
    }
}

void WindowRoot::MoveNotShowingWindowToDefaultDisplay(DisplayId defaultDisplayId, DisplayId displayId)
{
    for (auto& elem : windowNodeMap_) {
        auto& windowNode = elem.second;
        if (windowNode->GetDisplayId() == displayId && !windowNode->currentVisibility_) {
            std::vector<DisplayId> newShowingDisplays = { defaultDisplayId };
            windowNode->SetShowingDisplays(newShowingDisplays);
            windowNode->isShowingOnMultiDisplays_ = false;
            if (windowNode->GetWindowToken()) {
                windowNode->GetWindowToken()->UpdateDisplayId(windowNode->GetDisplayId(), defaultDisplayId);
            }
            windowNode->SetDisplayId(defaultDisplayId);
        }
    }
}

void WindowRoot::ProcessDisplayDestroy(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap)
{
    DisplayId displayId = (displayInfo == nullptr) ? DISPLAY_ID_INVALID : displayInfo->GetDisplayId();
    ScreenId displayGroupId = (displayInfo == nullptr) ? SCREEN_ID_INVALID : displayInfo->GetScreenGroupId();
    auto& displayIdVec = displayIdMap_[displayGroupId];

    auto iter = windowNodeContainerMap_.find(displayGroupId);
    if (iter == windowNodeContainerMap_.end() ||
        std::find(displayIdVec.begin(), displayIdVec.end(), displayId) == displayIdVec.end() ||
        displayInfoMap.find(displayId) == displayInfoMap.end()) {
        WLOGFE("[Display Destroy] could not find display, destroy failed, displayId: %{public}" PRIu64"", displayId);
        return;
    }

    // erase displayId in displayIdMap
    auto displayIter = std::remove(displayIdVec.begin(), displayIdVec.end(), displayId);
    displayIdVec.erase(displayIter, displayIdVec.end());

    // container process display destroy
    auto container = iter->second;
    if (container == nullptr) {
        WLOGFE("window node container is nullptr, displayId :%{public}" PRIu64 "", displayId);
        return;
    }
    WLOGFI("[Display Destroy] displayId: %{public}" PRIu64"", displayId);

    std::vector<uint32_t> needDestroyWindows;
    auto displayRectMap = GetAllDisplayRectsByDisplayInfo(displayInfoMap);
    // erase displayId in displayRectMap
    auto displayRectIter = displayRectMap.find(displayId);
    displayRectMap.erase(displayRectIter);
    container->GetMultiDisplayController()->ProcessDisplayDestroy(
        defaultDisplayId, displayInfo, displayRectMap, needDestroyWindows);
    for (auto id : needDestroyWindows) {
        auto node = GetWindowNode(id);
        if (node != nullptr) {
            DestroyWindowInner(node);
        }
    }
    // move window which is not showing on destroyed display to default display
    MoveNotShowingWindowToDefaultDisplay(defaultDisplayId, displayId);
    WLOGFI("[Display Destroy] displayId: %{public}" PRIu64" ", displayId);
}

void WindowRoot::ProcessDisplayChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type)
{
    if (displayInfo == nullptr) {
        WLOGFE("get display failed");
        return;
    }
    DisplayId displayId = displayInfo->GetDisplayId();
    ScreenId displayGroupId = displayInfo->GetScreenGroupId();
    auto& displayIdVec = displayIdMap_[displayGroupId];
    auto iter = windowNodeContainerMap_.find(displayGroupId);
    if (iter == windowNodeContainerMap_.end() || std::find(displayIdVec.begin(),
        displayIdVec.end(), displayId) == displayIdVec.end()) {
        WLOGFE("[Display Change] could not find display, change failed, displayId: %{public}" PRIu64"", displayId);
        return;
    }
    // container process display change
    auto container = iter->second;
    if (container == nullptr) {
        WLOGFE("window node container is nullptr, displayId :%{public}" PRIu64 "", displayId);
        return;
    }

    auto displayRectMap = GetAllDisplayRectsByDisplayInfo(displayInfoMap);
    container->GetMultiDisplayController()->ProcessDisplayChange(defaultDisplayId, displayInfo, displayRectMap, type);
}

float WindowRoot::GetVirtualPixelRatio(DisplayId displayId) const
{
    auto container = const_cast<WindowRoot*>(this)->GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return 1.0;  // Use DefaultVPR 1.0
    }
    return container->GetDisplayVirtualPixelRatio(displayId);
}

Rect WindowRoot::GetDisplayGroupRect(DisplayId displayId) const
{
    Rect fullDisplayRect;
    auto container = const_cast<WindowRoot*>(this)->GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return fullDisplayRect;
    }
    return container->GetDisplayGroupRect();
}

WMError WindowRoot::GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo)
{
    for (auto& iter : windowNodeContainerMap_) {
        auto container = iter.second;
        std::vector<sptr<WindowInfo>> windowList;
        container->GetWindowList(windowList);
        for (auto window : windowList) {
            windowInfo->windowList_.emplace_back(window);
        }
    }
    return WMError::WM_OK;
}

void WindowRoot::SetMaxAppWindowNumber(int windowNum)
{
    maxAppWindowNumber_ = windowNum;
}

void WindowRoot::SetSplitRatios(const std::vector<float>& splitRatioNumbers)
{
    auto& splitRatios = splitRatioConfig_.splitRatios;
    splitRatios.clear();
    splitRatios = splitRatioNumbers;
    for (auto iter = splitRatios.begin(); iter != splitRatios.end();) {
        if (*iter > 0 && *iter < 1) { // valid ratio range (0, 1)
            iter++;
        } else {
            iter = splitRatios.erase(iter);
        }
    }
    std::sort(splitRatios.begin(), splitRatios.end());
    auto iter = std::unique(splitRatios.begin(), splitRatios.end());
    splitRatios.erase(iter, splitRatios.end()); // remove duplicate ratios
}

void WindowRoot::SetExitSplitRatios(const std::vector<float>& exitSplitRatios)
{
    if (exitSplitRatios.size() != 2) {
        return;
    }
    if (exitSplitRatios[0] > 0 && exitSplitRatios[0] < DEFAULT_SPLIT_RATIO) {
        splitRatioConfig_.exitSplitStartRatio = exitSplitRatios[0];
    }
    if (exitSplitRatios[1] > DEFAULT_SPLIT_RATIO && exitSplitRatios[1] < 1) {
        splitRatioConfig_.exitSplitEndRatio = exitSplitRatios[1];
    }
}

WMError WindowRoot::GetModeChangeHotZones(DisplayId displayId,
    ModeChangeHotZones& hotZones, const ModeChangeHotZonesConfig& config)
{
    auto container = GetOrCreateWindowNodeContainer(displayId);
    if (container == nullptr) {
        WLOGFE("GetModeChangeHotZones failed, window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    container->GetModeChangeHotZones(displayId, hotZones, config);
    return WMError::WM_OK;
}

void WindowRoot::RemoveSingleUserWindowNodes()
{
    std::vector<DisplayId> displayIds = GetAllDisplayIds();
    for (auto id : displayIds) {
        sptr<WindowNodeContainer> container = GetOrCreateWindowNodeContainer(id);
        if (container == nullptr) {
            WLOGFI("get container failed %{public}" PRIu64"", id);
            continue;
        }
        container->RemoveSingleUserWindowNodes();
    }
}

WMError WindowRoot::UpdateRsTree(uint32_t windowId, bool isAdd)
{
    sptr<WindowNode> node = GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto container = GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (container == nullptr) {
        WLOGFE("window container could not be found");
        return WMError::WM_ERROR_NULLPTR;
    }
    for (auto& displayId : node->GetShowingDisplays()) {
        container->UpdateRSTree(node, displayId, isAdd);
    }
    RSTransaction::FlushImplicitTransaction();
    return WMError::WM_OK;
}
} // namespace Rosen
} // namespace OHOS
