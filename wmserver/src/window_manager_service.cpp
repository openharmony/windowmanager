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

#include "window_manager_service.h"

#include <ability_manager_client.h>
#include <cinttypes>
#include <hitrace_meter.h>
#include <ipc_skeleton.h>
#include <parameters.h>
#include <rs_iwindow_animation_controller.h>
#include <system_ability_definition.h>

#include "display_manager_service_inner.h"
#include "dm_common.h"
#include "drag_controller.h"
#include "minimize_app.h"
#include "permission.h"
#include "remote_animation.h"
#include "singleton_container.h"
#include "starting_window.h"
#include "ui/rs_ui_director.h"
#include "window_helper.h"
#include "window_inner_manager.h"
#include "window_manager_agent_controller.h"
#include "window_manager_config.h"
#include "window_manager_hilog.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowManagerService"};
}
WM_IMPLEMENT_SINGLE_INSTANCE(WindowManagerService)

const bool REGISTER_RESULT = SystemAbility::MakeAndRegisterAbility(&SingletonContainer::Get<WindowManagerService>());

WindowManagerService::WindowManagerService() : SystemAbility(WINDOW_MANAGER_SERVICE_ID, true),
    rsInterface_(RSInterfaces::GetInstance())
{
    windowRoot_ = new WindowRoot(
        [this](Event event, const sptr<IRemoteObject>& remoteObject) { OnWindowEvent(event, remoteObject); });
    inputWindowMonitor_ = new InputWindowMonitor(windowRoot_);
    windowController_ = new WindowController(windowRoot_, inputWindowMonitor_);
    snapshotController_ = new SnapshotController(windowRoot_);
    dragController_ = new DragController(windowRoot_);
    windowDumper_ = new WindowDumper(windowRoot_);
    freezeDisplayController_ = new FreezeController();
    windowCommonEvent_ = std::make_shared<WindowCommonEvent>();
    wmsTaskLooper_ = std::make_unique<WindowTaskLooper>();
    startingOpen_ = system::GetParameter("persist.window.sw.enabled", "1") == "1"; // startingWin default enabled

    // init RSUIDirector, it will handle animation callback
    rsUiDirector_ = RSUIDirector::Create();
    rsUiDirector_->SetUITaskRunner([this](const std::function<void()>& task) { wmsTaskLooper_->PostTask(task); });
    rsUiDirector_->Init(false);
}

void WindowManagerService::OnStart()
{
    WLOGFI("WindowManagerService::OnStart start");
    if (!Init()) {
        return;
    }
    WindowInnerManager::GetInstance().Start(system::GetParameter("persist.window.holder.enable", "0") == "1");
    sptr<IDisplayChangeListener> listener = new DisplayChangeListener();
    DisplayManagerServiceInner::GetInstance().RegisterDisplayChangeListener(listener);
    RegisterSnapshotHandler();
    RegisterWindowManagerServiceHandler();
    RegisterWindowVisibilityChangeCallback();
    wmsTaskLooper_->Start();
    AddSystemAbilityListener(COMMON_EVENT_SERVICE_ID);
}


void WindowManagerService::OnAddSystemAbility(int32_t systemAbilityId, const std::string &deviceId)
{
    WLOGFI(" %{public}d", systemAbilityId);
    if (systemAbilityId == COMMON_EVENT_SERVICE_ID) {
        windowCommonEvent_->SubscriberEvent();
    }
}

void WindowManagerService::OnAccountSwitched() const
{
    wmsTaskLooper_->PostTask([this]() {
        windowRoot_->RemoveSingleUserWindowNodes();
    });
    WLOGFI("called");
}

void WindowManagerService::WindowVisibilityChangeCallback(std::shared_ptr<RSOcclusionData> occlusionData)
{
    WLOGFD("NotifyWindowVisibilityChange: enter");
    std::weak_ptr<RSOcclusionData> weak(occlusionData);
    return wmsTaskLooper_->ScheduleTask([this, weak]() {
        auto weakOcclusionData = weak.lock();
        if (weakOcclusionData == nullptr) {
            WLOGFE("weak occlusionData is nullptr");
            return;
        }
        windowRoot_->NotifyWindowVisibilityChange(weakOcclusionData);
    }).wait();
}

