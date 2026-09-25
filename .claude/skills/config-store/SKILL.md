---
name: config-store
description: Add, change, deprecate or migrate persistent printer settings in the journaled EEPROM config_store (src/persistent_stores/store_instances/config_store/store_definition.hpp, StoreItem, journal::hash, DeprecatedStore, migrations). Use whenever a change needs a value that survives reboot, alters an existing setting's type/default/meaning, or removes a setting — mistakes here silently corrupt users' saved configuration after a firmware update.
---

# Persistent settings (`config_store`)

Settings live in `struct CurrentStore` in `src/persistent_stores/store_instances/config_store/store_definition.hpp`. Each item is identified in the EEPROM journal by a 14-bit ID, derived from a **name string** hashed at build time. The mechanics:
- `utils/persistent_stores/journal_hashes_generator.py` → `build/.../gen_journal_hashes.hpp`.
- Only values that differ from the default are written.
- All values are mirrored in RAM.

These facts drive every rule below.

## The rules (non-negotiable)

1. **Never change a hash name.** Existing printers' data is keyed by it.
2. **Never delete an item.** Deprecate it: move it into `DeprecatedStore`.
3. **Never reuse a name** that has ever been shipped, whether it is in `CurrentStore` or `DeprecatedStore`, even commented out. The generator scans comments too, so a commented-out item still reserves its ID. Old journal entries under that ID would be read as the new item.
4. **Changing the default value is a deprecation.** Unchanged values were never written, so a new default silently changes what users have. Changing the stored type or meaning is also a deprecation.
5. **Order-sensitive enums and arrays that are persisted must never be reordered or shrunk.** Examples: `PresetFilamentType`, `extended_printer_type_model`, `SelftestResult` layouts. Only append.
6. **Keep writes rare.** `set()` writes only on change, but EEPROM wears out. Don't persist values that change continuously during a print. Keep them in RAM, or use `ram_only = true`.
7. **Never call `set()` from an ISR:** it takes a mutex. `get()` from an ISR returns the RAM value.

## Adding a setting

```cpp
// in CurrentStore, near related items, gated like the feature it belongs to:
#if HAS_MY_FEATURE()
    StoreItem<bool, false, ItemFlag::features, journal::hash("My Feature Enabled")> my_feature_enabled;
    StoreItem<uint16_t, defaults::my_feature_timeout_s, ItemFlag::user_interface, journal::hash("My Feature Timeout")> my_feature_timeout_s;
#endif
```

- **Name:** a unique, descriptive English string. Search the file (comments included) for the exact string first. If the build fails with **`hash colision`**, pick a different name; don't "fix" the generator.
- **Default:** put non-trivial defaults in `defaults.hpp` (`namespace config_store_ns::defaults`) and constants in `constants.hpp`.
- **Flags** (`ItemFlag` in the same file) drive selective factory reset and diagnostics dumps:
  - `calibrations`, `network`, `user_interface`, `stats`, `hw_config`, `printer_state`, `user_presets`, `features`, `dev_items`, `special`, `security`.
  - `common_misconfigurations` is an add-on flag and must never be used alone: `ItemFlag::features | ItemFlag::common_misconfigurations`.
- **Types:** trivially copyable, at most 512 bytes (`MAX_ITEM_SIZE`). Strings are `std::array<char, N>` (access with `.get_c_str()` / `.set(const char*)`).
- **Arrays:** use `StoreItemArray<T, default, flags, journal::hash("Name"), max_item_count, item_count>`.
  - `max_item_count` must be a **literal**, because the generator reads the number of consecutive IDs to reserve from the source text.
  - Reserve headroom for growth, e.g. `16` for per-tool data even if `HOTENDS` is smaller.
- **Optional template args:** `StoreItem<T, def, flags, hash, hash_alloc_range = 1, ram_only = false>`.

### Using it

```cpp
#include <config_store/store_instance.hpp>

bool on = config_store().my_feature_enabled.get();
config_store().my_feature_enabled.set(true);
config_store().my_feature_timeout_s.set_to_default();
config_store().hotend_type.get(idx);  config_store().hotend_type.set(idx, v);   // arrays

// several items atomically, in one journal transaction:
{
    auto &store = config_store();
    auto transaction = store.get_backend().transaction_guard();
    store.a.set(1);
    store.b.set(2);
}
```

- `.transform(f)` / `.apply(f)` perform a read-modify-write under the lock.
- For C code there is `store_c_api.h`.
- To read an item in gdb: `p 'config_store_journal()::instance'.<item>`.

### UI and G-code exposure

- Menu items usually persist in `OnChange` (see the `gui-menu` skill).
- If the setting belongs in the user's `.ini` or Connect settings, check the existing handlers in `src/common/` and `src/connect/` for similar items.

## Deprecating or replacing a setting

1. Move the item **verbatim**, with the same type, default and hash string, from `CurrentStore` into `struct DeprecatedStore` (lower in the same file).
   - Deprecated items take no flags: `StoreItem<T, default, journal::hash("Old Name")>`.
   - Add a comment saying what replaced it.
2. Add the successor to `CurrentStore` under a **new** name. The convention is the old name plus ` V2`, ` V3`, …, e.g. `"Sound Mode"` → `"Sound Mode V2"`.
3. If old values must carry over, add an **item-level migration**:
   - In `migrations.hpp`, add `namespace deprecated_ids { inline constexpr uint16_t my_item[] { decltype(DeprecatedStore::old_item)::hashed_id }; }` and declare `void my_item(journal::Backend &backend);` in `namespace migrations`.
   - Implement it in `migrations.cpp`:

     ```cpp
     void my_item(journal::Backend &backend) {
         const auto old_value = read_old_item_value<decltype(DeprecatedStore::old_item)>(backend);
         using NewItem = decltype(CurrentStore::new_item);
         backend.save_migration_item<NewItem::value_type>(NewItem::hashed_id, convert(old_value));
     }
     ```

     When the old item may be absent and absence should keep the new default, use `backend.read_items_for_migrations(callback)` directly, as `side_leds_enable` does.
   - Append `{ migrations::my_item, deprecated_ids::my_item }` to the **end** of `migration_functions[]`. The order there is part of the on-device format.
4. Check that nothing else still references the old member.

## Whole-store (semantic) migrations

These are for one-off fixes that need the whole store or printer context, e.g. "users upgrading from < 6.x must have X set". Edit `CurrentStore::perform_config_migrations()` in `store_definition.cpp`:
- Increment `newest_config_version` in `store_definition.hpp`.
- Append `if (should_migrate<N>()) { ... }` at the **end** of the function, with N being the new version. Keep the instruction comment at the bottom.

## Verify

- Build at least one preset where the item exists and one where it's gated out. The hash generator runs at configure/build time.
- Run the persistent-store unit tests: `python3 utils/build_tests.py --run -- -R "journal|config|eeprom"` (tests in `tests/unit/persistent_stores/`).
- For size: `utils/persistent_stores/config_store_analysis.py build/<cfg>/firmware`.
- In the PR description, state explicitly that the change adds, deprecates or migrates a config item, and why the default was chosen.
