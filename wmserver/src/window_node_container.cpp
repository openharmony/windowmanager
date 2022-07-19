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

#include "window_node_container.h"

#include <ability_manager_client.h>
#include <algorithm>
#include <cinttypes>
#include <ctime>
#include <display_power_mgr_client.h>
#include <hitrace_meter.h>
#include <power_mgr_client.h>

#include "common_event_manager.h"
#include "dm_common.h"
#include "remote_animation.h"
#include "starting_window.h"
#include "window_helper.h"
#include "window_inner_manager.h"
#include "window_layout_policy_cascade.h"
#include "window_layout_policy_tile.h"
#include "window_manager_agent_controller.h"
#include "window_manager_hilog.h"
#include "window_manager_service.h"
#include "wm_common.h"
#include "wm_common_inner.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowNodeContainer"};
    constexpr int WINDOW_NAME_MAX_LENGTH = 10;
    const char DISABLE_WINDOW_ANIMATION_PATH[] = "/etc/disable_window_animation";
    constexpr uint32_t MAX_BRIGHTNESS = 255;
    constexpr uint32_t SPLIT_WINDOWS_CNT = 2;
    constexpr uint32_t EXIT_SPLIT_POINTS_NUMBER = 2;
}

WindowNodeContainer::WindowNodeContainer(const sptr<DisplayInfo>& displayInfo, ScreenId displayGroupId)
{
    DisplayId displayId = displayInfo->GetDisplayId();

    // create and displayGroupInfo and displayGroupController
    displayGroupInfo_ = new DisplayGroupInfo(displayGroupId, displayInfo);
    displayGroupController_ = new DisplayGroupController(this, displayGroupInfo_);
    displayGroupController_->InitNewDisplay(displayId);

    // init layout policy
    layoutPolicies_[WindowLayoutMode::CASCADE] = new WindowLayoutPolicyCascade(displayGroupInfo_,
        displayGroupController_->displayGroupWindowTree_);
    layoutPolicies_[WindowLayoutMode::TILE] = new WindowLayoutPolicyTile(displayGroupInfo_,
        displayGroupController_->displayGroupWindowTree_);
    layoutPolicy_ = layoutPolicies_[WindowLayoutMode::CASCADE];
    layoutPolicy_->Launch();

    Rect initalDividerRect = layoutPolicies_[WindowLayoutMode::CASCADE]->GetDividerRect(displayId);
    displayGroupController_->SetDividerRect(displayId, initalDividerRect);
    // init avoidAreaController
    avoidController_ = new AvoidAreaController(focusedWindow_);
}

WindowNodeContainer::~WindowNodeContainer()
{
    Destroy();
}

uint32_t WindowNodeContainer::GetWindowCountByType(WindowType windowType)
{
    uint32_t windowNumber = 0;
    auto counter = [&windowNumber, &windowType](sptr<WindowNode>& windowNode) {
        if (windowNode->GetWindowType() == windowType && !windowNode->startingWindowShown_) ++windowNumber;
    };
    std::for_each(belowAppWindowNode_->children_.begin(), belowAppWindowNode_->children_.end(), counter);
    std::for_each(appWindowNode_->children_.begin(), appWindowNode_->children_.end(), counter);
    std::for_each(aboveAppWindowNode_->children_.begin(), aboveAppWindowNode_->children_.end(), counter);
    return windowNumber;
}

WMError WindowNodeContainer::AddWindowNodeOnWindowTree(sptr<WindowNode>& node, const sptr<WindowNode>& parentNode)
{
    sptr<WindowNode> root = FindRoot(node->GetWindowType());
    if (root == nullptr) {
        WLOGFE("root is nullptr!");
        return WMError::WM_ERROR_NULLPTR;
    }
    node->requestedVisibility_ = true;
    if (parentNode != nullptr) { // subwindow
        if (parentNode->parent_ != root &&
            !((parentNode->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED)) &&
            (parentNode->parent_ == aboveAppWindowNode_))) {
            WLOGFE("window type and parent window not match or try to add subwindow to subwindow, which is forbidden");
            return WMError::WM_ERROR_INVALID_PARAM;
        }
        node->currentVisibility_ = parentNode->currentVisibility_;
        node->parent_ = parentNode;
    } else { // mainwindow
        node->parent_ = root;
        node->currentVisibility_ = true;
        for (auto& child : node->children_) {
            child->currentVisibility_ = child->requestedVisibility_;
        }
        if (WindowHelper::IsSystemBarWindow(node->GetWindowType())) {
            displayGroupController_->sysBarNodeMaps_[node->GetDisplayId()][node->GetWindowType()] = node;
        }
    }
    return WMError::WM_OK;
}

WMError WindowNodeContainer::ShowStartingWindow(sptr<WindowNode>& node)
{
    if (node->currentVisibility_) {
        WLOGFE("current window is visible, windowId: %{public}u", node->GetWindowId());
        return WMError::WM_ERROR_INVALID_OPERATION;
    }

    WMError res = AddWindowNodeOnWindowTree(node, nullptr);
    if (res != WMError::WM_OK) {
        return res;
    }
    UpdateWindowTree(node);
    displayGroupController_->PreProcessWindowNode(node, WindowUpdateType::WINDOW_UPDATE_ADDED);
    StartingWindow::UpdateRSTree(node);
    AssignZOrder();
    layoutPolicy_->AddWindowNode(node);
    WLOGFI("ShowStartingWindow windowId: %{public}u end", node->GetWindowId());
    return WMError::WM_OK;
}

WMError WindowNodeContainer::IsTileRectSatisfiedWithSizeLimits(sptr<WindowNode>& node)
{
    if (layoutMode_ == WindowLayoutMode::TILE &&
        !layoutPolicy_->IsTileRectSatisfiedWithSizeLimits(node)) {
        WLOGFE("layoutMode is tile, default rect is not satisfied with size limits of window, windowId: %{public}u",
            node->GetWindowId());
        return WMError::WM_ERROR_INVALID_WINDOW_MODE_OR_SIZE;
    }
    return WMError::WM_OK;
}

