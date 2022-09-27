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

#include "window_controller.h"
#include <ability_manager_client.h>
#include <chrono>
#include <hisysevent.h>
#include <hitrace_meter.h>
#include <parameters.h>
#include <power_mgr_client.h>
#include <rs_window_animation_finished_callback.h>
#include <transaction/rs_transaction.h>
#include <sstream>

#include "minimize_app.h"
#include "remote_animation.h"
#include "starting_window.h"
#include "window_manager_hilog.h"
#include "window_helper.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowController"};
    constexpr uint32_t TOUCH_HOT_AREA_MAX_NUM = 10;
}

uint32_t WindowController::GenWindowId()
{
    return ++windowId_;
}

void WindowController::StartingWindow(sptr<WindowTransitionInfo> info, sptr<Media::PixelMap> pixelMap,
    uint32_t bkgColor, bool isColdStart)
{
    if (!info || info->GetAbilityToken() == nullptr) {
        WLOGFE("info or AbilityToken is nullptr!");
        return;
    }
    StartAsyncTraceArgs(HITRACE_TAG_WINDOW_MANAGER, static_cast<int32_t>(TraceTaskId::STARTING_WINDOW),
        "wms:async:ShowStartingWindow");
    auto node = windowRoot_->FindWindowNodeWithToken(info->GetAbilityToken());
    if (node == nullptr) {
        if (!isColdStart) {
            WLOGFE("no windowNode exists but is hot start!");
            return;
        }
        node = StartingWindow::CreateWindowNode(info, GenWindowId());
        if (node == nullptr) {
            return;
        }
        if (windowRoot_->SaveWindow(node) != WMError::WM_OK) {
            return;
        }
        if (!RemoteAnimation::CheckAnimationController()) {
            UpdateWindowAnimation(node);
        }
    } else {
        if (isColdStart) {
            WLOGFE("windowNode exists but is cold start!");
            return;
        }
        if (WindowHelper::IsValidWindowMode(info->GetWindowMode()) &&
            (node->GetWindowMode() != info->GetWindowMode())) {
            WLOGFW("set starting window mode. starting mode is: %{public}u, window mode is:%{public}u.",
                node->GetWindowMode(), info->GetWindowMode());
            node->SetWindowMode(info->GetWindowMode());
        }
    }

    if (StartingWindow::NeedToStopStartingWindow(node->GetWindowMode(), node->GetModeSupportInfo(), info)) {
        WLOGFE("need to cancel starting window");
        return;
    }

    if (windowRoot_->AddWindowNode(0, node, true) != WMError::WM_OK) {
        return;
    }
    StartingWindow::DrawStartingWindow(node, pixelMap, bkgColor, isColdStart);
    RSTransaction::FlushImplicitTransaction();
    node->startingWindowShown_ = true;
    WLOGFI("StartingWindow show success with id:%{public}u!", node->GetWindowId());
}

void WindowController::CancelStartingWindow(sptr<IRemoteObject> abilityToken)
{
    auto node = windowRoot_->FindWindowNodeWithToken(abilityToken);
    if (node == nullptr) {
        WLOGFI("cannot find windowNode!");
        return;
    }
    if (!node->startingWindowShown_) {
        WLOGFE("CancelStartingWindow failed because client window has shown id:%{public}u", node->GetWindowId());
        return;
    }
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:CancelStartingWindow(%u)", node->GetWindowId());
    FinishAsyncTraceArgs(HITRACE_TAG_WINDOW_MANAGER, static_cast<int32_t>(TraceTaskId::STARTING_WINDOW),
        "wms:async:ShowStartingWindow");
    WLOGFI("CancelStartingWindow with id:%{public}u!", node->GetWindowId());
    node->isAppCrash_ = true;
    WMError res = windowRoot_->DestroyWindow(node->GetWindowId(), false);
    if (res != WMError::WM_OK) {
        WLOGFE("DestroyWindow failed!");
    }
}

