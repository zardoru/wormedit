// Included inside namespace wormedit after the shared native value codecs.
// Tags are stable schema vocabulary, independent of variant ordering.
template<class Value> struct bms_codec_tag_t;
#define BMS_CODEC_TAG(Type, Tag) \
    template<> struct bms_codec_tag_t<Type> { static constexpr std::string_view name = Tag; };
#define BMS_CODEC_VALUE(Type, Tag, ...) \
    BMS_CODEC_TAG(Type, Tag) \
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Type, __VA_ARGS__)

BMS_CODEC_VALUE(bms_program_position_t, "position", measure, fraction)
BMS_CODEC_VALUE(bms_native_resource_reference_t, "resource_reference", name_space, identifier)
BMS_CODEC_VALUE(bms_note_attributes_t, "attributes", semantic_layers, label, osu_hitsample)
BMS_CODEC_VALUE(bms_note_content_t, "note", position, lane, kind, wav, mine_value, attributes)
BMS_CODEC_VALUE(bms_hold_endpoint_t, "hold_endpoint", position, lane, wav, attributes)
BMS_CODEC_VALUE(bms_bgm_content_t, "bgm", position, column, wav, structural_slot, semantic_layers)
BMS_CODEC_VALUE(bms_bga_content_t, "bga", position, kind, bmp)
BMS_CODEC_VALUE(bms_timing_definition_t, "timing_definition", kind, identifier, value)
BMS_CODEC_VALUE(bms_timing_reference_t, "timing_reference", position, kind, identifier)
BMS_CODEC_VALUE(bms_direct_bpm_t, "direct_bpm", position, value)
BMS_CODEC_VALUE(bms_measure_length_t, "measure_length", measure, length)
BMS_CODEC_VALUE(bms_resource_declaration_t, "resource_declaration", name_space, identifier, resource)
BMS_CODEC_TAG(bms_metadata_declaration_t, "metadata")
BMS_CODEC_TAG(bms_native_content_t, "native")
BMS_CODEC_TAG(bms_root_scope_t, "root")
BMS_CODEC_VALUE(bms_random_scope_t, "random", range)
BMS_CODEC_VALUE(bms_if_scope_t, "if", value)
BMS_CODEC_VALUE(bms_else_if_scope_t, "else_if", previous_arm, value)
BMS_CODEC_VALUE(bms_else_scope_t, "else", previous_arm)
BMS_CODEC_VALUE(bms_switch_scope_t, "switch", value)
BMS_CODEC_VALUE(bms_skip_point_t, "skip", scope, after_child, provenance)
BMS_CODEC_VALUE(bms_case_scope_t, "case", value, skips)
BMS_CODEC_VALUE(bms_set_random_t, "set_random", value)
BMS_CODEC_VALUE(bms_set_switch_t, "set_switch", value)
BMS_CODEC_TAG(bms_text_t, "text")
BMS_CODEC_TAG(bms_chart_content_t, "chart")
BMS_CODEC_TAG(note_t, "note")
BMS_CODEC_TAG(bgm_event_t, "bgm")
BMS_CODEC_TAG(bga_event_t, "bga")
BMS_CODEC_TAG(bpm_event_t, "bpm")
BMS_CODEC_TAG(pause_event_t, "pause")
BMS_CODEC_TAG(warp_event_t, "warp")
BMS_CODEC_TAG(scroll_event_t, "scroll")
BMS_CODEC_TAG(speed_event_t, "speed")
BMS_CODEC_TAG(kiai_event_t, "kiai")
BMS_CODEC_TAG(timing_anchor_t, "timing_anchor")
BMS_CODEC_TAG(bookmark_t, "bookmark")
BMS_CODEC_TAG(section_t, "section")
BMS_CODEC_TAG(sv_recipe_t, "sv_recipe")
BMS_CODEC_TAG(sound_channel_t, "sound_channel")
#undef BMS_CODEC_VALUE
#undef BMS_CODEC_TAG

void to_json(json& value, const bms_text_t& text) {
    const auto bytes = std::as_bytes(std::span{text.bytes.data(), text.bytes.size()});
    value = {{"bytes_hex", bytes_to_hex(std::vector<std::byte>(bytes.begin(), bytes.end()))},
        {"encoding", text.encoding}};
}
void from_json(const json& value, bms_text_t& text) {
    const auto bytes = hex_to_bytes(value.at("bytes_hex").get<std::string>());
    text.bytes.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    value.at("encoding").get_to(text.encoding);
}
void to_json(json& value, const bms_root_scope_t&) { value = json::object(); }
void from_json(const json& value, bms_root_scope_t&) {
    if (!value.is_object() || !value.empty()) throw std::invalid_argument("Root has no payload fields.");
}
void to_json(json& value, const bms_metadata_declaration_t& metadata) {
    value = {{"field", metadata.field}};
    std::visit([&](const auto& data) { value["value"] = data; }, metadata.value);
}
void from_json(const json& value, bms_metadata_declaration_t& metadata) {
    value.at("field").get_to(metadata.field);
    const auto& data = value.at("value");
    if (data.is_string()) metadata.value = data.get<std::string>();
    else metadata.value = data.get<signed_rational_t>();
}

// Definitions follow their payload codecs, so ADL sees the same serializers used
// for ordinary charts. The non-default-constructible native value uses its factory.
void to_json(json& value, const bms_native_content_t& content);
void to_json(json& value, const bms_chart_content_t& content);
void from_json(const json& value, bms_chart_content_t& content);
auto read_bms_native_content(const json& value) -> bms_native_content_t;

