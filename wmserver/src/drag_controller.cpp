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
#include "drag_controller.h"

#include <vector>

#include "display.h"
#include "vsync_station.h"
#include "wm_common.h"
#include "window_helper.h"
#include "window_inner_manager.h"
#include "window_manager_hilog.h"
#include "window_manager_service.h"
#include "window_node.h"
#include "window_node_container.h"
#include "window_property.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "DragController"};
}

void DragController::UpdateDragInfo(uint32_t windowId)
{
    PointInfo point;
    if (!GetHitPoint(windowId, point)) {
        return;
    }
    sptr<WindowNode> dragNode = windowRoot_->GetWindowNode(windowId);
    if (dragNode == nullptr) {
        return;
    }
    sptr<WindowNode> hitWindowNode = GetHitWindow(dragNode->GetDisplayId(), point);
    if (hitWindowNode == nullptr) {
        WLOGFE("Get point failed %{public}d %{public}d", point.x, point.y);
        return;
    }
    if (hitWindowNode->GetWindowId() == hitWindowId_) {
        hitWindowNode->GetWindowToken()->UpdateWindowDragInfo(point, DragEvent::DRAG_EVENT_MOVE);
        return;
    }
    hitWindowNode->GetWindowToken()->UpdateWindowDragInfo(point, DragEvent::DRAG_EVENT_IN);
    sptr<WindowNode> oldHitWindow = windowRoot_->GetWindowNode(hitWindowId_);
    if (oldHitWindow != nullptr) {
        oldHitWindow->GetWindowToken()->UpdateWindowDragInfo(point, DragEvent::DRAG_EVENT_OUT);
    }
    hitWindowId_ = hitWindowNode->GetWindowId();
}

void DragController::StartDrag(uint32_t windowId)
{
    PointInfo point;
    if (!GetHitPoint(windowId, point)) {
        WLOGFE("Get hit point failed");
        return;
    }
    sptr<WindowNode> dragNode = windowRoot_->GetWindowNode(windowId);
    if (dragNode == nullptr) {
        return;
    }
    sptr<WindowNode> hitWindow = GetHitWindow(dragNode->GetDisplayId(), point);
    if (hitWindow == nullptr) {
        WLOGFE("Get point failed %{public}d %{public}d", point.x, point.y);
        return;
    }
    hitWindow->GetWindowToken()->UpdateWindowDragInfo(point, DragEvent::DRAG_EVENT_IN);
    hitWindowId_ = windowId;
    WLOGFI("start Drag");
}

void DragController::FinishDrag(uint32_t windowId)
{
    sptr<WindowNode> node = windowRoot_->GetWindowNode(windowId);
    if (node == nullptr) {
        WLOGFE("get node failed");
        return;
    }
    if (node->GetWindowType() != WindowType::WINDOW_TYPE_DRAGGING_EFFECT) {
        return;
    }

    sptr<WindowNode> hitWindow = windowRoot_->GetWindowNode(hitWindowId_);
    if (hitWindow != nullptr) {
        auto property = node->GetWindowProperty();
        PointInfo point = {property->GetWindowRect().posX_ + property->GetHitOffset().x,
            property->GetWindowRect().posY_ + property->GetHitOffset().y};
        hitWindow->GetWindowToken()->UpdateWindowDragInfo(point, DragEvent::DRAG_EVENT_END);
    }
    WLOGFI("end drag");
}

sptr<WindowNode> DragController::GetHitWindow(DisplayId id, PointInfo point)
{
    // Need get display by point
    if (id == DISPLAY_ID_INVALID) {
        WLOGFE("Get invalid display");
        return nullptr;
    }
    sptr<WindowNodeContainer> container = windowRoot_->GetOrCreateWindowNodeContainer(id);
    if (container == nullptr) {
        WLOGFE("get container failed %{public}" PRIu64"", id);
        return nullptr;
    }

    std::vector<sptr<WindowNode>> windowNodes;
    container->TraverseContainer(windowNodes);
    for (auto windowNode : windowNodes) {
        if (windowNode->GetWindowType() >= WindowType::WINDOW_TYPE_PANEL) {
            continue;
        }
        if (WindowHelper::IsPointInTargetRect(point.x, point.y, windowNode->GetWindowRect())) {
            return windowNode;
        }
    }
    return nullptr;
}