WMError WindowController::NotifyWindowTransition(sptr<WindowTransitionInfo>& srcInfo,
    sptr<WindowTransitionInfo>& dstInfo)
{
    WLOGFI("NotifyWindowTransition begin!");
    if (!srcInfo || !dstInfo) {
        WLOGFE("srcInfo or dstInfo is nullptr!");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto dstNode = windowRoot_->FindWindowNodeWithToken(dstInfo->GetAbilityToken());
    auto srcNode = windowRoot_->FindWindowNodeWithToken(srcInfo->GetAbilityToken());
    if (!RemoteAnimation::CheckTransition(srcInfo, srcNode, dstInfo, dstNode)) {
        return WMError::WM_ERROR_NO_REMOTE_ANIMATION;
    }
    StartAsyncTraceArgs(HITRACE_TAG_WINDOW_MANAGER, static_cast<int32_t>(TraceTaskId::REMOTE_ANIMATION),
        "wms:async:ShowRemoteAnimation");
    auto transitionEvent = RemoteAnimation::GetTransitionEvent(srcInfo, dstInfo, srcNode, dstNode);
    switch (transitionEvent) {
        case TransitionEvent::APP_TRANSITION: {
            if (dstNode->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN) {
                windowRoot_->MinimizeStructuredAppWindowsExceptSelf(dstNode); // avoid split/float mode minimize
            }
            return RemoteAnimation::NotifyAnimationTransition(srcInfo, dstInfo, srcNode, dstNode, windowRoot_);
        }
        case TransitionEvent::MINIMIZE:
            return RemoteAnimation::NotifyAnimationMinimize(srcInfo, srcNode, windowRoot_);
        case TransitionEvent::CLOSE:
            return RemoteAnimation::NotifyAnimationClose(srcInfo, srcNode, TransitionEvent::CLOSE, windowRoot_);
        case TransitionEvent::BACK:
            return RemoteAnimation::NotifyAnimationClose(srcInfo, srcNode, TransitionEvent::BACK, windowRoot_);
        default:
            return WMError::WM_ERROR_NO_REMOTE_ANIMATION;
    }
    return WMError::WM_OK;
}

WMError WindowController::GetFocusWindowNode(DisplayId displayId, sptr<WindowNode>& windowNode)
{
    auto windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(displayId);
    if (windowNodeContainer == nullptr) {
        WLOGFE("windowNodeContainer is null, displayId: %{public}" PRIu64"", displayId);
        return WMError::WM_ERROR_NULLPTR;
    }
    uint32_t focusWindowId = windowNodeContainer->GetFocusWindow();
    WLOGFI("focusWindowId: %{public}u", focusWindowId);
    auto thisWindowNode = windowRoot_->GetWindowNode(focusWindowId);
    if (thisWindowNode == nullptr || !thisWindowNode->currentVisibility_) {
        WLOGFE("focusWindowNode is null or invisible, focusWindowId: %{public}u", focusWindowId);
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    windowNode = thisWindowNode;
    return WMError::WM_OK;
}

WMError WindowController::GetFocusWindowInfo(sptr<IRemoteObject>& abilityToken)
{
    DisplayId displayId = DisplayManagerServiceInner::GetInstance().GetDefaultDisplayId();
    sptr<WindowNode> windowNode;
    WMError res = GetFocusWindowNode(displayId, windowNode);
    if (res == WMError::WM_OK) {
        abilityToken = windowNode->abilityToken_;
    }
    return res;
}

WMError WindowController::CreateWindow(sptr<IWindow>& window, sptr<WindowProperty>& property,
    const std::shared_ptr<RSSurfaceNode>& surfaceNode, uint32_t& windowId, sptr<IRemoteObject> token,
    int32_t pid, int32_t uid)
{
    uint32_t parentId = property->GetParentId();
    if ((parentId != INVALID_WINDOW_ID) && !WindowHelper::IsSubWindow(property->GetWindowType())) {
        WLOGFE("create window failed, type is error");
        return WMError::WM_ERROR_INVALID_TYPE;
    }
    sptr<WindowNode> node = windowRoot_->FindWindowNodeWithToken(token);
    if (node != nullptr && WindowHelper::IsMainWindow(property->GetWindowType()) && node->startingWindowShown_) {
        StartingWindow::HandleClientWindowCreate(node, window, windowId, surfaceNode, property, pid, uid);
        windowRoot_->AddDeathRecipient(node);
        windowRoot_->AddSurfaceNodeIdWindowNodePair(surfaceNode->GetId(), node);
        return WMError::WM_OK;
    }
    windowId = GenWindowId();
    sptr<WindowProperty> windowProperty = new WindowProperty(property);
    windowProperty->SetWindowId(windowId);
    node = new WindowNode(windowProperty, window, surfaceNode, pid, uid);
    node->abilityToken_ = token;
    UpdateWindowAnimation(node);
    WLOGFI("createWindow name:%{public}u, windowName:%{public}s",
        windowId, node->GetWindowName().c_str());
    return windowRoot_->SaveWindow(node);
}

WMError WindowController::AddWindowNode(sptr<WindowProperty>& property)
{
    auto node = windowRoot_->GetWindowNode(property->GetWindowId());
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }

    if (node->currentVisibility_ && !node->startingWindowShown_) {
        WLOGFE("current window is visible, windowId: %{public}u", node->GetWindowId());
        return WMError::WM_ERROR_INVALID_OPERATION;
    }

    // using starting window rect if client rect is empty
    if (WindowHelper::IsEmptyRect(property->GetRequestRect()) && node->startingWindowShown_) { // for tile and cascade
        property->SetRequestRect(node->GetRequestRect());
        property->SetWindowRect(node->GetWindowRect());
        property->SetDecoStatus(true);
    }
    node->GetWindowProperty()->CopyFrom(property);

    // Need 'check permission'
    // Need 'adjust property'
    UpdateWindowAnimation(node);
    WMError res = windowRoot_->AddWindowNode(property->GetParentId(), node);
    if (res != WMError::WM_OK) {
        MinimizeApp::ClearNodesWithReason(MinimizeReason::OTHER_WINDOW);
        return res;
    }
    windowRoot_->FocusFaultDetection();
    FlushWindowInfo(property->GetWindowId());
    HandleTurnScreenOn(node);

    if (node->GetWindowType() == WindowType::WINDOW_TYPE_STATUS_BAR ||
        node->GetWindowType() == WindowType::WINDOW_TYPE_NAVIGATION_BAR) {
        sysBarWinId_[node->GetWindowType()] = node->GetWindowId();
        ResizeSystemBarPropertySizeIfNeed(node);
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_INPUT_METHOD_FLOAT) {
        ResizeSoftInputCallingWindowIfNeed(node);
    }
    StopBootAnimationIfNeed(node->GetWindowType());
    MinimizeApp::ExecuteMinimizeAll();
    return WMError::WM_OK;
}

void WindowController::ResizeSoftInputCallingWindowIfNeed(const sptr<WindowNode>& node)
{
    auto callingWindowId = node->GetCallingWindow();
    auto callingWindow = windowRoot_->GetWindowNode(callingWindowId);
    if (callingWindow == nullptr) {
        auto windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(node->GetDisplayId());
        if (windowNodeContainer == nullptr) {
            WLOGFE("windowNodeContainer is null, displayId:%{public}" PRIu64"", node->GetDisplayId());
            return;
        }
        callingWindowId = windowNodeContainer->GetFocusWindow();
        callingWindow = windowRoot_->GetWindowNode(callingWindowId);
    }
    if (callingWindow == nullptr || !callingWindow->currentVisibility_ ||
        callingWindow->GetWindowMode() != WindowMode::WINDOW_MODE_FLOATING) {
        WLOGFE("callingWindow is null or invisible or not float window, callingWindowId:%{public}u", callingWindowId);
        return;
    }
    Rect softInputWindowRect = node->GetWindowRect();
    Rect callingWindowRect = callingWindow->GetWindowRect();
    Rect rect = WindowHelper::GetOverlap(softInputWindowRect, callingWindowRect, 0, 0);
    if (WindowHelper::IsEmptyRect(rect)) {
        WLOGFE("there is no overlap");
        return;
    }
    Rect requestedRect = callingWindowRect;
    requestedRect.posY_ = softInputWindowRect.posY_ - static_cast<int32_t>(requestedRect.height_);
    Rect statusBarWindowRect = { 0, 0, 0, 0 };
    auto statusbarWindow = windowRoot_->GetWindowNode(sysBarWinId_[WindowType::WINDOW_TYPE_STATUS_BAR]);
    if (statusbarWindow != nullptr && statusbarWindow->parent_ != nullptr) {
        statusBarWindowRect = statusbarWindow->GetWindowRect();
    }
    int32_t posY = std::max(requestedRect.posY_, static_cast<int32_t>(statusBarWindowRect.height_));
    if (posY != requestedRect.posY_) {
        requestedRect.height_ = softInputWindowRect.posY_ - posY;
        requestedRect.posY_ = posY;
    }
    callingWindowRestoringRect_ = callingWindowRect;
    callingWindowId_ = callingWindow->GetWindowId();
    ResizeRect(callingWindowId_, requestedRect, WindowSizeChangeReason::DRAG);
}

void WindowController::RestoreCallingWindowSizeIfNeed()
{
    auto callingWindow = windowRoot_->GetWindowNode(callingWindowId_);
    if (!WindowHelper::IsEmptyRect(callingWindowRestoringRect_) && callingWindow != nullptr &&
        callingWindow->GetWindowMode() == WindowMode::WINDOW_MODE_FLOATING) {
        ResizeRect(callingWindowId_, callingWindowRestoringRect_, WindowSizeChangeReason::DRAG);
    }
    callingWindowRestoringRect_ = { 0, 0, 0, 0 };
    callingWindowId_ = 0u;
}

void WindowController::ResizeSystemBarPropertySizeIfNeed(const sptr<WindowNode>& node)
{
    auto displayInfo = DisplayManagerServiceInner::GetInstance().GetDisplayById(node->GetDisplayId());
    if (displayInfo == nullptr) {
        WLOGFE("displayInfo is null");
        return;
    }
    auto width = static_cast<uint32_t>(displayInfo->GetWidth());
    auto height = static_cast<uint32_t>(displayInfo->GetHeight());
    Rect newRect = node->GetWindowRect();
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_STATUS_BAR) {
        auto statusBarHeight = newRect.height_;
        newRect = { 0, 0, width, statusBarHeight };
    } else if (node->GetWindowType() == WindowType::WINDOW_TYPE_NAVIGATION_BAR) {
        auto naviBarHeight = newRect.height_;
        newRect = { 0, static_cast<int32_t>(height - naviBarHeight), width, naviBarHeight };
    }
    if (newRect != node->GetWindowRect()) {
        ResizeRect(node->GetWindowId(), newRect, WindowSizeChangeReason::DRAG);
    }
}

