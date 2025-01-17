/*
 * Copyright (c) 2022-2022 Huawei Device Co., Ltd.
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

#ifndef WINODW_EXTENSION_CLIENT_PROXY_H
#define WINODW_EXTENSION_CLIENT_PROXY_H

#include <iremote_proxy.h>
#include "window_extension_client_interface.h"

namespace OHOS {
namespace Rosen {
class WindowExtensionClientProxy : public IRemoteProxy<IWindowExtensionClient> {
public:
    WindowExtensionClientProxy(const sptr<IRemoteObject>& impl)
        : IRemoteProxy<IWindowExtensionClient>(impl) {};
    ~WindowExtensionClientProxy() {};
    void OnWindowReady(const std::shared_ptr<RSSurfaceNode>& surfaceNode) override;
    void OnBackPress() override;
    void OnKeyEvent(const std::shared_ptr<MMI::KeyEvent>& keyEvent) override;
    void OnPointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent) override;
private:
    static inline BrokerDelegator<WindowExtensionClientProxy> delegator_;
};
} // namespace Rosen
} // namespace OHOS
#endif // WINODW_EXTENSION_CLIENT_PROXY_H