void WindowManagerService::RegisterWindowVisibilityChangeCallback()
{
    auto windowVisibilityChangeCb = std::bind(&WindowManagerService::WindowVisibilityChangeCallback, this,
        std::placeholders::_1);
    if (rsInterface_.RegisterOcclusionChangeCallback(windowVisibilityChangeCb) != WM_OK) {
        WLOGFW("WindowManagerService::RegisterWindowVisibilityChangeCallback failed, create async thread!");
        auto fun = [this, windowVisibilityChangeCb]() {
            WLOGFI("WindowManagerService::RegisterWindowVisibilityChangeCallback async thread enter!");
            int counter = 0;
            while (rsInterface_.RegisterOcclusionChangeCallback(windowVisibilityChangeCb) != WM_OK) {
                usleep(10000); // 10000us equals to 10ms
                counter++;
                if (counter >= 2000) { // wait for 2000 * 10ms = 20s
                    WLOGFE("WindowManagerService::RegisterWindowVisibilityChangeCallback timeout!");
                    return;
                }
            }
            WLOGFI("WindowManagerService::RegisterWindowVisibilityChangeCallback async thread register handler"
                " successfully!");
        };
        std::thread thread(fun);
        thread.detach();
        WLOGFI("WindowManagerService::RegisterWindowVisibilityChangeCallback async thread has been detached!");
    } else {
        WLOGFI("WindowManagerService::RegisterWindowVisibilityChangeCallback OnStart succeed!");
    }
}

void WindowManagerService::RegisterSnapshotHandler()
{
    if (snapshotController_ == nullptr) {
        snapshotController_ = new SnapshotController(windowRoot_);
    }
    if (AAFwk::AbilityManagerClient::GetInstance()->RegisterSnapshotHandler(snapshotController_) != ERR_OK) {
        WLOGFW("WindowManagerService::RegisterSnapshotHandler failed, create async thread!");
        auto fun = [this]() {
            WLOGFI("WindowManagerService::RegisterSnapshotHandler async thread enter!");
            int counter = 0;
            while (AAFwk::AbilityManagerClient::GetInstance()->RegisterSnapshotHandler(snapshotController_) != ERR_OK) {
                usleep(10000); // 10000us equals to 10ms
                counter++;
                if (counter >= 2000) { // wait for 2000 * 10ms = 20s
                    WLOGFE("WindowManagerService::RegisterSnapshotHandler timeout!");
                    return;
                }
            }
            WLOGFI("WindowManagerService::RegisterSnapshotHandler async thread register handler successfully!");
        };
        std::thread thread(fun);
        thread.detach();
        WLOGFI("WindowManagerService::RegisterSnapshotHandler async thread has been detached!");
    } else {
        WLOGFI("WindowManagerService::RegisterSnapshotHandler OnStart succeed!");
    }
}

void WindowManagerService::RegisterWindowManagerServiceHandler()
{
    if (wmsHandler_ == nullptr) {
        wmsHandler_ = new WindowManagerServiceHandler();
    }
    if (AAFwk::AbilityManagerClient::GetInstance()->RegisterWindowManagerServiceHandler(wmsHandler_) != ERR_OK) {
        WLOGFW("RegisterWindowManagerServiceHandler failed, create async thread!");
        auto fun = [this]() {
            WLOGFI("RegisterWindowManagerServiceHandler async thread enter!");
            int counter = 0;
            while (AAFwk::AbilityManagerClient::GetInstance()->
                RegisterWindowManagerServiceHandler(wmsHandler_) != ERR_OK) {
                usleep(10000); // 10000us equals to 10ms
                counter++;
                if (counter >= 2000) { // wait for 2000 * 10ms = 20s
                    WLOGFE("RegisterWindowManagerServiceHandler timeout!");
                    return;
                }
            }
            WLOGFI("RegisterWindowManagerServiceHandler async thread register handler successfully!");
        };
        std::thread thread(fun);
        thread.detach();
        WLOGFI("RegisterWindowManagerServiceHandler async thread has been detached!");
    } else {
        WLOGFI("RegisterWindowManagerServiceHandler OnStart succeed!");
    }
}

void WindowManagerServiceHandler::NotifyWindowTransition(
    sptr<AAFwk::AbilityTransitionInfo> from, sptr<AAFwk::AbilityTransitionInfo> to)
{
    sptr<WindowTransitionInfo> fromInfo = new WindowTransitionInfo(from);
    sptr<WindowTransitionInfo> toInfo = new WindowTransitionInfo(to);
    WindowManagerService::GetInstance().NotifyWindowTransition(fromInfo, toInfo, false);
}

