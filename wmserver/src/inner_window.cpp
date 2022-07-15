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

#include "inner_window.h"

#include "ui_service_mgr_client.h"
#include "window_manager_hilog.h"
#include "surface_draw.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "InnerWindow"};
    const std::string IMAGE_PLACE_HOLDER_PNG_PATH = "/etc/window/resources/bg_place_holder.png";
}
WM_IMPLEMENT_SINGLE_INSTANCE(PlaceHolderWindow)
WM_IMPLEMENT_SINGLE_INSTANCE(DividerWindow)

void PlaceholderWindowListener::OnTouchOutside() const
{
    WLOGFD("place holder touch outside");
    PlaceHolderWindow::GetInstance().Destroy();
}

void PlaceholderWindowListener::OnKeyEvent(std::shared_ptr<MMI::KeyEvent>& keyEvent)
{
    WLOGFD("place holder get key event");
    PlaceHolderWindow::GetInstance().Destroy();
}

void PlaceholderWindowListener::OnPointerInputEvent(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    WLOGFD("place holder get point event");
    PlaceHolderWindow::GetInstance().Destroy();
}

void PlaceholderWindowListener::AfterUnfocused()
{
    WLOGFD("place holder after unfocused");
    PlaceHolderWindow::GetInstance().Destroy();
}

void PlaceHolderWindow::Create(std::string name, DisplayId displyId, Rect rect, WindowMode mode)
{
    WLOGFD("create inner display id: %{public}" PRIu64"", displyId);
    if (window_ != nullptr) {
        WLOGFW("window has created.");
        return;
    }
    sptr<WindowOption> option = new (std::nothrow) WindowOption();
    if (option == nullptr) {
        WLOGFE("window option is nullptr.");
        return;
    }
    option->SetFocusable(false);
    option->SetWindowMode(mode);
    option->SetWindowRect(rect);
    option->SetWindowType(WindowType::WINDOW_TYPE_PLACEHOLDER);
    window_ = Window::Create(name, option);
    if (window_ == nullptr) {
        WLOGFE("window is nullptr.");
        return;
    }
    window_->AddWindowFlag(WindowFlag::WINDOW_FLAG_FORBID_SPLIT_MOVE);
    RegitsterWindowListener();
    if (!OHOS::Rosen::SurfaceDraw::DrawImage(window_->GetSurfaceNode(), rect.width_, rect.height_,
        IMAGE_PLACE_HOLDER_PNG_PATH)) {
        WLOGE("draw surface failed");
        return;
    }
    window_->Show();
    WLOGFD("create palce holder Window end");
}

void PlaceHolderWindow::RegitsterWindowListener()
{
    if (window_ == nullptr) {
        WLOGFE("Window is nullptr, regitster window listener failed.");
        return;
    }
    if (listener_ == nullptr) {
        listener_ = new (std::nothrow) PlaceholderWindowListener();
    }
    window_->RegisterTouchOutsideListener(listener_);
    window_->RegisterInputEventListener(listener_);
    window_->RegisterLifeCycleListener(listener_);
}

void PlaceHolderWindow::UnRegitsterWindowListener()
{
    if (window_ == nullptr || listener_ == nullptr) {
        WLOGFE("Window or listener is nullptr, unregitster window listener failed.");
        return;
    }
    window_->UnregisterTouchOutsideListener(listener_);
    window_->UnregisterInputEventListener(listener_);
    window_->UnregisterLifeCycleListener(listener_);
}

void PlaceHolderWindow::Destroy()
{
    WLOGFI("destroy place holder window begin.");
    if (window_ != nullptr) {
        WLOGFI("destroy place holder window not nullptr.");
        UnRegitsterWindowListener();
        window_->Destroy();
    }
    window_ = nullptr;
    WLOGFI("destroy place holder window end.");
}

DividerWindow::~DividerWindow()
{
    Destroy();
}

void DividerWindow::Create(std::string name, DisplayId displayId, const Rect rect, WindowMode mode)
{
    displayId_ = displayId;
    WLOGFD("create divider dialog display id: %{public}" PRIu64"", displayId_);
    auto dialogCallback = [this](int32_t id, const std::string& event, const std::string& params) {
        WLOGFD("divider dialog window get param: %{public}s", params.c_str());
    };
    Ace::UIServiceMgrClient::GetInstance()->ShowDialog(name, params_, WindowType::WINDOW_TYPE_DOCK_SLICE,
        rect.posX_, rect.posY_, rect.width_, rect.height_, dialogCallback, &dialogId_);
    WLOGFD("create divider dialog window id: %{public}d success", dialogId_);
}

void DividerWindow::Destroy()
{
    if (dialogId_ == IVALID_DIALOG_WINDOW_ID) {
        return;
    }
    WLOGFD("destroy divider dialog window id:: %{public}d.", dialogId_);
    Ace::UIServiceMgrClient::GetInstance()->CancelDialog(dialogId_);
    dialogId_ = IVALID_DIALOG_WINDOW_ID;
}

void DividerWindow::Update(uint32_t width, uint32_t height)
{
    if (dialogId_ == IVALID_DIALOG_WINDOW_ID) {
        return;
    }
    WLOGFD("update divider dialog window dialog id:%{public}d width:%{public}u height:%{public}u.",
        dialogId_, width, height);
    std::stringstream sstream;
    sstream << "{\"width\":" << std::to_string(width) << "," << "\"height\":" << std::to_string(height) << "}";
    // data is json file format
    std::string data = sstream.str();
    Ace::UIServiceMgrClient::GetInstance()->UpdateDialog(dialogId_, data);
}
} // Rosen
} // OHOS