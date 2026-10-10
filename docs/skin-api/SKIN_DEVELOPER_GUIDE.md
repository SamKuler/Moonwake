# Moonwake Skin API 1 开发者指南

- 接口版本：Skin API 1
- 文档状态：开发期稳定契约；公开发布后在 Skin API 1 内保持兼容
- 面向对象：第三方皮肤作者、设计工具作者和自动化构建工具作者
- 文档位置：仓库公开文档 `docs/skin-api/`
- 版本基线：应用 0.9.0、内置皮肤 1.0.0

## 1. Skin API 能做什么

Skin API 允许皮肤包重新定义：

- Studio、Listening、Effects 和 Settings 四个基础响应式布局，以及可选的 Library、Collection、Immersive 和 Mini 页面。
- 应用背景、面板、按钮、开关、下拉框、滑杆、旋钮、列表、仪表和装饰图层。
- normal、selected、hover、pressed、focused、disabled 和 dragging 状态。
- 字体、SVG、图片、渐变、阴影、圆角和状态动画。
- 播放器公开组件的位置、尺寸、层级和键盘焦点顺序。

Skin API 不允许皮肤：

- 执行 JavaScript、Lua、原生代码或 shell。
- 访问网络、任意用户文件、进程、音频缓冲或插件实例；包内资源只能由宿主在验证后读取。
- 改变 Play、Add Music、Scan Plug-ins 等操作的业务含义。
- 注册新的业务事件或调用未公开的播放器方法。

皮肤是一个声明式视图。播放器拥有数据、音频处理、操作行为、键盘语义和可访问性名称；皮肤定义公开组件的布局、绘制和 `focusOrder`。复合组件的行与槽位几何同时用于绘制、原生点击区域、滚动和拖放。

macOS 文件选择器、系统窗口管理按钮和第三方插件编辑器由各自宿主管理，不受 Skin API 控制。

一个皮肤包拥有 Studio、Listening、Effects 和 Settings 的完整体验。播放器在工作区之间切换时不会替换 active skin；皮肤作者通过不同布局入口引用不同 style，例如 Studio 使用 textButton.console，Listening 使用 textButton.lightPrimary。

## 2. 快速开始

创建目录：

    MyFirstSkin/
    ├── manifest.json
    ├── tokens.json
    ├── components/
    │   ├── index.json
    │   └── primary-button.json
    └── layouts/
        ├── studio.json
        ├── listening.json
        ├── effects.json
        └── settings.json

最小 manifest.json：

~~~json
{
  "skinApi": 1,
  "id": "com.example.my-first-skin",
  "name": "My First Skin",
  "version": "1.0.0",
  "author": {
    "name": "Example Studio"
  },
  "minAppVersion": "0.7.0",
  "extends": "builtin.foundation",
  "capabilities": ["tokens", "renderers", "layouts"],
  "entrypoints": {
    "tokens": "tokens.json",
    "components": "components/index.json",
    "layouts": {
      "studio": "layouts/studio.json",
      "listening": "layouts/listening.json",
      "effects": "layouts/effects.json",
      "settings": "layouts/settings.json"
    }
  }
}
~~~

开发时从内置皮肤的 Settings → Appearance → Developer folder 选择该目录。应用会验证并进入开发预览；已选择目录的入口显示为 Reload folder，对应可选组件 `settings.skins.reload`。独立验证使用 `MoonwakeSkinTool validate`。开发目录不能直接 Apply，发布前应先用 Skin Tool 打包为 `MyFirstSkin.moonwake-skin`，再通过 Import skin、Preview 和 Apply 导入并应用。

schema/ 下的五个 JSON Schema 由播放器 SkinSchemaRegistry 通过 MoonwakeSkinTool schema 生成，是机器可读字段、类型和枚举的真源；本文解释其语义。发布验收会重新生成 Schema、验证本文全部 fenced JSON 与完整示例包，禁止手工维护两套字段集合。

### 2.1 可直接运行的完整示例

`docs/skin-api/examples/CompleteThirdParty/` 是可直接加载的 Skin API 1 参考包，包含四个基础布局、曲库总览和集合详情、tokens、renderer index，以及按钮、封面、歌单网格、效果链和输出仪表等 renderer。示例满足全部 required component 规则，并选用曲库集合、歌单编辑、插件筛选、格式开关、输出 Mixer 等可选组件；完整目录见第 10 节 Component Catalog。从仓库根目录执行：

    ./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty/
    ./build/MoonwakeSkinTool layout docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800
    ./build/MoonwakeSkinTool snapshot docs/skin-api/examples/CompleteThirdParty/ --workspace listening --size 1240x800 --output /tmp/complete-third-party.png

快照默认使用 representative 状态：当前 workspace 按钮为 selected，首个可聚焦控件为 focused。视觉验收可附加 `--state normal|hover|pressed|selected|focused|disabled`，把同一状态注入全部公开组件 renderer；这可生成可比较的状态矩阵，同时仍经过生产 SkinRenderer，且不会启动音频设备。

Skin Tool 的 verify-docs 命令会解析本文每个 json fenced block；对带 `skin-doc-test` 标记的完整文档执行对应 Schema 和语义校验，并用同一编译器验证完整示例：

    ./build/MoonwakeSkinTool verify-docs --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md --example docs/skin-api/examples/CompleteThirdParty/

示例目录中的 README.md 说明每个文件和公开模块的对应关系。若本文片段、Schema 与完整示例不一致，以 SkinSchemaRegistry 生成的 Schema 和稳定语义章节为准，并将该不一致作为文档缺陷修复。

## 3. 发布包和路径规则

### 3.1 两种输入形式

- 开发目录：未压缩目录，允许手动 Reload。
- 发布包：扩展名为 .moonwake-skin 的 ZIP 文件。

两种形式的根目录结构完全相同。

### 3.2 路径

- JSON 中一律使用相对于包根目录的正斜杠路径。
- 路径区分大小写，即使当前文件系统不区分。
- 不允许绝对路径、反斜杠、..、符号链接或包外资源。
- 不允许空 segment、"." segment、NUL、加密 ZIP 或多卷 ZIP。
- 单条路径最长 512 UTF-8 bytes，单个 segment 最长 255 bytes，归档最多 2048 entries。
- 所有路径必须使用 Unicode NFC 规范化。
- NFC 后重复、以及 Unicode case-fold 后重复的路径会被拒绝，避免在默认 macOS 文件系统上覆盖。
- assets/ 之外的资源引用会被拒绝。

有效：

    assets/icons/play.svg

无效：

    ../shared/play.svg
    /Users/me/play.svg
    assets\icons\play.svg

### 3.3 固定资源限制

| 项目 | 限制 |
|---|---:|
| .moonwake-skin 压缩大小 | 25 MiB |
| 解压后总大小 | 100 MiB |
| ZIP entry 数 | 2048 |
| 路径 / segment | 512 / 255 UTF-8 bytes |
| 单个 JSON | 2 MiB |
| 单张位图 | 4096 × 4096 |
| 单张位图解码内存 | 64 MiB |
| 全包位图总像素 | 32,000,000 |
| 全包图片解码缓存 | 128 MiB |
| 资产文件总数 | 512 |
| 单个字体 | 12 MiB |
| 布局和声明绘制节点总数 | 5000 |
| 单个 Render Program 最坏展开绘制操作 | 5000 |
| 最大嵌套深度 | 32 |
| 单个 SVG XML 元素 | 10,000 |
| 全包 SVG XML 元素 | 100,000 |
| 全包 SVG path command | 250,000 |
| 单个 Render Program 阴影层 | 8 |
| 单帧阴影离屏像素预算 | 16,000,000 |
| 单条表达式词法单元 | 256 |
| 同时活动动画 | 128 |
| 单段动画时长 | 10000 ms |

SVG 解析器禁用 DTD 与 entity。允许元素仅为 svg、g、path、rect、circle、ellipse、line、polyline、polygon、defs、linearGradient、radialGradient、stop、clipPath；允许属性仅为对应几何属性、fill、stroke、stroke-width、stroke-linecap、stroke-linejoin、opacity、transform、gradientUnits、gradientTransform、stop-color、stop-opacity、id 和 clip-path。url() 只能引用同文件 #id。script、style、事件属性、image、foreignObject、外链与远程资源一律拒绝。

## 4. manifest.json

### 4.1 完整示例

~~~json
{
  "skinApi": 1,
  "id": "io.meridian.warm-hifi",
  "name": "Warm Hi-Fi",
  "version": "1.2.0",
  "author": {
    "name": "Meridian Audio Design",
    "url": "https://example.invalid"
  },
  "description": "A warm listening-room skin.",
  "license": "MIT",
  "minAppVersion": "0.7.0",
  "extends": "builtin.foundation",
  "capabilities": ["tokens", "renderers", "layouts", "assets", "animations"],
  "entrypoints": {
    "tokens": "tokens.json",
    "components": "components/index.json",
    "layouts": {
      "studio": "layouts/studio.json",
      "listening": "layouts/listening.json",
      "effects": "layouts/effects.json",
      "settings": "layouts/settings.json"
    }
  }
}
~~~

### 4.2 字段

| 字段 | 类型 | 必需 | 规则 |
|---|---|---|---|
| skinApi | integer | 是 | Skin API 1 固定为 1 |
| id | string | 是 | 反向域名，小写 ASCII，最长 128 |
| name | string | 是 | 1 至 80 个 Unicode 字符 |
| version | string | 是 | SemVer，不允许省略 patch |
| author.name | string | 是 | 1 至 80 个字符 |
| author.url | string | 否 | http 或 https URL，仅作为文字展示，不由应用打开或加载 |
| description | string | 否 | 最长 500 个字符 |
| license | string | 否 | SPDX ID 或自定义短名称 |
| minAppVersion | string | 是 | SemVer |
| extends | string | 否 | Skin API 1 只允许 builtin.foundation |
| capabilities | string[] | 是 | 去重、按字典序不是必需 |
| entrypoints.tokens | path | 否 | 省略时继承基础 tokens |
| entrypoints.components | path | 否 | 省略时继承基础 renderers |
| entrypoints.layouts | object | 否 | 四个基础入口可继承 Foundation；library、collection、immersive、mini 为可选入口 |

ID 必须匹配：

    ^[a-z0-9]+([.-][a-z0-9]+)+$

### 4.3 Capabilities

Skin API 1 已知 capability：

- tokens
- renderers
- layouts
- assets
- animations

manifest 声明了某项 capability，但未提供相应入口或内容时产生错误。播放器不认识的 capability 产生 COMPAT003 错误。

## 5. Tokens

### 5.1 文件结构

~~~json
{
  "colors": {
    "app.background": "#CBC4B8",
    "surface.primary": "#EEE9E0",
    "surface.secondary": "#E3DDD3",
    "control.default": "#D8D0C4",
    "control.active": "#D69762",
    "text.primary": "#292A28",
    "text.secondary": "#5E5A53",
    "focus": "#A85F38",
    "danger": "#B75445"
  },
  "metrics": {
    "radius.surface": 12,
    "radius.control": 7,
    "stroke.default": 1,
    "space.xs": 4,
    "space.sm": 8,
    "space.md": 12,
    "space.lg": 20
  },
  "durations": {
    "control.fast": 90,
    "control.normal": 150
  },
  "fonts": {
    "display": {
      "families": ["Avenir Next", "sans-serif"],
      "weight": 700
    },
    "body": {
      "families": ["Avenir Next", "sans-serif"],
      "weight": 400
    },
    "data": {
      "families": ["SF Mono", "monospace"],
      "weight": 400
    }
  },
  "gradients": {
    "knob.metal": {
      "type": "linear",
      "angle": 135,
      "stops": [
        { "position": 0, "color": "#F0EADF" },
        { "position": 0.52, "color": "#B9B3A8" },
        { "position": 1, "color": "#77736D" }
      ]
    }
  },
  "strings": {
    "appearance": "light"
  }
}
~~~

### 5.2 Token 类型

| 分组 | 值类型 |
|---|---|
| colors | #RRGGBB 或 #RRGGBBAA，alpha 在末尾 |
| metrics | 有限 JSON number，逻辑像素 |
| durations | 0 至 10000 的毫秒整数 |
| fonts | Font Object |
| gradients | Gradient Object |
| strings | 最长 256 字符的字符串 |

所有数字必须为有限值。NaN、Infinity 和科学计数法溢出会被拒绝。

### 5.3 引用

任何支持 token 的属性都可以使用：

~~~json
{ "$ref": "colors.control.active" }
~~~

引用目标必须存在且类型兼容。引用在导入时解析，循环引用产生 TOKEN004。

皮肤可以覆盖 builtin.foundation 的同名 token，也可以定义自有 token。第三方自有 token 应以 vendor. 前缀组织，例如 colors.vendor.meridian.recordRed。

### 5.4 字体

系统字体：

~~~json
{
  "families": ["Avenir Next", "Helvetica Neue", "sans-serif"],
  "weight": 600,
  "italic": false,
  "tracking": 0
}
~~~

包内字体：

~~~json
{
  "source": "assets/fonts/ExampleSans-Regular.ttf",
  "families": ["Example Sans"],
  "weight": 400
}
~~~

如果 source 字体成功加载，该文件中的具体 typeface 优先；如果未声明 source 或加载失败，按 `families` 顺序选择第一个在当前平台可用的字体族，再匹配该字体族的字重与斜体样式。未安装的字体族会被跳过，例如 `SF Mono` 不可用时，`["SF Mono", "Menlo", "monospace"]` 会继续选择可用的 `Menlo`；后续字体族仍可用于缺失字形回退。`sans-serif`、`serif` 和 `monospace` 是三个大小写不敏感的精确逻辑别名，分别映射到 JUCE 在当前平台上的默认无衬线、衬线和等宽字体。它们不是名为这些字符串的物理字体，也不会进行前缀或模糊匹配。

`weight` 是数值字重请求，不是简单的 regular/bold 开关。播放器读取候选系统字体的真实 style 名称，把常见名称归一为下列权重，并选择与请求值距离最近且斜体状态一致的样式：

| style 名称类别 | 权重 |
|---|---:|
| Thin、Hairline | 100 |
| Ultra Light、Extra Light | 200 |
| Light | 300 |
| Regular、Book、Roman、Plain | 400 |
| Medium | 500 |
| Semi Bold、Demi Bold | 600 |
| Bold | 700 |
| Extra Bold、Ultra Bold | 800 |
| Black、Heavy | 900 |

斜体不匹配的代价高于任何字重距离，因此 `500 + italic` 会优先选择 `Medium Italic`，而不是非斜体 Medium。若字体没有相同斜体状态，才使用最接近的可用样式。播放器在皮肤资源缓存创建时解析字体族、style 和 typeface；绘制阶段只查询已解析条目，不重复枚举系统字体。包内字体与系统 fallback 仍受每个 Skin Session 的资源命名空间和包字体隔离规则约束。皮肤作者必须拥有分发字体的许可。

## 6. 值、上下文和表达式

### 6.1 三类值

字面量：

~~~json
{ "fill": "#D69762" }
~~~

Token 引用：

~~~json
{ "fill": { "$ref": "colors.control.active" } }
~~~

上下文引用：

~~~json
{ "text": { "$context": "text.primary" } }
~~~

表达式：

~~~json
{ "radius": { "$expr": "min(bounds.width, bounds.height) * 0.5" } }
~~~

包含 $ref、$context 或 $expr 的对象不能包含其他字段。

### 6.2 表达式语法

允许：

- 有限数字、true、false。
- 使用单引号的 UTF-8 字符串；允许反斜杠、单引号、换行和制表符转义，不允许插值。
- 使用 color('#RRGGBB') 或 color('#RRGGBBAA') 构造 color。
- 小括号。
- +、-、*、/。
- ==、!=、<、<=、>、>=。
- and、or、not。
- min(number...)、max(number...)、clamp(number, number, number)、abs(number)、round(number)、lerp(number, number, number)。
- alpha(color, number) 和 mix(color, color, number)，比例 clamp 到 0 至 1，mix 使用预乘 alpha 的 linear-sRGB 插值。
- token.<group>.<name> token 引用。
- 由 renderer target 声明的白名单上下文路径。

不允许：

- 赋值。
- 数组或对象构造。
- 自定义函数。
- 循环、递归。
- 动态字段访问。
- 字符串拼接。
- 文件、时间、随机数或网络。

静态类型只有 number、boolean、string 和 color，没有隐式转换。+、-、*、/ 与有序比较只接受 number；== 和 != 要求两侧同类型；and、or、not 只接受 boolean，and 与 or 短路。

优先级从高到低：

1. 函数、token/context 引用和小括号。
2. unary - 与 not。
3. * 与 /。
4. + 与 -。
5. ==、!=、<、<=、>、>=。
6. and。
7. or。

