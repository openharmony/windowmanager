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

#ifndef OHOS_ROSEN_REMOTE_ANIMATION_H
#define OHOS_ROSEN_REMOTE_ANIMATION_H

#include <refbase.h>
#include <rs_iwindow_animation_controller.h>
#include <rs_window_animation_target.h>

#include "wm_common.h"
#include "window_node.h"
#include "window_root.h"
#include "window_transition_info.h"

namespace OHOS {
namespace Rosen {
enum class TransitionEvent : uint32_t {
    APP_TRANSITION,
    BACK,
    HOME,
    MINIMIZE,
    CLOSE,
    UNKNOWN,
};

class RemoteAnimation : public RefBase {
public:
    RemoteAnimation() = delete;
    ~RemoteAnimation() = default;

    static bool CheckTransition(sptr<WindowTransitionInfo> srcInfo, const sptr<WindowNode>& srcNode,
        sptr<WindowTransitionInfo> dstInfo, const sptr<WindowNode>& dstNode);
    static TransitionEvent GetTransitionEvent(sptr<WindowTransitionInfo> srcInfo,
        sptr<WindowTransitionInfo> dstInfo, const sptr<WindowNode>& srcNode, const sptr<WindowNode>& dstNode);
    static WMError SetWindowAnimationController(const sptr<RSIWindowAnimationController>& controller);
    static WMError NotifyAnimationTransition(sptr<WindowTransitionInfo> srcInfo, sptr<WindowTransitionInfo> dstInfo,
        const sptr<WindowNode>& srcNode, const sptr<WindowNode>& dstNode, sptr<WindowRoot>& windowRoot);
    static WMError NotifyAnimationMinimize(sptr<WindowTransitionInfo> srcInfo, const sptr<WindowNode>& srcNode,
        sptr<WindowRoot>& windowRoot);
    static WMError NotifyAnimationClose(sptr<WindowTransitionInfo> srcInfo, const sptr<WindowNode>& srcNode,
        TransitionEvent event, sptr<WindowRoot>& windowRoot);
    static void OnRemoteDie(const sptr<IRemoteObject>& remoteObject);
    static bool CheckAnimationController();
    static WMError NotifyAnimationByHome(sptr<WindowRoot>& windowRoot);
    static WMError NotifyAnimationScreenUnlock(std::function<void(void)> callback);
private:
    static sptr<RSWindowAnimationTarget> CreateWindowAnimationTarget(sptr<WindowTransitionInfo> info,
        const sptr<WindowNode>& windowNode);
    static sptr<RSIWindowAnimationController> windowAnimationController_;
};
} // Rosen
} // OHOS
#endif // OHOS_ROSEN_REMOTE_ANIMATION_H
