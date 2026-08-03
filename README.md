# Moonwake

Moonwake 是一款面向 macOS 的多声道音乐播放器与效果器宿主。它提供两套可切换的工作区：Studio A 用于路由、效果链和输出监看，Listening B 用于日常聆听；应用默认进入 Listening B。

当前版本为 `0.1.0`，处于开发阶段。

## 功能

- 播放本地音乐，管理播放队列、曲目信息和持久化状态。
- Studio A / Listening B 两套完整布局在同一窗口内切换，切换时保持窗口外边界和播放状态，并共享统一的皮肤组件规范。
- 在独立扫描进程中发现 VST3 与 Audio Unit 插件，并将其作为插入效果器加载。
- 原生支持 Stereo、5.1、5.1.2 和 7.1.2；路由使用真实声道布局，插件声明支持相应总线布局时按该声道数处理，不兼容插件会被拒绝。
- 音频设备、采样率、缓冲区和输出布局设置，以及只读的输出端口映射视图。
- 声明式 Skin API 1：皮肤可以定义 Studio、Listening、Effects、Settings 的布局与视觉样式，并可作为 `.moonwake-skin` 包导入。

## 构建要求

- macOS
- Xcode Command Line Tools
- CMake 3.22 或更高版本
- 支持 C++20 的 Apple Clang
- 首次配置时可访问 GitHub，以获取固定版本 JUCE 8.0.12

## 构建、运行与测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
open build/Moonwake_artefacts/Debug/Moonwake.app
ctest --test-dir build --output-on-failure
```

默认构建包含：

- `Moonwake`：主应用。
- `MoonwakePluginScanner`：嵌入应用包的隔离插件扫描器。
- `MoonwakeTests`：单元与集成测试。
- `MoonwakeSkinTool`：皮肤验证、布局检查、快照与 Schema 工具。
- `MoonwakeHostingFixture`：仅供测试使用的多声道 VST3 fixture。

默认配置会对本地构建的应用包及其内嵌扫描器 helper 执行 ad-hoc 签名；关闭 `MOONWAKE_ADHOC_SIGN_LOCAL_BUILDS` 后不执行。正式分发仍需要使用开发者证书完成签名、公证与发布流程。

## 开发第三方皮肤

公开接口从 [Skin API 开发者指南](docs/skin-api/SKIN_DEVELOPER_GUIDE.md) 开始。配套目录还包含：

- [错误码参考](docs/skin-api/ERROR_CODES.md)
- [JSON Schema](docs/skin-api/schema/)
- [可直接验证、满足全部必需契约的第三方最小示例](docs/skin-api/examples/CompleteThirdParty/)

构建完成后，可以直接验证公开文档与示例：

```sh
./build/MoonwakeSkinTool schema --output-dir docs/skin-api/schema --force
./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty
./build/MoonwakeSkinTool verify-docs \
  --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md \
  --example docs/skin-api/examples/CompleteThirdParty
```

Skin API 1 是声明式、受限的外观与布局接口。皮肤不能执行代码、访问网络或任意用户文件，也不能改变播放器操作的业务含义；只能引用皮肤包内经过验证的 JSON、字体和图像资源。

## 运行时数据

Moonwake 会在当前用户目录写入以下数据：

```text
~/Library/Application Support/Moonwake/
├── state.xml       # 队列及原始音乐路径、工作区、音量、输出布局、效果链、皮肤选择与开发皮肤目录
├── plugins.xml     # 已扫描插件缓存
└── Skins/          # 已安装的外部皮肤及内部校验元数据
```

应用包本身不会把播放的音乐复制到该目录。需要彻底清理开发运行数据时，先退出所有 Moonwake 实例，再删除上面的 `Moonwake` 目录；这会同时清空保存状态、插件缓存与已安装皮肤。

构建产物位于仓库的 `build/` 或 `build-*` 目录，均已被 `.gitignore` 排除。

## 仓库结构

```text
Resources/Skins/   内置皮肤与布局
Source/            应用、音频、插件、皮肤和 UI 源码
Tests/             单元、集成与视觉隔离测试
Tools/             MoonwakeSkinTool 入口
docs/skin-api/     第三方 Skin API 公开文档、Schema 与示例
```

## 许可

Copyright © 2026 Sam Kuler。

Moonwake 的程序源码、测试和内置皮肤以 [GNU Affero General Public License v3.0 only](LICENSE) 发布。第三方分发包含 JUCE 的构建产物时，还必须遵守 [JUCE 8 自身的 AGPLv3 或商业许可条款](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md)；Moonwake 的许可证不会替代或修改 JUCE 的许可证。

Skin API 资料采用目录级例外：

- `docs/skin-api/` 中的说明文档使用 [CC BY 4.0](docs/skin-api/LICENSE)。
- `docs/skin-api/schema/` 中的 JSON Schema 使用 [MIT](docs/skin-api/schema/LICENSE)。
- `docs/skin-api/examples/CompleteThirdParty/` 示例包使用 [MIT](docs/skin-api/examples/CompleteThirdParty/LICENSE)。

第三方皮肤不因使用 Skin API 而自动采用 Moonwake 的许可证；其作者应在皮肤 manifest 中声明自己有权使用的许可证。

## 作者

Moonwake — by Sam Kuler