void WindowController::HandleTurnScreenOn(const sptr<WindowNode>& node)
{
    if (node == nullptr) {
        WLOGFE("window is invalid");
        return;
    }
    WLOGFI("handle turn screen on: [%{public}s, %{public}d]", node->GetWindowName().c_str(), node->IsTurnScreenOn());
    // reset ipc identity
    std::string identity = IPCSkeleton::ResetCallingIdentity();
    if (node->IsTurnScreenOn() && !PowerMgr::PowerMgrClient::GetInstance().IsScreenOn()) {
        WLOGFI("turn screen on");
        PowerMgr::PowerMgrClient::GetInstance().WakeupDevice();
    }
    // set ipc identity to raw
    IPCSkeleton::SetCallingIdentity(identity);
}

WMError WindowController::RemoveWindowNode(uint32_t windowId)
{
    auto removeFunc = [this, windowId]() {
        WMError res = windowRoot_->RemoveWindowNode(windowId);
        if (res != WMError::WM_OK) {
            WLOGFE("RemoveWindowNode failed");
            return res;
        }
        windowRoot_->FocusFaultDetection();
        FlushWindowInfo(windowId);
        return res;
    };
    auto windowNode = windowRoot_->GetWindowNode(windowId);
    if (windowNode == nullptr) {
        WLOGFE("windowNode is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    WMError res = WMError::WM_ERROR_NO_REMOTE_ANIMATION;
    if (windowNode->GetWindowType() == WindowType::WINDOW_TYPE_KEYGUARD) {
        if (RemoteAnimation::NotifyAnimationScreenUnlock(removeFunc) == WMError::WM_OK) {
            WLOGFI("NotifyAnimationScreenUnlock with remote animation");
            res = WMError::WM_OK;
        }
    }
    if (res != WMError::WM_OK) {
        res = removeFunc();
    }
    if (windowNode->GetWindowType() == WindowType::WINDOW_TYPE_INPUT_METHOD_FLOAT) {
        RestoreCallingWindowSizeIfNeed();
    }
    return res;
}

WMError WindowController::DestroyWindow(uint32_t windowId, bool onlySelf)
{
    DisplayId displayId = DISPLAY_ID_INVALID;
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node != nullptr) {
        displayId = node->GetDisplayId();
    }
    WMError res = windowRoot_->DestroyWindow(windowId, onlySelf);
    if (res != WMError::WM_OK) {
        return res;
    }
    windowRoot_->FocusFaultDetection();
    FlushWindowInfoWithDisplayId(displayId);
    return res;
}

WMError WindowController::ResizeRect(uint32_t windowId, const Rect& rect, WindowSizeChangeReason reason)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (node->GetWindowMode() != WindowMode::WINDOW_MODE_FLOATING) {
        WLOGFE("fullscreen window could not resize");
        return WMError::WM_ERROR_INVALID_OPERATION;
    }
    auto property = node->GetWindowProperty();
    node->SetWindowSizeChangeReason(reason);
    Rect lastRect = property->GetWindowRect();
    Rect newRect;
    if (reason == WindowSizeChangeReason::MOVE) {
        newRect = { rect.posX_, rect.posY_, lastRect.width_, lastRect.height_ };
        if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
            if (windowRoot_->IsForbidDockSliceMove(node->GetDisplayId())) {
                WLOGFI("dock slice is forbidden to move");
                newRect = lastRect;
            } else if (windowRoot_->isVerticalDisplay(node)) {
                newRect.posX_ = lastRect.posX_;
            } else {
                newRect.posY_ = lastRect.posY_;
            }
        }
    } else if (reason == WindowSizeChangeReason::RESIZE) {
        newRect = { lastRect.posX_, lastRect.posY_, rect.width_, rect.height_ };
    } else if (reason == WindowSizeChangeReason::DRAG) {
        newRect = rect;
    }
    property->SetRequestRect(newRect);
    WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_RECT);
    if (res != WMError::WM_OK) {
        return res;
    }
    FlushWindowInfo(windowId);
    return WMError::WM_OK;
}

