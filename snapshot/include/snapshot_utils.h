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

#ifndef SNAPSHOT_UTILS_H
#define SNAPSHOT_UTILS_H

#include <cstdint>
#include <pixel_map.h>
#include <string>

#include "display_manager.h"
#include "dm_common.h"

namespace OHOS {
constexpr int BPP = 4;

struct WriteToPngParam {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bitDepth;
    const uint8_t *data;
};

struct CmdArgments {
    bool isDisplayIdSet = false;
    Rosen::DisplayId displayId = Rosen::DISPLAY_ID_INVALID;
    std::string fileName;
    bool isWidthSet = false;
    int32_t width = -1;
    bool isHeightSet = false;
    int32_t height = -1;
};

class SnapShotUtils {
public:
    SnapShotUtils() = default;
    ~SnapShotUtils() = default;

    static void PrintUsage(const std::string &cmdLine);
    static bool CheckFileNameValid(const std::string &fileName);
    static std::string GenerateFileName(int offset = 0);
    static bool CheckWidthAndHeightValid(int32_t w, int32_t h);
    static bool WriteToPng(const std::string &fileName, const WriteToPngParam &param);
    static bool WriteToPng(int fd, const WriteToPngParam &param);
    static bool WriteToPngWithPixelMap(const std::string &fileName, Media::PixelMap &pixelMap);
    static bool WriteToPngWithPixelMap(int fd, Media::PixelMap &pixelMap);
    static bool ProcessArgs(int argc, char * const argv[], CmdArgments& cmdArgments);
    static bool CheckWHValid(int32_t param);
    static bool CheckParamValid(const WriteToPngParam &param);
private:
    static bool ProcessDisplayId(Rosen::DisplayId &displayId, bool isDisplayIdSet);
};
}

#endif // SNAPSHOT_UTILS_H