int32_t WindowManagerServiceHandler::GetFocusWindow(sptr<IRemoteObject>& abilityToken)
{
    return static_cast<int32_t>(WindowManagerService::GetInstance().GetFocusWindowInfo(abilityToken));
}

void WindowManagerServiceHandler::StartingWindow(
    sptr<AAFwk::AbilityTransitionInfo> info, sptr<Media::PixelMap> pixelMap)
{
    sptr<WindowTransitionInfo> windowInfo = new WindowTransitionInfo(info);
    WLOGFI("hot start is called");
    WindowManagerService::GetInstance().StartingWindow(windowInfo, pixelMap, false);
}

void WindowManagerServiceHandler::StartingWindow(
    sptr<AAFwk::AbilityTransitionInfo> info, sptr<Media::PixelMap> pixelMap, uint32_t bgColor)
{
    sptr<WindowTransitionInfo> windowInfo = new WindowTransitionInfo(info);
    WLOGFI("cold start is called");
    WindowManagerService::GetInstance().StartingWindow(windowInfo, pixelMap, true, bgColor);
}

void WindowManagerServiceHandler::CancelStartingWindow(sptr<IRemoteObject> abilityToken)
{
    WLOGFI("WindowManagerServiceHandler CancelStartingWindow!");
    WindowManagerService::GetInstance().CancelStartingWindow(abilityToken);
}

bool WindowManagerService::Init()
{
    WLOGFI("WindowManagerService::Init start");
    bool ret = Publish(this);
    if (!ret) {
        WLOGFW("WindowManagerService::Init failed");
        return false;
    }
    if (WindowManagerConfig::LoadConfigXml()) {
        WindowManagerConfig::DumpConfig();
        ConfigureWindowManagerService();
    }
    WLOGFI("WindowManagerService::Init success");
    return true;
}

int WindowManagerService::Dump(int fd, const std::vector<std::u16string>& args)
{
    if (windowDumper_ == nullptr) {
        windowDumper_ = new WindowDumper(windowRoot_);
    }
    return wmsTaskLooper_->ScheduleTask([this, fd, &args]() {
        return static_cast<int>(windowDumper_->Dump(fd, args));
    }).get();
}

void WindowManagerService::ConfigureWindowManagerService()
{
    const auto& enableConfig = WindowManagerConfig::GetEnableConfig();
    const auto& intNumbersConfig = WindowManagerConfig::GetIntNumbersConfig();
    const auto& floatNumbersConfig = WindowManagerConfig::GetFloatNumbersConfig();

    if (enableConfig.count("decor") != 0) {
        systemConfig_.isSystemDecorEnable_ = enableConfig.at("decor");
    }

    if (enableConfig.count("minimizeByOther") != 0) {
        MinimizeApp::SetMinimizedByOtherConfig(enableConfig.at("minimizeByOther"));
    }

    if (enableConfig.count("stretchable") != 0) {
        systemConfig_.isStretchable_ = enableConfig.at("stretchable");
    }

    if (intNumbersConfig.count("defaultWindowMode") != 0) {
        auto numbers = intNumbersConfig.at("defaultWindowMode");
        if (numbers.size() == 1 &&
            (numbers[0] == static_cast<uint32_t>(WindowMode::WINDOW_MODE_FULLSCREEN) ||
             numbers[0] == static_cast<uint32_t>(WindowMode::WINDOW_MODE_FLOATING))) {
            systemConfig_.defaultWindowMode_ = static_cast<WindowMode>(static_cast<uint32_t>(numbers[0]));
            StartingWindow::SetDefaultWindowMode(systemConfig_.defaultWindowMode_);
        }
    }

    if (intNumbersConfig.count("maxAppWindowNumber") != 0) {
        auto numbers = intNumbersConfig.at("maxAppWindowNumber");
        if (numbers.size() == 1) {
            if (numbers[0] > 0) {
                windowRoot_->SetMaxAppWindowNumber(static_cast<uint32_t>(numbers[0]));
            }
        }
    }

    if (intNumbersConfig.count("modeChangeHotZones") != 0) {
        auto numbers = intNumbersConfig.at("modeChangeHotZones");
        if (numbers.size() == 3) { // 3 hot zones
            hotZonesConfig_.fullscreenRange_ = static_cast<uint32_t>(numbers[0]); // 0 fullscreen
            hotZonesConfig_.primaryRange_ = static_cast<uint32_t>(numbers[1]);    // 1 primary
            hotZonesConfig_.secondaryRange_ = static_cast<uint32_t>(numbers[2]);  // 2 secondary
            hotZonesConfig_.isModeChangeHotZoneConfigured_ = true;
        }
    }

    if (floatNumbersConfig.count("splitRatios") != 0) {
        windowRoot_->SetSplitRatios(floatNumbersConfig.at("splitRatios"));
    }

    if (floatNumbersConfig.count("exitSplitRatios") != 0) {
        windowRoot_->SetExitSplitRatios(floatNumbersConfig.at("exitSplitRatios"));
    }
}