WMError WindowController::RequestFocus(uint32_t windowId)
{
    if (windowRoot_ == nullptr) {
        return WMError::WM_ERROR_NULLPTR;
    }
    return windowRoot_->RequestFocus(windowId);
}

WMError WindowController::SetWindowMode(uint32_t windowId, WindowMode dstMode)
{
    HITRACE_METER(HITRACE_TAG_WINDOW_MANAGER);
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    WMError ret = windowRoot_->SetWindowMode(node, dstMode);
    if (ret != WMError::WM_OK) {
        return ret;
    }
    FlushWindowInfo(windowId);
    MinimizeApp::ExecuteMinimizeAll();
    return WMError::WM_OK;
}

WMError WindowController::SetWindowBackgroundBlur(uint32_t windowId, WindowBlurLevel dstLevel)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }

    WLOGFI("WindowEffect WindowController SetWindowBackgroundBlur level: %{public}u", dstLevel);
    node->SetWindowBackgroundBlur(dstLevel);
    FlushWindowInfo(windowId);
    return WMError::WM_OK;
}

void WindowController::NotifyDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type)
{
    WLOGFD("DisplayStateChangeType:%{public}u", type);
    switch (type) {
        case DisplayStateChangeType::BEFORE_SUSPEND: {
            isScreenLocked_ = true;
            windowRoot_->ProcessWindowStateChange(WindowState::STATE_FROZEN, WindowStateChangeReason::KEYGUARD);
            break;
        }
        case DisplayStateChangeType::BEFORE_UNLOCK: {
            windowRoot_->ProcessWindowStateChange(WindowState::STATE_UNFROZEN, WindowStateChangeReason::KEYGUARD);
            isScreenLocked_ = false;
            break;
        }
        case DisplayStateChangeType::CREATE: {
            windowRoot_->ProcessDisplayCreate(defaultDisplayId, displayInfo, displayInfoMap);
            break;
        }
        case DisplayStateChangeType::DESTROY: {
            windowRoot_->ProcessDisplayDestroy(defaultDisplayId, displayInfo, displayInfoMap);
            break;
        }
        case DisplayStateChangeType::SIZE_CHANGE:
        case DisplayStateChangeType::UPDATE_ROTATION:
        case DisplayStateChangeType::VIRTUAL_PIXEL_RATIO_CHANGE: {
            ProcessDisplayChange(defaultDisplayId, displayInfo, displayInfoMap, type);
            break;
        }
        default: {
            WLOGFE("unknown DisplayStateChangeType:%{public}u", type);
            return;
        }
    }
}

