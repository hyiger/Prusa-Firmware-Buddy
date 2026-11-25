/**
 * @file fs_event_autolock.hpp
 * @author Radek Vana
 * @brief ctor locks events (autoload and M600 injection), dtor unlocks it
 * normally it is handled automaticaly but in some cases manual call is needed
 * @date 2021-02-14
 */

#pragma once

#include <feature/filament_sensor/filament_sensors_handler.hpp>

class FS_EventAutolock {
public:
    FS_EventAutolock(const FS_EventAutolock &other) = delete;
    FS_EventAutolock(FS_EventAutolock &&other) = delete;
    inline FS_EventAutolock() { FSensors_instance().IncEvLock(); }
    inline ~FS_EventAutolock() { FSensors_instance().DecEvLock(); }
};

/// The event lock of a filament load or unload, see FilamentSensors::IncLoadEvLock
class FS_LoadEventAutolock {
public:
    FS_LoadEventAutolock(const FS_LoadEventAutolock &other) = delete;
    FS_LoadEventAutolock(FS_LoadEventAutolock &&other) = delete;
    inline FS_LoadEventAutolock() { FSensors_instance().IncLoadEvLock(); }
    inline ~FS_LoadEventAutolock() { FSensors_instance().DecLoadEvLock(); }
};
