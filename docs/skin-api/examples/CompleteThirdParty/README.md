# Complete Third-Party Skin

这是 Skin API 1 的“契约完整、可选项最小”可执行参考包。目录本身可以通过 Settings → Appearance / Skins → Developer Folder 载入；应用会自动验证并进入开发预览，随后可使用 Reload Folder 重载。它提供四个满足全部 required component 规则的布局，并继承 `builtin.foundation` 中未覆盖的 renderer。

完整接口见 [开发者指南](../../SKIN_DEVELOPER_GUIDE.md)，机器可读约束见 [JSON Schema](../../schema/)。

## 文件与模块

| 文件 | 公开模块 | 作用 |
|---|---|---|
| `manifest.json` | Manifest | 声明身份、版本、capability 和四个入口；对应 `manifest.schema.json` |
| `tokens.json` | Tokens | 定义本示例使用的颜色、尺寸、字体、渐变和动画时长；对应 `tokens.schema.json` |
| `components/index.json` | Renderer Index | 把稳定 target/style 名映射到 Render Definition；对应 `component-index.schema.json` |
| `components/button-primary.json` | Render Definition | 主操作按钮及状态动画；对应 `render.schema.json` |
| `components/button-neutral.json` | Render Definition | 中性按钮；对应 `render.schema.json` |
| `components/surface-card.json` | Render Definition | 卡片 surface；对应 `render.schema.json` |
| `components/seek-slider.json` | Render Definition | 进度轨道和拖动柄；对应 `render.schema.json` |
| `components/volume-knob.json` | Render Definition | rotary volume 旋钮；对应 `render.schema.json` |
| `layouts/studio.json` | Layout Definition | Studio 深色工作台结构；对应 `layout.schema.json` |
| `layouts/listening.json` | Layout Definition | Listening 亮色聆听结构；对应 `layout.schema.json` |
| `layouts/effects.json` | Layout Definition | VST3/AU 效果链管理结构；对应 `layout.schema.json` |
| `layouts/settings.json` | Layout Definition | Audio、只读多声道端口映射、Plug-ins、Skins 的 required component 最小结构；对应 `layout.schema.json` |

本示例有意省略可选的标题、摘要、列表和辅助操作，例如 `settings.plugins.formats`、`settings.plugins.knownList`、`settings.skins.preview`、`settings.skins.remove` 与 `settings.skins.reload`。要构建完整设置体验，请按开发者指南的 Component Catalog 选择这些可选 ID；不要把本示例的最小布局误认为唯一推荐的信息架构。

## 验证

从仓库根目录运行：

    ./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty/
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace studio --size 1040x700
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800
    ./build/MoonwakeSkinTool snapshot docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800 --output /tmp/complete-third-party.png

验证必须同时满足 JSON Schema、语义类型、required component、唯一 `focusOrder`、最小尺寸和资源预算。单个文件能通过 JSON 语法检查，不代表整个包有效。

## 扩展方式

本示例包按 [MIT License](LICENSE) 发布，可以复制和修改。基于示例创建自己的皮肤时，必须重新填写 `manifest.json` 的 `id`、`name`、`version`、`author`、`description`、`license`、`minAppVersion`、`capabilities` 和全部 `entrypoints`；不要把示例的作者或兼容性元数据误作自己作品的信息。新增 style 时，在 `components/index.json` 注册并确保 renderer 的 `target` 与该 style 的 target 相同。改变布局时只绑定开发者指南 Component Catalog 中的 ID；业务行为和可访问性名称仍由播放器提供。发布前完成开发者指南的发布检查清单。
