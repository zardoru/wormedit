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
