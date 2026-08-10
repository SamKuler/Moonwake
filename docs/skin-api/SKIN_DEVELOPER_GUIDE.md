# Moonwake Skin API 1 开发者指南

- 接口版本：Skin API 1
- 文档状态：开发期稳定契约；公开发布后在 Skin API 1 内保持兼容
- 面向对象：第三方皮肤作者、设计工具作者和自动化构建工具作者
- 文档位置：仓库公开文档 `docs/skin-api/`
- 版本基线：应用 0.2.0、内置皮肤 1.0.0

## 1. Skin API 能做什么

Skin API 允许皮肤包重新定义：

- Studio、Listening、Effects 和 Settings 四个响应式布局。
- 应用背景、面板、按钮、开关、下拉框、滑杆、旋钮、列表、仪表和装饰图层。
- normal、selected、hover、pressed、focused、disabled 和 dragging 状态。
- 字体、SVG、图片、渐变、阴影、圆角和状态动画。
- 播放器公开组件的位置、尺寸、层级和键盘焦点顺序。

Skin API 不允许皮肤：

- 执行 JavaScript、Lua、原生代码或 shell。
- 访问网络、任意用户文件、进程、音频缓冲或插件实例；包内资源只能由宿主在验证后读取。
- 改变 Play、Add Music、Scan Plug-ins 等操作的业务含义。
- 注册新的业务事件或调用未公开的播放器方法。

皮肤是一个声明式视图。播放器拥有数据与行为，皮肤只决定如何放置和绘制已经公开的组件。

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
  "minAppVersion": "0.1.0",
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

开发时在 Settings → Appearance / Skins → Developer Folder 中选择该目录。应用会自动验证并以开发预览模式加载；目录已选择后，按钮显示为 Reload Folder。独立验证使用 `MoonwakeSkinTool validate`。开发目录不能直接 Apply，发布前应先用 Skin Tool 打包为 `MyFirstSkin.moonwake-skin`，再通过 Import Package、Preview 和 Apply Preview 完成正式导入。

schema/ 下的五个 JSON Schema 由播放器 SkinSchemaRegistry 通过 MoonwakeSkinTool schema 生成，是机器可读字段、类型和枚举的真源；本文解释其语义。发布验收会重新生成 Schema、验证本文全部 fenced JSON 与完整示例包，禁止手工维护两套字段集合。

### 2.1 可直接运行的完整示例

`docs/skin-api/examples/CompleteThirdParty/` 是一个自包含、满足全部 required component 规则的 Skin API 1 最小开发目录，包含四个可验证布局、tokens、renderer index、按钮、surface、seek 和 volume renderer。它有意省略部分可选组件，完整列表见第 10 节 Component Catalog。它不是伪代码；从仓库根目录执行：

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
  "minAppVersion": "0.1.0",
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
| entrypoints.layouts | object | 否 | 缺失入口继承基础布局 |

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

如果 source 字体成功加载，该文件中的具体 typeface 优先；如果加载失败，按 `families` 顺序回退。`sans-serif`、`serif` 和 `monospace` 是三个大小写不敏感的精确逻辑别名，分别映射到 JUCE 在当前平台上的默认无衬线、衬线和等宽字体。它们不是名为这些字符串的物理字体，也不会进行前缀或模糊匹配。

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
| state.focused | boolean | 是否有键盘焦点 |
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

### 7.1 Renderer 调用粒度与专属 Context