WMError WindowNodeContainer::AddWindowNode(sptr<WindowNode>& node, sptr<WindowNode>& parentNode)
{
    if (!node->startingWindowShown_) {
        WMError res = AddWindowNodeOnWindowTree(node, parentNode);
        if (res != WMError::WM_OK) {
            return res;
        }
        UpdateWindowTree(node);
        displayGroupController_->PreProcessWindowNode(node, WindowUpdateType::WINDOW_UPDATE_ADDED);
        // add node on RSTree
        for (auto& displayId : node->GetShowingDisplays()) {
            UpdateRSTree(node, displayId, true, node->isPlayAnimationShow_);
        }
    } else {
        node->isPlayAnimationShow_ = false;
        node->startingWindowShown_ = false;
        ReZOrderShowWhenLockedWindowIfNeeded(node);
    }
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(node->GetDisplayId());
    if (windowPair == nullptr) {
        WLOGFE("Window pair is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    windowPair->UpdateIfSplitRelated(node);
    if (node->IsSplitMode()) {
        // raise the z-order of window pair
        RaiseSplitRelatedWindowToTop(node);
    }
    AssignZOrder();
    layoutPolicy_->AddWindowNode(node);
    NotifyIfAvoidAreaChanged(node, AvoidControlType::AVOID_NODE_ADD);
    DumpScreenWindowTree();
    NotifyAccessibilityWindowInfo(node, WindowUpdateType::WINDOW_UPDATE_ADDED);
    UpdateCameraFloatWindowStatus(node, true);
    if (WindowHelper::IsAppWindow(node->GetWindowType())) {
        backupWindowIds_.clear();
    }

    if (node->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
        isScreenLocked_ = true;
    }
    WLOGFI("AddWindowNode windowId: %{public}u end", node->GetWindowId());
    return WMError::WM_OK;
}

void WindowNodeContainer::UpdateRSTreeWhenShowingDisplaysChange(sptr<WindowNode>& node,
                                                                const std::vector<DisplayId>& lastShowingDisplays,
                                                                const std::vector<DisplayId>& curShowingDisplays)
{
    // Update RSTree
    for (auto& displayId : lastShowingDisplays) {
        if (std::find(curShowingDisplays.begin(), curShowingDisplays.end(), displayId) == curShowingDisplays.end()) {
            UpdateRSTree(node, displayId, false);
            WLOGFI("remove from RSTree : %{public}" PRIu64"", displayId);
        }
    }

    for (auto& displayId : curShowingDisplays) {
        if (std::find(lastShowingDisplays.begin(), lastShowingDisplays.end(), displayId) == lastShowingDisplays.end()) {
            UpdateRSTree(node, displayId, true);
            WLOGFI("add on RSTree : %{public}" PRIu64"", displayId);
        }
    }
}

WMError WindowNodeContainer::UpdateWindowNode(sptr<WindowNode>& node, WindowUpdateReason reason)
{
    // Preprocess node
    const auto lastShowingDisplays = node->GetShowingDisplays();
    displayGroupController_->PreProcessWindowNode(node, WindowUpdateType::WINDOW_UPDATE_ACTIVE);
    const auto& curShowingDisplays = node->GetShowingDisplays();

    // Update RSTree
    UpdateRSTreeWhenShowingDisplaysChange(node, lastShowingDisplays, curShowingDisplays);

    if (WindowHelper::IsMainWindow(node->GetWindowType()) && WindowHelper::IsSwitchCascadeReason(reason)) {
        SwitchLayoutPolicy(WindowLayoutMode::CASCADE, node->GetDisplayId());
    }
    layoutPolicy_->UpdateWindowNode(node);
    NotifyIfAvoidAreaChanged(node, AvoidControlType::AVOID_NODE_UPDATE);
    DumpScreenWindowTree();
    WLOGFI("UpdateWindowNode windowId: %{public}u end", node->GetWindowId());
    return WMError::WM_OK;
}

void WindowNodeContainer::RemoveWindowNodeFromWindowTree(sptr<WindowNode>& node)
{
    // remove this node from node vector of display
    sptr<WindowNode> root = FindRoot(node->GetWindowType());

    // remove this node from parent
    auto iter = std::find(node->parent_->children_.begin(), node->parent_->children_.end(), node);
    if (iter != node->parent_->children_.end()) {
        node->parent_->children_.erase(iter);
    } else {
        WLOGFE("can't find this node in parent");
    }
    node->parent_ = nullptr;
}

void WindowNodeContainer::RemoveNodeFromRSTree(sptr<WindowNode>& node)
{
    if (!node->isPlayAnimationHide_) { // update rs tree after animation
        bool isAnimationPlayed = false;
        if (RemoteAnimation::CheckAnimationController() && WindowHelper::IsMainWindow(node->GetWindowType())) {
            isAnimationPlayed = true;
        }
        for (auto& displayId : node->GetShowingDisplays()) {
            UpdateRSTree(node, displayId, false, isAnimationPlayed);
        }
    } else { // not update rs tree before animation
        node->isPlayAnimationHide_ = false;
    }
}

WMError WindowNodeContainer::RemoveWindowNode(sptr<WindowNode>& node)
{
    if (node == nullptr) {
        WLOGFE("window node or surface node is nullptr, invalid");
        return WMError::WM_ERROR_DESTROYED_OBJECT;
    }
    if (node->parent_ == nullptr) {
        WLOGFW("can't find parent of this node");
    } else {
        RemoveWindowNodeFromWindowTree(node);
    }

    node->requestedVisibility_ = false;
    node->currentVisibility_ = false;
    // When RemoteAnimation exists, Remove node from RSTree after animation
    RemoveNodeFromRSTree(node);

    displayGroupController_->UpdateDisplayGroupWindowTree();

    layoutPolicy_->RemoveWindowNode(node);
    WindowMode lastMode = node->GetWindowMode();
    if (HandleRemoveWindow(node) != WMError::WM_OK) {
        return WMError::WM_ERROR_NULLPTR;
    }
    if (!WindowHelper::IsFloatingWindow(lastMode)) {
        NotifyDockWindowStateChanged(node, true);
    }
    NotifyIfAvoidAreaChanged(node, AvoidControlType::AVOID_NODE_REMOVE);
    DumpScreenWindowTree();
    NotifyAccessibilityWindowInfo(node, WindowUpdateType::WINDOW_UPDATE_REMOVED);
    RecoverScreenDefaultOrientationIfNeed(node->GetDisplayId());
    UpdateCameraFloatWindowStatus(node, false);
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
        isScreenLocked_ = false;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_BOOT_ANIMATION) {
        DisplayManagerServiceInner::GetInstance().SetGravitySensorSubscriptionEnabled();
    }
    WLOGFI("RemoveWindowNode windowId: %{public}u end", node->GetWindowId());
    return WMError::WM_OK;
}

WMError WindowNodeContainer::HandleRemoveWindow(sptr<WindowNode>& node)
{
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(node->GetDisplayId());
    if (windowPair == nullptr) {
        WLOGFE("Window pair is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    windowPair->HandleRemoveWindow(node);
    auto dividerWindow = windowPair->GetDividerWindow();
    auto type = node->GetWindowType();
    if ((type == WindowType::WINDOW_TYPE_STATUS_BAR || type == WindowType::WINDOW_TYPE_NAVIGATION_BAR) &&
        dividerWindow != nullptr) {
        UpdateWindowNode(dividerWindow, WindowUpdateReason::UPDATE_RECT);
    }
    return WMError::WM_OK;
}

WMError WindowNodeContainer::DestroyWindowNode(sptr<WindowNode>& node, std::vector<uint32_t>& windowIds)
{
    WMError ret = RemoveWindowNode(node);
    if (ret != WMError::WM_OK) {
        WLOGFE("RemoveWindowNode failed");
        return ret;
    }
    StartingWindow::ReleaseStartWinSurfaceNode(node);
    node->surfaceNode_ = nullptr;
    windowIds.push_back(node->GetWindowId());
    for (auto& child : node->children_) { // destroy sub window if exists
        windowIds.push_back(child->GetWindowId());
        child->parent_ = nullptr;
        if (child->surfaceNode_ != nullptr) {
            WLOGFI("child surfaceNode set nullptr");
            child->surfaceNode_ = nullptr;
        }
    }

    // clear vector cache completely, swap with empty vector
    auto emptyVector = std::vector<sptr<WindowNode>>();
    node->children_.swap(emptyVector);
    WLOGFI("DestroyWindowNode windowId: %{public}u end", node->GetWindowId());
    return WMError::WM_OK;
}

void WindowNodeContainer::UpdateSizeChangeReason(sptr<WindowNode>& node, WindowSizeChangeReason reason)
{
    if (!node->GetWindowToken()) {
        WLOGFE("windowToken is null");
        return;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
        for (auto& childNode : appWindowNode_->children_) {
            if (childNode->IsSplitMode()) {
                childNode->GetWindowToken()->UpdateWindowRect(childNode->GetWindowRect(),
                    childNode->GetDecoStatus(), reason);
                childNode->ResetWindowSizeChangeReason();
                WLOGFI("Notify split window that the drag action is start or end, windowId: %{public}d, "
                    "reason: %{public}u", childNode->GetWindowId(), reason);
            }
        }
    } else {
        node->GetWindowToken()->UpdateWindowRect(node->GetWindowRect(), node->GetDecoStatus(), reason);
        node->ResetWindowSizeChangeReason();
        WLOGFI("Notify window that the drag action is start or end, windowId: %{public}d, "
            "reason: %{public}u", node->GetWindowId(), reason);
    }
}

void WindowNodeContainer::UpdateWindowTree(sptr<WindowNode>& node)
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    node->priority_ = zorderPolicy_->GetWindowPriority(node->GetWindowType());
    RaiseInputMethodWindowPriorityIfNeeded(node);
    RaiseShowWhenLockedWindowIfNeeded(node);
    auto parentNode = node->parent_;
    auto position = parentNode->children_.end();
    int splitWindowCnt = 0;
    for (auto iter = parentNode->children_.begin(); iter < parentNode->children_.end(); ++iter) {
        if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE && splitWindowCnt == SPLIT_WINDOWS_CNT) {
            position = iter;
            break;
        }
        if (WindowHelper::IsSplitWindowMode((*iter)->GetWindowMode())) {
            splitWindowCnt++;
        }
        if ((*iter)->priority_ > node->priority_) {
            position = iter;
            break;
        }
    }
    parentNode->children_.insert(position, node);
}

bool WindowNodeContainer::UpdateRSTree(sptr<WindowNode>& node, DisplayId displayId, bool isAdd, bool animationPlayed)
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    uint32_t animationFlag = node->GetWindowProperty()->GetAnimationFlag();
    if (animationFlag == static_cast<uint32_t>(WindowAnimation::CUSTOM)) {
        WLOGFI("not need to update RsTree since SystemWindow CustomAnimation is playing");
        return true;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_APP_COMPONENT) {
        WLOGFI("WINDOW_TYPE_APP_COMPONENT not need to update RsTree");
        return true;
    }
    static const bool IsWindowAnimationEnabled = ReadIsWindowAnimationEnabledProperty();
    auto updateRSTreeFunc = [&]() {
        auto& dms = DisplayManagerServiceInner::GetInstance();
        WLOGFI("UpdateRSTree windowId: %{public}d, displayId: %{public}" PRIu64", isAdd: %{public}d",
            node->GetWindowId(), displayId, isAdd);
        if (isAdd) {
            auto& surfaceNode = node->leashWinSurfaceNode_ != nullptr ? node->leashWinSurfaceNode_ : node->surfaceNode_;
            dms.UpdateRSTree(displayId, surfaceNode, true);
            for (auto& child : node->children_) {
                if (child->currentVisibility_) {
                    dms.UpdateRSTree(displayId, child->surfaceNode_, true);
                }
            }
        } else {
            auto& surfaceNode = node->leashWinSurfaceNode_ != nullptr ? node->leashWinSurfaceNode_ : node->surfaceNode_;
            dms.UpdateRSTree(displayId, surfaceNode, false);
            for (auto& child : node->children_) {
                dms.UpdateRSTree(displayId, child->surfaceNode_, false);
            }
        }
    };

    if (node->EnableDefaultAnimation(IsWindowAnimationEnabled, animationPlayed)) {
        WLOGFI("add or remove window with animation");
        // default transition duration: 350ms
        static const RSAnimationTimingProtocol timingProtocol(350);
        // default transition curve: EASE OUT
        static const Rosen::RSAnimationTimingCurve curve = Rosen::RSAnimationTimingCurve::EASE_OUT;
        // add window with transition animation
        StartTraceArgs(HITRACE_TAG_WINDOW_MANAGER, "Animate(%u)", node->GetWindowId());
        RSNode::Animate(timingProtocol, curve, updateRSTreeFunc);
        FinishTrace(HITRACE_TAG_WINDOW_MANAGER);
    } else {
        // add or remove window without animation
        WLOGFI("add or remove window without animation");
        updateRSTreeFunc();
    }
    return true;
}

void WindowNodeContainer::RecoverScreenDefaultOrientationIfNeed(DisplayId displayId)
{
    if (displayGroupController_->displayGroupWindowTree_[displayId][WindowRootNodeType::APP_WINDOW_NODE]->empty()) {
        WLOGFI("appWindowNode_ child is empty in display  %{public}" PRIu64"", displayId);
        auto aboveWindows =
            *displayGroupController_->displayGroupWindowTree_[displayId][WindowRootNodeType::ABOVE_WINDOW_NODE];
        for (auto iter = aboveWindows.begin(); iter != aboveWindows.end(); iter++) {
            auto windowMode = (*iter)->GetWindowMode();
            if (WindowHelper::IsFullScreenWindow(windowMode) || WindowHelper::IsSplitWindowMode(windowMode)) {
                return;
            }
        }
        auto belowWindows =
            *displayGroupController_->displayGroupWindowTree_[displayId][WindowRootNodeType::BELOW_WINDOW_NODE];
        Orientation targetOrientation = Orientation::UNSPECIFIED;
        for (auto iter = belowWindows.begin(); iter != belowWindows.end(); iter++) {
            if ((*iter)->GetWindowType() == WindowType::WINDOW_TYPE_DESKTOP) {
                targetOrientation = (*iter)->GetRequestedOrientation();
                break;
            }
        }
        DisplayManagerServiceInner::GetInstance().SetOrientationFromWindow(displayId, targetOrientation);
    }
}

