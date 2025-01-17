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
#include <gtest/gtest.h>
#include <transaction/rs_transaction.h>
#include "display_test_utils.h"
#include "display.h"
#include "screen.h"
#include "surface_draw.h"
#include "wm_common.h"
#include "window.h"
#include "window_option.h"
#include "window_manager_hilog.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS::Rosen {
namespace  {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, HILOG_DOMAIN_WINDOW, "DisplayManagerTest"};
    const int WAIT_FOR_SYNC_US = 1000 * 500;  // 500ms
}

class DisplayManagerTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    virtual void SetUp() override;
    virtual void TearDown() override;

    sptr<Window> CreateWindow(std::string name, WindowMode mode, Rect rect);
    bool DrawWindowColor(const sptr<Window>& window, uint32_t color, int32_t width, int32_t height);
    static inline DisplayId displayId_;
    static inline int32_t displayWidth_;
    static inline int32_t displayHeight_;
};

void DisplayManagerTest::SetUpTestCase()
{
    displayId_ = DisplayManager::GetInstance().GetDefaultDisplayId();
    sptr<Display> display = DisplayManager::GetInstance().GetDefaultDisplay();
    if (display == nullptr) {
        return;
    }
    displayWidth_ = display->GetWidth();
    displayHeight_ = display->GetHeight();
}

void DisplayManagerTest::TearDownTestCase()
{
}

void DisplayManagerTest::SetUp()
{
}

void DisplayManagerTest::TearDown()
{
}

sptr<Window> DisplayManagerTest::CreateWindow(std::string name, WindowMode mode, Rect rect)
{
    sptr<WindowOption> option = new WindowOption();
    option->SetDisplayId(displayId_);
    option->SetWindowType(WindowType::WINDOW_TYPE_APP_MAIN_WINDOW);
    int32_t width = 0;
    int32_t height = 0;
    if (mode != WindowMode::WINDOW_MODE_FULLSCREEN) {
        option->SetWindowRect(rect);
    } else {
        width = displayWidth_;
        height = displayHeight_;
    }
    option->SetWindowMode(mode);
    option->SetWindowName(name);
    sptr<Window> window = Window::Create(option->GetWindowName(), option);
    window->AddWindowFlag(WindowFlag::WINDOW_FLAG_SHOW_WHEN_LOCKED);
    window->Show();
    usleep(WAIT_FOR_SYNC_US / 20); // wait for rect updated
    width = window->GetRect().width_;
    height = window->GetRect().height_;
    DrawWindowColor(window, 0x66000000, width, height); // 0x66000000 color_black
    RSTransaction::FlushImplicitTransaction();
    return window;
}

bool DisplayManagerTest::DrawWindowColor(const sptr<Window>& window, uint32_t color, int32_t width, int32_t height)
{
    auto surfaceNode = window->GetSurfaceNode();
    if (surfaceNode == nullptr) {
        WLOGFE("Failed to GetSurfaceNode!");
        return false;
    }
    SurfaceDraw::DrawColor(surfaceNode, width, height, color);
    surfaceNode->SetAbilityBGAlpha(255); // 255 is alpha
    return true;
}

