/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "starting_window.h"
#include <ability_manager_client.h>
#include <display_manager_service_inner.h>
#include <hitrace_meter.h>
#include <transaction/rs_transaction.h>
#include "remote_animation.h"
#include "window_helper.h"
#include "window_manager_hilog.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "StartingWindow"};
    const char DISABLE_WINDOW_ANIMATION_PATH[] = "/etc/disable_window_animation";
}

std::recursive_mutex StartingWindow::mutex_;
WindowMode StartingWindow::defaultMode_ = WindowMode::WINDOW_MODE_FULLSCREEN;

bool StartingWindow::NeedToStopStartingWindow(WindowMode winMode, uint32_t modeSupportInfo,
    const sptr<WindowTransitionInfo>& info)
{
    if (!WindowHelper::IsMainWindow(info->GetWindowType())) {
        return false;
    }

    if ((!WindowHelper::IsWindowModeSupported(modeSupportInfo, winMode)) ||
        (WindowHelper::IsOnlySupportSplitAndShowWhenLocked(info->GetShowFlagWhenLocked(), modeSupportInfo))) {
        WLOGFE("window mode is not be supported or not support floating mode in tile, cancel starting window");
        return true;
    }
    return false;
}

sptr<WindowNode> StartingWindow::CreateWindowNode(const sptr<WindowTransitionInfo>& info, uint32_t winId)
{
    sptr<WindowProperty> property = new(std::nothrow) WindowProperty();
    if (property == nullptr || info == nullptr) {
        return nullptr;
    }

    property->SetRequestRect(info->GetWindowRect());
    if (WindowHelper::IsValidWindowMode(info->GetWindowMode())) {
        property->SetWindowMode(info->GetWindowMode());
    } else {
        property->SetWindowMode(defaultMode_);
    }

    property->SetDisplayId(info->GetDisplayId());
    property->SetWindowType(info->GetWindowType());
    property->AddWindowFlag(WindowFlag::WINDOW_FLAG_NEED_AVOID);
    if (info->GetShowFlagWhenLocked()) {
        property->AddWindowFlag(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED);
    }
    property->SetWindowId(winId);
    sptr<WindowNode> node = new(std::nothrow) WindowNode(property);
    if (node == nullptr) {
        return nullptr;
    }
    node->abilityToken_ = info->GetAbilityToken();
    node->SetWindowSizeLimits(info->GetWindowSizeLimits());

    uint32_t modeSupportInfo = 0;
    WindowHelper::ConvertSupportModesToSupportInfo(modeSupportInfo, info->GetWindowSupportModes());
    node->SetModeSupportInfo(modeSupportInfo);

    if (CreateLeashAndStartingSurfaceNode(node) != WMError::WM_OK) {
        return nullptr;
    }
    return node;
}

WMError StartingWindow::CreateLeashAndStartingSurfaceNode(sptr<WindowNode>& node)
{
    struct RSSurfaceNodeConfig rsSurfaceNodeConfig;
    rsSurfaceNodeConfig.SurfaceNodeName = "leashWindow" + std::to_string(node->GetWindowId());
    node->leashWinSurfaceNode_ = RSSurfaceNode::Create(rsSurfaceNodeConfig);
    if (node->leashWinSurfaceNode_ == nullptr) {
        WLOGFE("create leashWinSurfaceNode failed");
        return WMError::WM_ERROR_NULLPTR;
    }

    rsSurfaceNodeConfig.SurfaceNodeName = "startingWindow" + std::to_string(node->GetWindowId());
    node->startingWinSurfaceNode_ = RSSurfaceNode::Create(rsSurfaceNodeConfig);
    if (node->startingWinSurfaceNode_ == nullptr) {
        WLOGFE("create startingWinSurfaceNode failed");
        node->leashWinSurfaceNode_ = nullptr;
        return WMError::WM_ERROR_NULLPTR;
    }
    WLOGFI("Create leashWinSurfaceNode and startingWinSurfaceNode success with id:%{public}u!", node->GetWindowId());
    return WMError::WM_OK;
}

void StartingWindow::DrawStartingWindow(sptr<WindowNode>& node,
    sptr<Media::PixelMap> pixelMap, uint32_t bkgColor, bool isColdStart)
{
    // using snapshot to support hot start since node destroy when hide
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:DrawStartingWindow(%u)", node->GetWindowId());
    Rect rect = node->GetWindowRect();
    if (RemoteAnimation::CheckAnimationController() && node->leashWinSurfaceNode_) {
        node->leashWinSurfaceNode_->SetBounds(rect.posX_, rect.posY_, -1, -1);
    }
    if (!isColdStart) {
        return;
    }
    if (node->startingWinSurfaceNode_ == nullptr) {
        WLOGFE("no starting Window SurfaceNode!");
        return;
    }
    if (pixelMap == nullptr) {
        SurfaceDraw::DrawColor(node->startingWinSurfaceNode_, rect.width_, rect.height_, bkgColor);
        return;
    }
    SurfaceDraw::DrawImageRect(node->startingWinSurfaceNode_, rect, pixelMap, bkgColor);
}

