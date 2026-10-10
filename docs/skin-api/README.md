# Moonwake Skin API 1

这里是 Moonwake 第三方皮肤 SDK 的公开入口。

- [开发者指南](SKIN_DEVELOPER_GUIDE.md)：完整接口、组件目录、布局、renderer、token、打包和兼容性约定。
- [错误码参考](ERROR_CODES.md)：验证、编译、资源、布局和安装阶段的稳定诊断码。
- [JSON Schema](schema/)：由生产代码中的 `SkinSchemaRegistry` 生成的机器可读契约。
- [CompleteThirdParty](examples/CompleteThirdParty/)：包含四个基础入口、曲库总览、集合详情、全屏歌词和迷你播放器，可直接验证的第三方皮肤示例。

## 验证 SDK

先在仓库根目录完成 Moonwake 构建，然后执行：

```sh
./build/MoonwakeSkinTool schema --output-dir docs/skin-api/schema --force
./build/MoonwakeSkinTool validate docs/skin-api/examples/CompleteThirdParty
./build/MoonwakeSkinTool verify-docs \
  --guide docs/skin-api/SKIN_DEVELOPER_GUIDE.md \
  --example docs/skin-api/examples/CompleteThirdParty
```

Skin API 1 当前是开发期稳定契约。公开发布后，应用可以修正文档、验证器和实现缺陷，但不得在不提升 Skin API 主版本的情况下破坏已发布字段、组件 ID 或语义。

每份 Schema 的 `$id` 都使用固定的标准 UUID URN。它们是无需联网解析的规范标识，不是下载地址；验证器应使用本目录中的 Schema 文件或由 `MoonwakeSkinTool schema` 生成的副本。已发布的 UUID 不得复用到不同 Schema。

## 许可

Copyright © 2026 Sam Kuler。

- 本目录中的说明文档，包括开发者指南和错误码参考，使用 [CC BY 4.0](LICENSE)。转载或改编时应署名 “Moonwake Skin API — by Sam Kuler”，并注明是否作出修改。
- [`schema/`](schema/) 中的机器可读 JSON Schema 使用 [MIT](schema/LICENSE)。
- [`examples/CompleteThirdParty/`](examples/CompleteThirdParty/) 示例包使用 [MIT](examples/CompleteThirdParty/LICENSE)。

这些许可只覆盖 Moonwake 提供的文档、Schema 和示例，不替第三方皮肤作者选择其作品的许可证。
