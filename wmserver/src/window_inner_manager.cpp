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

#include "window_inner_manager.h"

#include "ability_manager_client.h"
#include "ui_service_mgr_client.h"
#include "window.h"
#include "window_manager_hilog.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowInnerManager"};
}
WM_IMPLEMENT_SINGLE_INSTANCE(WindowInnerManager)

WindowInnerManager::WindowInnerManager() : eventHandler_(nullptr), eventLoop_(nullptr),
    state_(InnerWMRunningState::STATE_NOT_START)
{
}

WindowInnerManager::~WindowInnerManager()
{
    Stop();
}

bool WindowInnerManager::Init()
{
    // create handler for inner command at server
    eventLoop_ = AppExecFwk::EventRunner::Create(INNER_WM_THREAD_NAME);
    if (eventLoop_ == nullptr) {
        return false;
    }
    eventHandler_ = std::make_shared<AppExecFwk::EventHandler>(eventLoop_);
    if (eventHandler_ == nullptr) {
        return false;
    }

    moveDragController_ = new MoveDragController();
    if (!moveDragController_->Init()) {
        WLOGFE("Init window drag controller failed");
        return false;
    }

    WLOGFI("init window inner manager service success.");
    return true;
}

void WindowInnerManager::Start(bool enableRecentholder)
{
    isRecentHolderEnable_ = enableRecentholder;
    if (state_ == InnerWMRunningState::STATE_RUNNING) {
        WLOGFI("window inner manager service has already started.");
    }
    if (!Init()) {
        WLOGFI("failed to init window inner manager service.");
        return;
    }
    state_ = InnerWMRunningState::STATE_RUNNING;
    eventLoop_->Run();

    pid_ = getpid();
    WLOGFI("window inner manager service start success.");
}

void WindowInnerManager::Stop()
{
    WLOGFI("stop window inner manager service.");
    if (eventLoop_ != nullptr) {
        eventLoop_->Stop();
        eventLoop_.reset();
    }
    if (eventHandler_ != nullptr) {
        eventHandler_.reset();
    }
    moveDragController_->Stop();
    state_ = InnerWMRunningState::STATE_NOT_START;
}

void WindowInnerManager::CreateInnerWindow(std::string name, DisplayId displayId, Rect rect,
    WindowType type, WindowMode mode)
{
    bool recentHolderWindowFlag = isRecentHolderEnable_;
    auto task = [name, displayId, rect, mode, type, recentHolderWindowFlag]() {
        switch (type) {
            case WindowType::WINDOW_TYPE_PLACEHOLDER: {
                if (recentHolderWindowFlag) {
                    PlaceHolderWindow::GetInstance().Create(name, displayId, rect, mode);
                }
                break;
            }
            case WindowType::WINDOW_TYPE_DOCK_SLICE: {
                DividerWindow::GetInstance().Create(name, displayId, rect, mode);
                DividerWindow::GetInstance().Update(rect.width_, rect.height_);
                break;
            }
            default:
                break;
        }
    };
    PostTask(task, "CreateInnerWindow");
    return;
}

void WindowInnerManager::DestroyInnerWindow(DisplayId displayId, WindowType type)
{
    bool recentHolderWindowFlag = isRecentHolderEnable_;
    auto task = [type, recentHolderWindowFlag]() {
        switch (type) {
            case WindowType::WINDOW_TYPE_PLACEHOLDER: {
                if (recentHolderWindowFlag) {
                    PlaceHolderWindow::GetInstance().Destroy();
                }
                break;
            }
            case WindowType::WINDOW_TYPE_DOCK_SLICE: {
                DividerWindow::GetInstance().Destroy();
                break;
            }
            default:
                break;
        }
    };
    PostTask(task, "DestroyInnerWindow");
    return;
}