const std::vector<uint32_t>& WindowNodeContainer::Destroy()
{
    // clear vector cache completely, swap with empty vector
    auto emptyVector = std::vector<uint32_t>();
    removedIds_.swap(emptyVector);
    for (auto& node : belowAppWindowNode_->children_) {
        DestroyWindowNode(node, removedIds_);
    }
    for (auto& node : appWindowNode_->children_) {
        DestroyWindowNode(node, removedIds_);
    }
    for (auto& node : aboveAppWindowNode_->children_) {
        DestroyWindowNode(node, removedIds_);
    }
    return removedIds_;
}

sptr<WindowNode> WindowNodeContainer::FindRoot(WindowType type) const
{
    if (WindowHelper::IsAppWindow(type) || type == WindowType::WINDOW_TYPE_DOCK_SLICE ||
        type == WindowType::WINDOW_TYPE_APP_COMPONENT || type == WindowType::WINDOW_TYPE_PLACEHOLDER) {
        return appWindowNode_;
    }
    if (WindowHelper::IsBelowSystemWindow(type)) {
        return belowAppWindowNode_;
    }
    if (WindowHelper::IsAboveSystemWindow(type)) {
        return aboveAppWindowNode_;
    }
    return nullptr;
}

sptr<WindowNode> WindowNodeContainer::FindWindowNodeById(uint32_t id) const
{
    std::vector<sptr<WindowNode>> rootNodes = { aboveAppWindowNode_, appWindowNode_, belowAppWindowNode_ };
    for (auto& rootNode : rootNodes) {
        for (auto& node : rootNode->children_) {
            if (node->GetWindowId() == id) {
                return node;
            }
            for (auto& subNode : node->children_) {
                if (subNode->GetWindowId() == id) {
                    return subNode;
                }
            }
        }
    }
    return nullptr;
}

void WindowNodeContainer::UpdateFocusStatus(uint32_t id, bool focused) const
{
    auto node = FindWindowNodeById(id);
    if (node == nullptr) {
        WLOGFW("cannot find focused window id:%{public}d", id);
    } else {
        if (node->GetWindowToken()) {
            node->GetWindowToken()->UpdateFocusStatus(focused);
        }
        if (node->abilityToken_ == nullptr) {
            WLOGFI("abilityToken is null, window : %{public}d", id);
        }
        if (focused) {
            WLOGFW("current focus window: windowId: %{public}d, windowName: %{public}s",
                id, node->GetWindowProperty()->GetWindowName().c_str());
        }
        sptr<FocusChangeInfo> focusChangeInfo = new FocusChangeInfo(node->GetWindowId(), node->GetDisplayId(),
            node->GetCallingPid(), node->GetCallingUid(), node->GetWindowType(), node->abilityToken_);
        WindowManagerAgentController::GetInstance().UpdateFocusChangeInfo(
            focusChangeInfo, focused);
    }
}

void WindowNodeContainer::UpdateActiveStatus(uint32_t id, bool isActive) const
{
    auto node = FindWindowNodeById(id);
    if (node == nullptr) {
        WLOGFE("cannot find active window id: %{public}d", id);
        return;
    }
    if (node->GetWindowToken()) {
        node->GetWindowToken()->UpdateActiveStatus(isActive);
    }
}

void WindowNodeContainer::UpdateBrightness(uint32_t id, bool byRemoved)
{
    auto node = FindWindowNodeById(id);
    if (node == nullptr) {
        WLOGFE("cannot find active window id: %{public}d", id);
        return;
    }

    if (!byRemoved) {
        if (!WindowHelper::IsAppWindow(node->GetWindowType())) {
            return;
        }
    }
    WLOGFI("brightness: [%{public}f, %{public}f]", GetDisplayBrightness(), node->GetBrightness());
    if (node->GetBrightness() == UNDEFINED_BRIGHTNESS) {
        if (GetDisplayBrightness() != node->GetBrightness()) {
            WLOGFI("adjust brightness with default value");
            DisplayPowerMgr::DisplayPowerMgrClient::GetInstance().RestoreBrightness();
            SetDisplayBrightness(UNDEFINED_BRIGHTNESS); // UNDEFINED_BRIGHTNESS means system default brightness
        }
        SetBrightnessWindow(INVALID_WINDOW_ID);
    } else {
        if (GetDisplayBrightness() != node->GetBrightness()) {
            WLOGFI("adjust brightness with value: %{public}u", ToOverrideBrightness(node->GetBrightness()));
            DisplayPowerMgr::DisplayPowerMgrClient::GetInstance().OverrideBrightness(
                ToOverrideBrightness(node->GetBrightness()));
            SetDisplayBrightness(node->GetBrightness());
        }
        SetBrightnessWindow(node->GetWindowId());
    }
}

void WindowNodeContainer::AssignZOrder()
{
    zOrder_ = 0;
    WindowNodeOperationFunc func = [this](sptr<WindowNode> node) {
        if (node->leashWinSurfaceNode_ != nullptr) {
            node->leashWinSurfaceNode_->SetPositionZ(zOrder_);
        }

        if (node->surfaceNode_ != nullptr) {
            node->surfaceNode_->SetPositionZ(zOrder_);
        }

        if (node->startingWinSurfaceNode_ != nullptr) {
            node->startingWinSurfaceNode_->SetPositionZ(zOrder_);
        }
        ++zOrder_;
        return false;
    };
    TraverseWindowTree(func, false);
    displayGroupController_->UpdateDisplayGroupWindowTree();
}

WMError WindowNodeContainer::SetFocusWindow(uint32_t windowId)
{
    if (focusedWindow_ == windowId) {
        WLOGFI("focused window do not change, id: %{public}u", windowId);
        return WMError::WM_DO_NOTHING;
    }
    UpdateFocusStatus(focusedWindow_, false);
    focusedWindow_ = windowId;
    sptr<WindowNode> node = FindWindowNodeById(windowId);
    NotifyAccessibilityWindowInfo(node, WindowUpdateType::WINDOW_UPDATE_FOCUSED);
    UpdateFocusStatus(focusedWindow_, true);
    return WMError::WM_OK;
}

uint32_t WindowNodeContainer::GetFocusWindow() const
{
    return focusedWindow_;
}

WMError WindowNodeContainer::SetActiveWindow(uint32_t windowId, bool byRemoved)
{
    if (activeWindow_ == windowId) {
        WLOGFI("active window do not change, id: %{public}u", windowId);
        return WMError::WM_DO_NOTHING;
    }
    UpdateActiveStatus(activeWindow_, false);
    activeWindow_ = windowId;
    UpdateActiveStatus(activeWindow_, true);
    UpdateBrightness(activeWindow_, byRemoved);
    return WMError::WM_OK;
}

void WindowNodeContainer::SetDisplayBrightness(float brightness)
{
    displayBrightness_ = brightness;
}

float WindowNodeContainer::GetDisplayBrightness() const
{
    return displayBrightness_;
}

void WindowNodeContainer::SetBrightnessWindow(uint32_t windowId)
{
    brightnessWindow_ = windowId;
}

uint32_t WindowNodeContainer::GetBrightnessWindow() const
{
    return brightnessWindow_;
}

uint32_t WindowNodeContainer::ToOverrideBrightness(float brightness)
{
    return static_cast<uint32_t>(brightness * MAX_BRIGHTNESS);
}

uint32_t WindowNodeContainer::GetActiveWindow() const
{
    return activeWindow_;
}

sptr<WindowLayoutPolicy> WindowNodeContainer::GetLayoutPolicy() const
{
    return layoutPolicy_;
}

sptr<AvoidAreaController> WindowNodeContainer::GetAvoidController() const
{
    return avoidController_;
}

sptr<DisplayGroupController> WindowNodeContainer::GetMultiDisplayController() const
{
    return displayGroupController_;
}

sptr<WindowNode> WindowNodeContainer::GetRootNode(WindowRootNodeType type) const
{
    if (type == WindowRootNodeType::ABOVE_WINDOW_NODE) {
        return aboveAppWindowNode_;
    } else if (type == WindowRootNodeType::APP_WINDOW_NODE) {
        return appWindowNode_;
    } else if (type == WindowRootNodeType::BELOW_WINDOW_NODE) {
        return belowAppWindowNode_;
    }
    return nullptr;
}

void WindowNodeContainer::HandleKeepScreenOn(const sptr<WindowNode>& node, bool requireLock)
{
    if (requireLock && node->keepScreenLock_ == nullptr) {
        // reset ipc identity
        std::string identity = IPCSkeleton::ResetCallingIdentity();
        node->keepScreenLock_ = PowerMgr::PowerMgrClient::GetInstance().CreateRunningLock(node->GetWindowName(),
            PowerMgr::RunningLockType::RUNNINGLOCK_SCREEN);
        // set ipc identity to raw
        IPCSkeleton::SetCallingIdentity(identity);
    }
    if (node->keepScreenLock_ == nullptr) {
        return;
    }
    WLOGFI("handle keep screen on: [%{public}s, %{public}d]", node->GetWindowName().c_str(), requireLock);
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "container:HandleKeepScreenOn(%s, %d)",
        node->GetWindowName().c_str(), requireLock);
    ErrCode res;
    // reset ipc identity
    std::string identity = IPCSkeleton::ResetCallingIdentity();
    if (requireLock) {
        res = node->keepScreenLock_->Lock();
    } else {
        res = node->keepScreenLock_->UnLock();
    }
    // set ipc identity to raw
    IPCSkeleton::SetCallingIdentity(identity);
    if (res != ERR_OK) {
        WLOGFE("handle keep screen running lock failed: [operation: %{public}d, err: %{public}d]", requireLock, res);
    }
}