除法分母为零返回 number 0，并在当前 Skin Session 记录一次 EXPR021 warning。其他非有限结果记录 EXPR020，使用该属性在 renderer target 中定义的默认值。token 在导入时解析为带类型常量；token 类型与表达式位置不兼容时导入失败。

### 6.3 通用 Render Context

所有 renderer 可以读取：

| 路径 | 类型 | 含义 |
|---|---|---|
| bounds.x | number | 本地绘制区左侧，通常为 0 |
| bounds.y | number | 本地绘制区顶部，通常为 0 |
| bounds.width | number | 绘制宽度 |
| bounds.height | number | 绘制高度 |
| contentBounds.* | number | 扣除组件内容边距后的区域 |
| scaleFactor | number | 显示缩放 |
| state.enabled | boolean | 控件是否可用 |
| state.hover | boolean | 指针是否悬停 |
| state.pressed | boolean | 是否按下 |
| state.focused | boolean | 是否需要显示键盘焦点反馈；鼠标点击取得的焦点不启用该状态，Tab 导航启用 |
| state.selected | boolean | 按钮、开关或行是否选中 |
| state.dragging | boolean | 是否拖动 |
| value.normalized | number | 0 至 1 的归一化值 |
| value.minimum | number | 控件最小值 |
| value.maximum | number | 控件最大值 |
| value.display | string | 应用生成的显示值 |
| text.primary | string | 主文本 |
| text.secondary | string | 次文本 |
| text.align | string | 应用提供的语义文字对齐；九个值见下文 |
| icon.name | string | 应用提供的语义图标名 |
| icon.path | string | 应用为该语义名提供的 24 × 24 fallback SVG path；只对 textButton 及其 icon slot 开放 |

未被某个组件提供的路径不能引用；编译器根据 renderer target 检查。

`text.align` 的可能值为 `topLeft`、`top`、`topRight`、`left`、`center`、`right`、`bottomLeft`、`bottom`、`bottomRight`。Text layer 可以写：

~~~json
{
  "align": { "$context": "text.align" }
}
~~~

这是对齐可随组件语义变化的唯一动态形式；Skin API 1 的 `align` Schema 不接受 `$expr`。Renderer 若写字面量，例如 `"align": "center"`，该字面量就是有意覆盖，不读取组件的 `text.align`。

## 7. Renderer Index

components/index.json 将 style 名映射到 renderer 文件：

~~~json
{
  "textButton": {
    "primary": "components/primary-button.json",
    "neutral": "components/neutral-button.json",
    "danger": "components/danger-button.json",
    "effectSlotAction": "components/effect-slot-action.json",
    "effectSlotDanger": "components/effect-slot-danger.json"
  },
  "toggleButton": {
    "default": "components/toggle.json"
  },
  "comboBox": {
    "default": "components/combo.json"
  },
  "linearSlider": {
    "default": "components/linear-slider.json",
    "seek": "components/seek-slider.json"
  },
  "rotarySlider": {
    "default": "components/knob.json"
  },
  "progressBar": {
    "default": "components/progress.json"
  },
  "table": {
    "queue": {
      "renderer": "components/queue-table.json",
      "slots": {
        "header.surface": "surface.tableHeader",
        "header.text": "label.tableHeader",
        "row.surface": "surface.tableRow",
        "cell.title": "label.tableCell",
        "cell.location": "label.tableCellMuted",
        "cell.duration": "label.tableCellData"
      }
    }
  },
  "label": {
    "tableHeader": "components/table-header-label.json",
    "tableCell": "components/table-cell-label.json",
    "tableCellMuted": "components/table-cell-muted-label.json",
    "tableCellData": "components/table-cell-data-label.json",
    "effectSlotName": "components/effect-slot-name.json"
  },
  "surface": {
    "card": "components/card.json",
    "tableHeader": "components/table-header-surface.json",
    "tableRow": "components/table-row-surface.json",
    "effectSlot": "components/effect-slot-surface.json",
    "dragIndicator": "components/drag-indicator.json"
  },
  "outputStage": {
    "default": "components/output-stage.json"
  },
  "effectChain": {
    "default": {
      "renderer": "components/effect-chain.json",
      "slots": {
        "slot.surface": "surface.effectSlot",
        "slot.name": "label.effectSlotName",
        "slot.bypass": "textButton.effectSlotAction",
        "slot.editor": "textButton.effectSlotAction",
        "slot.remove": "textButton.effectSlotDanger",
        "slot.dragIndicator": "surface.dragIndicator"
      }
    }
  }
}
~~~

Style 的完整名称为 target.variant，例如 textButton.primary。布局绑定的 style target 必须与组件描述符要求的 target 相容。

简单 style entry 是 renderer 文件路径。复合组件使用对象 entry：renderer 绘制组件整体，slots 把组件内部稳定的调用点绑定到其他 style。slot 名由 target/Component Catalog 固定，不能自定义；style 引用的 target 必须与该 slot 的类型一致。具有语义图标的 textButton 额外公开 `icon.<semantic>` surface slot；只有 style 显式绑定该 slot 时才绘制图标，旧皮肤因此保持原样。

复合组件中逐行或逐槽变化的专属 Context 只在对应 slot 调用时有效，并会继承到该 slot 绑定的子 renderer；例如 pluginList 的 `plugin.*` 可用于 `row.surface` 绑定的 surface renderer 和 `row.name` 绑定的 label renderer。复合组件自身的主体 renderer 每个组件只调用一次，因此不能引用逐行字段。编译器分别校验主体与 slot 上下文，避免导入成功后才出现缺值。

### 7.1 复合组件的行布局（应用 0.5.0）

### 列表内容裁切

应用 0.6.1 起，table、channelMap、pluginList、skinList renderer 可声明顶层 `"contentClip": "body"`；应用 0.8.0 新增的 directoryList 同样支持该字段。它引用该 renderer 根层的 rect、roundedRect 或 ellipse。原生列表的内容、表头和滚动条按该形状的描边内缘裁切；外层背景与焦点反馈保持完整。几何随 frame、radius、transform 和状态 variants 解析。被引用层的 visible/opacity 不影响裁切形状，因此可使用透明专用层。

未声明 contentClip 的皮肤保留矩形内容区域。引用未知层、嵌套层或不支持的形状／target 会产生 RENDER004。声明该字段的包将 minAppVersion 设为 0.6.1 或更高；使用 directoryList 时须为 0.8.0 或更高。

`table`、`effectChain`、`channelMap`、`channelMixer`、`pluginList`、`skinList`、`playlistGrid`、`directoryList`、`smartPlaylistEditor`、`smartPlaylistRuleList`、`outputCalibration` 和 `popupMenu` renderer 可声明顶层 `rowLayout`，为行高、行间距、表头高度和内部 slot 提供统一几何。应用将同一份解析结果用于原生控件边界、绘制、滚动、拖放索引和焦点可见性。颜色、字体、图形与状态绘制继续由 renderer layers 和 slot styles 定义。

下例为 `pluginList` 的行布局：

```json
{
  "apiVersion": 1,
  "target": "pluginList",
  "layers": [],
  "rowLayout": {
    "rowHeight": 52,
    "rowGap": 8,
    "headerHeight": 0,
    "slots": {
      "row.name": {
        "x": 16,
        "y": 0,
        "width": { "$expr": "bounds.width - 32" },
        "height": { "$context": "layout.rowHeight" }
      }
    },
    "breakpoints": [
      { "when": { "maxWidth": 400 }, "rowHeight": 80 }
    ]
  }
}
```

`rowHeight` 和 `slots` 必需。`effectChain` 和 `channelMixer` 在合并当前断点后必须包含其目录定义的全部原生交互 slot；`playlistGrid` 必须放置全部五个卡片 slot；`smartPlaylistEditor`、`smartPlaylistRuleList` 和 `outputCalibration` 必须放置其目录定义的全部公开 slot；其余 target 的 `slots` 可为空对象。`rowGap`、`headerHeight` 缺省为 `0`。数值接受 number、`$ref`、`$context`、`$expr`，与绘制属性共用类型检查、受限表达式和继承 token 重绑定。`rowHeight` 至少为 `1`，间距、表头高度和 slot 宽高不得为负；这些度量及 slot 坐标沿用 Skin Layout 的 100000 逻辑像素范围。slot 名称必须属于该 target 的 Component Catalog；原生交互 slot 同时遵循对应控件的目录最小尺寸，换算后的原生控件矩形必须完整位于行内，避免裁切后无法操作。声明 `header.label`、`empty.message` 等公开 slot 时，坐标相对于它们各自的宿主绘制区域。

度量解析时，`bounds`、`contentBounds` 来自整个组件；slot 矩形解析时，两者为从 `(0, 0)` 开始、宽度等于组件宽度、高度等于 `rowHeight` 的行矩形。`container.width`、`container.height` 表示传入的可用容器尺寸，供空态和表头布局引用。slot 表达式另可读取 `layout.rowHeight`、`layout.rowGap`、`layout.headerHeight`。所有行布局可读取 `table.rowCount`、`table.columnCount`；table 的宿主额外提供 `table.presentation`（`table` 或 `list`）。逐项业务字段仍只用于相应 slot 的绘制 context。

`breakpoints` 最多 16 项；`when` 接受 `minWidth`、`maxWidth`，端点包含在内，至少声明一项。断点按声明顺序处理，匹配项覆写所声明的度量和 slot；未声明的值保留，slot 按 ID 合并。宽度为扣除可见滚动条后可用容器的局部宽度，单位与 Skin Layout 一致。布局几何不参与 renderer 状态动画。

未声明 `rowLayout` 的既有 Skin API 1 renderer 继续有效，应用可采用 Foundation 的行布局。应用 0.7.0 新增的 `playlistGrid` 必须声明 `rowLayout` 和 `controlStyle`，并放置全部五个卡片 slot。应用 0.8.0 新增的 `directoryList`、`smartPlaylistEditor` 和 `smartPlaylistRuleList` 必须声明 `rowLayout`；两个智能歌单 target 还须放置全部公开 slot。无效尺寸、未知或缺少必需 slot、交互区域过小或越出行边界时报告 `RENDER004`；context 和表达式结果类型错误沿用 `RENDER005`，token 类型错误沿用 `RENDER006`，表达式诊断沿用 `EXPR` 错误码。应用 0.9.0 新增的 `outputCalibration` 也必须声明 `rowLayout` 并放置全部公开 slot。Foundation 为原有复合组件提供完整布局，使旧皮肤也有可用的几何回退。

### 7.2 Renderer 调用粒度与专属 Context

| target | 调用粒度 | 额外 Context | 固定 slots |
|---|---|---|---|
| label | 每个 Label 一次 | text.primary、text.secondary、text.align | 无 |
| textButton | 每个按钮主体一次；显式绑定时再调用 icon slot | text.primary、text.align、icon.name、icon.path、state.* | 该组件声明的 icon.&lt;semantic&gt; surface slot；playPause 同时声明 icon.play 与 icon.pause |
| toggleButton | 每个开关一次 | text.primary、text.align、state.selected | 无 |
| comboBox | 主体一次；弹出菜单使用当前布局解析出的 popupMenu 样式 | text.primary、text.align、state.*、combo.popupOpen、combo.hasSelection | 无 |
| textEditor | 主体一次 | text.primary、text.align、state.focused | 无 |
| linearSlider | 每帧一次 | value.*、state.dragging | 无 |
| rotarySlider | 每帧一次 | value.*、state.dragging | 无 |
| progressBar | 每帧一次 | value.normalized、value.display | 无 |
| scrollBar | 每个原生滚动条一次 | scroll.orientation、scroll.thumbStart、scroll.thumbSize、scroll.part、state.* | 无 |
| popupMenu | 背景、item、separator 或 sectionHeader 各一次 | menu.part、menu.itemIndex、text.primary、text.secondary、text.align、icon.name、state.enabled、state.selected | 无 |
| tooltip | 每个原生 tooltip 一次 | text.primary、text.align | 无 |
| surface | 每个 container 或 visual 节点一次 | bounds.* | 无 |
| table | 表主体一次 | table.rowCount、table.columnCount | header.surface、header.text、row.surface、cell.title、cell.location、cell.duration、cell.artist、cell.album |
| artwork | 封面组件一次 | artwork.hasTrack、artwork.hasImage、artwork.aspectRatio | frame.background、frame.overlay |
| effectChain | 链主体一次；每个动态槽分别调用 slots | 主体：chain.slotCount；slots：chain.slotCount、slot.index、slot.bypassed、slot.hasError、slot.name | slot.surface、slot.name、slot.bypass、slot.editor、slot.remove、slot.dragIndicator；可选 slot.scope（应用 0.4.0 起） |
| outputStage | 输出级每帧一次 | text.primary（曲目标题）、text.secondary（当前设备名）、meter.*、output.* | 无 |
| channelMap | 输出端口映射 | 绘制行 slots：channel.index、channel.sourceLabel、channel.deviceLabel、channel.connected；row.output：标准 comboBox context | row.surface、row.source、row.destination、row.output（routing 的 comboBox）、header.label、empty.message |
| outputCalibration | 选中扬声器的校准表单与测试信号控制 | 标准 state.*；子控件提供 text.* 和控件 context | calibration.channelLabel、calibration.channel、calibration.trimLabel、calibration.trim、calibration.delayLabel、calibration.delay、calibration.polarity、calibration.signalLabel、calibration.signal、calibration.levelLabel、calibration.level、calibration.frequencyLabel、calibration.frequency、calibration.startStop、calibration.status |
| channelMixer | 每声道混音行及原生控制槽位 | text.primary、value.*、state.* | row.surface、row.source、row.destination、row.peak、row.gain、row.mute、row.solo |
| pluginList | 主体一次；每个可见插件调用 slots | slots：plugin.index、plugin.name、plugin.manufacturer、plugin.format、plugin.enabled | row.surface、row.name、row.manufacturer、row.format、row.status、header.label、empty.message |
| skinList | 主体一次；每个可见皮肤分别调用 slots | 行 slots：skin.index、skin.id、skin.name、skin.author、skin.version、skin.builtin、skin.active、skin.previewed | row.surface、row.name、row.author、row.version、row.status、empty.message |
| directoryList | 主体一次；每个可见目录分别调用行 slots；空列表调用 empty.message | 行 slots：directory.index、directory.id、directory.name、directory.path、directory.automatic | row.surface、row.name、row.path、row.status、empty.message |
| smartPlaylistEditor | 覆盖层主体一次；内部控件使用对应 slot 样式 | 标准 state.*、text.* 和控件 context | editor.heading、editor.nameLabel、editor.name、editor.match、editor.rules、editor.add、editor.preview、editor.save、editor.cancel |
| smartPlaylistRuleList | 列表主体一次；每条规则的原生控件使用对应 slot 样式 | 标准 state.*、text.* 和控件 context | rule.field、rule.operator、rule.value、rule.boolean、rule.remove |

复合 entry 与简单 entry 使用同一规范查找顺序：当前皮肤精确 style → 父皮肤同名精确 style → 当前皮肤 target.default → 父皮肤 target.default → builtin.foundation target.default。component 省略 style 时从当前皮肤 target.default 开始。某个 slot 未显式绑定时从该 slot 类型的当前皮肤 target.default 开始。Foundation 为原有 target 提供默认 renderer；`playlistGrid` 是应用 0.7.0 新增的可选 target，使用它的皮肤必须提供可解析的网格样式。未使用网格的旧皮肤无需增加 renderer 或页面。

scrollBar、popupMenu 和 tooltip 是 JuceSkinAdapter 使用的 ancillary target，不对应可放进 layout 的 Component ID。它们与其他 renderer target 使用相同的命名样式规则，可以在 components/index.json 注册 `studio`、`listening` 等样式，并通过布局的 `ancillaryStyles` 选择。每个完整皮肤仍必须提供或继承 scrollBar.default、popupMenu.default 和 tooltip.default；builtin.foundation 提供三者，确保未指定映射的 Viewport、菜单和提示使用稳定回退。ComboBox 的自身 style 不传播给 popup；ComboBox 与其他来源的菜单均使用当前布局解析出的 popupMenu 样式。使用这些命名附属样式及布局映射的皮肤要求应用 `0.3.0` 或更高。

`combo.popupOpen` 表示原生弹出菜单正在显示，`combo.hasSelection` 表示有有效选项；两者要求应用 `0.5.0` 或更高。内置皮肤在菜单显示且有选项时隐藏被覆盖的触发器图层；无选项时菜单出现在触发器下方，触发器保持可见。菜单关闭后恢复触发器及焦点反馈。

