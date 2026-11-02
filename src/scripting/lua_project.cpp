#include "lua_project.h"
#include <charconv>
#include "../core/text_util.h"
#include <wormedit/formats/native_project_storage.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif
#include <stdexcept>

namespace wormedit::scripting {
namespace {
// Shared verbatim by discovery and frozen execution. Lua parses Lua; the host only resolves
// bounded file requests. Included helper modules can return functions/tables as usual.
constexpr std::string_view frozen_header = "-- Wormedit frozen project.lua API1\n";
constexpr std::string_view project_runtime = R"lua(
local type, pairs, pcall, error, load, tostring = type, pairs, pcall, error, load, tostring
local integer_type = math.type
local storyboard_image, register_storyboard_image = image, _wormedit_osbx_project_image
local storyboard_animation, storyboard_sample = animation, sample
local sample_audio_file, register_chart_audio = audio_file, _wormedit_chart_project_audio
_wormedit_chart_project_audio = nil
_wormedit_osbx_project_image = nil
local match, sub, gsub = string.match, string.sub, string.gsub
local make_environment = _wormedit_make_project_environment
_wormedit_make_project_environment = nil
local include_from, asset
local active, loaded, seen = {}, {}, {}
local output = {notes = {}, layers = {}, sv_points = {}, sv_spans = {}}
local failed
local configuration = {assets = {}}
local declared_name
local asset_names = {}
project = configuration
local function fail(message)
  failed = failed or message
  error(message, 0)
end
local function relative(name, base)
  if type(name) ~= 'string' or name == '' or match(name, '%z') then fail('Expected a relative project path') end
  name = gsub(name, '\\', '/')
  if sub(name, 1, 1) == '/' or match(name, '^%a:') then fail('Project paths must be relative') end
  return (match(base, '^(.*[/])') or '') .. name
end
local function append(destination, values, maximum)
  if values == nil then return end
  if type(values) ~= 'table' then fail('Generator output arrays must be tables') end
  local count = 0
  for key in pairs(values) do
    if integer_type(key) ~= 'integer' or key < 1 then fail('Generator arrays must be dense') end
    count = count + 1
  end
  if #destination + count > maximum then fail('Project generator output limit exceeded') end
  for i = 1, count do
    if values[i] == nil then fail('Generator arrays must be dense') end
    destination[#destination+1] = values[i]
  end
end
local function collect(value)
  if type(value) ~= 'table' or seen[value] or (value.notes == nil and value.layers == nil and value.sv_points == nil and value.sv_spans == nil) then return end
  seen[value] = true
  for key in pairs(value) do
    if key ~= 'notes' and key ~= 'layers' and key ~= 'sv_points' and key ~= 'sv_spans' then fail('Unknown generator proposal field') end
  end
  append(output.notes, value.notes, maximum_notes)
  append(output.layers, value.layers, maximum_layers)
  append(output.sv_points, value.sv_points, maximum_notes)
  append(output.sv_spans, value.sv_spans, maximum_layers)
end
function worm_project(name)
  if type(name) ~= 'string' or name == '' or declared_name then fail('Declare worm_project once with a name') end
  declared_name = name
  configuration.name = name
end
local function media_settings(value, base, field)
  if type(value) ~= 'table' then fail('Storyboard media requires a settings table') end
  local copy = {}
  for key,item in pairs(value) do copy[key] = item end
  copy[field] = relative(value[field], base)
  return copy
end
local function media_identity(binding, id, suffix)
  if type(binding) ~= 'table' or type(binding.scope) ~= 'string' then fail('Storyboard media declaration has no scope') end
  return '@osbx/' .. binding.scope .. '/' .. id .. '/' .. suffix
end
local function animation_asset(id, value, base)
  if type(value) ~= 'table' or integer_type(value.frame_count) ~= 'integer' or value.frame_count < 1 or value.frame_count > 64 then
    fail('Animation capture must fit the existing 64-file project graph')
  end
  local binding = storyboard_animation(id, media_settings(value, base, 'file'))
  for index=0,value.frame_count-1 do
    asset('image', {id=media_identity(binding,id,'frame/' .. index), file=value.file}, base, 'animation-frame/' .. index)
  end
  return binding
end
local function sample_asset(id, value, base)
  local binding = storyboard_sample(id, media_settings(value, base, 'path'))
  asset('audio', {id=media_identity(binding,id,'sample'), file=value.path}, base, 'sample')
  return binding
end
local function environment(path)
  return make_environment {
    include = function(name) return include_from(name, path) end,
    audio = function(value) return asset('audio', value, path) end,
    audio_file = function(value)
      if configuration.assets[value] then return sample_audio_file(value) end
      return sample_audio_file(relative(value, path))
    end,
    image = function(value)
      if type(value) == 'string' and storyboard_image then return storyboard_image(value) end
      local declared = asset('image', value, path)
      if register_storyboard_image then register_storyboard_image {id=value.id,file=declared.file} end
      return declared
    end,
    midi = function(value) return asset('midi', value, path) end,
    animation = function(id,value) return animation_asset(id,value,path) end,
    sample = function(id,value) return sample_asset(id,value,path) end,
  }
end
include_from = function(name, base)
  local path, source = read('source', relative(name, base))
  if active[path] then fail('Project include cycle: ' .. path) end
  if loaded[path] then return loaded[path].value end
  active[path] = true
  local chunk, message = load(source, '@' .. path, 't', environment(path))
  local ok, value
  if chunk then ok, value = pcall(chunk) else ok, value = false, message end
  active[path] = nil
  if not ok then fail(tostring(value)) end
  loaded[path] = {value = value}
  collect(value)
  return value
end
asset = function(kind, value, base, read_kind)
  if type(value) ~= 'table' or type(value.id) ~= 'string' or value.id == '' or #value.id > 128 then
    fail('Asset declarations require a stable id and file')
  end
  for key in pairs(value) do
    if key ~= 'id' and key ~= 'file' then fail('Unknown asset declaration field') end
  end
  if asset_names[value.id] then fail('Duplicate project asset id: ' .. value.id) end
  local path, hash, identity, stream, probe_error = read(read_kind or kind, relative(value.file, base))
  local declared = {file = path, content_hash = hash, kind = kind, stream = stream, probe_error = probe_error}
  if identity ~= '' then declared.asset_id = identity end
  asset_names[value.id] = true
  if kind == 'audio' and read_kind ~= 'sample' and register_chart_audio then register_chart_audio(value.id,declared) end
  configuration.assets[value.id] = declared
  return declared
end
include_from(entry, '')
if failed then error(failed, 0) end
if not declared_name then error('project.lua requires worm_project', 0) end
return output
)lua";

auto output_limits_source() -> std::string {
    return "local maximum_notes = " + std::to_string(maximum_generator_notes) +
        "\nlocal maximum_layers = " + std::to_string(maximum_generator_layers) + '\n';
}

auto lua_string_literal(std::string_view text) -> std::string {
    std::string result{"\""};
    for (const unsigned char byte : text) {
        if (byte == '"' || byte == '\\') { result += '\\'; result += static_cast<char>(byte); }
        else if (byte < 32 || byte == 127) {
            result += '\\';
            result += static_cast<char>('0' + byte / 100);
            result += static_cast<char>('0' + byte / 10 % 10);
            result += static_cast<char>('0' + byte % 10);
        } else result += static_cast<char>(byte);
    }
    return result + '"';
}
auto stream_literal(const std::optional<audio_stream_info_t>& stream) -> std::string {
    if (!stream) return "nil";
    return "{sample_rate=" + std::to_string(stream->sample_rate) + ",channels=" +
        std::to_string(stream->channels) + ",frame_count=" + std::to_string(*stream->frame_count) +
        ",seekable=" + (stream->seekable ? "true" : "false") + "}";
}
class project_source_builder_t {
  public:
    project_source_builder_t(const song_document_t& song, std::filesystem::path base, std::stop_token stop)
        : song_(song), stop_(stop) {
        std::error_code error;
        root_ = std::filesystem::canonical(base, error);
        if (error) throw std::runtime_error("Project scripts require an existing project directory.");
    }
    auto resolve(const std::filesystem::path& path) const -> std::filesystem::path {
        std::error_code error;
        const auto absolute = std::filesystem::canonical(path.is_absolute() ? path : root_ / path, error);
        if (error || !std::filesystem::is_regular_file(absolute, error))
            throw std::runtime_error("Cannot read project file: " + text_util::path_utf8(path));
        const auto relative = absolute.lexically_relative(root_);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("Project file escapes the project directory: " + text_util::path_utf8(path));
        return relative;
    }
    auto entry(const std::filesystem::path& path) const -> std::string { return text_util::path_utf8(resolve(path)); }
    auto read(std::string_view kind, std::string_view path) -> result_t<lua_project_file_t, error_t<failure_code_t>> {
        try { return read_checked(kind, path); }
        catch (const std::exception& error) {
            failure_ = error.what();
            return std::unexpected(error_t{failure_code_t::invalid_data, *failure_});
        }
    }
    auto discovery(std::string_view entry) const -> std::string {
        return output_limits_source() + "local read = _wormedit_read_project_file\n_wormedit_read_project_file = nil\nlocal entry = " +
            lua_string_literal(entry) + '\n' + std::string(project_runtime);
    }
    auto frozen(std::string_view entry) const -> std::string {
        std::string result = std::string(frozen_header) + output_limits_source() + "local files = {\n";
        for (const auto& [key, canonical] : aliases_) {
            const auto& file = files_.at(canonical);
            result += '[' + lua_string_literal(key.first + ":" + key.second) + "]={" + lua_string_literal(file.path) + ',' +
                lua_string_literal(file.content) + ',' + lua_string_literal(file.asset_id) + ',' + lua_string_literal(key.first == "source" ? native_project_content_checksum(file.content) : file.content) + ',' + stream_literal(file.audio_stream) + "," + (file.audio_probe_error ? lua_string_literal(*file.audio_probe_error) : "nil") + "},\n";
        }
        result += R"lua(}
local path_parts, join_parts, project_error = string.gmatch, table.concat, error
local function read(kind, path)
  local parts = {}
  for part in path_parts(path, '[^/]+') do
    if part == '..' then
      if #parts == 0 then project_error('Project path escapes the project directory', 0) end
      parts[#parts] = nil
    elseif part ~= '.' then parts[#parts+1] = part end
  end
  local file = files[kind .. ':' .. join_parts(parts, '/')]
  if not file then project_error('File was not declared in the frozen project: ' .. path, 0) end
  return file[1], file[2], file[3], file[5], file[6]
end
local entry = )lua";
        return result + lua_string_literal(entry) + '\n' + std::string(project_runtime);
    }
    auto failure() const -> const std::optional<std::string>& { return failure_; }
  private:
    auto bytes(const std::filesystem::path& relative, bool source, bool count_toward_limit = true) -> std::string {
        const std::size_t maximum = source ? maximum_generator_source_bytes : 64U * 1024U * 1024U;
        auto& total = source ? source_bytes_ : asset_bytes_;
        std::ifstream input(root_ / relative, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open project file: " + text_util::path_utf8(relative));
        std::string result;
        std::array<char, 16384> buffer{};
        while (input) {
            if (stop_.stop_requested()) throw std::runtime_error("Project source loading cancelled.");
            const auto remaining = maximum - (count_toward_limit ? total : 0) - result.size();
            input.read(buffer.data(), static_cast<std::streamsize>(std::min(buffer.size(), remaining + 1)));
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > remaining) throw std::runtime_error("Project files exceed the aggregate byte limit.");
            result.append(buffer.data(), count);
        }
        if (input.bad()) throw std::runtime_error("Project file read failed.");
        if (count_toward_limit) total += result.size();
        if (source && !text_util::is_valid_bounded_text(result, maximum))
            throw std::runtime_error("Project source must be bounded UTF8 text.");
        return result;
    }
    template<class Assets>
    auto identity(const Assets& assets, const std::filesystem::path& relative) const -> std::string {
        for (const auto& asset : assets) {
            std::error_code error;
            const auto path = std::filesystem::weakly_canonical(asset.source_path.is_absolute() ? asset.source_path : root_ / asset.source_path, error);
            if (!error && path == root_ / relative) return std::to_string(asset.id.value);
        }
        return {};
    }
    auto probe_audio(const std::filesystem::path& relative, std::string_view hash) -> audio_stream_info_t {
        if (stop_.stop_requested()) throw std::runtime_error("Project discovery cancelled");
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        const auto path = root_ / relative;
        const auto before = std::filesystem::last_write_time(path);
        const auto size = std::filesystem::file_size(path);
        audio::decoder_t decoder;
        std::string error;
        if (!decoder.open(path, &error, audio::decoder_index_t::skip))
            throw std::runtime_error("Declared audio probe failed: " + error);
        const auto info = decoder.info();
        if (!info || info->source_sample_rate == 0 || info->source_channel_count == 0 ||
            info->source_frame_count > static_cast<std::uint64_t>(INT64_MAX))
            throw std::runtime_error("Declared audio geometry is unavailable or out of range");
        if (stop_.stop_requested()) throw std::runtime_error("Project discovery cancelled");
        if (std::filesystem::last_write_time(path) != before || std::filesystem::file_size(path) != size ||
            native_project_content_checksum(bytes(relative, false, false)) != hash)
            throw std::runtime_error("Declared audio changed while its receipt was prepared");
        return {.sample_rate = info->source_sample_rate, .channels = info->source_channel_count,
            .frame_count = info->source_frame_count, .seekable = info->seekable};
#else
        (void)relative; (void)hash;
        throw std::runtime_error("Declared audio probing is unavailable in this build");
#endif
    }
    auto asset_identity(std::string_view kind, const std::filesystem::path& path) const -> std::string {
        if (kind == "audio") return identity(song_.audio_assets, path);
        if (kind == "image") return identity(song_.visual_assets, path);
        return identity(song_.midi_assets, path);
    }
    struct read_target_t {
        std::string kind;
        std::filesystem::path path;
        auto operator==(const read_target_t&) const -> bool = default;
    };
    auto read_target(std::string_view kind, std::string_view path) const -> read_target_t {
        if (!text_util::is_valid_bounded_text(path, 4096) || path.empty()) throw std::runtime_error("Invalid project path.");
        constexpr std::string_view prefix = "animation-frame/";
        if (kind.starts_with(prefix)) {
            const auto number = kind.substr(prefix.size());
            std::uint32_t index{};
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size())
                throw std::runtime_error("Invalid Animation frame index.");
            storyboard_frame_arena_t arena;
            const auto frame = storyboard_animation_t::frame_path(path, index, arena);
            if (!frame) throw std::runtime_error("Animation frame path exceeds its bounds.");
            return {"image", resolve(text_util::path_from_utf8(*frame))};
        }
        if (kind != "source" && kind != "audio" && kind != "image" && kind != "midi" && kind != "sample")
            throw std::runtime_error("Unknown project file kind.");
        return {std::string{kind}, resolve(text_util::path_from_utf8(path))};
    }
    auto read_checked(std::string_view kind, std::string_view path) -> lua_project_file_t {
        if (failure_) throw std::runtime_error(*failure_);
        if (stop_.stop_requested()) throw std::runtime_error("Project source loading cancelled.");
        const auto target = read_target(kind, path);
        const auto& relative = target.path;
        const auto key = std::pair{target.kind, text_util::path_utf8(relative)};
        aliases_[{std::string(kind), text_util::path_utf8(text_util::path_from_utf8(path).lexically_normal())}] = key;
        if (aliases_.size() > 256) throw std::runtime_error("Project path alias limit exceeded.");
        if (const auto found = files_.find(key); found != files_.end()) return found->second;
        if (files_.size() >= 64) throw std::runtime_error("Project include/asset graph exceeds 64 files.");
        auto data = bytes(relative, kind == "source");
        lua_project_file_t file{.path = key.second, .content = kind == "source" ? std::move(data) : native_project_content_checksum(data),
            .asset_id = kind == "source" ? std::string{} : asset_identity(target.kind == "sample" ? "audio" : target.kind, relative)};
        if (kind == "audio") {
            // Legacy asset declarations only promise bytes. Typed audio_file
            // additionally requires this same-content decoder geometry receipt.
            try { file.audio_stream = probe_audio(relative, file.content); }
            catch (const std::exception& error) {
                if (stop_.stop_requested()) throw;
                file.audio_probe_error = error.what();
            }
        }
        files_.emplace(key, file);
        return file;
    }
    const song_document_t& song_;
    std::filesystem::path root_;
    std::stop_token stop_;
    std::map<std::pair<std::string, std::string>, lua_project_file_t> files_;
    std::map<std::pair<std::string, std::string>, std::pair<std::string, std::string>> aliases_;
    std::optional<std::string> failure_;
    std::size_t source_bytes_{}, asset_bytes_{};
};
} // namespace

auto lua_project_source_t::is_frozen(std::string_view source) -> bool { return source.starts_with(frozen_header); }

auto lua_project_source_t::from(const song_document_t& song, const chart_document_t& chart,
    const generator_recipe_t& recipe, std::span<const editor_selection_object_t> selection,
    const std::filesystem::path& base, lua_sandbox_limits_t limits) -> diagnosed_t<lua_project_source_t> {
    try {
        project_source_builder_t builder(song, base, limits.cancellation);
        const auto entry = builder.entry(recipe.source_path);
        auto discovery = recipe;
        discovery.source = builder.discovery(entry);
        discovery.source_hash = native_project_content_checksum(discovery.source);
        const auto evaluated = run_lua_generator(song, chart, discovery, selection, limits,
            [&](std::string_view kind, std::string_view path) { return builder.read(kind, path); });
        if (builder.failure()) throw std::runtime_error(*builder.failure());
        if (!evaluated.ok()) return {.diagnostics = evaluated.diagnostics};
        auto source = builder.frozen(entry);
        if (source.size() > maximum_generator_source_bytes)
            throw std::runtime_error("Frozen project source exceeds 64 KiB, including its module table and runtime.");
        return {.value = {.source = std::move(source)}};
    } catch (const std::exception& error) {
        return {.diagnostics = {{.severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_binding, .message = error.what()}}};
    }
}
}