void WindowController::ProcessSystemBarChange(const sptr<DisplayInfo>& displayInfo)
{
    DisplayId displayId = displayInfo->GetDisplayId();
    auto width = static_cast<uint32_t>(displayInfo->GetWidth());
    auto height = static_cast<uint32_t>(displayInfo->GetHeight());
    const auto& statusBarNode = windowRoot_->GetWindowNode(sysBarWinId_[WindowType::WINDOW_TYPE_STATUS_BAR]);
    if (statusBarNode != nullptr && statusBarNode->GetDisplayId() == displayId) {
        auto statusBarHeight = statusBarNode->GetWindowRect().height_;
        Rect newRect = { 0, 0, width, statusBarHeight };
        ResizeRect(sysBarWinId_[WindowType::WINDOW_TYPE_STATUS_BAR], newRect, WindowSizeChangeReason::DRAG);
    }
    const auto& naviBarNode = windowRoot_->GetWindowNode(sysBarWinId_[WindowType::WINDOW_TYPE_NAVIGATION_BAR]);
    if (naviBarNode != nullptr && naviBarNode->GetDisplayId() == displayId) {
        auto naviBarHeight = naviBarNode->GetWindowRect().height_;
        Rect newRect = { 0, static_cast<int32_t>(height - naviBarHeight), width, naviBarHeight };
        ResizeRect(sysBarWinId_[WindowType::WINDOW_TYPE_NAVIGATION_BAR], newRect, WindowSizeChangeReason::DRAG);
    }
}

void WindowController::ProcessDisplayChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type)
{
    if (displayInfo == nullptr) {
        WLOGFE("get display failed");
        return;
    }
    auto windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(displayInfo->GetDisplayId());
    if (windowNodeContainer != nullptr) {
        windowNodeContainer->BeforeProcessWindowAvoidAreaChangeWhenDisplayChange();
    }
    DisplayId displayId = displayInfo->GetDisplayId();
    switch (type) {
        case DisplayStateChangeType::SIZE_CHANGE:
        case DisplayStateChangeType::UPDATE_ROTATION:
            ProcessSystemBarChange(displayInfo);
            [[fallthrough]];
        case DisplayStateChangeType::VIRTUAL_PIXEL_RATIO_CHANGE: {
            windowRoot_->ProcessDisplayChange(defaultDisplayId, displayInfo, displayInfoMap, type);
            break;
        }
        default: {
            WLOGFE("unknown DisplayStateChangeType:%{public}u", type);
            return;
        }
    }
    FlushWindowInfoWithDisplayId(displayId);
    if (windowNodeContainer != nullptr) {
        windowNodeContainer->ProcessWindowAvoidAreaChangeWhenDisplayChange();
    }
    WLOGFI("Finish ProcessDisplayChange");
}

void WindowController::StopBootAnimationIfNeed(WindowType type) const
{
    if (WindowType::WINDOW_TYPE_DESKTOP == type) {
        WLOGFI("stop boot animation");
        system::SetParameter("persist.window.boot.inited", "1");
        RecordBootAnimationEvent();
    }
}

void WindowController::RecordBootAnimationEvent() const
{
    uint64_t time = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::steady_clock::now()).
        time_since_epoch().count();
    WLOGFI("boot animation done duration(s): %{public}" PRIu64"", static_cast<uint64_t>(time));
    std::ostringstream os;
    os << "boot animation done duration(s): " << time <<";";
    int32_t ret = OHOS::HiviewDFX::HiSysEvent::Write(
        OHOS::HiviewDFX::HiSysEvent::Domain::WINDOW_MANAGER,
        "WINDOW_BOOT_ANIMATION_DONE",
        OHOS::HiviewDFX::HiSysEvent::EventType::BEHAVIOR,
        "MSG", os.str());
    if (ret != 0) {
        WLOGFE("Write HiSysEvent error, ret:%{public}d", ret);
    }
}

WMError WindowController::SetWindowType(uint32_t windowId, WindowType type)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto property = node->GetWindowProperty();
    property->SetWindowType(type);
    UpdateWindowAnimation(node);
    WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_TYPE);
    if (res != WMError::WM_OK) {
        return res;
    }
    FlushWindowInfo(windowId);
    WLOGFI("SetWindowType end");
    return res;
}

WMError WindowController::SetWindowFlags(uint32_t windowId, uint32_t flags)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    auto property = node->GetWindowProperty();
    uint32_t oldFlags = property->GetWindowFlags();
    property->SetWindowFlags(flags);
    // only forbid_split_move flag change, just set property
    if ((oldFlags ^ flags) == static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_FORBID_SPLIT_MOVE)) {
        return WMError::WM_OK;
    }
    WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_FLAGS);
    if (res != WMError::WM_OK) {
        return res;
    }
    FlushWindowInfo(windowId);
    WLOGFI("SetWindowFlags end");
    return res;
}

WMError WindowController::SetSystemBarProperty(uint32_t windowId, WindowType type, const SystemBarProperty& property)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    node->SetSystemBarProperty(type, property);
    WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_OTHER_PROPS);
    if (res != WMError::WM_OK) {
        return res;
    }
    FlushWindowInfo(windowId);
    WLOGFI("SetSystemBarProperty end");
    return res;
}

