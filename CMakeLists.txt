cmake_minimum_required(VERSION 3.22)

project(Moonwake VERSION 0.11.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    set(MOONWAKE_MAC_SOURCES Source/library/MacEmbeddedArtworkReader.mm)
    set(MOONWAKE_NOW_PLAYING_SOURCE Source/app/MacNowPlayingBridge.mm)
    set(MOONWAKE_MAC_APP_SOURCES Source/ui/PlayerWindowMac.mm)
    set_source_files_properties(Source/app/MacNowPlayingBridge.mm Source/ui/PlayerWindowMac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
elseif (WIN32)
    set(MOONWAKE_NOW_PLAYING_SOURCE Source/app/WindowsNowPlayingBridge.cpp)
else()
    set(MOONWAKE_NOW_PLAYING_SOURCE Source/app/NullNowPlayingBridge.cpp)
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files and narrow string literals use UTF-8 regardless of the Windows code page.
if (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
endif()

set(MOONWAKE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE")
if (NOT EXISTS "${MOONWAKE_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${MOONWAKE_JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

add_library(MoonwakeEbur128 STATIC ThirdParty/libebur128/ebur128.c)
target_include_directories(MoonwakeEbur128 PUBLIC ThirdParty/libebur128)
set_target_properties(MoonwakeEbur128 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
if (WIN32)
    target_include_directories(MoonwakeEbur128 SYSTEM PRIVATE ThirdParty/libebur128/queue)
endif()
if (MSVC)
    target_compile_definitions(MoonwakeEbur128 PRIVATE _USE_MATH_DEFINES)
endif()

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
        JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
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
    ${MOONWAKE_NOW_PLAYING_SOURCE}
    Source/app/ProductPaths.cpp
    Source/app/PlaybackCoordinator.cpp
    Source/app/PlaybackCheckpoint.cpp
    Source/app/PlayerController.cpp
    Source/app/PlayerLoudness.cpp
    Source/app/PlayerLibraryActions.cpp
    Source/app/PlayerMusicDirectories.cpp
    Source/app/PlayerPresets.cpp
    Source/app/AutoSaveScheduler.cpp
    Source/app/NowPlayingCoordinator.cpp
    Source/audio/AudioEngine.cpp
    Source/audio/OutputMeter.cpp
    Source/audio/LoudnessNormalization.cpp
    Source/audio/LoudnessComparison.cpp
    Source/audio/OutputCalibrationProcessor.cpp
    Source/audio/GaplessAudioSource.cpp
    Source/audio/ChannelRouter.cpp
    Source/audio/ChannelMappedAudioSource.cpp
    Source/audio/EffectGraph.cpp
    Source/audio/OutputLayout.cpp
    Source/lyrics/LyricsDocument.cpp
    Source/lyrics/LyricsService.cpp
    Source/library/TrackTagReader.cpp
    Source/library/TrackMetadataReader.cpp
    Source/library/LoudnessScanService.cpp
    Source/library/ArtworkExtractor.cpp
    Source/library/ArtworkStore.cpp
    Source/library/LibraryProjection.cpp
    Source/library/MusicLibrary.cpp
    Source/library/SmartPlaylistRules.cpp
    Source/library/MusicDirectoryScanService.cpp
    Source/playlist/PlaylistFile.cpp
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
    Source/state/ListeningPresets.cpp
    Source/state/ListeningPresetStore.cpp
    Source/state/EffectChainSerialization.cpp
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
    Source/ui/MusicLibraryPanel.cpp
    Source/ui/SmartPlaylistEditor.cpp
    Source/ui/settings/MusicDirectoriesPanel.cpp
    Source/ui/PlaylistGridComponent.cpp
    Source/ui/SkinTableListBox.cpp
    Source/ui/settings/AudioSettingsPanel.cpp
    Source/ui/settings/PluginSettingsPanel.cpp
    Source/ui/settings/SkinLibraryPanel.cpp
    Source/ui/AlbumArtworkComponent.cpp
    Source/ui/MainComponent.cpp
    Source/ui/MainListening.cpp
    Source/ui/PlayerWindow.cpp
    Source/ui/OutputMixerComponent.cpp
    Source/ui/OutputCalibrationComponent.cpp
    Source/ui/PresetControls.cpp
    Source/ui/LoudnessControls.cpp
    Source/ui/LyricsComponent.cpp
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
    PLIST_TO_MERGE [=[<plist><dict>
        <key>CFBundleDocumentTypes</key>
        <array><dict>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>wav</string><string>aiff</string><string>aif</string>
                <string>mp3</string><string>flac</string><string>m4a</string>
                <string>aac</string><string>alac</string><string>caf</string><string>ogg</string>
            </array>
            <key>CFBundleTypeName</key><string>Audio files</string>
            <key>CFBundleTypeRole</key><string>Viewer</string>
            <key>LSHandlerRank</key><string>Alternate</string>
        </dict></array>
    </dict></plist>]=]
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
        JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
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
    get_target_property(MOONWAKE_INFO_PLIST Moonwake MACOSX_BUNDLE_INFO_PLIST)
    get_target_property(MOONWAKE_SCANNER_INFO_PLIST MoonwakePluginScanner MACOSX_BUNDLE_INFO_PLIST)
    # Bundle metadata changes must also re-embed the helper and refresh both signatures.
    set_property(TARGET Moonwake APPEND PROPERTY LINK_DEPENDS
        "$<TARGET_FILE:MoonwakePluginScanner>"
        "${MOONWAKE_INFO_PLIST}"
        "${MOONWAKE_SCANNER_INFO_PLIST}")
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
elseif (WIN32)
    add_custom_target(MoonwakeDeployScanner
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Moonwake>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MoonwakePluginScanner>"
            "$<TARGET_FILE_DIR:Moonwake>/$<TARGET_FILE_NAME:MoonwakePluginScanner>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Moonwake>/licenses/libebur128"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libebur128/COPYING"
            "$<TARGET_FILE_DIR:Moonwake>/licenses/libebur128/COPYING"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libebur128/queue/COPYING"
            "$<TARGET_FILE_DIR:Moonwake>/licenses/libebur128/queue-COPYING"
        DEPENDS MoonwakePluginScanner
        VERBATIM
    )
    add_dependencies(Moonwake MoonwakeDeployScanner)
endif()

add_executable(MoonwakeTests
        ${MOONWAKE_MAC_SOURCES}
        ${MOONWAKE_NOW_PLAYING_SOURCE}
        Source/app/ProductPaths.cpp
        Source/app/PlaybackCoordinator.cpp
        Source/app/PlaybackCheckpoint.cpp
        Source/app/PlayerController.cpp
        Source/app/PlayerLoudness.cpp
        Source/app/PlayerLibraryActions.cpp
        Source/app/PlayerMusicDirectories.cpp
        Source/app/PlayerPresets.cpp
        Source/audio/AudioEngine.cpp
        Source/audio/OutputMeter.cpp
        Source/audio/LoudnessNormalization.cpp
        Source/audio/LoudnessComparison.cpp
        Source/audio/OutputCalibrationProcessor.cpp
        Source/audio/GaplessAudioSource.cpp
        Source/lyrics/LyricsDocument.cpp
        Source/lyrics/LyricsService.cpp
        Source/library/TrackTagReader.cpp
        Source/library/TrackMetadataReader.cpp
        Source/library/LoudnessScanService.cpp
        Source/library/ArtworkExtractor.cpp
        Source/library/ArtworkStore.cpp
        Source/library/LibraryProjection.cpp
        Source/library/MusicLibrary.cpp
        Source/library/SmartPlaylistRules.cpp
        Source/library/MusicDirectoryScanService.cpp
        Source/playlist/PlaylistFile.cpp
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
        Source/state/ListeningPresets.cpp
        Source/state/ListeningPresetStore.cpp
        Source/state/EffectChainSerialization.cpp
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
        Source/ui/MusicLibraryPanel.cpp
        Source/ui/SmartPlaylistEditor.cpp
        Source/ui/settings/MusicDirectoriesPanel.cpp
        Source/ui/PlaylistGridComponent.cpp
        Source/ui/SkinTableListBox.cpp
        Source/ui/settings/AudioSettingsPanel.cpp
        Source/ui/settings/PluginSettingsPanel.cpp
        Source/ui/settings/SkinLibraryPanel.cpp
        Source/ui/AlbumArtworkComponent.cpp
        Source/ui/OutputMixerComponent.cpp
        Source/ui/OutputCalibrationComponent.cpp
        Source/ui/PresetControls.cpp
        Source/ui/LoudnessControls.cpp
        Source/ui/LyricsComponent.cpp
        Source/ui/OutputStageComponent.cpp
        Source/ui/SkinFallbackDiagnostic.cpp
        Tests/TestMain.cpp
        Tests/LyricsDocumentTests.cpp
        Tests/LyricsEmbeddedTests.cpp
        Tests/LyricsServiceTests.cpp
        Tests/LyricsComponentTests.cpp
        Tests/ListeningPresetsTests.cpp
        Tests/PlayerPresetControllerTests.cpp
        Tests/PresetControlsTests.cpp
        Tests/OutputCalibrationTests.cpp
        Tests/OutputCalibrationComponentTests.cpp
        Tests/AppStateStoreTests.cpp
        Tests/AudioEngineTests.cpp
        Tests/QueuePublicationTests.cpp
        Tests/OutputMeterTests.cpp
        Tests/LoudnessTests.cpp
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
        Tests/PlayerLibraryTests.cpp
        Tests/MusicLibraryPanelTests.cpp
        Tests/SmartPlaylistEditorTests.cpp
        Tests/MusicDirectoriesPanelTests.cpp
        Tests/PlaylistGridTests.cpp
        Tests/PlayerEffectControllerTests.cpp
        Tests/SkinEffectChainViewportTests.cpp
        Tests/LibraryProjectionTests.cpp
        Tests/SkinEditableComboTests.cpp
        Tests/MusicLibraryTests.cpp
        Tests/SmartPlaylistRulesTests.cpp
        Tests/MusicDirectoryScanServiceTests.cpp
        Tests/PlaylistFileTests.cpp
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
        Tests/SkinContentClipTests.cpp
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
        JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
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

if (WIN32)
    # C++/WinRT and the desktop SMTC interop are provided by the Windows SDK.
    target_link_libraries(Moonwake PRIVATE windowsapp shcore propsys)
    target_link_libraries(MoonwakeTests PRIVATE windowsapp shcore propsys)
    target_sources(Moonwake PRIVATE Source/app/WindowsApplicationIdentity.cpp)
    target_sources(MoonwakeTests PRIVATE
        Source/app/WindowsApplicationIdentity.cpp
        Tests/WindowsNowPlayingBridgeTests.cpp)
endif()

if (APPLE)
    # Native popup menus require an application bundle and an active GUI host.
    set_target_properties(MoonwakeTests PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.samkuler.moonwake.tests"
        MACOSX_BUNDLE_BUNDLE_NAME "Moonwake Tests"
    )
    set_source_files_properties(Tests/TestMain.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(MoonwakeTests PRIVATE
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreAudioKit"
        "-framework MediaPlayer"
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
            -DGENERATED_INFO_PLIST=${MOONWAKE_INFO_PLIST}
            -DINFO_PLIST=$<TARGET_BUNDLE_DIR:Moonwake>/Contents/Info.plist
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyBundleDocumentRole.cmake)
endif()
if (WIN32)
    add_test(NAME MoonwakeScannerDeployment
        COMMAND ${CMAKE_COMMAND}
            -DSCANNER=$<TARGET_FILE_DIR:Moonwake>/$<TARGET_FILE_NAME:MoonwakePluginScanner>
            -DSOURCE_SCANNER=$<TARGET_FILE:MoonwakePluginScanner>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyScannerDeployment.cmake)
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

if (NOT APPLE)
    if (WIN32)
        # The system ICU library is available on Windows 10 version 1903 and later.
        set(MOONWAKE_UNICODE_LIBRARY icu)
    else()
        find_package(ICU REQUIRED COMPONENTS uc)
        set(MOONWAKE_UNICODE_LIBRARY ICU::uc)
    endif()
    foreach(target IN ITEMS Moonwake MoonwakeTests MoonwakeSkinTool)
        target_link_libraries(${target} PRIVATE ${MOONWAKE_UNICODE_LIBRARY})
    endforeach()
endif()
