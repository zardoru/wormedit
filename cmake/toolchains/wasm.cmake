# Resolve either an activated emsdk or an installed Emscripten (for example Debian's).
find_program(WORMEDIT_EMCC NAMES emcc emcc.py
    HINTS "$ENV{EMSDK}/upstream/emscripten" REQUIRED)
get_filename_component(WORMEDIT_EMCC_REAL "${WORMEDIT_EMCC}" REALPATH)
get_filename_component(WORMEDIT_EMSCRIPTEN_ROOT "${WORMEDIT_EMCC_REAL}" DIRECTORY)
include("${WORMEDIT_EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
