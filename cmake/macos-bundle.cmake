# Kept behind the APPLE branch in CMakeLists.txt; also safe to include directly elsewhere.
if(NOT APPLE)
    return()
endif()

# Only application-owned installs belong in the package. Dependencies were configured
# earlier with their own default component; never install their SDK headers/libraries.
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Wormedit)
set(WORMEDIT_INSTALL_BINDIR "Wormedit.app/Contents/Resources")
# cmake/third-party-notices.cmake is included after this file, so its notices follow this
# override into Contents/Resources/third-party under the Wormedit component.

if(DEFINED WORMEDIT_VERSION_FULL)
    set(wormedit_macos_version "${WORMEDIT_VERSION_FULL}")
else()
    set(wormedit_macos_version "${PROJECT_VERSION}")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/macos-package-version.txt"
    CONTENT "${wormedit_macos_version}\n")

set(wormedit_macos_icon "${CMAKE_BINARY_DIR}/macos-icon/Wormedit.icns")
add_custom_command(OUTPUT "${wormedit_macos_icon}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/macos-icon"
    COMMAND xcrun swift "${CMAKE_SOURCE_DIR}/packaging/macos/generate-icon.swift"
        "${CMAKE_BINARY_DIR}/macos-icon/Wormedit.iconset"
    COMMAND iconutil -c icns "${CMAKE_BINARY_DIR}/macos-icon/Wormedit.iconset"
        -o "${wormedit_macos_icon}"
    DEPENDS "${CMAKE_SOURCE_DIR}/packaging/macos/generate-icon.swift"
    COMMENT "Generating replaceable macOS placeholder icon" VERBATIM)
set_source_files_properties("${wormedit_macos_icon}" PROPERTIES
    GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
target_sources(wormedit PRIVATE "${wormedit_macos_icon}")
set_target_properties(wormedit PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME Wormedit
    MACOSX_BUNDLE_BUNDLE_NAME Wormedit
    MACOSX_BUNDLE_GUI_IDENTIFIER org.wormedit.Wormedit
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE Wormedit.icns
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in")
# A development bundle uses the same SDL resource base as the installed bundle.
add_custom_command(TARGET wormedit POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_BINARY_DIR}/assets"
        "$<TARGET_BUNDLE_DIR:wormedit>/Contents/Resources/assets" VERBATIM)
install(TARGETS wormedit BUNDLE DESTINATION ".")

# BundleUtilities handles recursive dependencies AND install-name rewriting (including
# frameworks). RUNTIME_DEPENDENCY_SET only copies libraries; it cannot do the latter.
# Run explicitly after installation so all resources/notices exist before signing.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/macos-fixup.cmake" CONTENT "
include(BundleUtilities)
# Homebrew bottles can supply read-only dylibs; only copied bundle items are changed.
set(BU_CHMOD_BUNDLE_ITEMS TRUE)
fixup_bundle(\"\${WORMEDIT_APP}\" \"\" \"$<TARGET_FILE_DIR:SDL3::SDL3>;${WORMEDIT_FFMPEG_LIBRARY_DIRS}\")
")
