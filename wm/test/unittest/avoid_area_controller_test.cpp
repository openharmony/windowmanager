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

#include "avoid_area_controller_test.h"
#include "avoid_area_controller.h"
#include "display_manager.h"
#include "mock_window_adapter.h"
#include "singleton_mocker.h"
#include "window_node.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS {
namespace Rosen {
namespace {
    constexpr HiviewDFX::HiLogLabel LABEL = {LOG_CORE, 0, "AvoidAreaControllerTest"};

    const Rect EMPTY_RECT = {0, 0, 0, 0};
    const uint32_t LEFT = 0;
    const uint32_t TOP = 1;
    const uint32_t RIGHT = 2;
    const uint32_t BOTTOM = 3;
    const float BARRATIO = 0.3;
}

Rect AvoidAreaControllerTest::topAvoidRect_ = EMPTY_RECT;
Rect AvoidAreaControllerTest::leftAvoidRect_ = EMPTY_RECT;

static bool RectEqualToRect(const Rect& l, const Rect& r)
{
    bool res = ((l.posX_ == r.posX_) && (l.posY_ == r.posY_) && (l.width_ == r.width_) && (l.height_ == r.height_));
    if (!res) {
        WLOGFE("      [   x    y    w    h]");
        WLOGFE("Expect:%{public}4d %{public}4d %{public}4u %{public}4u ", l.posX_, l.posY_, l.width_, l.height_);
        WLOGFE("Get:   %{public}4d %{public}4d %{public}4u %{public}4u ", r.posX_, r.posY_, r.width_, r.height_);
    }
    return res;
}

void AvoidAreaControllerTest::InitByScreenRect(const Rect& screenRect)
{
    topAvoidRect_ = {0, 0, screenRect.width_,
        std::min(static_cast<uint32_t>(screenRect.height_), static_cast<uint32_t>(screenRect.width_ * BARRATIO))};
    // make sure leftAvoidRect h > w and w < screen.width. And use a bigger ratio to cover origin topAvoid
    leftAvoidRect_ = {0, 0,
        std::min(static_cast<uint32_t>(screenRect.width_), static_cast<uint32_t>(screenRect.height_ * BARRATIO)),
        screenRect.height_};
}

void AvoidAreaControllerTest::SetUpTestCase()
{
    auto display = DisplayManager::GetInstance().GetDisplayById(0);
    if (display == nullptr) {
        WLOGFE("GetDefaultDisplay: failed!");
    } else {
        WLOGFI("GetDefaultDisplay: id %{public}" PRIu64", w %{public}d, h %{public}d, fps %{public}u",
            display->GetId(), display->GetWidth(), display->GetHeight(), display->GetFreshRate());
    }
    Rect screenRect = {0, 0, display->GetWidth(), display->GetHeight()};
    AvoidAreaControllerTest::InitByScreenRect(screenRect);
}

void AvoidAreaControllerTest::TearDownTestCase()
{
}

void AvoidAreaControllerTest::SetUp()
{
}

void AvoidAreaControllerTest::TearDown()
{
}

namespace {
/**
 * @tc.name: IsAvoidAreaNode01
 * @tc.desc: Create a APP Window. Test IsAvoidAreaNode
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, IsAvoidAreaNode01, Function | SmallTest | Level2)
{
    auto avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::APP_WINDOW_BASE);
    node->SetWindowProperty(property);

    ASSERT_EQ(false, avoidAreaController->IsAvoidAreaNode(node));
}

/**
 * @tc.name: IsAvoidAreaNode02
 * @tc.desc: Create a Status Bar Window. Test IsAvoidAreaNode
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, IsAvoidAreaNode02, Function | SmallTest | Level2)
{
    auto avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);

    ASSERT_TRUE(avoidAreaController->IsAvoidAreaNode(node));
}

/**
 * @tc.name: AddAvoidAreaNode01
 * @tc.desc: Add a new avoid area Node
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, AddAvoidAreaNode01, Function | SmallTest | Level2)
{
    sptr<WindowNode> node = new WindowNode();

    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);

    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node));
}

/**
 * @tc.name: AddAvoidAreaNode02
 * @tc.desc: Add a exist avoid area Node
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, AddAvoidAreaNode02, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node1 = new WindowNode();
    sptr<WindowProperty> property1 = new WindowProperty();
    property1->SetWindowId(100u);
    property1->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    property1->SetWindowRect(topAvoidRect_);
    node1->SetWindowProperty(property1);
    node1->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node1));

    sptr<WindowNode> node2 = new WindowNode();
    sptr<WindowProperty> property2 = new WindowProperty();
    property2->SetWindowId(100u);
    property2->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node2->SetWindowProperty(property2);
    node2->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_ERROR_INVALID_PARAM, avoidAreaController->AddAvoidAreaNode(node2));
}

/**
 * @tc.name: AddAvoidAreaNode03
 * @tc.desc: Add two new avoid area Nodes
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, AddAvoidAreaNode03, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node1 = new WindowNode();
    sptr<WindowProperty> property1 = new WindowProperty();
    property1->SetWindowId(100u);
    property1->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node1->SetWindowProperty(property1);
    node1->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node1));

    sptr<WindowNode> node2 = new WindowNode();
    sptr<WindowProperty> property2 = new WindowProperty();
    property2->SetWindowId(101u);
    property2->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node2->SetWindowProperty(property2);
    node2->SetLayoutRect(leftAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node2));
}

/**
 * @tc.name: RemoveAvoidAreaNode01
 * @tc.desc: Add a new avoid area. And Remove this.
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, RemoveAvoidAreaNode01, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node1 = new WindowNode();
    sptr<WindowProperty> property1 = new WindowProperty();
    property1->SetWindowId(100u);
    property1->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node1->SetWindowProperty(property1);
    node1->SetLayoutRect(leftAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node1));

    ASSERT_EQ(WMError::WM_OK, avoidAreaController->RemoveAvoidAreaNode(node1));
}

/**
 * @tc.name: RemoveAvoidAreaNode02
 * @tc.desc: Remove a unexist avoid area.
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, RemoveAvoidAreaNode02, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetLayoutRect(topAvoidRect_);
    node->SetWindowProperty(property);
    ASSERT_EQ(WMError::WM_ERROR_INVALID_PARAM, avoidAreaController->RemoveAvoidAreaNode(node));
}

/**
 * @tc.name: UpdateAvoidAreaNode01
 * @tc.desc: Add a new avoid area node and update this
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, UpdateAvoidAreaNode01, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node));

    property->SetWindowRect(leftAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->UpdateAvoidAreaNode(node));
}

/**
 * @tc.name: UpdateAvoidAreaNode02
 * @tc.desc: update a unexist avoid area node
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, UpdateAvoidAreaNode02, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);

    ASSERT_EQ(WMError::WM_ERROR_INVALID_PARAM, avoidAreaController->UpdateAvoidAreaNode(node));
}

/**
 * @tc.name: GetAvoidArea01
 * @tc.desc: GetAvoidArea
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, GetAvoidArea01, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    std::vector<Rect> avoidArea = avoidAreaController->GetAvoidArea();
    ASSERT_EQ(4u, static_cast<uint32_t>(avoidArea.size()));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[LEFT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[TOP]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[RIGHT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[BOTTOM]));
}

/**
 * @tc.name: GetAvoidArea02
 * @tc.desc: Add a new node. And GetAvoidArea
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, GetAvoidArea02, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node));

    std::vector<Rect> avoidArea = avoidAreaController->GetAvoidArea();
    ASSERT_EQ(4u, static_cast<uint32_t>(avoidArea.size()));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[LEFT]));
    ASSERT_TRUE(RectEqualToRect(topAvoidRect_, avoidArea[TOP]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[RIGHT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[BOTTOM]));
}

/**
 * @tc.name: GetAvoidArea03
 * @tc.desc: Add a new node. Update this node. And GetAvoidArea
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, GetAvoidArea03, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node));

    node->SetLayoutRect(leftAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->UpdateAvoidAreaNode(node));

    std::vector<Rect> avoidArea = avoidAreaController->GetAvoidArea();
    ASSERT_EQ(4u, static_cast<uint32_t>(avoidArea.size()));
    ASSERT_TRUE(RectEqualToRect(leftAvoidRect_, avoidArea[LEFT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[TOP]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[RIGHT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[BOTTOM]));
}

/**
 * @tc.name: GetAvoidAreaByType01
 * @tc.desc: Search a unexist AvoidAreaType. And GetAvoidAreaByType
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, GetAvoidAreaByType01, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    std::vector<Rect> avoidArea = avoidAreaController->GetAvoidAreaByType(AvoidAreaType::TYPE_CUTOUT);

    ASSERT_EQ(4u, static_cast<uint32_t>(avoidArea.size()));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[LEFT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[TOP]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[RIGHT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[BOTTOM]));
}

/**
 * @tc.name: GetAvoidAreaByType02
 * @tc.desc: Search a unexist AvoidAreaType. And GetAvoidAreaByType
 * @tc.type: FUNC
 * @tc.require: AR000GGTVD
 */
HWTEST_F(AvoidAreaControllerTest, GetAvoidAreaByType02, Function | SmallTest | Level2)
{
    sptr<AvoidAreaController> avoidAreaController = new AvoidAreaController(nullptr);

    sptr<WindowNode> node = new WindowNode();
    sptr<WindowProperty> property = new WindowProperty();
    property->SetWindowId(100u);
    property->SetWindowType(WindowType::WINDOW_TYPE_STATUS_BAR);
    node->SetWindowProperty(property);
    node->SetLayoutRect(topAvoidRect_);
    ASSERT_EQ(WMError::WM_OK, avoidAreaController->AddAvoidAreaNode(node));

    std::vector<Rect> avoidArea = avoidAreaController->GetAvoidAreaByType(AvoidAreaType::TYPE_SYSTEM);

    ASSERT_EQ(4u, static_cast<uint32_t>(avoidArea.size()));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[LEFT]));
    ASSERT_TRUE(RectEqualToRect(topAvoidRect_, avoidArea[TOP]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[RIGHT]));
    ASSERT_TRUE(RectEqualToRect(EMPTY_RECT, avoidArea[BOTTOM]));
}
}
} // namespace Rosen
} // namespace OHOS