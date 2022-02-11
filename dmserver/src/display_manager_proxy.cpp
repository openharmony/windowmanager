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

#include "display_manager_proxy.h"

#include <cinttypes>
#include <ipc_types.h>

#include "window_manager_hilog.h"

#include <parcel.h>

namespace OHOS::Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_DISPLAY, "DisplayManagerProxy"};
}

DisplayId DisplayManagerProxy::GetDefaultDisplayId()
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetDefaultDisplayId: remote is nullptr");
        return DISPLAY_ID_INVALD;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetDefaultDisplayId: WriteInterfaceToken failed");
        return DISPLAY_ID_INVALD;
    }
    if (remote->SendRequest(TRANS_ID_GET_DEFAULT_DISPLAY_ID, data, reply, option) != ERR_NONE) {
        WLOGFW("GetDefaultDisplayId: SendRequest failed");
        return DISPLAY_ID_INVALD;
    }

    DisplayId displayId = reply.ReadUint64();
    WLOGFI("DisplayManagerProxy::GetDefaultDisplayId %" PRIu64"", displayId);
    return displayId;
}

DisplayInfo DisplayManagerProxy::GetDisplayInfoById(DisplayId displayId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetDisplayInfoById: remote is nullptr");
        return DisplayInfo();
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetDisplayInfoById: WriteInterfaceToken failed");
        return DisplayInfo();
    }
    if (!data.WriteUint64(displayId)) {
        WLOGFW("GetDisplayInfoById: WriteUint64 displayId failed");
        return DisplayInfo();
    }
    if (remote->SendRequest(TRANS_ID_GET_DISPLAY_BY_ID, data, reply, option) != ERR_NONE) {
        WLOGFW("GetDisplayInfoById: SendRequest failed");
        return DisplayInfo();
    }

    sptr<DisplayInfo> info = reply.ReadParcelable<DisplayInfo>();
    if (info == nullptr) {
        WLOGFW("DisplayManagerProxy::GetDisplayInfoById SendRequest nullptr.");
        return DisplayInfo();
    }
    return *info;
}

ScreenId DisplayManagerProxy::CreateVirtualScreen(VirtualScreenOption virtualOption)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::CreateVirtualScreen: remote is nullptr");
        return SCREEN_ID_INVALID;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("DisplayManagerProxy::CreateVirtualScreen: WriteInterfaceToken failed");
        return SCREEN_ID_INVALID;
    }
    bool res = data.WriteString(virtualOption.name_) && data.WriteUint32(virtualOption.width_) &&
        data.WriteUint32(virtualOption.height_) && data.WriteFloat(virtualOption.density_) &&
        data.WriteRemoteObject(virtualOption.surface_->GetProducer()->AsObject()) &&
        data.WriteInt32(virtualOption.flags_) && data.WriteBool(virtualOption.isForShot);
    if (!res) {
        WLOGFE("DisplayManagerProxy::Write data failed");
        return SCREEN_ID_INVALID;
    }
    if (remote->SendRequest(TRANS_ID_CREATE_VIRTUAL_SCREEN, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::CreateVirtualScreen: SendRequest failed");
        return SCREEN_ID_INVALID;
    }

    ScreenId screenId = static_cast<ScreenId>(reply.ReadUint64());
    WLOGFI("DisplayManagerProxy::CreateVirtualScreen %" PRIu64"", screenId);
    return screenId;
}