| target | 调用粒度 | 额外 Context | 固定 slots |
|---|---|---|---|
| label | 每个 Label 一次 | text.primary、text.secondary、text.align | 无 |
| textButton | 每个按钮主体一次；显式绑定时再调用 icon slot | text.primary、text.align、icon.name、icon.path、state.* | 该组件声明的 icon.&lt;semantic&gt; surface slot；playPause 同时声明 icon.play 与 icon.pause |
| toggleButton | 每个开关一次 | text.primary、text.align、state.selected | 无 |
| comboBox | 主体一次；弹出菜单固定交给 popupMenu.default | text.primary、text.align、state.* | 无 |
| textEditor | 主体一次 | text.primary、text.align、state.focused | 无 |
| linearSlider | 每帧一次 | value.*、state.dragging | 无 |
| rotarySlider | 每帧一次 | value.*、state.dragging | 无 |
| progressBar | 每帧一次 | value.normalized、value.display | 无 |
| scrollBar | 每个原生滚动条一次 | scroll.orientation、scroll.thumbStart、scroll.thumbSize、scroll.part、state.* | 无 |
| popupMenu | 背景、item、separator 或 sectionHeader 各一次 | menu.part、menu.itemIndex、text.primary、text.secondary、text.align、icon.name、state.enabled、state.selected | 无 |
| tooltip | 每个原生 tooltip 一次 | text.primary、text.align | 无 |
| surface | 每个 container 或 visual 节点一次 | bounds.* | 无 |
| table | 表主体一次 | table.rowCount、table.columnCount | header.surface、header.text、row.surface、cell.title、cell.location、cell.duration |
| artwork | 封面组件一次 | artwork.hasImage、artwork.aspectRatio | frame.background、frame.overlay |
| effectChain | 链主体一次；每个动态槽分别调用 slots | 主体：chain.slotCount；slots：chain.slotCount、slot.index、slot.bypassed、slot.hasError、slot.name | slot.surface、slot.name、slot.bypass、slot.editor、slot.remove、slot.dragIndicator |
| outputStage | 输出级每帧一次 | text.primary（曲目标题）、text.secondary（当前设备名）、meter.*、output.* | 无 |
| channelMap | 主体一次；每个只读通道映射调用 slots | slots：channel.index、channel.sourceLabel、channel.deviceLabel、channel.connected | row.surface、row.source、row.destination |
| pluginList | 主体一次；每个可见插件调用 slots | slots：plugin.index、plugin.name、plugin.manufacturer、plugin.format、plugin.enabled | row.surface、row.name、row.manufacturer、row.format、row.status |
| skinList | 主体一次；每个可见皮肤分别调用 slots | 行 slots：skin.index、skin.id、skin.name、skin.author、skin.version、skin.builtin、skin.active、skin.previewed | row.surface、row.name、row.author、row.version、row.status |

复合 entry 与简单 entry 使用同一规范查找顺序：当前皮肤精确 style → 父皮肤同名精确 style → 当前皮肤 target.default → 父皮肤 target.default → builtin.foundation target.default。component 省略 style 时从当前皮肤 target.default 开始。某个 slot 未显式绑定时从该 slot 类型的当前皮肤 target.default 开始。缺少 builtin.foundation 默认 renderer 属于应用构建错误。

scrollBar、popupMenu 和 tooltip 是 JuceSkinAdapter 使用的 ancillary target，不对应可放进 layout 的 Component ID。Skin API 1 对这三个 target 只允许名为 `default` 的 style；声明其他 variant 会拒绝导入。每个皮肤可以覆盖 scrollBar.default、popupMenu.default 和 tooltip.default；builtin.foundation 必须提供三者，从而保证 Viewport、菜单和提示不回落到平台默认深色绘制。ComboBox 的自身 variant 不传播给 popup；所有 ComboBox 以及非 ComboBox 打开的菜单都使用当前皮肤解析后的 popupMenu.default。

