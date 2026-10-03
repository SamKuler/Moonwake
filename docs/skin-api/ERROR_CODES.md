# Moonwake Skin API 1 Error Codes

- 状态：Skin API 1 开发期稳定约定；兼容承诺从公开发布开始
- 格式：CATEGORY + 三位十进制编号
- 稳定性：同一 Skin API 内，已发布 code 不复用、不改变含义

每条诊断同时包含 severity、location、JSON Pointer、message 与 suggestion。包内容错误的 location 使用包内相对路径；工具、安装和 I/O 错误可能使用本地路径或逻辑对象标识。下表固定 code 的机器含义；message 可以改进措辞。

## Package

| Code | Severity | 含义 |
|---|---|---|
| PKG001 | error | 路径不是安全的包内相对路径 |
| PKG002 | error | 包含符号链接、硬链接或设备文件 |
| PKG003 | error | 压缩、展开、单文件或 JSON 大小超限 |
| PKG004 | error | ZIP entry 或路径/segment 数量、长度超限 |
| PKG005 | error | NFC 或 Unicode case-fold 后路径冲突 |
| PKG006 | error | 加密、多卷或数据描述不一致的 ZIP |
| PKG007 | error | 输入既不是目录也不是可读 .moonwake-skin |
| PKG008 | error | 包或 ZIP entry 不可读取/不可用，或者请求与当前已安装、活动或预览选择冲突 |
| PKG009 | error | 资产文件数量超限 |
| PKG010 | error / warning | 私有快照、注册表、安装、恢复或清理事务失败；仅清理与耐久性降级可以报告 warning |
| PKG011 | error | 快照 SHA-256 清单生成失败 |
| PKG012 | error | 安装前快照摘要发生变化 |

## Manifest and Compatibility

| Code | Severity | 含义 |
|---|---|---|
| MANIFEST001 | error | 必需字段缺失 |
| MANIFEST002 | error | 字段 JSON 类型错误 |
| MANIFEST003 | error | skin id 不符合反向域名格式 |
| MANIFEST004 | error | version 或 minAppVersion 不是合法 SemVer 2.0 |
| MANIFEST005 | error | entrypoint 缺失、重复或路径错误 |
| MANIFEST006 | error | capability 与实际入口不一致 |
| COMPAT001 | error | skinApi 不受支持 |
| COMPAT002 | error | minAppVersion 高于当前应用 |
| COMPAT003 | error | capability 未被当前应用识别 |
| COMPAT004 | error | 没有可用的显式 API 迁移路径 |

## Schema

| Code | Severity | 含义 |
|---|---|---|
| SCHEMA001 | error | 封闭对象出现未知字段 |
| SCHEMA002 | error | 字段类型不符合 Schema |
| SCHEMA003 | error | 枚举值未知 |
| SCHEMA004 | error | 数值、长度、数组或字符串限制失败 |
| SCHEMA005 | error | oneOf/条件 Schema 没有唯一匹配 |

## Tokens

| Code | Severity | 含义 |
|---|---|---|
| TOKEN001 | error | token group 未知 |
| TOKEN002 | error | token 值格式错误 |
| TOKEN003 | error | token 引用不存在 |
| TOKEN004 | error | token 引用形成循环 |
| TOKEN005 | error | token 引用类型不兼容 |
| TOKEN006 | error | gradient stop、radius 或坐标非法 |
| TOKEN007 | error | 字体 source 或 fallback 定义非法 |

## Expressions

| Code | Severity | 含义 |
|---|---|---|
| EXPR001 | error | 非法词法单元 |
| EXPR002 | error | 语法或优先级结构非法 |
| EXPR003 | error | context path 不属于 renderer target |
| EXPR004 | error | 函数未知 |
| EXPR005 | error | 函数参数数量错误 |
| EXPR006 | error | 静态类型错误或发生隐式转换 |
| EXPR007 | error | 256-token 预算超限 |
| EXPR008 | error | 表达式深度超限 |
| EXPR020 | runtime error | 计算产生非有限结果，属性使用默认值 |
| EXPR021 | runtime warning | 除数为零，结果使用 number 0 |

## Components

| Code | Severity | 含义 |
|---|---|---|
| COMPONENT001 | error | Component ID 未知 |
| COMPONENT002 | error | Component kind 与 renderer target 不匹配 |
| COMPONENT003 | error | singleton Component ID 在一个布局重复 |
| COMPONENT004 | error | required Component ID 缺失 |
| COMPONENT005 | error | composite slot 未知或 slot target 不匹配 |
| COMPONENT006 | error | presentation 未知或没有 target 映射 |
| COMPONENT007 | error | 运行时 binding 缺失、重复或 JUCE 类型错误 |

## Layout

