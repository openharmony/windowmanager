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

#include "screen_rotation_controller.h"

#include <chrono>
#include <securec.h>

#include "display_manager_service_inner.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_DISPLAY, "ScreenRotationController"};
    constexpr int64_t ORIENTATION_SENSOR_SAMPLING_RATE = 200000000; // 200ms
    constexpr int64_t ORIENTATION_SENSOR_REPORTING_RATE = 0;
    constexpr long ORIENTATION_SENSOR_CALLBACK_TIME_INTERVAL = 200; // 200ms
    constexpr int VALID_INCLINATION_ANGLE_THRESHOLD_COEFFICIENT = 3;
}

DisplayId ScreenRotationController::defaultDisplayId_ = 0;
bool ScreenRotationController::isGravitySensorSubscribed_ = false;
SensorUser ScreenRotationController::user_;
Rotation ScreenRotationController::currentDisplayRotation_;
bool ScreenRotationController::isScreenRotationLocked_ = true;
long ScreenRotationController::lastCallbackTime_ = 0;
uint32_t ScreenRotationController::defaultDeviceRotationOffset_ = 0;
Orientation ScreenRotationController::lastOrientationType_ = Orientation::UNSPECIFIED;
Rotation ScreenRotationController::lastSensorDecidedRotation_;
Rotation ScreenRotationController::rotationLockedRotation_;
uint32_t ScreenRotationController::defaultDeviceRotation_ = 0;
std::map<SensorRotation, DeviceRotation> ScreenRotationController::sensorToDeviceRotationMap_;
std::map<DeviceRotation, Rotation> ScreenRotationController::deviceToDisplayRotationMap_;
DeviceRotation ScreenRotationController::lastSensorRotationConverted_ = DeviceRotation::INVALID;

void ScreenRotationController::SubscribeGravitySensor()
{
    WLOGFI("dms: Subscribe gravity Sensor");
    if (isGravitySensorSubscribed_) {
        WLOGFE("dms: gravity sensor's already subscribed");
        return;
    }
    Init();
    if (strcpy_s(user_.name, sizeof(user_.name), "ScreenRotationController") != EOK) {
        WLOGFE("dms strcpy_s error");
        return;
    }
    user_.userData = nullptr;
    user_.callback = &HandleGravitySensorEventCallback;
    if (SubscribeSensor(SENSOR_TYPE_ID_GRAVITY, &user_) != 0) {
        WLOGFE("dms: Subscribe gravity sensor failed");
        return;
    }
    SetBatch(SENSOR_TYPE_ID_GRAVITY, &user_, ORIENTATION_SENSOR_SAMPLING_RATE, ORIENTATION_SENSOR_REPORTING_RATE);
    SetMode(SENSOR_TYPE_ID_GRAVITY, &user_, SENSOR_ON_CHANGE);
    if (ActivateSensor(SENSOR_TYPE_ID_GRAVITY, &user_) != 0) {
        WLOGFE("dms: Activate gravity sensor failed");
        return;
    }
    isGravitySensorSubscribed_ = true;
}

void ScreenRotationController::UnsubscribeGravitySensor()
{
    WLOGFI("dms: Unsubscribe gravity Sensor");
    if (!isGravitySensorSubscribed_) {
        WLOGFE("dms: Orientation Sensor is not subscribed");
        return;
    }
    if (DeactivateSensor(SENSOR_TYPE_ID_GRAVITY, &user_) != 0) {
        WLOGFE("dms: Deactivate gravity sensor failed");
        return;
    }
    if (UnsubscribeSensor(SENSOR_TYPE_ID_GRAVITY, &user_) != 0) {
        WLOGFE("dms: Unsubscribe gravity sensor failed");
        return;
    }
    isGravitySensorSubscribed_ = false;
}

void ScreenRotationController::Init()
{
    ProcessRotationMapping();
    currentDisplayRotation_ = GetCurrentDisplayRotation();
    lastSensorDecidedRotation_ = currentDisplayRotation_;
    rotationLockedRotation_ = currentDisplayRotation_;
}

bool ScreenRotationController::IsScreenRotationLocked()
{
    return isScreenRotationLocked_;
}

void ScreenRotationController::SetScreenRotationLocked(bool isLocked)
{
    if (isLocked) {
        rotationLockedRotation_ = GetCurrentDisplayRotation();
    }
    isScreenRotationLocked_ = isLocked;
}

