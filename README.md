# Moonwake

Moonwake 是一款面向 macOS 的多声道音乐播放器与效果器宿主。它提供两套可切换的工作区：Studio A 用于路由、效果链和输出监看，Listening B 用于日常聆听；应用默认进入 Listening B。

当前版本为 `0.7.0`，处于开发阶段。

## 功能

- 播放本地音乐，并从音频标签读取作品名、艺术家、专辑、年份、曲序、格式、位深和声道信息。
- 以文件选择或拖放方式后台导入音频文件和文件夹；文件夹会递归发现支持的音乐。
- 曲库按文件身份保存唯一音乐记录；命名歌单和独立播放队列允许重复曲目，并保留每次出现的条目身份。
- 曲库首页以封面卡片浏览歌单、专辑和艺术家，展示曲目数、时长及已知的歌单创建日期；封面可由歌曲封面拼合，缺少封面时使用皮肤占位图。
- 曲库首页、歌曲列表与当前播放分为独立页面；收藏、播放队列和当前播放提供固定导航入口，Studio A / Listening B 切换保留当前页面与播放状态。
- 集合详情按曲名、艺术家、专辑、时长分列；曲库与播放页队列的右键菜单支持收藏、加入歌单和移除条目，曲库曲目可安排下一首或追加到队列。
- 浏览全部歌曲、收藏、专辑和艺术家；歌单支持新建、重命名、复制、删除、排序和批量编辑。删除歌单或移出歌单不会删除音乐文件。
- 单击选择，双击或显式播放动作建立播放队列；支持下一首播放、追加到队列末尾和将队列另存为歌单。浏览与编辑其他歌单不切换当前音轨。
- Cmd/Shift 多选，拖动重排，右键与 Actions 菜单操作当前可见选区；可撤销列表移除和重排，播放位置保持不变。搜索状态下禁用含糊的拖放位置。
- 后台导入本地 UTF-8 M3U/M3U8，保留重复与缺失条目；支持相对路径与本地 file URI，原子导出 UTF-8 M3U8。网络和流媒体引用报告为不支持。缺失曲目可重新定位。
- 按作品名、艺术家、专辑、格式或声道数即时搜索；浏览选区与当前播放标记独立。
- 手动切歌与自然播放结束采用独立的导航语义；随机播放保留实际播放历史，支持后退和前进，并跳过无法读取的曲目。
- 后台预读后续曲目并转换到设备采样率，在音频块内连续衔接曲目，支持单曲及队列循环。
- 读取嵌入式或同目录封面并保存为受限尺寸的本地缓存；曲库、歌单、收藏、队列、所选集合、歌曲搜索条件、播放偏好和工作区变更会自动原子保存。
- macOS 单实例运行；再次打开 Moonwake 或向其传入音乐文件时会唤回现有窗口。
- 在 macOS 控制中心发布当前作品、艺术家、专辑、封面、进度与队列位置，并响应键盘和耳机的播放、暂停、切歌及进度跳转命令。
- 保存当前作品的播放位置；重新启动后恢复到该位置并保持暂停。播放期间自动保存使用缓存的插件状态，暂停或正常退出时补存最新插件参数，避免捕获插件状态打断音频。
- 旧会话自动迁移为曲库、播放队列和 Imported queue 歌单，保留条目身份、顺序、循环／随机偏好和播放位置。
- 在后台读取保存的曲库与队列、重验证元数据和封面；恢复期间接收的导入与媒体键命令会在完成后执行。
- 输出路由支持为逻辑扬声器分配实际设备端口或断开，按设备和布局保存；设备设置可打开原生配置应用。
- 插件清单支持格式筛选、单格式扫描和自定义 VST3 目录管理。取消扫描保留已有记录及已完成的有效结果。
- Studio A / Listening B 两套完整布局在同一窗口内切换，切换时保持窗口外边界和播放状态，并共享统一的皮肤组件规范。
- Studio A 的播放页提供待播队列、效果链和可切换的 Peak、Peak + RMS、LUFS / True Peak 仪表；Listening B 以封面、缓存的封面氛围背景和曲名／艺术家列表为主体。曲库、详情与播放页共用固定位置的导航和播放控制，Studio 内直接管理效果链，Settings 及弹出菜单、提示和滚动条跟随当前工作区主题。随机与循环图标位于播放控制组两侧。
- 在独立扫描进程中发现 VST3 与 Audio Unit 插件，并将其作为插入效果器加载。
- 插件目录支持类型、厂商、AU/VST3 格式与文字组合筛选。Settings 可分别启用 AU/VST3 扫描及新增插件，偏好自动保存；已有和已保存的效果链继续可用。
- 提供 Mono、Stereo、2.1、LCR、Quad、5.0、5.1、6.1、7.0、7.1、5.1.2、5.1.4、7.1.2、7.1.4、7.1.6、9.1.4、9.1.6 共 17 种真实输出布局，最多 16 声道；扬声器布局不代表 Dolby Atmos 对象解码。
- 每个效果器可选择整个输出总线、单个扬声器或任意声道对；其余声道保留直通，延迟补偿保持各声道对齐。不兼容的插件总线或作用范围会被拒绝并显示原因。
- Studio A 的 Clear tails 清理插件尾音与宿主延迟缓冲，保留插件参数和播放位置；Bypass all 在原声与效果输出之间淡变，保留每个插件的旁路状态。全链旁路为会话内监听状态，插件继续处理，切换工作区不会改变该状态。
- Output 的 Mixer 模式提供每声道增益、Mute 和可多选的 Solo；Mute 优先，增益变化使用 20 ms 平滑。控制位于效果链之后、主音量之前，按扬声器身份保存，临时不在布局内的 Solo 不影响当前输出。
- 响度测量位于效果链与输出增益之后，使用固定版本 libebur128 1.2.6；加载、跳转或输出配置改变时开始新测量，工作区与仪表视图切换不重置累计值。
- 音频设备、采样率、缓冲区和输出布局设置，以及可编辑的输出端口路由。
- 保存输出设备、采样率和缓冲区偏好；设备暂时不可用时保留期望配置，使用可用设备并显示恢复诊断。
- 声明式 Skin API 1：皮肤控制页面布局、复合组件内部行与槽位布局，以及图标、滑块、菜单、提示、滚动条和输入框的视觉度量。绘制、点击区域和滚动使用同一份几何；皮肤可作为 `.moonwake-skin` 包导入。

