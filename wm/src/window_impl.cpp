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

#include "window_impl.h"

#include <cmath>
#include <sstream>

#include <ability_manager_client.h>
#include <hisysevent.h>
#include <ipc_skeleton.h>
#include <transaction/rs_interfaces.h>

#include "color_parser.h"
#include "display_manager.h"
#include "singleton_container.h"
#include "surface_capture_future.h"
#include "window_adapter.h"
#include "window_agent.h"
#include "window_helper.h"
#include "window_manager_hilog.h"
#include "wm_common.h"
#include "wm_common_inner.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowImpl"};
    const std::string PARAM_DUMP_HELP = "-h";
    const std::string WM_CALLBACK_THREAD_NAME = "window_listener_handler";
}

const WindowImpl::ColorSpaceConvertMap WindowImpl::colorSpaceConvertMap[] = {
    { ColorSpace::COLOR_SPACE_DEFAULT, ColorGamut::COLOR_GAMUT_SRGB },
    { ColorSpace::COLOR_SPACE_WIDE_GAMUT, ColorGamut::COLOR_GAMUT_DCI_P3 },
};

std::map<std::string, std::pair<uint32_t, sptr<Window>>> WindowImpl::windowMap_;
std::map<uint32_t, std::vector<sptr<WindowImpl>>> WindowImpl::subWindowMap_;
std::map<uint32_t, std::vector<sptr<WindowImpl>>> WindowImpl::appFloatingWindowMap_;
static int constructorCnt = 0;
static int deConstructorCnt = 0;
WindowImpl::WindowImpl(const sptr<WindowOption>& option)
{
    property_ = new (std::nothrow) WindowProperty();
    property_->SetWindowName(option->GetWindowName());
    property_->SetRequestRect(option->GetWindowRect());
    property_->SetWindowType(option->GetWindowType());
    property_->SetWindowMode(option->GetWindowMode());
    property_->SetWindowBackgroundBlur(option->GetWindowBackgroundBlur());
    property_->SetFullScreen(option->GetWindowMode() == WindowMode::WINDOW_MODE_FULLSCREEN);
    property_->SetFocusable(option->GetFocusable());
    property_->SetTouchable(option->GetTouchable());
    property_->SetDisplayId(option->GetDisplayId());
    property_->SetCallingWindow(option->GetCallingWindow());
    property_->SetWindowFlags(option->GetWindowFlags());
    property_->SetHitOffset(option->GetHitOffset());
    property_->SetRequestedOrientation(option->GetRequestedOrientation());
    windowTag_ = option->GetWindowTag();
    property_->SetTurnScreenOn(option->IsTurnScreenOn());
    property_->SetKeepScreenOn(option->IsKeepScreenOn());
    property_->SetBrightness(option->GetBrightness());
    AdjustWindowAnimationFlag();
    auto& sysBarPropMap = option->GetSystemBarProperty();
    for (auto it : sysBarPropMap) {
        property_->SetSystemBarProperty(it.first, it.second);
    }
    name_ = option->GetWindowName();
    callback_->onCallback = std::bind(&WindowImpl::OnVsync, this, std::placeholders::_1);

    struct RSSurfaceNodeConfig rsSurfaceNodeConfig;
    rsSurfaceNodeConfig.SurfaceNodeName = property_->GetWindowName();
    surfaceNode_ = RSSurfaceNode::Create(rsSurfaceNodeConfig);
    WLOGFI("WindowImpl constructorCnt: %{public}d name: %{public}s",
        ++constructorCnt, property_->GetWindowName().c_str());
}

void WindowImpl::InitListenerHandler()
{
    auto runner = AppExecFwk::EventRunner::Create(WM_CALLBACK_THREAD_NAME);
    if (runner == nullptr) {
        WLOGFE("init window callback runner failed.");
        return;
    }
    eventHandler_ = std::make_shared<AppExecFwk::EventHandler>(runner);
    if (eventHandler_ == nullptr) {
        WLOGFE("init window callback handler failed.");
        return;
    }
    isListenerHandlerRunning_ = true;
    WLOGFD("init window callback runner success.");
}

WindowImpl::~WindowImpl()
{
    if (eventHandler_ != nullptr) {
        eventHandler_.reset();
    }
    WLOGFI("windowName: %{public}s, windowId: %{public}d, deConstructorCnt: %{public}d, surfaceNode:%{public}d",
        GetWindowName().c_str(), GetWindowId(), ++deConstructorCnt, static_cast<uint32_t>(surfaceNode_.use_count()));
    Destroy();
}

sptr<Window> WindowImpl::Find(const std::string& name)
{
    auto iter = windowMap_.find(name);
    if (iter == windowMap_.end()) {
        return nullptr;
    }
    return iter->second.second;
}

const std::shared_ptr<AbilityRuntime::Context> WindowImpl::GetContext() const
{
    return context_;
}

sptr<Window> WindowImpl::FindTopWindow(uint32_t topWinId)
{
    if (windowMap_.empty()) {
        WLOGFE("Please create mainWindow First!");
        return nullptr;
    }
    for (auto iter = windowMap_.begin(); iter != windowMap_.end(); iter++) {
        if (topWinId == iter->second.first) {
            WLOGFI("FindTopWindow id: %{public}u", topWinId);
            return iter->second.second;
        }
    }
    WLOGFE("Cannot find topWindow!");
    return nullptr;
}

sptr<Window> WindowImpl::GetTopWindowWithId(uint32_t mainWinId)
{
    uint32_t topWinId = INVALID_WINDOW_ID;
    WMError ret = SingletonContainer::Get<WindowAdapter>().GetTopWindowId(mainWinId, topWinId);
    if (ret != WMError::WM_OK) {
        WLOGFE("GetTopWindowId failed with errCode:%{public}d", static_cast<int32_t>(ret));
        return nullptr;
    }
    return FindTopWindow(topWinId);
}

sptr<Window> WindowImpl::GetTopWindowWithContext(const std::shared_ptr<AbilityRuntime::Context>& context)
{
    if (windowMap_.empty()) {
        WLOGFE("Please create mainWindow First!");
        return nullptr;
    }
    uint32_t mainWinId = INVALID_WINDOW_ID;
    for (auto iter = windowMap_.begin(); iter != windowMap_.end(); iter++) {
        auto win = iter->second.second;
        if (context.get() == win->GetContext().get() && WindowHelper::IsMainWindow(win->GetType())) {
            mainWinId = win->GetWindowId();
            WLOGFI("GetTopWindow Find MainWinId:%{public}u.", mainWinId);
            break;
        }
    }
    WLOGFI("GetTopWindowfinal MainWinId:%{public}u!", mainWinId);
    if (mainWinId == INVALID_WINDOW_ID) {
        WLOGFE("Cannot find topWindow!");
        return nullptr;
    }
    uint32_t topWinId = INVALID_WINDOW_ID;
    WMError ret = SingletonContainer::Get<WindowAdapter>().GetTopWindowId(mainWinId, topWinId);
    if (ret != WMError::WM_OK) {
        WLOGFE("GetTopWindowId failed with errCode:%{public}d", static_cast<int32_t>(ret));
        return nullptr;
    }
    return FindTopWindow(topWinId);
}

std::vector<sptr<Window>> WindowImpl::GetSubWindow(uint32_t parentId)
{
    if (subWindowMap_.find(parentId) == subWindowMap_.end()) {
        WLOGFE("Cannot parentWindow with id: %{public}u!", parentId);
        return std::vector<sptr<Window>>();
    }
    return std::vector<sptr<Window>>(subWindowMap_[parentId].begin(), subWindowMap_[parentId].end());
}

std::shared_ptr<RSSurfaceNode> WindowImpl::GetSurfaceNode() const
{
    return surfaceNode_;
}

Rect WindowImpl::GetRect() const
{
    return property_->GetWindowRect();
}

Rect WindowImpl::GetRequestRect() const
{
    return property_->GetRequestRect();
}

WindowType WindowImpl::GetType() const
{
    return property_->GetWindowType();
}

WindowMode WindowImpl::GetMode() const
{
    return property_->GetWindowMode();
}

WindowBlurLevel WindowImpl::GetWindowBackgroundBlur() const
{
    return property_->GetWindowBackgroundBlur();
}

float WindowImpl::GetAlpha() const
{
    return property_->GetAlpha();
}

WindowState WindowImpl::GetWindowState() const
{
    return state_;
}

WMError WindowImpl::SetFocusable(bool isFocusable)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetFocusable(isFocusable);
    if (state_ == WindowState::STATE_SHOWN) {
        return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_FOCUSABLE);
    }
    return WMError::WM_OK;
}

bool WindowImpl::GetFocusable() const
{
    return property_->GetFocusable();
}

WMError WindowImpl::SetTouchable(bool isTouchable)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetTouchable(isTouchable);
    if (state_ == WindowState::STATE_SHOWN) {
        return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_TOUCHABLE);
    }
    return WMError::WM_OK;
}

bool WindowImpl::GetTouchable() const
{
    return property_->GetTouchable();
}

const std::string& WindowImpl::GetWindowName() const
{
    return name_;
}

uint32_t WindowImpl::GetWindowId() const
{
    return property_->GetWindowId();
}

uint32_t WindowImpl::GetWindowFlags() const
{
    return property_->GetWindowFlags();
}

uint32_t WindowImpl::GetRequestModeSupportInfo() const
{
    return property_->GetRequestModeSupportInfo();
}

uint32_t WindowImpl::GetModeSupportInfo() const
{
    return property_->GetModeSupportInfo();
}

SystemBarProperty WindowImpl::GetSystemBarPropertyByType(WindowType type) const
{
    auto curProperties = property_->GetSystemBarProperty();
    return curProperties[type];
}

WMError WindowImpl::GetAvoidAreaByType(AvoidAreaType type, AvoidArea& avoidArea)
{
    WLOGFI("GetAvoidAreaByType  Search Type: %{public}u", static_cast<uint32_t>(type));
    uint32_t windowId = property_->GetWindowId();
    WMError ret = SingletonContainer::Get<WindowAdapter>().GetAvoidAreaByType(windowId, type, avoidArea);
    if (ret != WMError::WM_OK) {
        WLOGFE("GetAvoidAreaByType errCode:%{public}d winId:%{public}u Type is :%{public}u.",
            static_cast<int32_t>(ret), property_->GetWindowId(), static_cast<uint32_t>(type));
    }
    return ret;
}

WMError WindowImpl::SetWindowType(WindowType type)
{
    WLOGFD("window id: %{public}u, type:%{public}u.", property_->GetWindowId(), static_cast<uint32_t>(type));
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (state_ == WindowState::STATE_CREATED) {
        if (!(WindowHelper::IsAppWindow(type) || WindowHelper::IsSystemWindow(type))) {
            WLOGFE("window type is invalid %{public}u.", type);
            return WMError::WM_ERROR_INVALID_PARAM;
        }
        property_->SetWindowType(type);
        if (isAppDecorEnable_ && windowSystemConfig_.isSystemDecorEnable_) {
            property_->SetDecorEnable(WindowHelper::IsMainWindow(property_->GetWindowType()));
        }
        AdjustWindowAnimationFlag();
        return WMError::WM_OK;
    }
    if (property_->GetWindowType() != type) {
        return WMError::WM_ERROR_INVALID_PARAM;
    }
    return WMError::WM_OK;
}

WMError WindowImpl::SetWindowMode(WindowMode mode)
{
    WLOGFI("[Client] Window %{public}u mode %{public}u", property_->GetWindowId(), static_cast<uint32_t>(mode));
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (!WindowHelper::IsWindowModeSupported(GetModeSupportInfo(), mode)) {
        WLOGFI("window %{public}u do not support window mode: %{public}u",
               property_->GetWindowId(), static_cast<uint32_t>(mode));
        return WMError::WM_DO_NOTHING;
    }
    if (state_ == WindowState::STATE_CREATED || state_ == WindowState::STATE_HIDDEN) {
        UpdateMode(mode);
    } else if (state_ == WindowState::STATE_SHOWN) {
        WindowMode lastMode = property_->GetWindowMode();
        property_->SetWindowMode(mode);
        WMError ret = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_MODE);
        if (ret != WMError::WM_OK) {
            property_->SetWindowMode(lastMode);
            return ret;
        }
        // set client window mode if success.
        UpdateMode(mode);
    }
    if (property_->GetWindowMode() != mode) {
        WLOGFE("set window mode filed! id: %{public}u.", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_PARAM;
    }
    return WMError::WM_OK;
}

WMError WindowImpl::SetWindowBackgroundBlur(WindowBlurLevel level)
{
    WLOGFI("[Client] Window %{public}u blurlevel %{public}u", property_->GetWindowId(), static_cast<uint32_t>(level));
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetWindowBackgroundBlur(level);
    return SingletonContainer::Get<WindowAdapter>().SetWindowBackgroundBlur(property_->GetWindowId(), level);
}

void WindowImpl::SetAlpha(float alpha)
{
    WLOGFI("[Client] Window %{public}u alpha %{public}f", property_->GetWindowId(), alpha);
    if (!IsWindowValid()) {
        return;
    }
    property_->SetAlpha(alpha);
    surfaceNode_->SetAlpha(alpha);
}

