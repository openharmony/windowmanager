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

#include "window_manager_hilog.h"

namespace OHOS::Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, 0, "AbstractDisplayController"};
}

AbstractDisplayController::AbstractDisplayController() : rsInterface_(&(RSInterfaces::GetInstance()))
{
    parepareRSScreenManger();
}

AbstractDisplayController::~AbstractDisplayController()
{
    rsInterface_ = nullptr;
}

void AbstractDisplayController::parepareRSScreenManger()
{
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

ScreenId AbstractDisplayController::CreateVirtualScreen(const VirtualDisplayInfo &virtualDisplayInfo,
    sptr<Surface> surface)
{
    if (rsInterface_ == nullptr) {
        return INVALID_SCREEN_ID;
    }
    ScreenId result = rsInterface_->CreateVirtualScreen(virtualDisplayInfo.name_, virtualDisplayInfo.width_,
        virtualDisplayInfo.height_, surface, virtualDisplayInfo.displayIdToMirror_, virtualDisplayInfo.flags_);
    WLOGFI("AbstractDisplayController::CreateVirtualDisplay id: %{public}" PRIu64"", result);
    return result;
}

bool AbstractDisplayController::DestroyVirtualScreen(ScreenId screenId)
{
    if (rsInterface_ == nullptr) {
        return false;
    }
    WLOGFI("AbstractDisplayController::DestroyVirtualScreen");
    rsInterface_->RemoveVirtualScreen(screenId);
    return true;
}

sptr<Media::PixelMap> AbstractDisplayController::GetScreenSnapshot(ScreenId screenId)
{
    if (rsInterface_ == nullptr) {
        return nullptr;
    }

    sptr<Media::PixelMap> screenshot = nullptr;

    return screenshot;
}
} // namespace OHOS::Rosen