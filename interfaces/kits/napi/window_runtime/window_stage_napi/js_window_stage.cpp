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

#include "js_window_stage.h"
#include <string>
#include "js_runtime_utils.h"
#include "js_window.h"
#include "js_window_register_manager.h"
#include "js_window_utils.h"
#include "window_manager_hilog.h"
namespace OHOS {
namespace Rosen {
using namespace AbilityRuntime;
namespace {
const int CONTENT_STORAGE_ARG = 2;
constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "JsWindowStage"};
} // namespace

std::unique_ptr<JsWindowRegisterManager> g_listenerManager = std::make_unique<JsWindowRegisterManager>();
JsWindowStage::JsWindowStage(const std::shared_ptr<Rosen::WindowScene>& windowScene)
    : windowScene_(windowScene)
{
}

JsWindowStage::~JsWindowStage()
{
}

void JsWindowStage::Finalizer(NativeEngine* engine, void* data, void* hint)
{
    WLOGFI("[NAPI]Finalizer");
    std::unique_ptr<JsWindowStage>(static_cast<JsWindowStage*>(data));
}

NativeValue* JsWindowStage::SetUIContent(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]SetUIContent");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnSetUIContent(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::GetMainWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]GetMainWindow");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnGetMainWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::On(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]On");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnEvent(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::Off(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]Off");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OffEvent(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::LoadContent(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]LoadContent");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnLoadContent(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::GetWindowMode(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]GetWindowMode");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnGetWindowMode(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::CreateSubWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]CreateSubWindow");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnCreateSubWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::GetSubWindow(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]GetSubWindow");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnGetSubWindow(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::SetShowOnLockScreen(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]SetShowOnLockScreen");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnSetShowOnLockScreen(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::DisableWindowDecor(NativeEngine* engine, NativeCallbackInfo* info)
{
    WLOGFI("[NAPI]DisableWindowDecor");
    JsWindowStage* me = CheckParamsAndGetThis<JsWindowStage>(engine, info);
    return (me != nullptr) ? me->OnDisableWindowDecor(*engine, *info) : nullptr;
}

NativeValue* JsWindowStage::OnSetUIContent(NativeEngine& engine, NativeCallbackInfo& info)
{
    if (info.argc < 2) { // 2: minimum param num
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        return engine.CreateUndefined();
    }
    auto weakScene = windowScene_.lock();
    if (weakScene == nullptr || weakScene->GetMainWindow() == nullptr) {
        WLOGFE("[NAPI]WindowScene is null or window is null");
        return engine.CreateUndefined();
    }

    // Parse info->argv[0] as abilitycontext
    auto objContext = ConvertNativeValueTo<NativeObject>(info.argv[0]);
    if (objContext == nullptr) {
        WLOGFE("[NAPI]Context is nullptr");
        return engine.CreateUndefined();
    }

    // Parse info->argv[1] as url
    std::string contextUrl;
    if (!ConvertFromJsValue(engine, info.argv[1], contextUrl)) {
        WLOGFE("[NAPI]Failed to convert parameter to url");
        return engine.CreateUndefined();
    }
    weakScene->GetMainWindow()->SetUIContent(contextUrl, &engine, info.argv[CONTENT_STORAGE_ARG]);
    return engine.CreateUndefined();
}

NativeValue* JsWindowStage::OnGetMainWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    AsyncTask::CompleteCallback complete =
        [weak = windowScene_](NativeEngine& engine, AsyncTask& task, int32_t status) {
            auto weakScene = weak.lock();
            if (weakScene == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR)));
                WLOGFE("[NAPI]WindowScene_ is nullptr!");
                return;
            }
            auto window = weakScene->GetMainWindow();
            if (window != nullptr) {
                task.Resolve(engine, OHOS::Rosen::CreateJsWindowObject(engine, window));
                WLOGFI("[NAPI]Get main window [%{public}u, %{public}s]",
                    window->GetWindowId(), window->GetWindowName().c_str());
            } else {
                task.Reject(engine, CreateJsError(engine,
                    static_cast<int32_t>(Rosen::WMError::WM_ERROR_NULLPTR),
                    "Get main window failed."));
            }
        };
    NativeValue* callback = (info.argv[0]->TypeOf() == NATIVE_FUNCTION) ? info.argv[0] : nullptr;
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowStage::OnGetMainWindow",
        engine, CreateAsyncTaskWithLastParam(engine, callback, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowStage::OnEvent(NativeEngine& engine, NativeCallbackInfo& info)
{
    auto weakScene = windowScene_.lock();
    if (weakScene == nullptr || info.argc < 2) { // 2: minimum param nums
        WLOGFE("[NAPI]Window scene is null or argc is invalid: %{public}zu", info.argc);
        return engine.CreateUndefined();
    }
    // Parse info->argv[0] as string
    std::string eventString;
    if (!ConvertFromJsValue(engine, info.argv[0], eventString)) {
        WLOGFE("[NAPI]Failed to convert parameter to string");
        return engine.CreateUndefined();
    }
    NativeValue* value = info.argv[1];
    if (!value->IsCallable()) {
        WLOGFE("[NAPI]Callback(info->argv[1]) is not callable");
        return engine.CreateUndefined();
    }

    auto window = weakScene->GetMainWindow();
    if (window == nullptr) {
        WLOGFE("[NAPI]Get window failed");
        return engine.CreateUndefined();
    }
    g_listenerManager->RegisterListener(window, eventString, CaseType::CASE_STAGE, engine, value);
    WLOGFI("[NAPI]Window [%{public}u, %{public}s] register event %{public}s, callback %{public}p",
        window->GetWindowId(), window->GetWindowName().c_str(), eventString.c_str(), value);

    return engine.CreateUndefined();
}

NativeValue* JsWindowStage::OffEvent(NativeEngine& engine, NativeCallbackInfo& info)
{
    auto weakScene = windowScene_.lock();
    if (weakScene == nullptr) {
        WLOGFE("[NAPI]Window scene is null");
        return engine.CreateUndefined();
    }

    // Parse info->argv[0] as string
    std::string eventString;
    if (!ConvertFromJsValue(engine, info.argv[0], eventString)) {
        WLOGFE("[NAPI]Failed to convert parameter to string");
        return engine.CreateUndefined();
    }
    if (eventString.compare("windowStageEvent") != 0) {
        WLOGFE("[NAPI]Envent %{public}s is invalid", eventString.c_str());
        return engine.CreateUndefined();
    }

    NativeValue* value = info.argv[1];
    auto window = weakScene->GetMainWindow();
    if (window == nullptr) {
        WLOGFE("[NAPI]Get window failed");
        return engine.CreateUndefined();
    }
    if (value->TypeOf() == NATIVE_FUNCTION) {
        g_listenerManager->UnregisterListener(window, eventString, CaseType::CASE_STAGE, value);
    } else if (value->TypeOf() == NativeValueType::NATIVE_UNDEFINED) {
        g_listenerManager->UnregisterListener(window, eventString, CaseType::CASE_STAGE, nullptr);
    } else {
        WLOGFE("[NAPI]Callback(info->argv[1]) is invalid");
    }
    WLOGFI("[NAPI]Window [%{public}u, %{public}s] unregister event %{public}s, callback %{public}p",
        window->GetWindowId(), window->GetWindowName().c_str(), eventString.c_str(), value);

    return engine.CreateUndefined();
}

static void LoadContentTask(std::weak_ptr<NativeReference> contentStorage, std::string contextUrl,
    sptr<Window> weakWindow, NativeEngine& engine, AsyncTask& task)
{
    NativeValue* nativeStorage = (contentStorage.lock() == nullptr) ? nullptr : contentStorage.lock()->Get();
    WMError ret = weakWindow->SetUIContent(contextUrl, &engine, nativeStorage, false);
    if (ret == WMError::WM_OK) {
        task.Resolve(engine, engine.CreateUndefined());
    } else {
        task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(ret), "Window load content failed"));
    }
    WLOGFI("[NAPI]Window [%{public}u, %{public}s] load content end, ret = %{public}d",
        weakWindow->GetWindowId(), weakWindow->GetWindowName().c_str(), ret);
    return;
}