scroll.orientation 为 `horizontal` 或 `vertical`；scroll.thumbStart 与 scroll.thumbSize 是 target 本地轴上的逻辑像素；scroll.part 为 `bar`、`buttonStart` 或 `buttonEnd`。`bar` 一次绘制轨道与 thumb，两个 button part 分别绘制可选箭头。menu.part 为 `background`、`item`、`separator` 或 `sectionHeader`；普通 item 的 menu.itemIndex 是当次菜单绘制中从 0 开始、跳过 separator 与 sectionHeader 的稳定视觉序号，非 item 调用固定为 -1。table 的 row.surface 接收整行 bounds，cell.* 分别接收其实际列 bounds；不要假定 cell slot 共享整行几何。tooltip 使用应用提供的 text.primary，皮肤不能替换其可访问性内容。

scrollBar 的 bounds 与 contentBounds 表示轨道绘制区域。滑块沿对应轴使用 scroll.thumbStart 与 scroll.thumbSize 定位，主轴尺寸与原生拖动命中区域一致；视觉留白放在交叉轴，thumbSize 为 0 时隐藏滑块。内置样式的 thickness 为 12、minThumbLength 为 24，水平与垂直滚动条遵循同一规则。

`combo.popupOpen` 表示菜单已打开，`combo.hasSelection` 表示控件存在选中值；两者不表示菜单在屏幕上是否覆盖触发控件。菜单位置会根据可用空间调整，内置皮肤在菜单打开期间保持按钮、文字与箭头可见。

原生 textEditor 的编辑内容、选区和光标由应用拥有。renderer 中 ID 为 `label` 的 text 层提供它们的 `font`、`fontSize`、`color`、`opacity`、`visible`、`frame` 和 `align`；这些值按当前状态 variants 和控件 bounds 解析。原生编辑区域采用该 frame 的控件坐标矩形及对齐方式，窗口改变尺寸或皮肤预览切换后会重新同步。`label` 应使用未变换的控件坐标；layer/group 的 transform 仍只影响声明式绘制，不改变原生编辑坐标。`maxLines`、`overflow` 不会更改应用决定的单行、多行、编辑或滚动行为。

textEditor 的占位文字仍由应用设置。空内容且未聚焦时，该文字通过 `text.primary` 交给同一 `label` 层绘制；已有输入或获得焦点时，主体 renderer 的 `text.primary` 为空，由原生文字层负责内容。应用关闭原生占位文字的第二次绘制，字号、颜色与目标矩形均遵循上述 label 配置。popupMenu 和 tooltip 的原生尺寸测量也使用其 `label` 字体及 frame 内边距，tooltip 的可用行数遵循 `maxLines`。

### 7.3 原生控件度量（应用 0.5.0）

renderer 可声明 `controlStyle`，为宿主的原生命中区域和附属窗口提供与绘制一致的度量。省略整个对象时继承 target.default；声明时须包含该 target 的完整字段，不能出现其他 target 的字段。

| target | controlStyle 字段 |
|---|---|
| textButton | iconBounds：x、y、width、height |
| linearSlider / rotarySlider | trackBounds：x、y、width、height；thumbRadius |
| tooltip | maxWidth（含文字内边距）、offsetX、offsetY |
| scrollBar | thickness、minThumbLength |
| textEditor | selectionColour、selectedTextColour、caretColour |
| playlistGrid | minItemWidth（至少 120）、itemGap（至少 0） |
| lyrics | padding、lineGap（至少 0）、followAnchor（0–1）、followDurationMs（0–2000） |

数值和颜色可以引用 token，几何字段可使用受限表达式。Slider 的 layers 以完整控件 bounds 绘制，trackBounds 描述原生输入映射使用的同一轨道；自定义滑块必须让两者一致。复合组件中原生子控件使用其 slot 映射出的 style。图标使用公开语义路径，尺寸和位置由 iconBounds 控制。Tooltip 会在父窗口边界内避让，滚动条 thickness 同时参与布局留白和原生滚动条宽度。TextEditor 的选区、选中文字和光标使用对应皮肤颜色。

`scrollBar.thickness` 是布局级的逻辑像素度量，接受数值、token 引用和只依赖 token 的表达式，不读取控件 bounds、状态或显示器缩放 context。不同页面和断点可通过 `ancillaryStyles` 选择不同宽度，保证页面预留空间与所有原生滚动条使用相同值。

## 8. Render Definition

### 8.1 按钮示例

~~~json
{
  "apiVersion": 1,
  "target": "textButton",
  "layers": [
    {
      "id": "body",
      "type": "roundedRect",
      "frame": {
        "anchor": "bounds",
        "inset": 0.5
      },
      "radius": { "$ref": "metrics.radius.control" },
      "fill": { "$ref": "colors.control.default" },
      "stroke": {
        "color": "#00000018",
        "width": 1
      }
    },
    {
      "id": "focus",
      "type": "roundedRect",
      "visible": { "$context": "state.focused" },
      "frame": {
        "anchor": "bounds",
        "inset": 1.5
      },
      "radius": { "$ref": "metrics.radius.control" },
      "fill": "#00000000",
      "stroke": {
        "color": { "$ref": "colors.focus" },
        "width": 2
      }
    },
    {
      "id": "label",
      "type": "text",
      "frame": {
        "anchor": "contentBounds"
      },
      "text": { "$context": "text.primary" },
      "font": { "$ref": "fonts.body" },
      "fontSize": 13,
      "color": { "$ref": "colors.text.primary" },
      "align": "center",
      "maxLines": 1,
      "overflow": "ellipsis"
    }
  ],
  "variants": {
    "selected": {
      "body.fill": { "$ref": "colors.control.active" }
    },
    "hover": {
      "body.opacity": 0.92
    },
    "pressed": {
      "body.opacity": 0.82,
      "label.transform.translateY": 1
    },
    "disabled": {
      "body.opacity": 0.42,
      "label.opacity": 0.55
    }
  },
  "transitions": {
    "hover": {
      "durationMs": { "$ref": "durations.control.fast" },
      "easing": "easeOut",
      "properties": ["body.opacity"]
    },
    "pressed": {
      "durationMs": 60,
      "easing": "easeOut",
      "properties": ["body.opacity", "label.transform.translateY"]
    }
  }
}
~~~

### 8.2 Layer 类型

| type | 主要属性 |
|---|---|
| rect | frame、fill、stroke |
| roundedRect | frame、radius、fill、stroke |
| ellipse | frame、fill、stroke |
| line | start、end、color、width、cap |
| arc | center、radiusX、radiusY、startAngle、endAngle、stroke |
| path | data、fill、stroke、transform |
| text | frame、text、font、fontSize、color、align、overflow |
| image | frame、source、fit、opacity、tint |
| svg | frame、source、fit、opacity、tint |
| group | children、transform、opacity、clip |
| repeat | source、limit、template |

所有角度使用度，0 度指向右侧，正值顺时针。颜色使用线性插值，但 JSON 输入仍为 sRGB 十六进制。

Text 的 `align` 支持九个字面枚举，也支持 `{ "$context": "text.align" }`。字面值始终优先表达皮肤作者的固定设计；只有显式绑定上下文时，组件目录的语义对齐才会生效。

Text 的 `maxLines` 是实际显示的最大行数，显式换行与自动换行都计入上限。`overflow: "clip"` 与 `"ellipsis"` 保持声明的 `fontSize` 和字宽；多行空间不足时减少显示行数，不自动缩字。clip 隐去超出内容，ellipsis 在最后可见行标记被省略的内容，包括被行数上限截断的后续段落。只有 `overflow: "shrink"` 可以缩小字号或横向压缩字形；其结果仍受 maxLines 和 frame 限制。Unicode 字形、组合字符、字体回退和双向文字交由 JUCE 排版。

path.data 使用 SVG 1.1 path-data 子集 M/m、L/l、H/h、V/v、C/c、S/s、Q/q、T/t、A/a、Z/z；只允许有限十进制数，不允许 CSS、变量或 URL。它可以直接写字符串，也可以用 `$ref` 引用 strings token。默认 fill rule 为 non-zero。单个 path 最多 65536 UTF-8 bytes，命令数计入全包 250000 path-command 预算。

transform 的 pivotX/pivotY 使用 layer 本地逻辑像素；省略时为 layer frame 中心。固定顺序为先围绕 pivot scale，再 rotation，最后 translate，即矩阵 translate × pivot × rotate × scale × inverse-pivot。rotation 使用度。

### 8.3 Frame

简写：

~~~json
{
  "anchor": "bounds",
  "inset": 2
}
~~~

完整形式：

~~~json
{
  "x": 4,
  "y": 4,
  "width": { "$expr": "bounds.width - 8" },
  "height": { "$expr": "bounds.height - 8" }
}
~~~

anchor 允许 bounds 或 contentBounds。inset 可以是一个数字或：

~~~json
{
  "top": 4,
  "right": 8,
  "bottom": 4,
  "left": 8
}
~~~

### 8.4 Fill、Stroke 和 Shadow

fill 可以是颜色字面量、color token 引用或 gradient token 引用。Skin API 1 不允许 inline Gradient Object；渐变必须在 tokens.json 的 gradients 中定义，以便预算、排序与缓存只执行一次。stroke：

~~~json
{
  "color": "#292A28",
  "width": 1.5,
  "placement": "inside"
}
~~~

placement 为 inside、center 或 outside。Layer 可附加 shadow：

~~~json
{
  "color": "#00000035",
  "radius": 10,
  "offsetX": 0,
  "offsetY": 4
}
~~~

渐变坐标相对当前 layer frame。linear 的 angle 0 度从左到右，90 度从上到下，渐变轴穿过 frame 中心并延伸到边框交点。radial 的 centerX/centerY 与 radius 都以 frame 为归一化坐标；center 为 0 至 1，radius 大于 0 且不超过 2，radius 1 表示较短边的一半。stops 必须按 position 非递减排列。

image/svg 的 source 可以直接写安全的 `assets/...` 路径，也可以用 `$ref` 引用 strings token；token 所声明包的资源命名空间会随编译结果保留，因此父子皮肤存在同路径资源时不会串包。font token 的 source 采用同一逐绑定归属规则。fit 为 contain、cover、fill 或 center。tint 省略时不改变资源；存在时把解码后的预乘 linear-sRGB RGBA 分量与 tint 相乘。SVG tint 在矢量栅格化后执行，不允许通过 CSS currentColor 获取外部状态。

### 8.5 Variant 合并

base layers 创建初始值。运行时按以下顺序应用匹配 variant：

1. selected
2. hover
3. pressed
4. dragging
5. focused
6. disabled

Variant 的键是 layer-id.property-path。引用不存在的 layer 或不可修改字段产生 RENDER012。

### 8.6 Repeat

repeat 只允许白名单数据源。输出仪表可以使用 meter.channels：

~~~json
{
  "id": "channels",
  "type": "repeat",
  "source": "meter.channels",
  "limit": 16,
  "template": {
    "id": "channel-bar",
    "type": "roundedRect",
    "frame": {
      "x": { "$expr": "item.index * 18" },
      "y": { "$expr": "bounds.height * (1 - clamp((item.peakDb + 60) / 60, 0, 1))" },
      "width": 12,
      "height": { "$expr": "bounds.height * clamp((item.peakDb + 60) / 60, 0, 1)" }
    },
    "radius": 3,
    "fill": { "$ref": "colors.control.active" }
  }
}
~~~

允许的数据源：

- meter.channels，最多 16（应用 0.4.0 起）。
- table.visibleColumns，最多 4；按实际可见列顺序提供 item.index 和 item.label，0.7.0 起支持第四列。
- output.channelLabels，最多 16（应用 0.4.0 起）。

repeat.template 不能包含 repeat，即使藏在 group 子树中也不允许。编译器按 source 的最大元素数静态展开计数；单个 Render Program 的最坏绘制操作超过 5000 时拒绝导入。Skin API 1 不允许皮肤创建任意数组。

### 8.7 默认值与运行时表达式失败

下表给出可选绘制属性的默认值，以及 required/optional 动态属性发生 EXPR020 时采用的回退值。表达式求值过程中任一 number 中间结果变成非有限值，会终止整个当前表达式并只替换当前属性，不跳过整个 renderer：

| 属性 | 默认 / EXPR020 回退 |
|---|---|
| frame x、y、width、height；point x、y | 0 |
| frame inset 及各边 | 0 |
| visible | true |
| opacity | 1 |
| transform translateX、translateY、rotation | 0 |
| transform scaleX、scaleY | 1 |
| transform pivotX、pivotY | 当前 layer frame 的中心 |
| fill；line/path/text color；stroke.color | transparent black `#00000000` |
| stroke.width；line.width | 0 |
| stroke.placement | center |
| stroke.cap；line.cap | butt |
| shadow.color | `#00000000` |
| shadow.radius、offsetX、offsetY | 0 |
| roundedRect.radius；arc radiusX、radiusY、startAngle、endAngle | 0 |
| fontSize | 12 |
| text.align | left |
| text.maxLines | 1 |
| text.overflow | clip |
| image/svg fit | contain |
| image/svg tint | opaque white `#FFFFFFFF` |
| clip | false |

必需字段在导入时仍必须存在；该表不会把缺失 required 字段变成合法。number 结果可以直接触发 EXPR020；boolean 或 color 结果的表达式如果内部包含非有限 number 子表达式，也可以触发 EXPR020，并使用该属性对应的 boolean 或 color 回退。string 不支持包含数字运算的字符串构造，font 与 fit 不接受 `$expr`，因此它们只有省略时默认值而没有可触发的 EXPR020 路径。静态类型不匹配直接拒绝导入。EXPR021 除零固定返回 number 0，因此不再应用本表的其他回退。

## 9. Layout Definition

### 9.1 文件结构

~~~json
{
  "apiVersion": 1,
  "root": {
    "id": "root",
    "type": "column",
    "children": []
  },
  "breakpoints": [
    {
      "when": {
        "maxWidth": 1099
      },
      "root": {
        "id": "root-compact",
        "type": "column",
        "children": []
      }
    }
  ]
}
~~~

系统按声明顺序选择第一个匹配 breakpoint；没有匹配时使用 root。每个 breakpoint 提供完整 root，不执行隐式树补丁，因此结构变化明确且可独立验证。

when 支持 minWidth、maxWidth、minHeight、maxHeight、minScaleFactor、maxScaleFactor，以及可选的 `workspace`。尺寸与 scale factor 的边界比较均为包含；所有已声明条件同时成立时才匹配。宽高使用逻辑像素，scale factor 只参与断点选择和最终绘制，不把布局数字重复放大。

`workspace` 仅接受 `studio` 或 `listening`，省略时匹配两个工作区。它可以独立作为条件，也可以与尺寸条件组合：

```json
{ "workspace": "studio", "maxWidth": 1099 }
```

Studio、Listening、Effects 和 Settings 是四个基础入口。应用 0.7.0 增加可选的 `library` 和 `collection` 入口，分别用于曲库总览和集合详情；Foundation 不提供这两个页面；播放器在皮肤同时提供或继承 `library` 与 `collection` 时启用分层曲库导航，否则保持原有工作区布局。单独的页面仍可在 Skin Tool 中预览。页面导航与 Studio／Listening 工作区独立，应用将当前工作区传给 Effects、Settings、Library 和 Collection，因此它们可以通过完整 breakpoint root 选择相应样式与结构。未显式提供工作区的解析器调用和 Skin Tool 预览，从 Studio 入口推导 `studio`；其余入口默认 `listening`。没有 `workspace` 条件的既有皮肤保持原来的匹配行为，未匹配时仍使用 root。

不可达检查也包含工作区：相同尺寸下的 Studio 和 Listening 分支可以并列；若某分支在它匹配的每个工作区中，尺寸与 scale factor 范围都被先前分支完整覆盖，则报 `LAYOUT010`。不同分支部分重叠仍遵循声明顺序。`workspace` 的未知枚举值报 `SCHEMA003`，字段类型和其他 Schema 限制沿用现有错误码。使用工作区条件的皮肤必须设置 `minAppVersion` 为 `0.3.0` 或更高。

共享面板的完整双工作区示例见 `examples/CompleteThirdParty/layouts/effects.json` 和 `layouts/settings.json`。

布局文件与每个 breakpoint 还可声明可选的 `ancillaryStyles`，用于没有独立 Component Node 的 popup menu、tooltip、scroll bar，以及应用创建的智能歌单编辑器：

```json
{
  "popupMenu": "popupMenu.studio",
  "tooltip": "tooltip.studio",
  "scrollBar": "scrollBar.studio",
  "smartPlaylistEditor": "smartPlaylistEditor.studio"
}
```