void StartingWindow::HandleClientWindowCreate(sptr<WindowNode>& node, sptr<IWindow>& window,
    uint32_t& windowId, const std::shared_ptr<RSSurfaceNode>& surfaceNode, sptr<WindowProperty>& property,
    int32_t pid, int32_t uid)
{
    node->surfaceNode_ = surfaceNode;
    node->SetWindowToken(window);
    node->SetCallingPid(pid);
    node->SetCallingUid(uid);
    windowId = node->GetWindowId();
    WLOGFI("after set Id:%{public}u, requestRect:[%{public}d, %{public}d, %{public}u, %{public}u]",
        node->GetWindowId(), node->GetRequestRect().posX_, node->GetRequestRect().posY_,
        node->GetRequestRect().width_, node->GetRequestRect().height_);

    // Register FirstFrame Callback to rs, replace startwin
    wptr<WindowNode> weak = node;
    auto firstFrameCompleteCallback = [weak]() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        FinishAsyncTraceArgs(HITRACE_TAG_WINDOW_MANAGER, static_cast<int32_t>(TraceTaskId::STARTING_WINDOW),
            "wms:async:ShowStartingWindow");
        auto weakNode = weak.promote();
        if (weakNode == nullptr || weakNode->leashWinSurfaceNode_ == nullptr) {
            WLOGFE("windowNode or leashWinSurfaceNode_ is nullptr");
            return;
        }
        WLOGFI("StartingWindow::Replace surfaceNode, id: %{public}u", weakNode->GetWindowId());
        weakNode->leashWinSurfaceNode_->RemoveChild(weakNode->startingWinSurfaceNode_);
        weakNode->leashWinSurfaceNode_->AddChild(weakNode->surfaceNode_, -1);
        weakNode->startingWinSurfaceNode_ = nullptr;
        AAFwk::AbilityManagerClient::GetInstance()->CompleteFirstFrameDrawing(weakNode->abilityToken_);
        RSTransaction::FlushImplicitTransaction();
    };
    node->surfaceNode_->SetBufferAvailableCallback(firstFrameCompleteCallback);
    RSTransaction::FlushImplicitTransaction();
}

void StartingWindow::ReleaseStartWinSurfaceNode(sptr<WindowNode>& node)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!node->leashWinSurfaceNode_) {
        WLOGFI("cannot release leashwindow since leash is null, id:%{public}u", node->GetWindowId());
        return;
    }
    auto leashName = node->leashWinSurfaceNode_->GetName();
    node->leashWinSurfaceNode_->RemoveChild(node->startingWinSurfaceNode_);
    node->leashWinSurfaceNode_->RemoveChild(node->surfaceNode_);
    node->leashWinSurfaceNode_ = nullptr;
    node->startingWinSurfaceNode_ = nullptr;
    WLOGFI("Release startwindow surfaceNode end id: %{public}u, [leashWinSurface]: use_count: %{public}ld, \
        [startWinSurface]: use_count: %{public}ld ", node->GetWindowId(),
        node->leashWinSurfaceNode_.use_count(), node->startingWinSurfaceNode_.use_count());
    RSTransaction::FlushImplicitTransaction();
}

void StartingWindow::UpdateRSTree(sptr<WindowNode>& node)
{
    auto updateRSTreeFunc = [&]() {
        auto& dms = DisplayManagerServiceInner::GetInstance();
        DisplayId displayId = node->GetDisplayId();
        if (!node->surfaceNode_) { // cold start
            if (!WindowHelper::IsMainWindow(node->GetWindowType())) {
                WLOGFE("window id:%{public}d type: %{public}u is not Main Window!",
                    node->GetWindowId(), static_cast<uint32_t>(node->GetWindowType()));
            }
            dms.UpdateRSTree(displayId, node->leashWinSurfaceNode_, true);
            node->leashWinSurfaceNode_->AddChild(node->startingWinSurfaceNode_, -1);
        } else { // hot start
            const auto& displayIdVec = node->GetShowingDisplays();
            for (auto& shownDisplayId : displayIdVec) {
                if (node->leashWinSurfaceNode_) { // to app
                    dms.UpdateRSTree(shownDisplayId, node->leashWinSurfaceNode_, true);
                } else { // to launcher
                    dms.UpdateRSTree(shownDisplayId, node->surfaceNode_, true);
                }
                for (auto& child : node->children_) {
                    if (child->currentVisibility_) {
                        dms.UpdateRSTree(shownDisplayId, child->surfaceNode_, true);
                    }
                }
            }
        }
    };
    static const bool IsWindowAnimationEnabled = access(DISABLE_WINDOW_ANIMATION_PATH, F_OK) == 0 ? false : true;
    if (IsWindowAnimationEnabled && !RemoteAnimation::CheckAnimationController()) {
        // default transition duration: 350ms
        static const RSAnimationTimingProtocol timingProtocol(350);
        // default transition curve: EASE OUT
        static const Rosen::RSAnimationTimingCurve curve = Rosen::RSAnimationTimingCurve::EASE_OUT;
        // add window with transition animation
        RSNode::Animate(timingProtocol, curve, updateRSTreeFunc);
    } else {
        // add or remove window without animation
        updateRSTreeFunc();
    }
}
void StartingWindow::SetDefaultWindowMode(WindowMode defaultMode)
{
    defaultMode_ = defaultMode;
}
} // Rosen
} // OHOS
