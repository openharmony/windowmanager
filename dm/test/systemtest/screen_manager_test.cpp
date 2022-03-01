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

// gtest
#include <gtest/gtest.h>

#include "display_test_utils.h"
#include "future.h"
#include "screen.h"
#include "window.h"
#include "window_option.h"
#include "window_manager_hilog.h"
using namespace testing;
using namespace testing::ext;

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_DISPLAY, "ScreenManagerTest"};
}
class ScreenManagerTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    virtual void SetUp() override;
    virtual void TearDown() override;
    sptr<Window> CreateWindowByDisplayId(DisplayId displayId);
    static sptr<Display> defaultDisplay_;
    static DisplayId defaultDisplayId_;
    static ScreenId defaultScreenId_;
    static std::string defaultName_;
    static uint32_t defaultWidth_;
    static uint32_t defaultHeight_;
    static float defaultDensity_;
    static int32_t defaultFlags_;
    static VirtualScreenOption defaultOption_;
    static uint32_t waitCount_;
    const uint32_t sleepUs_ = 10 * 1000;
    const uint32_t maxWaitCount_ = 2000;
    const uint32_t execTimes_ = 10;
    const uint32_t acquireFrames_ = 1;
    static constexpr uint32_t TEST_SPEEP_S = 1; // test spleep time
    static constexpr long TIME_OUT = 1000;
};

class ScreenChangeListener : public ScreenManager::IScreenListener {
public:
    virtual void OnConnect(ScreenId screenId) override
    {
        WLOGFI("OnConnect, screenId:%{public}" PRIu64"", screenId);
        connectFuture_.SetValue(screenId);
    }
    virtual void OnDisconnect(ScreenId screenId) override
    {
        WLOGFI("OnDisconnect, screenId:%{public}" PRIu64"", screenId);
        disconnectFuture_.SetValue(screenId);
    }
    virtual void OnChange(ScreenId screenId) override
    {
        WLOGFI("OnChange, screenId:%{public}" PRIu64"", screenId);
        changeFuture_.SetValue(screenId);
    }
    RunnableFuture<ScreenId> connectFuture_;
    RunnableFuture<ScreenId> disconnectFuture_;
    RunnableFuture<ScreenId> changeFuture_;
};

class ScreenGroupChangeListener : public ScreenManager::IScreenGroupListener {
public:
    virtual void OnChange(const std::vector<ScreenId>& screenIds, ScreenGroupChangeEvent event) override
    {
        for (auto screenId : screenIds) {
            changeFuture_.SetValue(std::make_pair(screenId, event));
            usleep(10 * 1000); // wait 10000 us
        }
    }
    RunnableFuture<std::pair<ScreenId, ScreenGroupChangeEvent>> changeFuture_;
};

sptr<Display> ScreenManagerTest::defaultDisplay_ = nullptr;
DisplayId ScreenManagerTest::defaultDisplayId_ = DISPLAY_ID_INVALD;
ScreenId ScreenManagerTest::defaultScreenId_ = INVALID_SCREEN_ID;
std::string ScreenManagerTest::defaultName_ = "virtualScreen01";
uint32_t ScreenManagerTest::defaultWidth_ = 480;
uint32_t ScreenManagerTest::defaultHeight_ = 320;
float ScreenManagerTest::defaultDensity_ = 2.0;
int32_t ScreenManagerTest::defaultFlags_ = 0;
VirtualScreenOption ScreenManagerTest::defaultOption_ = {
    defaultName_, defaultWidth_, defaultHeight_, defaultDensity_, nullptr, defaultFlags_
};
uint32_t ScreenManagerTest::waitCount_ = 0;

void ScreenManagerTest::SetUpTestCase()
{
    defaultDisplay_ = DisplayManager::GetInstance().GetDefaultDisplay();
    defaultDisplayId_ = defaultDisplay_->GetId();
    defaultScreenId_ = defaultDisplay_->GetScreenId();
    defaultWidth_ = defaultDisplay_->GetWidth();
    defaultHeight_ = defaultDisplay_->GetHeight();
    defaultOption_.width_ = defaultWidth_;
    defaultOption_.height_ = defaultHeight_;
}