该对象只允许 `popupMenu`、`tooltip`、`scrollBar` 和 `smartPlaylistEditor` 四个键，每个值是已注册的完整 `target.style` 名称。选中 breakpoint 的映射逐键覆盖布局文件的映射；省略的键继承文件级映射，最终没有映射的 target 保持 `target.default`。该映射只替换相应附属控件的默认样式请求，不修改显式指定的 Component style 或 renderer slot。未知键报 `SCHEMA001`；target 不匹配或引用不存在报 `LAYOUT013`。前三个键要求应用 `0.3.0` 或更高，`smartPlaylistEditor` 要求 `0.8.0` 或更高。完整示例在 Effects 与 Settings 的 Studio 分支中覆盖前三个映射，并继承文件级的 `smartPlaylistEditor.default`；内置 Default 皮肤为编辑器提供独立的 Studio 与 Listening 样式。

### 9.2 节点通用字段

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| id | string | 无 | 布局文件内唯一 |
| type | enum | 无 | 节点类型 |
| width | length | auto | 宽度 |
| height | length | auto | 高度 |
| minWidth | number | 0 | 最小宽度 |
| maxWidth | number | 无上限 | 最大宽度 |
| minHeight | number | 0 | 最小高度 |
| maxHeight | number | 无上限 | 最大高度 |
| margin | inset | 0 | 外边距 |
| padding | inset | 0 | 内边距 |
| grow | number | 0 | 剩余空间增长权重 |
| shrink | number | 1 | 空间不足收缩权重 |
| alignSelf | enum | auto | 单节点交叉轴对齐 |
| aspectRatio | number | 无 | width / height |
| opacity | number | 1 | 0 至 1 |
| clip | boolean | false | 是否裁剪子节点 |
| zIndex | integer | 0 | stack 层级 |
| visible | boolean | true | 静态可见性 |
| visibleWhen | string | 无 | 0.7.0 起支持 `view.queueOpen`，0.10.0 起增加 `view.lyricsOpen`；布尔表达式为 false 时移除整个子树并回收其 flex 空间与间距 |

应用 0.8.0 起，`row`、`column`、`grid` 和 `stack` 容器支持可选 `pointerEvents: "none" | "block"`，默认 `none`。`block` 让容器矩形内的空白区域阻挡指向下层内容的鼠标与滚轮输入，子控件仍正常接收输入；它不增加点击动作、键盘焦点或模态行为。阻挡区域受祖先裁剪约束，隐藏或完全透明的容器不阻挡输入，`clip` 的子节点裁剪语义保持不变。`visual` 仍只允许 `none`，`component` 和 `scroll` 保持各自原生输入行为。

length 支持：

- JSON number：逻辑像素。
- 百分数字符串，例如 "36%"。
- fr 字符串，仅 grid tracks，例如 "1fr"。
- "auto"。

### 9.3 Row 和 Column

~~~json
{
  "id": "main",
  "type": "row",
  "gap": 12,
  "padding": 16,
  "align": "stretch",
  "justify": "start",
  "children": []
}
~~~

align：start、center、end、stretch。  
justify：start、center、end、spaceBetween、spaceAround。

### 9.4 Grid

~~~json
{
  "id": "settings-grid",
  "type": "grid",
  "columns": ["1fr", "1fr"],
  "rows": ["auto", "1fr"],
  "columnGap": 16,
  "rowGap": 12,
  "children": [
    {
      "id": "audio-card",
      "type": "column",
      "column": 1,
      "row": 1,
      "rowSpan": 2,
      "children": []
    }
  ]
}
~~~

column 与 row 从 1 开始。跨越轨道不得超出已声明网格。

### 9.5 Stack

~~~json
{
  "id": "artwork-stack",
  "type": "stack",
  "children": [
    {
      "id": "artwork",
      "type": "component",
      "component": "artwork.cover",
      "anchors": {
        "top": 0,
        "right": 0,
        "bottom": 0,
        "left": 0
      }
    },
    {
      "id": "badge",
      "type": "visual",
      "style": "surface.badge",
      "width": 80,
      "height": 24,
      "anchors": {
        "right": 12,
        "bottom": 12
      },
      "zIndex": 2
    }
  ]
}
~~~

anchors 允许 top、right、bottom、left、centerX 和 centerY。相对边同时存在时优先由边界确定尺寸；不能同时声明 left、right 和固定 width。

### 9.6 Scroll

scroll 必须恰好包含一个 child：

~~~json
{
  "id": "settings-scroll",
  "type": "scroll",
  "direction": "vertical",
  "child": {
    "id": "settings-content",
    "type": "column",
    "children": []
  }
}
~~~

direction 为 vertical 或 horizontal。Skin API 1 不支持双向滚动容器。滚动轴上的内容最小尺寸不会强迫视口扩张；内容保留自身尺寸，并通过滚动访问。键盘焦点进入视口外的子控件时，宿主自动滚动使其可见，包括同一容器内连续 Tab 和嵌套滚动区域。

容器的空白区域也接收滚轮输入。内嵌列表优先滚动自身内容；沿输入方向已到边界时，后续滚轮输入传递给仍可滚动的外层容器。一次输入使内层列表到达边界时，不会同时移动外层页面。列表支持像素级滚动，不要求视口高度是行高的整数倍。

### 9.7 Component Node

~~~json
{
  "id": "play",
  "type": "component",
  "component": "transport.playPause",
  "style": "textButton.primary",
  "width": 64,
  "height": 42,
  "focusOrder": 40
}
~~~

字段：

- component：公开 Component ID。
- style：与组件 renderer target 相容的 style。
- focusOrder：正整数；交互组件在同一布局入口内不得重复。
- presentation：仅在组件描述符允许时使用。

### 9.8 Visual Node

~~~json
{
  "id": "background",
  "type": "visual",
  "style": "surface.card",
  "pointerEvents": "none"
}
~~~

visual 的 pointerEvents 在 Skin API 1 只能是 none。装饰节点不能截获点击或键盘焦点。

### 9.9 规范布局算法

盒模型：

- width、height、min/max 和 aspectRatio 描述 border box。
- margin 位于 border box 外，padding 位于 border box 内。
- 子节点可用区域是父节点 content box。
- 百分比 width、left、right 相对父 content width；百分比 height、top、bottom 相对父 content height。
- margin、padding、gap 与所有数值长度必须非负。

auto 与固有尺寸：

- component 的 auto 尺寸来自 PlayerComponentCatalog 的稳定 preferred size，不从当前文本测量，因此曲名、语言和动态状态不会改变布局。
- visual 的 auto preferred size 为 0 × 0。
- row、column、grid、stack 的 auto size 是子节点最小贡献、margin、padding 与 gap 的确定性合计。
- scroll 的 viewport 由父布局确定；滚动轴 content extent 取 child 的测量尺寸。主轴溢出时，运行时在交叉轴内部预留滚动条空间，并将 child 约束到剩余可视区域。预留宽度来自当前布局解析出的 scrollBar 样式的 `controlStyle.thickness`，与原生滚动条使用同一逻辑像素值；未映射时使用 `scrollBar.default`，内置默认值为 12。单轴滚动不会自动启用第二个滚动方向。

Component Catalog 的 Skin API 1 默认尺寸：

| renderer target | minimum | preferred |
|---|---:|---:|
| label | 0 × 16 | 80 × 20 |
| textButton | 28 × 28 | 84 × 34 |
| toggleButton | 28 × 28 | 104 × 30 |
| comboBox | 60 × 28 | 132 × 32 |
| textEditor | 60 × 28 | 160 × 32 |
| linearSlider | 60 × 24 | 140 × 28 |
| rotarySlider | 48 × 48 | 72 × 72 |
| progressBar | 40 × 4 | 140 × 6 |
| table | 180 × 100 | 360 × 220 |
| playlistGrid | 120 × 160 | 600 × 400 |
| lyrics | 80 × 28 | 360 × 300 |
| artwork | 120 × 120 | 300 × 300 |
| effectChain | 220 × 120 | 360 × 300 |
| outputStage | 180 × 140 | 300 × 420 |
| outputCalibration | 180 × 160 | 340 × 400 |
| channelMap、channelMixer、pluginList、skinList | 180 × 100 | 360 × 240 |

Flex row/column：

1. 扣除 padding、child margin 和 gap。
2. fixed/percent 使用声明尺寸；auto 使用固有尺寸。
3. 应用 min/max 与 aspectRatio。
4. 正剩余空间按 grow 权重分配，达到 max 的节点冻结后重新分配。
5. 负剩余空间按 shrink × baseSize 分配，达到 min 的节点冻结后重新分配。
6. 剩余空间再按 justify 放置；交叉轴按 align/alignSelf 放置。

Grid：

1. 解析 fixed 与 percent tracks。
2. auto track 取所有单轨 child 的最大最小贡献。
3. 跨轨 child 的最小尺寸缺口先平均分配给覆盖的 auto tracks，再按 fr 权重分配给覆盖的 fr tracks。
4. 剩余空间按 fr 数值分配；空间不足时 fr 可以缩到 0，但 child 自身仍受 min 约束并使布局验证失败而不是静默重叠。

Stack：

- anchors 相对 stack content box。
- 同一轴声明两条相对边时由边界确定尺寸，禁止再声明该轴固定尺寸。
- 单边加固定尺寸合法；centerX/centerY 表示相对 content center 的逻辑像素偏移。
- zIndex 从小到大绘制，相同 zIndex 保持 JSON 顺序。

舍入：

- Engine 内部使用有限 float。
- 每组 sibling 在提交 JUCE 整数边界时使用累计余数分配，使最后一个 child 精确落在父 content edge。
- 不允许每个 child 独立 round 后留下裂缝。

Style 查找：

1. 当前皮肤的精确 target.variant。
2. 父皮肤的同名 target.variant。
3. 当前皮肤的 target.default。
4. 父皮肤的 target.default。
5. builtin.foundation 的 target.default。

component 省略 style 时从第 3 步开始。container 或 visual 的 style 必须是 surface target。style target 与 Component Descriptor 或 presentation 不匹配时导入失败。

## 10. Component Catalog

### 10.1 稳定性承诺

Component ID 是 Skin API 的公共 ABI：

- Skin API 1 生命周期内不重命名。
- 行为语义不改变。
- C++ 实现、类名和成员名可以改变。
- 新增可选 Component ID 不提升 Skin API。
- 修改已发布入口的必需组件集合须先判断 Skin API 主版本；新增可选入口可定义自身的必需组件，不改变旧入口要求。
- Skin API 1 的所有 Component ID 在每个最终布局中都是 singleton；独立必需 ID 恰好出现一次；可选 ID 及必需替代组中的每个 ID 最多出现一次。替代组至少提供一个可用组件。

### 10.2 应用框架

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| app.brand.eyebrow | label | 可选 | 应用副标题 |
| app.brand.title | label | 可选 | 应用名称 |
| app.deviceStatus | label | 可选 | 当前输出布局与设备摘要 |
| app.status | label | Studio、Listening、Library、Collection、Immersive、Mini 必需 | 最近一次操作或错误 |
| workspace.studio | textButton | Studio、Listening、Library、Collection 必需 | 切换到 Studio，保留当前页面 |
| workspace.listening | textButton | Studio、Listening、Library、Collection 必需 | 切换到 Listening，保留当前页面 |
| settings.open | textButton | Studio、Listening、Library、Collection 必需 | 打开 Settings |

workspace.studio 与 workspace.listening 由应用保持互斥 selected 状态。Effects 和 Settings 的关闭按钮为可选组件；内置皮肤通过顶部入口、工作区切换按钮和 Esc 返回主工作区。

### 10.3 曲库与队列

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| library.add | textButton | Studio、Listening 必需 | 打开本地音频文件或文件夹选择器；导入期间变为取消操作，图标上下文从 add 切换为 close；导入目标在开始时确定 |
| library.open | textButton | Library、Collection 必需；0.7.0 起 | 打开曲库总览 |
| player.open | textButton | Library、Collection 必需；0.7.0 起 | 返回播放器或工作区会话 |
| library.back | textButton | Collection 必需；0.7.0 起 | 返回曲库总览 |
| library.browsePlaylists / library.browseAlbums / library.browseArtists | textButton | 可选；0.7.0 起 | 切换总览中的歌单、专辑或艺术家集合 |
| library.browseSongs / library.browseFavourites / library.browseQueue | textButton | 可选；0.7.0 起 | 打开全部歌曲、收藏歌单；browseQueue 是展开当前布局共享队列的兼容入口，不改变浏览页面，须遵守下述队列目标与开关约束 |
| library.gridSearch | textEditor | 可选；0.7.0 起 | 筛选当前总览中的集合卡片 |
| library.grid | playlistGrid | Library 必需；0.7.0 起 | 具有封面、名称和摘要的集合卡片网格 |
| library.detailCover | artwork | 可选；0.7.0 起 | 当前集合封面，支持与 artwork.cover 相同的 presentation 和 frame slots |
| library.description | label | 可选；0.7.0 起 | 当前集合描述 |
| library.play / library.shufflePlay | textButton | 可选；0.7.0 起 | 从当前集合启动顺序或随机播放 |
| library.search | textEditor | 可选 | 按作品名、艺术家、专辑、格式或声道数筛选当前浏览集合；仅使用队列表格的旧皮肤继续筛选队列；应用 0.2.0 新增 |
| library.collections | comboBox | 可选；0.7.0 起 | 分组选择全部歌曲、专辑、艺术家、收藏和命名歌单；命名歌单可通过可编辑文字重命名 |
| library.newPlaylist | textButton | 可选；0.7.0 起 | 新建命名歌单 |
| library.actions | textButton | 可选；0.7.0 起 | 当前集合及选区的操作菜单，包含播放、下一首播放、追加队列、收藏、歌单编辑、撤销及本地 M3U/M3U8 交换；可用操作由应用决定 |
| library.title | label | 可选；0.7.0 起 | 当前浏览集合名称 |
| library.count | label | 可选；0.7.0 起 | 当前集合的曲目数量，右对齐 |
| library.empty | label | 可选；0.7.0 起 | 当前集合或搜索结果的空态提示 |
| library.tracks | table | Collection 必需；Studio、Listening 可替代 queue.table；0.7.0 起 | 当前浏览集合的有序条目 |
| queue.title | label | 可选 | 播放队列标题 |
| queue.count | label | 可选 | 播放队列曲目数量 |
| library.changeArtwork | textButton | 可选；0.7.0 起 | 选择命名歌单封面；其他集合禁用；支持 PNG/JPEG，应用缓存并保存 |
| queue.toggle | textButton | 可选；0.7.0 起 | 展开／收起共享队列；`state.selected` 表示展开；具有 `icon.queue` surface slot |
| queue.actions | textButton | 可选；0.7.0 起 | 当前队列的另存歌单、导出和清空菜单；右键条目还提供可用的上下移动 |
| queue.table | table | Studio、Listening 与 library.tracks 至少提供一个 | 当前播放条目及已发布的后续播放顺序，包含随机播放计划 |
| queue.empty | label | 可选 | 空队列提示；不能替代 library.add |
| queue.remove | textButton | 可选 | 删除队列选区 |
| queue.clear | textButton | 可选 | 清空播放队列 |
| queue.moveUp | textButton | 可选 | 上移队列选区 |
| queue.moveDown | textButton | 可选 | 下移队列选区 |

收藏以 `favourites` 集合身份常驻歌单网格首位，空收藏也保留。命名歌单的自定义封面优先于自动拼贴；歌单卡片、详情封面和详情标题的右键菜单提供更换／恢复自动封面、重命名、复制和删除；Actions 保留可聚焦的操作入口。内置皮肤与完整示例不单独摆放更换封面按钮，第三方皮肤仍可按需要使用可选的 `library.changeArtwork` 组件。曲目、收藏关系、封面文件缓存与播放行为由应用维护，皮肤通过组件和 slot 决定布局、字体、封面形状及各状态的绘制。

`queue.toggle` 不导航到新页面。应用公开只读布局状态 `view.queueOpen`，皮肤可以将 `visibleWhen: "view.queueOpen"` 声明在队列容器上，也可通过 `not view.queueOpen` 切换其他装饰区域。条件在测量前求值；flex 的空隙、margin 和尺寸一并回收，grid 的显式轨道保持声明尺寸。0.10.0 起还支持独立的 `view.lyricsOpen` 状态；未知上下文、token 和非布尔结果均被拒绝。编译器验证队列与歌词开关的全部四种组合状态，数值运算错误报告 `LAYOUT018`。`queue.toggle` 必须在两个状态中可达。声明 `queue.toggle` 或兼容入口 `library.browseQueue` 的每个完整 root 都必须在展开状态提供可见的 `queue.table`；`library.tracks` 不能替代此目标。使用 `library.browseQueue` 打开可折叠队列时，还必须声明可达的 `queue.toggle` 以供收起；队列表格始终可见时可省略开关。这些可达性错误报告 `LAYOUT011`，交互节点继续使用唯一正数 `focusOrder`。布局引擎参数缺省为关闭。完整示例的 Library 与 Collection 入口演示内嵌队列，Library 同时保留兼容入口。

