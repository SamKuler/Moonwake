if (NOT EXISTS "${SCANNER}")
    message(FATAL_ERROR "Plug-in scanner is missing beside the application: ${SCANNER}")
endif()

file(SHA256 "${SCANNER}" deployed_hash)
file(SHA256 "${SOURCE_SCANNER}" source_hash)
if (NOT deployed_hash STREQUAL source_hash)
    message(FATAL_ERROR "The deployed plug-in scanner does not match the current build")
endif()

execute_process(COMMAND "${SCANNER}" RESULT_VARIABLE result TIMEOUT 10)
if (NOT result STREQUAL "2")
    message(FATAL_ERROR "The deployed plug-in scanner did not reject an empty request: ${result}")
endif()
