#pragma once

struct ConfigStore {
    struct BoolTrue {
        bool get() const {
            return true;
        }
    };

    BoolTrue serial_print_screen_enabled;
};

inline ConfigStore &config_store() {
    static ConfigStore store;
    return store;
}