void WindowManagerService::OnStop()
{
    windowCommonEvent_->UnSubscriberEvent();
    WindowInnerManager::GetInstance().Stop();
    WLOGFI("ready to stop service.");
}

WMError WindowManagerService::NotifyWindowTransition(
    sptr<WindowTransitionInfo>& fromInfo, sptr<WindowTransitionInfo>& toInfo, bool isFromClient)
{
    if (!isFromClient) {
        WLOGFI("NotifyWindowTransition asynchronously.");
        wmsTaskLooper_->PostTask([this, fromInfo, toInfo]() mutable {
            return windowController_->NotifyWindowTransition(fromInfo, toInfo);
        });
        return WMError::WM_OK;
    } else {
        WLOGFI("NotifyWindowTransition synchronously.");
        return wmsTaskLooper_->ScheduleTask([this, &fromInfo, &toInfo]() {
            return windowController_->NotifyWindowTransition(fromInfo, toInfo);
        }).get();
    }
}

WMError WindowManagerService::GetFocusWindowInfo(sptr<IRemoteObject>& abilityToken)
{
    return wmsTaskLooper_->ScheduleTask([this, &abilityToken]() {
        return windowController_->GetFocusWindowInfo(abilityToken);
    }).get();
}

void WindowManagerService::StartingWindow(sptr<WindowTransitionInfo> info, sptr<Media::PixelMap> pixelMap,
    bool isColdStart, uint32_t bkgColor)
{
    if (!startingOpen_) {
        WLOGFI("startingWindow not open!");
        return;
    }
    return wmsTaskLooper_->PostTask([this, info, pixelMap, isColdStart, bkgColor]() {
        return windowController_->StartingWindow(info, pixelMap, bkgColor, isColdStart);
    });
}

void WindowManagerService::CancelStartingWindow(sptr<IRemoteObject> abilityToken)
{
    WLOGFI("begin CancelStartingWindow!");
    if (!startingOpen_) {
        WLOGFI("startingWindow not open!");
        return;
    }
    return wmsTaskLooper_->PostTask([this, abilityToken]() {
        return windowController_->CancelStartingWindow(abilityToken);
    });
}

WMError WindowManagerService::CreateWindow(sptr<IWindow>& window, sptr<WindowProperty>& property,
    const std::shared_ptr<RSSurfaceNode>& surfaceNode, uint32_t& windowId, sptr<IRemoteObject> token)
{
    if (window == nullptr || property == nullptr || surfaceNode == nullptr) {
        WLOGFE("window is invalid");
        return WMError::WM_ERROR_NULLPTR;
    }
    if ((!window) || (!window->AsObject())) {
        WLOGFE("failed to get window agent");
        return WMError::WM_ERROR_NULLPTR;
    }
    int pid = IPCSkeleton::GetCallingPid();
    int uid = IPCSkeleton::GetCallingUid();
    WMError ret = wmsTaskLooper_->ScheduleTask([this, pid, uid, &window, &property, &surfaceNode, &windowId, &token]() {
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:CreateWindow(%u)", windowId);
        return windowController_->CreateWindow(window, property, surfaceNode, windowId, token, pid, uid);
    }).get();
    accessTokenIdMaps_.insert(std::pair(windowId, IPCSkeleton::GetCallingTokenID()));
    return ret;
}

WMError WindowManagerService::AddWindow(sptr<WindowProperty>& property)
{
    return wmsTaskLooper_->ScheduleTask([this, &property]() {
        return HandleAddWindow(property);
    }).get();
}

