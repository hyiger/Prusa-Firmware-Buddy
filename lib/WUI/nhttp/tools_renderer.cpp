#include "tools_renderer.h"

#include <config_store/store_instance.hpp>
#include <segmented_json_macros.h>

namespace nhttp::handler {

namespace {

    ToolsState::Tool read_tool(VirtualToolIndex tool) {
        const PhysicalToolIndex physical = tool.to_physical();
        const FilamentType type = FilamentType::for_tool(tool);
        const FilamentSpool spool = FilamentSpool::for_tool(tool);
        const auto color = spool.color.color();

        ToolsState::Tool result {
            .loaded = type != FilamentType::none,
            .material = {},
            .vendor = spool.vendor.name(),
            .color = spool.color.name(),
            .color_hex = color ? color_to_hex(*color) : ColorHexString {},
            .nozzle_diameter = config_store().get_nozzle_diameter(physical),
            .hardened = config_store().get_nozzle_is_hardened(physical.to_raw()),
            .high_flow = config_store().get_nozzle_is_high_flow(physical.to_raw()),
        };
        if (result.loaded) {
            result.material = type.parameters().name;
        }
        return result;
    }

} // namespace

json::JsonResult ToolsRenderer::renderState(size_t resume_point, json::JsonOutput &output, ToolsState &state) const {
    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_FIELD_OBJ("tools");
            for (state.tool = 0, state.need_comma = false; state.tool < VirtualToolIndex::count; state.tool++) {
                if (!VirtualToolIndex::from_raw(state.tool).is_enabled()) {
                    continue;
                }
                state.current = read_tool(VirtualToolIndex::from_raw(state.tool));

                if (state.need_comma) {
                    JSON_COMMA;
                }
                JSON_CUSTOM("\"%d\":{", state.tool + 1);
                    JSON_FIELD_BOOL("loaded", state.current.loaded) JSON_COMMA;
                    JSON_FIELD_STR("material", state.current.material.data()) JSON_COMMA;
                    JSON_FIELD_STR("vendor", state.current.vendor.data()) JSON_COMMA;
                    JSON_FIELD_STR("color", state.current.color.data()) JSON_COMMA;
                    JSON_FIELD_STR("color_hex", state.current.color_hex.data()) JSON_COMMA;
                    JSON_FIELD_FFIXED("nozzle_diameter", state.current.nozzle_diameter, 2) JSON_COMMA;
                    JSON_FIELD_BOOL("hardened", state.current.hardened) JSON_COMMA;
                    JSON_FIELD_BOOL("high_flow", state.current.high_flow);
                JSON_OBJ_END;
                state.need_comma = true;
            }
        JSON_OBJ_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