应用默认在 Now Playing 展示队列；用户显式开关后，跨页面保留该选择。Library、Collection 与 Now Playing 复用同一个队列表格和播放计划；皮肤决定浮层、侧栏或上下布局。Default 皮肤将队列放在根 stack 中，通过锚点和 zIndex 覆盖正文，展开与收起不改变其他组件的尺寸和位置；容器声明 pointerEvents: "block" 防止空白区域点穿。点击队列按钮打开后，键盘焦点进入队列；再次点击按钮或由队列处理 Escape 时收起，队列中取得的键盘焦点回到开关按钮。当前编辑控件、状态提示及上层界面优先处理 Escape。内嵌队列继续遵循所在 flex 或 grid 的空间分配。随机播放的后续顺序来自已成功发布给音频引擎的计划，手动 Next 与自然换曲均遵循它。循环队列只展示有限的剩余前缀与一轮循环；当前条目通过 `row.playing` 标记。移除操作以队列条目为单位，当前播放条目固定；移动仅在可保留实际待播顺序的相邻非当前条目间启用。

`queue.table` 与 `library.tracks` 使用相同的 table renderer target、slots 和最小尺寸，可设置 `presentation: "table"` 或 `presentation: "list"`。table 展示标题、位置和时长列；list 使用无表头的较高行，展示标题和时长。省略 presentation 等同于 `table`，行高、表头高度和列几何继续使用解析出的 Skin rowLayout。布局切换和窗口缩放保留表格中的用户列调整；组件从皮肤布局卸载时恢复宿主原始列宽、顺序、可见性、行高和表头高度。`queue.table` 的 presentation 选项自应用 `0.3.0` 起提供；新增 `library.*` 组件要求 `minAppVersion` 为 `0.7.0` 或更高。

新组件沿用所属 kind 的目录最小尺寸：comboBox 为 60 × 28，textButton 为 28 × 28，label 为 0 × 16，table 为 180 × 100。建议焦点顺序为集合选择 100、导入 110、搜索 115、新建歌单 116、集合操作 117、曲目表格 120；同一入口内的其他交互组件须使用不同的正数。

集合详情提供 `library.newPlaylist` 或 `library.actions` 的歌单重命名操作时，应同时放置 `library.collections`。可将 `library.title` 与 `library.collections` 放在同一个高 48 像素的 stack 中，两者锚定四边，并在该详情页放置 `library.detailCover`。应用在普通状态显示只读标题，重命名期间显示可编辑的集合控件；renderer 无需模拟输入框或改变业务可访问性名称。完整示例的 `layouts/collection.json` 演示这组配对。

`library.grid` 的目录最小尺寸为 120 × 160。它的 `playlistGrid` renderer 使用 `controlStyle.minItemWidth` 和 `controlStyle.itemGap` 决定列数与间距；两个值均为逻辑像素。`rowLayout` 在每张卡片的宽度下解析，`bounds.width` 和 `contentBounds.width` 表示单张卡片宽度。正方形封面可以直接使用这个值作为高度，`rowHeight` 可以声明为 `{"$expr":"bounds.width + 72"}`，为封面下方的文字留下 72 像素。卡片高度与五个 slot 的几何由同一份解析结果决定。

| slot | renderer target | 内容 |
|---|---|---|
| card.surface | surface | 整张卡片的背景及 hover、pressed、selected、focused、disabled 状态 |
| card.cover | artwork | 单张封面或应用合成的拼贴；缺少封面时由 renderer 绘制占位图形 |
| card.title | label | 集合名称 |
| card.summary | label | 曲目数量与总时长摘要 |
| card.date | label | 集合说明与已知创建日期；智能歌单包含类型说明，未知日期不显示 |

五个 slot 均须在 `rowLayout.slots` 中声明。卡片是应用拥有的可访问按钮，键盘和点击行为由应用实现；slot 可以读取通用 `state.*`。这些 slot 还提供 `collection.id`、`collection.name`（string），`collection.trackCount`、`collection.durationSeconds`、`collection.createdAtMs`（number）和 `collection.hasArtwork`（boolean）。`createdAtMs` 使用 Unix 毫秒，0 表示未知；renderer 不应据此制造历史日期。`card.cover` 同时保留标准 `artwork.*` context。集合字段仅在网格卡片 slot 中可用，不属于独立 label、artwork 或网格背景的上下文。

卡片的键盘焦点由 `card.surface` 表现，该 slot 的 renderer 必须通过可见焦点验证。网格本体不接收键盘焦点，`playlistGrid` renderer 无需声明 focused 状态。

完整声明见示例包中的 `components/playlist-grid.json`、`components/collection-card.json` 和 `layouts/library.json`。新入口、组件和 renderer 均要求 `minAppVersion: "0.7.0"`。

两个表格的 `row.surface` 和 `cell.*` slots 提供以下上下文；表格主体和表头不提供逐行数据：

- row.index：整数。
- row.selected：布尔，当前浏览选区。
- row.playing：布尔，当前加载的播放条目标记；与 row.selected 独立，暂停时仍可保留标记；0.7.0 起。
- row.favourite：布尔，对应曲库记录是否收藏；0.7.0 起。
- row.dropEdge：number，-1 表示在该行前插入，0 表示无插入标记，1 表示在该行后插入；0.7.0 起。
- row.available：布尔。
- row.title：字符串。
- row.location：字符串。
- row.artist：字符串；没有艺术家元数据时为空。
- row.album：字符串；没有专辑元数据时为空。
- row.duration：字符串。
- column.id：title、location、duration、artist 或 album，仅在 cell.* slots 中提供。

引用 `row.playing`、`row.favourite` 或 `row.dropEdge` 的皮肤须将 `minAppVersion` 设为 `0.7.0` 或更高。

表格不向皮肤暴露文件对象；`row.location` 保持 Skin API 1 的原始绝对路径语义。`cell.location` 的 `text.primary` 与 `row.location` 始终是同一个绝对路径，`column.id` 仍为 `location`。Default 内置皮肤的专用 location label renderer 显式读取 `row.artist` 与 `row.album`，二者都为空时回退到 `text.primary`。第三方皮肤可以选择元数据字段，但不能把 `row.location` 或 `text.primary` 重新解释为艺术家/专辑。字符串内容及是否脱敏由应用决定，皮肤只决定是否以及如何绘制。

`library.tracks` 与 `queue.table` 的 table presentation 支持可选的 `cell.artist` 与 `cell.album` 独立列（0.7.0 起）。rowLayout 同时声明这两个 slot 时，应用用 Artist、Album 两列替代合并元数据列；各自的 `text.primary` 是对应字段。省略时保留 `cell.location` 的原有行为。list presentation 仍使用紧凑布局。列宽、行高和字体由皮肤决定，列排序由应用实现。

单击只改变浏览选择；双击或显式播放动作启动播放。Cmd/Shift 多选、菜单操作和拖拽使用条目身份，因此同一曲目的重复引用可分别选择和编辑。搜索、排序和切换集合不改变当前播放、音频设备或效果链。撤销恢复列表内容并保留当前播放位置；移除歌单条目不删除本地音乐文件。搜索投影无法确定完整列表插入位置时，应用禁用重排拖拽。皮肤通过 row.selected 与 row.playing 分别呈现选择和播放状态，通过 row.dropEdge 绘制插入线，业务动作和命中语义由应用拥有。

### 10.4 当前播放与封面

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| artwork.cover | artwork | 可选 | 应用生成或读取的封面 |
| artwork.metadata | label | 可选 | 位深、采样率、声道摘要 |
| nowPlaying.eyebrow | label | 可选 | NOW PLAYING 等提示 |
| nowPlaying.title | label | 可选 | 当前曲目标题 |
| nowPlaying.metadata | label | 可选 | 当前曲目来源或状态 |
| nowPlaying.artist | label | 可选；0.10.0 起 | 当前曲目艺术家 |

artwork renderer 可装饰 frame、background 和 overlay，但实际封面内容由组件提供，皮肤不能替换当前曲目的业务数据。`presentation: "contain"` 保留完整图片，`presentation: "cover"` 填满目标矩形并允许裁切。绘制顺序固定为 frame.background、可选封面氛围、artwork renderer、实际图片、frame.overlay；overlay 只负责图片之上的描边或装饰，不会反向裁切已经绘制的图片。

若要声明实际图片的目标矩形和圆角裁切，artwork renderer 可提供一个顶层 `roundedRect` layer，ID 固定为 `image-clip`：

```json
{
  "id": "image-clip",
  "type": "roundedRect",
  "frame": { "anchor": "bounds", "inset": 8 },
  "radius": { "$ref": "metrics.panelRadius" },
  "fill": "#00000000",
  "clip": true
}
```

- `frame` 是实际封面图片的目标矩形；contain/cover 在该矩形内计算。
- `radius` 是图片的 corner radius；`clip: true` 才会把同一个 roundedRect 路径应用到实际图片。
- `frame` 与 `radius` 可使用该 artwork renderer 原本允许的 token、bounds 表达式和 artwork 上下文；编译器继续按标准 roundedRect Schema 校验类型、引用和表达式。
- 该层仍属于 artwork renderer，因此 `fill` 通常应为透明色。overlay 仍只用于图片之后的视觉装饰。
- 既有 Skin API 1 皮肤若没有 `image-clip`，继续使用完整组件 bounds 且不额外裁切；这保证外部 JSON 皮肤向后兼容。Foundation 与 Default 都声明该层，确保 fallback 和内置皮肤的圆角意图一致。

artwork renderer 还可声明一个顶层 `roundedRect` layer，ID 为 `image-ambience`，把当前封面的低分辨率模糊图作为背景：

```json
{
  "id": "image-ambience",
  "type": "roundedRect",
  "frame": { "anchor": "bounds", "inset": 8 },
  "radius": { "$ref": "metrics.panelRadius" },
  "fill": "#00000000",
  "opacity": 0.18,
  "visible": { "$expr": "artwork.hasImage" }
}
```

`frame` 指定氛围图片的目标矩形，图片按 cover 填充并始终按 `radius` 裁切；`opacity` 与 `visible` 控制强度和显示。它们支持既有 token、bounds/artwork 上下文表达式及状态 variants。应用仅在封面改变时生成模糊缓存；标题等文本更新不触发重新生成。皮肤不提供封面路径，不读取用户文件。缺少此层或没有有效封面时不绘制氛围图，既有第三方皮肤与 Foundation 保持原行为。该层的 shadow 仍由标准 renderer 绘制，不改变 `image-clip` 的实际图片裁切契约。完整配置见 `examples/CompleteThirdParty/components/artwork.json`。

### 10.4.1 歌词与播放器呈现

应用 0.10.0 新增以下组件与可选入口；使用它们的皮肤声明 `minAppVersion: "0.10.0"`。未使用这些能力的 Skin API 1 包保持有效。

| ID | kind / target | 行为与状态 |
|---|---|---|
| lyrics.view | lyrics | 歌词正文；presentation 为 `document`（默认）或 `line` |
| lyrics.toggle | textButton | 展开／收起普通窗口的歌词；selected 表示展开；icon.lyrics |
| lyrics.actions | textButton | 应用提供的来源、导入、在线匹配、重试和时间偏移菜单；icon.more |
| lyrics.follow | textButton | 恢复播放位置跟随；selected 表示正在跟随 |
| player.fullscreen | textButton | 进入全屏呈现；icon.fullscreen |
| player.mini | textButton | 进入迷你窗口；icon.mini |
| player.restore | textButton | 恢复进入替代呈现前的窗口；Immersive、Mini 必需；icon.restore |
| player.pin | textButton | 迷你窗口置顶；selected 表示置顶；icon.pin |

全部按钮沿用 textButton 的最小尺寸 28 × 28、键盘焦点和状态上下文。普通 Listening 的焦点顺序宜为歌词开关、全屏、迷你、歌词操作、正文、播放控制；Mini 宜为置顶、恢复、正文、上一首、播放／暂停、下一首、音量。皮肤为每个交互 Component Node 提供唯一正数 `focusOrder`。`lyrics.view` 最小为 80 × 28；正文建议至少 300 × 260。业务文字、可访问性名称、歌词来源和时间戳由应用拥有。

`lyrics` renderer 必须提供 `controlStyle`：`padding` 为正文左右内边距，`lineGap` 为歌词段落之间的逻辑像素间距，`followAnchor` 为当前歌词顶边在视口高度中的比例，`followDurationMs` 为跟随滚动时间；0 表示立即定位。应用的减少动态效果设置会关闭跟随动画。超出单次 renderer 的 maxLines 时，应用分段绘制完整正文并保留原时间戳。当前行变化不重新压缩字体，换行按对应 label slot 的字体、字号与宽度计算；行高预留普通和 selected 样式中的较大值，避免逐行跳动。

| slot | target | 内容 |
|---|---|---|
| line.text | label | `text.primary` 为单段歌词；同时间的多语文本可以包含换行 |
| empty.title | label | `text.primary` 为加载、无歌词、纯音乐或错误状态标题 |
| empty.detail | label | `text.secondary` 为状态说明与可用恢复动作提示 |

`line.text` 的字体测量使用其根层 ID 为 `label` 的 Text layer（缺失时报告 `RENDER004`）；该 layer 的 frame 定义文本宽度，字号不会因长文本而缩小。正文样式宜使用 `maxLines: 32`、`overflow: "clip"`，单行呈现宜使用 `maxLines: 1`、`overflow: "ellipsis"`。正文组件使用应用计算的行 bounds 绘制每个 slot，皮肤决定字体、颜色、对齐与状态；空态 slot 接收整个内容区域，可自行安排标题和说明的 frame。

主体和 slots 均可访问 `lyrics.synced`、`lyrics.following`、`lyrics.hasText`（boolean），以及 `lyrics.presentation`（`document` / `line`）。行 slot 另提供 `line.index`、`line.timeSeconds`（number）、`line.active`、`line.past`（boolean）；主体与空态中的 line.index 为 -1、timeSeconds 为 0、active/past 为 false。行的 `state.selected` 表示当前播放行，hover 表示可跳转的鼠标指向行，focused 表示键盘浏览行；尚未浏览时对应当前播放行或首行。键盘焦点由 `line.text` 的文字颜色变化表现，空态由 `empty.title` 表现；这两个 slot 必须通过可见焦点验证。主体不绘制焦点框或整块背景高亮，文字字形度量在获得焦点时保持稳定。

同步正文跟随当前时间；滚轮、上下方向键与 Page Up / Page Down 暂停跟随，Home 或 lyrics.follow 恢复跟随。点击同步行或在键盘浏览后按 Enter 跳到原始时间戳，应用统一处理用户保存的时间偏移。纯文本可滚动阅读，不提供时间跳转。`line` 呈现只显示当前同步行；纯文本显示首段，尚未到第一条时间戳时不伪造活动行。

布局状态 `view.lyricsOpen` 默认关闭，与 `view.queueOpen` 独立。可将 `visibleWhen: "view.lyricsOpen"` 放在歌词列容器上以回收隐藏后的空间。每个声明 lyrics.toggle 的完整 root 都必须让该按钮在所有状态可达，并在歌词展开时提供可见的 lyrics.view；违反时报 `LAYOUT011`。

`entrypoints.layouts.immersive` 用于全屏，`entrypoints.layouts.mini` 用于紧凑窗口。两个入口都必须包含 `app.status`、`player.restore`、`transport.previous`、`transport.playPause`、`transport.next`；音量、封面、标题、歌词、操作菜单可按布局需要组合。它们复用当前播放、队列、音量和效果链，切换窗口呈现不重开音频设备。Foundation 为两个入口提供完整回退。 `app.status` 承载来源导入、搜索和保存失败等应用通知，必须有正面积且保持在可见区域内；可放入 stack 中作为浮层，不改变播放与歌词布局的尺寸。Mini 宜使用紧凑通知区域，避免遮住整个窗口。

布局文件可声明顶层 `preferredSize: { "width": 480, "height": 200 }`，表示窗口内容区的建议逻辑像素尺寸。数值必须大于 0，且不得小于 root 的 minWidth/minHeight；冲突报告 `LAYOUT019`。Mini 的窗口约束来自 root 的 minWidth、minHeight、maxWidth、maxHeight，建议尺寸来自 preferredSize；应用保存并恢复进入前的普通窗口边界。全屏使用实际屏幕内容区尺寸。完整示例的 Mini 最小为 380 × 170，建议为 480 × 200。