bool WindowNodeContainer::IsAboveSystemBarNode(sptr<WindowNode> node) const
{
    int32_t curPriority = zorderPolicy_->GetWindowPriority(node->GetWindowType());
    if ((curPriority > zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_STATUS_BAR)) &&
        (curPriority > zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_NAVIGATION_BAR))) {
        return true;
    }
    return false;
}

bool WindowNodeContainer::IsSplitImmersiveNode(sptr<WindowNode> node) const
{
    auto type = node->GetWindowType();
    return node->IsSplitMode() || type == WindowType::WINDOW_TYPE_DOCK_SLICE;
}

std::unordered_map<WindowType, SystemBarProperty> WindowNodeContainer::GetExpectImmersiveProperty() const
{
    std::unordered_map<WindowType, SystemBarProperty> sysBarPropMap {
        { WindowType::WINDOW_TYPE_STATUS_BAR,     SystemBarProperty() },
        { WindowType::WINDOW_TYPE_NAVIGATION_BAR, SystemBarProperty() },
    };

    std::vector<sptr<WindowNode>> rootNodes = { aboveAppWindowNode_, appWindowNode_, belowAppWindowNode_ };
    for (auto& node : rootNodes) {
        for (auto iter = node->children_.rbegin(); iter < node->children_.rend(); ++iter) {
            auto& sysBarPropMapNode = (*iter)->GetSystemBarProperty();
            if (IsAboveSystemBarNode(*iter)) {
                continue;
            }
            if (WindowHelper::IsFullScreenWindow((*iter)->GetWindowMode())
	        && (*iter)->GetWindowType() != WindowType::WINDOW_TYPE_PANEL) {
                WLOGFI("Top immersive window id: %{public}d. Use full immersive prop", (*iter)->GetWindowId());
                for (auto it : sysBarPropMap) {
                    sysBarPropMap[it.first] = (sysBarPropMapNode.find(it.first))->second;
                }
                return sysBarPropMap;
            } else if (IsSplitImmersiveNode(*iter)) {
                WLOGFI("Top split window id: %{public}d. Use split immersive prop", (*iter)->GetWindowId());
                for (auto it : sysBarPropMap) {
                    sysBarPropMap[it.first] = (sysBarPropMapNode.find(it.first))->second;
                    sysBarPropMap[it.first].enable_ = false;
                }
                return sysBarPropMap;
            }
        }
    }

    WLOGFI("No immersive window on top. Use default systembar Property");
    return sysBarPropMap;
}

void WindowNodeContainer::NotifyIfAvoidAreaChanged(const sptr<WindowNode>& node,
    const AvoidControlType avoidType) const
{
    auto checkFunc = [this](sptr<WindowNode> node) {
        return CheckWindowNodeWhetherInWindowTree(node);
    };
    avoidController_->ProcessWindowChange(node, avoidType, checkFunc);
    if (WindowHelper::IsSystemBarWindow(node->GetWindowType())) {
        NotifyIfSystemBarRegionChanged(node->GetDisplayId());
    } else {
        NotifyIfSystemBarTintChanged(node->GetDisplayId());
    }

    NotifyIfKeyboardRegionChanged(node, avoidType);
}

void WindowNodeContainer::BeforeProcessWindowAvoidAreaChangeWhenDisplayChange() const
{
    avoidController_->SetFlagForProcessWindowChange(true);
}

void WindowNodeContainer::ProcessWindowAvoidAreaChangeWhenDisplayChange() const
{
    avoidController_->SetFlagForProcessWindowChange(false);
    auto checkFunc = [this](sptr<WindowNode> node) {
        return CheckWindowNodeWhetherInWindowTree(node);
    };
    WindowNodeOperationFunc func = [avoidController = avoidController_, &checkFunc](sptr<WindowNode> node) {
        avoidController->ProcessWindowChange(node, AvoidControlType::AVOID_NODE_UPDATE, checkFunc);
        return false;
    };
    TraverseWindowTree(func, true);
}

void WindowNodeContainer::NotifyIfSystemBarTintChanged(DisplayId displayId) const
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    auto expectSystemBarProp = GetExpectImmersiveProperty();
    SystemBarRegionTints tints;
    SysBarTintMap& sysBarTintMap = displayGroupController_->sysBarTintMaps_[displayId];
    for (auto it : sysBarTintMap) {
        auto expectProp = expectSystemBarProp.find(it.first)->second;
        if (it.second.prop_ == expectProp) {
            continue;
        }
        WLOGFI("System bar prop update, Type: %{public}d, Visible: %{public}d, Color: %{public}x | %{public}x",
            static_cast<int32_t>(it.first), expectProp.enable_, expectProp.backgroundColor_, expectProp.contentColor_);
        sysBarTintMap[it.first].prop_ = expectProp;
        sysBarTintMap[it.first].type_ = it.first;
        tints.emplace_back(sysBarTintMap[it.first]);
    }
    WindowManagerAgentController::GetInstance().UpdateSystemBarRegionTints(displayId, tints);
}

void WindowNodeContainer::NotifyIfSystemBarRegionChanged(DisplayId displayId) const
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    SystemBarRegionTints tints;
    SysBarTintMap& sysBarTintMap = displayGroupController_->sysBarTintMaps_[displayId];
    SysBarNodeMap& sysBarNodeMap = displayGroupController_->sysBarNodeMaps_[displayId];
    for (auto it : sysBarTintMap) { // split screen mode not support yet
        auto sysNode = sysBarNodeMap[it.first];
        if (sysNode == nullptr || it.second.region_ == sysNode->GetWindowRect()) {
            continue;
        }
        const Rect& newRegion = sysNode->GetWindowRect();
        sysBarTintMap[it.first].region_ = newRegion;
        sysBarTintMap[it.first].type_ = it.first;
        tints.emplace_back(sysBarTintMap[it.first]);
        WLOGFI("system bar region update, type: %{public}d" \
            "region: [%{public}d, %{public}d, %{public}d, %{public}d]",
            static_cast<int32_t>(it.first), newRegion.posX_, newRegion.posY_, newRegion.width_, newRegion.height_);
    }
    WindowManagerAgentController::GetInstance().UpdateSystemBarRegionTints(displayId, tints);
}

void WindowNodeContainer::NotifyIfKeyboardRegionChanged(const sptr<WindowNode>& node,
    const AvoidControlType avoidType) const
{
    if (node->GetWindowType() != WindowType::WINDOW_TYPE_INPUT_METHOD_FLOAT) {
        WLOGFD("windowType: %{public}u", node->GetWindowType());
        return;
    }

    auto callingWindow = FindWindowNodeById(node->GetCallingWindow());
    if (callingWindow == nullptr) {
        WLOGFI("callingWindow: %{public}u does not be set", node->GetCallingWindow());
        callingWindow = FindWindowNodeById(GetFocusWindow());
    }
    if (callingWindow == nullptr || callingWindow->GetWindowToken() == nullptr) {
        WLOGFE("does not have correct callingWindow for input method window");
        return;
    }
    const WindowMode callingWindowMode = callingWindow->GetWindowMode();
    if (callingWindowMode == WindowMode::WINDOW_MODE_FULLSCREEN ||
        callingWindowMode == WindowMode::WINDOW_MODE_SPLIT_PRIMARY ||
        callingWindowMode == WindowMode::WINDOW_MODE_SPLIT_SECONDARY) {
        const Rect keyRect = node->GetWindowRect();
        const Rect callingRect = callingWindow->GetWindowRect();
        if (WindowHelper::IsEmptyRect(WindowHelper::GetOverlap(callingRect, keyRect, 0, 0))) {
            WLOGFD("no overlap between two windows");
            return;
        }
        Rect overlapRect = { 0, 0, 0, 0 };
        if (avoidType == AvoidControlType::AVOID_NODE_ADD || avoidType == AvoidControlType::AVOID_NODE_UPDATE) {
            overlapRect = WindowHelper::GetOverlap(keyRect, callingRect, callingRect.posX_, callingRect.posY_);
        }

        WLOGFI("keyboard size change callingWindow: [%{public}s, %{public}u], " \
        "overlap rect: [%{public}d, %{public}d, %{public}u, %{public}u]",
            callingWindow->GetWindowName().c_str(), callingWindow->GetWindowId(),
            overlapRect.posX_, overlapRect.posY_, overlapRect.width_, overlapRect.height_);
        sptr<OccupiedAreaChangeInfo> info = new OccupiedAreaChangeInfo(OccupiedAreaType::TYPE_INPUT, overlapRect);
        callingWindow->GetWindowToken()->UpdateOccupiedAreaChangeInfo(info);
        return;
    }
    WLOGFE("does not have correct callingWindowMode for input method window");
}

void WindowNodeContainer::NotifySystemBarTints(std::vector<DisplayId> displayIdVec)
{
    if (displayIdVec.size() != displayGroupController_->sysBarTintMaps_.size()) {
        WLOGE("[Immersive] the number of display is error");
    }

    for (auto displayId : displayIdVec) {
        SystemBarRegionTints tints;
        SysBarTintMap& sysBarTintMap = displayGroupController_->sysBarTintMaps_[displayId];
        for (auto it : sysBarTintMap) {
            WLOGFI("[Immersive] system bar cur notify, T: %{public}d, " \
                "V: %{public}d, C: %{public}x | %{public}x, " \
                "R: [%{public}d, %{public}d, %{public}d, %{public}d]",
                static_cast<int32_t>(it.first),
                sysBarTintMap[it.first].prop_.enable_,
                sysBarTintMap[it.first].prop_.backgroundColor_, sysBarTintMap[it.first].prop_.contentColor_,
                sysBarTintMap[it.first].region_.posX_, sysBarTintMap[it.first].region_.posY_,
                sysBarTintMap[it.first].region_.width_, sysBarTintMap[it.first].region_.height_);
            tints.push_back(sysBarTintMap[it.first]);
        }
        WindowManagerAgentController::GetInstance().UpdateSystemBarRegionTints(displayId, tints);
    }
}

