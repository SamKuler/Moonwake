# Complete Third-Party Skin

这是可直接加载的 Skin API 1 参考包，包含四个基础布局、曲库总览和集合详情，并展示封面卡片网格、歌单新建与编辑、独立播放队列、插件筛选、独立扫描范围、VST3 目录管理、设备配置、可编辑路由和多声道仪表／混音控件。各入口满足对应 required component 规则，原有 target 中未覆盖的 renderer 继承自 `builtin.foundation`。可从内置皮肤的 Settings → Appearance → Developer folder 加载该目录并进入开发预览。

完整接口见 [开发者指南](../../SKIN_DEVELOPER_GUIDE.md)，机器可读约束见 [JSON Schema](../../schema/)。

## 文件与模块

| 文件 | 公开模块 | 作用 |
|---|---|---|
| `manifest.json` | Manifest | 声明身份、版本、capability 和六个入口；对应 `manifest.schema.json` |
| `tokens.json` | Tokens | 定义本示例使用的颜色、尺寸、字体、渐变和动画时长；对应 `tokens.schema.json` |
| `components/index.json` | Renderer Index | 把稳定 target/style 名映射到 Render Definition；对应 `component-index.schema.json` |
| `components/button-primary.json` | Render Definition | 主操作按钮及状态动画；对应 `render.schema.json` |
| `components/button-neutral.json` | Render Definition | 中性按钮；对应 `render.schema.json` |
| `components/surface-studio.json` | Render Definition | Studio 共享面板背景；通过工作区断点选择 |
| `components/popup-menu-studio.json`、`tooltip-studio.json`、`scroll-bar-studio.json` | Render Definition | Studio 分支的附属控件样式，通过 ancillaryStyles 绑定 |
| `components/artwork.json` | Render Definition | 可选封面氛围与实际封面的圆角裁切 |
| `components/playlist-grid.json`、`collection-card.json` | Render Definition | 集合网格列宽、间距、卡片几何和交互状态 |
| `components/surface-card.json` | Render Definition | 卡片 surface；对应 `render.schema.json` |
| `components/seek-slider.json` | Render Definition | 进度轨道和拖动柄；对应 `render.schema.json` |
| `components/volume-knob.json` | Render Definition | rotary volume 旋钮；对应 `render.schema.json` |
| `components/effect-chain.json` | Render Definition | 效果器行及处理声道选择的槽位布局 |
| `components/output-stage.json` | Render Definition | 多声道电平与 LUFS / True Peak 仪表 |
| `components/output-mixer.json` | Render Definition | 每声道增益、Mute、Solo 的槽位布局 |
| `layouts/studio.json` | Layout Definition | Studio 深色工作台结构；对应 `layout.schema.json` |
| `layouts/listening.json` | Layout Definition | Listening 亮色聆听结构；对应 `layout.schema.json` |
| `layouts/effects.json` | Layout Definition | VST3/AU 效果链管理结构；对应 `layout.schema.json` |
| `layouts/settings.json` | Layout Definition | 音频设备、只读端口映射、插件筛选与格式开关、皮肤库；对应 `layout.schema.json` |
| `layouts/library.json` | Layout Definition | 集合分类、搜索及封面网格 |
| `layouts/collection.json` | Layout Definition | 集合封面、说明、播放动作及曲目列表 |

本示例省略部分可选组件，包括 `settings.plugins.formats`、`settings.skins.preview`、`settings.skins.remove` 与 `settings.skins.reload`。其他组件及其行为见开发者指南的 Component Catalog。

Effects、Settings、Library 与 Collection 的基础 root 用于 Listening；`when.workspace: "studio"` 分支提供完整的 Studio 布局，并选择 `surface.studio`。Library 与 Collection 为新增可选页面，原有四个入口仍完整保留。示例要求应用 `0.7.0` 或更高。

Studio 与 Listening 使用 `library.collections` 切换全部歌曲、专辑、艺术家、收藏、命名歌单与播放队列，并用 `library.tracks` 展示当前集合。集合选择器支持歌单名称编辑；`library.newPlaylist` 和 `library.actions` 提供创建、批量编辑与 M3U/M3U8 交换入口。单击选择与当前播放标记相互独立。旧皮肤可继续使用 `queue.table`，每个工作区至少保留一个可见的曲库或队列表格。

Studio 的输出区域支持最多 16 声道仪表、LUFS / True Peak 读数与 Mixer。`output.mode` 在同一 stack 内切换 `output.stage` 和 `output.mixer`；Mixer 的增益、Mute、Solo 通过声明式 slots 绘制。

## 验证

从仓库根目录运行：

    ./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty/
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace studio --size 1040x700
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace effects --size 1040x700 --theme studio
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace library --size 1040x700 --theme studio
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace collection --size 1240x800 --theme listening
    ./build/MoonwakeSkinTool snapshot docs/skin-api/examples/CompleteThirdParty/ --workspace settings --size 1240x800 --theme listening --output /tmp/settings-listening.png
    ./build/MoonwakeSkinTool snapshot docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800 --output /tmp/complete-third-party.png

验证必须同时满足 JSON Schema、语义类型、required component、唯一 `focusOrder`、最小尺寸和资源预算。单个文件能通过 JSON 语法检查，不代表整个包有效。

## 扩展方式

本示例包按 [MIT License](LICENSE) 发布，可以复制和修改。基于示例创建自己的皮肤时，必须重新填写 `manifest.json` 的 `id`、`name`、`version`、`author`、`description`、`license`、`minAppVersion`、`capabilities` 和全部 `entrypoints`；不要把示例的作者或兼容性元数据误作自己作品的信息。新增 style 时，在 `components/index.json` 注册并确保 renderer 的 `target` 与该 style 的 target 相同。改变布局时只绑定开发者指南 Component Catalog 中的 ID；业务行为和可访问性名称仍由播放器提供。发布前完成开发者指南的发布检查清单。

Collection 的宽表通过 `cell.artist`、`cell.album` 独立展示艺术家与专辑；其他 table renderer 保留合并元数据列。
