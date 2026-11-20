#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>

#include <emscripten/bind.h>

#include <fstream>
#include <iterator>
#include <span>
#include <string>

namespace {

// Only the codec crosses this browser boundary. No native filesystem publication is exposed.
auto normalize_native(const std::string& bytes) -> std::string {
    const auto parsed = wormedit::parse_native_project(bytes);
    if (!parsed.value) return {};
    return wormedit::write_native_project(*parsed.value).value.value_or("");
}

auto sample_native() -> std::string {
    std::ifstream input("/sample.bmson", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    const auto parsed = wormedit::parse_bmson_bytes(bytes);
    if (!parsed.value) return {};
    return wormedit::write_native_project(*parsed.value).value.value_or("");
}

} // namespace

EMSCRIPTEN_BINDINGS(wormedit_smoke) {
    emscripten::function("normalizeNative", &normalize_native);
    emscripten::function("sampleNative", &sample_native);
}