WMError WindowManagerService::HandleAddWindow(sptr<WindowProperty>& property)
{
    if (property == nullptr) {
        WLOGFE("property is nullptr");
        return WMError::WM_ERROR_NULLPTR;
    }
    Rect rect = property->GetRequestRect();
    uint32_t windowId = property->GetWindowId();
    WLOGFI("[WMS] Add: %{public}5d %{public}4d %{public}4d %{public}4d [%{public}4d %{public}4d " \
        "%{public}4d %{public}4d]", windowId, property->GetWindowType(), property->GetWindowMode(),
        property->GetWindowFlags(), rect.posX_, rect.posY_, rect.width_, rect.height_);
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:AddWindow(%u)", windowId);
    WMError res = windowController_->AddWindowNode(property);
    if (property->GetWindowType() == WindowType::WINDOW_TYPE_DRAGGING_EFFECT) {
        dragController_->StartDrag(windowId);
    }
    return res;
}

WMError WindowManagerService::RemoveWindow(uint32_t windowId)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId]() {
        return HandleRemoveWindow(windowId);
    }).get();
}

WMError WindowManagerService::HandleRemoveWindow(uint32_t windowId)
{
    WLOGFI("[WMS] Remove: %{public}u", windowId);
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:RemoveWindow(%u)", windowId);
    return windowController_->RemoveWindowNode(windowId);
}

WMError WindowManagerService::DestroyWindow(uint32_t windowId, bool onlySelf)
{
    if (!accessTokenIdMaps_.isExistAndRemove(windowId, IPCSkeleton::GetCallingTokenID())) {
        WLOGFI("Operation rejected");
        return WMError::WM_ERROR_INVALID_OPERATION;
    }
    return wmsTaskLooper_->ScheduleTask([this, windowId, onlySelf]() {
        WLOGFI("[WMS] Destroy: %{public}u", windowId);
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:DestroyWindow(%u)", windowId);
        auto node = windowRoot_->GetWindowNode(windowId);
        if (node != nullptr && node->GetWindowType() == WindowType::WINDOW_TYPE_DRAGGING_EFFECT) {
            dragController_->FinishDrag(windowId);
        }
        return windowController_->DestroyWindow(windowId, onlySelf);
    }).get();
}

WMError WindowManagerService::RequestFocus(uint32_t windowId)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId]() {
        WLOGFI("[WMS] RequestFocus: %{public}u", windowId);
        return windowController_->RequestFocus(windowId);
    }).get();
}

WMError WindowManagerService::SetWindowBackgroundBlur(uint32_t windowId, WindowBlurLevel level)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId, level]() {
        return windowController_->SetWindowBackgroundBlur(windowId, level);
    }).get();
}

AvoidArea WindowManagerService::GetAvoidAreaByType(uint32_t windowId, AvoidAreaType avoidAreaType)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId, avoidAreaType]() {
        WLOGFI("[WMS] GetAvoidAreaByType: %{public}u, Type: %{public}u", windowId,
            static_cast<uint32_t>(avoidAreaType));
        return windowController_->GetAvoidAreaByType(windowId, avoidAreaType);
    }).get();
}

void WindowManagerService::RegisterWindowManagerAgent(WindowManagerAgentType type,
    const sptr<IWindowManagerAgent>& windowManagerAgent)
{
    if ((windowManagerAgent == nullptr) || (windowManagerAgent->AsObject() == nullptr)) {
        WLOGFE("windowManagerAgent is null");
        return;
    }
    return wmsTaskLooper_->ScheduleTask([this, &windowManagerAgent, type]() {
        WindowManagerAgentController::GetInstance().RegisterWindowManagerAgent(windowManagerAgent, type);
        if (type == WindowManagerAgentType::WINDOW_MANAGER_AGENT_TYPE_SYSTEM_BAR) { // if system bar, notify once
            windowController_->NotifySystemBarTints();
        }
    }).wait();
}

void WindowManagerService::UnregisterWindowManagerAgent(WindowManagerAgentType type,
    const sptr<IWindowManagerAgent>& windowManagerAgent)
{
    if ((windowManagerAgent == nullptr) || (windowManagerAgent->AsObject() == nullptr)) {
        WLOGFE("windowManagerAgent is null");
        return;
    }
    return wmsTaskLooper_->ScheduleTask([this, &windowManagerAgent, type]() {
        WindowManagerAgentController::GetInstance().UnregisterWindowManagerAgent(windowManagerAgent, type);
    }).wait();
}