namespace {
/**
 * @tc.name: HasPrivateWindow
 * @tc.desc: Check whether there is a private window in the current display
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindow, Function | SmallTest | Level2)
{
    sptr<Window> window = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    window->SetPrivacyMode(true);
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(hasPrivateWindow);

    window->SetPrivacyMode(false);
    usleep(WAIT_FOR_SYNC_US);
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(!hasPrivateWindow);
    window->Destroy();
}

/**
 * @tc.name: HasPrivateWindowCovered
 * @tc.desc: The private window is covered
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindowCovered, Function | SmallTest | Level2)
{
    sptr<Window> window1 = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    // 10:rect.posX_, 120:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window2 = CreateWindow("private", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 500});
    window2->SetPrivacyMode(true);
    // 10:rect.posX_, 110:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window3 = CreateWindow("covered", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 500});
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(!hasPrivateWindow);
    window1->Destroy();
    window2->Destroy();
    window3->Destroy();
}

/**
 * @tc.name: HasPrivateWindowCovered01
 * @tc.desc: The private window is partially covered
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindowCovered01, Function | SmallTest | Level2)
{
    sptr<Window> window1 = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    // 10:rect.posX_, 120:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window2 = CreateWindow("private", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 500});
    window2->SetPrivacyMode(true);
    // 5:rect.posX_, 110:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window3 = CreateWindow("covered", WindowMode::WINDOW_MODE_FLOATING, Rect {5, 110, 650, 500});
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(hasPrivateWindow);
    window1->Destroy();
    window2->Destroy();
    window3->Destroy();
}

/**
 * @tc.name: HasPrivateWindowCovered02
 * @tc.desc: The private window is covered
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindowCovered02, Function | SmallTest | Level2)
{
    sptr<Window> window1 = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    // 10:rect.posX_, 120:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window2 = CreateWindow("private", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 500});
    window2->SetPrivacyMode(true);
    // 5:rect.posX_, 110:rect.posY_, 655:rect.width, 500:rect.height
    sptr<Window> window3 = CreateWindow("covered1", WindowMode::WINDOW_MODE_FLOATING, Rect {5, 110, 655, 500});
    // 5:rect.posX_, 300:rect.posY_, 655:rect.width, 500:rect.height
    sptr<Window> window4 = CreateWindow("covered2", WindowMode::WINDOW_MODE_FLOATING, Rect {5, 300, 655, 500});
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(!hasPrivateWindow);
    window1->Destroy();
    window2->Destroy();
    window3->Destroy();
    window4->Destroy();
}

/**
 * @tc.name: HasPrivateWindowCovered03
 * @tc.desc: The private window is partially covered
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindowCovered03, Function | SmallTest | Level2)
{
    sptr<Window> window1 = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    // 10:rect.posX_, 120:rect.pos_Y, rect.width_:650, rect.height_:700
    sptr<Window> window2 = CreateWindow("private", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 700});
    window2->SetPrivacyMode(true);
    // 5:rect.posX_, 110:rect.pos_Y, rect.width_:655, rect.height_:500
    sptr<Window> window3 = CreateWindow("covered1", WindowMode::WINDOW_MODE_FLOATING, Rect {5, 110, 655, 500});
    // 5:rect.posX_, 700:rect.pos_Y, rect.width_:655, rect.height_:500
    sptr<Window> window4 = CreateWindow("covered2", WindowMode::WINDOW_MODE_FLOATING, Rect {5, 700, 655, 500});
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(hasPrivateWindow);
    window1->Destroy();
    window2->Destroy();
    window3->Destroy();
    window4->Destroy();
}

/**
 * @tc.name: HasPrivateWindowSkipSnapShot
 * @tc.desc: set snap shot skip
 * @tc.type: FUNC
 */
HWTEST_F(DisplayManagerTest, HasPrivateWindowSkipSnapShot, Function | SmallTest | Level2)
{
    sptr<Window> window1 = CreateWindow("test", WindowMode::WINDOW_MODE_FULLSCREEN, Rect {0, 0, 0, 0});
    // 10:rect.posX_, 120:rect.posY_, 650:rect.width, 500:rect.height
    sptr<Window> window2 = CreateWindow("private", WindowMode::WINDOW_MODE_FLOATING, Rect {10, 120, 650, 500});
    window2->SetSnapshotSkip(true);
    usleep(WAIT_FOR_SYNC_US);
    bool hasPrivateWindow = false;
    DisplayId id = DisplayManager::GetInstance().GetDefaultDisplayId();
    DisplayManager::GetInstance().HasPrivateWindow(id, hasPrivateWindow);
    ASSERT_TRUE(!hasPrivateWindow);
    window1->Destroy();
    window2->Destroy();
}
}
} // namespace OHOS::Rosen
