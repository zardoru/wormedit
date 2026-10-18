#pragma once
#include <string>
#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

namespace wormedit {
// Changes only the working document; the caller owns publication and the returned Undo label.
auto stage_delete_bmson_bgm_marker(document_command_transaction_t& transaction, chart_id_t chart,
                                   object_id_t bgm_event) -> diagnosed_t<std::string>;
} // namespace wormedit