void ScreenRotationController::SetDefaultDeviceRotationOffset(uint32_t defaultDeviceRotationOffset)
{
    // Available options for defaultDeviceRotationOffset: {0, 90, 180, 270}
    if (defaultDeviceRotationOffset < 0 || defaultDeviceRotationOffset > 270 || defaultDeviceRotationOffset % 90 != 0) {
        return;
    }
    defaultDeviceRotationOffset_ = defaultDeviceRotationOffset;
}

void ScreenRotationController::HandleGravitySensorEventCallback(SensorEvent *event)
{
    if (!CheckCallbackTimeInterval()) {
        return;
    }
    if (event->sensorTypeId != SENSOR_TYPE_ID_GRAVITY) {
        WLOGE("dms: Orientation Sensor Callback is not SENSOR_TYPE_ID_GRAVITY");
        return;
    }
    Orientation orientation = GetPreferredOrientation();

    currentDisplayRotation_ = GetCurrentDisplayRotation();
    GravityData* gravityData = reinterpret_cast<GravityData*>(event->data);
    int sensorDegree = CalcRotationDegree(gravityData);
    DeviceRotation sensorRotationConverted = ConvertSensorToDeviceRotation(CalcSensorRotation(sensorDegree));
    lastSensorRotationConverted_ = sensorRotationConverted;
    if (!IsSensorRelatedOrientation(orientation)) {
        return;
    }
    if (sensorRotationConverted == DeviceRotation::INVALID) {
        return;
    }
    if (currentDisplayRotation_ == ConvertDeviceToDisplayRotation(sensorRotationConverted)) {
        return;
    }
    Rotation targetDisplayRotation = CalcTargetDisplayRotation(orientation, sensorRotationConverted);
    SetScreenRotation(targetDisplayRotation);
}

int ScreenRotationController::CalcRotationDegree(GravityData* gravityData)
{
    float x = gravityData->x;
    float y = gravityData->y;
    float z = gravityData->z;
    int degree = -1;
    if ((x * x + y * y) * VALID_INCLINATION_ANGLE_THRESHOLD_COEFFICIENT < z * z) {
        return degree;
    }
    // arccotx = pi / 2 - arctanx, 90 is used to calculate acot(in degree); degree = rad / pi * 180
    degree = 90 - static_cast<int>(round(atan2(y, -x) / M_PI * 180));
    // Normalize the degree to the range of 0~360
    return degree >= 0 ? degree % 360 : degree % 360 + 360;
}

Rotation ScreenRotationController::GetCurrentDisplayRotation()
{
    sptr<DisplayInfo> defaultDisplayInfo = DisplayManagerServiceInner::GetInstance().GetDefaultDisplay();
    if (defaultDisplayInfo == nullptr) {
        WLOGFE("Cannot get default display info");
        return defaultDeviceRotation_ == 0 ? ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT) :
            ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE);
    }
    return defaultDisplayInfo->GetRotation();
}

Orientation ScreenRotationController::GetPreferredOrientation()
{
    sptr<ScreenInfo> screenInfo = DisplayManagerServiceInner::GetInstance().GetScreenInfoByDisplayId(defaultDisplayId_);
    if (screenInfo == nullptr) {
        WLOGFE("Cannot get default screen info");
        return Orientation::UNSPECIFIED;
    }
    return screenInfo->GetOrientation();
}

Rotation ScreenRotationController::CalcTargetDisplayRotation(
    Orientation requestedOrientation, DeviceRotation sensorRotationConverted)
{
    switch (requestedOrientation) {
        case Orientation::SENSOR: {
            lastSensorDecidedRotation_ = ConvertDeviceToDisplayRotation(sensorRotationConverted);
            return ConvertDeviceToDisplayRotation(sensorRotationConverted);
        }
        case Orientation::SENSOR_VERTICAL: {
            return ProcessAutoRotationPortraitOrientation(sensorRotationConverted);
        }
        case Orientation::SENSOR_HORIZONTAL: {
            return ProcessAutoRotationLandscapeOrientation(sensorRotationConverted);
        }
        case Orientation::UNSPECIFIED:
        case Orientation::AUTO_ROTATION_RESTRICTED: {
            if (isScreenRotationLocked_) {
                return currentDisplayRotation_;
            }
            lastSensorDecidedRotation_ = ConvertDeviceToDisplayRotation(sensorRotationConverted);
            return ConvertDeviceToDisplayRotation(sensorRotationConverted);
        }
        case Orientation::AUTO_ROTATION_PORTRAIT_RESTRICTED: {
            if (isScreenRotationLocked_) {
                return currentDisplayRotation_;
            }
            return ProcessAutoRotationPortraitOrientation(sensorRotationConverted);
        }
        case Orientation::AUTO_ROTATION_LANDSCAPE_RESTRICTED: {
            if (isScreenRotationLocked_) {
                return currentDisplayRotation_;
            }
            return ProcessAutoRotationLandscapeOrientation(sensorRotationConverted);
        }
        default: {
            return currentDisplayRotation_;
        }
    }
}

