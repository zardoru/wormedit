# The browser package stages only the web entry point, its runtime and notices.
function(wormedit_install_browser_package)
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME WormeditWasm)
    set(WORMEDIT_INSTALL_BINDIR ".")
    install(FILES
        "${CMAKE_BINARY_DIR}/index.html"
        "$<TARGET_FILE:wormedit>"
        "${CMAKE_BINARY_DIR}/wormedit.wasm"
        "${CMAKE_BINARY_DIR}/wormedit.data"
        "${CMAKE_BINARY_DIR}/editor-files.js"
        "${CMAKE_BINARY_DIR}/editor-assets.js"
        "${CMAKE_BINARY_DIR}/fflate.js"
        "${CMAKE_BINARY_DIR}/fflate-LICENSE.txt"
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/scripts/serve-wasm.py"
        DESTINATION ".")
    install(FILES "${CMAKE_BINARY_DIR}/wormedit.worker.js" DESTINATION "." OPTIONAL)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/wasm/README.md" DESTINATION ".")
    install(FILES "${WORMEDIT_EMSCRIPTEN_ROOT}/LICENSE" DESTINATION "third-party/emscripten")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/third-party-notices.cmake")
endfunction()

wormedit_install_browser_package()
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "wormedit-${WORMEDIT_VERSION_FULL}-wasm")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};WormeditWasm;/")
include(CPack)
