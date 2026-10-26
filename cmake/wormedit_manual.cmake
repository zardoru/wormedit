# Builds docs/manual into HTML with Asciidoctor and installs it next to the program, so every
# package ships the user manual unless WORMEDIT_INSTALL_MANUAL is OFF. Without Asciidoctor a
# development build only warns, but `cmake --install` fails, so a package cannot silently omit it.
option(WORMEDIT_INSTALL_MANUAL "Build the HTML user manual with Asciidoctor and install it" ON)
if(NOT WORMEDIT_INSTALL_MANUAL)
    return()
endif()

# Ruby gems installed with `gem install --user-install` are often not on PATH.
file(GLOB wormedit_user_gem_bins "$ENV{HOME}/.local/share/gem/ruby/*/bin" "$ENV{HOME}/.gem/ruby/*/bin")
find_program(WORMEDIT_ASCIIDOCTOR asciidoctor HINTS ${wormedit_user_gem_bins})
if(NOT WORMEDIT_ASCIIDOCTOR)
    message(WARNING "Asciidoctor was not found, so the user manual cannot be built. Install it "
                    "(gem install asciidoctor) or configure with -DWORMEDIT_INSTALL_MANUAL=OFF.")
    install(CODE "message(FATAL_ERROR \"The user manual needs Asciidoctor; install it or configure with -DWORMEDIT_INSTALL_MANUAL=OFF.\")" COMPONENT Manual)
    return()
endif()

set(wormedit_manual_dir "${CMAKE_BINARY_DIR}/manual")
file(GLOB_RECURSE wormedit_manual_sources CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/docs/manual/*.adoc" "${CMAKE_SOURCE_DIR}/docs/manual/images/*.png"
    "${CMAKE_SOURCE_DIR}/docs/manual/fixtures/*")
# The packaged copy works offline: no web fonts or icon fonts from a CDN.
add_custom_command(
    OUTPUT "${wormedit_manual_dir}/index.html"
    COMMAND "${WORMEDIT_ASCIIDOCTOR}" --failure-level WARN -a webfonts! -a icons!
        -D "${wormedit_manual_dir}" "${CMAKE_SOURCE_DIR}/docs/manual/index.adoc"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/docs/manual/images"
        "${wormedit_manual_dir}/images"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/docs/manual/fixtures"
        "${wormedit_manual_dir}/fixtures"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${wormedit_manual_dir}/images/revisions.adoc"
    DEPENDS ${wormedit_manual_sources}
    COMMENT "Building the user manual"
    VERBATIM)
add_custom_target(manual ALL DEPENDS "${wormedit_manual_dir}/index.html")
install(FILES "${wormedit_manual_dir}/index.html" DESTINATION "${WORMEDIT_INSTALL_BINDIR}/manual" COMPONENT Manual)
install(DIRECTORY "${wormedit_manual_dir}/images" DESTINATION "${WORMEDIT_INSTALL_BINDIR}/manual" COMPONENT Manual)
install(DIRECTORY "${wormedit_manual_dir}/fixtures" DESTINATION "${WORMEDIT_INSTALL_BINDIR}/manual" COMPONENT Manual)

# Generate the same offline API book for Lua-enabled and Lua-disabled application builds.
include(${CMAKE_CURRENT_LIST_DIR}/dependencies/ldoc.cmake)
set(wormedit_lua_api_dir "${wormedit_manual_dir}/lua-api")
file(GLOB_RECURSE wormedit_lua_api_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/lua-api/*"
    "${wormedit_ldoc_SOURCE_DIR}/*.lua" "${wormedit_penlight_SOURCE_DIR}/lua/*.lua")
add_custom_command(OUTPUT "${wormedit_lua_api_dir}/index.html"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${wormedit_lua_api_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${wormedit_lua_api_dir}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy "${wormedit_ldoc_SOURCE_DIR}/COPYRIGHT" "${wormedit_lua_api_dir}/licenses/LDoc.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${wormedit_penlight_SOURCE_DIR}/LICENSE.md" "${wormedit_lua_api_dir}/licenses/Penlight.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${wormedit_lfs_SOURCE_DIR}/LICENSE" "${wormedit_lua_api_dir}/licenses/LuaFileSystem.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${lua_SOURCE_DIR}/lua.h" "${wormedit_lua_api_dir}/licenses/Lua-header-with-license.txt"
    COMMAND "${CMAKE_COMMAND}" -E env
        "LUA_PATH=${wormedit_penlight_SOURCE_DIR}/lua/?.lua;${wormedit_ldoc_SOURCE_DIR}/?.lua;${wormedit_ldoc_SOURCE_DIR}/?/init.lua"
        "${wormedit_ldoc_host}" "${wormedit_ldoc_SOURCE_DIR}/ldoc.lua"
        --fatalwarnings --config "${CMAKE_SOURCE_DIR}/docs/lua-api/config.ld"
        --dir "${wormedit_lua_api_dir}" "${CMAKE_SOURCE_DIR}/docs/lua-api"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/docs/lua-api/examples"
        "${wormedit_lua_api_dir}/sources"
    DEPENDS "${wormedit_ldoc_host}" ${wormedit_lua_api_sources}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/docs/lua-api"
    COMMENT "Building the offline Lua API reference with LDoc"
    VERBATIM)
add_custom_target(lua-api DEPENDS "${wormedit_lua_api_dir}/index.html")
add_dependencies(manual lua-api)
install(DIRECTORY "${wormedit_lua_api_dir}" DESTINATION "${WORMEDIT_INSTALL_BINDIR}/manual" COMPONENT Manual)

if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME documentation.lua_api_links
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check-lua-api-docs.py" "${wormedit_manual_dir}")
    set_tests_properties(documentation.lua_api_links PROPERTIES LABELS "unit;documentation")
endif()
