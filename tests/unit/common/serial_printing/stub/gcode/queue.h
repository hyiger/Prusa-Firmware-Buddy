#pragma once

class GCodeQueue {
public:
    static bool has_commands_queued();
    static bool pause_serial_commands;
};
