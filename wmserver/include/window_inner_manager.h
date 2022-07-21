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

#ifndef OHOS_ROSEN_WINDOW_INNER_MANAGER_H
#define OHOS_ROSEN_WINDOW_INNER_MANAGER_H

#include <refbase.h>
#include "event_handler.h"
#include "event_runner.h"

#include "inner_window.h"
#include "wm_common.h"
#include "wm_single_instance.h"

enum class InnerWMRunningState {
    STATE_NOT_START,
    STATE_RUNNING,
};
namespace OHOS {
namespace Rosen {
class WindowInnerManager : public RefBase {
WM_DECLARE_SINGLE_INSTANCE_BASE(WindowInnerManager);
using EventRunner = OHOS::AppExecFwk::EventRunner;
using EventHandler = OHOS::AppExecFwk::EventHandler;
public:
    void Start(bool enableRecentholder);
    void Stop();
    void CreateInnerWindow(std::string name, DisplayId displayId, Rect rect, WindowType type, WindowMode mode);
    void DestroyInnerWindow(DisplayId displayId, WindowType type);
    void UpdateInnerWindow(DisplayId displayId, WindowType type, uint32_t width, uint32_t height);

protected:
    WindowInnerManager();
    ~WindowInnerManager();

private:
    bool Init();

private:
    bool isRecentHolderEnable_ = false;
    std::shared_ptr<EventHandler> eventHandler_;
    std::shared_ptr<EventRunner> eventLoop_;
    InnerWMRunningState state_;
    const std::string INNER_WM_THREAD_NAME = "inner_window_manager";
};
} // namespace Rosen
} // namespace OHOS
#endif // OHOS_ROSEN_WINDOW_INNER_MANAGER_H