void ScreenManagerTest::TearDownTestCase()
{
}

void ScreenManagerTest::SetUp()
{
}

void ScreenManagerTest::TearDown()
{
}

sptr<Window> ScreenManagerTest::CreateWindowByDisplayId(DisplayId displayId)
{
    sptr<WindowOption> option = new WindowOption();
    if (option == nullptr) {
        return nullptr;
    }
    Rect displayRect = {0, 0, 200, 400};
    option->SetDisplayId(displayId);
    option->SetWindowRect(displayRect);
    option->SetWindowType(WindowType::WINDOW_TYPE_APP_MAIN_WINDOW);
    option->SetWindowMode(WindowMode::WINDOW_MODE_FULLSCREEN);
    option->SetWindowName("CreateVirtualWindow01");
    option->AddWindowFlag(WindowFlag::WINDOW_FLAG_NEED_AVOID);
    option->RemoveWindowFlag(WindowFlag::WINDOW_FLAG_PARENT_LIMIT);
    sptr<Window> window = Window::Create(option->GetWindowName(), option);
    window->Show();
    return window;
}

namespace {
/**
 * @tc.name: ScreenManager01
 * @tc.desc: Create a virtual screen and destroy it
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager01, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
}

/**
 * @tc.name: ScreenManager02
 * @tc.desc: Create a virtual screen as mirror of default screen, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager02, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    std::vector<ScreenId> mirrorIds;
    mirrorIds.push_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);
    ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
}

/**
 * @tc.name: ScreenManager03
 * @tc.desc: Create a virtual screen and destroy it for 10 times
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager03, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    for (uint32_t i = 0; i < execTimes_; i++) {
        ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
        ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
        ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    }
}

/**
 * @tc.name: ScreenManager04
 * @tc.desc: Create a virtual screen as mirror of default screen, and destroy virtual screen for 10 times
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager04, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    for (uint32_t i = 0; i < execTimes_; i++) {
        ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
        std::vector<ScreenId> mirrorIds;
        mirrorIds.push_back(virtualScreenId);
        ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);
        ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
        ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    }
}

/**
 * @tc.name: ScreenManager05
 * @tc.desc: Compare the length and width for recording screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager05, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    utils.SetDefaultWH(defaultDisplay_);
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);

    ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
    uint32_t lastCount = -1u;
    std::vector<ScreenId> mirrorIds;
    mirrorIds.push_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);

    while (utils.successCount_ < acquireFrames_ && waitCount_ <=  maxWaitCount_) {
        if (lastCount != utils.successCount_) {
            lastCount = utils.successCount_;
        }
        ASSERT_EQ(0, utils.failCount_);
        waitCount_++;
        usleep(sleepUs_);
    }
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    ASSERT_GT(utils.successCount_, 0);
    ASSERT_GT(maxWaitCount_, waitCount_);
    waitCount_ = 0;
}

/**
 * @tc.name: ScreenManager06
 * @tc.desc: Compare the length and width for recording screen, set VirtualScreen Surface before make mirror.
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager06, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    utils.SetDefaultWH(defaultDisplay_);
    defaultOption_.surface_ = nullptr;
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);

    ASSERT_TRUE(utils.CreateSurface());
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().SetVirtualScreenSurface(virtualScreenId, utils.psurface_));

    uint32_t lastCount = -1u;
    std::vector<ScreenId> mirrorIds;
    mirrorIds.push_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);

    while (utils.successCount_ < acquireFrames_ && waitCount_ <=  maxWaitCount_) {
        if (lastCount != utils.successCount_) {
            lastCount = utils.successCount_;
        }
        ASSERT_EQ(0, utils.failCount_);
        waitCount_++;
        usleep(sleepUs_);
    }
    DMError res = ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId);
    ASSERT_EQ(DMError::DM_OK, res);
    ASSERT_GT(utils.successCount_, 0);
    ASSERT_GT(maxWaitCount_, waitCount_);
    waitCount_ = 0;
}

/**
 * @tc.name: ScreenManager07
 * @tc.desc: Get and set screenMode
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager07, Function | MediumTest | Level2)
{
    sptr<Screen> screen = ScreenManager::GetInstance().GetScreenById(defaultScreenId_);
    auto modes = screen->GetSupportedModes();
    auto defaultModeId = screen->GetModeId();
    ASSERT_GT(modes.size(), 0);
    for (uint32_t modeIdx = 0; modeIdx < modes.size(); modeIdx++) {
        ASSERT_EQ(true, screen->SetScreenActiveMode(modeIdx));
        sleep(TEST_SPEEP_S);
        ASSERT_EQ(modeIdx, screen->GetModeId());
    }
    ASSERT_EQ(true, screen->SetScreenActiveMode(defaultModeId));
    sleep(TEST_SPEEP_S);
}

/**
 * @tc.name: ScreenManager08
 * @tc.desc: Create a virtual screen as expansion of default screen, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager08, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    std::vector<sptr<Screen>> screens = ScreenManager::GetInstance().GetAllScreens();
    sptr<Screen> DefaultScreen = screens.front();
    std::vector<ExpandOption> options = {{DefaultScreen->GetId(), 0, 0}, {virtualScreenId, defaultWidth_, 0}};
    ScreenId expansionId = ScreenManager::GetInstance().MakeExpand(options);
    ASSERT_NE(SCREEN_ID_INVALID, expansionId);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::REMOVE_FROM_GROUP, pair.second);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
}

/**
 * @tc.name: ScreenManager09
 * @tc.desc: Create a virtual screen as expansion of default screen, create windowNode on virtual screen,
 *           and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager09, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    std::vector<sptr<Screen>> screens = ScreenManager::GetInstance().GetAllScreens();
    sptr<Screen> DefaultScreen = screens.front();
    DisplayId virtualDisplayId = DISPLAY_ID_INVALD;
    std::vector<DisplayId> displayIds = DisplayManager::GetInstance().GetAllDisplayIds();
    for (auto& id : displayIds) {
        if (id != defaultDisplayId_) {
            virtualDisplayId = id; // find the display id of virtual screen
        }
    }
    sptr<Window> window = CreateWindowByDisplayId(virtualDisplayId);
    ASSERT_NE(nullptr, window);
    sleep(TEST_SPEEP_S);
    std::vector<ExpandOption> options = {{DefaultScreen->GetId(), 0, 0}, {virtualScreenId, defaultWidth_, 0}};
    ScreenId expansionId = ScreenManager::GetInstance().MakeExpand(options);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);
    sleep(TEST_SPEEP_S);
    ASSERT_NE(SCREEN_ID_INVALID, expansionId);
    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::REMOVE_FROM_GROUP, pair.second);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
    sleep(TEST_SPEEP_S);
    window->Destroy();
    // will add NotifyExpandDisconnect check logic.
}

/**
 * @tc.name: ScreenManager10
 * @tc.desc: Create a virtual screen and destroy it for 10 times, it's not for shot.
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager10, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    for (uint32_t i = 0; i < execTimes_; i++) {
        ScreenManager::GetInstance().RegisterScreenListener(screenListener);
        ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
        ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
        ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
        screenListener->connectFuture_.Reset();
        ASSERT_EQ(virtualScreenId, screenId);
        auto screen = ScreenManager::GetInstance().GetScreenById(virtualScreenId);
        ASSERT_EQ(virtualScreenId, screen->GetId());
        ASSERT_EQ(SCREEN_ID_INVALID, screen->GetParentId());
        ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
        screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
        screenListener->disconnectFuture_.Reset();
        ASSERT_EQ(virtualScreenId, screenId);
        ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    }
}

/**
 * @tc.name: ScreenManager11
 * @tc.desc: Create a virtual screen , mirror and destroy it for 10 times, it's not for shot.
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager11, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    for (uint32_t i = 0; i < 10; i++) {
        ScreenManager::GetInstance().RegisterScreenListener(screenListener);
        ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
        ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
        ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
        ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
        screenListener->connectFuture_.Reset();
        ASSERT_EQ(virtualScreenId, screenId);
        std::vector<ScreenId> mirrorIds;
        mirrorIds.push_back(virtualScreenId);
        ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);
        auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
        screenGroupChangeListener->changeFuture_.Reset();
        ASSERT_EQ(virtualScreenId, pair.first);
        ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);
        auto screen = ScreenManager::GetInstance().GetScreenById(virtualScreenId);
        ASSERT_EQ(virtualScreenId, screen->GetId());
        ASSERT_NE(SCREEN_ID_INVALID, screen->GetParentId());
        ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
        screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
        screenListener->disconnectFuture_.Reset();
        ASSERT_EQ(virtualScreenId, screenId);
        pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
        screenGroupChangeListener->changeFuture_.Reset();
        ASSERT_EQ(virtualScreenId, pair.first);
        ASSERT_EQ(ScreenGroupChangeEvent::REMOVE_FROM_GROUP, pair.second);
        ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
        ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
    }
}

/**
 * @tc.name: ScreenManager12
 * @tc.desc: Create a virtual screen as expansion of default screen cancel Make mirror, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager12, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ASSERT_NE(SCREEN_ID_INVALID, virtualScreenId);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);

    std::vector<ScreenId> mirrorIds;
    mirrorIds.push_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorIds);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);
    auto screen = ScreenManager::GetInstance().GetScreenById(virtualScreenId);
    ASSERT_EQ(virtualScreenId, screen->GetId());
    ASSERT_NE(SCREEN_ID_INVALID, screen->GetParentId());

    ScreenManager::GetInstance().RemoveVirtualScreenFromGroup(mirrorIds);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::REMOVE_FROM_GROUP, pair.second);

    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
}

/**
 * @tc.name: ScreenManager13
 * @tc.desc: Create a virtual screen as expansion of default screen cancel MakeExpand, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager13, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);

    std::vector<sptr<Screen>> screens = ScreenManager::GetInstance().GetAllScreens();
    sptr<Screen> DefaultScreen = screens.front();
    std::vector<ExpandOption> options = {{DefaultScreen->GetId(), 0, 0}, {virtualScreenId, defaultWidth_, 0}};
    ScreenId expansionId = ScreenManager::GetInstance().MakeExpand(options);
    ASSERT_NE(SCREEN_ID_INVALID, expansionId);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);

    std::vector<ScreenId> cancelScreens;
    cancelScreens.emplace_back(virtualScreenId);
    ScreenManager::GetInstance().RemoveVirtualScreenFromGroup(cancelScreens);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::REMOVE_FROM_GROUP, pair.second);

    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
}

/**
 * @tc.name: ScreenManager14
 * @tc.desc: Create a virtual screen, make expand to make mirror, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager14, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);

    std::vector<sptr<Screen>> screens = ScreenManager::GetInstance().GetAllScreens();
    sptr<Screen> DefaultScreen = screens.front();
    std::vector<ExpandOption> options = {{DefaultScreen->GetId(), 0, 0}, {virtualScreenId, defaultWidth_, 0}};
    ScreenId expansionId = ScreenManager::GetInstance().MakeExpand(options);
    ASSERT_NE(SCREEN_ID_INVALID, expansionId);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);

    std::vector<ScreenId> mirrorScreens;
    mirrorScreens.emplace_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorScreens);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::CHANGE_GROUP, pair.second);

    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
}

/**
 * @tc.name: ScreenManager15
 * @tc.desc: Create a virtual screen, make mirror to make expand, and destroy virtual screen
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager15, Function | MediumTest | Level2)
{
    DisplayTestUtils utils;
    ASSERT_TRUE(utils.CreateSurface());
    defaultOption_.surface_ = utils.psurface_;
    defaultOption_.isForShot_ = false;
    sptr<ScreenChangeListener> screenListener = new ScreenChangeListener();
    sptr<ScreenGroupChangeListener> screenGroupChangeListener = new ScreenGroupChangeListener();
    ScreenManager::GetInstance().RegisterScreenListener(screenListener);
    ScreenManager::GetInstance().RegisterScreenGroupListener(screenGroupChangeListener);
    ScreenId virtualScreenId = ScreenManager::GetInstance().CreateVirtualScreen(defaultOption_);
    ScreenId screenId = screenListener->connectFuture_.GetResult(TIME_OUT);
    screenListener->connectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);

    std::vector<ScreenId> mirrorScreens;
    mirrorScreens.emplace_back(virtualScreenId);
    ScreenManager::GetInstance().MakeMirror(defaultScreenId_, mirrorScreens);
    auto pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::ADD_TO_GROUP, pair.second);

    std::vector<sptr<Screen>> screens = ScreenManager::GetInstance().GetAllScreens();
    sptr<Screen> DefaultScreen = screens.front();
    std::vector<ExpandOption> options = {{DefaultScreen->GetId(), 0, 0}, {virtualScreenId, defaultWidth_, 0}};
    ScreenId expansionId = ScreenManager::GetInstance().MakeExpand(options);
    ASSERT_NE(SCREEN_ID_INVALID, expansionId);
    pair = screenGroupChangeListener->changeFuture_.GetResult(TIME_OUT);
    screenGroupChangeListener->changeFuture_.Reset();
    ASSERT_EQ(virtualScreenId, pair.first);
    ASSERT_EQ(ScreenGroupChangeEvent::CHANGE_GROUP, pair.second);

    ASSERT_EQ(DMError::DM_OK, ScreenManager::GetInstance().DestroyVirtualScreen(virtualScreenId));
    screenId = screenListener->disconnectFuture_.GetResult(TIME_OUT);
    screenListener->disconnectFuture_.Reset();
    ASSERT_EQ(virtualScreenId, screenId);
    ScreenManager::GetInstance().UnregisterScreenListener(screenListener);
    ScreenManager::GetInstance().UnregisterScreenGroupListener(screenGroupChangeListener);
}

/**
 * @tc.name: ScreenManager16
 * @tc.desc: Screen orientation.
 * @tc.type: FUNC
 */
HWTEST_F(ScreenManagerTest, ScreenManager16, Function | MediumTest | Level2)
{
    auto screens = ScreenManager::GetInstance().GetAllScreens();
    auto display = DisplayManager::GetInstance().GetDefaultDisplay();
    uint32_t orientation = static_cast<uint32_t>(Orientation::VERTICAL);
    uint32_t end = static_cast<uint32_t>(Orientation::REVERSE_HORIZONTAL);
    for (; orientation <= end; ++orientation) {
        screens[0]->SetOrientation(static_cast<Orientation>(orientation));
        usleep(1E6);
        ASSERT_EQ(static_cast<uint32_t>(screens[0]->GetOrientation()), orientation);
        ASSERT_EQ(static_cast<uint32_t>(display->GetOrientation()), orientation);
    }
    screens[0]->SetOrientation(Orientation::UNSPECIFIED);
    ASSERT_EQ(static_cast<uint32_t>(screens[0]->GetOrientation()), static_cast<uint32_t>(Orientation::UNSPECIFIED));
    ASSERT_EQ(static_cast<uint32_t>(display->GetOrientation()), static_cast<uint32_t>(Orientation::UNSPECIFIED));
}
}
} // namespace Rosen
} // namespace OHOS
