cmake_minimum_required(VERSION 3.22)

project(Moonwake VERSION 0.6.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    set(MOONWAKE_MAC_SOURCES Source/audio/MacEmbeddedArtworkReader.mm)
    set(MOONWAKE_MAC_APP_SOURCES Source/app/MacNowPlayingBridge.mm)
    set_source_files_properties(Source/app/MacNowPlayingBridge.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MOONWAKE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE")
if (NOT EXISTS "${MOONWAKE_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${MOONWAKE_JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

add_library(MoonwakeEbur128 STATIC ThirdParty/libebur128/ebur128.c)
target_include_directories(MoonwakeEbur128 PUBLIC ThirdParty/libebur128)
set_target_properties(MoonwakeEbur128 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

if (APPLE)
    # Keep JUCE's recommended warnings enabled for Moonwake sources while
    # silencing warnings that originate inside the vendored JUCE modules.
    set_property(SOURCE
        "${MOONWAKE_JUCE_DIR}/modules/juce_audio_processors_headless/juce_audio_processors_headless.mm"
        APPEND PROPERTY COMPILE_OPTIONS -Wno-deprecated-declarations)
    set_property(SOURCE
        "${MOONWAKE_JUCE_DIR}/modules/juce_audio_plugin_client/VST3/juce_VST3ManifestHelper.mm"
        APPEND PROPERTY COMPILE_OPTIONS -Wno-deprecated-declarations)
    set_property(SOURCE
        "${MOONWAKE_JUCE_DIR}/modules/juce_events/juce_events.mm"
        APPEND PROPERTY COMPILE_OPTIONS -Wno-implicit-int-float-conversion)
endif()

file(GLOB_RECURSE MOONWAKE_SKIN_RESOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Skins/*"
)
list(SORT MOONWAKE_SKIN_RESOURCE_FILES)
set(MOONWAKE_SKIN_RESOURCE_INDEX "")
foreach(resource_file IN LISTS MOONWAKE_SKIN_RESOURCE_FILES)
    file(RELATIVE_PATH resource_path
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Skins" "${resource_file}")
    string(APPEND MOONWAKE_SKIN_RESOURCE_INDEX "${resource_path}\n")
endforeach()
set(MOONWAKE_SKIN_RESOURCE_INDEX_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/MoonwakeSkinPaths.txt")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${MOONWAKE_SKIN_RESOURCE_INDEX_FILE}"
    "${MOONWAKE_SKIN_RESOURCE_INDEX}")

juce_add_binary_data(MoonwakeSkinResources
    NAMESPACE MoonwakeSkinResources
    HEADER_NAME MoonwakeSkinResources.h
    SOURCES
        "${MOONWAKE_SKIN_RESOURCE_INDEX_FILE}"
        ${MOONWAKE_SKIN_RESOURCE_FILES}
)
target_compile_definitions(MoonwakeSkinResources
    INTERFACE MOONWAKE_VERSION="${PROJECT_VERSION}"
)

juce_add_gui_app(MoonwakePluginScanner
    COMPANY_NAME "Sam Kuler"
    COMPANY_COPYRIGHT "Moonwake — by Sam Kuler"
    BUNDLE_ID "com.samkuler.moonwake.scanner"
    PRODUCT_NAME "MoonwakePluginScanner"
    VERSION "${PROJECT_VERSION}"
    BUILD_VERSION "${PROJECT_VERSION}"
    PLIST_TO_MERGE "<plist><dict><key>LSBackgroundOnly</key><true/></dict></plist>"
)

target_sources(MoonwakePluginScanner
    PRIVATE
        Source/plugins/PluginScannerMain.cpp
        Source/plugins/PluginScannerWorker.cpp
)

target_include_directories(MoonwakePluginScanner PRIVATE Source)
target_compile_definitions(MoonwakePluginScanner
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
)
target_link_libraries(MoonwakePluginScanner
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if (APPLE)
    target_link_libraries(MoonwakePluginScanner PRIVATE "-framework CoreAudioKit")
endif()

juce_add_plugin(MoonwakeHostingFixture
    COMPANY_NAME "Sam Kuler"
    COMPANY_COPYRIGHT "Moonwake — by Sam Kuler"
    BUNDLE_ID "com.samkuler.moonwake.hosting-fixture"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Smkl
    PLUGIN_CODE Mwfx
    FORMATS VST3
    PRODUCT_NAME "Moonwake Hosting Fixture"
)

target_sources(MoonwakeHostingFixture
    PRIVATE
        Tests/fixtures/plugins/MoonwakeHostingFixtureProcessor.cpp
        Tests/fixtures/plugins/MoonwakeHostingFixtureProcessor.h
)
target_include_directories(MoonwakeHostingFixture PRIVATE Tests/fixtures/plugins)
target_compile_definitions(MoonwakeHostingFixture
    PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(MoonwakeHostingFixture
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
)

set(MOONWAKE_SOURCES
    ${MOONWAKE_MAC_SOURCES}
    Source/app/ProductPaths.cpp
    Source/app/PlaybackCoordinator.cpp
    Source/app/PlaybackCheckpoint.cpp
    Source/app/PlayerController.cpp
    Source/app/AutoSaveScheduler.cpp
    Source/app/NowPlayingCoordinator.cpp
    Source/audio/AudioEngine.cpp
    Source/audio/OutputMeter.cpp
    Source/audio/GaplessAudioSource.cpp
    Source/audio/ChannelRouter.cpp
    Source/audio/ChannelMappedAudioSource.cpp
    Source/audio/EffectGraph.cpp
    Source/audio/OutputLayout.cpp
    Source/audio/TrackMetadataReader.cpp
    Source/library/ArtworkExtractor.cpp
    Source/library/ArtworkStore.cpp
    Source/library/LibraryFilter.cpp
    Source/library/LibraryImportService.cpp
    Source/library/LibraryRestore.cpp
    Source/playlist/TrackFileIdentity.cpp
    Source/playlist/PlaylistModel.cpp
    Source/plugins/EffectChainState.cpp
    Source/plugins/PluginFilter.cpp
    Source/plugins/PluginManager.cpp
    Source/plugins/PluginScanProcess.cpp
    Source/state/AppStateStore.cpp
    Source/state/StateSaveService.cpp
    Source/state/StateRestoreService.cpp
    Source/skin/SkinCompatibilityChecker.cpp
    Source/skin/SkinBuiltInPackages.cpp
    Source/skin/SkinCompiler.cpp
    Source/skin/SkinInstaller.cpp
    Source/skin/SkinManager.cpp
    Source/skin/SkinRegistry.cpp
    Source/skin/SkinSession.cpp
    Source/skin/PlayerComponentCatalog.cpp
    Source/skin/SkinComponentBindings.cpp
    Source/skin/SkinDiagnostic.cpp
    Source/skin/SkinExpression.cpp
    Source/skin/SkinLayoutCompiler.cpp
    Source/skin/SkinLayoutDefinition.cpp
    Source/skin/SkinLayoutEngine.cpp
    Source/skin/SkinManifest.cpp
    Source/skin/SkinPackage.cpp
    Source/skin/SkinAssetCache.cpp
    Source/skin/SkinFontResolver.cpp
    Source/skin/SkinAnimationEngine.cpp
    Source/skin/SkinRenderCompiler.cpp
    Source/skin/SkinRowLayout.cpp
    Source/skin/SkinControlStyle.cpp
    Source/skin/SkinRenderDefinition.cpp
    Source/skin/SkinRenderer.cpp
    Source/skin/SkinSchemaRegistry.cpp
    Source/skin/SkinSchemaValidator.cpp
    Source/skin/SkinTokenStore.cpp
    Source/skin/SkinValue.cpp
    Source/ui/EffectRackLayout.cpp
    Source/ui/PluginFilterControls.cpp
    Source/ui/JuceSkinAdapter.cpp
    Source/ui/SkinEffectChainViewport.cpp
    Source/ui/SkinLayoutHost.cpp
    Source/ui/SkinTableListBox.cpp
    Source/ui/settings/AudioSettingsPanel.cpp
    Source/ui/settings/PluginSettingsPanel.cpp
    Source/ui/settings/SkinLibraryPanel.cpp
    Source/ui/AlbumArtworkComponent.cpp
    Source/ui/MainComponent.cpp
    Source/ui/OutputMixerComponent.cpp
    Source/ui/OutputStageComponent.cpp
    Source/ui/SkinFallbackDiagnostic.cpp
)

juce_add_gui_app(Moonwake
    COMPANY_NAME "Sam Kuler"
    COMPANY_COPYRIGHT "Moonwake — by Sam Kuler"
    BUNDLE_ID "com.samkuler.moonwake"
    PRODUCT_NAME "Moonwake"
    VERSION "${PROJECT_VERSION}"
    BUILD_VERSION "${PROJECT_VERSION}"
    DOCUMENT_EXTENSIONS wav aiff aif mp3 flac m4a aac alac caf ogg
)

target_sources(Moonwake
    PRIVATE
        Source/Main.cpp
        ${MOONWAKE_SOURCES}
        ${MOONWAKE_MAC_APP_SOURCES}
)

target_include_directories(Moonwake
    PRIVATE
        Source
)

target_compile_definitions(Moonwake
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(Moonwake
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_gui_extra
        MoonwakeSkinResources
        MoonwakeEbur128
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_dependencies(Moonwake MoonwakePluginScanner)

if (APPLE)
    set_property(TARGET Moonwake APPEND PROPERTY LINK_DEPENDS
        "$<TARGET_FILE:MoonwakePluginScanner>")
    target_sources(Moonwake PRIVATE ThirdParty/libebur128/COPYING)
    set_source_files_properties(ThirdParty/libebur128/COPYING PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/licenses/libebur128")
    target_link_libraries(Moonwake PRIVATE
        "-framework AudioToolbox"
        "-framework AppKit"
        "-framework AVFoundation"
        "-framework CoreAudioKit"
        "-framework MediaPlayer"
    )
    option(MOONWAKE_ADHOC_SIGN_LOCAL_BUILDS
        "Ad-hoc sign local Moonwake bundles after embedding the scanner helper" ON)
    add_custom_command(TARGET Moonwake POST_BUILD
        COMMAND /usr/libexec/PlistBuddy -c
            "Set :CFBundleDocumentTypes:0:CFBundleTypeRole Viewer"
            "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Info.plist"
        COMMAND /usr/libexec/PlistBuddy -c
            "Set :CFBundleDocumentTypes:0:LSHandlerRank Alternate"
            "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Helpers/MoonwakePluginScanner.app"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:MoonwakePluginScanner>"
            "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Helpers/MoonwakePluginScanner.app"
        VERBATIM
    )
    if (MOONWAKE_ADHOC_SIGN_LOCAL_BUILDS)
        add_custom_command(TARGET Moonwake POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Helpers/MoonwakePluginScanner.app"
            COMMAND /usr/bin/codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:Moonwake>"
            VERBATIM
        )
    endif()
endif()

add_executable(MoonwakeTests
        ${MOONWAKE_MAC_SOURCES}
        Source/app/ProductPaths.cpp
        Source/app/PlaybackCoordinator.cpp
        Source/app/PlaybackCheckpoint.cpp
        Source/app/PlayerController.cpp
        Source/audio/AudioEngine.cpp
        Source/audio/OutputMeter.cpp
        Source/audio/GaplessAudioSource.cpp
        Source/audio/TrackMetadataReader.cpp
        Source/library/ArtworkExtractor.cpp
        Source/library/ArtworkStore.cpp
        Source/library/LibraryFilter.cpp
        Source/library/LibraryImportService.cpp
        Source/library/LibraryRestore.cpp
        Source/audio/ChannelRouter.cpp
        Source/audio/ChannelMappedAudioSource.cpp
        Source/audio/EffectGraph.cpp
        Source/playlist/TrackFileIdentity.cpp
        Source/playlist/PlaylistModel.cpp
        Source/plugins/EffectChainState.cpp
        Source/plugins/PluginFilter.cpp
        Source/plugins/PluginManager.cpp
        Source/plugins/PluginScanProcess.cpp
        Source/app/AutoSaveScheduler.cpp
        Source/app/NowPlayingCoordinator.cpp
        Source/audio/OutputLayout.cpp
        Source/state/AppStateStore.cpp
        Source/state/StateSaveService.cpp
        Source/state/StateRestoreService.cpp
        Source/skin/SkinCompatibilityChecker.cpp
        Source/skin/SkinBuiltInPackages.cpp
        Source/skin/SkinCompiler.cpp
        Source/skin/SkinInstaller.cpp
        Source/skin/SkinManager.cpp
        Source/skin/SkinRegistry.cpp
        Source/skin/SkinSession.cpp
        Source/skin/PlayerComponentCatalog.cpp
        Source/skin/SkinComponentBindings.cpp
        Source/skin/SkinDiagnostic.cpp
        Source/skin/SkinExpression.cpp
        Source/skin/SkinLayoutCompiler.cpp
        Source/skin/SkinLayoutDefinition.cpp
        Source/skin/SkinLayoutEngine.cpp
        Source/skin/SkinManifest.cpp
        Source/skin/SkinPackage.cpp
        Source/skin/SkinAssetCache.cpp
        Source/skin/SkinFontResolver.cpp
        Source/skin/SkinAnimationEngine.cpp
        Source/skin/SkinRenderCompiler.cpp
        Source/skin/SkinRowLayout.cpp
        Source/skin/SkinControlStyle.cpp
        Source/skin/SkinRenderDefinition.cpp
        Source/skin/SkinRenderer.cpp
        Source/skin/SkinSchemaRegistry.cpp
        Source/skin/SkinSchemaValidator.cpp
        Source/skin/SkinTokenStore.cpp
        Source/skin/SkinToolCommands.cpp
        Source/skin/SkinValue.cpp
        Source/ui/EffectRackLayout.cpp
        Source/ui/PluginFilterControls.cpp
        Source/ui/JuceSkinAdapter.cpp
        Source/ui/SkinEffectChainViewport.cpp
        Source/ui/SkinLayoutHost.cpp
        Source/ui/SkinTableListBox.cpp
        Source/ui/settings/AudioSettingsPanel.cpp
        Source/ui/settings/PluginSettingsPanel.cpp
        Source/ui/settings/SkinLibraryPanel.cpp
        Source/ui/AlbumArtworkComponent.cpp
        Source/ui/OutputMixerComponent.cpp
        Source/ui/OutputStageComponent.cpp
        Source/ui/SkinFallbackDiagnostic.cpp
        Tests/TestMain.cpp
        Tests/AppStateStoreTests.cpp
        Tests/AudioEngineTests.cpp
        Tests/QueuePublicationTests.cpp
        Tests/OutputMeterTests.cpp
        Tests/AudioDeviceSettingsTests.cpp
        Tests/ChannelRouterTests.cpp
        Tests/ChannelMappedAudioSourceTests.cpp
        Tests/EffectChainStateTests.cpp
        Tests/EffectGraphTests.cpp
        Tests/EffectRackLayoutTests.cpp
        Tests/PlaybackCoordinatorTests.cpp
        Tests/PlaybackCheckpointTests.cpp
        Tests/PlayerComponentCatalogTests.cpp
        Tests/OutputLayoutTests.cpp
        Tests/PlaylistModelTests.cpp
        Tests/PluginManagerTests.cpp
        Tests/PluginFormatIntegrationTests.cpp
        Tests/ProductIdentityTests.cpp
        Tests/SkinCompatibilityTests.cpp
        Tests/SkinIntegrationTests.cpp
        Tests/SkinLayoutHostTests.cpp
        Tests/SkinManagerTests.cpp
        Tests/SkinDiagnosticTests.cpp
        Tests/SkinExpressionTests.cpp
        Tests/SkinFontResolverTests.cpp
        Tests/SkinManifestTests.cpp
        Tests/SkinLayoutCompilerTests.cpp
        Tests/SkinLayoutEngineTests.cpp
        Tests/SkinPackageTests.cpp
        Tests/SkinRenderCompilerTests.cpp
        Tests/SkinRendererTests.cpp
        Tests/SkinTextLayoutTests.cpp
        Tests/OutputStageTests.cpp
        Tests/OutputMixerTests.cpp
        Tests/SkinTableListBoxTests.cpp
        Tests/SettingsPanelTests.cpp
        Tests/SkinVisualIsolationTests.cpp
        Tests/SkinTokenStoreTests.cpp
        Tests/SkinToolTests.cpp
        Tests/TrackMetadataReaderTests.cpp
        Tests/ArtworkExtractorTests.cpp
        Tests/AutoSaveSchedulerTests.cpp
        Tests/StateSaveServiceTests.cpp
        Tests/StateRestoreServiceTests.cpp
        Tests/LibraryRestoreTests.cpp
        Tests/ArtworkStoreTests.cpp
        Tests/LibraryFilterTests.cpp
        Tests/LibraryImportServiceTests.cpp
        Tests/NowPlayingCoordinatorTests.cpp
)

target_include_directories(MoonwakeTests
    PRIVATE
        Source
)

target_compile_definitions(MoonwakeTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        MOONWAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_ENABLE_ALLOCATION_HOOKS=1
        MOONWAKE_PLUGIN_SCANNER_PATH="$<TARGET_FILE:MoonwakePluginScanner>"
)

get_target_property(MOONWAKE_TEST_VST3_PATH
    MoonwakeHostingFixture_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
target_compile_definitions(MoonwakeTests
    PRIVATE
        JUCE_USE_MP3AUDIOFORMAT=1
        MOONWAKE_TEST_VST3_PATH="${MOONWAKE_TEST_VST3_PATH}"
)

add_dependencies(MoonwakeTests
    MoonwakePluginScanner
    MoonwakeHostingFixture_VST3
)

target_link_libraries(MoonwakeTests
    PRIVATE
        juce::juce_core
        juce::juce_cryptography
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_events
        juce::juce_gui_basics
        MoonwakeSkinResources
        MoonwakeEbur128
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if (APPLE)
    target_link_libraries(MoonwakeTests PRIVATE
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreAudioKit"
    )
endif()

add_test(NAME MoonwakeTests COMMAND MoonwakeTests)
set_tests_properties(MoonwakeTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if (APPLE AND MOONWAKE_ADHOC_SIGN_LOCAL_BUILDS)
    add_test(NAME MoonwakeBundleSignature
        COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2
            "$<TARGET_BUNDLE_DIR:Moonwake>")
endif()
if (APPLE)
    add_test(NAME MoonwakeDocumentRole
        COMMAND ${CMAKE_COMMAND}
            -DINFO_PLIST=$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Info.plist
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyBundleDocumentRole.cmake)
endif()

add_executable(MoonwakeSkinTool
    Tools/SkinToolMain.cpp
    Source/skin/SkinCompatibilityChecker.cpp
    Source/skin/SkinBuiltInPackages.cpp
    Source/skin/SkinCompiler.cpp
    Source/skin/PlayerComponentCatalog.cpp
    Source/skin/SkinDiagnostic.cpp
    Source/skin/SkinExpression.cpp
    Source/skin/SkinLayoutCompiler.cpp
    Source/skin/SkinLayoutDefinition.cpp
    Source/skin/SkinLayoutEngine.cpp
    Source/skin/SkinManifest.cpp
    Source/skin/SkinPackage.cpp
    Source/skin/SkinAssetCache.cpp
    Source/skin/SkinFontResolver.cpp
    Source/skin/SkinAnimationEngine.cpp
    Source/skin/SkinRenderCompiler.cpp
    Source/skin/SkinRowLayout.cpp
    Source/skin/SkinControlStyle.cpp
    Source/skin/SkinRenderDefinition.cpp
    Source/skin/SkinRenderer.cpp
    Source/skin/SkinSchemaRegistry.cpp
    Source/skin/SkinSchemaValidator.cpp
    Source/skin/SkinTokenStore.cpp
    Source/skin/SkinToolCommands.cpp
    Source/skin/SkinValue.cpp
)

target_include_directories(MoonwakeSkinTool PRIVATE Source)

target_compile_definitions(MoonwakeSkinTool
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(MoonwakeSkinTool
    PRIVATE
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_events
        juce::juce_gui_basics
        MoonwakeSkinResources
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