bool DragController::GetHitPoint(uint32_t windowId, PointInfo& point)
{
    sptr<WindowNode> windowNode = windowRoot_->GetWindowNode(windowId);
    if (windowNode == nullptr || windowNode->GetWindowType() != WindowType::WINDOW_TYPE_DRAGGING_EFFECT) {
        WLOGFE("Get hit point failed");
        return false;
    }
    sptr<WindowProperty> property = windowNode->GetWindowProperty();
    point.x = property->GetWindowRect().posX_ + property->GetHitOffset().x;
    point.y = property->GetWindowRect().posY_ + property->GetHitOffset().y;
    return true;
}

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::KeyEvent> keyEvent) const
{
    if (keyEvent == nullptr) {
        WLOGFE("KeyEvent is nullptr");
        return;
    }
    uint32_t windowId = static_cast<uint32_t>(keyEvent->GetAgentWindowId());
    WLOGFD("[WMS] Receive keyEvent, windowId: %{public}u", windowId);
    keyEvent->MarkProcessed();
}

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::AxisEvent> axisEvent) const
{
    if (axisEvent == nullptr) {
        WLOGFE("AxisEvent is nullptr");
        return;
    };
    WLOGFD("[WMS] Receive axisEvent, windowId: %{public}u", axisEvent->GetAgentWindowId());
    axisEvent->MarkProcessed();
}

void InputEventListener::OnInputEvent(std::shared_ptr<MMI::PointerEvent> pointerEvent) const
{
    if (pointerEvent == nullptr) {
        WLOGFE("PointerEvent is nullptr");
        return;
    }
    uint32_t windowId = static_cast<uint32_t>(pointerEvent->GetAgentWindowId());
    WLOGFD("[WMS] Receive pointerEvent, windowId: %{public}u", windowId);

    WindowInnerManager::GetInstance().ConsumePointerEvent(pointerEvent);
}

bool MoveDragController::Init()
{
    // create handler for input event
    inputEventHandler_ = std::make_shared<AppExecFwk::EventHandler>(
        AppExecFwk::EventRunner::Create(INNER_WM_INPUT_THREAD_NAME));
    if (inputEventHandler_ == nullptr) {
        return false;
    }
    inputListener_ = std::make_shared<InputEventListener>(InputEventListener());
    MMI::InputManager::GetInstance()->SetWindowInputEventConsumer(inputListener_, inputEventHandler_);
    VsyncStation::GetInstance().SetIsMainHandlerAvailable(false);
    VsyncStation::GetInstance().SetVsyncEventHandler(inputEventHandler_);
    return true;
}

void MoveDragController::Stop()
{
    if (inputEventHandler_ != nullptr) {
        inputEventHandler_.reset();
    }
}

void MoveDragController::HandleReadyToMoveOrDrag(uint32_t windowId, sptr<WindowProperty>& windowProperty,
    sptr<MoveDragProperty>& moveDragProperty)
{
    SetActiveWindowId(windowId);
    SetWindowProperty(windowProperty);
    SetDragProperty(moveDragProperty);
}

void MoveDragController::HandleEndUpMovingOrDragging(uint32_t windowId)
{
    if (activeWindowId_ != windowId) {
        WLOGFE("end up moving or dragging failed, windowId: %{public}u", windowId);
        return;
    }
    ResetMoveOrDragState();
}

void MoveDragController::HandleWindowRemovedOrDestroyed(uint32_t windowId)
{
    if (GetMoveDragProperty() == nullptr) {
        return;
    }
    if (!(GetMoveDragProperty()->startMoveFlag_ || GetMoveDragProperty()->startDragFlag_)) {
        return;
    }
    VsyncStation::GetInstance().RemoveCallback();
    ResetMoveOrDragState();
}