NativeValue* JsWindowStage::OnLoadContent(NativeEngine& engine, NativeCallbackInfo& info)
{
    WMError errCode = WMError::WM_OK;
    std::string contextUrl;
    if (!ConvertFromJsValue(engine, info.argv[0], contextUrl)) {
        WLOGFE("[NAPI]Failed to convert parameter to context url");
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    NativeValue* storage = nullptr;
    NativeValue* callBack = nullptr;
    NativeValue* value1 = info.argv[1];
    NativeValue* value2 = info.argv[2]; // 2: param index
    if (value1->TypeOf() == NATIVE_FUNCTION) {
        callBack = value1;
    } else if (value1->TypeOf() == NATIVE_OBJECT) {
        storage = value1;
    }
    if (value2->TypeOf() == NATIVE_FUNCTION) {
        callBack = value2;
    }
    std::weak_ptr<NativeReference> contentStorage = (storage == nullptr) ? nullptr :
        std::shared_ptr<NativeReference>(engine.CreateReference(storage, 1));
    AsyncTask::CompleteCallback complete =
        [weak = windowScene_, contentStorage, contextUrl, errCode](
            NativeEngine& engine, AsyncTask& task, int32_t status) {
            auto weakScene = weak.lock();
            if (weakScene == nullptr || errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode)));
                WLOGFE("[NAPI]Window scene is null or get invalid param");
                return;
            }
            auto win = weakScene->GetMainWindow();
            if (win == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR)));
                WLOGFE("[NAPI]Get window failed");
                return;
            }
            LoadContentTask(contentStorage, contextUrl, win, engine, task);
        };
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowStage::OnLoadContent",
        engine, CreateAsyncTaskWithLastParam(engine, callBack, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowStage::OnGetWindowMode(NativeEngine& engine, NativeCallbackInfo& info)
{
    AsyncTask::CompleteCallback complete =
        [weak = windowScene_](NativeEngine& engine, AsyncTask& task, int32_t status) {
            auto weakScene = weak.lock();
            if (weakScene == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR)));
                WLOGFE("[NAPI]windowScene_ is nullptr");
                return;
            }
            auto window = weakScene->GetMainWindow();
            if (window == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(Rosen::WMError::WM_ERROR_NULLPTR),
                    "Get window failed"));
                WLOGFE("[NAPI]Get window failed");
                return;
            }
            Rosen::WindowMode mode = window->GetMode();
            if (NATIVE_TO_JS_WINDOW_MODE_MAP.count(mode) != 0) {
                task.Resolve(engine, CreateJsValue(engine, NATIVE_TO_JS_WINDOW_MODE_MAP.at(mode)));
                WLOGFI("[NAPI]Window [%{public}u, %{public}s] get mode %{public}u, api mode %{public}u",
                    window->GetWindowId(), window->GetWindowName().c_str(),
                    mode, NATIVE_TO_JS_WINDOW_MODE_MAP.at(mode));
            } else {
                task.Resolve(engine, CreateJsValue(engine, mode));
                WLOGFE("[NAPI]Get mode %{public}u, but not in apimode", mode);
            }
        };
    NativeValue* callback = (info.argv[0]->TypeOf() == NATIVE_FUNCTION) ? info.argv[0] : nullptr;
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowStage::OnGetWindowMode",
        engine, CreateAsyncTaskWithLastParam(engine, callback, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowStage::OnCreateSubWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    WMError errCode = WMError::WM_OK;
    std::string windowName;
    if (!ConvertFromJsValue(engine, info.argv[0], windowName)) {
        WLOGFE("[NAPI]Failed to convert parameter to windowName");
        errCode = WMError::WM_ERROR_INVALID_PARAM;
    }
    AsyncTask::CompleteCallback complete =
        [weak = windowScene_, windowName, errCode](NativeEngine& engine, AsyncTask& task, int32_t status) {
            auto weakScene = weak.lock();
            if (weakScene == nullptr || errCode != WMError::WM_OK) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(errCode)));
                WLOGFE("[NAPI]Window scene is null or get invalid param");
                return;
            }
            sptr<Rosen::WindowOption> windowOption = new Rosen::WindowOption();
            windowOption->SetWindowType(Rosen::WindowType::WINDOW_TYPE_APP_SUB_WINDOW);
            windowOption->SetWindowMode(Rosen::WindowMode::WINDOW_MODE_FLOATING);
            auto window = weakScene->CreateWindow(windowName, windowOption);
            if (window == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(Rosen::WMError::WM_ERROR_NULLPTR),
                    "Get window failed"));
                WLOGFE("[NAPI]Get window failed");
                return;
            }
            task.Resolve(engine, CreateJsWindowObject(engine, window));
            WLOGFI("[NAPI]Create sub widdow %{public}s end", windowName.c_str());
        };
    NativeValue* callback = (info.argv[1]->TypeOf() == NATIVE_FUNCTION) ? info.argv[1] : nullptr;
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowStage::OnCreateSubWindow",
        engine, CreateAsyncTaskWithLastParam(engine, callback, nullptr, std::move(complete), &result));
    return result;
}