void WindowImpl::SetTransform(const Transform& trans)
{
    WLOGFI("[Client] Window %{public}u SetTransform", property_->GetWindowId());
    if (!IsWindowValid()) {
        return;
    }
    property_->SetTransform(trans);
    UpdateProperty(PropertyChangeAction::ACTION_UPDATE_TRANSFORM_PROPERTY);
}

Transform WindowImpl::GetTransform() const
{
    return property_->GetTransform();
}

WMError WindowImpl::AddWindowFlag(WindowFlag flag)
{
    if (flag == WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED && state_ != WindowState::STATE_CREATED) {
        WLOGFE("Only support add show when locked when window create, id: %{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_WINDOW;
    }

    uint32_t updateFlags = property_->GetWindowFlags() | (static_cast<uint32_t>(flag));
    return SetWindowFlags(updateFlags);
}

WMError WindowImpl::RemoveWindowFlag(WindowFlag flag)
{
    if (flag == WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED && state_ != WindowState::STATE_CREATED) {
        WLOGFE("Only support remove show when locked when window create, id: %{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_WINDOW;
    }

    uint32_t updateFlags = property_->GetWindowFlags() & (~(static_cast<uint32_t>(flag)));
    return SetWindowFlags(updateFlags);
}

WMError WindowImpl::SetWindowFlags(uint32_t flags)
{
    WLOGFI("[Client] Window %{public}u flags %{public}u", property_->GetWindowId(), flags);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (property_->GetWindowFlags() == flags) {
        return WMError::WM_OK;
    }
    property_->SetWindowFlags(flags);
    if (state_ == WindowState::STATE_CREATED || state_ == WindowState::STATE_HIDDEN) {
        return WMError::WM_OK;
    }
    WMError ret = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_FLAGS);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetWindowFlags errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
    }
    return ret;
}

void WindowImpl::OnNewWant(const AAFwk::Want& want)
{
    WLOGFD("[Client] Window [name:%{public}s, id:%{public}u] OnNewWant", name_.c_str(), property_->GetWindowId());
    if (uiContent_ != nullptr) {
        uiContent_->OnNewWant(want);
    }
}

WMError WindowImpl::SetUIContent(const std::string& contentInfo,
    NativeEngine* engine, NativeValue* storage, bool isdistributed, AppExecFwk::Ability* ability)
{
    WLOGFD("SetUIContent contentInfo: %{public}s", contentInfo.c_str());
    std::unique_ptr<Ace::UIContent> uiContent;
    if (ability != nullptr) {
        uiContent = Ace::UIContent::Create(ability);
    } else {
        uiContent = Ace::UIContent::Create(context_.get(), engine);
    }
    if (uiContent == nullptr) {
        WLOGFE("fail to SetUIContent id: %{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_NULLPTR;
    }
    if (!isAppDecorEnable_ || !windowSystemConfig_.isSystemDecorEnable_) {
        WLOGFI("app set decor enable false");
        property_->SetDecorEnable(false);
    }
    if (isdistributed) {
        uiContent->Restore(this, contentInfo, storage);
    } else {
        uiContent->Initialize(this, contentInfo, storage);
    }
    // make uiContent available after Initialize/Restore
    uiContent_ = std::move(uiContent);

    if (state_ == WindowState::STATE_SHOWN) {
        UpdateTitleButtonVisibility();
        Ace::ViewportConfig config;
        Rect rect = GetRect();
        config.SetSize(rect.width_, rect.height_);
        config.SetPosition(rect.posX_, rect.posY_);
        auto display = DisplayManager::GetInstance().GetDisplayById(property_->GetDisplayId());
        if (display == nullptr) {
            WLOGFE("get display failed displayId:%{public}" PRIu64", window id:%{public}u", property_->GetDisplayId(),
                property_->GetWindowId());
            return WMError::WM_ERROR_NULLPTR;
        }
        float virtualPixelRatio = display->GetVirtualPixelRatio();
        config.SetDensity(virtualPixelRatio);
        uiContent_->UpdateViewportConfig(config, WindowSizeChangeReason::UNDEFINED);
        WLOGFD("notify uiContent window size change end");
    }
    return WMError::WM_OK;
}

Ace::UIContent* WindowImpl::GetUIContent() const
{
    return uiContent_.get();
}

std::string WindowImpl::GetContentInfo()
{
    WLOGFD("GetContentInfo");
    if (uiContent_ == nullptr) {
        WLOGFE("fail to GetContentInfo id: %{public}u", property_->GetWindowId());
        return "";
    }
    return uiContent_->GetContentInfo();
}

ColorSpace WindowImpl::GetColorSpaceFromSurfaceGamut(ColorGamut ColorGamut)
{
    for (auto item: colorSpaceConvertMap) {
        if (item.surfaceColorGamut == ColorGamut) {
            return item.colorSpace;
        }
    }
    return ColorSpace::COLOR_SPACE_DEFAULT;
}

ColorGamut WindowImpl::GetSurfaceGamutFromColorSpace(ColorSpace colorSpace)
{
    for (auto item: colorSpaceConvertMap) {
        if (item.colorSpace == colorSpace) {
            return item.surfaceColorGamut;
        }
    }
    return ColorGamut::COLOR_GAMUT_SRGB;
}

bool WindowImpl::IsSupportWideGamut()
{
    return true;
}

void WindowImpl::SetColorSpace(ColorSpace colorSpace)
{
    auto surfaceGamut = GetSurfaceGamutFromColorSpace(colorSpace);
    surfaceNode_->SetColorSpace(surfaceGamut);
}

ColorSpace WindowImpl::GetColorSpace()
{
    auto surfaceGamut = surfaceNode_->GetColorSpace();
    return GetColorSpaceFromSurfaceGamut(surfaceGamut);
}

std::shared_ptr<Media::PixelMap> WindowImpl::Snapshot()
{
    WLOGFD("WMS-Clinet Snapshot");
    std::shared_ptr<SurfaceCaptureFuture> callback = std::make_shared<SurfaceCaptureFuture>();
    RSInterfaces::GetInstance().TakeSurfaceCapture(surfaceNode_, callback);
    std::shared_ptr<Media::PixelMap> pixelMap = callback->GetResult(2000); // wait for <= 2000ms
    if (pixelMap != nullptr) {
        WLOGFI("WMS-Clinet Save WxH = %{public}dx%{public}d", pixelMap->GetWidth(), pixelMap->GetHeight());
    } else {
        WLOGFE("Failed to get pixelmap, return nullptr!");
    }
    return pixelMap;
}

void WindowImpl::DumpInfo(const std::vector<std::string>& params, std::vector<std::string>& info)
{
    if (params.size() == 1 && params[0] == PARAM_DUMP_HELP) { // 1: params num
        WLOGFI("Dump ArkUI help Info");
        Ace::UIContent::ShowDumpHelp(info);
        return;
    }
    WLOGFI("ArkUI:DumpInfo");
    if (uiContent_ != nullptr) {
        uiContent_->DumpInfo(params, info);
    }
}

WMError WindowImpl::SetSystemBarProperty(WindowType type, const SystemBarProperty& property)
{
    WLOGFI("[Client] Window %{public}u SetSystemBarProperty type %{public}u " \
        "enable:%{public}u, backgroundColor:%{public}x, contentColor:%{public}x ",
        property_->GetWindowId(), static_cast<uint32_t>(type), property.enable_,
        property.backgroundColor_, property.contentColor_);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (GetSystemBarPropertyByType(type) == property) {
        return WMError::WM_OK;
    }
    property_->SetSystemBarProperty(type, property);
    if (state_ == WindowState::STATE_CREATED || state_ == WindowState::STATE_HIDDEN) {
        return WMError::WM_OK;
    }
    WMError ret = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_OTHER_PROPS);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetSystemBarProperty errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
    }
    return ret;
}

WMError WindowImpl::SetLayoutFullScreen(bool status)
{
    WLOGFI("[Client] Window %{public}u SetLayoutFullScreen: %{public}u", property_->GetWindowId(), status);
    if (!IsWindowValid() ||
        !WindowHelper::IsWindowModeSupported(GetModeSupportInfo(), WindowMode::WINDOW_MODE_FULLSCREEN)) {
        WLOGFE("invalid window or fullscreen mode is not be supported, winId:%{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    WMError ret = SetWindowMode(WindowMode::WINDOW_MODE_FULLSCREEN);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetWindowMode errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
        return ret;
    }
    if (status) {
        ret = RemoveWindowFlag(WindowFlag::WINDOW_FLAG_NEED_AVOID);
        if (ret != WMError::WM_OK) {
            WLOGFE("RemoveWindowFlag errCode:%{public}d winId:%{public}u",
                static_cast<int32_t>(ret), property_->GetWindowId());
            return ret;
        }
    } else {
        ret = AddWindowFlag(WindowFlag::WINDOW_FLAG_NEED_AVOID);
        if (ret != WMError::WM_OK) {
            WLOGFE("AddWindowFlag errCode:%{public}d winId:%{public}u",
                static_cast<int32_t>(ret), property_->GetWindowId());
            return ret;
        }
    }
    return ret;
}

WMError WindowImpl::SetFullScreen(bool status)
{
    WLOGFI("[Client] Window %{public}u SetFullScreen: %{public}d", property_->GetWindowId(), status);
    if (!IsWindowValid() ||
        !WindowHelper::IsWindowModeSupported(GetModeSupportInfo(), WindowMode::WINDOW_MODE_FULLSCREEN)) {
        WLOGFE("invalid window or fullscreen mode is not be supported, winId:%{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    SystemBarProperty statusProperty = GetSystemBarPropertyByType(
        WindowType::WINDOW_TYPE_STATUS_BAR);
    SystemBarProperty naviProperty = GetSystemBarPropertyByType(
        WindowType::WINDOW_TYPE_NAVIGATION_BAR);
    if (status) {
        statusProperty.enable_ = false;
        naviProperty.enable_ = false;
    } else {
        statusProperty.enable_ = true;
        naviProperty.enable_ = true;
    }
    WMError ret = SetSystemBarProperty(WindowType::WINDOW_TYPE_STATUS_BAR, statusProperty);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetSystemBarProperty errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
    }
    ret = SetSystemBarProperty(WindowType::WINDOW_TYPE_NAVIGATION_BAR, naviProperty);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetSystemBarProperty errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
    }
    ret = SetLayoutFullScreen(status);
    if (ret != WMError::WM_OK) {
        WLOGFE("SetLayoutFullScreen errCode:%{public}d winId:%{public}u",
            static_cast<int32_t>(ret), property_->GetWindowId());
    }
    return ret;
}

void WindowImpl::MapFloatingWindowToAppIfNeeded()
{
    if (!WindowHelper::IsAppFloatingWindow(GetType()) || context_.get() == nullptr) {
        return;
    }

    for (auto& winPair : windowMap_) {
        auto win = winPair.second.second;
        if (win->GetType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW &&
            context_.get() == win->GetContext().get()) {
            appFloatingWindowMap_[win->GetWindowId()].push_back(this);
            WLOGFI("Map FloatingWindow %{public}u to AppMainWindow %{public}u, type is %{public}u",
                GetWindowId(), win->GetWindowId(), GetType());
            return;
        }
    }
}

WMError WindowImpl::UpdateProperty(PropertyChangeAction action)
{
    return SingletonContainer::Get<WindowAdapter>().UpdateProperty(property_, action);
}

void WindowImpl::GetConfigurationFromAbilityInfo()
{
    auto abilityContext = AbilityRuntime::Context::ConvertTo<AbilityRuntime::AbilityContext>(context_);
    if (abilityContext == nullptr) {
        WLOGFE("id:%{public}u is not ability Window", property_->GetWindowId());
        return;
    }
    auto abilityInfo = abilityContext->GetAbilityInfo();
    if (abilityInfo == nullptr) {
        WLOGFE("id:%{public}u Ability window get ability info failed", property_->GetWindowId());
        return;
    }

    // get support modes configuration
    uint32_t modeSupportInfo = 0;
    WindowHelper::ConvertSupportModesToSupportInfo(modeSupportInfo, abilityInfo->windowModes);
    if (modeSupportInfo == 0) {
        WLOGFD("mode config param is 0, all modes is supported");
        modeSupportInfo = WindowModeSupport::WINDOW_MODE_SUPPORT_ALL;
    }
    WLOGFD("winId: %{public}u, modeSupportInfo: %{public}u", GetWindowId(), modeSupportInfo);
    SetRequestModeSupportInfo(modeSupportInfo);

    // get window size limits configuration
    WindowSizeLimits sizeLimits;
    sizeLimits.maxWidth_ = abilityInfo->maxWindowWidth;
    sizeLimits.maxHeight_ = abilityInfo->maxWindowHeight;
    sizeLimits.minWidth_ = abilityInfo->minWindowWidth;
    sizeLimits.minHeight_ = abilityInfo->minWindowHeight;
    sizeLimits.maxRatio_ = static_cast<float>(abilityInfo->maxWindowRatio);
    sizeLimits.minRatio_ = static_cast<float>(abilityInfo->minWindowRatio);
    property_->SetSizeLimits(sizeLimits);

    // get orientation configuration
    DisplayOrientation displayOrientation = static_cast<DisplayOrientation>(
        static_cast<uint32_t>(abilityInfo->orientation));
    if (ABILITY_TO_WMS_ORIENTATION_MAP.count(displayOrientation) == 0) {
        WLOGFE("id:%{public}u Do not support this Orientation type", property_->GetWindowId());
        return;
    }
    Orientation orientation = ABILITY_TO_WMS_ORIENTATION_MAP.at(displayOrientation);
    if (orientation < Orientation::BEGIN || orientation > Orientation::END) {
        WLOGFE("Set orientation from ability failed");
        return;
    }
    property_->SetRequestedOrientation(orientation);
}

