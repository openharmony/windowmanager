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

#include "zidl/window_manager_agent_proxy.h"
#include <ipc_types.h>
#include "window_manager_hilog.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, 0, "WindowManagerAgentProxy"};
}

void WindowManagerAgentProxy::UpdateFocusStatus(uint32_t windowId, const sptr<IRemoteObject>& abilityToken,
    WindowType windowType, int32_t displayId, bool focused)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option(MessageOption::TF_ASYNC);
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return;
    }

    if (!data.WriteUint32(windowId)) {
        WLOGFE("Write windowId failed");
        return;
    }

    if (!data.WriteRemoteObject(abilityToken)) {
        WLOGFI("Write abilityToken failed");
    }

    if (!data.WriteUint32(static_cast<uint32_t>(windowType))) {
        WLOGFE("Write windowType failed");
        return;
    }

    if (!data.WriteInt32(displayId)) {
        WLOGFE("Write displayId failed");
        return;
    }

    if (!data.WriteBool(focused)) {
        WLOGFE("Write Focus failed");
        return;
    }

    if (Remote()->SendRequest(TRANS_ID_UPDATE_FOCUS_STATUS, data, reply, option) != ERR_NONE) {
        WLOGFE("SendRequest failed");
    }
}

void WindowManagerAgentProxy::UpdateSystemBarRegionTints(uint64_t displayId, const SystemBarRegionTints& tints)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option(MessageOption::TF_ASYNC);
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return;
    }

    if (!data.WriteUint64(displayId)) {
        WLOGFE("Write displayId failed");
        return;
    }

    auto size = tints.size();
    if (!data.WriteUint32(static_cast<uint32_t>(size))) {
        WLOGFE("Write vector size failed");
        return;
    }
    for (auto it : tints) {
        // write key(type)
        if (!data.WriteUint32(static_cast<uint32_t>(it.type_))) {
            WLOGFE("Write type failed");
            return;
        }
        // write val(prop)
        if (!(data.WriteBool(it.prop_.enable_) && data.WriteUint32(it.prop_.backgroundColor_) &&
            data.WriteUint32(it.prop_.contentColor_))) {
            WLOGFE("Write prop failed");
            return;
        }
        // write val(region)
        if (!(data.WriteInt32(it.region_.posX_) && data.WriteInt32(it.region_.posY_) &&
            data.WriteInt32(it.region_.width_) && data.WriteInt32(it.region_.height_))) {
            WLOGFE("Write region failed");
            return;
        }
    }
    if (Remote()->SendRequest(TRANS_ID_UPDATE_SYSTEM_BAR_PROPS, data, reply, option) != ERR_NONE) {
        WLOGFE("SendRequest failed");
    }
}
} // namespace Rosen
} // namespace OHOS

