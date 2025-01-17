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

#ifndef OHOS_ROSEN_ACCESSIBILITY_CONNECTION_H
#define OHOS_ROSEN_ACCESSIBILITY_CONNECTION_H

#include <refbase.h>
#include <map>
#include <vector>

#include "window_node.h"
#include "window_root.h"
#include "window_node_container.h"
#include "wm_common.h"

namespace OHOS::Rosen {
class AccessibilityConnection : public RefBase {
public:
    explicit AccessibilityConnection(sptr<WindowRoot>& root) : windowRoot_(root) {}
    ~AccessibilityConnection() = default;
    void NotifyAccessibilityInfo(const sptr<WindowNode>& node, WindowUpdateType type);
    void GetAccessibilityWindowInfo(sptr<AccessibilityWindowInfo>& windowInfo) const;

private:
    sptr<WindowRoot> windowRoot_;
    std::map<sptr<WindowNodeContainer>, uint32_t> focusedWindowMap_;
    void NotifyAccessibilityWindowInfo(const sptr<WindowNodeContainer>& container, const sptr<WindowNode>& node,
        WindowUpdateType type) const;
    void GetWindowList(const sptr<WindowNodeContainer>& container, std::vector<sptr<WindowInfo>>& windowList) const;
    void FillWindowInfo(const sptr<WindowNode>& node, uint32_t focusedWindow, sptr<WindowInfo>& windowInfo) const;
};
}
#endif // OHOS_ROSEN_ACCESSIBILITY_CONTROLLER_H