void WindowInnerManager::UpdateInnerWindow(DisplayId displayId, WindowType type, uint32_t width, uint32_t height)
{
    bool recentHolderWindowFlag = isRecentHolderEnable_;
    auto task = [type, width, height, recentHolderWindowFlag]() {
        switch (type) {
            case WindowType::WINDOW_TYPE_PLACEHOLDER: {
                if (recentHolderWindowFlag) {
                    PlaceHolderWindow::GetInstance().Update(width, height);
                }
                break;
            }
            case WindowType::WINDOW_TYPE_DOCK_SLICE: {
                DividerWindow::GetInstance().Update(width, height);
                break;
            }
            default:
                break;
        }
    };
    PostTask(task, "UpdateInnerWindow");
    return;
}

void WindowInnerManager::MinimizeAbility(const wptr<WindowNode> &node, bool isFromUser)
{
    // asynchronously calls the MinimizeAbility of AbilityManager
    auto weakNode = node.promote();
    if (weakNode == nullptr || weakNode->startingWindowShown_) {
        WLOGE("minimize ability failed, because node is nullptr or start window node.");
        return;
    }
    wptr<IRemoteObject> weakToken(weakNode->abilityToken_);
    WLOGFD("minimize window %{public}u,  isfromuser: %{public}d", weakNode->GetWindowId(), isFromUser);
    auto task = [weakToken, isFromUser]() {
        auto token = weakToken.promote();
        if (token == nullptr) {
            WLOGE("minimize ability failed, because window token is nullptr.");
            return;
        }
        AAFwk::AbilityManagerClient::GetInstance()->MinimizeAbility(token, isFromUser);
    };
    PostTask(task, "MinimizeAbility");
}

void WindowInnerManager::PostTask(InnerTask &&task, std::string name, EventPriority priority)
{
    if (eventHandler_ == nullptr) {
        WLOGFE("listener handler is nullptr");
        return;
    }
    bool ret = eventHandler_->PostTask([task]() {
            task();
        }, name, 0, priority); // 0 is task delay time
    if (!ret) {
        WLOGFE("post listener callback task failed.");
        return;
    }
    return;
}

pid_t WindowInnerManager::GetPid()
{
    return pid_;
}

bool WindowInnerManager::NotifyServerReadyToMoveOrDrag(uint32_t windowId, sptr<WindowProperty>& windowProperty,
    sptr<MoveDragProperty>& moveDragProperty)
{
    if (moveDragController_->GetActiveWindowId() != INVALID_WINDOW_ID) {
        WLOGFW("Is already in dragging or moving state, invalid operation");
        return false;
    }
    moveDragController_->HandleReadyToMoveOrDrag(windowId, windowProperty, moveDragProperty);
    WLOGFI("NotifyServerReadyToMoveOrDrag, windowId: %{public}u", windowId);
    return true;
}

void WindowInnerManager::NotifyWindowEndUpMovingOrDragging(uint32_t windowId)
{
    if (moveDragController_->GetActiveWindowId() != windowId) {
        return;
    }
    moveDragController_->HandleEndUpMovingOrDragging(windowId);
    WLOGFI("NotifyWindowEndUpMovingOrDragging, windowId: %{public}u", windowId);
}

void WindowInnerManager::NotifyWindowRemovedOrDestroyed(uint32_t windowId)
{
    if (moveDragController_->GetActiveWindowId() != windowId) {
        return;
    }
    moveDragController_->HandleWindowRemovedOrDestroyed(windowId);
    WLOGFI("NotifyWindowRemovedOrDestroyed, windowId: %{public}u", windowId);
}

void WindowInnerManager::ConsumePointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    uint32_t windowId = static_cast<uint32_t>(pointerEvent->GetAgentWindowId());
    if (moveDragController_->GetActiveWindowId() != windowId ||
        moveDragController_->GetActiveWindowId() == INVALID_WINDOW_ID) {
        WLOGFE("active winId or inputEvent winId is invalid, windowId: %{public}u, activeWinId: %{public}u",
            windowId, moveDragController_->GetActiveWindowId());
        return;
    }
    moveDragController_->ConsumePointerEvent(pointerEvent);
}
} // Rosen
} // OHOS