void WindowNodeContainer::NotifyDockWindowStateChanged(sptr<WindowNode>& node, bool isEnable)
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    WLOGFI("[Immersive] begin isEnable: %{public}d", isEnable);
    if (isEnable) {
        for (auto& windowNode : appWindowNode_->children_) {
            if (windowNode->GetWindowId() == node->GetWindowId()) {
                continue;
            }
            if (!WindowHelper::IsFloatingWindow(windowNode->GetWindowMode())) {
                return;
            }
        }
    }
    SystemBarProperty prop;
    prop.enable_ = isEnable;
    SystemBarRegionTint tint;
    tint.type_ = WindowType::WINDOW_TYPE_LAUNCHER_DOCK;
    tint.prop_ = prop;
    SystemBarRegionTints tints;
    tints.push_back(tint);
    WindowManagerAgentController::GetInstance().UpdateSystemBarRegionTints(node->GetDisplayId(), tints);
}

void WindowNodeContainer::UpdateAvoidAreaListener(sptr<WindowNode>& windowNode, bool haveAvoidAreaListener)
{
    avoidController_->UpdateAvoidAreaListener(windowNode, haveAvoidAreaListener);
}

bool WindowNodeContainer::IsTopWindow(uint32_t windowId, sptr<WindowNode>& rootNode) const
{
    if (rootNode->children_.empty()) {
        WLOGFE("root does not have any node");
        return false;
    }
    auto node = *(rootNode->children_.rbegin());
    if (node == nullptr) {
        WLOGFE("window tree does not have any node");
        return false;
    }

    for (auto iter = node->children_.rbegin(); iter < node->children_.rend(); iter++) {
        if ((*iter)->priority_ > 0) {
            return (*iter)->GetWindowId() == windowId;
        } else {
            break;
        }
    }
    return node->GetWindowId() == windowId;
}

void WindowNodeContainer::RaiseOrderedWindowToTop(std::vector<sptr<WindowNode>>& orderedNodes,
    std::vector<sptr<WindowNode>>& windowNodes)
{
    for (auto iter = appWindowNode_->children_.begin(); iter != appWindowNode_->children_.end();) {
        uint32_t wid = (*iter)->GetWindowId();
        auto orderedIter = std::find_if(orderedNodes.begin(), orderedNodes.end(),
            [wid] (sptr<WindowNode> orderedNode) { return orderedNode->GetWindowId() == wid; });
        if (orderedIter != orderedNodes.end()) {
            iter = windowNodes.erase(iter);
        } else {
            iter++;
        }
    }
    for (auto iter = orderedNodes.begin(); iter != orderedNodes.end(); iter++) {
        UpdateWindowTree(*iter);
    }
    return;
}

void WindowNodeContainer::RaiseWindowToTop(uint32_t windowId, std::vector<sptr<WindowNode>>& windowNodes)
{
    auto iter = std::find_if(windowNodes.begin(), windowNodes.end(),
                             [windowId](sptr<WindowNode> node) {
                                 return node->GetWindowId() == windowId;
                             });
    // raise app node window to top
    if (iter != windowNodes.end()) {
        sptr<WindowNode> node = *iter;
        windowNodes.erase(iter);
        UpdateWindowTree(node);
        WLOGFI("raise window to top %{public}u", node->GetWindowId());
    }
}

void WindowNodeContainer::FillWindowInfo(sptr<WindowInfo>& windowInfo, const sptr<WindowNode>& node) const
{
    if (windowInfo == nullptr) {
        WLOGFE("windowInfo is null");
        return;
    }
    windowInfo->wid_ = static_cast<int32_t>(node->GetWindowId());
    windowInfo->windowRect_ = node->GetWindowRect();
    windowInfo->focused_ = node->GetWindowId() == focusedWindow_;
    windowInfo->displayId_ = node->GetDisplayId();
    windowInfo->mode_ = node->GetWindowMode();
    windowInfo->type_ = node->GetWindowType();
    auto property = node->GetWindowProperty();
    if (property != nullptr) {
        windowInfo->isDecorEnable_ = property->GetDecorEnable();
    }
}

void WindowNodeContainer::NotifyAccessibilityWindowInfo(const sptr<WindowNode>& node, WindowUpdateType type) const
{
    if (node == nullptr) {
        WLOGFE("window node is null");
        return;
    }
    bool isNeedNotify = false;
    switch (type) {
        case WindowUpdateType::WINDOW_UPDATE_ADDED:
            if (node->currentVisibility_) {
                isNeedNotify = true;
            }
            break;
        case WindowUpdateType::WINDOW_UPDATE_FOCUSED:
            if (node->GetWindowId() == focusedWindow_) {
                isNeedNotify = true;
            }
            break;
        case WindowUpdateType::WINDOW_UPDATE_REMOVED:
            isNeedNotify = true;
            break;
        case WindowUpdateType::WINDOW_UPDATE_PROPERTY:
            isNeedNotify = true;
            break;
        default:
            break;
    }
    if (isNeedNotify) {
        std::vector<sptr<WindowInfo>> windowList;
        GetWindowList(windowList);
        sptr<WindowInfo> windowInfo = new (std::nothrow) WindowInfo();
        sptr<AccessibilityWindowInfo> accessibilityWindowInfo = new (std::nothrow) AccessibilityWindowInfo();
        if (windowInfo != nullptr && accessibilityWindowInfo != nullptr) {
            FillWindowInfo(windowInfo, node);
            accessibilityWindowInfo->currentWindowInfo_ = windowInfo;
            accessibilityWindowInfo->windowList_ = windowList;
            WindowManagerAgentController::GetInstance().NotifyAccessibilityWindowInfo(accessibilityWindowInfo, type);
        }
    }
}

void WindowNodeContainer::GetWindowList(std::vector<sptr<WindowInfo>>& windowList) const
{
    std::vector<sptr<WindowNode>> windowNodes;
    TraverseContainer(windowNodes);
    for (auto node : windowNodes) {
        sptr<WindowInfo> windowInfo = new (std::nothrow) WindowInfo();
        if (windowInfo != nullptr) {
            FillWindowInfo(windowInfo, node);
            windowList.emplace_back(windowInfo);
        }
    }
}

void WindowNodeContainer::TraverseContainer(std::vector<sptr<WindowNode>>& windowNodes) const
{
    for (auto& node : belowAppWindowNode_->children_) {
        TraverseWindowNode(node, windowNodes);
    }
    for (auto& node : appWindowNode_->children_) {
        TraverseWindowNode(node, windowNodes);
    }
    for (auto& node : aboveAppWindowNode_->children_) {
        TraverseWindowNode(node, windowNodes);
    }
    std::reverse(windowNodes.begin(), windowNodes.end());
}

void WindowNodeContainer::TraverseWindowNode(sptr<WindowNode>& node, std::vector<sptr<WindowNode>>& windowNodes) const
{
    if (node == nullptr) {
        return;
    }
    auto iter = node->children_.begin();
    for (; iter < node->children_.end(); ++iter) {
        if ((*iter)->priority_ < 0) {
            windowNodes.emplace_back(*iter);
        } else {
            break;
        }
    }
    windowNodes.emplace_back(node);
    for (; iter < node->children_.end(); ++iter) {
        windowNodes.emplace_back(*iter);
    }
}

AvoidArea WindowNodeContainer::GetAvoidAreaByType(const sptr<WindowNode>& node, AvoidAreaType avoidAreaType) const
{
    if (CheckWindowNodeWhetherInWindowTree(node)) {
        return avoidController_->GetAvoidAreaByType(node, avoidAreaType);
    }
    return {};
}

bool WindowNodeContainer::CheckWindowNodeWhetherInWindowTree(const sptr<WindowNode>& node) const
{
    bool isInWindowTree = false;
    WindowNodeOperationFunc func = [&node, &isInWindowTree](sptr<WindowNode> windowNode) {
        if (node->GetWindowId() == windowNode->GetWindowId()) {
            isInWindowTree = true;
            return true;
        }
        return false;
    };
    TraverseWindowTree(func, true);
    return isInWindowTree;
}

void WindowNodeContainer::DumpScreenWindowTree()
{
    WLOGFI("-------- dump window info begin---------");
    WLOGFI("WindowName DisplayId WinId Type Mode Flag ZOrd Orientation abilityToken [   x    y    w    h]");
    uint32_t zOrder = zOrder_;
    WindowNodeOperationFunc func = [&zOrder](sptr<WindowNode> node) {
        Rect rect = node->GetWindowRect();
        const std::string& windowName = node->GetWindowName().size() < WINDOW_NAME_MAX_LENGTH ?
            node->GetWindowName() : node->GetWindowName().substr(0, WINDOW_NAME_MAX_LENGTH);
        WLOGI("DumpScreenWindowTree: %{public}10s %{public}9" PRIu64" %{public}5u %{public}4u %{public}4u %{public}4u "
            "%{public}4u %{public}11u %{public}d [%{public}4d %{public}4d %{public}4u %{public}4u]",
            windowName.c_str(), node->GetDisplayId(), node->GetWindowId(), node->GetWindowType(), node->GetWindowMode(),
            node->GetWindowFlags(), --zOrder, static_cast<uint32_t>(node->GetRequestedOrientation()),
            node->abilityToken_ != nullptr, rect.posX_, rect.posY_, rect.width_, rect.height_);
        return false;
    };
    TraverseWindowTree(func, true);
    WLOGFI("-------- dump window info end  ---------");
}

uint64_t WindowNodeContainer::GetScreenId(DisplayId displayId) const
{
    return DisplayManagerServiceInner::GetInstance().GetRSScreenId(displayId);
}

Rect WindowNodeContainer::GetDisplayRect(DisplayId displayId) const
{
    return displayGroupInfo_->GetDisplayRect(displayId);
}