void WindowImpl::UpdateTitleButtonVisibility()
{
    WLOGFI("[Client] UpdateTitleButtonVisibility");
    if (uiContent_ == nullptr || !isAppDecorEnable_) {
        return;
    }
    auto modeSupportInfo = GetModeSupportInfo();
    bool hideSplitButton = !(modeSupportInfo & WindowModeSupport::WINDOW_MODE_SUPPORT_SPLIT_PRIMARY);
    // not support fullscreen in split and floating mode, or not support float in fullscreen mode
    bool hideMaximizeButton = (!(modeSupportInfo & WindowModeSupport::WINDOW_MODE_SUPPORT_FULLSCREEN) &&
        (GetMode() == WindowMode::WINDOW_MODE_FLOATING || WindowHelper::IsSplitWindowMode(GetMode()))) ||
        (!(modeSupportInfo & WindowModeSupport::WINDOW_MODE_SUPPORT_FLOATING) &&
        GetMode() == WindowMode::WINDOW_MODE_FULLSCREEN);
    WLOGFI("[Client] [hideSplit, hideMaximize]: [%{public}d, %{public}d]", hideSplitButton, hideMaximizeButton);
    uiContent_->HideWindowTitleButton(hideSplitButton, hideMaximizeButton, false);
}

bool WindowImpl::IsAppMainOrSubOrFloatingWindow()
{
    // App main window need decor config, stretchable config and effect config
    // App sub window and float window need effect config
    if (WindowHelper::IsAppWindow(GetType())) {
        return true;
    }

    if (WindowHelper::IsAppFloatingWindow(GetType())) {
        for (auto& winPair : windowMap_) {
            auto win = winPair.second.second;
            if (win != nullptr && win->GetType() == WindowType::WINDOW_TYPE_APP_MAIN_WINDOW &&
                context_.get() == win->GetContext().get()) {
                isAppFloatingWindow_ = true;
                return true;
            }
        }
    }
    return false;
}

void WindowImpl::SetSystemConfig()
{
    if (!IsAppMainOrSubOrFloatingWindow()) {
        return;
    }
    if (SingletonContainer::Get<WindowAdapter>().GetSystemConfig(windowSystemConfig_) == WMError::WM_OK) {
        if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
            WLOGFD("get system decor enable:%{public}d", windowSystemConfig_.isSystemDecorEnable_);
            property_->SetDecorEnable(windowSystemConfig_.isSystemDecorEnable_);
            WLOGFD("get stretchable enable:%{public}d", windowSystemConfig_.isStretchable_);
            property_->SetStretchable(windowSystemConfig_.isStretchable_);
            // if window mode is undefined, set it from configuration
            if (property_->GetWindowMode() == WindowMode::WINDOW_MODE_UNDEFINED) {
                WLOGFD("get default window mode:%{public}u", windowSystemConfig_.defaultWindowMode_);
                property_->SetWindowMode(windowSystemConfig_.defaultWindowMode_);
            }
            if (property_->GetLastWindowMode() == WindowMode::WINDOW_MODE_UNDEFINED) {
                property_->SetLastWindowMode(windowSystemConfig_.defaultWindowMode_);
            }
        }
    }
}

bool WindowImpl::WindowCreateCheck(const std::string& parentName)
{
    // check window name, same window names are forbidden
    if (windowMap_.find(name_) != windowMap_.end()) {
        WLOGFE("WindowName(%{public}s) already exists.", name_.c_str());
        return false;
    }
    // check parent name, if create sub window and there is not exist parent Window, then return
    if (parentName != "") {
        if (windowMap_.find(parentName) == windowMap_.end()) {
            WLOGFE("ParentName is empty or valid. ParentName is %{public}s", parentName.c_str());
            return false;
        } else {
            uint32_t parentId = windowMap_[parentName].first;
            property_->SetParentId(parentId);
        }
    }

    if (CheckCameraFloatingWindowMultiCreated(property_->GetWindowType())) {
        WLOGFE("Camera Floating Window already exists.");
        return false;
    }
    return true;
}

WMError WindowImpl::Create(const std::string& parentName, const std::shared_ptr<AbilityRuntime::Context>& context)
{
    WLOGFI("[Client] Window [name:%{public}s] Create", name_.c_str());
    if (!WindowCreateCheck(parentName)) {
        return WMError::WM_ERROR_INVALID_PARAM;
    }

    context_ = context;
    sptr<WindowImpl> window(this);
    sptr<IWindow> windowAgent(new WindowAgent(window));
    static std::atomic<uint32_t> tempWindowId = 0;
    uint32_t windowId = tempWindowId++; // for test
    sptr<IRemoteObject> token = nullptr;
    if (context_ != nullptr) {
        token = context_->GetToken();
        if (token != nullptr) {
            property_->SetTokenState(true);
        }
    }
    SetSystemConfig();

    if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
        GetConfigurationFromAbilityInfo();
    } else if (property_->GetWindowMode() == WindowMode::WINDOW_MODE_UNDEFINED) {
        property_->SetWindowMode(WindowMode::WINDOW_MODE_FULLSCREEN);
    }

    WMError ret = SingletonContainer::Get<WindowAdapter>().CreateWindow(windowAgent, property_, surfaceNode_,
        windowId, token);
    RecordLifeCycleExceptionEvent(LifeCycleEvent::CREATE_EVENT, ret);
    if (ret != WMError::WM_OK) {
        WLOGFE("create window failed with errCode:%{public}d", static_cast<int32_t>(ret));
        return ret;
    }
    property_->SetWindowId(windowId);
    windowMap_.insert(std::make_pair(name_, std::pair<uint32_t, sptr<Window>>(windowId, this)));
    if (parentName != "") { // add to subWindowMap_
        subWindowMap_[property_->GetParentId()].push_back(this);
    }

    MapFloatingWindowToAppIfNeeded();

    state_ = WindowState::STATE_CREATED;
    InputTransferStation::GetInstance().AddInputWindow(this);
    needRemoveWindowInputChannel_ = true;
    return ret;
}

void WindowImpl::DestroyFloatingWindow()
{
    // remove from appFloatingWindowMap_
    for (auto& floatingWindows: appFloatingWindowMap_) {
        for (auto iter = floatingWindows.second.begin(); iter != floatingWindows.second.end(); ++iter) {
            if ((*iter) == nullptr) {
                continue;
            }
            if ((*iter)->GetWindowId() == GetWindowId()) {
                floatingWindows.second.erase(iter);
                break;
            }
        }
    }

    // Destroy app floating window if exist
    if (appFloatingWindowMap_.count(GetWindowId()) > 0) {
        auto& floatingWindows = appFloatingWindowMap_.at(GetWindowId());
        for (auto iter = floatingWindows.begin(); iter != floatingWindows.end(); iter = floatingWindows.begin()) {
            if ((*iter) == nullptr) {
                floatingWindows.erase(iter);
                continue;
            }
            (*iter)->Destroy();
        }
        appFloatingWindowMap_.erase(GetWindowId());
    }
}

void WindowImpl::DestroySubWindow()
{
    if (subWindowMap_.count(property_->GetParentId()) > 0) { // remove from subWindowMap_
        std::vector<sptr<WindowImpl>>& subWindows = subWindowMap_.at(property_->GetParentId());
        for (auto iter = subWindows.begin(); iter < subWindows.end(); ++iter) {
            if ((*iter) == nullptr) {
                continue;
            }
            if ((*iter)->GetWindowId() == GetWindowId()) {
                subWindows.erase(iter);
                break;
            }
        }
    }

    if (subWindowMap_.count(GetWindowId()) > 0) { // remove from subWindowMap_ and windowMap_
        auto& subWindows = subWindowMap_.at(GetWindowId());
        for (auto iter = subWindows.begin(); iter != subWindows.end(); iter = subWindows.begin()) {
            if ((*iter) == nullptr) {
                subWindows.erase(iter);
                continue;
            }
            (*iter)->Destroy(false);
        }
        subWindowMap_[GetWindowId()].clear();
        subWindowMap_.erase(GetWindowId());
    }
}

WMError WindowImpl::Destroy()
{
    return Destroy(true);
}

void WindowImpl::PostListenerTask(ListenerTaskCallback &&callback, Priority priority,
    const std::string taskName)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!isListenerHandlerRunning_) {
            InitListenerHandler();
        }
    }
    bool ret = eventHandler_->PostTask([this, callback]() {
            callback();
        }, taskName, 0, priority);
    if (!ret) {
        WLOGE("post listener callback task failed.");
        return;
    }
    WLOGD("post listener callback task: %{public}s success.", taskName.c_str());
    return;
}

WMError WindowImpl::Destroy(bool needNotifyServer)
{
    if (!IsWindowValid()) {
        return WMError::WM_OK;
    }

    WLOGFI("[Client] Window %{public}u Destroy", property_->GetWindowId());
    WMError ret = WMError::WM_OK;
    if (needNotifyServer) {
        NotifyBeforeDestroy(GetWindowName());
        if (subWindowMap_.count(GetWindowId()) > 0) {
            for (auto& subWindow : subWindowMap_.at(GetWindowId())) {
                NotifyBeforeSubWindowDestroy(subWindow);
            }
        }
        ret = SingletonContainer::Get<WindowAdapter>().DestroyWindow(property_->GetWindowId());
        RecordLifeCycleExceptionEvent(LifeCycleEvent::DESTROY_EVENT, ret);
        if (ret != WMError::WM_OK) {
            WLOGFE("destroy window failed with errCode:%{public}d", static_cast<int32_t>(ret));
            return ret;
        }
    } else {
        WLOGFI("Do not need to notify server to destroy window");
    }

    if (needRemoveWindowInputChannel_) {
        InputTransferStation::GetInstance().RemoveInputWindow(property_->GetWindowId());
    }
    windowMap_.erase(GetWindowName());
    DestroySubWindow();
    DestroyFloatingWindow();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = WindowState::STATE_DESTROYED;
        if (isWaitingFrame_) {
            VsyncStation::GetInstance().RemoveCallback(VsyncStation::CallbackType::CALLBACK_FRAME, callback_);
        }
    }
    return ret;
}

bool WindowImpl::NeedToStopShowing()
{
    if (!WindowHelper::IsMainWindow(property_->GetWindowType())) {
        return false;
    }
    // show failed when current mode is not support or window only supports split mode and can show when locked
    bool isShowWhenLocked = GetWindowFlags() & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED);
    if (!WindowHelper::IsWindowModeSupported(GetModeSupportInfo(), GetMode()) ||
        WindowHelper::IsOnlySupportSplitAndShowWhenLocked(isShowWhenLocked, GetModeSupportInfo())) {
        WLOGFE("current mode is not supported, windowId: %{public}u, modeSupportInfo: %{public}u, winMode: %{public}u",
            property_->GetWindowId(), GetModeSupportInfo(), GetMode());
        return true;
    }
    return false;
}

WMError WindowImpl::UpdateSurfaceNodeAfterCustomAnimation(bool isAdd)
{
    WLOGFI("[Client] Window [name:%{public}s, id:%{public}u] UpdateRsTree, isAdd:%{public}u",
        name_.c_str(), property_->GetWindowId(), isAdd);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (!WindowHelper::IsSystemWindow(property_->GetWindowType())) {
        WLOGFE("only system window can set");
        return WMError::WM_ERROR_INVALID_OPERATION;
    }
    AdjustWindowAnimationFlag(false); // false means update rs tree with default option
    // need time out check
    WMError ret = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_ANIMATION_FLAG);
    if (ret != WMError::WM_OK) {
        WLOGFE("UpdateProperty failed with errCode:%{public}d", static_cast<int32_t>(ret));
        return ret;
    }
    ret = SingletonContainer::Get<WindowAdapter>().UpdateRsTree(property_->GetWindowId(), isAdd);
    if (ret != WMError::WM_OK) {
        WLOGFE("UpdateRsTree failed with errCode:%{public}d", static_cast<int32_t>(ret));
        return ret;
    }
    return WMError::WM_OK;
}

WMError WindowImpl::PreProcessShow(uint32_t reason, bool withAnimation)
{
    if (state_ == WindowState::STATE_FROZEN) {
        WLOGFE("window is frozen, can not be shown, windowId: %{public}u", property_->GetWindowId());
        return WMError::WM_ERROR_INVALID_OPERATION;
    }
    SetDefaultOption();
    AdjustWindowAnimationFlag(withAnimation);

    if (NeedToStopShowing()) { // true means stop showing
        NotifyForegroundInvalidWindowMode();
        return WMError::WM_ERROR_INVALID_WINDOW_MODE_OR_SIZE;
    }

    // update title button visibility when show
    UpdateTitleButtonVisibility();
    return WMError::WM_OK;
}