template<class... Values> void to_json(json& value, const std::variant<Values...>& data) {
    std::visit([&](const auto& payload) {
        using type = std::remove_cvref_t<decltype(payload)>;
        value = {{"type", bms_codec_tag_t<type>::name}, {"data", payload}};
    }, data);
}
template<class Variant, std::size_t Index = 0>
auto read_bms_variant(const json& value) -> Variant {
    if constexpr (Index == std::variant_size_v<Variant>) {
        throw std::invalid_argument("Unknown scoped BMS payload tag.");
    } else {
        using type = std::variant_alternative_t<Index, Variant>;
        if (value.at("type") == bms_codec_tag_t<type>::name) {
            if constexpr (std::same_as<type, bms_native_content_t>)
                return read_bms_native_content(value.at("data"));
            else return value.at("data").get<type>();
        }
        return read_bms_variant<Variant, Index + 1>(value);
    }
}
template<class... Values> void from_json(const json& value, std::variant<Values...>& data) {
    data = read_bms_variant<std::variant<Values...>>(value);
}
void to_json(json& value, const bms_native_content_t& content) {
    const auto positions = content.positions();
    value = {{"payload", content.payload()},
        {"positions", std::vector<bms_program_position_t>(positions.begin(), positions.end())},
        {"resource_reference", content.resource_reference()}};
}
auto read_bms_native_content(const json& value) -> bms_native_content_t {
    auto content = bms_native_content_t::from(read_bms_variant<bms_native_event_t>(value.at("payload")),
        value.at("positions").get<std::vector<bms_program_position_t>>(),
        value.at("resource_reference").get<std::optional<bms_native_resource_reference_t>>());
    if (!content) throw std::invalid_argument("Invalid canonical scoped native content.");
    return std::move(*content);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_content_item_t, id, value, provenance)
void to_json(json& value, const bms_chart_content_t& content) { value = {{"items", content.items}}; }
void from_json(const json& value, bms_chart_content_t& content) { value.at("items").get_to(content.items); }
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_program_node_t, id, value, children)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(lane_input_profile_t, layout, input)

void to_json(json& value, const bms_program_document_t& owner) {
    const auto& prepared = owner.prepared();
    value = {{"difficulty", owner.difficulty()},
        {"nodes", std::vector<bms_program_node_t>(prepared.program.nodes().begin(), prepared.program.nodes().end())},
        {"measures", prepared.measures}, {"lane_profile", prepared.lane_profile},
        {"bgm_columns", prepared.bgm_columns}, {"resources", prepared.resources},
        {"audio_assets", prepared.audio_assets}, {"visual_assets", prepared.visual_assets},
        {"semantic_layers", prepared.semantic_layers}, {"sound_layer_defaults", prepared.sound_layer_defaults},
        {"evaluation", owner.evaluation().selections}};
}
auto read_bms_program_document(const json& value) -> std::shared_ptr<const bms_program_document_t> {
    auto program = bms_program_t::from(value.at("nodes").get<std::vector<bms_program_node_t>>());
    if (!program) throw std::invalid_argument(program.error().message);
    auto owner = bms_program_document_t::from(value.at("difficulty").get<chart_id_t>(),
        {.program = std::move(*program),
            .measures = value.at("measures").get<std::vector<measure_id_t>>(),
            .lane_profile = value.at("lane_profile").get<std::optional<lane_input_profile_t>>(),
            .bgm_columns = value.at("bgm_columns").get<std::vector<bgm_source_lane_t>>(),
            .resources = value.at("resources").get<std::vector<resource_entry_t>>(),
            .audio_assets = value.at("audio_assets").get<std::vector<audio_asset_t>>(),
            .visual_assets = value.at("visual_assets").get<std::vector<visual_asset_t>>(),
            .semantic_layers = value.at("semantic_layers").get<std::vector<semantic_layer_t>>(),
            .sound_layer_defaults = value.at("sound_layer_defaults").get<std::vector<sound_layer_default_t>>()},
        {.selections = value.at("evaluation").get<std::vector<branch_selection_t>>()});
    if (!owner.value || !owner.ok()) throw std::invalid_argument("Invalid scoped BMS document projection.");
    return std::move(*owner.value);
}

// A shared project asset is serialized once. A numeric pool entry borrows its
// immutable value from the project catalog; local inactive assets remain inline.
template<class Asset> void write_bms_asset_references(json& value, std::string_view key,
    const std::vector<Asset>& assets) {
    for (auto& entry : value.at(key)) {
        const auto id = entry.at("id").get<asset_id_t>();
        const auto found = std::ranges::find(assets, id, &Asset::id);
        if (found == assets.end()) continue;
        if (entry != json(*found)) throw std::invalid_argument("Scoped and project asset values conflict.");
        entry = id;
    }
}
template<class Asset> void read_bms_asset_references(json& value, std::string_view key,
    const std::vector<Asset>& assets) {
    for (auto& entry : value.at(key)) {
        if (!entry.is_number_unsigned()) continue;
        const auto found = std::ranges::find(assets, entry.get<asset_id_t>(), &Asset::id);
        if (found == assets.end()) throw std::invalid_argument("Scoped shared asset reference is missing.");
        entry = *found;
    }
}