static NativeValue* CreateJsSubWindowArrayObject(NativeEngine& engine,
    std::vector<sptr<Window>> subWinVec)
{
    NativeValue* objValue = engine.CreateArray(subWinVec.size());
    NativeArray* array = ConvertNativeValueTo<NativeArray>(objValue);
    if (array == nullptr) {
        WLOGFE("[NAPI]Failed to convert subWinVec to jsArrayObject");
        return nullptr;
    }
    uint32_t index = 0;
    for (size_t i = 0; i < subWinVec.size(); i++) {
        array->SetElement(index++, CreateJsWindowObject(engine, subWinVec[i]));
    }
    return objValue;
}

NativeValue* JsWindowStage::OnGetSubWindow(NativeEngine& engine, NativeCallbackInfo& info)
{
    AsyncTask::CompleteCallback complete =
        [weak = windowScene_](NativeEngine& engine, AsyncTask& task, int32_t status) {
            auto weakScene = weak.lock();
            if (weakScene == nullptr) {
                task.Reject(engine, CreateJsError(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR)));
                WLOGFE("[NAPI]Window scene is nullptr");
                return;
            }
            std::vector<sptr<Window>> subWindowVec = weakScene->GetSubWindow();
            task.Resolve(engine, CreateJsSubWindowArrayObject(engine, subWindowVec));
            WLOGFI("[NAPI]Get sub windows, size = %{public}zu", subWindowVec.size());
        };
    NativeValue* callback = (info.argv[0]->TypeOf() == NATIVE_FUNCTION) ? info.argv[0] : nullptr;
    NativeValue* result = nullptr;
    AsyncTask::Schedule("JsWindowStage::OnGetSubWindow",
        engine, CreateAsyncTaskWithLastParam(engine, callback, nullptr, std::move(complete), &result));
    return result;
}

