# 公告
因OpenHarmony部件设计规范化需求，2022.9月起**windowmanager**仓库更名为**window_window_manager**，本仓库归档不再维护。请前往[window_window_manager](https://gitee.com/openharmony/window_window_manager)仓库，了解最新代码信息。

# windowmanager

-   [简介](#简介)
-   [目录](#目录)
-   [约束](#约束)
-   [接口说明](#接口说明)
-   [相关仓](#相关仓)

## 简介

**窗口子系统** 提供窗口管理和Display管理的基础能力，是系统图形界面显示所需的基础子系统

其主要的结构如下图所示：

![窗口子系统架构图](./figures/WindowManager.png)

- **Window Manager Client**

    应用进程窗口管理接口层，提供窗口对对象抽象和窗口管理接口，对接原能力和UI框架。

- **Display Manager Client**

    应用进程Display管理接口层，提供Display信息抽象和Display管理接口。

- **Window Manager Server**

    窗口管理服务，提供窗口布局、Z序控制、窗口树结构、窗口拖拽、窗口快照等能力，并提供窗口布局和焦点窗口给多模输入

- **Display Manager Server**

    Display管理服务，提供Display信息、屏幕截图、屏幕亮灭和亮度处理控制，并处理Display与Screen映射关系

## 目录
```
foundation/window/window_manager/
├── dm                      # Dislplay Manager Client实现代码
├── dmserver                # Dislplay Manager Service实现代码
├── interfaces              # 对外接口存放目录
│   ├── innerkits           # native接口存放目录
│   └── kits                # js/napi接口存放目录
├── resources               # 框架使用资源文件存放目录
├── sa_profile              # 系统服务配置文件
├── snapshot                # 截屏命令行工具实现代码
├── utils                   # 工具类存放目录
├── wm                      # Window Manager Client实现代码
├── wmserver                # Window Manager Service实现代码
```

## 约束
- 语言版本
    - C++11或以上

## 接口说明

- [Window](https://gitee.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis/js-apis-window.md)
- [Display](https://gitee.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis/js-apis-display.md)

## 相关仓
- [graphic_graphic_2d](https://gitee.com/openharmony/graphic_graphic_2d)
- [arkui_ace_engine](https://gitee.com/openharmony/arkui_ace_engine)
- [ability_ability_runtime](https://gitee.com/openharmony/ability_ability_runtime)
- [multimodalinput_input](https://gitee.com/openharmony/multimodalinput_input)