scroll.orientation 为 `horizontal` 或 `vertical`；scroll.thumbStart 与 scroll.thumbSize 是 target 本地轴上的逻辑像素；scroll.part 为 `bar`、`buttonStart` 或 `buttonEnd`。`bar` 一次绘制轨道与 thumb，两个 button part 分别绘制可选箭头。menu.part 为 `background`、`item`、`separator` 或 `sectionHeader`；普通 item 的 menu.itemIndex 是当次菜单绘制中从 0 开始、跳过 separator 与 sectionHeader 的稳定视觉序号，非 item 调用固定为 -1。table 的 row.surface 接收整行 bounds，cell.title、cell.location 与 cell.duration 分别接收其实际列 bounds；不要假定三个 cell slot 共享整行几何。tooltip 使用应用提供的 text.primary，皮肤不能替换其可访问性内容。

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
  "limit": 10,
  "template": {
    "id": "channel-bar",
    "type": "roundedRect",
    "frame": {
      "x": { "$expr": "item.index * 18" },
      "y": { "$expr": "bounds.height * (1 - item.peak)" },
      "width": 12,
      "height": { "$expr": "bounds.height * item.peak" }
    },
    "radius": 3,
    "fill": { "$ref": "colors.control.active" }
  }
}
~~~

允许的数据源：

- meter.channels，最多 10。
- table.visibleColumns，最多 3。
- output.channelLabels，最多 10。

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

when 支持 minWidth、maxWidth、minHeight、maxHeight、minScaleFactor 和 maxScaleFactor；边界比较均为包含。宽高使用逻辑像素，scale factor 只参与断点选择和最终绘制，不把布局数字重复放大。

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

direction 为 vertical 或 horizontal。Skin API 1 不支持双向滚动容器。

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
- scroll 的 viewport 由父布局确定；滚动轴 content extent 取 child 的测量尺寸。仅当主轴确实溢出时，运行时会在交叉轴内部预留固定 8 个逻辑像素的滚动条 gutter，并把 child 交叉轴约束到剩余可视区域；因此单轴滚动不会把末端内容压在滚动条下面，也不会暗中开启第二个滚动方向。皮肤通过 `scrollBar.default` 绘制这条 8 px gutter，不应依赖平台默认宽度。

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
| artwork | 120 × 120 | 300 × 300 |
| effectChain | 220 × 120 | 360 × 300 |
| outputStage | 180 × 140 | 300 × 420 |
| channelMap、pluginList、skinList | 180 × 100 | 360 × 240 |

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
- 新增必需 Component ID 必须提升 Skin API。
- Skin API 1 的所有 Component ID 在每个最终布局中都是 singleton；必需 ID 恰好出现一次，可选 ID 最多出现一次。

### 10.2 应用框架

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| app.brand.eyebrow | label | 可选 | 应用副标题 |
| app.brand.title | label | 可选 | 应用名称 |
| app.deviceStatus | label | 可选 | 当前输出布局与设备摘要 |
| app.status | label | Studio、Listening 必需 | 最近一次操作或错误 |
| workspace.studio | textButton | Studio、Listening 必需 | 切换到 Studio |
| workspace.listening | textButton | Studio、Listening 必需 | 切换到 Listening |
| settings.open | textButton | Studio、Listening 必需 | 打开 Settings |

workspace.studio 与 workspace.listening 由应用保持互斥 selected 状态。

### 10.3 曲库与队列

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| library.add | textButton | Studio、Listening 必需 | 空闲时打开本地音频文件或文件夹选择器；导入期间变为可见的取消操作，图标上下文从 add 切换为 close |
| library.search | textEditor | 可选 | 按作品名、艺术家、专辑、格式或声道数筛选队列；应用 0.2.0 新增 |
| queue.title | label | 可选 | 队列标题 |
| queue.count | label | 可选 | 曲目数量 |
| queue.table | table | Studio、Listening 必需 | 展示与选择播放队列 |
| queue.empty | label | 可选 | 空队列提示；不能替代 library.add |
| queue.remove | textButton | 可选 | 删除选中曲目 |
| queue.clear | textButton | 可选 | 清空队列 |
| queue.moveUp | textButton | 可选 | 上移选中曲目 |
| queue.moveDown | textButton | 可选 | 下移选中曲目 |

queue.table 的公开行上下文：

- row.index：整数。
- row.selected：布尔。
- row.available：布尔。
- row.title：字符串。
- row.location：字符串。
- row.artist：字符串；没有艺术家元数据时为空。
- row.album：字符串；没有专辑元数据时为空。
- row.duration：字符串。
- column.id：title、location 或 duration。

