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

#ifndef OHOS_ROSEN_WINDOW_OPTION_H
#define OHOS_ROSEN_WINDOW_OPTION_H
#include <refbase.h>
#include <string>
#include <unordered_map>

#include "../dm/dm_common.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
class WindowOption : public RefBase {
public:
    WindowOption();
    virtual ~WindowOption() = default;

    void SetWindowRect(const struct Rect& rect);
    void SetWindowType(WindowType type);
    void SetWindowMode(WindowMode mode);
    void SetWindowBackgroundBlur(WindowBlurLevel level);
    void SetFocusable(bool isFocusable);
    void SetTouchable(bool isTouchable);
    void SetDisplayId(DisplayId displayId);
    void SetParentName(const std::string& parentName);
    void SetWindowName(const std::string& windowName);
    void AddWindowFlag(WindowFlag flag);
    void RemoveWindowFlag(WindowFlag flag);
    void SetWindowFlags(uint32_t flags);
    void SetSystemBarProperty(WindowType type, const SystemBarProperty& property);
    void SetHitOffset(int32_t x, int32_t y);
    void SetWindowTag(WindowTag windowTag);
    void SetKeepScreenOn(bool keepScreenOn);
    bool IsKeepScreenOn() const;
    void SetTurnScreenOn(bool turnScreenOn);
    bool IsTurnScreenOn() const;
    void SetBrightness(float brightness);
    float GetBrightness() const;
    void SetCallingWindow(uint32_t windowId);
    uint32_t GetCallingWindow() const;

    Rect GetWindowRect() const;
    WindowType GetWindowType() const;
    WindowMode GetWindowMode() const;
    WindowBlurLevel GetWindowBackgroundBlur() const;
    bool GetFocusable() const;
    bool GetTouchable() const;
    DisplayId GetDisplayId() const;
    const std::string& GetParentName() const;
    const std::string& GetWindowName() const;
    uint32_t GetWindowFlags() const;
    const std::unordered_map<WindowType, SystemBarProperty>& GetSystemBarProperty() const;
    const PointInfo& GetHitOffset() const;
    WindowTag GetWindowTag() const;
    Orientation GetRequestedOrientation() const;
    void SetRequestedOrientation(Orientation orientation);
private:
    Rect windowRect_ { 0, 0, 0, 0 };
    WindowType type_ { WindowType::WINDOW_TYPE_APP_MAIN_WINDOW };
    WindowMode mode_ { WindowMode::WINDOW_MODE_UNDEFINED };
    WindowBlurLevel level_ { WindowBlurLevel::WINDOW_BLUR_OFF };
    bool focusable_ { true };
    bool touchable_ { true };
    DisplayId displayId_ { 0 };
    std::string parentName_ { "" };
    std::string windowName_ { "" };
    uint32_t flags_ { 0 };
    PointInfo hitOffset_ { 0, 0 };
    WindowTag windowTag_;
    bool keepScreenOn_ = false;
    bool turnScreenOn_ = false;
    float brightness_ = UNDEFINED_BRIGHTNESS;
    uint32_t callingWindow_ = INVALID_WINDOW_ID;
    std::unordered_map<WindowType, SystemBarProperty> sysBarPropMap_ {
        { WindowType::WINDOW_TYPE_STATUS_BAR,     SystemBarProperty() },
        { WindowType::WINDOW_TYPE_NAVIGATION_BAR, SystemBarProperty() },
    };
    Orientation requestedOrientation_ { Orientation::UNSPECIFIED };
};
} // namespace Rosen
} // namespace OHOS
#endif // OHOS_ROSEN_WINDOW_OPTION_H