DMError DisplayManagerProxy::DestroyVirtualScreen(ScreenId screenId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::DestroyVirtualScreen: remote is nullptr");
        return DMError::DM_ERROR_REMOTE_CREATE_FAILED;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("DisplayManagerProxy::DestroyVirtualScreen: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("DisplayManagerProxy::DestroyVirtualScreen: WriteUint64 screenId failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_DESTROY_VIRTUAL_SCREEN, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::DestroyVirtualScreen: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    return static_cast<DMError>(reply.ReadInt32());
}

bool DisplayManagerProxy::RequestRotation(ScreenId screenId, Rotation rotation)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("fail to request rotation: remote is null");
        return false;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("fail to request rotation: WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("fail to request rotation: Write screenId failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(rotation))) {
        WLOGFW("fail to request rotation: Write rotation failed");
        return false;
    }
    if (remote->SendRequest(TRANS_ID_REQUEST_ROTATION, data, reply, option) != ERR_NONE) {
        WLOGFW("fail to request rotation: SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

std::shared_ptr<Media::PixelMap> DisplayManagerProxy::GetDispalySnapshot(DisplayId displayId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetDispalySnapshot: remote is nullptr");
        return nullptr;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetDispalySnapshot: WriteInterfaceToken failed");
        return nullptr;
    }

    if (!data.WriteUint64(displayId)) {
        WLOGFE("Write dispalyId failed");
        return nullptr;
    }

    if (remote->SendRequest(TRANS_ID_GET_DISPLAY_SNAPSHOT, data, reply, option) != ERR_NONE) {
        WLOGFW("GetDispalySnapshot: SendRequest failed");
        return nullptr;
    }

    std::shared_ptr<Media::PixelMap> pixelMap(reply.ReadParcelable<Media::PixelMap>());
    if (pixelMap == nullptr) {
        WLOGFW("DisplayManagerProxy::GetDispalySnapshot SendRequest nullptr.");
        return nullptr;
    }
    return pixelMap;
}

DMError DisplayManagerProxy::GetScreenSupportedColorGamuts(ScreenId screenId,
    std::vector<ScreenColorGamut>& colorGamuts)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::GetScreenSupportedColorGamuts: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::GetScreenSupportedColorGamuts: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("DisplayManagerProxy::GetScreenSupportedColorGamuts: WriteUint64 screenId failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_GET_SUPPORTED_COLOR_GAMUTS, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::GetScreenSupportedColorGamuts: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    if (ret != DMError::DM_OK) {
        return ret;
    }
    uint32_t size = reply.ReadUint32();
    colorGamuts.clear();
    for (uint32_t i = 0; i < size; i++) {
        ScreenColorGamut colorGamut = static_cast<ScreenColorGamut>(reply.ReadUint32());
        colorGamuts.push_back(colorGamut);
    }
    return ret;
}

DMError DisplayManagerProxy::GetScreenColorGamut(ScreenId screenId, ScreenColorGamut& colorGamut)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::GetScreenColorGamut: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::GetScreenColorGamut: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("DisplayManagerProxy::GetScreenColorGamut: WriteUint64 uint64_t failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_GET_COLOR_GAMUT, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::GetScreenColorGamut: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    if (ret != DMError::DM_OK) {
        return ret;
    }
    colorGamut = static_cast<ScreenColorGamut>(reply.ReadUint32());
    return ret;
}

DMError DisplayManagerProxy::SetScreenColorGamut(ScreenId screenId, int32_t colorGamutIdx)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::SetScreenColorGamut: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::SetScreenColorGamut: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId)) || !data.WriteInt32(colorGamutIdx)) {
        WLOGFW("DisplayManagerProxy::SetScreenColorGamut: Write failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_SET_COLOR_GAMUT, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::SetScreenColorGamut: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    return ret;
}

DMError DisplayManagerProxy::GetScreenGamutsMap(ScreenId screenId, ScreenGamutMap& gamutMap)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::GetScreenGamutsMap: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::GetScreenGamutsMap: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("DisplayManagerProxy::GetScreenGamutsMap: WriteUint64 screenId failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_GET_GAMUT_MAP, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::GetScreenGamutsMap: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    if (ret != DMError::DM_OK) {
        return ret;
    }
    gamutMap = static_cast<ScreenGamutMap>(reply.ReadUint32());
    return ret;
}

