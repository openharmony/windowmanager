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

#include "input_transfer_station.h"
#include <window_manager_hilog.h>

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "InputTransferStation"};
}
WM_IMPLEMENT_SINGLE_INSTANCE(InputTransferStation)

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::KeyEvent> keyEvent) const
{
    if (keyEvent == nullptr) {
        WLOGFE("KeyEvent is nullptr");
        return;
    }
    uint32_t windowId = static_cast<uint32_t>(keyEvent->GetAgentWindowId());
    WLOGFD("Receive keyEvent, windowId: %{public}u", windowId);
    auto channel = InputTransferStation::GetInstance().GetInputChannel(windowId);
    if (channel == nullptr) {
        WLOGFE("WindowInputChannel is nullptr");
        return;
    }
    channel->HandleKeyEvent(keyEvent);
}

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::AxisEvent> axisEvent) const
{
    if (axisEvent == nullptr) {
        WLOGFE("AxisEvent is nullptr");
        return;
    }
    WLOGFD("Receive axisEvent, windowId: %{public}d", axisEvent->GetAgentWindowId());
    axisEvent->MarkProcessed();
}

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::PointerEvent> pointerEvent) const
{
    if (pointerEvent == nullptr) {
        WLOGFE("PointerEvent is nullptr");
        return;
    }
    uint32_t windowId = static_cast<uint32_t>(pointerEvent->GetAgentWindowId());
    WLOGFD("Receive pointerEvent, windowId: %{public}u", windowId);
    auto channel = InputTransferStation::GetInstance().GetInputChannel(windowId);
    if (channel == nullptr) {
        WLOGFE("WindowInputChannel is nullptr");
        pointerEvent->MarkProcessed();
        return;
    }
    channel->HandlePointerEvent(pointerEvent);
}

void InputTransferStation::AddInputWindow(const sptr<Window>& window)
{
    uint32_t windowId = window->GetWindowId();
    WLOGFD("Add input window, windowId: %{public}u", windowId);
    sptr<WindowInputChannel> inputChannel = new WindowInputChannel(window);
    std::lock_guard<std::mutex> lock(mtx_);
    windowInputChannels_.insert(std::make_pair(windowId, inputChannel));
    if (inputListener_ == nullptr) {
        WLOGFD("Init input listener");
        std::shared_ptr<MMI::IInputEventConsumer> listener = std::make_shared<InputEventListener>(InputEventListener());
        MMI::InputManager::GetInstance()->SetWindowInputEventConsumer(listener);
        inputListener_ = listener;
    }
}

void InputTransferStation::RemoveInputWindow(uint32_t windowId)
{
    WLOGFD("Remove input window, windowId: %{public}u", windowId);
    sptr<WindowInputChannel> inputChannel = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto iter = windowInputChannels_.find(windowId);
        if (iter != windowInputChannels_.end()) {
            inputChannel = iter->second;
            windowInputChannels_.erase(windowId);
        }
    }
    if (inputChannel != nullptr) {
        inputChannel->Destroy();
    } else {
        WLOGFE("Can not find windowId: %{public}u", windowId);
    }
}

void InputTransferStation::SetInputListener(
    uint32_t windowId, const std::shared_ptr<MMI::IInputEventConsumer>& listener)
{
    WLOGFD("windowId: %{public}u", windowId);
    auto channel = GetInputChannel(windowId);
    if (channel == nullptr) {
        WLOGFE("WindowInputChannel is nullptr, windowId: %{public}u", windowId);
        return;
    }
    channel->SetInputListener(listener);
}

sptr<WindowInputChannel> InputTransferStation::GetInputChannel(uint32_t windowId)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto iter = windowInputChannels_.find(windowId);
    if (iter == windowInputChannels_.end()) {
        WLOGFE("Can not find channel according to windowId: %{public}u", windowId);
        return nullptr;
    }
    return iter->second;
}
}
}