### 10.5 播放控制

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| transport.previous | textButton | Studio、Listening、Library、Collection 必需 | 上一曲或回到曲首 |
| transport.playPause | textButton | Studio、Listening、Library、Collection 必需 | 播放或暂停 |
| transport.next | textButton | Studio、Listening、Library、Collection 必需 | 下一曲 |
| transport.seek | linearSlider | Studio、Listening、Library、Collection 必需 | 播放位置 |
| transport.time | label | Studio、Listening、Library、Collection 必需 | 当前时间与总时长 |
| transport.shuffle | toggleButton | 可选 | 随机播放 |
| transport.repeat | comboBox | 可选 | Repeat Off、All、One；保留既有下拉框接口 |
| transport.repeatCycle | textButton | 可选 | 点击循环切换 Off → All → One；0.6.1 起，图标 slots 为 icon.repeat / icon.repeatOne |
| transport.volumeLabel | label | 可选 | VOLUME |
| transport.volumeValue | label | 可选 | 格式化音量 |
| transport.volume | slider | Studio、Listening、Library、Collection 必需 | 主输出音量 |

transport.volume 允许 presentation：

- linearHorizontal
- rotary

transport.seek 仅允许 linearHorizontal。皮肤不能改变值域或步进。

### 10.6 效果链

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| effects.open | textButton | 可选 | 打开 Effects 工作面板 |
| effects.close | textButton | 可选 | 关闭 Effects 并返回当前工作区 |
| effects.summary | label | 可选 | 当前效果链摘要 |
| effects.title | label | 可选 | 面板标题 |
| effects.preset.select | comboBox | 可选 | 效果链预设选择与命名草稿；0.9.0 起 |
| effects.preset.actions | textButton | 可选 | 效果链预设保存、管理与导入导出；0.9.0 起 |
| effects.filterLabel | label | 可选 | 筛选标签 |
| effects.filter | textEditor | Effects 必需 | 名称、厂商、类型和格式的文字搜索 |
| effects.typeFilter | comboBox | 可选 | 按插件上报类型筛选，0.6.0 起 |
| effects.manufacturerFilter | comboBox | 可选 | 按厂商筛选，0.6.0 起 |
| effects.formatFilter | comboBox | 可选 | 按 AU/VST3 格式筛选，0.6.0 起 |
| effects.scope | comboBox | 可选 | 为新增效果器选择完整总线、单声道或声道对；0.4.0 起 |
| effects.scopeLabel | label | 可选 | 效果器作用范围标题；0.4.0 起 |
| effects.catalog | comboBox | Effects 必需 | 可插入插件目录 |
| effects.add | textButton | Effects 必需 | 插入所选插件 |
| effects.clearTails | textButton | 可选 | 清除所有已加载插件的尾音与宿主延迟缓冲，保留参数、作用声道和单插件旁路状态；0.6.1 起 |
| effects.bypassAll | textButton | 可选 | 切换全链监听旁路，保留每个插件的独立旁路状态；0.6.1 起 |
| effects.chain | effectChain | Effects 必需 | 动态效果槽列表 |
| effects.empty | label | 可选 | 空效果链提示 |
| effects.outputLayout | comboBox | 可选 | 当前处理声道布局 |

`effects.clearTails` 是一次性操作；没有已加载插件时禁用。播放中的音乐仍继续送入效果链，因此清除后可产生新的尾音。插件自身生成的持续声音不属于缓存尾音。

`effects.bypassAll` 通过 `state.selected` 表示旁路已开启，输出在原声与效果信号之间短暂淡变；效果器继续处理，以免恢复时释放冻结的旧缓存。该开关是会话内的监听状态，不写入插件预设或应用存档，切换工作区保持不变。空效果链且未开启旁路时禁用。两个按钮使用标准 `textButton` 状态和最小尺寸 28 × 28；建议宽度至少 104，并将焦点顺序安排在效果筛选控件之前。完整示例在 Effects 标题行放置这两个控件。

effects.chain 内部动态槽使用固定 style slots：

- slot.surface
- slot.name
- slot.bypass
- slot.editor
- slot.remove
- slot.dragIndicator

皮肤不能直接实例化、删除或重排动态槽；这些行为仍由 effects.chain 组件处理。

应用 0.4.0 增加可选 `slot.scope`（textButton）绘制槽，显示并编辑该效果器作用范围。空声道集合表示完整输出总线，显式单声道或声道对按扬声器身份保存；未选声道透传。插件不支持所选总线或切换后扬声器缺失时，变更失败并保留原处理链。

### 10.7 输出显示

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| output.stage | outputStage | 可选 | 当前多声道电平、响度和扬声器标签 |
| output.mixer | channelMixer | 可选 | 每声道增益、静音和独奏；组件自 0.4.0 起，独立 target 自 0.5.0 起 |
| output.calibration | outputCalibration | Settings 可选 | 逐扬声器 trim、delay、polarity 与测试信号；0.9.0 起 |
| output.mode | comboBox | 可选 | 选择 Peak、Peak + RMS、Loudness 或 Mixer；0.4.0 起 |
| output.reset | textButton | 可选 | 仪表模式下开始新测量；Mixer 模式下重置所有声道增益、静音和独奏；Settings 校准卡片内停止测试并重置校准；0.4.0 起，校准行为自 0.9.0 起 |

outputStage 提供：

- text.primary：当前曲目标题；没有曲目时为空。
- text.secondary：当前 CoreAudio 输出设备名称。
- meter.channelCount。
- meter.channels[n].index。
- meter.channels[n].label。
- meter.channels[n].peak：线性采样峰值，0 为静音，1 为 0 dBFS；超出满刻度时可大于 1。
- meter.channels[n].peakDb：同一采样峰值的 dBFS 值（20 × log10(peak)），显示下限为 -60；大于 0 的读数保留。
- meter.channels[n].isLfe。
- output.layout：`mono`、`stereo`、`2.1`、`3.0`、`quad`、`5.0`、`5.1`、`6.1`、`7.0`、`7.1`、`5.1.2`、`5.1.4`、`7.1.2`、`7.1.4`、`7.1.6`、`9.1.4`、`9.1.6`。
- meter.channels[n].rmsDb / truePeakDb：RMS / True Peak 电平，显示下限 -60 dB，保留正值。repeat 对应 `item.rmsDb` / `item.truePeakDb`。
- meter.mode：`peak`、`rms` 或 `loudness`。
- meter.available：后台是否已经发布测量结果。
- meter.interrupted：是否因测量队列丢帧重新开始本次测量，应用 0.5.0 起。
- meter.status：测量等待、中断或测量位置的显示文字。
- meter.momentary / shortTerm / integrated：LUFS 数值；无有效结果时表达式值为 -100。
- meter.momentaryText / shortTermText / integratedText：一位小数的 LUFS 字符串，无有效结果时为 `--`。
- meter.truePeakText：本次测量所有输出声道中的最大 dBTP，无有效结果时为 `--`。

新增测量字段、16 声道上限及模式控件要求 `minAppVersion: "0.4.0"`。

`meter.channels` 的 repeat 模板以 `item.peak`、`item.peakDb`、`item.label` 和 `item.isLfe` 访问当前声道。采样峰值来自效果链与输出增益之后的实际采样；RMS 使用 300 ms 窗口，LUFS 使用 K 加权及门限，Momentary / Short-term 分别为 400 ms / 3 s。LFE 不计入 LUFS，但保留独立峰值、RMS 与 True Peak。测量在后台工作线程进行，界面模式或 A/B 切换不重置测量。dB 转换在绘制线程执行。使用 `clamp((item.peakDb + 60) / 60, 0, 1)` 可将 -60 至 0 dBFS 映射为等距 dB 刻度的柱高；静音与低于量程的读数落在柱底。

`output.mixer` 使用 `channelMixer` target，要求应用 `0.5.0` 或更高；`channelMap` 用于设备声道映射，旧 channelMap 组件只读，routing 组件可编辑。Mixer 最小尺寸为 180 × 100，建议至少 300 × 240。它与 `output.stage` 可放在同一个 stack 内，应用根据 `output.mode` 切换可见性，布局尺寸保持不变。Mixer 按实际扬声器顺序显示可滚动行，按行依次遍历 gain、mute、solo；焦点顺序由宿主提供。各行的 slots：

- `row.surface` → surface：行分割线与背景。
- `row.source` → label：L、R、C、LFE 等扬声器简称。
- `row.destination` → label：声道增益的 dB 文本。
- `row.peak` → label：输出端测得的 dBFS 峰值。
- `row.gain` → linearSlider：`value.normalized` 为 -60 至 +6 dB 的位置，最低位置为静音；双击恢复 0 dB。
- `row.mute` / `row.solo` → toggleButton：`state.selected` 表示是否启用；`text.primary` 分别为 M / S，应用提供完整可访问性名称和提示。

每声道设置以扬声器身份持久化。Solo 可多选，Mute 优先于 Solo，未出现在当前布局内的 Solo 不参与当前总线选择。增益位于效果链后、主音量与仪表前，使用 20 ms 平滑。只读 `settings.audio.channelMap` 保留旧皮肤接口；可编辑端口使用 `settings.audio.routing`。

### 10.7.1 听音方案与效果链预设

`listeningPreset.select`（comboBox）和 `listeningPreset.actions`（textButton）提供听音方案入口，建议放在 Studio 主区工具行和 Settings Audio 区域的独立行。`effects.preset.select` / `effects.preset.actions` 在 Studio 的内嵌 Effects 卡片和 Effects 页面标题下组成预设行。两组控件均自应用 0.9.0 起提供，Skin API 保持为 1；使用新组件的包须声明 `minAppVersion: "0.9.0"`。

选择器展示已保存名称，未保存时显示占位文本，当前内容有变更时显示 modified。应用方案或效果链期间控件禁用，成功后再显示确认生效的预设。操作菜单包含 Save as、Update current、Revert to saved、Rename、Duplicate、Delete、Import 和 Export；需要现有记录的操作在空集合或没有选择时禁用；Revert to saved 仅当前已有预设且内容有变更时可用，成功应用后恢复保存值。名称输入复用 selector 的可编辑 ComboBox，Return 提交，Escape 取消；空名称或保存失败保留草稿，Actions 提供保存重试、继续编辑和取消。菜单与编辑器继承所在页面的 popupMenu 和 comboBox renderer。

两组选择器遵循 comboBox 的 60 × 28 最小尺寸，操作按钮遵循 textButton 的 28 × 28 最小尺寸；建议一行高 36，按钮宽 92。焦点顺序为选择器、操作按钮，再进入同一面板的其余控件。`state.enabled`、`state.focused`、`state.hover`、`state.pressed` 及 comboBox 的 `combo.hasSelection` / `combo.popupOpen` 沿用既有含义；皮肤不决定预设的持久化或应用行为。

### 10.8 Settings

| ID | 类型 | Settings 必需 | 行为或数据 |
|---|---|---|---|
| settings.title | label | 否 | 设置标题 |
| settings.close | textButton | 否 | 关闭 Settings |
| settings.audio.heading | label | 否 | Audio / Core Audio 分区标题 |
| settings.calibration.title | label | 否 | Speaker calibration 卡片标题；0.9.0 起 |
| output.calibration | outputCalibration | 否 | 扬声器校准与逐声道测试表单；0.9.0 起 |
| output.reset | textButton | 否 | 停止测试并重置所有扬声器校准；0.9.0 起 |
| settings.audio.device | comboBox | 是 | 选择 CoreAudio 输出设备 |
| settings.audio.deviceConfiguration | textButton | 否 | 打开设备配置界面；macOS 回退 Audio MIDI Setup，Windows 回退系统声音设置；0.6.1 起 |
| settings.audio.sampleRate | comboBox | 是 | 选择设备采样率 |
| settings.audio.bufferSize | comboBox | 是 | 选择缓冲区 |
| settings.audio.channelLayout | comboBox | 是 | 选择受支持的扬声器布局，受实际设备输出能力约束 |
| settings.audio.channelMap | channelMap | 否 | 只读查看逻辑声道到当前设备实际启用端口的映射；优先使用设备端口名，缺失时显示物理编号 Output N；端口不可在皮肤内重排 |
| settings.audio.routing | channelMap | 否 | 为每个逻辑声道选择物理输出或 Disconnected，通过各行的原生 ComboBox 选择；0.6.1 起 |
| settings.plugins.formats | label | 否 | 插件分区标题 |
| settings.plugins.enableVst3 | toggleButton | 否 | 启用 VST3 扫描和新增插件，0.6.0 起 |
| settings.plugins.enableAu | toggleButton | 否 | 启用 Audio Unit 扫描和新增插件，0.6.0 起 |
| settings.plugins.filter | textEditor | 否 | 搜索插件清单，0.6.0 起 |
| settings.plugins.typeFilter | comboBox | 否 | 按插件上报类型筛选，0.6.0 起 |
| settings.plugins.manufacturerFilter | comboBox | 否 | 按厂商筛选，0.6.0 起 |
| settings.plugins.formatFilter | comboBox | 否 | 按 AU/VST3 格式筛选，0.6.0 起 |
| settings.plugins.knownList | pluginList | 否 | 已发现插件 |
| settings.plugins.scanAll | textButton | 是 | 扫描选定格式的默认位置及已保存的 VST3 目录 |
| settings.plugins.scanFolder | textButton | 是 | 添加并保存额外 VST3 文件夹，由 Scan 开始扫描 |
| settings.plugins.cancel | textButton | 是 | 取消扫描 |
| settings.plugins.progress | progressBar | 是 | 扫描进度 |
| settings.plugins.scanScope | comboBox | 否 | 本次扫描范围：已启用格式、仅 VST3、仅 AU；0.6.1 起 |
| settings.plugins.defaultLocations | label | 否 | 默认扫描位置摘要，tooltip 显示完整路径；0.6.1 起 |
| settings.plugins.customDirectories | comboBox | 否 | 已保存的自定义 VST3 目录；0.6.1 起 |
| settings.plugins.removeDirectory | textButton | 否 | 移除所选扫描目录，保留已知插件与已加载实例；0.6.1 起 |
| settings.skins.heading | label | 否 | Appearance / Skins 分区标题 |
| settings.skins.library | skinList | 是 | 已安装与内置皮肤 |
| settings.skins.import | textButton | 是 | 导入 .moonwake-skin |
| settings.skins.remove | textButton | 否 | 删除选中第三方皮肤 |
| settings.skins.preview | textButton | 否 | 预览选中皮肤 |
| settings.skins.apply | textButton | 是 | 应用预览皮肤 |
| settings.skins.cancelPreview | textButton | 是 | 恢复预览前皮肤；未预览时 disabled |
| settings.skins.reload | textButton | 否 | 开发目录 Reload |

`settings.audio.routing` 的目标是当前设备的真实物理端口；输出布局决定逻辑扬声器顺序，效果链和 Mixer 保持该逻辑顺序。端口一对一分配，也可断开；其他声道已占用的端口不可重复选择。设置按设备身份和逻辑布局持久化，设备能力变化导致已保存路由不可用时，将输出断开并显示诊断，保留原配置以供设备恢复；没有保存路由的设备采用默认顺序。组件使用 channelMap 的行 slots。`row.output` 为 comboBox 槽位，使用标准 comboBox context（不提供 channel.*），几何由 rowLayout 决定，未声明时沿用 `row.destination` 的矩形；style 未映射时使用 comboBox.default。各行按扬声器顺序遍历，组件需声明正数且唯一的 focusOrder，内置布局将其安排在声卡格式选项之后；推荐最小尺寸为 180×100。焦点反馈由各行 ComboBox 提供，容器无需重复绘制焦点；交互 slot 的可见焦点仍由编译器校验。下拉菜单使用所在 layout 的 popupMenu renderer。旧 `settings.audio.channelMap` 继续提供只读投影。

`transport.repeatCycle` 最小尺寸为 28×28，使用 textButton 的 state.selected 表示循环开启，icon.repeatOne 表示单曲循环。应用提供状态名称与 tooltip；皮肤负责图形、尺寸及位置，推荐 focusOrder 为 235。一个布局选择 repeat 或 repeatCycle 即可。内置布局将 Shuffle、Previous、Play/Pause、Next、Repeat 放在同一播放控制组。

Studio 可直接绑定 effects.chain 及添加插件组件，和 Output／Mixer 共用工作区。内置 Studio 集成效果链；Effects entrypoint 继续供兼容皮肤使用。effects.scope 决定下一次插入的处理范围，旧布局缺少该控件时新增效果使用全逻辑总线；slot.scope 决定单个现有效果的范围；All channels 对应逻辑输出总线。两者均不修改物理设备路由。

插件筛选维度采用交集；文字搜索按空白拆分，所有词均需命中名称、厂商、类型或格式。类型来自插件元数据，VST3 的 `Fx` 显示为 `Effect`；缺失类型显示 `Uncategorised`，缺失厂商显示 `Unknown manufacturer`。Effects 只提供已启用格式的非乐器插件；Settings 保留全部已扫描记录，关闭格式的条目通过 `plugin.enabled: false` 和 `state.enabled: false` 交给皮肤绘制。