`queue.table` 不向皮肤暴露文件对象；`row.location` 保持 Skin API 1 的原始绝对路径语义。`cell.location` 的 `text.primary` 与 `row.location` 始终是同一个绝对路径，`column.id` 仍为 `location`。应用另行提供向后兼容的 `row.artist` 与 `row.album`；Default 内置皮肤的专用 location label renderer 会显式读取这两个字段，二者都为空时回退到 `text.primary`。第三方皮肤可以选择元数据字段，但不能把 `row.location` 或 `text.primary` 重新解释为艺术家/专辑。字符串内容及是否脱敏由应用决定，皮肤只决定是否以及如何绘制。

`library.search` 仅改变 `queue.table` 的可见投影，不改变播放队列顺序或当前播放项目。使用该可选 Component ID 的第三方皮肤必须将 `minAppVersion` 设为 `0.2.0` 或更高；未放置它的既有 Skin API 1 皮肤继续有效。

### 10.4 当前播放与封面

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| artwork.cover | artwork | 可选 | 应用生成或读取的封面 |
| artwork.metadata | label | 可选 | 位深、采样率、声道摘要 |
| nowPlaying.eyebrow | label | 可选 | NOW PLAYING 等提示 |
| nowPlaying.title | label | 可选 | 当前曲目标题 |
| nowPlaying.metadata | label | 可选 | 当前曲目来源或状态 |

artwork renderer 可装饰 frame、background 和 overlay，但实际封面内容由组件提供，皮肤不能替换当前曲目的业务数据。`presentation: "contain"` 保留完整图片，`presentation: "cover"` 填满目标矩形并允许裁切。绘制顺序固定为 frame.background、artwork renderer、实际图片、frame.overlay；overlay 只负责图片之上的描边或装饰，不会反向裁切已经绘制的图片。

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

### 10.5 播放控制

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| transport.previous | textButton | Studio、Listening 必需 | 上一曲或回到曲首 |
| transport.playPause | textButton | Studio、Listening 必需 | 播放或暂停 |
| transport.next | textButton | Studio、Listening 必需 | 下一曲 |
| transport.seek | linearSlider | Studio、Listening 必需 | 播放位置 |
| transport.time | label | Studio、Listening 必需 | 当前时间与总时长 |
| transport.shuffle | toggleButton | 可选 | 随机播放 |
| transport.repeat | comboBox | 可选 | Repeat Off、All、One |
| transport.volumeLabel | label | 可选 | VOLUME |
| transport.volumeValue | label | 可选 | 格式化音量 |
| transport.volume | slider | Studio、Listening 必需 | 主输出音量 |

transport.volume 允许 presentation：

- linearHorizontal
- rotary

transport.seek 仅允许 linearHorizontal。皮肤不能改变值域或步进。

### 10.6 效果链

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| effects.open | textButton | 可选 | 打开 Effects 工作面板 |
| effects.close | textButton | Effects 必需 | 关闭 Effects 并返回当前工作区 |
| effects.summary | label | 可选 | 当前效果链摘要 |
| effects.title | label | 可选 | 面板标题 |
| effects.filterLabel | label | 可选 | 筛选标签 |
| effects.filter | textEditor | Effects 必需 | 插件名称筛选 |
| effects.catalog | comboBox | Effects 必需 | 可插入插件目录 |
| effects.add | textButton | Effects 必需 | 插入所选插件 |
| effects.chain | effectChain | Effects 必需 | 动态效果槽列表 |
| effects.empty | label | 可选 | 空效果链提示 |
| effects.outputLayout | comboBox | 可选 | 当前处理声道布局 |

effects.chain 内部动态槽使用固定 style slots：

- slot.surface
- slot.name
- slot.bypass
- slot.editor
- slot.remove
- slot.dragIndicator

皮肤不能直接实例化、删除或重排动态槽；这些行为仍由 effects.chain 组件处理。