bool WindowNodeContainer::isVerticalDisplay(DisplayId displayId) const
{
    return displayGroupInfo_->GetDisplayRect(displayId).width_ < displayGroupInfo_->GetDisplayRect(displayId).height_;
}

void WindowNodeContainer::ProcessWindowStateChange(WindowState state, WindowStateChangeReason reason)
{
    switch (reason) {
        case WindowStateChangeReason::KEYGUARD: {
            int32_t topPriority = zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_KEYGUARD);
            TraverseAndUpdateWindowState(state, topPriority);
            break;
        }
        default:
            return;
    }
}

void WindowNodeContainer::TraverseAndUpdateWindowState(WindowState state, int32_t topPriority)
{
    std::vector<sptr<WindowNode>> rootNodes = { belowAppWindowNode_, appWindowNode_, aboveAppWindowNode_ };
    for (auto& node : rootNodes) {
        UpdateWindowState(node, topPriority, state);
    }
}

void WindowNodeContainer::UpdateWindowState(sptr<WindowNode> node, int32_t topPriority, WindowState state)
{
    if (node == nullptr) {
        return;
    }
    if (node->parent_ != nullptr && node->currentVisibility_) {
        if (node->priority_ < topPriority &&
            !(node->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED))) {
            if (node->GetWindowToken()) {
                node->GetWindowToken()->UpdateWindowState(state);
            }
            HandleKeepScreenOn(node, state);
        }
    }
    for (auto& childNode : node->children_) {
        UpdateWindowState(childNode, topPriority, state);
    }
}

void WindowNodeContainer::HandleKeepScreenOn(const sptr<WindowNode>& node, WindowState state)
{
    if (node == nullptr) {
        WLOGFE("window is invalid");
        return;
    }
    if (state == WindowState::STATE_FROZEN) {
        HandleKeepScreenOn(node, false);
    } else if (state == WindowState::STATE_UNFROZEN) {
        HandleKeepScreenOn(node, node->IsKeepScreenOn());
    } else {
        // do nothing
    }
}

sptr<WindowNode> WindowNodeContainer::FindDividerNode() const
{
    for (auto iter = appWindowNode_->children_.begin(); iter != appWindowNode_->children_.end(); iter++) {
        if ((*iter)->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
            return *iter;
        }
    }
    return nullptr;
}

void WindowNodeContainer::RaiseSplitRelatedWindowToTop(sptr<WindowNode>& node)
{
    if (node == nullptr) {
        return;
    }
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(node->GetDisplayId());
    if (windowPair == nullptr) {
        WLOGFE("Window pair is nullptr");
        return;
    }
    std::vector<sptr<WindowNode>> orderedPair = windowPair->GetOrderedPair(node);
    RaiseOrderedWindowToTop(orderedPair, appWindowNode_->children_);
    AssignZOrder();
    return;
}

WMError WindowNodeContainer::RaiseZOrderForAppWindow(sptr<WindowNode>& node, sptr<WindowNode>& parentNode)
{
    if (node == nullptr) {
        return WMError::WM_ERROR_NULLPTR;
    }

    if (IsTopWindow(node->GetWindowId(), appWindowNode_) || IsTopWindow(node->GetWindowId(), aboveAppWindowNode_)) {
        WLOGFI("it is already top app window, id: %{public}u", node->GetWindowId());
        return WMError::WM_ERROR_INVALID_TYPE;
    }

    if (WindowHelper::IsSubWindow(node->GetWindowType())) {
        if (parentNode == nullptr) {
            WLOGFE("window type is invalid");
            return WMError::WM_ERROR_NULLPTR;
        }
        RaiseWindowToTop(node->GetWindowId(), parentNode->children_); // raise itself
        if (parentNode->IsSplitMode()) {
            RaiseSplitRelatedWindowToTop(parentNode);
        } else {
            RaiseWindowToTop(node->GetParentId(), parentNode->parent_->children_); // raise parent window
        }
    } else if (WindowHelper::IsMainWindow(node->GetWindowType())) {
        if (node->IsSplitMode()) {
            RaiseSplitRelatedWindowToTop(node);
        } else {
            RaiseWindowToTop(node->GetWindowId(), node->parent_->children_);
        }
    } else {
        // do nothing
    }
    AssignZOrder();
    WLOGFI("RaiseZOrderForAppWindow finished");
    DumpScreenWindowTree();
    return WMError::WM_OK;
}

sptr<WindowNode> WindowNodeContainer::GetNextFocusableWindow(uint32_t windowId) const
{
    sptr<WindowNode> nextFocusableWindow;
    bool previousFocusedWindowFound = false;
    WindowNodeOperationFunc func = [windowId, &nextFocusableWindow, &previousFocusedWindowFound](
        sptr<WindowNode> node) {
        if (previousFocusedWindowFound && node->GetWindowProperty()->GetFocusable()) {
            nextFocusableWindow = node;
            return true;
        }
        if (node->GetWindowId() == windowId) {
            previousFocusedWindowFound = true;
        }
        return false;
    };
    TraverseWindowTree(func, true);
    return nextFocusableWindow;
}

sptr<WindowNode> WindowNodeContainer::GetNextActiveWindow(uint32_t windowId) const
{
    auto currentNode = FindWindowNodeById(windowId);
    if (currentNode == nullptr) {
        WLOGFE("cannot find window id: %{public}u by tree", windowId);
        return nullptr;
    }
    WLOGFI("current window: [%{public}u, %{public}u]", windowId, static_cast<uint32_t>(currentNode->GetWindowType()));
    if (WindowHelper::IsSystemWindow(currentNode->GetWindowType())) {
        for (auto& node : appWindowNode_->children_) {
            if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
                continue;
            }
            return node;
        }
        for (auto& node : belowAppWindowNode_->children_) {
            if (node->GetWindowType() == WindowType::WINDOW_TYPE_DESKTOP) {
                return node;
            }
        }
    } else if (WindowHelper::IsAppWindow(currentNode->GetWindowType())) {
        std::vector<sptr<WindowNode>> windowNodes;
        TraverseContainer(windowNodes);
        auto iter = std::find_if(windowNodes.begin(), windowNodes.end(), [windowId](sptr<WindowNode>& node) {
            return node->GetWindowId() == windowId;
            });
        if (iter == windowNodes.end()) {
            WLOGFE("could not find this window");
            return nullptr;
        }
        int index = std::distance(windowNodes.begin(), iter);
        for (size_t i = static_cast<size_t>(index) + 1; i < windowNodes.size(); i++) {
            if (windowNodes[i]->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
                continue;
            }
            return windowNodes[i];
        }
    } else {
        // do nothing
    }
    WLOGFE("could not get next active window");
    return nullptr;
}

bool WindowNodeContainer::IsForbidDockSliceMove(DisplayId displayId) const
{
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
    if (windowPair == nullptr) {
        WLOGFE("window pair is nullptr");
        return true;
    }
    if (windowPair->IsForbidDockSliceMove()) {
        return true;
    }
    return false;
}

bool WindowNodeContainer::IsDockSliceInExitSplitModeArea(DisplayId displayId) const
{
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
    if (windowPair == nullptr) {
        WLOGFE("window pair is nullptr");
        return false;
    }
    std::vector<int32_t> exitSplitPoints = layoutPolicy_->GetExitSplitPoints(displayId);
    if (exitSplitPoints.size() != EXIT_SPLIT_POINTS_NUMBER) {
        return false;
    }
    return windowPair->IsDockSliceInExitSplitModeArea(exitSplitPoints);
}

void WindowNodeContainer::ExitSplitMode(DisplayId displayId)
{
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
    if (windowPair == nullptr) {
        WLOGFE("window pair is nullptr");
        return;
    }
    windowPair->ExitSplitMode();
}

void WindowNodeContainer::MinimizeAllAppWindows(DisplayId displayId)
{
    WMError ret =  MinimizeAppNodeExceptOptions(MinimizeReason::MINIMIZE_ALL);
    SwitchLayoutPolicy(WindowLayoutMode::CASCADE, displayId);
    if (ret != WMError::WM_OK) {
        WLOGFE("Minimize all app window failed");
    }
    return;
}

void WindowNodeContainer::MinimizeOldestAppWindow()
{
    for (auto& appNode : appWindowNode_->children_) {
        if (appNode->GetWindowType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW) {
            MinimizeApp::AddNeedMinimizeApp(appNode, MinimizeReason::MAX_APP_COUNT);
            return;
        }
    }
    for (auto& appNode : aboveAppWindowNode_->children_) {
        if (appNode->GetWindowType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW) {
            MinimizeApp::AddNeedMinimizeApp(appNode, MinimizeReason::MAX_APP_COUNT);
            return;
        }
    }
    WLOGFI("no window needs to minimize");
}

WMError WindowNodeContainer::ToggleShownStateForAllAppWindows(
    std::function<bool(uint32_t, WindowMode)> restoreFunc, bool restore)
{
    WLOGFI("ToggleShownStateForAllAppWindows");
    for (auto node : aboveAppWindowNode_->children_) {
        if (node->GetWindowType() == WindowType::WINDOW_TYPE_LAUNCHER_RECENT &&
            node->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN && restore) {
            return WMError::WM_DO_NOTHING;
        }
    }
    // to do, backup reentry: 1.ToggleShownStateForAllAppWindows fast; 2.this display should reset backupWindowIds_.
    if (!restore && appWindowNode_->children_.empty() && !backupWindowIds_.empty()) {
        backupWindowIds_.clear();
        backupWindowMode_.clear();
        backupDisplaySplitWindowMode_.clear();
        backupDividerWindowRect_.clear();
    }
    if (!restore && !appWindowNode_->children_.empty() && backupWindowIds_.empty()) {
        WLOGFI("backup");
        BackUpAllAppWindows();
    } else if (restore && !backupWindowIds_.empty()) {
        WLOGFI("restore");
        RestoreAllAppWindows(restoreFunc);
    } else {
        WLOGFI("do nothing because shown app windows is empty or backup windows is empty.");
    }
    return WMError::WM_OK;
}