| Code | Severity | 含义 |
|---|---|---|
| LAYOUT001 | error | node type 或字段组合非法 |
| LAYOUT002 | error | node id 重复 |
| LAYOUT003 | error | 32 层深度超限 |
| LAYOUT004 | error | 5000 声明节点预算超限 |
| LAYOUT005 | error | 长度、min/max 或 aspectRatio 非法 |
| LAYOUT006 | error | flex align/justify/grow/shrink 组合非法 |
| LAYOUT007 | error | grid track、位置或 span 非法 |
| LAYOUT008 | error | stack anchors 过约束 |
| LAYOUT009 | error | scroll child 或 direction 非法 |
| LAYOUT010 | error | breakpoint 范围非法，或在匹配的全部工作区中被先前分支完整覆盖 |
| LAYOUT011 | error | required component 静态隐藏、透明或零尺寸 |
| LAYOUT012 | error | focusOrder 缺失或重复 |
| LAYOUT013 | error | Component、容器或 ancillaryStyles 引用不存在或 target 不匹配 |
| LAYOUT014 | error | component presentation 非法 |
| LAYOUT015 | error | 任一交互 Component Node 的点击区域小于 28 × 28 |
| LAYOUT016 | error | min constraint 无法放入最低窗口 |
| LAYOUT017 | runtime error | resolved bounds 非有限、负数或越界 |

## Rendering

| Code | Severity | 含义 |
|---|---|---|
| RENDER001 | error | renderer target 未知 |
| RENDER002 | error | layer type 未知 |
| RENDER003 | error | layer id 重复 |
| RENDER004 | error | layer property 类型或组合非法，或 contentClip 未引用受支持列表 target 的根层 rect/roundedRect/ellipse |
| RENDER005 | error | context path 或结果类型非法 |
| RENDER006 | error | token 类型与绘制属性不兼容 |
| RENDER007 | error | asset path 非法 |
| RENDER008 | error | repeat.template 内再次出现 repeat |
| RENDER009 | error | repeat source 未知或不适用于当前 target，或 limit 不在该数据源允许的范围内：meter.channels 为 1–16、table.visibleColumns 为 1–3、output.channelLabels 为 1–16 |
| RENDER010 | error | 最坏展开绘制操作超过 5000 |
| RENDER011 | error | transition、from 或插值类型非法 |
| RENDER012 | error | variant property path 引用未知 layer/field |
| RENDER013 | error | SVG path data 或 command 预算非法 |
| RENDER014 | error | transform、gradient 或 tint 语义非法 |

## Assets

| Code | Severity | 含义 |
|---|---|---|
| ASSET001 | error | 引用资源不存在 |
| ASSET002 | error | 文件格式不支持 |
| ASSET003 | error | 单图尺寸或解码内存超限 |
| ASSET004 | error | 全包像素或 128 MiB 解码缓存超限 |
| ASSET005 | error | 字体解析失败且没有可用 fallback |
| ASSET006 | error | XML 含 DTD、entity 或解析失败 |
| ASSET007 | error | SVG 元素不在 allowlist |
| ASSET008 | error | SVG 属性不在 allowlist |
| ASSET009 | error | SVG 引用不是同文件 #id |
| ASSET010 | error | SVG DOM/path-command 预算超限 |
| ASSET011 | error | 阴影层或离屏像素预算超限 |

## Runtime

| Code | Severity | 含义 |
|---|---|---|
| RUNTIME001 | warning | 可选 visual 绘制失败，session degraded |
| RUNTIME002 | error | required renderer 失败，session fatal |
| RUNTIME003 | reserved | 为未来独立的 layout session 运行时解析诊断保留；当前实现使用 LAYOUT017 或 RUNTIME007 |
| RUNTIME004 | error | 已安装皮肤文件缺失或摘要错误 |
| RUNTIME005 | error | preview 创建或切换失败 |
| RUNTIME006 | reserved | 为未来显式的自动回退诊断保留；当前回退由 session 状态表示 |
| RUNTIME007 | error | SkinSession fault sink 不可用 |

## Tool

| Code | Severity | 含义 |
|---|---|---|
| TOOL001 | error | 命令或参数未知 |
| TOOL002 | error | 必需参数缺失 |
| TOOL003 | error | 输出已存在且未提供 --force |
| TOOL004 | error | 可重现打包或输出写入失败 |
| TOOL005 | error | 输入类型不适用于所请求的命令 |
| TOOL006 | error | 所请求的 workspace 在已编译皮肤中不可用 |
| TOOL007 | error | 输出路径不安全、为符号链接或位于输入树内 |
| TOOL008 | error | 开发者指南或完整文档示例验证失败 |

新增 code 只能追加，不能复用空洞。一个错误同时满足多个类别时，报告流水线最早阶段的根因 code，后续派生错误不重复报告。