### 10.7 输出显示

| ID | 类型 | 必需位置 | 行为或数据 |
|---|---|---|---|
| output.stage | outputStage | 可选 | 当前多声道峰值与扬声器标签 |

outputStage 提供：

- text.primary：当前曲目标题；没有曲目时为空。
- text.secondary：当前 CoreAudio 输出设备名称。
- meter.channelCount。
- meter.channels[n].index。
- meter.channels[n].label。
- meter.channels[n].peak，0 至 1。
- meter.channels[n].isLfe。
- output.layout：stereo、5.1、5.1.2 或 7.1.2。

### 10.8 Settings

| ID | 类型 | Settings 必需 | 行为或数据 |
|---|---|---|---|
| settings.title | label | 否 | 设置标题 |
| settings.close | textButton | 是 | 关闭 Settings |
| settings.audio.heading | label | 否 | Audio / Core Audio 分区标题 |
| settings.audio.device | comboBox | 是 | 选择 CoreAudio 输出设备 |
| settings.audio.sampleRate | comboBox | 是 | 选择设备采样率 |
| settings.audio.bufferSize | comboBox | 是 | 选择缓冲区 |
| settings.audio.channelLayout | comboBox | 是 | Stereo、5.1、5.1.2、7.1.2 |
| settings.audio.channelMap | channelMap | 否 | 只读查看逻辑声道到 CoreAudio 输出端口的顺序映射；端口不可在皮肤内重排 |
| settings.plugins.formats | label | 否 | VST3 与 AU 支持摘要 |
| settings.plugins.knownList | pluginList | 否 | 已发现插件 |
| settings.plugins.scanAll | textButton | 是 | 扫描标准 VST3/AU 位置 |
| settings.plugins.scanFolder | textButton | 是 | 选择额外 VST3 文件夹 |
| settings.plugins.cancel | textButton | 是 | 取消扫描 |
| settings.plugins.progress | progressBar | 是 | 扫描进度 |
| settings.skins.heading | label | 否 | Appearance / Skins 分区标题 |
| settings.skins.library | skinList | 是 | 已安装与内置皮肤 |
| settings.skins.import | textButton | 是 | 导入 .moonwake-skin |
| settings.skins.remove | textButton | 否 | 删除选中第三方皮肤 |
| settings.skins.preview | textButton | 否 | 预览选中皮肤 |
| settings.skins.apply | textButton | 是 | 应用预览皮肤 |
| settings.skins.cancelPreview | textButton | 是 | 恢复预览前皮肤；未预览时 disabled |
| settings.skins.reload | textButton | 否 | 开发目录 Reload |

内置皮肤的 remove 始终 disabled。音频设备选项由 CoreAudio 提供；皮肤不能添加虚假设备或改变设备配置值。

`settings.skins.library` 的 slots 语义固定如下：

- `row.surface`：单个皮肤卡片背景；可读取完整 `skin.*` Context。
- `row.name`、`row.author`、`row.version`：皮肤清单元数据。
- `row.status`：ACTIVE、PREVIEW、BUILT-IN 或 IMPORTED 等由应用提供的状态文字；皮肤只决定呈现。

皮肤库视口是紧凑、可滚动的卡片区域，不提供额外 header 或空白占位 slots。导入包与开发目录入口由相邻的公共按钮 Component IDs 提供，皮肤不应在列表内部复制这些动作。

皮肤不得根据 `skin.*` Context 改写按钮动作，也不得把 `row.status` 画成另一个可点击控制。应用保留选择、键盘滚动、预览、应用与删除行为。

### 10.9 Label 语义对齐

以下对齐是 Component Catalog 的 Skin API 1 公共约定。使用 `{ "$context": "text.align" }` 的 Label renderer 会收到：

| 对齐 | Component ID |
|---|---|
| right | app.deviceStatus、queue.count、transport.volumeLabel、transport.volumeValue |
| center | artwork.metadata、queue.empty、transport.time、effects.empty |
| left | 其余所有公开 label Component ID |

