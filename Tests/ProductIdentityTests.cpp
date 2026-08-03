#include <juce_core/juce_core.h>

#include "app/ProductIdentity.h"
#include "app/ProductPaths.h"

namespace
{
class ProductIdentityTests final : public juce::UnitTest
{
public:
    ProductIdentityTests()
        : UnitTest ("ProductIdentity", "app")
    {
    }

    void runTest() override
    {
        beginTest ("publishes the frozen Moonwake identity contract");
        {
            expectEquals (juce::String (MoonwakeIdentity::productName),
                          juce::String ("Moonwake"));
            expectEquals (juce::String::fromUTF8 (MoonwakeIdentity::displayCredit),
                          juce::String::fromUTF8 ("Moonwake \xe2\x80\x94 by Sam Kuler"));
            expectEquals (juce::String (MoonwakeIdentity::authorName),
                          juce::String ("Sam Kuler"));
            expectEquals (juce::String (MoonwakeIdentity::appBundleId),
                          juce::String ("com.samkuler.moonwake"));
            expectEquals (juce::String (MoonwakeIdentity::scannerBundleId),
                          juce::String ("com.samkuler.moonwake.scanner"));
            expectEquals (juce::String (MoonwakeIdentity::version),
                          juce::String ("0.1.0"));
            expectEquals (juce::String (MoonwakeIdentity::skinApiName),
                          juce::String ("Moonwake Skin API"));
            expectEquals (MoonwakeIdentity::skinApiVersion, 1);
            expectEquals (juce::String (MoonwakeIdentity::skinSchemaGenerator),
                          juce::String ("Moonwake SkinSchemaRegistry"));
            expectEquals (juce::String (MoonwakeIdentity::skinManifestSchemaUri),
                          juce::String ("urn:uuid:944f2c6f-b982-4265-8de2-5a1675c62e13"));
            expectEquals (juce::String (MoonwakeIdentity::skinTokensSchemaUri),
                          juce::String ("urn:uuid:c864e7d5-12f1-4c4e-90d9-efdcc26a3016"));
            expectEquals (juce::String (MoonwakeIdentity::skinLayoutSchemaUri),
                          juce::String ("urn:uuid:a26f96b8-98c9-4773-a0e3-a17fd295b84f"));
            expectEquals (juce::String (MoonwakeIdentity::skinComponentIndexSchemaUri),
                          juce::String ("urn:uuid:f37db386-b854-462a-a26f-b281c16b38fb"));
            expectEquals (juce::String (MoonwakeIdentity::skinRenderSchemaUri),
                          juce::String ("urn:uuid:f0c2d818-4a3f-4d3e-9c4a-c5142029e7ef"));
            expectEquals (juce::String (MoonwakeIdentity::skinArchiveExtension),
                          juce::String (".moonwake-skin"));
            expectEquals (juce::String (MoonwakeIdentity::stateRootType),
                          juce::String ("MoonwakeState"));
            expectEquals (juce::String (MoonwakeIdentity::applicationSupportLeaf),
                          juce::String ("Moonwake"));
        }

        beginTest ("uses the macOS Application Support location");
        {
            const auto root = ProductPaths::applicationDataRoot();
#if JUCE_MAC
            expect (root.getFullPathName().endsWith (
                "Library/Application Support/Moonwake"));
#endif
            expect (ProductPaths::installedSkinsRoot().getParentDirectory() == root);
            expectEquals (ProductPaths::installedSkinsRoot().getFileName(),
                          juce::String ("Skins"));
        }
    }
};

ProductIdentityTests productIdentityTests;
}