void MoveDragController::ConsumePointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    if (pointerEvent == nullptr) {
        WLOGFE("pointerEvent is nullptr or is handling pointer event");
        return;
    }
    if (pointerEvent->GetPointerAction() == MMI::PointerEvent::POINTER_ACTION_MOVE) {
        moveEvent_ = pointerEvent;
        VsyncStation::GetInstance().RequestVsync(vsyncCallback_);
    } else {
        WLOGFI("[WMS] Dispatch non-move event, action: %{public}d", pointerEvent->GetPointerAction());
        HandlePointerEvent(pointerEvent);
        pointerEvent->MarkProcessed();
    }
}

void MoveDragController::OnReceiveVsync(int64_t timeStamp)
{
    if (moveEvent_ == nullptr) {
        WLOGFE("moveEvent is nullptr");
        return;
    }
    WLOGFD("[OnReceiveVsync] receive move event, action: %{public}d", moveEvent_->GetPointerAction());
    HandlePointerEvent(moveEvent_);
    moveEvent_->MarkProcessed();
}

Rect MoveDragController::GetHotZoneRect()
{
    Rect hotZoneRect;
    const auto& startRectExceptCorner =  moveDragProperty_->startRectExceptCorner_;
    const auto& startRectExceptFrame =  moveDragProperty_->startRectExceptFrame_;
    if ((moveDragProperty_->startPointPosX_ > startRectExceptCorner.posX_ &&
        (moveDragProperty_->startPointPosX_ < startRectExceptCorner.posX_ +
         static_cast<int32_t>(startRectExceptCorner.width_))) &&
        (moveDragProperty_->startPointPosY_ > startRectExceptCorner.posY_ &&
        (moveDragProperty_->startPointPosY_ < startRectExceptCorner.posY_ +
        static_cast<int32_t>(startRectExceptCorner.height_)))) {
        hotZoneRect = startRectExceptFrame; // drag type: left/right/top/bottom
    } else {
        hotZoneRect = startRectExceptCorner; // drag type: left_top/right_top/left_bottom/right_bottom
    }
    return hotZoneRect;
}

void MoveDragController::HandleDragEvent(int32_t posX, int32_t posY, int32_t pointId)
{
    if (moveDragProperty_ == nullptr) {
        return;
    }
    if (!moveDragProperty_->startDragFlag_ || (pointId != moveDragProperty_->startPointerId_)) {
        return;
    }
    const auto& startPointPosX = moveDragProperty_->startPointPosX_;
    const auto& startPointPosY = moveDragProperty_->startPointPosY_;
    const auto& startPointRect = moveDragProperty_->startPointRect_;
    Rect newRect = startPointRect;
    Rect hotZoneRect = GetHotZoneRect();
    int32_t diffX = posX - startPointPosX;
    int32_t diffY = posY - startPointPosY;

    if (startPointPosX <= hotZoneRect.posX_) {
        if (diffX > static_cast<int32_t>(startPointRect.width_)) {
            diffX = static_cast<int32_t>(startPointRect.width_);
        }
        newRect.posX_ += diffX;
        newRect.width_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.width_) - diffX);
    } else if (startPointPosX >= hotZoneRect.posX_ + static_cast<int32_t>(hotZoneRect.width_)) {
        if (diffX < 0 && (-diffX > static_cast<int32_t>(startPointRect.width_))) {
            diffX = -(static_cast<int32_t>(startPointRect.width_));
        }
        newRect.width_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.width_) + diffX);
    }
    if (startPointPosY <= hotZoneRect.posY_) {
        if (diffY > static_cast<int32_t>(startPointRect.height_)) {
            diffY = static_cast<int32_t>(startPointRect.height_);
        }
        newRect.posY_ += diffY;
        newRect.height_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.height_) - diffY);
    } else if (startPointPosY >= hotZoneRect.posY_ + static_cast<int32_t>(hotZoneRect.height_)) {
        if (diffY < 0 && (-diffY > static_cast<int32_t>(startPointRect.height_))) {
            diffY = -(static_cast<int32_t>(startPointRect.height_));
        }
        newRect.height_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.height_) + diffY);
    }
    WLOGFI("[WMS] HandleDragEvent, id: %{public}u, newRect: [%{public}d, %{public}d, %{public}d, %{public}d]",
        windowProperty_->GetWindowId(), newRect.posX_, newRect.posY_, newRect.width_, newRect.height_);
    windowProperty_->SetRequestRect(newRect);
    windowProperty_->SetWindowSizeChangeReason(WindowSizeChangeReason::DRAG);
    windowProperty_->SetDragType(moveDragProperty_->dragType_);
    WindowManagerService::GetInstance().UpdateProperty(windowProperty_, PropertyChangeAction::ACTION_UPDATE_RECT, true);
}

