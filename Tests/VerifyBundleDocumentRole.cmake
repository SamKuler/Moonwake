foreach (plist IN ITEMS "${GENERATED_INFO_PLIST}" "${INFO_PLIST}")
    if (NOT EXISTS "${plist}")
        message (FATAL_ERROR "Moonwake Info.plist is missing: ${plist}")
    endif()

    execute_process (
        COMMAND /usr/libexec/PlistBuddy -c
            "Print :CFBundleDocumentTypes:0:CFBundleTypeRole" "${plist}"
        OUTPUT_VARIABLE role
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE role_result)
    execute_process (
        COMMAND /usr/libexec/PlistBuddy -c
            "Print :CFBundleDocumentTypes:0:LSHandlerRank" "${plist}"
        OUTPUT_VARIABLE rank
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rank_result)

    if (NOT role_result EQUAL 0 OR NOT role STREQUAL "Viewer")
        message (FATAL_ERROR "Expected document role Viewer in ${plist}, found '${role}'")
    endif()
    if (NOT rank_result EQUAL 0 OR NOT rank STREQUAL "Alternate")
        message (FATAL_ERROR "Expected handler rank Alternate in ${plist}, found '${rank}'")
    endif()

    execute_process (
        COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleDocumentTypes:1" "${plist}"
        OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE extra_type_result)
    if (extra_type_result EQUAL 0)
        message (FATAL_ERROR "Unexpected additional document type in ${plist}")
    endif()
endforeach()