格式开关使用 `state.selected` 显示当前偏好，扫描期间 disabled；开关关闭后不再扫描该格式，也不能新增该格式的插件。偏好随会话保存，旧会话默认启用 AU 和 VST3。已加载效果继续处理音频，已保存效果链恢复时仍允许载入原有实例。显示格式筛选只影响列表，本次扫描范围不会修改格式启用偏好。关闭两种格式后扫描入口 disabled；目录管理在空闲时仍可使用，扫描期间禁用。AU 从系统注册表发现，自定义目录只用于 VST3。取消扫描保留已有记录与本次已完成的有效结果。筛选组件采用标准 comboBox/textEditor 的最小尺寸和焦点行为；内置 Effects 筛选焦点顺序为搜索、类型、厂商、格式、插件目录，Settings 为格式开关、筛选、清单和扫描动作。旧布局缺少 scanScope 时，本次扫描使用已启用格式，隐藏的单格式选择不会限制扫描。皮肤预览保留可见筛选控件的值，进入缺少相应控件的旧布局时清除不可操作的条件。

内置皮肤的 remove 始终 disabled。Foundation 仅用于继承与运行时故障回退，不出现在可选皮肤列表中；旧会话保存的 Foundation 选择在启动时恢复为默认皮肤。故障回退后仍可通过皮肤列表预览并应用其他皮肤。`builtin.foundation` 的继承契约保持有效。音频设备选项由 CoreAudio 提供；皮肤不能添加虚假设备或改变设备配置值。

`settings.skins.library` 的 slots 语义固定如下：

- `row.surface`：单个皮肤卡片背景；可读取完整 `skin.*` Context。
- `row.name`、`row.author`、`row.version`：皮肤清单元数据。
- `row.status`：ACTIVE、PREVIEW、BUILT-IN 或 IMPORTED 等由应用提供的状态文字；皮肤只决定呈现。

皮肤库视口是紧凑、可滚动的卡片区域，不提供额外 header 或空白占位 slots。导入包与开发目录入口由相邻的公共按钮 Component IDs 提供，皮肤不应在列表内部复制这些动作。

皮肤不得根据 `skin.*` Context 改写按钮动作，也不得把 `row.status` 画成另一个可点击控制。应用保留选择、键盘滚动、预览、应用与删除行为。

### 10.8.1 手动输出校准

`output.calibration` 使用 `outputCalibration` kind 和 target，放在 Settings Audio 区域下方的独立 Speaker calibration 卡片中；卡片标题绑定 `settings.calibration.title`，Reset 按钮绑定 `output.reset`。Settings 各区块纵向排列，校准随设置页滚动可达，离开 Settings 即停止测试。最小尺寸为 180 × 160，建议为 340 × 400；内容高度由 `rowLayout.rowHeight` 定义，超出可用卡片高度时垂直滚动，Tab 导航自动将当前控件带入可见区域。

扬声器选择器按当前输出布局列出最多 16 个具名声道；方向键可选择声道。trim 范围为 -24 至 +12 dB，delay 为 0 至 200 ms，polarity 切换正常或反相。数值通过 Return 或离开输入框提交，非法输入保留为草稿并显示范围错误，不转换为零。扬声器身份及各项数值由应用持有。

测试信号为 Pink noise 或 Sine tone，默认电平 -30 dBFS，可设置 -60 至 -12 dBFS；正弦频率范围为 20 至 20000 Hz。选择 LFE 时默认 60 Hz 正弦，其余扬声器默认粉红噪声。Start test / Stop test 显式控制开始和停止，状态文字始终标明测试或待测扬声器。切换扬声器或离开校准界面停止测试；测试信号不保存到预设。

| Slot | Target | 内容 |
|---|---|---|
| calibration.channelLabel / calibration.channel | label / comboBox | 扬声器标题与选择器 |
| calibration.trimLabel / calibration.trim | label / textEditor | Trim (dB) 与增益数值 |
| calibration.delayLabel / calibration.delay | label / textEditor | Delay (ms) 与延时数值 |
| calibration.polarity | textButton | 正常/反相，state.selected 表示反相 |
| calibration.signalLabel / calibration.signal | label / comboBox | 测试信号标题与类型 |
| calibration.levelLabel / calibration.level | label / textEditor | Level (dBFS) 与测试电平 |
| calibration.frequencyLabel / calibration.frequency | label / textEditor | Tone (Hz) 与正弦频率；粉红噪声时禁用 |
| calibration.startStop | textButton | 开始或停止测试 |
| calibration.status | label | 当前扬声器、运行状态或可恢复错误；建议支持两行文字 |

所有公开 slot 都必须在 `rowLayout.slots` 中放置，缺少 slot、交互框过小或越出内容边界报告 `RENDER004`。字体、颜色、圆角、间距、表单几何和状态绘制完全由 Skin 提供。默认焦点顺序为 channel、trim、delay、polarity、signal、level、frequency、startStop；每个焦点只由对应 slot renderer 绘制一次。

### 10.9 Label 语义对齐

以下对齐是 Component Catalog 的 Skin API 1 公共约定。使用 `{ "$context": "text.align" }` 的 Label renderer 会收到：

| 对齐 | Component ID |
|---|---|
| right | app.deviceStatus、queue.count、transport.volumeLabel、transport.volumeValue |
| center | artwork.metadata、queue.empty、transport.time、effects.empty |
| left | 其余所有公开 label Component ID |

队列表格另有固定列语义：`header.text` 的 Duration（column 3）为 `right`，Title 与 Location 表头为 `left`；`cell.duration` 为 `right`，`cell.title` 与 `cell.location` 为 `left`。这些值通过同一个 `text.align` 上下文传给 slot label renderer。

公开 Component ID 的 `text.align` 遵循本表；未绑定公开 ID 的原生 Label 则按 JUCE justification 映射到相同九值集合。Renderer 中声明的字面 `align` 优先于上下文。

### 10.10 TextButton 语义图标

语义图标描述动作，不描述素材文件、Unicode 字符或某个内置主题的外观。Skin API 1 的稳定名称与默认呈现为：

| icon.name | Component ID | 默认呈现 |
|---|---|---|
| add | library.add、effects.add | icon-only |
| settings | settings.open | icon-only |
| effects | effects.open | icon-only |
| close | effects.close、settings.close | icon-only |
| previous / play / pause / next | transport.previous、transport.playPause、transport.next | icon-only；播放中 playPause 从 play 切到 pause |
| remove | queue.remove | icon-only |
| moveUp / moveDown | queue.moveUp、queue.moveDown | icon-only |
| scan / folderSearch | settings.plugins.scanAll、settings.plugins.scanFolder | leading icon + text |
| import / reload / preview / remove | 对应 settings.skins.* 操作 | leading icon + text |

`queue.clear`、workspace 模式、Cancel Scan、Apply 和 Cancel preview 没有图标语义：它们依赖对象范围、模式名或结果性文字，改成纯图标会降低可理解性。

推荐的第三方实现把 textButton style 写成复合 entry，将它需要支持的 slot 映射到 surface renderer。例如一个 add 按钮 style 可以声明 `"icon.add": "surface.actionIcon"`。`surface.actionIcon` 的 Path layer 可把 `data` 绑定到 `{ "$context": "icon.path" }`，并用以下 transform 把标准 24 × 24 path 放进应用提供的 icon bounds：pivotX/pivotY 为 0，scaleX 为 `bounds.width / 24`，scaleY 为 `bounds.height / 24`，translateX/translateY 分别为 `bounds.x` 与 `bounds.y`。这是 Path `data` 唯一允许的动态来源；任意 `$expr` 或 `text.primary` 动态 path 会被拒绝，避免把业务文本当图形语法解析。应用提供的 fallback path 在 catalog 初始化时预编译，正常 paint 不重新解析路径。

皮肤不必使用 fallback path。它可以在 `icon.<semantic>` slot 使用自己的静态 Path/SVG，也可以在 textButton 主体的 Text layer 将 `icon.name` 交给自带 ligature icon font。无论哪种方式，都不能改变语义名。纯图标命中区至少 36 × 36，常规图形建议 16–20 px；leading 图标左侧建议保留 10 px，文字 frame 左 inset 建议至少 34 px。

是否显示可见文字由 renderer 决定，但按钮的真实 `text.primary`、title、tooltip、VoiceOver role 与动作始终由应用保留。第三方皮肤不得用 visual node 另造一个可点击图标来替代公开 TextButton。

## 11. Required Component Rules

Studio 和 Listening 每个最终布局必须且只能包含一次以下独立必需组件：

~~~text
library.add
transport.previous
transport.playPause
transport.next
transport.seek
transport.time
transport.volume
settings.open
workspace.studio
workspace.listening
app.status
~~~

每个 Studio 和 Listening 最终布局还必须包含 `queue.table` 或 `library.tracks`，也可以同时包含两者。至少一个表格须可见、具有非零尺寸且透明度大于 0；使用 `queue.toggle` 的布局允许队列表格在关闭状态按条件折叠，但按钮必须可见，展开状态必须显示表格；祖先容器的隐藏或透明同样参与检查。`queue.table` 的 descriptor 通过 `requiredAlternatives` 声明 `library.tracks`；替代关系只满足工作区内容要求，不改变两个组件的数据归属，也不能满足 `queue.toggle` 或 `library.browseQueue` 对队列目标的要求。仅提供原有 `queue.table` 的皮肤继续有效。

可选的 Library 和 Collection 入口各自必须包含 `library.open`、`player.open`、`workspace.studio`、`workspace.listening`、`settings.open`、`app.status`，以及 `transport.previous`、`transport.playPause`、`transport.next`、`transport.seek`、`transport.time`、`transport.volume`。Library 还必须包含 `library.grid`；Collection 还必须包含 `library.tracks` 和 `library.back`。这些组件在基础 root 和每个完整 breakpoint root 中都必须可用。`library.add` 在这两个入口中可选。

Settings 最终布局必须包含前一节标记为“是”的全部组件。

Effects 最终布局必须包含：

~~~text
effects.filter
effects.catalog
effects.add
effects.chain
~~~

如果皮肤 extends builtin.foundation，编译器可以从基础入口继承完整布局。但一旦皮肤提供某个布局入口，该入口必须独立满足 required rules，不会自动把缺少的组件隐式插入到任意位置。

Skin API 1 不允许在同一布局中重复同一个 Component ID。应将唯一的 `library.add` 放在空队列和非空队列状态下都可访问的位置，例如 Queue 标题区。

## 12. 完整 Listening 布局示例

<!-- skin-doc-test: schema=layout route=listening semantics=complete -->
~~~json
{
  "apiVersion": 1,
  "root": {
    "id": "root",
    "type": "column",
    "children": [
      {
        "id": "header",
        "type": "row",
        "height": 60,
        "padding": { "top": 8, "right": 20, "bottom": 8, "left": 20 },
        "gap": 8,
        "align": "center",
        "children": [
          {
            "id": "brand",
            "type": "component",
            "component": "app.brand.title",
            "grow": 1
          },
          {
            "id": "studio-mode",
            "type": "component",
            "component": "workspace.studio",
            "style": "textButton.neutral",
            "width": 112,
            "focusOrder": 10
          },
          {
            "id": "listening-mode",
            "type": "component",
            "component": "workspace.listening",
            "style": "textButton.primary",
            "width": 112,
            "focusOrder": 20
          },
          {
            "id": "settings",
            "type": "component",
            "component": "settings.open",
            "style": "textButton.neutral",
            "width": 86,
            "focusOrder": 30
          }
        ]
      },
      {
        "id": "content",
        "type": "row",
        "grow": 1,
        "gap": 12,
        "padding": 8,
        "children": [
          {
            "id": "artwork-card",
            "type": "stack",
            "width": "36%",
            "minWidth": 300,
            "style": "surface.card",
            "children": [
              {
                "id": "artwork",
                "type": "component",
                "component": "artwork.cover",
                "anchors": {
                  "top": 18,
                  "right": 18,
                  "bottom": 50,
                  "left": 18
                }
              },
              {
                "id": "artwork-meta",
                "type": "component",
                "component": "artwork.metadata",
                "height": 24,
                "anchors": {
                  "right": 18,
                  "bottom": 14,
                  "left": 18
                }
              }
            ]
          },
          {
            "id": "right",
            "type": "column",
            "grow": 1,
            "gap": 10,
            "children": [
              {
                "id": "now-playing-card",
                "type": "column",
                "height": 142,
                "padding": 18,
                "gap": 4,
                "style": "surface.card",
                "children": [
                  {
                    "id": "now-title",
                    "type": "component",
                    "component": "nowPlaying.title",
                    "height": 46
                  },
                  {
                    "id": "now-meta",
                    "type": "component",
                    "component": "nowPlaying.metadata",
                    "height": 22
                  },
                  {
                    "id": "seek",
                    "type": "component",
                    "component": "transport.seek",
                    "style": "linearSlider.seek",
                    "height": 28,
                    "minHeight": 28,
                    "focusOrder": 50
                  },
                  {
                    "id": "time",
                    "type": "component",
                    "component": "transport.time",
                    "height": 18
                  }
                ]
              },
              {
                "id": "queue-card",
                "type": "column",
                "grow": 1,
                "padding": 12,
                "style": "surface.card",
                "children": [
                  {
                    "id": "queue-header",
                    "type": "row",
                    "height": 34,
                    "gap": 6,
                    "children": [
                      {
                        "id": "queue-title",
                        "type": "component",
                        "component": "queue.title",
                        "grow": 1
                      },
                      {
                        "id": "add-music",
                        "type": "component",
                        "component": "library.add",
                        "style": "textButton.primary",
                        "width": 104,
                        "focusOrder": 40
                      },
                      {
                        "id": "remove",
                        "type": "component",
                        "component": "queue.remove",
                        "style": "textButton.neutral",
                        "width": 70,
                        "focusOrder": 90
                      }
                    ]
                  },
                  {
                    "id": "queue",
                    "type": "component",
                    "component": "queue.table",
                    "style": "table.queue",
                    "grow": 1,
                    "focusOrder": 100
                  }
                ]
              },
              {
                "id": "transport-card",
                "type": "row",
                "height": 104,
                "padding": 14,
                "gap": 8,
                "align": "center",
                "style": "surface.card",
                "children": [
                  {
                    "id": "previous",
                    "type": "component",
                    "component": "transport.previous",
                    "style": "textButton.neutral",
                    "width": 64,
                    "height": 44,
                    "focusOrder": 60
                  },
                  {
                    "id": "play",
                    "type": "component",
                    "component": "transport.playPause",
                    "style": "textButton.primary",
                    "width": 72,
                    "height": 48,
                    "focusOrder": 70
                  },
                  {
                    "id": "next",
                    "type": "component",
                    "component": "transport.next",
                    "style": "textButton.neutral",
                    "width": 64,
                    "height": 44,
                    "focusOrder": 80
                  },
                  {
                    "id": "transport-spacer",
                    "type": "visual",
                    "style": "surface.transparent",
                    "grow": 1
                  },
                  {
                    "id": "volume",
                    "type": "component",
                    "component": "transport.volume",
                    "style": "rotarySlider.default",
                    "presentation": "rotary",
                    "width": 90,
                    "height": 90,
                    "focusOrder": 110
                  }
                ]
              }
            ]
          }
        ]
      },
      {
        "id": "status",
        "type": "component",
        "component": "app.status",
        "height": 28
      }
    ]
  }
}
~~~

该示例省略了一些可选操作，但包含全部 required component。Add Music 明确属于 Queue，而不属于 Settings。

## 13. 状态、交互和事件

皮肤不能声明 onClick、onChange 或业务事件。Component Node 绑定后，应用自动连接原有行为。

皮肤只读取视觉状态：

- enabled
- hover
- pressed
- focused
- selected
- dragging

应用保证：

- transport.playPause 的文本和 icon 随播放状态变化。
- workspace 按钮的 selected 状态互斥。
- queue 操作在没有有效选择时 disabled。
- 插件扫描期间按钮与 progress 状态正确。
- 不可用曲目通过 row.available 暴露。
- Settings 中内置皮肤删除按钮 disabled。

皮肤不得通过隐藏、透明度为 0 或零尺寸规避 required component；必需替代组至少保留一个可用组件。编译器会检查最终尺寸约束、祖先可见性和透明度。

## 14. 动画

### 14.1 支持的触发器

- hover
- pressed
- selected
- focused
- disabled
- value
- workspaceEnter
- previewEnter

hover、pressed、selected、focused、disabled 和 value 使用 computed transition：旧状态已计算属性是起点，新状态已计算属性是终点。动画尚未完成时再次变化，以当前屏幕上的插值结果为新起点。

workspaceEnter 在某个布局入口成为当前 route 后触发一次；previewEnter 在新的 Preview Skin Session 安装后触发一次。两个入口触发器必须提供 from patch：