WMError WindowImpl::Show(uint32_t reason, bool withAnimation)
{
    WLOGFI("[Client] Window [name:%{public}s, id:%{public}u] Show, reason:%{public}u, withAnimation:%{public}d",
        name_.c_str(), property_->GetWindowId(), reason, withAnimation);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    WindowStateChangeReason stateChangeReason = static_cast<WindowStateChangeReason>(reason);
    if (stateChangeReason == WindowStateChangeReason::KEYGUARD ||
        stateChangeReason == WindowStateChangeReason::TOGGLING) {
        state_ = WindowState::STATE_SHOWN;
        NotifyAfterForeground();
        return WMError::WM_OK;
    }
    if (state_ == WindowState::STATE_SHOWN) {
        if (property_->GetWindowType() == WindowType::WINDOW_TYPE_DESKTOP) {
            WLOGFI("desktop window [id:%{public}u] is shown, minimize all app windows", property_->GetWindowId());
            SingletonContainer::Get<WindowAdapter>().MinimizeAllAppWindows(property_->GetDisplayId());
        } else {
            WLOGFI("window is already shown id: %{public}u, raise to top", property_->GetWindowId());
            SingletonContainer::Get<WindowAdapter>().ProcessPointDown(property_->GetWindowId());
        }
        NotifyAfterUnfocused(false);
        return WMError::WM_OK;
    }
    WMError ret = PreProcessShow(reason, withAnimation);
    if (ret != WMError::WM_OK) {
        return ret;
    }

    ret = SingletonContainer::Get<WindowAdapter>().AddWindow(property_);
    RecordLifeCycleExceptionEvent(LifeCycleEvent::SHOW_EVENT, ret);
    if (ret == WMError::WM_OK || ret == WMError::WM_ERROR_DEATH_RECIPIENT) {
        state_ = WindowState::STATE_SHOWN;
        NotifyAfterForeground();
        uint32_t animationFlag = property_->GetAnimationFlag();
        if (animationFlag == static_cast<uint32_t>(WindowAnimation::CUSTOM)) {
            // CustomAnimation is enabled when animationTranistionController_ exists
            animationTranistionController_->AnimationForShown();
        }
    } else if (ret == WMError::WM_ERROR_INVALID_WINDOW_MODE_OR_SIZE) {
        NotifyForegroundInvalidWindowMode();
    } else {
        NotifyForegroundFailed();
        WLOGFE("show window id:%{public}u errCode:%{public}d", property_->GetWindowId(), static_cast<int32_t>(ret));
    }
    return ret;
}

WMError WindowImpl::Hide(uint32_t reason, bool withAnimation)
{
    WLOGFD("[Client] Window [name:%{public}s, id:%{public}u] Hide, reason:%{public}u, withAnimation:%{public}d",
        name_.c_str(), property_->GetWindowId(), reason, withAnimation);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    WindowStateChangeReason stateChangeReason = static_cast<WindowStateChangeReason>(reason);
    if (stateChangeReason == WindowStateChangeReason::KEYGUARD ||
        stateChangeReason == WindowStateChangeReason::TOGGLING) {
        state_ = stateChangeReason == WindowStateChangeReason::KEYGUARD ?
            WindowState::STATE_FROZEN : WindowState::STATE_HIDDEN;
        NotifyAfterBackground();
        return WMError::WM_OK;
    }
    if (state_ == WindowState::STATE_HIDDEN || state_ == WindowState::STATE_CREATED) {
        WLOGFI("window is already hidden id: %{public}u", property_->GetWindowId());
        return WMError::WM_OK;
    }
    WMError ret = WMError::WM_OK;
    if (WindowHelper::IsSystemWindow(property_->GetWindowType())) {
        AdjustWindowAnimationFlag(withAnimation);
        // when show(true) with default, hide() with None, to adjust animationFlag to disabled default animation
        WMError ret = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_ANIMATION_FLAG);
        if (ret != WMError::WM_OK) {
            WLOGFE("UpdateProperty failed with errCode:%{public}d", static_cast<int32_t>(ret));
            return ret;
        }
    }
    ret = SingletonContainer::Get<WindowAdapter>().RemoveWindow(property_->GetWindowId());
    RecordLifeCycleExceptionEvent(LifeCycleEvent::HIDE_EVENT, ret);
    if (ret != WMError::WM_OK) {
        WLOGFE("hide errCode:%{public}d for winId:%{public}u", static_cast<int32_t>(ret), property_->GetWindowId());
        return ret;
    }
    state_ = WindowState::STATE_HIDDEN;
    NotifyAfterBackground();
    uint32_t animationFlag = property_->GetAnimationFlag();
    if (animationFlag == static_cast<uint32_t>(WindowAnimation::CUSTOM)) {
        animationTranistionController_->AnimationForHidden();
    }
    return ret;
}

WMError WindowImpl::MoveTo(int32_t x, int32_t y)
{
    WLOGFD("[Client] Window [name:%{public}s, id:%{public}d] MoveTo %{public}d %{public}d",
        name_.c_str(), property_->GetWindowId(), x, y);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    Rect rect = property_->GetRequestRect();
    Rect moveRect = { x, y, rect.width_, rect.height_ }; // must keep w/h, which may maintain stashed resize info
    property_->SetRequestRect(moveRect);
    if (state_ == WindowState::STATE_HIDDEN || state_ == WindowState::STATE_CREATED) {
        WLOGFI("window is hidden or created! id: %{public}u, oriPos: [%{public}d, %{public}d, "
               "movePos: [%{public}d, %{public}d]", property_->GetWindowId(), rect.posX_, rect.posY_, x, y);
        return WMError::WM_OK;
    }
    property_->SetWindowSizeChangeReason(WindowSizeChangeReason::MOVE);
    return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_RECT);
}

WMError WindowImpl::Resize(uint32_t width, uint32_t height)
{
    WLOGFD("[Client] Window [name:%{public}s, id:%{public}d] Resize %{public}u %{public}u",
        name_.c_str(), property_->GetWindowId(), width, height);
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }

    Rect rect = property_->GetRequestRect();
    Rect resizeRect = { rect.posX_, rect.posY_, width, height };
    property_->SetRequestRect(resizeRect);
    property_->SetDecoStatus(false);
    if (state_ == WindowState::STATE_HIDDEN || state_ == WindowState::STATE_CREATED) {
        WLOGFI("window is hidden or created! id: %{public}u, oriRect: [%{public}u, %{public}u], "
               "resizeRect: [%{public}u, %{public}u]", property_->GetWindowId(), rect.width_,
               rect.height_, width, height);
        return WMError::WM_OK;
    }
    property_->SetWindowSizeChangeReason(WindowSizeChangeReason::RESIZE);
    return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_RECT);
}

WMError WindowImpl::SetKeepScreenOn(bool keepScreenOn)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetKeepScreenOn(keepScreenOn);
    if (state_ == WindowState::STATE_SHOWN) {
        return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_KEEP_SCREEN_ON);
    }
    return WMError::WM_OK;
}

bool WindowImpl::IsKeepScreenOn() const
{
    return property_->IsKeepScreenOn();
}

WMError WindowImpl::SetTurnScreenOn(bool turnScreenOn)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetTurnScreenOn(turnScreenOn);
    if (state_ == WindowState::STATE_SHOWN) {
        return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_TURN_SCREEN_ON);
    }
    return WMError::WM_OK;
}

bool WindowImpl::IsTurnScreenOn() const
{
    return property_->IsTurnScreenOn();
}

WMError WindowImpl::SetBackgroundColor(uint32_t color)
{
    if (uiContent_ != nullptr) {
        uiContent_->SetBackgroundColor(color);
        return WMError::WM_OK;
    }
    WLOGFI("uiContent is nullptr, windowId: %{public}u, use FA mode", GetWindowId());
    if (aceAbilityHandler_ != nullptr) {
        aceAbilityHandler_->SetBackgroundColor(color);
        return WMError::WM_OK;
    }
    WLOGFE("FA mode could not set background color: %{public}u", GetWindowId());
    return WMError::WM_ERROR_INVALID_OPERATION;
}

uint32_t WindowImpl::GetBackgroundColor() const
{
    if (uiContent_ != nullptr) {
        return uiContent_->GetBackgroundColor();
    }
    WLOGFI("uiContent is nullptr, windowId: %{public}u, use FA mode", GetWindowId());
    if (aceAbilityHandler_ != nullptr) {
        return aceAbilityHandler_->GetBackgroundColor();
    }
    WLOGFE("FA mode does not get background color: %{public}u", GetWindowId());
    return 0xffffffff; // means no background color been set, default color is white
}

WMError WindowImpl::SetBackgroundColor(const std::string& color)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    uint32_t colorValue;
    if (ColorParser::Parse(color, colorValue)) {
        WLOGFI("SetBackgroundColor: window: %{public}s, value: [%{public}s, %{public}u]",
            name_.c_str(), color.c_str(), colorValue);
        return SetBackgroundColor(colorValue);
    }
    WLOGFE("invalid color string: %{public}s", color.c_str());
    return WMError::WM_ERROR_INVALID_PARAM;
}

WMError WindowImpl::SetTransparent(bool isTransparent)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    ColorParam backgroundColor;
    backgroundColor.value = GetBackgroundColor();
    if (isTransparent) {
        backgroundColor.argb.alpha = 0x00; // 0x00: completely transparent
        return SetBackgroundColor(backgroundColor.value);
    } else {
        backgroundColor.value = GetBackgroundColor();
        if (backgroundColor.argb.alpha == 0x00) {
            backgroundColor.argb.alpha = 0xff; // 0xff: completely opaque
            return SetBackgroundColor(backgroundColor.value);
        }
    }
    return WMError::WM_OK;
}

bool WindowImpl::IsTransparent() const
{
    ColorParam backgroundColor;
    backgroundColor.value = GetBackgroundColor();
    WLOGFI("color: %{public}u, alpha: %{public}u", backgroundColor.value, backgroundColor.argb.alpha);
    return backgroundColor.argb.alpha == 0x00; // 0x00: completely transparent
}

WMError WindowImpl::SetBrightness(float brightness)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (brightness < MINIMUM_BRIGHTNESS || brightness > MAXIMUM_BRIGHTNESS) {
        WLOGFE("invalid brightness value: %{public}f", brightness);
        return WMError::WM_ERROR_INVALID_PARAM;
    }
    if (!WindowHelper::IsAppWindow(GetType())) {
        WLOGFE("non app window does not support set brightness, type: %{public}u", GetType());
        return WMError::WM_ERROR_INVALID_TYPE;
    }
    property_->SetBrightness(brightness);
    if (state_ == WindowState::STATE_SHOWN) {
        return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_SET_BRIGHTNESS);
    }
    return WMError::WM_OK;
}

float WindowImpl::GetBrightness() const
{
    return property_->GetBrightness();
}

WMError WindowImpl::SetCallingWindow(uint32_t windowId)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    property_->SetCallingWindow(windowId);
    return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_CALLING_WINDOW);
}

void WindowImpl::RecordLifeCycleExceptionEvent(LifeCycleEvent event, WMError errCode) const
{
    if (!(errCode > WMError::WM_ERROR_NEED_REPORT_BASE && errCode < WMError::WM_ERROR_NEED_REPORT_END)) {
        return;
    }
    std::ostringstream oss;
    oss << "life cycle is abnormal: " << "window_name: " << name_
        << ", id:" << GetWindowId() << ", event: " << TransferLifeCycleEventToString(event)
        << ", errCode: " << static_cast<int32_t>(errCode) << ";";
    std::string info = oss.str();
    WLOGFI("window life cycle exception: %{public}s", info.c_str());
    int32_t ret = OHOS::HiviewDFX::HiSysEvent::Write(
        OHOS::HiviewDFX::HiSysEvent::Domain::WINDOW_MANAGER,
        "WINDOW_LIFE_CYCLE_EXCEPTION",
        OHOS::HiviewDFX::HiSysEvent::EventType::FAULT,
        "PID", getpid(),
        "UID", getuid(),
        "MSG", info);
    if (ret != 0) {
        WLOGFE("Write HiSysEvent error, ret:%{public}d", ret);
    }
}

std::string WindowImpl::TransferLifeCycleEventToString(LifeCycleEvent type) const
{
    std::string event;
    switch (type) {
        case LifeCycleEvent::CREATE_EVENT:
            event = "CREATE";
            break;
        case LifeCycleEvent::SHOW_EVENT:
            event = "SHOW";
            break;
        case LifeCycleEvent::HIDE_EVENT:
            event = "HIDE";
            break;
        case LifeCycleEvent::DESTROY_EVENT:
            event = "DESTROY";
            break;
        default:
            event = "UNDEFINE";
            break;
    }
    return event;
}

void WindowImpl::SetPrivacyMode(bool isPrivacyMode)
{
    property_->SetPrivacyMode(isPrivacyMode);
    surfaceNode_->SetSecurityLayer(isPrivacyMode);
}

bool WindowImpl::IsPrivacyMode() const
{
    return property_->GetPrivacyMode();
}

void WindowImpl::DisableAppWindowDecor()
{
    if (!WindowHelper::IsMainWindow(property_->GetWindowType())) {
        WLOGFE("window decoration is invalid on sub window");
        return;
    }
    WLOGFI("disable app window decoration.");
    isAppDecorEnable_ = false;
}

bool WindowImpl::IsDecorEnable() const
{
    WLOGFD("get decor enable %{public}d", property_->GetDecorEnable());
    return property_->GetDecorEnable();
}