Rotation ScreenRotationController::ProcessAutoRotationPortraitOrientation(DeviceRotation sensorRotationConverted)
{
    if (IsDeviceRotationHorizontal(sensorRotationConverted)) {
        return currentDisplayRotation_;
    }
    lastSensorDecidedRotation_ = ConvertDeviceToDisplayRotation(sensorRotationConverted);
    return ConvertDeviceToDisplayRotation(sensorRotationConverted);
}

Rotation ScreenRotationController::ProcessAutoRotationLandscapeOrientation(DeviceRotation sensorRotationConverted)
{
    if (IsDeviceRotationVertical(sensorRotationConverted)) {
        return currentDisplayRotation_;
    }
    lastSensorDecidedRotation_ = ConvertDeviceToDisplayRotation(sensorRotationConverted);
    return ConvertDeviceToDisplayRotation(sensorRotationConverted);
}

void ScreenRotationController::SetScreenRotation(Rotation targetRotation)
{
    if (targetRotation == GetCurrentDisplayRotation()) {
        return;
    }
    DisplayManagerServiceInner::GetInstance().GetDefaultDisplay()->SetRotation(targetRotation);
    DisplayManagerServiceInner::GetInstance().SetRotationFromWindow(defaultDisplayId_, targetRotation);
    WLOGFI("dms: Set screen rotation: %{public}u", targetRotation);
}

bool ScreenRotationController::CheckCallbackTimeInterval()
{
    std::chrono::milliseconds ms = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now()).time_since_epoch();
    long currentTimeInMillitm = ms.count();
    if (currentTimeInMillitm - lastCallbackTime_ < ORIENTATION_SENSOR_CALLBACK_TIME_INTERVAL) {
        return false;
    }
    lastCallbackTime_ = currentTimeInMillitm;
    return true;
}

DeviceRotation ScreenRotationController::CalcDeviceRotation(SensorRotation sensorRotation)
{
    if (sensorRotation == SensorRotation::INVALID) {
        return DeviceRotation::INVALID;
    }
    int32_t bias = defaultDeviceRotationOffset_ / 90; // offset(in degree) divided by 90 to get rotation bias
    int32_t deviceRotationValue = static_cast<int32_t>(sensorRotation) - bias;
    while (deviceRotationValue < 0) {
        // +4 is used to normalize the values into the range 0~3, corresponding to the four rotations.
        deviceRotationValue += 4;
    }
    if (defaultDeviceRotation_ == 1) {
        deviceRotationValue += defaultDeviceRotation_;
        // if device's default rotation is landscape, swap 0 and 90, 180 and 270, use %2 to adjust range.
        (deviceRotationValue % 2 == 0) && (deviceRotationValue -= 2);
    }
    return static_cast<DeviceRotation>(deviceRotationValue);
}

bool ScreenRotationController::IsSensorRelatedOrientation(Orientation orientation)
{
    if ((orientation >= Orientation::UNSPECIFIED && orientation <= Orientation::REVERSE_HORIZONTAL) ||
        orientation == Orientation::LOCKED) {
        return false;
    }
    return true;
}