~~~json
{
  "workspaceEnter": {
    "durationMs": 180,
    "easing": "easeOut",
    "properties": ["body.opacity", "body.transform.translateY"],
    "from": {
      "body.opacity": 0,
      "body.transform.translateY": 8
    },
    "respectReduceMotion": true
  }
}
~~~

from 的每个 key 必须同时出现在 properties 中。终点是 renderer 在进入后的正常 computed value。

### 14.2 Easing

- linear
- easeIn
- easeOut
- easeInOut
- springGentle

number、color、opacity 和 transform 数值支持插值；color 使用预乘 alpha linear-sRGB。boolean、string、path、font、image 和 gradient 不插值，在进度达到 1 时切换。image/source 与 font 离散切换期间会同时保留其 binding 的资源包命名空间；父子皮肤即使使用相同 `assets/...` 路径也不会串包。springGentle 只允许 number 和 transform。

### 14.3 Reduce Motion

respectReduceMotion 默认 true。系统开启 Reduce Motion 时：

- durationMs 大于 0 的状态过渡缩短到 0。
- value 驱动的仪表仍更新，但不增加补间。
- 皮肤不能将 respectReduceMotion 设为 false 用于 workspaceEnter 或 previewEnter。

## 15. 可访问性

第三方皮肤必须：

- 保留 required component。
- 为所有交互组件提供唯一正整数 focusOrder。
- focused 状态必须有可见变化。
- 不使用仅靠颜色区分的 enabled、selected 或错误状态。
- 文本不可通过 visual layer 替代真实组件文本；纯图标 TextButton 可以不绘制可见文字，但必须保留同一个真实按钮组件及其应用提供的可访问名称。
- interactive component 的最小点击区域为 28 × 28；推荐 36 × 36。
- 正文文本与背景对比度推荐至少 4.5:1。
- 大文字推荐至少 3:1。
- disabled 状态仍应可辨认，而不是完全透明。

应用保留鼠标操作后的真实键盘焦点及快捷键行为，但只在键盘导航时向 renderer 提供 `state.focused`。焦点视觉、选中态和旁通态互相独立。

真实组件的 VoiceOver 名称、角色和值由应用提供，皮肤不能覆盖。Visual Node 默认且必须对辅助技术隐藏。

导入时会在 160 × 48 与 320 × 160 两个规范探针尺寸分别离屏绘制 unfocused/focused 状态，并比较最终像素；两个尺寸都必须产生变化。只改变被 `opacity: 0`、全透明填充、零宽描边或无效阴影遮蔽的属性不算可见焦点；仅在特殊尺寸成立的 focused 表达式也不能通过该检查。第三方作者应让焦点指示在两种探针尺寸及实际最小控件尺寸上都清晰可见。

## 16. Settings 信息架构

Settings 的功能分区：

| 分区 | 功能 |
|---|---|
| Audio | CoreAudio 设备、采样率、缓冲区、输出布局及可选的端口映射 |
| Music library | 音乐目录登记、手动扫描、取消扫描、停止管理及自动扫描开关 |
| Plug-ins | AU/VST3 格式开关、插件搜索和筛选、已扫描清单、标准位置与额外 VST3 文件夹扫描、取消及进度 |
| Appearance | 皮肤库、导入、预览、应用、删除，以及开发目录加载和重载 |

library.add 不得出现在 Settings 布局。它必须出现在 Studio 与 Listening 主工作区。

## 17. 版本和兼容

### 17.1 Skin API

skinApi 只在破坏性变化时增加。同一 Skin API 内：

- Skin API 1 是封闭协议；未知字段、Component ID、renderer target 或 capability 都会拒绝导入。
- 新播放器继续接受符合 Skin API 1 的旧皮肤。
- 新增字段、可选 Component ID、token 类型或 capability 时，使用新的 minAppVersion；旧播放器明确拒绝该皮肤。
- 已有 ID 的行为不能改变。
- 新增必需 Component ID 或改变既有语义必须提升 skinApi。
- 未知 capability 使用稳定错误码 COMPAT003。

### 17.2 皮肤版本

manifest version 使用 SemVer：

- patch：不改变设计接口的资源或错误修复。
- minor：向后兼容的新样式或布局。
- major：皮肤作者自己的不兼容视觉变化。

同一 skin id 可以安装多个版本。每次安装最多保留两个版本：新导入版本，以及受保护的当前版本或最近的可回滚版本；其余版本通过可恢复事务清理。相同 id/version 的不同内容会被拒绝，因此发布任何内容变化都必须提升皮肤版本。

### 17.3 App 版本

minAppVersion 高于当前播放器时拒绝导入。皮肤不应根据 app version 编写表达式分支。

## 18. 导入、预览和失败行为

导入阶段：

1. Capture immutable private snapshot
2. Package security and SHA-256 manifest
3. Manifest compatibility
4. JSON Schema
5. Token resolution
6. Render compilation
7. Layout compilation
8. Component requirements
9. Assets
10. Minimum-size dry layout
11. Recheck snapshot digest
12. Atomic install from snapshot

Import skin 成功不代表立即应用。Preview 创建临时 Skin Session，Apply 持久化，Cancel preview 恢复此前皮肤。预览期间播放器同时保护预览版本和此前已确认版本，不允许删除它们；为避免版本保留清理破坏 Cancel，预览期间也拒绝导入这两个 skin id 的另一版本。先 Apply 或 Cancel preview 再导入即可。

导入开始后应用不再读取用户选择的原文件或目录。即使原路径在验证期间被替换，编译与安装仍只使用同一份私有不可变快照。Reload folder 每次都重新捕获快照，且始终是只预览；开发目录中的版本不能直接 Apply，作者必须先通过 Import skin 正式导入。

播放器会用安装时记录的 SHA-256 发现意外的安装后内容变化，并拒绝链接、超长或畸形完整性记录。这个检查面向不受信任的声明式皮肤输入和意外文件损坏，不是本机账户入侵防护：拥有同一 macOS 用户文件系统写权限的恶意进程能够同时改写包和应用本地元数据，皮肤格式不承诺抵御该攻击者。

运行时错误：

- 单个可选 visual renderer 失败：隐藏该 visual，Skin Session 进入 degraded，并按 code/path 只记录一次 warning。
- required component renderer 失败：Skin Session 进入 fatal；SkinManager 在下一次消息线程调度中终止预览或回退 builtin.foundation。
- layout 产生非有限边界：终止当前 Skin Session。
- 资源在安装后丢失：回退继承资源；仍失败则 safe-default。

发生 fatal 回退时，SkinManager 在切换到 `builtin.foundation` 之前复制失败 Skin Session 的 selection、health 与完整 runtime report，作为 last failed session record 保留到下一次失败或宿主清除。该记录目前作为宿主内部诊断接口保留；回退后的 active session 不会覆盖失败证据。

每个 Preview 或 active skin 都有独立 Skin Session，拥有自己的资源缓存、动画、运行时报告和 health。Renderer 通过 fault sink 报告，不能在 paint 内直接替换 session。切换与回退保持音频播放和应用状态。

meter 数据是 UI 读取的最多 16 个声道测量快照。皮肤只看到 SkinRenderContext 中的副本，不能访问 AudioEngine、callback buffer、锁或实时线程；复杂 renderer 只可能影响 UI 帧率，不会进入音频处理路径。

## 19. 诊断格式

GUI 和 Skin Tool 使用相同报告：

~~~text
ERROR COMPONENT004 layouts/listening.json#/root/children/1
Required component "library.add" is missing.
Suggestion: add one visible component node with component "library.add".
~~~

稳定错误类别：

| 前缀 | 类别 |
|---|---|
| PKG | 包与路径 |
| MANIFEST | 清单 |
| COMPAT | 版本与 capability |
| SCHEMA | JSON Schema |
| TOKEN | token 与引用 |
| EXPR | 表达式 |
| LAYOUT | 布局 |
| COMPONENT | 组件契约 |
| RENDER | 绘制 |
| ASSET | 资源 |
| RUNTIME | 预览、激活与回退 |

错误码在同一 Skin API 内保持含义稳定。

完整稳定注册表见同目录 [ERROR_CODES.md](ERROR_CODES.md)。新增 code 只能追加；不能复用、重编号或改变既有机器含义。

## 20. Skin Tool

仓库提供独立命令行工具 `MoonwakeSkinTool`。以下命令假设当前目录为仓库根目录，并已完成默认 `build/` 构建。

验证开发目录：

    ./build/MoonwakeSkinTool validate MyFirstSkin/

打包：

    ./build/MoonwakeSkinTool pack MyFirstSkin/ --output MyFirstSkin.moonwake-skin [--force]

验证发布包：

    ./build/MoonwakeSkinTool validate MyFirstSkin.moonwake-skin

生成布局报告：

    ./build/MoonwakeSkinTool layout MyFirstSkin/ --workspace listening --size 1240x800

生成快照：

    ./build/MoonwakeSkinTool snapshot MyFirstSkin/ --workspace listening --size 1240x800 --output preview.png [--theme studio|listening] [--state representative|normal|hover|pressed|selected|focused|disabled] [--force]
    ./build/MoonwakeSkinTool layout MyFirstSkin/ --workspace effects --size 1240x800 --theme studio
    ./build/MoonwakeSkinTool snapshot MyFirstSkin/ --workspace settings --size 1240x800 --theme listening --output settings-listening.png

`layout` 与 `snapshot` 可传入 `--queue-open` 和 `--lyrics-open` 预览队列与歌词展开状态，两者独立，省略时为关闭。`--workspace` 同时支持 `immersive` 和 `mini`。其他命令或重复使用上述开关报告 `TOOL001`。

`layout` 与 `snapshot` 的 `--workspace` 接受 `studio|listening|effects|settings|library|collection`，`--theme studio|listening` 选择断点匹配的当前工作区。可选页面必须由皮肤声明。`--theme` 可省略，默认按入口推导；也可在两个主入口上显式覆盖，便于检查条件分支。未知值、重复参数或其他命令使用此参数均报 `TOOL001`，缺少参数值报 `TOOL002`。

导出当前应用的规范 Schema：

    ./build/MoonwakeSkinTool schema --output-dir SkinAPI1Schema/ [--force]

验证开发者指南全部 JSON fenced block，并编译完整示例：

    ./build/MoonwakeSkinTool verify-docs --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md --example docs/skin-api/examples/CompleteThirdParty/

所有命令非零退出表示失败。validate 在仅有 warning 时退出 0，可使用 --warnings-as-errors。

## 21. 发布前检查清单

- manifest 的 id、version、license 和 minAppVersion 正确。
- 四个基础布局入口，以及声明的 Library／Collection 入口，均可通过验证。
- Studio 与 Listening 包含全部 required component。
- Settings 没有 library.add。
- Add Music 在主队列中清晰可见；若采用 library.search，搜索框与队列宽度和焦点顺序一致。
- 1040 × 700 下无裁切、重叠或零尺寸控件。
- 所有交互组件有唯一 focusOrder。
- focused、selected、disabled 状态可辨认。
- B 或其他亮色布局没有继承深色控件颜色。
- Reduce Motion 下没有强制进入动画。
- 包中没有未引用的大型资源。
- 字体和图片具有分发许可。
- validate 无 error。
- Preview 可以通过 Settings → Appearance → Cancel preview 取消；fatal renderer 会自动回退随应用内置的 `builtin.foundation`。

## 22. Skin API 1 的兼容承诺

第三方可以长期依赖：

- 本文列出的 Component ID。
- manifest、tokens、layout 和 renderer 的字段语义。
- 状态合并顺序。
- required component 规则。
- 资源限制。
- 诊断类别。
- 不执行任意代码的安全模型。

实现可以从 JUCE Component 重构为其他内部结构，但只要 Skin API 仍为 1，以上契约不会因内部 C++ 类名或布局实现变化而改变。


## 音乐目录与智能歌单（0.8.0）

使用本节组件的皮肤须声明 `minAppVersion: "0.8.0"`。Skin API 1 继续由应用拥有目录扫描、规则验证、预览和保存；皮肤负责组件几何、文字绘制、交互状态与焦点反馈。

### Settings 音乐目录

| Component ID | kind / renderer target | 行为 |
|---|---|---|
| settings.music.heading | label | Music library 分区标题 |
| settings.music.directories | directoryList | 已注册文件夹的可滚动、可键盘选择列表 |
| settings.music.add | textButton | 选择并注册文件夹 |
| settings.music.scan | textButton | 扫描已注册文件夹，包含暂停自动扫描的目录 |
| settings.music.cancel | textButton | 请求取消正在运行的扫描 |
| settings.music.remove | textButton | 停止管理选中目录，保留曲库曲目与歌单 |
| settings.music.automatic | toggleButton | 切换选中目录的自动扫描 |
| settings.music.status | label | 扫描进度、完成状态或可恢复错误 |

`directoryList` 最小尺寸为 180 × 100，建议为 360 × 160。必须提供 `rowLayout`；行高、间距以及 `row.surface`（surface）、`row.name`、`row.path`、`row.status`、`empty.message`（label）均由皮肤定义。行 slots 提供 `directory.index`（number）、`directory.id`、`directory.name`、`directory.path`（string）和 `directory.automatic`（boolean）。列表主体不提供行上下文；`state.selected` 指向当前选中行。`contentClip` 支持与其他原生列表相同的形状裁切。

建议按目录列表 → Add folder → Scan now → Cancel scan → Stop managing → Automatic scanning 安排唯一且正数的 `focusOrder`。同一页面的各分区沿视觉顺序递增编号，目录分区位于 Audio 与 Plug-ins 之间。扫描期间取消按钮启用，目录配置操作禁用；空列表禁用扫描和选中项操作。`status` 为只读信息。分区放入 Settings 的垂直滚动容器，目录列表内部独立滚动。

### 智能歌单编辑器

`library.smartPlaylistEditor` 的 kind 和 renderer target 为 `smartPlaylistEditor`；`library.smartPlaylistRules` 为 `smartPlaylistRuleList`。两者由应用在智能歌单操作中创建，不要求在页面布局内常驻。当前页面通过 `ancillaryStyles.smartPlaylistEditor` 选择编辑器样式，例如 `smartPlaylistEditor.studio` 或 `smartPlaylistEditor.listening`；未声明时使用 `smartPlaylistEditor.default`。编辑器中 ComboBox 的菜单仍使用页面的 `popupMenu` 样式。Foundation 提供三个新增 target 的完整默认实现。

编辑器覆盖宿主窗口；renderer layers 绘制遮罩和面板。其 `rowLayout` 接收宿主窗口尺寸；将 `rowHeight` 设为 `container.height` 时，slots 的 `bounds` 与宿主窗口边界相同。面板尺寸、位置和各控件几何完全由该 renderer 定义。推荐编辑区为 840 × 530 并在视口缩小时收缩；组件目录最小尺寸为 560 × 360，推荐尺寸为 760 × 480。

| smartPlaylistEditor slot | target | 内容 |
|---|---|---|
| editor.heading | label | Smart playlist 标题 |
| editor.nameLabel | label | Name 标签 |
| editor.name | textEditor | 歌单名称 |
| editor.match | comboBox | Match all rules / Match any rule |
| editor.rules | smartPlaylistRuleList | 滚动规则列表 |
| editor.add | textButton | 添加规则，达到 16 条时禁用 |
| editor.preview | label | 匹配曲目数、规则错误或保存失败原因 |
| editor.save | textButton | 保存有效草稿 |
| editor.cancel | textButton | 放弃草稿并关闭 |

`smartPlaylistRuleList` 最小尺寸为 512 × 100，推荐为 700 × 240。其 `rowLayout` 在每条规则的可用宽度下解析，行高与间距共同决定滚动范围。`rule.field`、`rule.operator`、`rule.boolean` 使用 comboBox；`rule.value` 使用 textEditor；`rule.remove` 使用 textButton。`rule.value` 和 `rule.boolean` 共享一个值区域，应用根据字段类型只显示其中一个。字段和运算符来自应用支持的规则集合，皮肤不能添加运算符或执行表达式来改变规则业务。

两个智能歌单 target 必须声明 `rowLayout` 并放置全部公开 slots；交互槽位使用对应 primitive 控件的最小尺寸且须处于原生行边界内。槽位使用标准 `state.*`、`text.*` 和控件上下文，不公开曲库对象。焦点顺序为 Name → Match → 按行 Field / Operator / Value / Remove → Add rule → Save → Cancel。焦点由当前 primitive 控件绘制，列表与面板不叠加第二层焦点框。

编辑器展示草稿的实时匹配数。名称为空或模型校验失败时禁用 Save；保存失败保留草稿并显示原因，成功保存后关闭；Cancel 和 Escape 不改变已保存歌单。规则结果显示在现有歌单详情中，不能手工添加、移除或重排曲目。播放、排队和收藏操作保持可用。
