# Project guidelines

## C++ function complexity

- Keep each new C++ function at or below a cognitive complexity score of 25, as measured by clang-tidy's `readability-function-cognitive-complexity` check.
- Check affected files with `clang-tidy -p build <source-file>` (or point `-p` to the directory containing `compile_commands.json`).
- When a function exceeds the threshold, extract cohesive helper functions that clarify responsibilities. Do not add `NOLINT` solely to silence a complexity warning.
- Existing violations may be addressed incrementally; avoid increasing their complexity while working nearby.
