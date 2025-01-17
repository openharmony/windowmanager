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

#ifndef OHOS_ROSEN_STARTING_WINDOW_H
#define OHOS_ROSEN_STARTING_WINDOW_H

#include <refbase.h>
#include "pixel_map.h"
#include "animation_config.h"
#include "surface_draw.h"
#include "wm_common.h"
#include "window_node.h"
#include "window_transition_info.h"

namespace OHOS {
namespace Rosen {
class StartingWindow : public RefBase {
public:
    StartingWindow() = delete;
    ~StartingWindow() = default;

    static sptr<WindowNode> CreateWindowNode(const sptr<WindowTransitionInfo>& info, uint32_t winId);
    static void HandleClientWindowCreate(sptr<WindowNode>& node, sptr<IWindow>& window,
        uint32_t& windowId, const std::shared_ptr<RSSurfaceNode>& surfaceNode, sptr<WindowProperty>& property,
        int32_t pid, int32_t uid);
    static void DrawStartingWindow(sptr<WindowNode>& node, sptr<Media::PixelMap> pixelMap, uint32_t bkgColor,
        bool isColdStart);
    static void UpdateRSTree(sptr<WindowNode>& node, const AnimationConfig& animationConfig);
    static void ReleaseStartWinSurfaceNode(sptr<WindowNode>& node);
    static bool NeedToStopStartingWindow(WindowMode winMode, uint32_t modeSupportInfo,
        const sptr<WindowTransitionInfo>& info);

private:
    static WMError CreateLeashAndStartingSurfaceNode(sptr<WindowNode>& node);
    static SurfaceDraw surfaceDraw_;
    static std::recursive_mutex mutex_;
};
} // Rosen
} // OHOS
#endif // OHOS_ROSEN_STARTING_WINDOW_H