WMError WindowImpl::Drag(const Rect& rect)
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    Rect requestRect = rect;
    property_->SetRequestRect(requestRect);
    property_->SetWindowSizeChangeReason(WindowSizeChangeReason::DRAG);
    property_->SetDragType(dragType_);
    return UpdateProperty(PropertyChangeAction::ACTION_UPDATE_RECT);
}

WMError WindowImpl::Maximize()
{
    WLOGFI("[Client] Window %{public}u Maximize", property_->GetWindowId());
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
        return SetFullScreen(true);
    } else {
        WLOGFI("Maximize Window failed. The window is not main window");
        return WMError::WM_ERROR_INVALID_PARAM;
    }
}

WMError WindowImpl::NotifyWindowTransition(TransitionReason reason)
{
    sptr<WindowTransitionInfo> fromInfo = new(std::nothrow) WindowTransitionInfo();
    sptr<WindowTransitionInfo> toInfo = new(std::nothrow) WindowTransitionInfo();
    if (fromInfo == nullptr || toInfo == nullptr) {
        WLOGFE("client new windowTransitionInfo failed");
        return WMError::WM_ERROR_NO_MEM;
    }
    auto abilityContext = AbilityRuntime::Context::ConvertTo<AbilityRuntime::AbilityContext>(context_);
    if (abilityContext == nullptr) {
        WLOGFE("id:%{public}d is not ability Window", property_->GetWindowId());
        return WMError::WM_ERROR_NO_MEM;
    }
    auto abilityInfo = abilityContext->GetAbilityInfo();
    if (abilityInfo == nullptr) {
        return WMError::WM_ERROR_NULLPTR;
    }
    fromInfo->SetBundleName(context_->GetBundleName());
    fromInfo->SetAbilityName(abilityInfo->name);
    fromInfo->SetWindowMode(property_->GetWindowMode());
    fromInfo->SetWindowRect(property_->GetWindowRect());
    fromInfo->SetAbilityToken(context_->GetToken());
    fromInfo->SetWindowType(property_->GetWindowType());
    fromInfo->SetDisplayId(property_->GetDisplayId());
    fromInfo->SetTransitionReason(reason);
    return SingletonContainer::Get<WindowAdapter>().NotifyWindowTransition(fromInfo, toInfo);
}

WMError WindowImpl::Minimize()
{
    WLOGFI("[Client] Window %{public}u Minimize", property_->GetWindowId());
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
        if (context_ != nullptr) {
            WMError ret = NotifyWindowTransition(TransitionReason::MINIMIZE);
            if (ret != WMError::WM_OK) {
                WLOGFI("[Client] Window %{public}u Minimize without remote animation ret:%{public}u",
                    property_->GetWindowId(), static_cast<uint32_t>(ret));
                AAFwk::AbilityManagerClient::GetInstance()->MinimizeAbility(context_->GetToken(), true);
            }
        } else {
            Hide();
        }
    }
    return WMError::WM_OK;
}

WMError WindowImpl::Recover()
{
    WLOGFI("[Client] Window %{public}u Normalize", property_->GetWindowId());
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
        SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
    }
    return WMError::WM_OK;
}

WMError WindowImpl::Close()
{
    WLOGFI("[Client] Window %{public}u Close", property_->GetWindowId());
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    if (WindowHelper::IsMainWindow(property_->GetWindowType())) {
        auto abilityContext = AbilityRuntime::Context::ConvertTo<AbilityRuntime::AbilityContext>(context_);
        if (abilityContext != nullptr) {
            WMError ret = NotifyWindowTransition(TransitionReason::CLOSE);
            if (ret != WMError::WM_OK) {
                WLOGFI("[Client] Window %{public}u Close without remote animation ret:%{public}u",
                    property_->GetWindowId(), static_cast<uint32_t>(ret));
                abilityContext->CloseAbility();
            }
        } else {
            Destroy();
        }
    }
    return WMError::WM_OK;
}

void WindowImpl::StartMove()
{
    if (!WindowHelper::IsMainFloatingWindow(GetType(), GetMode())) {
        WLOGFI("[StartMove] current window can not be moved, windowId %{public}u", GetWindowId());
        return;
    }
    startMoveFlag_ = true;
    WLOGFI("[StartMove] windowId %{public}u", GetWindowId());
}

WMError WindowImpl::RequestFocus() const
{
    if (!IsWindowValid()) {
        return WMError::WM_ERROR_INVALID_WINDOW;
    }
    return SingletonContainer::Get<WindowAdapter>().RequestFocus(property_->GetWindowId());
}

void WindowImpl::SetInputEventConsumer(const std::shared_ptr<IInputEventConsumer>& inputEventConsumer)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    inputEventConsumer_ = inputEventConsumer;
}

void WindowImpl::AddInputEventListener(const std::shared_ptr<MMI::IInputEventConsumer>& inputEventListener)
{
    InputTransferStation::GetInstance().SetInputListener(GetWindowId(), inputEventListener);
}

