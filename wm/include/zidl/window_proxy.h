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

#ifndef OHOS_WINDOW_PROXY_H
#define OHOS_WINDOW_PROXY_H

#include "window_interface.h"
#include "iremote_proxy.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
class WindowProxy : public IRemoteProxy<IWindow> {
public:
    explicit WindowProxy(const sptr<IRemoteObject>& impl) : IRemoteProxy<IWindow>(impl) {};

    ~WindowProxy() {};

    void UpdateWindowRect(const struct Rect& rect, bool decoStatus, WindowSizeChangeReason reason) override;
    void UpdateWindowMode(WindowMode mode) override;
    void UpdateFocusStatus(bool focused) override;
    void UpdateAvoidArea(const std::vector<Rect>& avoidAreas) override;
    void UpdateWindowState(WindowState state) override;
    void UpdateWindowDragInfo(const PointInfo& point, DragEvent event) override;
    void UpdateDisplayId(DisplayId from, DisplayId to) override;
    void UpdateOccupiedAreaChangeInfo(const sptr<OccupiedAreaChangeInfo>& info) override;
    void UpdateActiveStatus(bool isActive) override;
    sptr<WindowProperty> GetWindowProperty() override;
    void NotifyTouchOutside() override;
    void DumpInfo(const std::vector<std::string>& params, std::vector<std::string>& info) override;
private:
    static inline BrokerDelegator<WindowProxy> delegator_;
};
} // namespace Rosen
} // namespace OHOS
#endif // OHOS_WINDOW_PROXY_H