WMError WindowManagerService::SetWindowAnimationController(const sptr<RSIWindowAnimationController>& controller)
{
    if (controller == nullptr) {
        WLOGFE("RSWindowAnimation: Failed to set window animation controller, controller is null!");
        return WMError::WM_ERROR_NULLPTR;
    }

    sptr<AgentDeathRecipient> deathRecipient = new AgentDeathRecipient(
        [this](sptr<IRemoteObject>& remoteObject) {
            wmsTaskLooper_->ScheduleTask([&remoteObject]() {
                RemoteAnimation::OnRemoteDie(remoteObject);
            }).wait();
        }
    );
    controller->AsObject()->AddDeathRecipient(deathRecipient);
    return wmsTaskLooper_->ScheduleTask([this, &controller]() {
        return windowController_->SetWindowAnimationController(controller);
    }).get();
}

void WindowManagerService::OnWindowEvent(Event event, const sptr<IRemoteObject>& remoteObject)
{
    if (event == Event::REMOTE_DIED) {
        return wmsTaskLooper_->ScheduleTask([this, &remoteObject, event]() {
            uint32_t windowId = windowRoot_->GetWindowIdByObject(remoteObject);
            auto node = windowRoot_->GetWindowNode(windowId);
            if (node != nullptr && node->GetWindowType() == WindowType::WINDOW_TYPE_DRAGGING_EFFECT) {
                dragController_->FinishDrag(windowId);
            }
            windowController_->DestroyWindow(windowId, true);
        }).wait();
    }
}

void WindowManagerService::NotifyDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type)
{
    HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:NotifyDisplayStateChange(%u)", type);
    DisplayId displayId = (displayInfo == nullptr) ? DISPLAY_ID_INVALID : displayInfo->GetDisplayId();
    if (type == DisplayStateChangeType::FREEZE) {
        freezeDisplayController_->FreezeDisplay(displayId);
    } else if (type == DisplayStateChangeType::UNFREEZE) {
        freezeDisplayController_->UnfreezeDisplay(displayId);
    } else {
        wmsTaskLooper_->PostTask([this, defaultDisplayId, displayInfo, displayInfoMap, type]() mutable {
            windowController_->NotifyDisplayStateChange(defaultDisplayId, displayInfo, displayInfoMap, type);
        });
    }
}

void DisplayChangeListener::OnDisplayStateChange(DisplayId defaultDisplayId, sptr<DisplayInfo> displayInfo,
    const std::map<DisplayId, sptr<DisplayInfo>>& displayInfoMap, DisplayStateChangeType type)
{
    WindowManagerService::GetInstance().NotifyDisplayStateChange(defaultDisplayId, displayInfo, displayInfoMap, type);
}

void DisplayChangeListener::OnGetWindowPreferredOrientation(DisplayId displayId, Orientation &orientation)
{
    WindowManagerService::GetInstance().GetWindowPreferredOrientation(displayId, orientation);
}

void DisplayChangeListener::OnScreenshot(DisplayId displayId)
{
    WindowManagerService::GetInstance().OnScreenshot(displayId);
}

void WindowManagerService::ProcessPointDown(uint32_t windowId, bool isStartDrag)
{
    return wmsTaskLooper_->PostTask([this, windowId, isStartDrag]() {
        windowController_->ProcessPointDown(windowId, isStartDrag);
    });
}

void WindowManagerService::ProcessPointUp(uint32_t windowId)
{
    return wmsTaskLooper_->PostTask([this, windowId]() {
        windowController_->ProcessPointUp(windowId);
    });
}

void WindowManagerService::MinimizeAllAppWindows(DisplayId displayId)
{
    return wmsTaskLooper_->PostTask([this, displayId]() {
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:MinimizeAllAppWindows(%" PRIu64")", displayId);
        WLOGFI("displayId %{public}" PRIu64"", displayId);
        windowController_->MinimizeAllAppWindows(displayId);
    });
}

WMError WindowManagerService::ToggleShownStateForAllAppWindows()
{
    wmsTaskLooper_->PostTask([this]() {
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:ToggleShownStateForAllAppWindows");
        return windowController_->ToggleShownStateForAllAppWindows();
    });
    return  WMError::WM_OK;
}

WMError WindowManagerService::GetTopWindowId(uint32_t mainWinId, uint32_t& topWinId)
{
    return wmsTaskLooper_->ScheduleTask([this, &topWinId, mainWinId]() {
        return windowController_->GetTopWindowId(mainWinId, topWinId);
    }).get();
}