void WindowNodeContainer::BackUpAllAppWindows()
{
    std::set<DisplayId> displayIdSet;
    backupWindowMode_.clear();
    backupDisplaySplitWindowMode_.clear();
    std::vector<sptr<WindowNode>> children = appWindowNode_->children_;
    for (auto& appNode : children) {
        if (!WindowHelper::IsMainWindow(appNode->GetWindowType())) {
            continue;
        }
        auto windowMode = appNode->GetWindowMode();
        backupWindowMode_[appNode->GetWindowId()] = windowMode;
        if (WindowHelper::IsSplitWindowMode(windowMode)) {
            backupDisplaySplitWindowMode_[appNode->GetDisplayId()].insert(windowMode);
        }
        displayIdSet.insert(appNode->GetDisplayId());
    }
    for (auto& appNode : children) {
        // exclude exceptional window
        if (!WindowHelper::IsMainWindow(appNode->GetWindowType())) {
            WLOGFE("is not main window, windowId:%{public}u", appNode->GetWindowId());
            continue;
        }
        // minimize window
        WLOGFD("minimize window, windowId:%{public}u", appNode->GetWindowId());
        backupWindowIds_.emplace_back(appNode->GetWindowId());
        WindowManagerService::GetInstance().HandleRemoveWindow(appNode->GetWindowId());
        AAFwk::AbilityManagerClient::GetInstance()->DoAbilityBackground(appNode->abilityToken_,
            static_cast<uint32_t>(WindowStateChangeReason::TOGGLING));
    }
    backupDividerWindowRect_.clear();
    for (auto displayId : displayIdSet) {
        auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
        if (windowPair == nullptr || windowPair->GetDividerWindow() == nullptr) {
            continue;
        }
        backupDividerWindowRect_[displayId] = windowPair->GetDividerWindow()->GetWindowRect();
    }
}

void WindowNodeContainer::RestoreAllAppWindows(std::function<bool(uint32_t, WindowMode)> restoreFunc)
{
    std::vector<uint32_t> backupWindowIds(backupWindowIds_);
    auto displayIds = DisplayManagerServiceInner::GetInstance().GetAllDisplayIds();
    std::vector<sptr<WindowPair>> windowPairs;
    for (auto displayId : displayIds) {
        auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
        if (windowPair != nullptr) {
            if (backupDisplaySplitWindowMode_[displayId].count(WindowMode::WINDOW_MODE_SPLIT_PRIMARY) > 0 &&
                backupDisplaySplitWindowMode_[displayId].count(WindowMode::WINDOW_MODE_SPLIT_SECONDARY) > 0) {
                windowPair->SetAllSplitAppWindowsRestoring(true);
            }
            windowPairs.emplace_back(windowPair);
        }
    }
    for (auto windowId: backupWindowIds) {
        if (!restoreFunc(windowId, backupWindowMode_[windowId])) {
            WLOGFE("restore %{public}u failed", windowId);
            continue;
        }
        WLOGFD("restore %{public}u", windowId);
    }
    for (auto windowPair : windowPairs) {
        windowPair->SetAllSplitAppWindowsRestoring(false);
    }
    layoutPolicy_->SetSplitDividerWindowRects(backupDividerWindowRect_);
    backupWindowIds_.clear();
    backupWindowMode_.clear();
    backupDividerWindowRect_.clear();
}

bool WindowNodeContainer::IsAppWindowsEmpty() const
{
    return appWindowNode_->children_.empty();
}

WMError WindowNodeContainer::MinimizeAppNodeExceptOptions(MinimizeReason reason,
    const std::vector<uint32_t> &exceptionalIds, const std::vector<WindowMode> &exceptionalModes)
{
    if (appWindowNode_->children_.empty()) {
        return WMError::WM_OK;
    }
    for (auto& appNode : appWindowNode_->children_) {
        // exclude exceptional window
        if (std::find(exceptionalIds.begin(), exceptionalIds.end(), appNode->GetWindowId()) != exceptionalIds.end() ||
            std::find(exceptionalModes.begin(), exceptionalModes.end(),
                appNode->GetWindowMode()) != exceptionalModes.end() ||
                appNode->GetWindowType() != WindowType::WINDOW_TYPE_APP_MAIN_WINDOW) {
            continue;
        }
        MinimizeApp::AddNeedMinimizeApp(appNode, reason);
    }
    return WMError::WM_OK;
}

WMError WindowNodeContainer::MinimizeStructuredAppWindowsExceptSelf(const sptr<WindowNode>& node)
{
    std::vector<uint32_t> exceptionalIds = { node->GetWindowId() };
    std::vector<WindowMode> exceptionalModes = { WindowMode::WINDOW_MODE_FLOATING, WindowMode::WINDOW_MODE_PIP };
    return MinimizeAppNodeExceptOptions(MinimizeReason::OTHER_WINDOW, exceptionalIds, exceptionalModes);
}

void WindowNodeContainer::ResetLayoutPolicy()
{
    layoutPolicy_->Reset();
}

WMError WindowNodeContainer::SwitchLayoutPolicy(WindowLayoutMode dstMode, DisplayId displayId, bool reorder)
{
    WLOGFI("SwitchLayoutPolicy src: %{public}d dst: %{public}d, reorder: %{public}d, displayId: %{public}" PRIu64"",
        static_cast<uint32_t>(layoutMode_), static_cast<uint32_t>(dstMode), static_cast<uint32_t>(reorder), displayId);
    if (dstMode < WindowLayoutMode::BASE || dstMode >= WindowLayoutMode::END) {
        WLOGFE("invalid layout mode");
        return WMError::WM_ERROR_INVALID_PARAM;
    }
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(displayId);
    if (windowPair == nullptr) {
        WLOGFE("Window pair is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (layoutMode_ != dstMode) {
        if (layoutMode_ == WindowLayoutMode::CASCADE) {
            layoutPolicy_->Reset();
            windowPair->Clear();
        }
        layoutMode_ = dstMode;
        layoutPolicy_->Clean();
        layoutPolicy_ = layoutPolicies_[dstMode];
        layoutPolicy_->Launch();
        DumpScreenWindowTree();
    } else {
        WLOGFI("Current layout mode is already: %{public}d", static_cast<uint32_t>(dstMode));
    }
    if (reorder) {
        windowPair->Clear();
        layoutPolicy_->Reorder();
        DumpScreenWindowTree();
    }
    NotifyIfSystemBarTintChanged(displayId);
    return WMError::WM_OK;
}

void WindowNodeContainer::UpdateModeSupportInfoWhenKeyguardChange(const sptr<WindowNode>& node, bool up)
{
    if (!WindowHelper::IsWindowModeSupported(node->GetWindowProperty()->GetRequestModeSupportInfo(),
                                             WindowMode::WINDOW_MODE_SPLIT_PRIMARY)) {
        WLOGFD("window doesn't support split mode, winId: %{public}d", node->GetWindowId());
        return;
    }
    uint32_t modeSupportInfo;
    if (up) {
        modeSupportInfo = node->GetModeSupportInfo() & (~WindowModeSupport::WINDOW_MODE_SUPPORT_SPLIT_PRIMARY);
    } else {
        modeSupportInfo = node->GetModeSupportInfo() | WindowModeSupport::WINDOW_MODE_SUPPORT_SPLIT_PRIMARY;
    }
    node->SetModeSupportInfo(modeSupportInfo);
    if (node->GetWindowToken() != nullptr) {
        node->GetWindowToken()->UpdateWindowModeSupportInfo(modeSupportInfo);
    }
}

void WindowNodeContainer::RaiseInputMethodWindowPriorityIfNeeded(const sptr<WindowNode>& node) const
{
    if (node->GetWindowType() != WindowType::WINDOW_TYPE_INPUT_METHOD_FLOAT || !isScreenLocked_) {
        return;
    }

    WLOGFI("raise input method float window priority.");
    node->priority_ = zorderPolicy_->GetWindowPriority(
        WindowType::WINDOW_TYPE_KEYGUARD) + 2; // 2: higher than keyguard and show when locked window
}

void WindowNodeContainer::ReZOrderShowWhenLockedWindows(bool up)
{
    WLOGFI("Keyguard change %{public}u, re-zorder showWhenLocked window", up);
    std::vector<sptr<WindowNode>> needReZOrderNodes;
    auto& srcRoot = up ? appWindowNode_ : aboveAppWindowNode_;
    auto& dstRoot = up ? aboveAppWindowNode_ : appWindowNode_;

    auto dstPriority = up ? zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_KEYGUARD) + 1 :
        zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_APP_MAIN_WINDOW);

    for (auto iter = srcRoot->children_.begin(); iter != srcRoot->children_.end();) {
        if ((*iter)->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED)) {
            needReZOrderNodes.emplace_back(*iter);
            iter = srcRoot->children_.erase(iter);
        } else {
            iter++;
        }
    }

    for (auto& needReZOrderNode : needReZOrderNodes) {
        needReZOrderNode->priority_ = dstPriority;
        needReZOrderNode->parent_ = dstRoot;
        auto parentNode = needReZOrderNode->parent_;
        auto position = parentNode->children_.end();
        for (auto iter = parentNode->children_.begin(); iter < parentNode->children_.end(); ++iter) {
            if ((*iter)->priority_ > needReZOrderNode->priority_) {
                position = iter;
                break;
            }
        }

        UpdateModeSupportInfoWhenKeyguardChange(needReZOrderNode, up);

        parentNode->children_.insert(position, needReZOrderNode);
        if (up && WindowHelper::IsSplitWindowMode(needReZOrderNode->GetWindowMode())) {
            needReZOrderNode->GetWindowProperty()->ResumeLastWindowMode();
            if (needReZOrderNode->GetWindowToken() != nullptr) {
                needReZOrderNode->GetWindowToken()->UpdateWindowMode(needReZOrderNode->GetWindowMode());
            }
            auto windowPair = displayGroupController_->GetWindowPairByDisplayId(needReZOrderNode->GetDisplayId());
            if (windowPair == nullptr) {
                WLOGFE("Window pair is nullptr");
                return;
            }
            windowPair->UpdateIfSplitRelated(needReZOrderNode);
        }
        WLOGFI("window %{public}u re-zorder when keyguard change %{public}u", needReZOrderNode->GetWindowId(), up);
    }
}