void WindowImpl::RegisterLifeCycleListener(const sptr<IWindowLifeCycle>& listener)
{
    if (listener == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(lifecycleListeners_.begin(), lifecycleListeners_.end(), listener) != lifecycleListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    lifecycleListeners_.emplace_back(listener);
}

void WindowImpl::RegisterWindowChangeListener(sptr<IWindowChangeListener>& listener)
{
    if (listener == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(windowChangeListeners_.begin(), windowChangeListeners_.end(), listener) !=
        windowChangeListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    windowChangeListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterLifeCycleListener(const sptr<IWindowLifeCycle>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    lifecycleListeners_.erase(std::remove_if(lifecycleListeners_.begin(), lifecycleListeners_.end(),
        [listener](sptr<IWindowLifeCycle> registeredListener) {
            return registeredListener == listener;
        }), lifecycleListeners_.end());
}

void WindowImpl::UnregisterWindowChangeListener(sptr<IWindowChangeListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    windowChangeListeners_.erase(std::remove_if(windowChangeListeners_.begin(), windowChangeListeners_.end(),
        [listener](sptr<IWindowChangeListener> registeredListener) {
            return registeredListener == listener;
        }), windowChangeListeners_.end());
}

void WindowImpl::RegisterAvoidAreaChangeListener(sptr<IAvoidAreaChangedListener>& listener)
{
    if (listener == nullptr) {
        WLOGFE("register avoid area listener fail. listener is null");
        return;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (std::find(avoidAreaChangeListeners_.begin(), avoidAreaChangeListeners_.end(), listener) !=
            avoidAreaChangeListeners_.end()) {
            WLOGFE("avoid area listener already registered");
            return;
        }
        avoidAreaChangeListeners_.push_back(listener);
    }
    if (avoidAreaChangeListeners_.size() == 1) {
        SingletonContainer::Get<WindowAdapter>().UpdateAvoidAreaListener(property_->GetWindowId(), true);
    }
}

void WindowImpl::UnregisterAvoidAreaChangeListener(sptr<IAvoidAreaChangedListener>& listener)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        avoidAreaChangeListeners_.erase(std::remove_if(avoidAreaChangeListeners_.begin(),
            avoidAreaChangeListeners_.end(),
            [listener](sptr<IAvoidAreaChangedListener> registeredListener) {
                return registeredListener == listener;
            }), avoidAreaChangeListeners_.end());
    }
    if (avoidAreaChangeListeners_.empty()) {
        SingletonContainer::Get<WindowAdapter>().UpdateAvoidAreaListener(property_->GetWindowId(), false);
    }
}

void WindowImpl::RegisterDragListener(const sptr<IWindowDragListener>& listener)
{
    if (listener == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(windowDragListeners_.begin(), windowDragListeners_.end(), listener) != windowDragListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    windowDragListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterDragListener(const sptr<IWindowDragListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto iter = std::find(windowDragListeners_.begin(), windowDragListeners_.end(), listener);
    if (iter == windowDragListeners_.end()) {
        WLOGFE("could not find this listener");
        return;
    }
    windowDragListeners_.erase(iter);
}

void WindowImpl::RegisterDisplayMoveListener(sptr<IDisplayMoveListener>& listener)
{
    if (listener == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(displayMoveListeners_.begin(), displayMoveListeners_.end(), listener) !=
        displayMoveListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    displayMoveListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterDisplayMoveListener(sptr<IDisplayMoveListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto iter = std::find(displayMoveListeners_.begin(), displayMoveListeners_.end(), listener);
    if (iter == displayMoveListeners_.end()) {
        WLOGFE("could not find the listener");
        return;
    }
    displayMoveListeners_.erase(iter);
}

void WindowImpl::RegisterInputEventListener(const sptr<IInputEventListener>& listener)
{
    if (listener == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(inputEventListeners_.begin(), inputEventListeners_.end(), listener) != inputEventListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    inputEventListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterInputEventListener(const sptr<IInputEventListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto iter = std::find(inputEventListeners_.begin(), inputEventListeners_.end(), listener);
    if (iter == inputEventListeners_.end()) {
        WLOGFE("could not find the listener");
        return;
    }
    inputEventListeners_.erase(iter);
}

void WindowImpl::RegisterWindowDestroyedListener(const NotifyNativeWinDestroyFunc& func)
{
    WLOGFI("JS RegisterWindowDestroyedListener the listener");
    notifyNativefunc_ = std::move(func);
}

void WindowImpl::RegisterOccupiedAreaChangeListener(const sptr<IOccupiedAreaChangeListener>& listener)
{
    if (listener == nullptr) {
        WLOGFE(" listener is nullptr");
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(occupiedAreaChangeListeners_.begin(), occupiedAreaChangeListeners_.end(), listener) !=
        occupiedAreaChangeListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    occupiedAreaChangeListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterOccupiedAreaChangeListener(const sptr<IOccupiedAreaChangeListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    occupiedAreaChangeListeners_.erase(std::remove_if(occupiedAreaChangeListeners_.begin(),
        occupiedAreaChangeListeners_.end(), [listener](sptr<IOccupiedAreaChangeListener> registeredListener) {
            return registeredListener == listener;
        }), occupiedAreaChangeListeners_.end());
}

void WindowImpl::RegisterTouchOutsideListener(const sptr<ITouchOutsideListener>& listener)
{
    if (listener == nullptr) {
        WLOGFE("listener is nullptr");
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(touchOutsideListeners_.begin(), touchOutsideListeners_.end(), listener) !=
        touchOutsideListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    touchOutsideListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterTouchOutsideListener(const sptr<ITouchOutsideListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    touchOutsideListeners_.erase(std::remove_if(touchOutsideListeners_.begin(),
        touchOutsideListeners_.end(), [listener](sptr<ITouchOutsideListener> registeredListener) {
            return registeredListener == listener;
        }), touchOutsideListeners_.end());
}

void WindowImpl::RegisterAnimationTransitionController(const sptr<IAnimationTransitionController>& listener)
{
    if (listener == nullptr) {
        WLOGFE("listener is nullptr");
        return;
    }
    animationTranistionController_ = listener;
}

void WindowImpl::RegisterScreenshotListener(const sptr<IScreenshotListener>& listener)
{
    if (listener == nullptr) {
        WLOGFE("listener is nullptr");
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(screenshotListeners_.begin(), screenshotListeners_.end(), listener) !=
        screenshotListeners_.end()) {
        WLOGFE("Listener already registered");
        return;
    }
    screenshotListeners_.emplace_back(listener);
}

void WindowImpl::UnregisterScreenshotListener(const sptr<IScreenshotListener>& listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    screenshotListeners_.erase(std::remove_if(screenshotListeners_.begin(),
        screenshotListeners_.end(), [listener](sptr<IScreenshotListener> registeredListener) {
            return registeredListener == listener;
        }), screenshotListeners_.end());
}

void WindowImpl::SetAceAbilityHandler(const sptr<IAceAbilityHandler>& handler)
{
    if (handler == nullptr) {
        WLOGFI("ace ability handler is nullptr");
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    aceAbilityHandler_ = handler;
}

void WindowImpl::SetRequestModeSupportInfo(uint32_t modeSupportInfo)
{
    property_->SetRequestModeSupportInfo(modeSupportInfo);
    SetModeSupportInfo(modeSupportInfo);
}

void WindowImpl::SetModeSupportInfo(uint32_t modeSupportInfo)
{
    property_->SetModeSupportInfo(modeSupportInfo);
}

void WindowImpl::UpdateRect(const struct Rect& rect, bool decoStatus, WindowSizeChangeReason reason)
{
    auto display = DisplayManager::GetInstance().GetDisplayById(property_->GetDisplayId());
    if (display == nullptr) {
        WLOGFE("get display failed displayId:%{public}" PRIu64", window id:%{public}u", property_->GetDisplayId(),
            property_->GetWindowId());
        return;
    }

    property_->SetDecoStatus(decoStatus);
    if (reason == WindowSizeChangeReason::HIDE) {
        property_->SetRequestRect(rect);
        return;
    }
    property_->SetWindowRect(rect);

    // update originRect when floating window show for the first time.
    if (!isOriginRectSet_ && WindowHelper::IsMainFloatingWindow(GetType(), GetMode())) {
        property_->SetOriginRect(rect);
        isOriginRectSet_ = true;
    }
    WLOGFD("winId:%{public}u, rect[%{public}d, %{public}d, %{public}u, %{public}u], reason:%{public}u",
        property_->GetWindowId(), rect.posX_, rect.posY_, rect.width_, rect.height_, reason);
    Rect rectToAce = rect;
    // update rectToAce for stretchable window
    if (windowSystemConfig_.isStretchable_ && WindowHelper::IsMainFloatingWindow(GetType(), GetMode())) {
        if (reason == WindowSizeChangeReason::DRAG || reason == WindowSizeChangeReason::DRAG_END ||
            reason == WindowSizeChangeReason::DRAG_START || reason == WindowSizeChangeReason::RECOVER ||
            reason == WindowSizeChangeReason::MOVE) {
            rectToAce = property_->GetOriginRect();
        } else {
            property_->SetOriginRect(rect);
        }
    }
    WLOGFD("sizeChange callback size: %{public}lu", (unsigned long)windowChangeListeners_.size());

    NotifySizeChange(rectToAce, reason);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (uiContent_ == nullptr) {
            return;
        }
        Ace::ViewportConfig config;
        WLOGFI("UpdateViewportConfig Id:%{public}u, windowRect:[%{public}d, %{public}d, %{public}u, %{public}u]",
            property_->GetWindowId(), rectToAce.posX_, rectToAce.posY_, rectToAce.width_, rectToAce.height_);
        config.SetSize(rectToAce.width_, rectToAce.height_);
        config.SetPosition(rectToAce.posX_, rectToAce.posY_);
        config.SetDensity(display->GetVirtualPixelRatio());
        uiContent_->UpdateViewportConfig(config, reason);
        WLOGFI("notify uiContent window size change end");
    }
}

void WindowImpl::UpdateMode(WindowMode mode)
{
    WLOGI("UpdateMode %{public}u", mode);
    property_->SetWindowMode(mode);
    UpdateTitleButtonVisibility();
    NotifyModeChange(mode);
    if (uiContent_ != nullptr) {
        uiContent_->UpdateWindowMode(mode);
        WLOGFI("notify uiContent window mode change end");
    }
}

void WindowImpl::UpdateModeSupportInfo(uint32_t modeSupportInfo)
{
    WLOGI("modeSupportInfo: %{public}u, winId: %{public}u", modeSupportInfo, GetWindowId());
    SetModeSupportInfo(modeSupportInfo);
    UpdateTitleButtonVisibility();
}

void WindowImpl::HandleBackKeyPressedEvent(const std::shared_ptr<MMI::KeyEvent>& keyEvent)
{
    std::shared_ptr<IInputEventConsumer> inputEventConsumer;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        inputEventConsumer = inputEventConsumer_;
    }
    bool isConsumed = false;
    if (inputEventConsumer != nullptr) {
        WLOGFI("Transfer back key event to inputEventConsumer");
        isConsumed = inputEventConsumer->OnInputEvent(keyEvent);
    } else if (!inputEventListeners_.empty()) {
        WLOGFI("inputEventListeners_ is not empty");
        return;
    } else if (uiContent_ != nullptr) {
        WLOGFI("Transfer back key event to uiContent");
        isConsumed = uiContent_->ProcessBackPressed();
    } else {
        WLOGFE("There is no back key event consumer");
    }
    if (isConsumed || !WindowHelper::IsMainWindow(property_->GetWindowType())) {
        WLOGFI("Back key event is consumed or it is not a main window");
        return;
    }
    auto abilityContext = AbilityRuntime::Context::ConvertTo<AbilityRuntime::AbilityContext>(context_);
    if (abilityContext == nullptr) {
        WLOGFE("abilityContext is null");
        return;
    }
    // TerminateAbility will invoke last ability, CloseAbility will not.
    bool shouldTerminateAbility = WindowHelper::IsFullScreenWindow(property_->GetWindowMode());
    WMError ret = NotifyWindowTransition(shouldTerminateAbility ? TransitionReason::BACK : TransitionReason::CLOSE);
    if (ret != WMError::WM_OK) {
        if (shouldTerminateAbility) {
            abilityContext->TerminateSelf();
        } else {
            abilityContext->CloseAbility();
        }
    }
    WLOGFI("Window %{public}u will be closed, WindowTransition ret: %{public}u, shouldTerminateAbility: %{public}u",
        property_->GetWindowId(), static_cast<uint32_t>(ret), static_cast<uint32_t>(shouldTerminateAbility));
}

void WindowImpl::ConsumeKeyEvent(std::shared_ptr<MMI::KeyEvent>& keyEvent)
{
    NotifyKeyEvent(keyEvent);
    int32_t keyCode = keyEvent->GetKeyCode();
    int32_t keyAction = keyEvent->GetKeyAction();
    WLOGFI("KeyCode: %{public}d, action: %{public}d", keyCode, keyAction);
    if (keyCode == MMI::KeyEvent::KEYCODE_BACK && keyAction == MMI::KeyEvent::KEY_ACTION_UP) {
        HandleBackKeyPressedEvent(keyEvent);
    } else {
        std::shared_ptr<IInputEventConsumer> inputEventConsumer;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            inputEventConsumer = inputEventConsumer_;
        }
        if (inputEventConsumer != nullptr) {
            WLOGFI("Transfer key event to inputEventConsumer");
            (void)inputEventConsumer->OnInputEvent(keyEvent);
        } else if (uiContent_ != nullptr) {
            WLOGFI("Transfer key event to uiContent");
            (void)uiContent_->ProcessKeyEvent(keyEvent);
        } else {
            WLOGFE("There is no key event consumer");
        }
    }
}

void WindowImpl::HandleMoveEvent(int32_t posX, int32_t posY, int32_t pointId)
{
    if (!startMoveFlag_ || (pointId != startPointerId_)) {
        return;
    }
    int32_t targetX = startPointRect_.posX_ + (posX - startPointPosX_);
    int32_t targetY = startPointRect_.posY_ + (posY - startPointPosY_);
    auto res = MoveTo(targetX, targetY);
    if (res != WMError::WM_OK) {
        WLOGFE("move window: %{public}u failed", GetWindowId());
    }
}

void WindowImpl::HandleDragEvent(int32_t posX, int32_t posY, int32_t pointId)
{
    if (!startDragFlag_ || (pointId != startPointerId_)) {
        return;
    }
    int32_t diffX = posX - startPointPosX_;
    int32_t diffY = posY - startPointPosY_;
    Rect newRect = startPointRect_;

    Rect hotZoneRect;
    if ((startPointPosX_ > startRectExceptCorner_.posX_ &&
        (startPointPosX_ < startRectExceptCorner_.posX_ + static_cast<int32_t>(startRectExceptCorner_.width_))) &&
        (startPointPosY_ > startRectExceptCorner_.posY_ &&
        (startPointPosY_ < startRectExceptCorner_.posY_ + static_cast<int32_t>(startRectExceptCorner_.height_)))) {
        hotZoneRect = startRectExceptFrame_; // drag type: left/right/top/bottom
    } else {
        hotZoneRect = startRectExceptCorner_; // drag type: left_top/right_top/left_bottom/right_bottom
    }

    if (startPointPosX_ <= hotZoneRect.posX_) {
        if (diffX > static_cast<int32_t>(startPointRect_.width_)) {
            diffX = static_cast<int32_t>(startPointRect_.width_);
        }
        newRect.posX_ += diffX;
        newRect.width_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.width_) - diffX);
    } else if (startPointPosX_ >= hotZoneRect.posX_ + static_cast<int32_t>(hotZoneRect.width_)) {
        if (diffX < 0 && (-diffX > static_cast<int32_t>(startPointRect_.width_))) {
            diffX = -(static_cast<int32_t>(startPointRect_.width_));
        }
        newRect.width_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.width_) + diffX);
    }
    if (startPointPosY_ <= hotZoneRect.posY_) {
        if (diffY > static_cast<int32_t>(startPointRect_.height_)) {
            diffY = static_cast<int32_t>(startPointRect_.height_);
        }
        newRect.posY_ += diffY;
        newRect.height_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.height_) - diffY);
    } else if (startPointPosY_ >= hotZoneRect.posY_ + static_cast<int32_t>(hotZoneRect.height_)) {
        if (diffY < 0 && (-diffY > static_cast<int32_t>(startPointRect_.height_))) {
            diffY = -(static_cast<int32_t>(startPointRect_.height_));
        }
        newRect.height_ = static_cast<uint32_t>(static_cast<int32_t>(newRect.height_) + diffY);
    }
    auto res = Drag(newRect);
    if (res != WMError::WM_OK) {
        WLOGFE("drag window: %{public}u failed", GetWindowId());
    }
}

void WindowImpl::HandleModeChangeHotZones(int32_t posX, int32_t posY)
{
    if (!WindowHelper::IsMainFloatingWindow(GetType(), GetMode())) {
        return;
    }

    ModeChangeHotZones hotZones;
    auto res = SingletonContainer::Get<WindowAdapter>().GetModeChangeHotZones(property_->GetDisplayId(), hotZones);
    WLOGFI("[HotZone] Window %{public}u, Pointer[%{public}d, %{public}d]", GetWindowId(), posX, posY);
    if (res == WMError::WM_OK) {
        WLOGFI("[HotZone] Fullscreen [%{public}d, %{public}d, %{public}u, %{public}u]", hotZones.fullscreen_.posX_,
            hotZones.fullscreen_.posY_, hotZones.fullscreen_.width_, hotZones.fullscreen_.height_);
        WLOGFI("[HotZone] Primary [%{public}d, %{public}d, %{public}u, %{public}u]", hotZones.primary_.posX_,
            hotZones.primary_.posY_, hotZones.primary_.width_, hotZones.primary_.height_);
        WLOGFI("[HotZone] Secondary [%{public}d, %{public}d, %{public}u, %{public}u]", hotZones.secondary_.posX_,
            hotZones.secondary_.posY_, hotZones.secondary_.width_, hotZones.secondary_.height_);

        if (WindowHelper::IsPointInTargetRect(posX, posY, hotZones.fullscreen_)) {
            SetFullScreen(true);
        } else if (WindowHelper::IsPointInTargetRect(posX, posY, hotZones.primary_)) {
            SetWindowMode(WindowMode::WINDOW_MODE_SPLIT_PRIMARY);
        } else if (WindowHelper::IsPointInTargetRect(posX, posY, hotZones.secondary_)) {
            SetWindowMode(WindowMode::WINDOW_MODE_SPLIT_SECONDARY);
        }
    }
}

void WindowImpl::UpdatePointerEvent(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    property_->ComputeTransform();
    MMI::PointerEvent::PointerItem pointerItem;
    if (!pointerEvent->GetPointerItem(pointerEvent->GetPointerId(), pointerItem)) {
        WLOGFW("Point item is invalid");
        return;
    }
    Rect winRect = GetRect();
    PointInfo originPos =
        WindowHelper::CalculateOriginPosition(property_->GetTransformMat(), property_->GetPlane(),
        { pointerItem.GetDisplayX(), pointerItem.GetDisplayY() });
    WLOGI("Pointer event has been updated,window id:%{public}u, before->now:"
        "[%{public}d,%{public}d]->[%{public}d,%{public}d]",
        property_->GetWindowId(), pointerItem.GetDisplayX(), pointerItem.GetDisplayY(), originPos.x, originPos.y);
    pointerItem.SetDisplayX(originPos.x);
    pointerItem.SetDisplayY(originPos.y);
    pointerItem.SetWindowX(originPos.x - winRect.posX_);
    pointerItem.SetWindowY(originPos.y - winRect.posY_);
    pointerEvent->UpdatePointerItem(pointerEvent->GetPointerId(), pointerItem);
}

void WindowImpl::UpdatePointerEventForStretchableWindow(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    MMI::PointerEvent::PointerItem pointerItem;
    if (!pointerEvent->GetPointerItem(pointerEvent->GetPointerId(), pointerItem)) {
        WLOGFW("Point item is invalid");
        return;
    }
    const Rect& originRect = property_->GetOriginRect();
    PointInfo originPos =
        WindowHelper::CalculateOriginPosition(originRect, GetRect(),
        { pointerItem.GetDisplayX(), pointerItem.GetDisplayY() });
    pointerItem.SetDisplayX(originPos.x);
    pointerItem.SetDisplayY(originPos.y);
    pointerItem.SetWindowX(originPos.x - originRect.posX_);
    pointerItem.SetWindowY(originPos.y - originRect.posY_);
    pointerEvent->UpdatePointerItem(pointerEvent->GetPointerId(), pointerItem);
}


void WindowImpl::EndMoveOrDragWindow(int32_t posX, int32_t posY, int32_t pointId)
{
    if (pointId != startPointerId_) {
        return;
    }

    if (startDragFlag_) {
        SingletonContainer::Get<WindowAdapter>().ProcessPointUp(GetWindowId());
        startDragFlag_ = false;
    }

    if (startMoveFlag_) {
        if (GetType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
            SingletonContainer::Get<WindowAdapter>().ProcessPointUp(GetWindowId());
        }
        startMoveFlag_ = false;
        HandleModeChangeHotZones(posX, posY);
    }
    pointEventStarted_ = false;
}

void WindowImpl::UpdateDragType()
{
    if (!startDragFlag_) {
        dragType_ = DragType::DRAG_UNDEFINED;
        return;
    }
    if (startPointPosX_ > startRectExceptCorner_.posX_ &&
        (startPointPosX_ < startRectExceptCorner_.posX_ + static_cast<int32_t>(startRectExceptCorner_.width_))) {
        dragType_ = DragType::DRAG_HEIGHT;
    } else if (startPointPosY_ > startRectExceptCorner_.posY_ &&
        (startPointPosY_ < startRectExceptCorner_.posY_ + static_cast<int32_t>(startRectExceptCorner_.height_))) {
        dragType_ = DragType::DRAG_WIDTH;
    } else {
        dragType_ = DragType::DRAG_CORNER;
    }
}

void WindowImpl::ReadyToMoveOrDragWindow(int32_t globalX, int32_t globalY, int32_t pointId, const Rect& rect)
{
    if (pointEventStarted_) {
        return;
    }
    TransformHelper::Vector2 hotZoneScale(1, 1);
    if (property_->GetTransform() != Transform::Identity()) {
        property_->ComputeTransform();
        hotZoneScale = WindowHelper::CalculateHotZoneScale(property_->GetTransformMat(),
            property_->GetPlane());
    }
    startPointRect_ = rect;
    startPointPosX_ = globalX;
    startPointPosY_ = globalY;
    startPointerId_ = pointId;
    pointEventStarted_ = true;

    // calculate window inner rect except frame
    auto display = DisplayManager::GetInstance().GetDisplayById(property_->GetDisplayId());
    if (display == nullptr) {
        WLOGFE("get display failed displayId:%{public}" PRIu64", window id:%{public}u", property_->GetDisplayId(),
            property_->GetWindowId());
        return;
    }
    float virtualPixelRatio = display->GetVirtualPixelRatio();

    startRectExceptFrame_.posX_ = startPointRect_.posX_ +
        static_cast<int32_t>(WINDOW_FRAME_WIDTH * virtualPixelRatio / hotZoneScale.x_);
    startRectExceptFrame_.posY_ = startPointRect_.posY_ +
        static_cast<int32_t>(WINDOW_FRAME_WIDTH * virtualPixelRatio / hotZoneScale.y_);
    startRectExceptFrame_.width_ = startPointRect_.width_ -
        static_cast<uint32_t>((WINDOW_FRAME_WIDTH + WINDOW_FRAME_WIDTH) * virtualPixelRatio / hotZoneScale.x_);
    startRectExceptFrame_.height_ = startPointRect_.height_ -
        static_cast<uint32_t>((WINDOW_FRAME_WIDTH + WINDOW_FRAME_WIDTH) * virtualPixelRatio / hotZoneScale.y_);

    startRectExceptCorner_.posX_ = startPointRect_.posX_ +
        static_cast<int32_t>(WINDOW_FRAME_CORNER_WIDTH * virtualPixelRatio / hotZoneScale.x_);
    startRectExceptCorner_.posY_ = startPointRect_.posY_ +
        static_cast<int32_t>(WINDOW_FRAME_CORNER_WIDTH * virtualPixelRatio / hotZoneScale.y_);
    startRectExceptCorner_.width_ = startPointRect_.width_ -
        static_cast<uint32_t>((WINDOW_FRAME_CORNER_WIDTH + WINDOW_FRAME_CORNER_WIDTH) *
        virtualPixelRatio / hotZoneScale.x_);
    startRectExceptCorner_.height_ = startPointRect_.height_ -
        static_cast<uint32_t>((WINDOW_FRAME_CORNER_WIDTH + WINDOW_FRAME_CORNER_WIDTH) *
        virtualPixelRatio / hotZoneScale.y_);

    if (GetType() == WindowType::WINDOW_TYPE_DOCK_SLICE) {
        startMoveFlag_ = true;
        SingletonContainer::Get<WindowAdapter>().ProcessPointDown(property_->GetWindowId(), true);
    } else if (!WindowHelper::IsPointInTargetRect(startPointPosX_, startPointPosY_, startRectExceptFrame_) ||
        (WindowHelper::IsPointInTargetRect(startPointPosX_, startPointPosY_, startRectExceptFrame_) &&
        (!WindowHelper::IsPointInWindowExceptCorner(startPointPosX_, startPointPosY_, startRectExceptCorner_)))) {
        startDragFlag_ = true;
        SingletonContainer::Get<WindowAdapter>().ProcessPointDown(property_->GetWindowId(), true);
    }

    UpdateDragType();

    return;
}

void WindowImpl::ConsumeMoveOrDragEvent(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    MMI::PointerEvent::PointerItem pointerItem;
    int32_t pointId = pointerEvent->GetPointerId();
    if (!pointerEvent->GetPointerItem(pointId, pointerItem)) {
        WLOGFW("Point item is invalid");
        return;
    }
    int32_t pointGlobalX = pointerItem.GetDisplayX();
    int32_t pointGlobalY = pointerItem.GetDisplayY();
    int32_t action = pointerEvent->GetPointerAction();
    switch (action) {
        // Ready to move or drag
        case MMI::PointerEvent::POINTER_ACTION_DOWN:
        case MMI::PointerEvent::POINTER_ACTION_BUTTON_DOWN: {
            Rect rect = GetRect();
            ReadyToMoveOrDragWindow(pointGlobalX, pointGlobalY, pointId, rect);
            WLOGFI("[Point Down]: windowId: %{public}u, action: %{public}d, hasPointStarted: %{public}d, "
                   "startMove: %{public}d, startDrag: %{public}d, pointPos: [%{public}d, %{public}d], "
                   "winRect: [%{public}d, %{public}d, %{public}u, %{public}u]",
                   GetWindowId(), action, pointEventStarted_, startMoveFlag_, startDragFlag_,
                   pointGlobalX, pointGlobalY, rect.posX_, rect.posY_, rect.width_, rect.height_);
            break;
        }
        // Start to move or drag
        case MMI::PointerEvent::POINTER_ACTION_MOVE: {
            HandleMoveEvent(pointGlobalX, pointGlobalY, pointId);
            HandleDragEvent(pointGlobalX, pointGlobalY, pointId);
            break;
        }
        // End move or drag
        case MMI::PointerEvent::POINTER_ACTION_UP:
        case MMI::PointerEvent::POINTER_ACTION_BUTTON_UP:
        case MMI::PointerEvent::POINTER_ACTION_CANCEL: {
            EndMoveOrDragWindow(pointGlobalX, pointGlobalY, pointId);
            WLOGFI("[Point Up/Cancel]: windowId: %{public}u, action: %{public}d, startMove: %{public}d, "
                   "startDrag: %{public}d", GetWindowId(), action, startMoveFlag_, startDragFlag_);
            break;
        }
        default:
            break;
    }
}

bool WindowImpl::IsPointerEventConsumed()
{
    return startDragFlag_ || startMoveFlag_;
}

void WindowImpl::AdjustWindowAnimationFlag(bool withAnimation)
{
    // when show/hide with animation
    // use custom animation when transitionController exists; else use default animation
    WindowType winType = property_->GetWindowType();
    bool isAppWindow = WindowHelper::IsAppWindow(winType);
    if (withAnimation && !isAppWindow && animationTranistionController_) {
        // use custom animation
        property_->SetAnimationFlag(static_cast<uint32_t>(WindowAnimation::CUSTOM));
    } else if (isAppWindow || (withAnimation && !animationTranistionController_)) {
        // use default animation
        property_->SetAnimationFlag(static_cast<uint32_t>(WindowAnimation::DEFAULT));
    } else {
        // with no animation
        property_->SetAnimationFlag(static_cast<uint32_t>(WindowAnimation::NONE));
    }
}

void WindowImpl::ConsumePointerEvent(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    // If windowRect transformed, transform event back to its origin position
    if (property_->GetTransform() != Transform::Identity()) {
        UpdatePointerEvent(pointerEvent);
    }
    int32_t action = pointerEvent->GetPointerAction();
    if (action == MMI::PointerEvent::POINTER_ACTION_DOWN || action == MMI::PointerEvent::POINTER_ACTION_BUTTON_DOWN) {
        WLOGI("WMS process point down, window: [name:%{public}s, id:%{public}u], action: %{public}d",
            name_.c_str(), GetWindowId(), action);
        if (GetType() == WindowType::WINDOW_TYPE_LAUNCHER_RECENT) {
            MMI::PointerEvent::PointerItem pointerItem;
            if (!pointerEvent->GetPointerItem(pointerEvent->GetPointerId(), pointerItem)) {
                WLOGFW("Point item is invalid");
                return;
            }
            if (!WindowHelper::IsPointInTargetRect(pointerItem.GetDisplayX(), pointerItem.GetDisplayY(), GetRect())) {
                NotifyAfterUnfocused(false);
                return;
            }
        }
        SingletonContainer::Get<WindowAdapter>().ProcessPointDown(property_->GetWindowId());
    }

    // If point event type is up, should reset start move flag
    if (WindowHelper::IsMainFloatingWindow(GetType(), GetMode()) ||
        GetType() == WindowType::WINDOW_TYPE_DOCK_SLICE ||
        (action == MMI::PointerEvent::POINTER_ACTION_UP || action == MMI::PointerEvent::POINTER_ACTION_BUTTON_UP ||
        action == MMI::PointerEvent::POINTER_ACTION_CANCEL)) {
        ConsumeMoveOrDragEvent(pointerEvent);
    }

    if (IsPointerEventConsumed()) {
        return;
    }
    if (windowSystemConfig_.isStretchable_ && GetMode() == WindowMode::WINDOW_MODE_FLOATING) {
        WLOGFI("update pointer event for stretchable window");
        UpdatePointerEventForStretchableWindow(pointerEvent);
    }
    NotifyPointEvent(pointerEvent);
    std::shared_ptr<IInputEventConsumer> inputEventConsumer;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        inputEventConsumer = inputEventConsumer_;
    }
    if (inputEventConsumer != nullptr) {
        WLOGFI("Transfer pointer event to inputEventConsumer");
        (void)inputEventConsumer->OnInputEvent(pointerEvent);
    } else if (uiContent_ != nullptr) {
        WLOGFI("Transfer pointer event to uiContent");
        (void)uiContent_->ProcessPointerEvent(pointerEvent);
    } else {
        WLOGE("pointerEvent is not consumed, windowId: %{public}u", GetWindowId());
        pointerEvent->MarkProcessed();
    }
}

void WindowImpl::OnVsync(int64_t timeStamp)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        isWaitingFrame_ = false;
    }
    uiContent_->ProcessVsyncEvent(static_cast<uint64_t>(timeStamp));
}

