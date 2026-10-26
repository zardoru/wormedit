SDL3 and embedded third-party notices

LICENSE.txt is SDL's top-level zlib license. SDL3-source-notices.tar.xz
contains the selected SDL checkout's complete include/ and src/ trees plus
LICENSE.txt, preserving all embedded copyright and license notices without
rewriting them. It excludes Git history, build artifacts and tests.

In particular, consult the archive's src/hidapi/LICENSE* (HIDAPI is used
under its BSD option), src/video/yuv2rgb/LICENSE, src/video/khronos/ headers
(Khronos notices, with Apache-2.0.txt provided here), src/libm/ sources
(Sun Microsystems and other math notices), and any further notices retained
in the individual source/header files. HIDAPI and YUV licenses are also
copied into adjacent directories for direct access.

Wormedit's bounded SDL presentation changes live in private generated
translation-unit overrides; they do not alter this upstream source tree.
Those changes use Wormedit's project license, installed beside the program.
