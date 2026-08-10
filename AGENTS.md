# AGENTS.md

本文件定义自动化开发代理在 Moonwake 仓库中的工作约定。适用于仓库根目录及全部子目录；更深层级的 `AGENTS.md` 可以为其目录补充或覆盖更具体的规则。

## 产品约束

- 产品名为 Moonwake，作者署名为 `by Sam Kuler`，bundle ID 前缀为 `com.samkuler.moonwake`。
- 当前优先平台是 macOS。不要以跨平台抽象为由削弱 macOS 的 VST3、Audio Unit、签名或原生音频支持。
- Listening B 是默认工作区；Studio A 与 Listening B 的切换不得重启音频设备或重建播放状态，并须保持窗口外边界不变。
- Stereo、5.1、5.1.2、7.1.2 必须对应真实的 2、6、8、10 声道设备缓冲和效果总线，不得用固定立体声缓冲或仅 UI 标签冒充。源素材不存在的扬声器声道可以按 `ChannelRouter` 规则保持静音；升配和降混行为必须显式测试。
- 插件扫描必须留在独立 helper 进程；不要把不受信任插件扫描移回主进程。

## 目录职责

- `Source/app/`：应用状态协调、播放意图、自动保存调度与产品路径；不得承载实时音频处理。
- `Source/audio/`：实时音频、声道路由、设备与效果处理图。
- `Source/library/`：曲库搜索投影、文件与文件夹发现、后台元数据导入及封面缓存；不得直接控制播放传输。
- `Source/plugins/`：VST3/AU 发现、扫描、缓存与效果链状态。
- `Source/skin/`：Skin API 的解析、验证、编译、渲染、安装与工具实现。
- `Source/ui/`：JUCE 组件、布局宿主和 Settings 面板。
- `Resources/Skins/`：随应用内置的皮肤包；必须满足与第三方包相同的稳定契约。
- `Tests/`：单元、集成、真实格式宿主和 UI/皮肤回归测试。
- `docs/skin-api/`：对第三方开发者公开的 Skin API 1 契约。

## 构建与验证

使用独立构建目录，不要在源码目录生成 CMake 产物：

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

涉及皮肤接口、Schema、布局、renderer 或公开组件目录的修改，还必须运行：

```sh
./build/MoonwakeSkinTool schema --output-dir docs/skin-api/schema --force
./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty
./build/MoonwakeSkinTool verify-docs \
  --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md \
  --example docs/skin-api/examples/CompleteThirdParty
```

macOS 应用构建后应校验包内 helper 与签名：

```sh
codesign --verify --deep --strict --verbose=2 \
  build/Moonwake_artefacts/Debug/Moonwake.app
```

不要在没有当前命令输出的情况下声称构建、测试或签名通过。

## C++ 与实时音频规则

- 使用项目设定的 C++20 和 JUCE 8.0.12；依赖版本必须固定。
- JUCE 由 `ThirdParty/JUCE` submodule 提供；不要恢复配置期 `FetchContent` 下载，也不要修改或提交 submodule 内部文件。
- 音频回调中禁止文件系统访问、动态分配、阻塞锁、进程启动、插件扫描和 UI 调用。
- 对声道数、总线布局和插件 layout support 做显式验证；修改音频链时至少覆盖 2、6、8、10 声道。
- 状态文件与插件缓存采用可恢复、原子式写入；错误必须能到达 UI，而不是静默吞掉。
- 保持源码职责边界，不在 UI 组件中重新实现路由、持久化或皮肤编译逻辑。

## Skin API 规则

- Skin API 1 当前是开发期稳定契约。公开发布后，修改已发布字段、枚举、组件 ID、错误码或语义前，先判断是否需要新的 API 主版本。
- `SkinSchemaRegistry` 生成的 Schema 是机器可读真源；不要手工维护另一套不一致的 Schema。
- 修改接口时必须同步更新实现、Schema、开发者指南、错误码、完整示例和测试。
- 皮肤保持声明式和沙箱化：不得执行脚本或原生代码，不得访问网络、用户文件、音频缓冲和插件实例。
- 业务行为与可访问性名称由应用拥有；皮肤只能绑定公开组件并改变布局和绘制。
- 新增 UI 控件时，先定义稳定组件 ID、kind、可绑定状态、最小尺寸和焦点顺序，再供内置或第三方皮肤使用。
- Studio、Listening、Effects、Settings 四个入口必须保持完整；Foundation fallback 必须始终可用。
- 焦点只绘制一层明确的视觉反馈，避免组件焦点环与 renderer 状态叠加成双框。

## UI/UX 规则

- 优先使用皮肤 token 和公开 renderer，不在业务面板中散落硬编码字体、颜色、圆角和间距。
- 保持文字基线、控件边界和同类面板对齐；在最小窗口 `1040x700` 与默认窗口 `1240x800` 下检查遮挡、裁切和重叠。
- 图标用于常见、可识别的紧凑操作；可能含糊的动作必须提供文字或 tooltip，不为追求图标化牺牲可理解性。
- 只读信息不得伪装成可点击按钮。下拉框、弹出菜单、按钮和列表必须使用其所在 Skin API layout 解析出的 renderer，不得回落到 JUCE 平台默认外观。
- 修改视觉样式后，应对 Studio、Listening、Effects、Settings 以及 normal/focused/selected/disabled 状态做截图验收。

## Git 与交付

- 提交信息使用 Conventional Commits，例如 `feat: ...`、`fix: ...`、`docs: ...`、`test: ...`。
- 不提交 `build/`、`build-*`、`cmake-build-*`、`.worktrees/`、`.DS_Store`、本机运行时数据或内部 `.superpowers/` 计划。
- `README.md`、`AGENTS.md` 和 `docs/skin-api/` 是正式公开交付物，应进入仓库历史。
- 不重写、删除或清理用户已有改动；执行破坏性 Git 操作前必须获得明确授权。
- 合并前运行 `git diff --check`，并确认没有意外二进制、构建缓存或私有路径。
