#pragma once

#include "segmented_json.h"

#include <filament.hpp>
#include <filament_library.hpp>
#include <tool_index.hpp>

namespace nhttp::handler {

class ToolsState {
public:
    /// The tool being rendered. Read once when its object starts, so a resumed render stays consistent.
    struct Tool {
        bool loaded = false;
        FilamentTypeParameters::Name material;
        FilamentLibraryName vendor;
        FilamentLibraryName color;
        ColorHexString color_hex {};
        float nozzle_diameter = 0;
        bool hardened = false;
        bool high_flow = false;
    };

    uint8_t tool = 0;
    bool need_comma = false;
    Tool current;
};

/// Renders /api/v1/tools: the filament in each enabled tool, with its vendor and color, and the tool's nozzle.
/// Tools are keyed from "1", like the tools Connect reports. Unset values are empty strings.
class ToolsRenderer final : public json::JsonRenderer<ToolsState> {
public:
    ToolsRenderer()
        : JsonRenderer(ToolsState {}) {}

    json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, ToolsState &state) const override;
};

} // namespace nhttp::handler