void WindowNodeContainer::ReZOrderShowWhenLockedWindowIfNeeded(const sptr<WindowNode>& node)
{
    if (!(node->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED)) ||
        !isScreenLocked_) {
        return;
    }

    WLOGFI("ShowWhenLocked window %{public}u re-zorder to up", node->GetWindowId());
    ReZOrderShowWhenLockedWindows(true);
}

void WindowNodeContainer::RaiseShowWhenLockedWindowIfNeeded(const sptr<WindowNode>& node)
{
    // if keyguard window show, raise show when locked windows
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
        ReZOrderShowWhenLockedWindows(true);
        return;
    }

    // if show when locked window show, raise itself when exist keyguard
    if (!(node->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED)) ||
        !isScreenLocked_) {
        return;
    }

    WLOGFI("ShowWhenLocked window %{public}u raise itself", node->GetWindowId());
    node->priority_ = zorderPolicy_->GetWindowPriority(WindowType::WINDOW_TYPE_KEYGUARD) + 1;
    node->parent_ = aboveAppWindowNode_;
    if (WindowHelper::IsSplitWindowMode(node->GetWindowMode())) {
        node->GetWindowProperty()->ResumeLastWindowMode();
        if (node->GetWindowToken() != nullptr) {
            node->GetWindowToken()->UpdateWindowMode(node->GetWindowMode());
        }
    }
}

void WindowNodeContainer::DropShowWhenLockedWindowIfNeeded(const sptr<WindowNode>& node)
{
    // if keyguard window hide, drop show when locked windows
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
        ReZOrderShowWhenLockedWindows(false);
        AssignZOrder();
    }
}

void WindowNodeContainer::TraverseWindowTree(const WindowNodeOperationFunc& func, bool isFromTopToBottom) const
{
    std::vector<sptr<WindowNode>> rootNodes = { belowAppWindowNode_, appWindowNode_, aboveAppWindowNode_ };
    if (isFromTopToBottom) {
        std::reverse(rootNodes.begin(), rootNodes.end());
    }

    for (auto& node : rootNodes) {
        if (isFromTopToBottom) {
            for (auto iter = node->children_.rbegin(); iter != node->children_.rend(); ++iter) {
                if (TraverseFromTopToBottom(*iter, func)) {
                    return;
                }
            }
        } else {
            for (auto iter = node->children_.begin(); iter != node->children_.end(); ++iter) {
                if (TraverseFromBottomToTop(*iter, func)) {
                    return;
                }
            }
        }
    }
}

bool WindowNodeContainer::TraverseFromTopToBottom(sptr<WindowNode> node, const WindowNodeOperationFunc& func) const
{
    if (node == nullptr) {
        return false;
    }
    auto iterBegin = node->children_.rbegin();
    for (; iterBegin != node->children_.rend(); ++iterBegin) {
        if ((*iterBegin)->priority_ <= 0) {
            break;
        }
        if (func(*iterBegin)) {
            return true;
        }
    }
    if (func(node)) {
        return true;
    }
    for (; iterBegin != node->children_.rend(); ++iterBegin) {
        if (func(*iterBegin)) {
            return true;
        }
    }
    return false;
}

bool WindowNodeContainer::TraverseFromBottomToTop(sptr<WindowNode> node, const WindowNodeOperationFunc& func) const
{
    if (node == nullptr) {
        return false;
    }
    auto iterBegin = node->children_.begin();
    for (; iterBegin != node->children_.end(); ++iterBegin) {
        if ((*iterBegin)->priority_ >= 0) {
            break;
        }
        if (func(*iterBegin)) {
            return true;
        }
    }
    if (func(node)) {
        return true;
    }
    for (; iterBegin != node->children_.end(); ++iterBegin) {
        if (func(*iterBegin)) {
            return true;
        }
    }
    return false;
}

float WindowNodeContainer::GetVirtualPixelRatio(DisplayId displayId) const
{
    return layoutPolicy_->GetVirtualPixelRatio(displayId);
}

Rect WindowNodeContainer::GetDisplayGroupRect() const
{
    return layoutPolicy_->GetDisplayGroupRect();
}

bool WindowNodeContainer::ReadIsWindowAnimationEnabledProperty()
{
    if (access(DISABLE_WINDOW_ANIMATION_PATH, F_OK) == 0) {
        return false;
    }
    return true;
}

WMError WindowNodeContainer::SetWindowMode(sptr<WindowNode>& node, WindowMode dstMode)
{
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    WindowMode srcMode = node->GetWindowMode();
    if (srcMode == dstMode) {
        return WMError::WM_OK;
    }

    if (WindowHelper::IsSplitWindowMode(dstMode) && isScreenLocked_ &&
        (node->GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED))) {
        return WMError::WM_ERROR_INVALID_PARAM;
    }

    WMError res = WMError::WM_OK;
    if ((srcMode == WindowMode::WINDOW_MODE_FULLSCREEN) && (dstMode == WindowMode::WINDOW_MODE_FLOATING)) {
        node->SetWindowSizeChangeReason(WindowSizeChangeReason::RECOVER);
    } else if (dstMode == WindowMode::WINDOW_MODE_FULLSCREEN) {
        node->SetWindowSizeChangeReason(WindowSizeChangeReason::MAXIMIZE);
        if (srcMode == WindowMode::WINDOW_MODE_FLOATING) {
            node->SetRequestRect(node->GetWindowRect());
        }
    } else {
        node->SetWindowSizeChangeReason(WindowSizeChangeReason::RESIZE);
    }
    node->SetWindowMode(dstMode);
    auto windowPair = displayGroupController_->GetWindowPairByDisplayId(node->GetDisplayId());
    if (windowPair == nullptr) {
        WLOGFE("Window pair is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    windowPair->UpdateIfSplitRelated(node);
    if (WindowHelper::IsMainWindow(node->GetWindowType())) {
        if (WindowHelper::IsFloatingWindow(node->GetWindowMode())) {
            NotifyDockWindowStateChanged(node, true);
        } else {
            NotifyDockWindowStateChanged(node, false);
        }
    }

    if (node->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN &&
        WindowHelper::IsAppWindow(node->GetWindowType())) {
        // minimize other app window
        res = MinimizeStructuredAppWindowsExceptSelf(node);
        if (res != WMError::WM_OK) {
            return res;
        }
    }
    if (node->GetWindowToken() != nullptr) {
        node->GetWindowToken()->UpdateWindowMode(node->GetWindowMode());
    }
    res = UpdateWindowNode(node, WindowUpdateReason::UPDATE_MODE);
    if (res != WMError::WM_OK) {
        WLOGFE("Set window mode failed, update node failed");
        return res;
    }
    return WMError::WM_OK;
}

void WindowNodeContainer::GetModeChangeHotZones(DisplayId displayId, ModeChangeHotZones& hotZones,
    const ModeChangeHotZonesConfig& config)
{
    const auto& displayRect = displayGroupInfo_->GetDisplayRect(displayId);

    hotZones.fullscreen_.width_ = displayRect.width_;
    hotZones.fullscreen_.height_ = config.fullscreenRange_;

    hotZones.primary_.width_ = config.primaryRange_;
    hotZones.primary_.height_ = displayRect.height_;

    hotZones.secondary_.posX_ = static_cast<int32_t>(displayRect.width_) - config.secondaryRange_;
    hotZones.secondary_.width_ = config.secondaryRange_;
    hotZones.secondary_.height_ = displayRect.height_;
}

float WindowNodeContainer::GetDisplayVirtualPixelRatio(DisplayId displayId) const
{
    return displayGroupInfo_->GetDisplayVirtualPixelRatio(displayId);
}

sptr<DisplayInfo> WindowNodeContainer::GetDisplayInfo(DisplayId displayId)
{
    return displayGroupInfo_->GetDisplayInfo(displayId);
}

Orientation WindowNodeContainer::GetWindowPreferredOrientation()
{
    std::vector<sptr<WindowNode>> windowNodes;
    TraverseContainer(windowNodes);
    for (auto node : windowNodes) {
        if (node->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN) {
            return node->GetRequestedOrientation();
        }
        if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
            return Orientation::UNSPECIFIED;
        }
    }
    return Orientation::UNSPECIFIED;
}

void WindowNodeContainer::UpdateCameraFloatWindowStatus(const sptr<WindowNode>& node, bool isShowing)
{
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_FLOAT_CAMERA) {
        WindowManagerAgentController::GetInstance().UpdateCameraFloatWindowStatus(node->GetAccessTokenId(), isShowing);
    }
}

WindowLayoutMode WindowNodeContainer::GetCurrentLayoutMode() const
{
    return layoutMode_;
}

void WindowNodeContainer::RemoveSingleUserWindowNodes()
{
    std::vector<sptr<WindowNode>> windowNodes;
    TraverseContainer(windowNodes);
    for (auto& windowNode : windowNodes) {
        if (windowNode->GetWindowType() == WindowType::WINDOW_TYPE_DESKTOP ||
            windowNode->GetWindowType() == WindowType::WINDOW_TYPE_STATUS_BAR ||
            windowNode->GetWindowType() == WindowType::WINDOW_TYPE_NAVIGATION_BAR ||
            windowNode->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD ||
            windowNode->GetWindowType() == WindowType::WINDOW_TYPE_POINTER ||
            windowNode->GetWindowType() == WindowType::WINDOW_TYPE_BOOT_ANIMATION) {
            continue;
        }
        WLOGFI("remove window %{public}s, windowId %{public}d",
            windowNode->GetWindowName().c_str(), windowNode->GetWindowId());
        windowNode->GetWindowProperty()->SetAnimationFlag(static_cast<uint32_t>(WindowAnimation::NONE));
        RemoveWindowNode(windowNode);
    }
}
} // namespace Rosen
} // namespace OHOS
