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

#ifndef WINDOW_EXTENSION_CLIENT_STUB_H
#define WINDOW_EXTENSION_CLIENT_STUB_H

#include <iremote_stub.h>
#include "window_extension_client_interface.h"

namespace OHOS {
namespace Rosen {
class WindowExtensionClientStub : public IRemoteStub<IWindowExtensionClient> {
public:
    WindowExtensionClientStub() = default;
    ~WindowExtensionClientStub() = default;

    virtual int OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply,
        MessageOption& option) override;
};
} // namespace Rosen
} // namespace OHOS
#endif // WINDOW_EXTENSION_CLIENT_STUB_H