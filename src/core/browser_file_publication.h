#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cerrno>
#include <filesystem>

namespace wormedit::platform {
inline auto browser_exchange_directories(const std::filesystem::path& source,
                                         const std::filesystem::path& destination) -> bool {
    return MAIN_THREAD_EM_ASM_INT({
        const source = UTF8ToString($0);
        const destination = UTF8ToString($1);
        const parked = source + '.browser-swap';
        try {
            try { FS.lstat(parked); return 0; }
            catch (error) { if (error.errno !== $2) return 0; }
            FS.rename(destination, parked);
            try { FS.rename(source, destination); }
            catch (error) { FS.rename(parked, destination); return 0; }
            FS.rename(parked, source);
            return 1;
        } catch (error) { return 0; }
    }, source.c_str(), destination.c_str(), ENOENT) != 0;
}

// MEMFS has no hard links. Every filesystem mutation is serialized on the browser
// main thread, including proxied worker syscalls. One synchronous callback therefore
// makes check-and-rename exclusive within this tab. This promises no host durability.
inline auto browser_move_no_replace(const std::filesystem::path& source,
                                     const std::filesystem::path& destination) -> int {
    const auto error = MAIN_THREAD_EM_ASM_INT({
        try {
            const source = UTF8ToString($0);
            const destination = UTF8ToString($1);
            try { FS.lstat(destination); return $2; }
            catch (error) { if (error.errno !== $3) return error.errno || $4; }
            FS.rename(source, destination);
            return 0;
        } catch (error) { return error.errno || $4; }
    }, source.c_str(), destination.c_str(), EEXIST, ENOENT, EIO);
    if (!error) return 0;
    errno = error;
    return -1;
}
} // namespace wormedit::platform
#endif