void WindowImpl::RequestFrame()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_ == WindowState::STATE_DESTROYED) {
        WLOGFE("RequestFrame failed, window is destroyed");
        return;
    }
    VsyncStation::GetInstance().RequestVsync(VsyncStation::CallbackType::CALLBACK_FRAME, callback_);
    isWaitingFrame_ = true;
}

void WindowImpl::UpdateFocusStatus(bool focused)
{
    WLOGFI("window focus status: %{public}d, id: %{public}u", focused, property_->GetWindowId());
    if (focused) {
        NotifyAfterFocused();
    } else {
        NotifyAfterUnfocused();
    }
}

void WindowImpl::UpdateConfiguration(const std::shared_ptr<AppExecFwk::Configuration>& configuration)
{
    if (uiContent_ != nullptr) {
        WLOGFD("notify ace winId:%{public}u", GetWindowId());
        uiContent_->UpdateConfiguration(configuration);
    }
    if (subWindowMap_.count(GetWindowId()) == 0) {
        return;
    }
    for (auto& subWindow : subWindowMap_.at(GetWindowId())) {
        subWindow->UpdateConfiguration(configuration);
    }
}

void WindowImpl::UpdateAvoidArea(const sptr<AvoidArea>& avoidArea, AvoidAreaType type)
{
    WLOGFI("Window Update AvoidArea, id: %{public}u", property_->GetWindowId());
    NotifyAvoidAreaChange(avoidArea, type);
}

void WindowImpl::UpdateWindowState(WindowState state)
{
    WLOGFI("[Client] Window %{public}u, %{public}s WindowState to set:%{public}u", GetWindowId(), name_.c_str(), state);
    if (!IsWindowValid()) {
        return;
    }
    auto abilityContext = AbilityRuntime::Context::ConvertTo<AbilityRuntime::AbilityContext>(context_);
    switch (state) {
        case WindowState::STATE_FROZEN: {
            if (abilityContext != nullptr && windowTag_ == WindowTag::MAIN_WINDOW) {
                WLOGFD("DoAbilityBackground KEYGUARD, id: %{public}u", GetWindowId());
                AAFwk::AbilityManagerClient::GetInstance()->DoAbilityBackground(abilityContext->GetToken(),
                    static_cast<uint32_t>(WindowStateChangeReason::KEYGUARD));
            } else {
                state_ = WindowState::STATE_FROZEN;
                NotifyAfterBackground();
            }
            break;
        }
        case WindowState::STATE_UNFROZEN: {
            if (abilityContext != nullptr && windowTag_ == WindowTag::MAIN_WINDOW) {
                WLOGFD("DoAbilityForeground KEYGUARD, id: %{public}u", GetWindowId());
                AAFwk::AbilityManagerClient::GetInstance()->DoAbilityForeground(abilityContext->GetToken(),
                    static_cast<uint32_t>(WindowStateChangeReason::KEYGUARD));
            } else {
                state_ = WindowState::STATE_SHOWN;
                NotifyAfterForeground();
            }
            break;
        }
        case WindowState::STATE_SHOWN: {
            if (abilityContext != nullptr && windowTag_ == WindowTag::MAIN_WINDOW) {
                WLOGFD("WindowState::STATE_SHOWN, id: %{public}u", GetWindowId());
                AAFwk::AbilityManagerClient::GetInstance()->DoAbilityForeground(abilityContext->GetToken(),
                    static_cast<uint32_t>(WindowStateChangeReason::TOGGLING));
            } else {
                state_ = WindowState::STATE_SHOWN;
                NotifyAfterForeground();
            }
            break;
        }
        default: {
            WLOGFE("windowState to set is invalid");
            break;
        }
    }
}

