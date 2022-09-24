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

#include "js_display.h"

#include <cinttypes>
#include <map>
#include "display.h"
#include "display_info.h"
#include "window_manager_hilog.h"

namespace OHOS {
namespace Rosen {
using namespace AbilityRuntime;
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_DISPLAY, "JsDisplay"};
    const std::map<DisplayState, DisplayStateMode> NATIVE_TO_JS_DISPLAY_STATE_MAP {
        { DisplayState::UNKNOWN, DisplayStateMode::STATE_UNKNOWN },
        { DisplayState::OFF,     DisplayStateMode::STATE_OFF     },
        { DisplayState::ON,      DisplayStateMode::STATE_ON      },
    };
}

static thread_local std::map<DisplayId, std::shared_ptr<NativeReference>> g_JsDisplayMap;
std::recursive_mutex g_mutex;

JsDisplay::JsDisplay(const sptr<Display>& display) : display_(display)
{
}

JsDisplay::~JsDisplay()
{
    WLOGFI("JsDisplay::~JsDisplay is called");
}

void JsDisplay::Finalizer(NativeEngine* engine, void* data, void* hint)
{
    WLOGFI("JsDisplay::Finalizer is called");
    auto jsDisplay = std::unique_ptr<JsDisplay>(static_cast<JsDisplay*>(data));
    if (jsDisplay == nullptr) {
        WLOGFE("JsDisplay::Finalizer jsDisplay is null");
        return;
    }
    sptr<Display> display = jsDisplay->display_;
    if (display == nullptr) {
        WLOGFE("JsDisplay::Finalizer display is null");
        return;
    }
    DisplayId displayId = display->GetId();
    WLOGFI("JsDisplay::Finalizer displayId : %{public}" PRIu64"", displayId);
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_JsDisplayMap.find(displayId) != g_JsDisplayMap.end()) {
        WLOGFI("JsDisplay::Finalizer Display is destroyed: %{public}" PRIu64"", displayId);
        g_JsDisplayMap.erase(displayId);
    }
}

std::shared_ptr<NativeReference> FindJsDisplayObject(DisplayId displayId)
{
    WLOGFI("[NAPI]Try to find display %{public}" PRIu64" in g_JsDisplayMap", displayId);
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_JsDisplayMap.find(displayId) == g_JsDisplayMap.end()) {
        WLOGFI("[NAPI]Can not find display %{public}" PRIu64" in g_JsDisplayMap", displayId);
        return nullptr;
    }
    return g_JsDisplayMap[displayId];
}

NativeValue* CreateJsDisplayObject(NativeEngine& engine, sptr<Display>& display)
{
    WLOGFI("JsDisplay::CreateJsDisplay is called");
    NativeValue* objValue = nullptr;
    std::shared_ptr<NativeReference> jsDisplayObj = FindJsDisplayObject(display->GetId());
    if (jsDisplayObj != nullptr && jsDisplayObj->Get() != nullptr) {
        WLOGFI("[NAPI]FindJsDisplayObject %{public}" PRIu64"", display->GetId());
        objValue = jsDisplayObj->Get();
    }
    if (objValue == nullptr) {
        objValue = engine.CreateObject();
    }
    NativeObject* object = ConvertNativeValueTo<NativeObject>(objValue);
    if (object == nullptr) {
        WLOGFE("Failed to convert prop to jsObject");
        return engine.CreateUndefined();
    }
    std::unique_ptr<JsDisplay> jsDisplay = std::make_unique<JsDisplay>(display);
    object->SetNativePointer(jsDisplay.release(), JsDisplay::Finalizer, nullptr);
    auto info = display->GetDisplayInfo();
    if (info == nullptr) {
        WLOGFE("Failed to GetDisplayInfo");
        return engine.CreateUndefined();
    }
    object->SetProperty("id", CreateJsValue(engine, static_cast<uint32_t>(info->GetDisplayId())));
    object->SetProperty("width", CreateJsValue(engine, info->GetWidth()));
    object->SetProperty("height", CreateJsValue(engine, info->GetHeight()));
    object->SetProperty("refreshRate", CreateJsValue(engine, info->GetRefreshRate()));
    object->SetProperty("name", CreateJsValue(engine, info->GetName()));
    object->SetProperty("alive", CreateJsValue(engine, true));
    if (NATIVE_TO_JS_DISPLAY_STATE_MAP.count(info->GetDisplayState()) != 0) {
        object->SetProperty("state", CreateJsValue(engine, NATIVE_TO_JS_DISPLAY_STATE_MAP.at(info->GetDisplayState())));
    } else {
        object->SetProperty("state", CreateJsValue(engine, DisplayStateMode::STATE_UNKNOWN));
    }
    object->SetProperty("rotation", CreateJsValue(engine, info->GetRotation()));
    object->SetProperty("densityDPI", CreateJsValue(engine, info->GetVirtualPixelRatio() * DOT_PER_INCH));
    object->SetProperty("densityPixels", CreateJsValue(engine, info->GetVirtualPixelRatio()));
    object->SetProperty("scaledDensity", CreateJsValue(engine, info->GetVirtualPixelRatio()));
    object->SetProperty("xDPI", CreateJsValue(engine, 0.0f));
    object->SetProperty("yDPI", CreateJsValue(engine, 0.0f));
    if (jsDisplayObj == nullptr || jsDisplayObj->Get() == nullptr) {
        std::shared_ptr<NativeReference> jsDisplayRef;
        jsDisplayRef.reset(engine.CreateReference(objValue, 1));
        DisplayId displayId = display->GetId();
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_JsDisplayMap[displayId] = jsDisplayRef;
    }
    return objValue;
}
}  // namespace Rosen
}  // namespace OHOS
