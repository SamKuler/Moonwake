if (NOT EXISTS "${INFO_PLIST}")
    message (FATAL_ERROR "Moonwake Info.plist was not built: ${INFO_PLIST}")
endif()

execute_process (
    COMMAND /usr/libexec/PlistBuddy -c
        "Print :CFBundleDocumentTypes:0:CFBundleTypeRole" "${INFO_PLIST}"
    OUTPUT_VARIABLE role
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE role_result)
execute_process (
    COMMAND /usr/libexec/PlistBuddy -c
        "Print :CFBundleDocumentTypes:0:LSHandlerRank" "${INFO_PLIST}"
    OUTPUT_VARIABLE rank
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE rank_result)

if (NOT role_result EQUAL 0 OR NOT role STREQUAL "Viewer")
    message (FATAL_ERROR "Expected document role Viewer, found '${role}'")
endif()
if (NOT rank_result EQUAL 0 OR NOT rank STREQUAL "Alternate")
    message (FATAL_ERROR "Expected handler rank Alternate, found '${rank}'")
endif()