## 构建要求

- macOS
- Xcode Command Line Tools
- CMake 3.22 或更高版本
- 支持 C++20 的 Apple Clang
- 已初始化固定到 JUCE 8.0.12 的 `ThirdParty/JUCE` submodule

## 构建、运行与测试

```sh
git submodule update --init --recursive
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

macOS 默认配置会对本地构建的应用包及其内嵌扫描器 helper 执行 ad-hoc 签名；关闭 `MOONWAKE_ADHOC_SIGN_LOCAL_BUILDS` 后不执行。正式分发仍需要使用开发者证书完成签名、公证与发布流程。

### Windows 适配

macOS 是优先验证平台。Windows 已加入平台格式选择、扫描 helper 部署和文件读取适配，仍需完成 Windows 编译、设备播放及真实 VST3 插件验证。

在安装 Visual Studio 2022 的“使用 C++ 的桌面开发”组件和 Windows SDK 后，可使用 PowerShell 构建 x64 版本：

```powershell
git submodule update --init --recursive
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Debug --parallel 8
ctest --test-dir build-windows -C Debug --output-on-failure
& .\build-windows\Moonwake_artefacts\Debug\Moonwake.exe
```

构建会把 `MoonwakePluginScanner.exe` 和第三方许可证复制到 `Moonwake.exe` 所在目录。移动或分发应用时应保留这些文件；扫描始终在独立 helper 进程中执行。`MoonwakeScannerDeployment` 测试检查 helper 副本与当前构建一致，并验证它能启动。

Windows 使用 VST3；Audio Unit 只在 macOS 注册，Windows 的 AU 开关不可用，扫描范围中不提供 AU。默认扫描目录由 JUCE 提供，包含 `%LOCALAPPDATA%\Programs\Common\VST3` 和当前进程对应的 `%ProgramFiles%\Common Files\VST3`；其他位置可在 Settings → Plug-ins 添加。已有缓存中的不支持格式保留为不可用记录。

Windows 构建沿用 JUCE 的默认音频后端配置，包括 WASAPI；当前未启用 ASIO。Windows 的音频设备、驱动缓冲和多声道输出能力需要在目标设备上验证，macOS 的控制中心媒体集成不适用于 Windows。

## 开发第三方皮肤

公开接口从 [Skin API 开发者指南](docs/skin-api/SKIN_DEVELOPER_GUIDE.md) 开始。配套目录还包含：

- [错误码参考](docs/skin-api/ERROR_CODES.md)
- [JSON Schema](docs/skin-api/schema/)
- [可直接验证的第三方参考皮肤](docs/skin-api/examples/CompleteThirdParty/)

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
├── state.xml       # 队列及原始音乐路径、播放位置、工作区、音量、音频设备与插件格式偏好、输出布局、效果链、皮肤选择与开发皮肤目录
├── plugins.xml     # 已扫描插件缓存
├── Artwork/        # 从音乐标签或同目录图片生成的受限尺寸封面缓存
└── Skins/          # 已安装的外部皮肤及内部校验元数据
```

Windows 使用 `%APPDATA%\Moonwake\`，其文件和子目录结构相同。

应用包本身不会把播放的音乐复制到该目录。需要彻底清理开发运行数据时，先退出 Moonwake，再删除上面的 `Moonwake` 目录；这会同时清空保存状态、封面缓存、插件缓存与已安装皮肤。

构建产物位于仓库的 `build/` 或 `build-*` 目录，均已被 `.gitignore` 排除。

## 仓库结构

```text
Resources/Skins/   内置皮肤与布局
Source/            应用、音频、插件、皮肤和 UI 源码
Tests/             单元、集成与视觉隔离测试
Tools/             MoonwakeSkinTool 入口
docs/skin-api/     第三方 Skin API 公开文档、Schema 与示例
ThirdParty/JUCE/   固定版本的 JUCE submodule（源码本体不进入 Moonwake 历史）
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