队列表格另有固定列语义：`header.text` 的 Duration（column 3）为 `right`，Title 与 Location 表头为 `left`；`cell.duration` 为 `right`，`cell.title` 与 `cell.location` 为 `left`。这些值通过同一个 `text.align` 上下文传给 slot label renderer。

应用内已绑定的公开 Component ID 以本表为准，即使内部 JUCE Label 临时使用了不同 justification。不是公开 Component ID 的原生 Label 才按其 JUCE justification 映射到相同九值集合。第三方 renderer 若声明字面 `align`，会明确覆盖上下文，既有皮肤因此不会被强制改变。

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

`queue.clear`、workspace 模式、Cancel Scan、Apply Preview 和 Cancel Preview 没有图标语义：它们依赖对象范围、模式名或结果性文字，改成纯图标会降低可理解性。

推荐的第三方实现把 textButton style 写成复合 entry，将它需要支持的 slot 映射到 surface renderer。例如一个 add 按钮 style 可以声明 `"icon.add": "surface.actionIcon"`。`surface.actionIcon` 的 Path layer 可把 `data` 绑定到 `{ "$context": "icon.path" }`，并用以下 transform 把标准 24 × 24 path 放进应用提供的 icon bounds：pivotX/pivotY 为 0，scaleX 为 `bounds.width / 24`，scaleY 为 `bounds.height / 24`，translateX/translateY 分别为 `bounds.x` 与 `bounds.y`。这是 Path `data` 唯一允许的动态来源；任意 `$expr` 或 `text.primary` 动态 path 会被拒绝，避免把业务文本当图形语法解析。应用提供的 fallback path 在 catalog 初始化时预编译，正常 paint 不重新解析路径。

皮肤不必使用 fallback path。它可以在 `icon.<semantic>` slot 使用自己的静态 Path/SVG，也可以在 textButton 主体的 Text layer 将 `icon.name` 交给自带 ligature icon font。无论哪种方式，都不能改变语义名。纯图标命中区至少 36 × 36，常规图形建议 16–20 px；leading 图标左侧建议保留 10 px，文字 frame 左 inset 建议至少 34 px。

是否显示可见文字由 renderer 决定，但按钮的真实 `text.primary`、title、tooltip、VoiceOver role 与动作始终由应用保留。第三方皮肤不得用 visual node 另造一个可点击图标来替代公开 TextButton。

## 11. Required Component Rules

Studio 和 Listening 每个最终布局必须且只能包含一次：

~~~text
library.add
queue.table
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

Settings 最终布局必须包含前一节标记为“是”的全部组件。

Effects 最终布局必须包含：

~~~text
effects.close
effects.filter
effects.catalog
effects.add
effects.chain
~~~

如果皮肤 extends builtin.foundation，编译器可以从基础入口继承完整布局。但一旦皮肤提供某个布局入口，该入口必须独立满足 required rules，不会自动把缺少的组件隐式插入到任意位置。

这样可以避免第三方布局看起来成功，实际却依靠不可预测的紧急浮层。

Skin API 1 不允许在同一布局中重复同一个 Component ID。需要在空队列和标题栏同时放置两个 Add Music 按钮属于未来的可复制 command-view 能力；当前版本应把唯一的 library.add 放在始终可访问的 Queue 标题区。

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

皮肤不得通过隐藏、透明度为 0 或零尺寸规避 required component。编译器会检查最终尺寸约束和静态可见性。

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

真实组件的 VoiceOver 名称、角色和值由应用提供，皮肤不能覆盖。Visual Node 默认且必须对辅助技术隐藏。

导入时会在 160 × 48 与 320 × 160 两个规范探针尺寸分别离屏绘制 unfocused/focused 状态，并比较最终像素；两个尺寸都必须产生变化。只改变被 `opacity: 0`、全透明填充、零宽描边或无效阴影遮蔽的属性不算可见焦点；仅在特殊尺寸成立的 focused 表达式也不能通过该检查。第三方作者应让焦点指示在两种探针尺寸及实际最小控件尺寸上都清晰可见。

