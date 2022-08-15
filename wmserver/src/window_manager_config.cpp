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

#include "window_manager_config.h"
#include "config_policy_utils.h"
#include "window_helper.h"
#include "window_manager_hilog.h"

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "WindowManagerConfig"};
}

std::map<std::string, bool> WindowManagerConfig::enableConfig_;
std::map<std::string, std::vector<int>> WindowManagerConfig::intNumbersConfig_;
std::map<std::string, std::vector<float>> WindowManagerConfig::floatNumbersConfig_;


std::string WindowManagerConfig::GetConfigPath(const std::string& configFileName)
{
    char buf[PATH_MAX + 1];
    char* configPath = GetOneCfgFile(configFileName.c_str(), buf, PATH_MAX + 1);
    char tmpPath[PATH_MAX + 1] = { 0 };
    if (!configPath || strlen(configPath) == 0 || strlen(configPath) > PATH_MAX || !realpath(configPath, tmpPath)) {
        WLOGFI("[WmConfig] can not get customization config file");
        return "/system/" + configFileName;
    }
    return std::string(tmpPath);
}

bool WindowManagerConfig::LoadConfigXml()
{
    auto configFilePath = GetConfigPath("etc/window/resources/window_manager_config.xml");
    xmlDocPtr docPtr = xmlReadFile(configFilePath.c_str(), nullptr, XML_PARSE_NOBLANKS);
    WLOGFI("[WmConfig] filePath: %{public}s", configFilePath.c_str());
    if (docPtr == nullptr) {
        WLOGFE("[WmConfig] load xml error!");
        return false;
    }

    xmlNodePtr rootPtr = xmlDocGetRootElement(docPtr);
    if (rootPtr == nullptr || rootPtr->name == nullptr ||
        xmlStrcmp(rootPtr->name, reinterpret_cast<const xmlChar*>("Configs"))) {
        WLOGFE("[WmConfig] get root element failed!");
        xmlFreeDoc(docPtr);
        return false;
    }

    for (xmlNodePtr curNodePtr = rootPtr->xmlChildrenNode; curNodePtr != nullptr; curNodePtr = curNodePtr->next) {
        if (!IsValidNode(*curNodePtr)) {
            WLOGFE("[WmConfig]: invalid node!");
            continue;
        }

        auto nodeName = curNodePtr->name;
        if (!xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("decor")) ||
            !xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("minimizeByOther")) ||
            !xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("stretchable"))) {
            ReadEnableConfigInfo(curNodePtr);
            continue;
        }
        if (!xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("maxAppWindowNumber")) ||
            !xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("modeChangeHotZones")) ||
            !xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("defaultWindowMode"))) {
            ReadIntNumbersConfigInfo(curNodePtr);
            continue;
        }

        if (!xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("splitRatios")) ||
            !xmlStrcmp(nodeName, reinterpret_cast<const xmlChar*>("exitSplitRatios"))) {
            ReadFloatNumbersConfigInfo(curNodePtr);
            continue;
        }
    }
    xmlFreeDoc(docPtr);
    return true;
}

bool WindowManagerConfig::IsValidNode(const xmlNode& currNode)
{
    if (currNode.name == nullptr || currNode.type == XML_COMMENT_NODE) {
        return false;
    }
    return true;
}

void WindowManagerConfig::ReadEnableConfigInfo(const xmlNodePtr& currNode)
{
    xmlChar* enable = xmlGetProp(currNode, reinterpret_cast<const xmlChar*>("enable"));
    if (enable == nullptr) {
        WLOGFE("[WmConfig] read xml node error: nodeName:(%{public}s)", currNode->name);
        return;
    }

    std::string nodeName = reinterpret_cast<const char *>(currNode->name);
    if (!xmlStrcmp(enable, reinterpret_cast<const xmlChar*>("true"))) {
        enableConfig_[nodeName] = true;
    } else if (!xmlStrcmp(enable, reinterpret_cast<const xmlChar*>("false"))) {
        enableConfig_[nodeName] = false;
    }
    xmlFree(enable);
}

void WindowManagerConfig::ReadIntNumbersConfigInfo(const xmlNodePtr& currNode)
{
    xmlChar* context = xmlNodeGetContent(currNode);
    if (context == nullptr) {
        WLOGFE("[WmConfig] read xml node error: nodeName:(%{public}s)", currNode->name);
        return;
    }

    std::vector<int> numbersVec;
    std::string numbersStr = reinterpret_cast<const char*>(context);
    if (numbersStr.size() == 0) {
        xmlFree(context);
        return;
    }
    auto numbers = WindowHelper::Split(numbersStr, " ");
    for (auto& num : numbers) {
        if (!WindowHelper::IsNumber(num)) {
            WLOGFE("[WmConfig] read int number error: nodeName:(%{public}s)", currNode->name);
            xmlFree(context);
            return;
        }

        numbersVec.emplace_back(std::stoi(num));
    }

    std::string nodeName = reinterpret_cast<const char *>(currNode->name);
    intNumbersConfig_[nodeName] = numbersVec;
    xmlFree(context);
}

void WindowManagerConfig::ReadFloatNumbersConfigInfo(const xmlNodePtr& currNode)
{
    xmlChar* context = xmlNodeGetContent(currNode);
    if (context == nullptr) {
        WLOGFE("[WmConfig] read xml node error: nodeName:(%{public}s)", currNode->name);
        return;
    }

    std::vector<float> numbersVec;
    std::string numbersStr = reinterpret_cast<const char*>(context);
    if (numbersStr.size() == 0) {
        xmlFree(context);
        return;
    }
    auto numbers = WindowHelper::Split(numbersStr, " ");
    for (auto& num : numbers) {
        if (!WindowHelper::IsFloatingNumber(num)) {
            WLOGFE("[WmConfig] read float number error: nodeName:(%{public}s)", currNode->name);
            xmlFree(context);
            return;
        }
        numbersVec.emplace_back(std::stof(num));
    }

    std::string nodeName = reinterpret_cast<const char *>(currNode->name);
    floatNumbersConfig_[nodeName] = numbersVec;
    xmlFree(context);
}

const std::map<std::string, bool>& WindowManagerConfig::GetEnableConfig()
{
    return enableConfig_;
}

const std::map<std::string, std::vector<int>>& WindowManagerConfig::GetIntNumbersConfig()
{
    return intNumbersConfig_;
}

const std::map<std::string, std::vector<float>>& WindowManagerConfig::GetFloatNumbersConfig()
{
    return floatNumbersConfig_;
}

void WindowManagerConfig::DumpConfig()
{
    for (auto& enable : enableConfig_) {
        WLOGFI("[WmConfig] Enable: %{public}s %{public}u", enable.first.c_str(), enable.second);
    }

    for (auto& numbers : intNumbersConfig_) {
        WLOGFI("[WmConfig] Int numbers: %{public}s %{public}zu", numbers.first.c_str(), numbers.second.size());
        for (auto& num : numbers.second) {
            WLOGFI("[WmConfig] Num: %{public}d", num);
        }
    }

    for (auto& numbers : floatNumbersConfig_) {
        WLOGFI("[WmConfig] Float numbers: %{public}s %{public}zu", numbers.first.c_str(), numbers.second.size());
        for (auto& num : numbers.second) {
            WLOGFI("[WmConfig] Num: %{public}f", num);
        }
    }
}
} // namespace Rosen
} // namespace OHOS
