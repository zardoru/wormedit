# Browser transport only: MIT-licensed ZIP import/download, never a native parser replacement.
FetchContent_Declare(wormedit_fflate
    URL https://registry.npmjs.org/fflate/-/fflate-0.8.2.tgz
    URL_HASH SHA256=61fd5061e2fc8e5e3e3129f7f2fec7bd78a313e1bf4becbf1cc1cc9998d141dc
    SOURCE_SUBDIR __wormedit_source_only)
FetchContent_MakeAvailable(wormedit_fflate)
configure_file("${wormedit_fflate_SOURCE_DIR}/umd/index.js" "${CMAKE_BINARY_DIR}/fflate.js" COPYONLY)
configure_file("${wormedit_fflate_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/fflate-LICENSE.txt" COPYONLY)
