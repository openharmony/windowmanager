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

#ifndef OHOS_INPUT_WINDOW_MONITOR_H
#define OHOS_INPUT_WINDOW_MONITOR_H

#include <unordered_set>
#include <input_manager.h>
#include <refbase.h>

#include "display_info.h"
#include "window_root.h"
#include "wm_common.h"

namespace OHOS {
namespace Rosen {
class InputWindowMonitor : public RefBase {
public:
    explicit InputWindowMonitor(sptr<WindowRoot>& root) : windowRoot_(root) {}
    ~InputWindowMonitor() = default;
    void UpdateInputWindow(uint32_t windowId);
    void UpdateInputWindowByDisplayId(DisplayId displayId);

private:
    sptr<WindowRoot> windowRoot_;
    MMI::DisplayGroupInfo displayGroupInfo_;
    std::unordered_set<WindowType> windowTypeSkipped_ { WindowType::WINDOW_TYPE_POINTER,
        WindowType::WINDOW_TYPE_DRAGGING_EFFECT, WindowType::WINDOW_TYPE_FREEZE_DISPLAY};
    void TraverseWindowNodes(const std::vector<sptr<WindowNode>>& windowNodes,
                             std::vector<MMI::WindowInfo>& windowsInfo);
    void UpdateDisplayGroupInfo(const sptr<WindowNodeContainer>& windowNodeContainer,
                                MMI::DisplayGroupInfo& displayGroupInfo);
    void UpdateDisplayInfo(const sptr<DisplayInfo>& displayInfo,
                           std::vector<MMI::DisplayInfo>& displayInfoVector);
    MMI::Direction GetDisplayDirectionForMmi(Rotation rotation);
};
}
}
#endif // OHOS_INPUT_WINDOW_MONITOR_H
