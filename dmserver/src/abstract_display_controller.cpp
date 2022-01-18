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

#include "abstract_display_controller.h"

#include <cinttypes>
#include <surface.h>

#include "display_manager_service.h"
#include "window_manager_hilog.h"
#include "window_manager_service.h"

namespace OHOS::Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, 0, "AbstractDisplayController"};
}

AbstractDisplayController::AbstractDisplayController(std::recursive_mutex& mutex)
    : mutex_(mutex), rsInterface_(&(RSInterfaces::GetInstance()))
{
}

AbstractDisplayController::~AbstractDisplayController()
{
    rsInterface_ = nullptr;
    abstractScreenController_ = nullptr;
}

void AbstractDisplayController::Init(sptr<AbstractScreenController> abstractScreenController)
{
    WLOGFD("display controller init");
    displayCount_ = 0;
    abstractScreenController_ = abstractScreenController;
    abstractScreenCallback_ = new AbstractScreenController::AbstractScreenCallback();
    abstractScreenCallback_->onConnected_
        = std::bind(&AbstractDisplayController::OnAbstractScreenConnected, this, std::placeholders::_1);
    abstractScreenCallback_->onDisconnected_
        = std::bind(&AbstractDisplayController::OnAbstractScreenDisconnected, this, std::placeholders::_1);
    abstractScreenCallback_->onChanged_
        = std::bind(&AbstractDisplayController::OnAbstractScreenChanged, this, std::placeholders::_1);
    abstractScreenController->RegisterAbstractScreenCallback(abstractScreenCallback_);

    // TODO: Active the code after "rsDisplayNode_->SetScreenId(rsScreenId)" is provided.
    /*std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (dummyDisplay_ == nullptr) {
        sptr<AbstractDisplay> display = new AbstractDisplay(displayCount_.fetch_add(1), SCREEN_ID_INVALID,
            AbstractDisplay::DEFAULT_WIDTH, AbstractDisplay::DEFAULT_HIGHT, AbstractDisplay::DEFAULT_FRESH_RATE);
        abstractDisplayMap_.insert((std::make_pair(display->GetId(), display)));
        dummyDisplay_ = display;
    }*/
}

ScreenId AbstractDisplayController::GetDefaultScreenId()
{
    if (rsInterface_ == nullptr) {
        return INVALID_SCREEN_ID;
    }
    return rsInterface_->GetDefaultScreenId();
}

RSScreenModeInfo AbstractDisplayController::GetScreenActiveMode(ScreenId id)
{
    RSScreenModeInfo screenModeInfo;
    if (rsInterface_ == nullptr) {
        return screenModeInfo;
    }
    return rsInterface_->GetScreenActiveMode(id);
}

std::shared_ptr<Media::PixelMap> AbstractDisplayController::GetScreenSnapshot(DisplayId displayId, ScreenId screenId)
{
    if (rsInterface_ == nullptr) {
        return nullptr;
    }

    std::shared_ptr<RSDisplayNode> displayNode =
        SingletonContainer::Get<WindowManagerService>().GetDisplayNode(displayId);

    std::shared_ptr<ScreenshotCallback> callback = std::make_shared<ScreenshotCallback>();
    rsInterface_->TakeSurfaceCapture(displayNode, callback);

    int counter = 0;
    while (!callback->IsPixelMapOk()) {
        usleep(10000); // 10000us equals to 10ms
        counter++;
        if (counter >= 200) { // wait for 200 * 10ms = 2s
            WLOGFE("Failed to get pixelmap, timeout");
            return nullptr;
        }
    }
    std::shared_ptr<Media::PixelMap> screenshot = callback->GetPixelMap();

    if (screenshot == nullptr) {
        WLOGFE("Failed to get pixelmap from RS, return nullptr!");
    }
    return screenshot;
}

void AbstractDisplayController::OnAbstractScreenConnected(sptr<AbstractScreen> absScreen)
{
    WLOGI("connect new screen. id:%{public}" PRIu64"", absScreen->dmsId_);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (absScreen->type_ == ScreenType::REAL) {
        sptr<AbstractScreenGroup> group = absScreen->GetGroup();
        if (group == nullptr) {
            WLOGE("the group information of the screen is wrong");
            return;
        }
        if (group->combination_ == ScreenCombination::SCREEN_ALONE) {
            BindAloneScreenLocked(absScreen);
        } else if (group->combination_ == ScreenCombination::SCREEN_MIRROR) {
            AddScreenToMirrorLocked(group, absScreen);
        } else {
            WLOGE("support in future. combination:%{public}ud", group->combination_);
        }
    }
}

void AbstractDisplayController::OnAbstractScreenDisconnected(sptr<AbstractScreen> absScreen)
{
}

void AbstractDisplayController::OnAbstractScreenChanged(sptr<AbstractScreen> absScreen)
{
}

void AbstractDisplayController::BindAloneScreenLocked(sptr<AbstractScreen> realAbsScreen)
{
    ScreenId mainScreenId = abstractScreenController_->GetMainAbstractScreenId();
    if (mainScreenId == SCREEN_ID_INVALID) {
        if (dummyDisplay_ == nullptr) {
            sptr<AbstractScreenInfo> info = realAbsScreen->GetActiveScreenInfo();
            if (info == nullptr) {
                WLOGE("bind alone screen error, cannot get info.");
                return;
            }
            sptr<AbstractDisplay> display = new AbstractDisplay(displayCount_.fetch_add(1),
                realAbsScreen->dmsId_, info->width_, info->height_, info->freshRate_);
            abstractDisplayMap_.insert((std::make_pair(display->GetId(), display)));
            WLOGI("create display for new screen. screen:%{public}" PRIu64", display:%{public}" PRIu64"",
                realAbsScreen->dmsId_, display->GetId());
        } else {
            WLOGI("bind display for new screen. screen:%{public}" PRIu64", display:%{public}" PRIu64"",
                realAbsScreen->dmsId_, dummyDisplay_->GetId());
            dummyDisplay_->BindAbstractScreenId(realAbsScreen->dmsId_);
            dummyDisplay_ = nullptr;
        }
    } else {
        WLOGE("the succedent real screen should be ALONE. %{public}" PRIu64"", realAbsScreen->dmsId_);
    }
}

void AbstractDisplayController::AddScreenToMirrorLocked(sptr<AbstractScreenGroup> group,
    sptr<AbstractScreen> realAbsScreen)
{
    WLOGI("bind screen to mirror. screen:%{public}" PRIu64"", realAbsScreen->dmsId_);
}
} // namespace OHOS::Rosen