void WindowController::NotifySystemBarTints()
{
    windowRoot_->NotifySystemBarTints();
}

WMError WindowController::SetWindowAnimationController(const sptr<RSIWindowAnimationController>& controller)
{
    return RemoteAnimation::SetWindowAnimationController(controller);
}

AvoidArea WindowController::GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType) const
{
    return windowRoot_->GetAvoidAreaByType(windowId, avoidAreaType);
}

WMError WindowController::ProcessPointDown(uint32_t windowId, bool isStartDrag)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFW("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (!node->currentVisibility_) {
        WLOGFE("this window is not visible and not in window tree, windowId: %{public}u", windowId);
        return WMError::WM_ERROR_INVALID_OPERATION;
    }

    NotifyTouchOutside(node);

    if (isStartDrag) {
        WMError res = windowRoot_->UpdateSizeChangeReason(windowId, WindowSizeChangeReason::DRAG_START);
        return res;
    }

    WLOGFI("process point down, windowId: %{public}u", windowId);
    WMError zOrderRes = windowRoot_->RaiseZOrderForAppWindow(node);
    WMError focusRes = windowRoot_->RequestFocus(windowId);
    windowRoot_->RequestActiveWindow(windowId);
    if (zOrderRes == WMError::WM_OK || focusRes == WMError::WM_OK) {
        FlushWindowInfo(windowId);
        WLOGFI("ProcessPointDown end");
        return WMError::WM_OK;
    }
    windowRoot_->FocusFaultDetection();
    return WMError::WM_ERROR_INVALID_OPERATION;
}

WMError WindowController::ProcessPointUp(uint32_t windowId)
{
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFW("could not find window");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (node->GetWindowType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
        DisplayId displayId = node->GetDisplayId();
        if (windowRoot_->IsDockSliceInExitSplitModeArea(displayId)) {
            windowRoot_->ExitSplitMode(displayId);
        } else {
            auto property = node->GetWindowProperty();
            node->SetWindowSizeChangeReason(WindowSizeChangeReason::DRAG_END);
            property->SetRequestRect(property->GetWindowRect());
            WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_RECT);
            if (res == WMError::WM_OK) {
                FlushWindowInfo(windowId);
            }
        }
    }
    WMError res = windowRoot_->UpdateSizeChangeReason(windowId, WindowSizeChangeReason::DRAG_END);
    if (res != WMError::WM_OK) {
        return res;
    }
    return WMError::WM_OK;
}

void WindowController::MinimizeAllAppWindows(DisplayId displayId)
{
    windowRoot_->MinimizeAllAppWindows(displayId);
    if (RemoteAnimation::NotifyAnimationByHome(windowRoot_) != WMError::WM_OK) {
        MinimizeApp::ExecuteMinimizeAll();
    }
}

WMError WindowController::ToggleShownStateForAllAppWindows()
{
    if (isScreenLocked_) {
        return WMError::WM_DO_NOTHING;
    }
    return windowRoot_->ToggleShownStateForAllAppWindows();
}

WMError WindowController::GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId)
{
    return windowRoot_->GetTopWindowId(mainWinId, topWinId);
}

void WindowController::FlushWindowInfo(uint32_t windowId)
{
    WLOGFI("FlushWindowInfo");
    RSTransaction::FlushImplicitTransaction();
    inputWindowMonitor_->UpdateInputWindow(windowId);
}

void WindowController::FlushWindowInfoWithDisplayId(DisplayId displayId)
{
    WLOGFI("FlushWindowInfoWithDisplayId");
    RSTransaction::FlushImplicitTransaction();
    inputWindowMonitor_->UpdateInputWindowByDisplayId(displayId);
}

void WindowController::UpdateWindowAnimation(const sptr<WindowNode>& node)
{
    if (node == nullptr || (node->leashWinSurfaceNode_ == nullptr && node->surfaceNode_ == nullptr)) {
        WLOGFE("windowNode or surfaceNode is nullptr");
        return;
    }

    uint32_t animationFlag = node->GetWindowProperty()->GetAnimationFlag();
    uint32_t windowId = node->GetWindowProperty()->GetWindowId();
    WLOGFI("windowId: %{public}u, animationFlag: %{public}u", windowId, animationFlag);
    if (animationFlag == static_cast<uint32_t>(WindowAnimation::DEFAULT)) {
        // set default transition effect for window: scale from 1.0 to 0.7, fade from 1.0 to 0.0
        static const auto effect = RSTransitionEffect::Create()->Scale(Vector3f(0.7f, 0.7f, 0.0f))->Opacity(0.0f);
        if (node->leashWinSurfaceNode_) {
            node->leashWinSurfaceNode_->SetTransitionEffect(effect);
        }
        if (node->surfaceNode_) {
            node->surfaceNode_->SetTransitionEffect(effect);
        }
    } else {
        if (node->leashWinSurfaceNode_) {
            node->leashWinSurfaceNode_->SetTransitionEffect(nullptr);
        }
        if (node->surfaceNode_) {
            node->surfaceNode_->SetTransitionEffect(nullptr);
        }
    }
}

WMError WindowController::SetWindowLayoutMode(WindowLayoutMode mode)
{
    WMError res = WMError::WM_OK;
    auto displayIds = windowRoot_->GetAllDisplayIds();
    for (auto displayId : displayIds) {
        WMError res = windowRoot_->SetWindowLayoutMode(displayId, mode);
        if (res != WMError::WM_OK) {
            return res;
        }
        FlushWindowInfoWithDisplayId(displayId);
    }
    MinimizeApp::ExecuteMinimizeAll();
    return res;
}

