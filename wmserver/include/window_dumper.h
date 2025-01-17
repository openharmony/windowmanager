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

#ifndef OHOS_ROSEN_WINDOW_DUMPER_H
#define OHOS_ROSEN_WINDOW_DUMPER_H

#include "dm_common.h"
#include "window_root.h"

namespace OHOS {
namespace Rosen {
enum DumpType : uint32_t {
    DUMP_ALL = 0,
    DUMP_WINDOW,
    DUMP_NONE = 100,
};
class WindowDumper : public RefBase {
public:
    WindowDumper(const sptr<WindowRoot>& root) : windowRoot_(root) {}
    WMError Dump(int fd, const std::vector<std::u16string>& args) const;

private:
    WMError DumpWindowInfo(const std::vector<std::string>& args, std::string& dumpInfo) const;
    WMError DumpAllWindowInfo(std::string& dumpInfo) const;
    WMError DumpScreenGroupWindowInfo(ScreenId screenGroupId, const sptr<WindowNodeContainer>& windowNodeContainer,
        std::string& dumpInfo) const;
    bool IsValidDigitString(const std::string& windowIdStr) const;
    WMError DumpSpecifiedWindowInfo(uint32_t windowId, const std::vector<std::string>& params,
        std::string& dumpInfo) const;
    void ShowHelpInfo(std::string& dumpInfo) const;
    void ShowAceDumpHelp(std::string& dumpInfo) const;
    void ShowIllegalArgsInfo(std::string& dumpInfo, WMError errCode) const;
    const sptr<WindowRoot> windowRoot_;
};
}
}
#endif // OHOS_ROSEN_WINDOW_DUMPER_H