#pragma once

// GCC's -Wdangling-reference assumes a reference-returning function may return a reference into
// any temporary argument. Mark functions whose result refers only to a non-temporary argument
// (for example a JSON node, never the path string built for its error message).
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(gnu::no_dangling)
#define WORMEDIT_NO_DANGLING [[gnu::no_dangling]]
#endif
#endif
#ifndef WORMEDIT_NO_DANGLING
#define WORMEDIT_NO_DANGLING
#endif