void MoveDragController::HandleMoveEvent(int32_t posX, int32_t posY, int32_t pointId)
{
    if (moveDragProperty_ == nullptr) {
        return;
    }
    if (!moveDragProperty_->startMoveFlag_ || (pointId != moveDragProperty_->startPointerId_)) {
        return;
    }
    int32_t targetX = moveDragProperty_->startPointRect_.posX_ + (posX - moveDragProperty_->startPointPosX_);
    int32_t targetY = moveDragProperty_->startPointRect_.posY_ + (posY - moveDragProperty_->startPointPosY_);

    const Rect& oriRect = windowProperty_->GetRequestRect();
    Rect newRect = { targetX, targetY, oriRect.width_, oriRect.height_ };
    WLOGFI("[WMS] HandleMoveEvent, id: %{public}u, newRect: [%{public}d, %{public}d, %{public}d, %{public}d]",
        windowProperty_->GetWindowId(), newRect.posX_, newRect.posY_, newRect.width_, newRect.height_);
    windowProperty_->SetRequestRect(newRect);
    windowProperty_->SetWindowSizeChangeReason(WindowSizeChangeReason::MOVE);
    WindowManagerService::GetInstance().UpdateProperty(windowProperty_, PropertyChangeAction::ACTION_UPDATE_RECT, true);
}

void MoveDragController::HandlePointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    MMI::PointerEvent::PointerItem pointerItem;
    int32_t pointId = pointerEvent->GetPointerId();
    if (!pointerEvent->GetPointerItem(pointId, pointerItem)) {
        WLOGFE("Point item is invalid");
        return;
    }

    int32_t pointPosX = pointerItem.GetDisplayX();
    int32_t pointPosY = pointerItem.GetDisplayY();
    int32_t action = pointerEvent->GetPointerAction();
    switch (action) {
        // ready to move or drag
        case MMI::PointerEvent::POINTER_ACTION_MOVE: {
            HandleMoveEvent(pointPosX, pointPosY, pointId);
            HandleDragEvent(pointPosX, pointPosY, pointId);
            break;
        }
        // End move or drag
        case MMI::PointerEvent::POINTER_ACTION_UP:
        case MMI::PointerEvent::POINTER_ACTION_BUTTON_UP:
        case MMI::PointerEvent::POINTER_ACTION_CANCEL: {
            WindowManagerService::GetInstance().NotifyWindowClientPointUp(activeWindowId_, pointerEvent);
            break;
        }
        default:
            break;
    }
}

void MoveDragController::SetDragProperty(const sptr<MoveDragProperty>& moveDragProperty)
{
    moveDragProperty_->CopyFrom(moveDragProperty);
}

void MoveDragController::SetWindowProperty(const sptr<WindowProperty>& windowProperty)
{
    windowProperty_->CopyFrom(windowProperty);
}

const sptr<MoveDragProperty>& MoveDragController::GetMoveDragProperty() const
{
    return moveDragProperty_;
}

const sptr<WindowProperty>& MoveDragController::GetWindowProperty() const
{
    return windowProperty_;
}

void MoveDragController::ResetMoveOrDragState()
{
    activeWindowId_ = INVALID_WINDOW_ID;
    auto moveDragProperty = new MoveDragProperty();
    SetDragProperty(moveDragProperty);
}

void MoveDragController::SetActiveWindowId(uint32_t activeWindowId)
{
    activeWindowId_ = activeWindowId;
}

uint32_t MoveDragController::GetActiveWindowId() const
{
    return activeWindowId_;
}
}
}