void ScreenRotationController::ProcessSwitchToSensorRelatedOrientation(
    Orientation orientation, DeviceRotation sensorRotationConverted)
{
    if (lastOrientationType_ == orientation) {
        return;
    }
    lastOrientationType_ = orientation;
    switch (orientation) {
        case Orientation::AUTO_ROTATION_RESTRICTED: {
            if (isScreenRotationLocked_) {
                SetScreenRotation(rotationLockedRotation_);
                return;
            }
            [[fallthrough]];
        }
        case Orientation::SENSOR: {
            ProcessSwitchToAutoRotation(sensorRotationConverted);
            return;
        }
        case Orientation::AUTO_ROTATION_PORTRAIT_RESTRICTED: {
            if (isScreenRotationLocked_) {
                ProcessSwitchToAutoRotationPortraitRestricted();
                return;
            }
            [[fallthrough]];
        }
        case Orientation::SENSOR_VERTICAL: {
            ProcessSwitchToAutoRotationPortrait(sensorRotationConverted);
            return;
        }
        case Orientation::AUTO_ROTATION_LANDSCAPE_RESTRICTED: {
            if (isScreenRotationLocked_) {
                ProcessSwitchToAutoRotationLandscapeRestricted();
                return;
            }
            [[fallthrough]];
        }
        case Orientation::SENSOR_HORIZONTAL: {
            ProcessSwitchToAutoRotationLandscape(sensorRotationConverted);
            return;
        }
        default: {
            return;
        }
    }
}

void ScreenRotationController::ProcessSwitchToAutoRotation(DeviceRotation rotation)
{
    if (rotation != DeviceRotation::INVALID) {
        return;
    }
    SetScreenRotation(lastSensorDecidedRotation_);
}

void ScreenRotationController::ProcessSwitchToAutoRotationPortrait(DeviceRotation rotation)
{
    if (IsCurrentDisplayVertical()) {
        return;
    }
    if (IsDeviceRotationVertical(rotation)) {
        return;
    }
    SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT));
}

void ScreenRotationController::ProcessSwitchToAutoRotationLandscape(DeviceRotation rotation)
{
    if (IsCurrentDisplayHorizontal()) {
        return;
    }
    if (IsDeviceRotationHorizontal(rotation)) {
        return;
    }
    SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE));
}

void ScreenRotationController::ProcessSwitchToAutoRotationPortraitRestricted()
{
    if (IsCurrentDisplayVertical()) {
        return;
    }
    if (IsDisplayRotationVertical(rotationLockedRotation_)) {
        SetScreenRotation(rotationLockedRotation_);
        return;
    }
    SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT));
}

void ScreenRotationController::ProcessSwitchToAutoRotationLandscapeRestricted()
{
    if (IsCurrentDisplayHorizontal()) {
        return;
    }
    if (IsDisplayRotationHorizontal(rotationLockedRotation_)) {
        SetScreenRotation(rotationLockedRotation_);
        return;
    }
    SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE));
}

SensorRotation ScreenRotationController::CalcSensorRotation(int sensorDegree)
{
    // Use ROTATION_0 when degree range is [0, 30]∪[330, 359]
    if (sensorDegree >= 0 && (sensorDegree <= 30 || sensorDegree >= 330)) {
        return SensorRotation::ROTATION_0;
    } else if (sensorDegree >= 60 && sensorDegree <= 120) { // Use ROTATION_90 when degree range is [60, 120]
        return SensorRotation::ROTATION_90;
    } else if (sensorDegree >= 150 && sensorDegree <= 210) { // Use ROTATION_180 when degree range is [150, 210]
        return SensorRotation::ROTATION_180;
    } else if (sensorDegree >= 240 && sensorDegree <= 300) { // Use ROTATION_270 when degree range is [240, 300]
        return SensorRotation::ROTATION_270;
    } else {
        return SensorRotation::INVALID;
    }
}

DeviceRotation ScreenRotationController::ConvertSensorToDeviceRotation(SensorRotation sensorRotation)
{
    return sensorToDeviceRotationMap_.at(sensorRotation);
}

Rotation ScreenRotationController::ConvertDeviceToDisplayRotation(DeviceRotation deviceRotation)
{
    if (deviceRotation == DeviceRotation::INVALID) {
        return GetCurrentDisplayRotation();
    }
    return deviceToDisplayRotationMap_.at(deviceRotation);
}

