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
#include "js_window_manager.h"
#include <ability.h>
#include <cinttypes>
#include <new>
#include "ability_context.h"
#include "display_manager.h"
#include "dm_common.h"
#include "js_window.h"
#include "js_window_utils.h"
#include "window_helper.h"
#include "window_manager_hilog.h"
#include "window_option.h"
#include "singleton_container.h"
namespace OHOS {
namespace Rosen {
using namespace AbilityRuntime;
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "JsWindowManager"};
}

JsWindowManager::JsWindowManager() : registerManager_(std::make_unique<JsWindowRegisterManager>())
{
}

JsWindowManager::~JsWindowManager()
{
}

void JsWindowManager::Finalizer(NativeEngine* engine, void* data, void* hint)
{
    WLOGFI("[NAPI]Finalizer");
    std::unique_ptr<JsWindowManager>(static_cast<JsWindowManager*>(data));
}

NativeValue* JsWindowManager::CreateWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnCreateWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::FindWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnFindWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::MinimizeAll(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnMinimizeAll(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::ToggleShownStateForAllAppWindows(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnToggleShownStateForAllAppWindows(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::RegisterWindowManagerCallback(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnRegisterWindowMangerCallback(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::UnregisterWindowMangerCallback(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnUnregisterWindowManagerCallback(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::GetTopWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnGetTopWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowManager::SetWindowLayoutMode(NativeEngine* engine, NativeCallbackInfo* info)
{
    JsWindowManager* me = CheckParamsAndGetThis<JsWindowManager>(engine, info);
    return (me != nullptr) ? me->OnSetWindowLayoutMode(*engine, *info) : nullptr;
}

static void GetNativeContext(NativeEngine& engine, NativeValue* nativeContext, void*& contextPtr, WMError& errCode)
{
    AppExecFwk::Ability* ability = nullptr;
    bool isOldApi = GetAPI7Ability(engine, ability);
    WLOGFI("[NAPI]FA mode:%{public}u", isOldApi);
    if (isOldApi) {
        return;
    }
    if (nativeContext != nullptr) {
        auto objContext = AbilityRuntime::ConvertNativeValueTo<NativeObject>(nativeContext);
        if (objContext == nullptr) {
            WLOGFE("[NAPI]ConvertNativeValueTo Context Object failed");
            errCode = WMError::WM_ERROR_INVALID_PARAM;
            return;
        }
        contextPtr = objContext->GetNativePointer();
    }
}

static bool GetWindowTypeAndParentName(NativeEngine& engine, std::string& parentName, WindowType& winType,
    NativeValue* nativeString, NativeValue* nativeType)
{
    NativeNumber* type = ConvertNativeValueTo<NativeNumber>(nativeType);
    if (type == nullptr) {
        WLOGFE("[NAPI]Failed to convert parameter to windowType");
        return false;
    }
    // adapt to the old version
    if (static_cast<uint32_t>(*type) >= static_cast<uint32_t>(WindowType::SYSTEM_WINDOW_BASE)) {
        winType = static_cast<WindowType>(static_cast<uint32_t>(*type));
    } else {
        if (static_cast<uint32_t>(*type) >= static_cast<uint32_t>(ApiWindowType::TYPE_BASE) &&
            static_cast<uint32_t>(*type) < static_cast<uint32_t>(ApiWindowType::TYPE_END)) {
            winType = JS_TO_NATIVE_WINDOW_TYPE_MAP.at(static_cast<ApiWindowType>(static_cast<uint32_t>(*type)));
        } else {
            WLOGFE("[NAPI]Type %{public}u is not supported", static_cast<uint32_t>(*type));
            return false;
        }
    }
    AppExecFwk::Ability* ability = nullptr;
    bool isOldApi = GetAPI7Ability(engine, ability);
    if (isOldApi) {
        if (ability == nullptr || !WindowHelper::IsSubWindow(winType)) {
            WLOGE("[NAPI]FA mode GetAPI7Ability failed or type %{public}u is not subWinodw", winType);
            return false;
        }
        auto window = ability->GetWindow();
        if (window == nullptr) {
            WLOGE("[NAPI]Get mainWindow failed");
            return false;
        }
        parentName = window->GetWindowName();
    } else {
        if (!WindowHelper::IsSystemWindow(winType)) {
            WLOGFE("[NAPI]Only SystemWindow support create in stage mode, type is %{public}u", winType);
            return false;
        }
    }
    return true;
}

static void CreateSystemWindowTask(void* contextPtr, std::string windowName, WindowType winType,
    NativeEngine& engine, AsyncTask& task)
{
    WLOGFI("[NAPI]CreateSystemWindowTask");
    auto context = static_cast<std::weak_ptr<AbilityRuntime::Context>*>(contextPtr);
    if (contextPtr == nullptr || context == nullptr) {
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Context is nullptr"));
        WLOGFE("[NAPI]Context is nullptr");
        return;
    }
    if (winType == WindowType::WINDOW_TYPE_FLOAT || winType == WindowType::WINDOW_TYPE_FLOAT_CAMERA) {
        auto abilityContext = Context::ConvertTo<AbilityRuntime::AbilityContext>(context->lock());
        if (abilityContext != nullptr) {
            if (!CheckCallingPermission("ohos.permission.SYSTEM_FLOAT_WINDOW")) {
                task.Reject(engine, CreateJsError(engine,
                    static_cast<int32_t>(WMError::WM_ERROR_INVALID_PERMISSION),
                    "TYPE_FLOAT CheckCallingPermission failed"));
                return;
            }
        }
    }
    sptr<WindowOption> windowOption = new(std::nothrow) WindowOption();
    if (windowOption == nullptr) {
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "New window option failed"));
        WLOGFE("[NAPI]New window option failed");
        return;
    }
    windowOption->SetWindowType(winType);
    sptr<Window> window = Window::Create(windowName, windowOption, context->lock());
    if (window != nullptr) {
        task.Resolve(engine, CreateJsWindowObject(engine, window));
    } else {
        WLOGFE("[NAPI]Create window failed");
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Create window failed"));
    }
}

static void CreateSubWindowTask(std::string parentWinName, std::string windowName, WindowType winType,
    NativeEngine& engine, AsyncTask& task)
{
    WLOGFI("[NAPI]CreateSubWindowTask, parent name = %{public}s", parentWinName.c_str());
    sptr<WindowOption> windowOption = new(std::nothrow) WindowOption();
    if (windowOption == nullptr) {
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "New window option failed"));
        WLOGFE("[NAPI]New window option failed");
        return;
    }
    windowOption->SetWindowType(winType);
    windowOption->SetWindowMode(Rosen::WindowMode::WINDOW_MODE_FLOATING);
    windowOption->SetParentName(parentWinName);
    sptr<Window> window = Window::Create(windowName, windowOption);
    if (window != nullptr) {
        task.Resolve(engine, CreateJsWindowObject(engine, window));
    } else {
        WLOGFE("[NAPI]Create window failed");
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Create window failed"));
    }
}

NativeValue* JsWindowManager::OnCreateWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnCreateWindow");
    NativeValue* nativeString = nullptr;
    NativeValue* nativeContext = nullptr;
    NativeValue* nativeType = nullptr;
    NativeValue* callback = nullptr;
    if (info.argc >= 2 && info.argv[0]->TypeOf() == NATIVE_STRING) { // 2: minimum params num
        nativeString = info.argv[0];
        nativeType = info.argv[1];
        // 2: minimum params num
        callback = (info.argc == 2) ? nullptr :
            (info.argv[2]->TypeOf() == NATIVE_FUNCTION ? info.argv[2] : nullptr); // 2: index of callback
    } else if (info.argc >= 3) { // 3: minimum params num
        nativeContext = info.argv[0]->TypeOf() == NATIVE_OBJECT ? info.argv[0] : nullptr;
        nativeString = info.argv[1];
        nativeType = info.argv[2]; // 2: index of type
        // 3: minimum params num;
        callback = (info.argc == 3) ? nullptr :
            (info.argv[3]->TypeOf() == NATIVE_FUNCTION ? info.argv[3] : nullptr); // 3: index of callback
    }
    std::string windowName;
    WMError errCode = WMError::WM_OK;
    if (!ConvertFromJsValue(engine, nativeString, windowName)) {
        WLOGFE("[NAPI]Failed to convert parameter to windowName");
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    std::string parentName;
    WindowType winType = WindowType::SYSTEM_WINDOW_BASE;
    if (errCode == WMError::WM_OK &&
        !GetWindowTypeAndParentName(engine, parentName, winType, nativeString, nativeType)) {
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    void* contextPtr = nullptr;
    GetNativeContext(engine, nativeContext, contextPtr, errCode);

    WLOGFI("[NAPI]Window name = %{public}s, type = %{public}u, err = %{public}d", windowName.c_str(), winType, errCode);
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            if (parentName.empty()) {
                return CreateSystemWindowTask(contextPtr, windowName, winType, engine, task);
            } else {
                return CreateSubWindowTask(parentName, windowName, winType, engine, task);
            }
        };
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnCreateWindow", engine,
        CreateAsyncTaskWithLastParam(engine, callback, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManager::OnFindWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnFindWindow");
    std::string windowName;
    WMError errCode = WMError::WM_OK;
    if (info.argc < 1 || info.argc > 2) { // 2: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    } else {
        if (!ConvertFromJsValue(engine, info.argv[0], windowName)) {
            WLOGFE("[NAPI]Failed to convert parameter to windowName");
            errCode = WMError::WM_ERROR_INVALID_PARAM;
        }
    }
    WLOGFI("[NAPI]Window name = %{public}s, err = %{public}d", windowName.c_str(), errCode);
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            std::shared_ptr<NativeReference> jsWindowObj = FindJsWindowObject(windowName);
            if (jsWindowObj != nullptr && jsWindowObj->Get() != nullptr) {
                WLOGFI("[NAPI]Find window: %{public}s, use exist js window", windowName.c_str());
                task.Resolve(engine, jsWindowObj->Get());
            } else {
                sptr<Window> window = Window::Find(windowName);
                if (window == nullptr) {
                    WLOGFE("[NAPI]Cannot find window: %{public}s", windowName.c_str());
                    task.Reject(engine, CreateJsError(engine,
                        static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Cannot find window"));
                } else {
                    task.Resolve(engine, CreateJsWindowObject(engine, window));
                    WLOGFI("[NAPI]Find window: %{public}s, create js window", windowName.c_str());
                }
            }
        };

    NativeValue* lastParam = (info.argc <= 1) ? nullptr :
        (info.argv[1]->TypeOf() == NATIVE_FUNCTION ? info.argv[1] : nullptr);
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnFindWindow",
        engine, CreateAsyncTaskWithLastParam(engine, lastParam, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManager::OnMinimizeAll(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnMinimizeAll");
    WMError errCode = WMError::WM_OK;
    if (info.argc < 1 || info.argc > 2) { // 2: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    int64_t displayId = static_cast<int64_t>(DISPLAY_ID_INVALID);
    if (errCode == WMError::WM_OK && !ConvertFromJsValue(engine, info.argv[0], displayId)) {
        WLOGFE("[NAPI]Failed to convert parameter to displayId");
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    if (displayId < 0 ||
        SingletonContainer::Get<DisplayManager>().GetDisplayById(static_cast<uint64_t>(displayId)) == nullptr) {
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }

    WLOGFI("[NAPI]Display id = %{public}" PRIu64", err = %{public}d", static_cast<uint64_t>(displayId), errCode);
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            SingletonContainer::Get<WindowManager>().MinimizeAllAppWindows(static_cast<uint64_t>(displayId));
            task.Resolve(engine, engine.CreateUndefined());
            WLOGFI("[NAPI]OnMinimizeAll success");
        };
    NativeValue* lastParam = (info.argc <= 1) ? nullptr :
        (info.argv[1]->TypeOf() == NATIVE_FUNCTION ? info.argv[1] : nullptr);
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnMinimizeAll",
        engine, CreateAsyncTaskWithLastParam(engine, lastParam, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManager::OnToggleShownStateForAllAppWindows(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnToggleShownStateForAllAppWindows");
    WMError errCode = WMError::WM_OK;
    if (info.argc < 0 || info.argc > 1) { // 1: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            WMError ret = SingletonContainer::Get<WindowManager>().ToggleShownStateForAllAppWindows();
            if (ret == WMError::WM_OK) {
                task.Resolve(engine, engine.CreateUndefined());
                WLOGFI("[NAPI]OnToggleShownStateForAllAppWindows success");
            } else {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(ret),
                    "OnToggleShownStateForAllAppWindows failed"));
            }
        };
    NativeValue* lastParam = (info.argc <= 0) ? nullptr :
        (info.argv[0]->TypeOf() == NATIVE_FUNCTION ? info.argv[0] : nullptr);
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnToggleShownStateForAllAppWindows",
        engine, CreateAsyncTaskWithLastParam(engine, lastParam, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManager::OnRegisterWindowMangerCallback(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnRegisterWindowMangerCallback");
    if (info.argc != 2) { // 2: params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        return engine.CreateUndefined();
    }
    std::string cbType;
    if (!ConvertFromJsValue(engine, info.argv[0], cbType)) {
        WLOGFE("[NAPI]Failed to convert parameter to callbackType");
        return engine.CreateUndefined();
    }
    NativeValue* value = info.argv[1];
    if (!value->IsCallable()) {
        WLOGFI("[NAPI]Callback(argv[1]) is not callable");
        return engine.CreateUndefined();
    }

    registerManager_->RegisterListener(nullptr, cbType, CaseType::CASE_WINDOW_MANAGER, engine, value);
    WLOGFI("[NAPI]Register end, type = %{public}s, callback = %{public}p", cbType.c_str(), value);
    return engine.CreateUndefined();
}

NativeValue* JsWindowManager::OnUnregisterWindowManagerCallback(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnUnregisterWindowManagerCallback");
    if (info.argc < 1 || info.argc > 2) { // 2: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        return engine.CreateUndefined();
    }
    std::string cbType;
    if (!ConvertFromJsValue(engine, info.argv[0], cbType)) {
        WLOGFE("[NAPI]Failed to convert parameter to callbackType");
        return engine.CreateUndefined();
    }

    NativeValue* value = nullptr;
    if (info.argc == 1) {
        registerManager_->UnregisterListener(nullptr, cbType, CaseType::CASE_WINDOW_MANAGER, value);
    } else {
        value = info.argv[1];
        if (!value->IsCallable()) {
            WLOGFI("[NAPI]Callback(argv[1]) is not callable");
            return engine.CreateUndefined();
        }
        registerManager_->UnregisterListener(nullptr, cbType, CaseType::CASE_WINDOW_MANAGER, value);
    }
    WLOGFI("[NAPI]Unregister end, type = %{public}s, callback = %{public}p", cbType.c_str(), value);
    return engine.CreateUndefined();
}

static void GetTopWindowTask(void* contextPtr, NativeEngine& engine, AsyncTask& task)
{
    std::string windowName;
    sptr<Window> window = nullptr;
    AppExecFwk::Ability* ability = nullptr;
    bool isOldApi = GetAPI7Ability(engine, ability);
    if (isOldApi) {
        if (ability->GetWindow() == nullptr) {
            task.Reject(engine, CreateJsError(engine,
                static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "FA mode can not get ability window"));
            WLOGE("[NAPI]FA mode can not get ability window");
            return;
        }
        window = Window::GetTopWindowWithId(ability->GetWindow()->GetWindowId());
    } else {
        auto context = static_cast<std::weak_ptr<AbilityRuntime::Context>*>(contextPtr);
        if (contextPtr == nullptr || context == nullptr) {
            task.Reject(engine, CreateJsError(engine,
                static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Stage mode without context"));
            WLOGFE("[NAPI]Stage mode without context");
            return;
        }
        window = Window::GetTopWindowWithContext(context->lock());
    }
    if (window == nullptr) {
        task.Reject(engine, CreateJsError(engine,
            static_cast<int32_t>(WMError::WM_ERROR_NULLPTR), "Get top window failed"));
        WLOGFE("[NAPI]Get top window failed");
        return;
    }
    windowName = window->GetWindowName();
    std::shared_ptr<NativeReference> jsWindowObj = FindJsWindowObject(windowName);
    if (jsWindowObj != nullptr && jsWindowObj->Get() != nullptr) {
        task.Resolve(engine, jsWindowObj->Get());
    } else {
        task.Resolve(engine, CreateJsWindowObject(engine, window));
    }
    WLOGFI("[NAPI]Get top window %{public}s success", windowName.c_str());
    return;
}

NativeValue* JsWindowManager::OnGetTopWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnGetTopWindow");
    WMError errCode = WMError::WM_OK;
    NativeValue* nativeContext = nullptr;
    NativeValue* nativeCallback = nullptr;
    void* contextPtr = nullptr;
    if (info.argc > 2) { // 2: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    } else {
        if (info.argc > 0 && info.argv[0]->TypeOf() == NATIVE_OBJECT) { // (context, callback?)
            nativeContext = info.argv[0];
            nativeCallback = (info.argc == 1) ? nullptr :
                (info.argv[1]->TypeOf() == NATIVE_FUNCTION ? info.argv[1] : nullptr);
        } else { // (callback?)
            nativeCallback = (info.argc == 0) ? nullptr :
                (info.argv[0]->TypeOf() == NATIVE_FUNCTION ? info.argv[0] : nullptr);
        }
        GetNativeContext(engine, nativeContext, contextPtr, errCode);
    }

    WLOGFI("[NAPI]Context %{public}p, err %{public}u", contextPtr, errCode);
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            return GetTopWindowTask(contextPtr, engine, task);
        };
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnGetTopWindow",
        engine, CreateAsyncTaskWithLastParam(engine, nativeCallback, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManager::OnSetWindowLayoutMode(NativeEngine& engine, NativeCallbackInfo& info)
{
    WLOGFI("[NAPI]OnSetWindowLayoutMode");
    WMError errCode = WMError::WM_OK;
    if (info.argc < 1 || info.argc > 2) { // 1: minimum params num; 2: maximum params num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    WindowLayoutMode winLayoutMode = WindowLayoutMode::CASCADE;
    if (errCode == WMError::WM_OK) {
        NativeNumber* nativeMode = ConvertNativeValueTo<NativeNumber>(info.argv[0]);
        if (nativeMode == nullptr) {
            WLOGFE("[NAPI]Failed to convert parameter to windowLayoutMode");
            errCode = WMError::WM_ERROR_INVALID_PARAM;
        } else {
            winLayoutMode = static_cast<WindowLayoutMode>(static_cast<uint32_t>(*nativeMode));
        }
    }
    if (winLayoutMode != WindowLayoutMode::CASCADE && winLayoutMode != WindowLayoutMode::TILE) {
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }

    WLOGFI("[NAPI]LayoutMode = %{public}u, err = %{public}d", winLayoutMode, errCode);
    AsyncTask::CompleteCallback complete =
        [=](NativeEngine& engine, AsyncTask& task, int32_t status) {
            if (errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode), "Invalidate params"));
                return;
            }
            WMError ret = SingletonContainer::Get<WindowManager>().SetWindowLayoutMode(winLayoutMode);
            if (ret == WMError::WM_OK) {
                task.Resolve(engine, engine.CreateUndefined());
                WLOGFI("[NAPI]SetWindowLayoutMode success");
            } else {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(ret), "SetWindowLayoutMode failed"));
            }
        };
    // 1: maximum params num; 1: index of callback
    NativeValue* lastParam = (info.argc <= 1) ? nullptr :
        (info.argv[1]->TypeOf() == NATIVE_FUNCTION ? info.argv[1] : nullptr);
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowManager::OnSetWindowLayoutMode",
        engine, CreateAsyncTaskWithLastParam(engine, lastParam, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowManagerInit(NativeEngine* engine, NativeValue* exportObj)
{
    WLOGFI("[NAPI]JsWindowManagerInit");

    if (engine == nullptr || exportObj == nullptr) {
        WLOGFE("[NAPI]JsWindowManagerInit engine or exportObj is nullptr");
        return nullptr;
    }

    NativeObject* object = ConvertNativeValueTo<NativeObject>(exportObj);
    if (object == nullptr) {
        WLOGFE("[NAPI]JsWindowManagerInit object is nullptr");
        return nullptr;
    }

    std::unique_ptr<JsWindowManager> jsWinManager = std::make_unique<JsWindowManager>();
    object->SetNativePointer(jsWinManager.release(), JsWindowManager::Finalizer, nullptr);
    object->SetProperty("WindowType", WindowTypeInit(engine));
    object->SetProperty("AvoidAreaType", AvoidAreaTypeInit(engine));
    object->SetProperty("WindowMode", WindowModeInit(engine));
    object->SetProperty("ColorSpace", ColorSpaceInit(engine));
    object->SetProperty("WindowStageEventType", WindowStageEventTypeInit(engine));
    object->SetProperty("WindowLayoutMode", WindowLayoutModeInit(engine));
    object->SetProperty("Orientation", OrientationInit(engine));
    BindNativeFunction(*engine, *object, "create", JsWindowManager::CreateWindow);
    BindNativeFunction(*engine, *object, "find", JsWindowManager::FindWindow);
    BindNativeFunction(*engine, *object, "on", JsWindowManager::RegisterWindowManagerCallback);
    BindNativeFunction(*engine, *object, "off", JsWindowManager::UnregisterWindowMangerCallback);
    BindNativeFunction(*engine, *object, "getTopWindow", JsWindowManager::GetTopWindow);
    BindNativeFunction(*engine, *object, "minimizeAll", JsWindowManager::MinimizeAll);
    BindNativeFunction(*engine, *object, "toggleShownStateForAllAppWindows",
        JsWindowManager::ToggleShownStateForAllAppWindows);
    BindNativeFunction(*engine, *object, "setWindowLayoutMode", JsWindowManager::SetWindowLayoutMode);
    return engine->CreateUndefined();
}
}  // namespace Rosen
}  // namespace OHOS