WMError WindowManagerService::SetWindowLayoutMode(WindowLayoutMode mode)
{
    return wmsTaskLooper_->ScheduleTask([this, mode]() {
        WLOGFI("layoutMode: %{public}u", mode);
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:SetWindowLayoutMode");
        return windowController_->SetWindowLayoutMode(mode);
    }).get();
}

WMError WindowManagerService::UpdateProperty(sptr<WindowProperty>& windowProperty, PropertyChangeAction action)
{
    if (windowProperty == nullptr) {
        WLOGFE("property is invalid");
        return WMError::WM_ERROR_NULLPTR;
    }
    if (action == PropertyChangeAction::ACTION_UPDATE_TRANSFORM_PROPERTY) {
        wmsTaskLooper_->PostTask([this, windowProperty, action]() mutable {
            windowController_->UpdateProperty(windowProperty, action);
        });
        return WMError::WM_OK;
    }
    return wmsTaskLooper_->ScheduleTask([this, &windowProperty, action]() {
        HITRACE_METER_FMT(HITRACE_TAG_WINDOW_MANAGER, "wms:UpdateProperty");
        WMError res = windowController_->UpdateProperty(windowProperty, action);
        if (action == PropertyChangeAction::ACTION_UPDATE_RECT && res == WMError::WM_OK &&
            windowProperty->GetWindowSizeChangeReason() == WindowSizeChangeReason::MOVE) {
            dragController_->UpdateDragInfo(windowProperty->GetWindowId());
        }
        return res;
    }).get();
}

WMError WindowManagerService::GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo)
{
    if (windowInfo == nullptr) {
        WLOGFE("windowInfo is invalid");
        return WMError::WM_ERROR_NULLPTR;
    }
    return wmsTaskLooper_->ScheduleTask([this, &windowInfo]() {
        return windowRoot_->GetAccessibilityWindowInfo(windowInfo);
    }).get();
}

WMError WindowManagerService::GetSystemConfig(SystemConfig& systemConfig)
{
    systemConfig = systemConfig_;
    return WMError::WM_OK;
}

WMError WindowManagerService::GetModeChangeHotZones(DisplayId displayId, ModeChangeHotZones& hotZones)
{
    if (!hotZonesConfig_.isModeChangeHotZoneConfigured_) {
        return WMError::WM_DO_NOTHING;
    }

    return windowController_->GetModeChangeHotZones(displayId, hotZones, hotZonesConfig_);
}

void WindowManagerService::MinimizeWindowsByLauncher(std::vector<uint32_t> windowIds, bool isAnimated,
    sptr<RSIWindowAnimationFinishedCallback>& finishCallback)
{
    return wmsTaskLooper_->ScheduleTask([this, windowIds, isAnimated, &finishCallback]() mutable {
        return windowController_->MinimizeWindowsByLauncher(windowIds, isAnimated, finishCallback);
    }).get();
}

void WindowManagerService::GetWindowPreferredOrientation(DisplayId displayId, Orientation &orientation)
{
    wmsTaskLooper_->ScheduleTask([this, displayId, &orientation]() mutable {
        orientation = windowController_->GetWindowPreferredOrientation(displayId);
    }).wait();
}

WMError WindowManagerService::UpdateAvoidAreaListener(uint32_t windowId, bool haveAvoidAreaListener)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId, haveAvoidAreaListener]() {
        sptr<WindowNode> node = windowRoot_->GetWindowNode(windowId);
        if (node == nullptr) {
            WLOGFE("get window node failed. win %{public}u", windowId);
            return WMError::WM_DO_NOTHING;
        }
        sptr<WindowNodeContainer> container = windowRoot_->GetWindowNodeContainer(node->GetDisplayId());
        if (container == nullptr) {
            WLOGFE("get container failed. win %{public}u display %{public}" PRIu64"", windowId, node->GetDisplayId());
            return WMError::WM_DO_NOTHING;
        }
        container->UpdateAvoidAreaListener(node, haveAvoidAreaListener);
        return WMError::WM_OK;
    }).get();
}

WMError WindowManagerService::UpdateRsTree(uint32_t windowId, bool isAdd)
{
    return wmsTaskLooper_->ScheduleTask([this, windowId, isAdd]() {
        return windowRoot_->UpdateRsTree(windowId, isAdd);
    }).get();
}

void WindowManagerService::OnScreenshot(DisplayId displayId)
{
    wmsTaskLooper_->PostTask([this, displayId]() {
        windowController_->OnScreenshot(displayId);
    });
}
} // namespace Rosen
} // namespace OHOS