sptr<WindowProperty> WindowImpl::GetWindowProperty()
{
    WLOGFI("[Client] Window %{public}u, %{public}s", GetWindowId(), name_.c_str());
    if (!IsWindowValid()) {
        return nullptr;
    }
    return property_;
}

void WindowImpl::UpdateDragEvent(const PointInfo& point, DragEvent event)
{
    NotifyDragEvent(point, event);
}

void WindowImpl::NotifyDragEvent(const PointInfo& point, DragEvent event)
{
    std::vector<sptr<IWindowDragListener>> windowDragListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        windowDragListeners = windowDragListeners_;
    }
    Rect rect = GetRect();
    PostListenerTask([windowDragListeners, rect, point, event]() {
        for (auto& listener : windowDragListeners) {
            if (listener != nullptr) {
                listener->OnDrag(point.x - rect.posX_, point.y - rect.posY_, event);
            }
        }
    });
}

void WindowImpl::UpdateDisplayId(DisplayId from, DisplayId to)
{
    WLOGFD("update displayId. win %{public}u", GetWindowId());
    NotifyDisplayMoveChange(from, to);
    property_->SetDisplayId(to);
}

void WindowImpl::UpdateOccupiedAreaChangeInfo(const sptr<OccupiedAreaChangeInfo>& info)
{
    WLOGFI("Window Update OccupiedArea, id: %{public}u", property_->GetWindowId());
    NotifyOccupiedAreaChange(info);
}

void WindowImpl::UpdateActiveStatus(bool isActive)
{
    WLOGFD("window active status: %{public}d, id: %{public}u", isActive, property_->GetWindowId());
    if (isActive) {
        NotifyAfterActive();
    } else {
        NotifyAfterInactive();
    }
}

void WindowImpl::NotifyScreenshot()
{
    std::vector<sptr<IScreenshotListener>> screenshotListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        screenshotListeners = screenshotListeners_;
    }
    PostListenerTask([screenshotListeners]() {
        for (auto& screenshotListener : screenshotListeners) {
            if (screenshotListener != nullptr) {
                screenshotListener->OnScreenshot();
            }
        }
    });
}

void WindowImpl::NotifyTouchOutside()
{
    std::vector<sptr<ITouchOutsideListener>> touchOutsideListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        touchOutsideListeners = touchOutsideListeners_;
    }
    PostListenerTask([touchOutsideListeners]() {
        for (auto& touchOutsideListener : touchOutsideListeners) {
            if (touchOutsideListener != nullptr) {
                touchOutsideListener->OnTouchOutside();
            }
        }
    });
}

void WindowImpl::NotifySizeChange(Rect rect, WindowSizeChangeReason reason)
{
    std::vector<sptr<IWindowChangeListener>> windowChangeListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        windowChangeListeners = windowChangeListeners_;
    }
    PostListenerTask([windowChangeListeners, rect, reason]() {
        for (auto& listener : windowChangeListeners) {
            if (listener != nullptr) {
                listener->OnSizeChange(rect, reason);
            }
        }
    });
}

void WindowImpl::NotifyKeyEvent(std::shared_ptr<MMI::KeyEvent> &keyEvent)
{
    std::vector<sptr<IInputEventListener>> inputEventListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        inputEventListeners = inputEventListeners_;
    }
    PostListenerTask([this, inputEventListeners, keyEvent]() mutable {
        for (auto& listener : inputEventListeners) {
            if (listener != nullptr) {
                listener->OnKeyEvent(keyEvent);
            }
        }
    });
}

void WindowImpl::NotifyPointEvent(std::shared_ptr<MMI::PointerEvent>& pointerEvent)
{
    std::vector<sptr<IInputEventListener>> inputEventListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        inputEventListeners = inputEventListeners_;
    }
    PostListenerTask([this, inputEventListeners, pointerEvent]() mutable {
        for (auto& listener : inputEventListeners) {
            if (listener != nullptr) {
                listener->OnPointerInputEvent(pointerEvent);
            }
        }
    });
}

void WindowImpl::NotifyAvoidAreaChange(const sptr<AvoidArea>& avoidArea, AvoidAreaType type)
{
    std::vector<sptr<IAvoidAreaChangedListener>> avoidAreaChangeListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        avoidAreaChangeListeners = avoidAreaChangeListeners_;
    }
    PostListenerTask([avoidAreaChangeListeners, outAvoidArea = *avoidArea, type]() {
        for (auto& listener : avoidAreaChangeListeners) {
            if (listener != nullptr) {
                listener->OnAvoidAreaChanged(outAvoidArea, type);
            }
        }
    });
}

void WindowImpl::NotifyDisplayMoveChange(DisplayId from, DisplayId to)
{
    std::vector<sptr<IDisplayMoveListener>> displayMoveListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        displayMoveListeners = displayMoveListeners_;
    }
    PostListenerTask([displayMoveListeners, from, to]() {
        for (auto& listener : displayMoveListeners) {
            if (listener != nullptr) {
                listener->OnDisplayMove(from, to);
            }
        }
    });
}

void WindowImpl::NotifyModeChange(WindowMode mode)
{
    std::vector<sptr<IWindowChangeListener>> windowChangeListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        windowChangeListeners = windowChangeListeners_;
    }
    PostListenerTask([windowChangeListeners, mode]() {
        for (auto& listener : windowChangeListeners) {
            if (listener != nullptr) {
                listener->OnModeChange(mode);
            }
        }
    });
}

void WindowImpl::NotifyOccupiedAreaChange(const sptr<OccupiedAreaChangeInfo>& info)
{
    std::vector<sptr<IOccupiedAreaChangeListener>> occupiedAreaChangeListeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        occupiedAreaChangeListeners = occupiedAreaChangeListeners_;
    }
    PostListenerTask([occupiedAreaChangeListeners, info]() mutable {
        for (auto& listener : occupiedAreaChangeListeners) {
            if (listener != nullptr) {
                listener->OnSizeChange(info);
            }
        }
    });
}

void WindowImpl::SetNeedRemoveWindowInputChannel(bool needRemoveWindowInputChannel)
{
    needRemoveWindowInputChannel_ = needRemoveWindowInputChannel;
}

Rect WindowImpl::GetSystemAlarmWindowDefaultSize(Rect defaultRect)
{
    auto display = DisplayManager::GetInstance().GetDisplayById(property_->GetDisplayId());
    if (display == nullptr) {
        WLOGFE("get display failed displayId:%{public}" PRIu64", window id:%{public}u", property_->GetDisplayId(),
            property_->GetWindowId());
        return defaultRect;
    }
    uint32_t width = static_cast<uint32_t>(display->GetWidth());
    uint32_t height = static_cast<uint32_t>(display->GetHeight());
    WLOGFI("width:%{public}u, height:%{public}u, displayId:%{public}" PRIu64"",
        width, height, property_->GetDisplayId());
    Rect rect;
    uint32_t alarmWidth = static_cast<uint32_t>((static_cast<float>(width) *
        SYSTEM_ALARM_WINDOW_WIDTH_RATIO));
    uint32_t alarmHeight = static_cast<uint32_t>((static_cast<float>(height) *
        SYSTEM_ALARM_WINDOW_HEIGHT_RATIO));

    rect = { static_cast<int32_t>((width - alarmWidth) / 2), static_cast<int32_t>((height - alarmHeight) / 2),
                alarmWidth, alarmHeight }; // divided by 2 to middle the window
    return rect;
}

void WindowImpl::SetDefaultOption()
{
    switch (property_->GetWindowType()) {
        case WindowType::WINDOW_TYPE_STATUS_BAR:
        case WindowType::WINDOW_TYPE_NAVIGATION_BAR:
        case WindowType::WINDOW_TYPE_VOLUME_OVERLAY:
        case WindowType::WINDOW_TYPE_INPUT_METHOD_FLOAT: {
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
            property_->SetFocusable(false);
            break;
        }
        case WindowType::WINDOW_TYPE_SYSTEM_ALARM_WINDOW: {
            property_->SetRequestRect(GetSystemAlarmWindowDefaultSize(property_->GetRequestRect()));
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
            break;
        }
        case WindowType::WINDOW_TYPE_KEYGUARD: {
            RemoveWindowFlag(WindowFlag::WINDOW_FLAG_NEED_AVOID);
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FULLSCREEN);
            break;
        }
        case WindowType::WINDOW_TYPE_DRAGGING_EFFECT: {
            property_->SetWindowFlags(0);
            break;
        }
        case WindowType::WINDOW_TYPE_APP_COMPONENT: {
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
            property_->SetAnimationFlag(static_cast<uint32_t>(WindowAnimation::NONE));
            break;
        }
        case WindowType::WINDOW_TYPE_TOAST:
        case WindowType::WINDOW_TYPE_FLOAT:
        case WindowType::WINDOW_TYPE_FLOAT_CAMERA:
        case WindowType::WINDOW_TYPE_VOICE_INTERACTION:
        case WindowType::WINDOW_TYPE_LAUNCHER_DOCK:
        case WindowType::WINDOW_TYPE_SEARCHING_BAR:
        case WindowType::WINDOW_TYPE_SCREENSHOT:
        case WindowType::WINDOW_TYPE_DIALOG: {
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
            break;
        }
        case WindowType::WINDOW_TYPE_BOOT_ANIMATION:
        case WindowType::WINDOW_TYPE_POINTER: {
            property_->SetFocusable(false);
            break;
        }
        case WindowType::WINDOW_TYPE_DOCK_SLICE: {
            property_->SetWindowMode(WindowMode::WINDOW_MODE_FLOATING);
            property_->SetFocusable(false);
            break;
        }
        default:
            break;
    }
}

bool WindowImpl::IsWindowValid() const
{
    bool res = ((state_ > WindowState::STATE_INITIAL) && (state_ < WindowState::STATE_BOTTOM));
    if (!res) {
        WLOGFI("window is already destroyed or not created! id: %{public}u", GetWindowId());
    }
    return res;
}

bool WindowImpl::IsLayoutFullScreen() const
{
    uint32_t flags = GetWindowFlags();
    auto mode = GetMode();
    bool needAvoid = (flags & static_cast<uint32_t>(WindowFlag::WINDOW_FLAG_NEED_AVOID));
    return (mode == WindowMode::WINDOW_MODE_FULLSCREEN && !needAvoid);
}

bool WindowImpl::IsFullScreen() const
{
    auto statusProperty = GetSystemBarPropertyByType(WindowType::WINDOW_TYPE_STATUS_BAR);
    auto naviProperty = GetSystemBarPropertyByType(WindowType::WINDOW_TYPE_NAVIGATION_BAR);
    return (IsLayoutFullScreen() && !statusProperty.enable_ && !naviProperty.enable_);
}

void WindowImpl::SetRequestedOrientation(Orientation orientation)
{
    if (property_->GetRequestedOrientation() == orientation) {
        return;
    }
    property_->SetRequestedOrientation(orientation);
    if (state_ == WindowState::STATE_SHOWN) {
        UpdateProperty(PropertyChangeAction::ACTION_UPDATE_ORIENTATION);
    }
}

Orientation WindowImpl::GetRequestedOrientation()
{
    return property_->GetRequestedOrientation();
}

WMError WindowImpl::SetTouchHotAreas(const std::vector<Rect>& rects)
{
    std::vector<Rect> lastTouchHotAreas;
    property_->GetTouchHotAreas(lastTouchHotAreas);

    property_->SetTouchHotAreas(rects);
    WMError result = UpdateProperty(PropertyChangeAction::ACTION_UPDATE_TOUCH_HOT_AREA);
    if (result != WMError::WM_OK) {
        property_->SetTouchHotAreas(lastTouchHotAreas);
    }
    return result;
}
void WindowImpl::GetRequestedTouchHotAreas(std::vector<Rect>& rects) const
{
    property_->GetTouchHotAreas(rects);
}

bool WindowImpl::CheckCameraFloatingWindowMultiCreated(WindowType type)
{
    if (type != WindowType::WINDOW_TYPE_FLOAT_CAMERA) {
        return false;
    }

    for (auto& winPair : windowMap_) {
        if (winPair.second.second->GetType() == WindowType::WINDOW_TYPE_FLOAT_CAMERA) {
            return true;
        }
    }
    uint32_t accessTokenId = static_cast<uint32_t>(IPCSkeleton::GetCallingTokenID());
    property_->SetAccessTokenId(accessTokenId);
    WLOGFI("Create camera float window, accessTokenId = %{public}u", accessTokenId);
    return false;
}
} // namespace Rosen
} // namespace OHOS
