#pragma once

#ifndef MOONWAKE_VERSION
 #error MOONWAKE_VERSION must be supplied by the build system
#endif

namespace MoonwakeIdentity
{
inline constexpr auto productName = "Moonwake";
inline constexpr auto displayCredit = "Moonwake \xe2\x80\x94 by Sam Kuler";
inline constexpr auto authorName = "Sam Kuler";
inline constexpr auto appBundleId = "com.samkuler.moonwake";
inline constexpr auto scannerBundleId = "com.samkuler.moonwake.scanner";
inline constexpr auto version = MOONWAKE_VERSION;
inline constexpr auto skinApiName = "Moonwake Skin API";
inline constexpr int skinApiVersion = 1;
inline constexpr auto skinSchemaGenerator = "Moonwake SkinSchemaRegistry";
inline constexpr auto skinManifestSchemaUri = "urn:uuid:944f2c6f-b982-4265-8de2-5a1675c62e13";
inline constexpr auto skinTokensSchemaUri = "urn:uuid:c864e7d5-12f1-4c4e-90d9-efdcc26a3016";
inline constexpr auto skinLayoutSchemaUri = "urn:uuid:a26f96b8-98c9-4773-a0e3-a17fd295b84f";
inline constexpr auto skinComponentIndexSchemaUri = "urn:uuid:f37db386-b854-462a-a26f-b281c16b38fb";
inline constexpr auto skinRenderSchemaUri = "urn:uuid:f0c2d818-4a3f-4d3e-9c4a-c5142029e7ef";
inline constexpr auto skinArchiveExtension = ".moonwake-skin";
inline constexpr auto stateRootType = "MoonwakeState";
inline constexpr auto applicationSupportLeaf = "Moonwake";
}