## 16. Settings 信息架构

Settings 只应包含：

1. Audio Output
   - CoreAudio device
   - Sample rate
   - Buffer size
   - Channel layout
   - Optional channel map

2. Plug-ins
   - Supported formats
   - Known plug-ins
   - Scan standard locations
   - Scan VST3 folder
   - Cancel and progress

3. Skins
   - Installed library
   - Import, preview, apply, remove
   - Developer directory and reload

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

Import Package 成功不代表立即应用。Preview 创建临时 Skin Session，Apply Preview 持久化，Cancel Preview 恢复此前皮肤。预览期间播放器同时保护预览版本和此前已确认版本，不允许删除它们；为避免版本保留清理破坏 Cancel，预览期间也拒绝导入这两个 skin id 的另一版本。先 Apply Preview 或 Cancel Preview 再导入即可。

导入开始后应用不再读取用户选择的原文件或目录。即使原路径在验证期间被替换，编译与安装仍只使用同一份私有不可变快照。Reload Folder 每次都重新捕获快照，且始终是只预览；开发目录中的版本不能直接 Apply Preview，作者必须先通过 Import Package 正式导入。

播放器会用安装时记录的 SHA-256 发现意外的安装后内容变化，并拒绝链接、超长或畸形完整性记录。这个检查面向不受信任的声明式皮肤输入和意外文件损坏，不是本机账户入侵防护：拥有同一 macOS 用户文件系统写权限的恶意进程能够同时改写包和应用本地元数据，皮肤格式不承诺抵御该攻击者。

运行时错误：

- 单个可选 visual renderer 失败：隐藏该 visual，Skin Session 进入 degraded，并按 code/path 只记录一次 warning。
- required component renderer 失败：Skin Session 进入 fatal；SkinManager 在下一次消息线程调度中终止预览或回退 builtin.foundation。
- layout 产生非有限边界：终止当前 Skin Session。
- 资源在安装后丢失：回退继承资源；仍失败则 safe-default。

发生 fatal 回退时，SkinManager 在切换到 `builtin.foundation` 之前复制失败 Skin Session 的 selection、health 与完整 runtime report，作为 last failed session record 保留到下一次失败或宿主清除。该记录目前作为宿主内部诊断接口保留；回退后的 active session 不会覆盖失败证据。

每个 Preview 或 active skin 都有独立 Skin Session，拥有自己的资源缓存、动画、运行时报告和 health。Renderer 通过 fault sink 报告，不能在 paint 内直接替换 session。切换与回退不会停止音频，也不会重建 PlayerController。

meter 数据是 UI 读取的最多 10 个声道原子快照。皮肤只看到 SkinRenderContext 中的副本，不能访问 AudioEngine、callback buffer、锁或实时线程；复杂 renderer 只可能影响 UI 帧率，不会进入音频处理路径。

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

    ./build/MoonwakeSkinTool snapshot MyFirstSkin/ --workspace listening --size 1240x800 --output preview.png [--state representative|normal|hover|pressed|selected|focused|disabled] [--force]

导出当前应用的规范 Schema：

    ./build/MoonwakeSkinTool schema --output-dir SkinAPI1Schema/ [--force]

验证开发者指南全部 JSON fenced block，并编译完整示例：

    ./build/MoonwakeSkinTool verify-docs --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md --example docs/skin-api/examples/CompleteThirdParty/

所有命令非零退出表示失败。validate 在仅有 warning 时退出 0，可使用 --warnings-as-errors。

## 21. 发布前检查清单

- manifest 的 id、version、license 和 minAppVersion 正确。
- 四个布局入口均可通过验证。
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
- Preview 可以通过 Settings → Appearance / Skins → Cancel Preview 取消；fatal renderer 会自动回退随应用内置的 `builtin.foundation`。

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