WMError WindowController::UpdateProperty(sptr<WindowProperty>& property, PropertyChangeAction action)
{
    if (property == nullptr) {
        WLOGFE("property is invalid");
        return WMError::WM_ERROR_NULLPTR;
    }
    uint32_t windowId = property->GetWindowId();
    auto node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("window is invalid");
        return WMError::WM_ERROR_NULLPTR;
    }
    WLOGFI("window: [%{public}s, %{public}u] update property for action: %{public}u", node->GetWindowName().c_str(),
        node->GetWindowId(), static_cast<uint32_t>(action));
    WMError ret = WMError::WM_OK;
    switch (action) {
        case PropertyChangeAction::ACTION_UPDATE_RECT: {
            node->SetDecoStatus(property->GetDecoStatus());
            node->SetOriginRect(property->GetOriginRect());
            node->SetDragType(property->GetDragType());
            ret = ResizeRect(windowId, property->GetRequestRect(), property->GetWindowSizeChangeReason());
            if (node->GetWindowMode() == WindowMode::WINDOW_MODE_FLOATING && ret == WMError::WM_OK &&
                callingWindowId_ == windowId && !WindowHelper::IsEmptyRect(callingWindowRestoringRect_)) {
                if (property->GetWindowSizeChangeReason() != WindowSizeChangeReason::MOVE) {
                    callingWindowId_ = 0u;
                    callingWindowRestoringRect_ = { 0, 0, 0, 0 };
                } else {
                    auto windowRect = node->GetWindowRect();
                    callingWindowRestoringRect_.posX_ = windowRect.posX_;
                    callingWindowRestoringRect_.posY_ = windowRect.posY_;
                }
            }
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_MODE: {
            ret = SetWindowMode(windowId, property->GetWindowMode());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_FLAGS: {
            ret = SetWindowFlags(windowId, property->GetWindowFlags());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_OTHER_PROPS: {
            auto& props = property->GetSystemBarProperty();
            for (auto& iter : props) {
                SetSystemBarProperty(windowId, iter.first, iter.second);
            }
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_FOCUSABLE: {
            node->SetFocusable(property->GetFocusable());
            windowRoot_->UpdateFocusableProperty(windowId);
            FlushWindowInfo(windowId);
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_TOUCHABLE: {
            node->SetTouchable(property->GetTouchable());
            FlushWindowInfo(windowId);
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_CALLING_WINDOW: {
            node->SetCallingWindow(property->GetCallingWindow());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_ORIENTATION: {
            node->SetRequestedOrientation(property->GetRequestedOrientation());
            if (WindowHelper::IsRotatableWindow(node->GetWindowType(), node->GetWindowMode())) {
                DisplayManagerServiceInner::GetInstance().
                    SetOrientationFromWindow(node->GetDisplayId(), property->GetRequestedOrientation());
            }
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_TURN_SCREEN_ON: {
            node->SetTurnScreenOn(property->IsTurnScreenOn());
            HandleTurnScreenOn(node);
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_KEEP_SCREEN_ON: {
            node->SetKeepScreenOn(property->IsKeepScreenOn());
            windowRoot_->HandleKeepScreenOn(node->GetWindowId(), node->IsKeepScreenOn());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_SET_BRIGHTNESS: {
            node->SetBrightness(property->GetBrightness());
            windowRoot_->SetBrightness(node->GetWindowId(), node->GetBrightness());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_MODE_SUPPORT_INFO: {
            node->SetModeSupportInfo(property->GetModeSupportInfo());
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_TOUCH_HOT_AREA: {
            std::vector<Rect> rects;
            property->GetTouchHotAreas(rects);
            return UpdateTouchHotAreas(node, rects);
        }
        case PropertyChangeAction::ACTION_UPDATE_ANIMATION_FLAG: {
            node->GetWindowProperty()->SetAnimationFlag(property->GetAnimationFlag());
            UpdateWindowAnimation(node);
            break;
        }
        case PropertyChangeAction::ACTION_UPDATE_TRANSFORM_PROPERTY: {
            node->SetTransform(property->GetTransform());
            node->SetWindowSizeChangeReason(WindowSizeChangeReason::TRANSFORM);
            ret = UpdateTransform(windowId);
            break;
        }
        default:
            break;
    }
    if (ret == WMError::WM_OK) {
        NotifyWindowPropertyChanged(node);
    }
    return ret;
}

void WindowController::NotifyWindowPropertyChanged(const sptr<WindowNode>& node)
{
    auto windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (windowNodeContainer == nullptr) {
        WLOGFE("windowNodeContainer is null");
        return;
    }
    windowNodeContainer->NotifyAccessibilityWindowInfo(node, WindowUpdateType::WINDOW_UPDATE_PROPERTY);
}

WMError WindowController::GetModeChangeHotZones(DisplayId displayId,
    ModeChangeHotZones& hotZones, const ModeChangeHotZonesConfig& config)
{
    return windowRoot_->GetModeChangeHotZones(displayId, hotZones, config);
}

WMError WindowController::UpdateTouchHotAreas(const sptr<WindowNode>& node, const std::vector<Rect>& rects)
{
    std::ostringstream oss;
    int index = 0;
    for (const auto& rect : rects) {
        oss << "[ " << rect.posX_ << ", " << rect.posY_ << ", " << rect.width_ << ", " << rect.height_ << " ]";
        index++;
        if (index < static_cast<int32_t>(rects.size())) {
            oss <<", ";
        }
    }
    WLOGFI("windowId: %{public}u, size: %{public}d, rects: %{public}s",
        node->GetWindowId(), static_cast<int32_t>(rects.size()), oss.str().c_str());
    if (rects.size() > TOUCH_HOT_AREA_MAX_NUM) {
        WLOGFE("the number of touch hot areas exceeds the maximum");
        return WMError::WM_ERROR_INVALID_PARAM;
    }

    std::vector<Rect> hotAreas;
    if (rects.empty()) {
        hotAreas.emplace_back(node->GetFullWindowHotArea());
    } else {
        Rect windowRect = node->GetWindowRect();
        if (!WindowHelper::CalculateTouchHotAreas(windowRect, rects, hotAreas)) {
            WLOGFE("the requested touch hot areas are incorrect");
            return WMError::WM_ERROR_INVALID_PARAM;
        }
    }
    node->GetWindowProperty()->SetTouchHotAreas(rects);
    node->SetTouchHotAreas(hotAreas);
    FlushWindowInfo(node->GetWindowId());
    return WMError::WM_OK;
}

WMError WindowController::UpdateTransform(uint32_t windowId)
{
    WMError res = windowRoot_->UpdateWindowNode(windowId, WindowUpdateReason::UPDATE_TRANSFORM);
    if (res != WMError::WM_OK) {
        return res;
    }
    FlushWindowInfo(windowId);
    return WMError::WM_OK;
}

void WindowController::NotifyTouchOutside(const sptr<WindowNode>& node)
{
    auto windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(node->GetDisplayId());
    if (windowNodeContainer == nullptr) {
        WLOGFE("window node container is null");
        return;
    }

    std::vector<sptr<WindowNode>> windowNodes;
    windowNodeContainer->TraverseContainer(windowNodes);
    uint32_t skipNodeId = GetEmbedNodeId(windowNodes, node);
    for (const auto& windowNode : windowNodes) {
        if (windowNode == nullptr || windowNode->GetWindowToken() == nullptr ||
            windowNode->GetWindowId() == skipNodeId ||
            windowNode->GetWindowId() == node->GetWindowId()) {
            WLOGFD("continue %{public}s", windowNode == nullptr ? "nullptr" : windowNode->GetWindowName().c_str());
            continue;
        }
        WLOGFD("notify %{public}s id %{public}d", windowNode->GetWindowName().c_str(), windowNode->GetWindowId());
        windowNode->GetWindowToken()->NotifyTouchOutside();
    }
}

uint32_t WindowController::GetEmbedNodeId(const std::vector<sptr<WindowNode>>& windowNodes,
    const sptr<WindowNode>& node)
{
    if (node->GetWindowType() != WindowType::WINDOW_TYPE_APP_COMPONENT) {
        return 0;
    }

    Rect nodeRect = node->GetWindowRect();
    bool isSkip = true;
    for (auto& windowNode : windowNodes) {
        if (windowNode == nullptr) {
            continue;
        }
        if (windowNode->GetWindowId() == node->GetWindowId()) {
            isSkip = false;
            continue;
        }
        if (isSkip) {
            continue;
        }
        if (nodeRect.IsInsideOf(windowNode->GetWindowRect())) {
            WLOGI("TouchOutside window type is component %{public}s windowNode %{public}d",
                windowNode->GetWindowName().c_str(), windowNode->GetWindowId());
            return windowNode->GetWindowId();
        }
    }
    return 0;
}

void WindowController::MinimizeWindowsByLauncher(std::vector<uint32_t>& windowIds, bool isAnimated,
    sptr<RSIWindowAnimationFinishedCallback>& finishCallback)
{
    windowRoot_->MinimizeTargetWindows(windowIds);
    auto func = []() {
        MinimizeApp::ExecuteMinimizeTargetReason(MinimizeReason::GESTURE_ANIMATION);
    };
    if (!isAnimated) {
        func();
    } else {
        finishCallback = new(std::nothrow) RSWindowAnimationFinishedCallback(func);
        if (finishCallback == nullptr) {
            WLOGFE("New RSIWindowAnimationFinishedCallback failed");
            func();
            return;
        }
    }
}

Orientation WindowController::GetWindowPreferredOrientation(DisplayId displayId)
{
    sptr<WindowNodeContainer> windowNodeContainer = windowRoot_->GetOrCreateWindowNodeContainer(displayId);
    if (windowNodeContainer != nullptr) {
        return windowNodeContainer->GetWindowPreferredOrientation();
    }
    return Orientation::UNSPECIFIED;
}

void WindowController::OnScreenshot(DisplayId displayId)
{
    sptr<WindowNode> windowNode;
    WMError res = GetFocusWindowNode(displayId, windowNode);
    if (res != WMError::WM_OK) {
        return;
    }
    auto windowToken = windowNode->GetWindowToken();
    if (windowToken == nullptr) {
        WLOGFE("notify screenshot failed: window token is null.");
        return;
    }
    windowToken->NotifyScreenshot();
}
} // namespace OHOS
} // namespace Rosen