DMError DisplayManagerProxy::SetScreenGamutsMap(ScreenId screenId, ScreenGamutMap gamutMap)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::SetScreenGamutsMap: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::SetScreenGamutsMap: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId)) || !data.WriteUint32(static_cast<uint32_t>(gamutMap))) {
        WLOGFW("DisplayManagerProxy::SetScreenGamutsMap: Writ failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_SET_GAMUT_MAP, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::SetScreenGamutsMap: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    return ret;
}

DMError DisplayManagerProxy::SetScreenColorTransform(ScreenId screenId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("DisplayManagerProxy::SetScreenColorTransform: remote is nullptr");
        return DMError::DM_ERROR_NULLPTR;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFW("DisplayManagerProxy::SetScreenColorTransform: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUint64(static_cast<uint64_t>(screenId))) {
        WLOGFW("DisplayManagerProxy::SetScreenColorTransform: WriteUint64 screenId failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_SET_COLOR_TRANSFORM, data, reply, option) != ERR_NONE) {
        WLOGFW("DisplayManagerProxy::SetScreenColorTransform: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    DMError ret = static_cast<DMError>(reply.ReadInt32());
    return ret;
}

bool DisplayManagerProxy::RegisterDisplayManagerAgent(const sptr<IDisplayManagerAgent>& displayManagerAgent,
    DisplayManagerAgentType type)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }

    if (!data.WriteRemoteObject(displayManagerAgent->AsObject())) {
        WLOGFE("Write IDisplayManagerAgent failed");
        return false;
    }

    if (!data.WriteUint32(static_cast<uint32_t>(type))) {
        WLOGFE("Write DisplayManagerAgent type failed");
        return false;
    }

    if (Remote()->SendRequest(TRANS_ID_REGISTER_DISPLAY_MANAGER_AGENT, data, reply, option) != ERR_NONE) {
        WLOGFE("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::UnregisterDisplayManagerAgent(const sptr<IDisplayManagerAgent>& displayManagerAgent,
    DisplayManagerAgentType type)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }

    if (!data.WriteRemoteObject(displayManagerAgent->AsObject())) {
        WLOGFE("Write IWindowManagerAgent failed");
        return false;
    }

    if (!data.WriteUint32(static_cast<uint32_t>(type))) {
        WLOGFE("Write DisplayManagerAgent type failed");
        return false;
    }

    if (Remote()->SendRequest(TRANS_ID_UNREGISTER_DISPLAY_MANAGER_AGENT, data, reply, option) != ERR_NONE) {
        WLOGFE("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::WakeUpBegin(PowerStateChangeReason reason)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(reason))) {
        WLOGFE("Write PowerStateChangeReason failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_WAKE_UP_BEGIN, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::WakeUpEnd()
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_WAKE_UP_END, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::SuspendBegin(PowerStateChangeReason reason)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(reason))) {
        WLOGFE("Write PowerStateChangeReason failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_SUSPEND_BEGIN, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::SuspendEnd()
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_SUSPEND_END, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::SetScreenPowerForAll(DisplayPowerState state, PowerStateChangeReason reason)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(state))) {
        WLOGFE("Write DisplayPowerState failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(reason))) {
        WLOGFE("Write PowerStateChangeReason failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_SET_SCREEN_POWER_FOR_ALL, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

bool DisplayManagerProxy::SetDisplayState(DisplayState state)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(state))) {
        WLOGFE("Write DisplayState failed");
        return false;
    }
    if (Remote()->SendRequest(TRANS_ID_SET_DISPLAY_STATE, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}

DisplayState DisplayManagerProxy::GetDisplayState(DisplayId displayId)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return DisplayState::UNKNOWN;
    }
    if (!data.WriteUint64(displayId)) {
        WLOGFE("Write displayId failed");
        return DisplayState::UNKNOWN;
    }
    if (Remote()->SendRequest(TRANS_ID_GET_DISPLAY_STATE, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return DisplayState::UNKNOWN;
    }
    return static_cast<DisplayState>(reply.ReadUint32());
}

void DisplayManagerProxy::NotifyDisplayEvent(DisplayEvent event)
{
    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("WriteInterfaceToken failed");
        return;
    }
    if (!data.WriteUint32(static_cast<uint32_t>(event))) {
        WLOGFE("Write DisplayEvent failed");
        return;
    }
    if (Remote()->SendRequest(TRANS_ID_NOTIFY_DISPLAY_EVENT, data, reply, option) != ERR_NONE) {
        WLOGFW("SendRequest failed");
        return;
    }
}

DMError DisplayManagerProxy::MakeMirror(ScreenId mainScreenId, std::vector<ScreenId> mirrorScreenId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("create mirror fail: remote is null");
        return DMError::DM_ERROR_REMOTE_CREATE_FAILED;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("create mirror fail: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    bool res = data.WriteUint64(static_cast<uint64_t>(mainScreenId)) &&
        data.WriteUInt64Vector(mirrorScreenId);
    if (!res) {
        WLOGFE("create mirror fail: data write failed");
        return DMError::DM_ERROR_WRITE_DATA_FAILED;
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_MAKE_MIRROR, data, reply, option) != ERR_NONE) {
        WLOGFW("create mirror fail: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    return static_cast<DMError>(reply.ReadInt32());
}

sptr<ScreenInfo> DisplayManagerProxy::GetScreenInfoById(ScreenId screenId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetScreenInfoById: remote is nullptr");
        return nullptr;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetScreenInfoById: WriteInterfaceToken failed");
        return nullptr;
    }
    if (!data.WriteUint64(screenId)) {
        WLOGFE("GetScreenInfoById: Write screenId failed");
        return nullptr;
    }
    if (remote->SendRequest(TRANS_ID_GET_SCREEN_INFO_BY_ID, data, reply, option) != ERR_NONE) {
        WLOGFW("GetScreenInfoById: SendRequest failed");
        return nullptr;
    }

    sptr<ScreenInfo> info = reply.ReadStrongParcelable<ScreenInfo>();
    if (info == nullptr) {
        WLOGFW("GetScreenInfoById SendRequest nullptr.");
        return nullptr;
    }
    for (int i = 0; i < info->modes_.size(); i++) {
        WLOGFI("info modes is width: %{public}u, height: %{public}u, freshRate: %{public}u",
            info->modes_[i]->width_, info->modes_[i]->height_, info->modes_[i]->freshRate_);
    }
    return info;
}

sptr<ScreenGroupInfo> DisplayManagerProxy::GetScreenGroupInfoById(ScreenId screenId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetScreenGroupInfoById: remote is nullptr");
        return nullptr;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetScreenGroupInfoById: WriteInterfaceToken failed");
        return nullptr;
    }
    if (!data.WriteUint64(screenId)) {
        WLOGFE("GetScreenGroupInfoById: Write screenId failed");
        return nullptr;
    }
    if (remote->SendRequest(TRANS_ID_GET_SCREEN_GROUP_INFO_BY_ID, data, reply, option) != ERR_NONE) {
        WLOGFW("GetScreenGroupInfoById: SendRequest failed");
        return nullptr;
    }

    sptr<ScreenGroupInfo> info = reply.ReadStrongParcelable<ScreenGroupInfo>();
    if (info == nullptr) {
        WLOGFW("GetScreenGroupInfoById SendRequest nullptr.");
        return nullptr;
    }
    return info;
}

std::vector<sptr<ScreenInfo>> DisplayManagerProxy::GetAllScreenInfos()
{
    std::vector<sptr<ScreenInfo>> screenInfos;
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("GetAllScreenInfos: remote is nullptr");
        return screenInfos;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("GetAllScreenInfos: WriteInterfaceToken failed");
        return screenInfos;
    }
    if (remote->SendRequest(TRANS_ID_GET_ALL_SCREEN_INFOS, data, reply, option) != ERR_NONE) {
        WLOGFW("GetAllScreenInfos: SendRequest failed");
        return screenInfos;
    }

    uint32_t nums = reply.ReadUint32();
    for (uint32_t i = 0; i < nums; ++i) {
        sptr<ScreenInfo> info = reply.ReadStrongParcelable<ScreenInfo>();
        screenInfos.emplace_back(info);
    }
    return screenInfos;
}

DMError DisplayManagerProxy::MakeExpand(std::vector<ScreenId> screenId, std::vector<Point> startPoint)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("MakeExpand: remote is null");
        return DMError::DM_ERROR_REMOTE_CREATE_FAILED;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("MakeExpand: WriteInterfaceToken failed");
        return DMError::DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED;
    }
    if (!data.WriteUInt64Vector(screenId)) {
        WLOGFE("MakeExpand: write screenId failed");
        return DMError::DM_ERROR_WRITE_DATA_FAILED;
    }
    uint32_t num = startPoint.size();
    if (!data.WriteUint32(num)) {
        WLOGFE("MakeExpand: write startPoint size failed");
        return DMError::DM_ERROR_WRITE_DATA_FAILED;
    }
    for (auto point: startPoint) {
        if (!(data.WriteInt32(point.posX_) && data.WriteInt32(point.posY_))) {
            WLOGFE("MakeExpand: write startPoint failed");
            return DMError::DM_ERROR_WRITE_DATA_FAILED;
        }
    }
    if (remote->SendRequest(TRANS_ID_SCREEN_MAKE_EXPAND, data, reply, option) != ERR_NONE) {
        WLOGFE("MakeExpand: SendRequest failed");
        return DMError::DM_ERROR_IPC_FAILED;
    }
    return static_cast<DMError>(reply.ReadInt32());
}

bool DisplayManagerProxy::SetScreenActiveMode(ScreenId screenId, uint32_t modeId)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        WLOGFW("SetScreenActiveMode: remote is null");
        return false;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    if (!data.WriteInterfaceToken(GetDescriptor())) {
        WLOGFE("SetScreenActiveMode: WriteInterfaceToken failed");
        return false;
    }
    if (!data.WriteUint64(screenId) || !data.WriteUint32(modeId)) {
        WLOGFE("SetScreenActiveMode: write screenId/modeId failed");
        return false;
    }
    if (remote->SendRequest(TRANS_ID_SET_SCREEN_ACTIVE_MODE, data, reply, option) != ERR_NONE) {
        WLOGFE("SetScreenActiveMode: SendRequest failed");
        return false;
    }
    return reply.ReadBool();
}
} // namespace OHOS::Rosen