NativeValue* JsWindowStage::OnSetShowOnLockScreen(NativeEngine& engine, NativeCallbackInfo& info)
{
    if (info.argc != 1) {
        WLOGFE("[NAPI]Argc is invalid: %{public}zu", info.argc);
        return CreateJsValue(engine, static_cast<int32_t>(WMError::WM_ERROR_INVALID_PARAM));
    }
    auto weakScene = windowScene_.lock();
    if (weakScene == nullptr || weakScene->GetMainWindow() == nullptr) {
        WLOGFE("[NAPI]WindowScene is null or window is null");
        return CreateJsValue(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR));
    }

    bool showOnLockScreen = false;
    NativeBoolean* nativeVal = ConvertNativeValueTo<NativeBoolean>(info.argv[0]);
    if (nativeVal == nullptr) {
        WLOGFE("[NAPI]Failed to convert parameter to boolean");
        return CreateJsValue(engine, static_cast<int32_t>(WMError::WM_ERROR_INVALID_PARAM));
    } else {
        showOnLockScreen = static_cast<bool>(*nativeVal);
    }

    auto window = weakScene->GetMainWindow();
    WMError ret;
    if (showOnLockScreen) {
        ret = window->AddWindowFlag(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED);
    } else {
        ret = window->RemoveWindowFlag(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED);
    }
    WLOGFI("[NAPI]Window [%{public}u, %{public}s] SetShowOnLockScreen %{public}u, ret = %{public}u",
        window->GetWindowId(), window->GetWindowName().c_str(), showOnLockScreen, ret);

    return CreateJsValue(engine, static_cast<int32_t>(ret));
}

NativeValue* JsWindowStage::OnDisableWindowDecor(NativeEngine& engine, NativeCallbackInfo& info)
{
    auto weakScene = windowScene_.lock();
    if (weakScene == nullptr || weakScene->GetMainWindow() == nullptr) {
        WLOGFE("[NAPI]WindowScene is null or window is null");
        return CreateJsValue(engine, static_cast<int32_t>(WMError::WM_ERROR_NULLPTR));
    }

    auto window = weakScene->GetMainWindow();
    window->DisableAppWindowDecor();
    WLOGFI("[NAPI]Window [%{public}u, %{public}s] disable app window decor end",
        window->GetWindowId(), window->GetWindowName().c_str());
    return engine.CreateUndefined();
}

NativeValue* CreateJsWindowStage(NativeEngine& engine,
    std::shared_ptr<Rosen::WindowScene> windowScene)
{
    WLOGFI("[NAPI]CreateJsWindowStage");
    NativeValue* objValue = engine.CreateObject();
    NativeObject* object = ConvertNativeValueTo<NativeObject>(objValue);

    std::unique_ptr<JsWindowStage> jsWindowStage = std::make_unique<JsWindowStage>(windowScene);
    object->SetNativePointer(jsWindowStage.release(), JsWindowStage::Finalizer, nullptr);

    BindNativeFunction(engine,
        *object, "setUIContent", JsWindowStage::SetUIContent);
    BindNativeFunction(engine,
        *object, "loadContent", JsWindowStage::LoadContent);
    BindNativeFunction(engine,
        *object, "getMainWindow", JsWindowStage::GetMainWindow);
    BindNativeFunction(engine,
        *object, "getWindowMode", JsWindowStage::GetWindowMode);
    BindNativeFunction(engine,
        *object, "createSubWindow", JsWindowStage::CreateSubWindow);
    BindNativeFunction(engine,
        *object, "getSubWindow", JsWindowStage::GetSubWindow);
    BindNativeFunction(engine, *object, "on", JsWindowStage::On);
    BindNativeFunction(engine, *object, "off", JsWindowStage::Off);
    BindNativeFunction(engine,
        *object, "setShowOnLockScreen", JsWindowStage::SetShowOnLockScreen);
    BindNativeFunction(engine,
        *object, "disableWindowDecor", JsWindowStage::DisableWindowDecor);

    return objValue;
}
}  // namespace Rosen
}  // namespace OHOS