void ScreenRotationController::ProcessRotationMapping()
{
    sptr<SupportedScreenModes> modes =
        DisplayManagerServiceInner::GetInstance().GetScreenModesByDisplayId(defaultDisplayId_);
    // 0 means PORTRAIT, 1 means LANDSCAPE.
    defaultDeviceRotation_ = modes->width_ < modes->height_ ? 0 : 1;
    if (deviceToDisplayRotationMap_.empty()) {
        deviceToDisplayRotationMap_ = {
            {DeviceRotation::ROTATION_PORTRAIT,
                defaultDeviceRotation_ == 0 ? Rotation::ROTATION_0 : Rotation::ROTATION_90},
            {DeviceRotation::ROTATION_LANDSCAPE,
                defaultDeviceRotation_ == 1 ? Rotation::ROTATION_0 : Rotation::ROTATION_90},
            {DeviceRotation::ROTATION_PORTRAIT_INVERTED,
                defaultDeviceRotation_ == 0 ? Rotation::ROTATION_180 : Rotation::ROTATION_270},
            {DeviceRotation::ROTATION_LANDSCAPE_INVERTED,
                defaultDeviceRotation_ == 1 ? Rotation::ROTATION_180 : Rotation::ROTATION_270},
        };
    }
    if (sensorToDeviceRotationMap_.empty()) {
        sensorToDeviceRotationMap_ = {
            {SensorRotation::ROTATION_0, CalcDeviceRotation(SensorRotation::ROTATION_0)},
            {SensorRotation::ROTATION_90, CalcDeviceRotation(SensorRotation::ROTATION_90)},
            {SensorRotation::ROTATION_180, CalcDeviceRotation(SensorRotation::ROTATION_180)},
            {SensorRotation::ROTATION_270, CalcDeviceRotation(SensorRotation::ROTATION_270)},
            {SensorRotation::INVALID, DeviceRotation::INVALID},
        };
    }
}

bool ScreenRotationController::IsDeviceRotationVertical(DeviceRotation deviceRotation)
{
    return (deviceRotation == DeviceRotation::ROTATION_PORTRAIT) ||
        (deviceRotation == DeviceRotation::ROTATION_PORTRAIT_INVERTED);
}

bool ScreenRotationController::IsDeviceRotationHorizontal(DeviceRotation deviceRotation)
{
    return (deviceRotation == DeviceRotation::ROTATION_LANDSCAPE) ||
        (deviceRotation == DeviceRotation::ROTATION_LANDSCAPE_INVERTED);
}

bool ScreenRotationController::IsCurrentDisplayVertical()
{
    return IsDisplayRotationVertical(GetCurrentDisplayRotation());
}

bool ScreenRotationController::IsCurrentDisplayHorizontal()
{
    return IsDisplayRotationHorizontal(GetCurrentDisplayRotation());
}

bool ScreenRotationController::IsDisplayRotationVertical(Rotation rotation)
{
    return (rotation == ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT)) ||
        (rotation == ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT_INVERTED));
}

bool ScreenRotationController::IsDisplayRotationHorizontal(Rotation rotation)
{
    return (rotation == ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE)) ||
        (rotation == ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE_INVERTED));
}

bool ScreenRotationController::IsGravitySensorEnabled()
{
    return isGravitySensorSubscribed_;
}

void ScreenRotationController::ProcessSwitchToSensorUnrelatedOrientation(Orientation orientation)
{
    if (lastOrientationType_ == orientation) {
        return;
    }
    lastOrientationType_ = orientation;
    switch (orientation) {
        case Orientation::UNSPECIFIED: {
            SetScreenRotation(Rotation::ROTATION_0);
            break;
        }
        case Orientation::VERTICAL: {
            SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT));
            break;
        }
        case Orientation::REVERSE_VERTICAL: {
            SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_PORTRAIT_INVERTED));
            break;
        }
        case Orientation::HORIZONTAL: {
            SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE));
            break;
        }
        case Orientation::REVERSE_HORIZONTAL: {
            SetScreenRotation(ConvertDeviceToDisplayRotation(DeviceRotation::ROTATION_LANDSCAPE_INVERTED));
            break;
        }
        default: {
            return;
        }
    }
}

void ScreenRotationController::ProcessOrientationSwitch(Orientation orientation)
{
    if (!IsSensorRelatedOrientation(orientation)) {
        ProcessSwitchToSensorUnrelatedOrientation(orientation);
    } else {
        ProcessSwitchToSensorRelatedOrientation(orientation, lastSensorRotationConverted_);
    }
}
} // Rosen
} // OHOS