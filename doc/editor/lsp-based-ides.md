### Development with LSP-based IDEs (Visual Studio [Code], Vim, Sublime Text, etc.)

1. Run `python utils/bootstrap.py` to download required dependencies.
2. From the repository root, configure the project with one of the CMake presets, for example:

    ```bash
    .dependencies/cmake-3.28.3/bin/cmake --preset mini_debug_boot
    ```

    Presets are named `<printer preset>_<debug|release>_<boot|noboot>` (list them with `cmake --list-presets`). They are generated from `utils/presets/presets.json`. A preset sets the generator (Ninja), the toolchain, the build type and the build directory (`build/<preset name>`). Extra options can be added as `-DNAME=VALUE`; see `ProjectOptions.cmake` for the available ones (most of them map one-to-one to `build.py`'s options).
3. Configuring generates `build/<preset name>/compile_commands.json`, which an LSP server can pick up to provide autocompletion (we recommend `clangd`). Point your editor to it or symlink it to the repository root (`ln -s build/mini_debug_boot/compile_commands.json`). Build with `.dependencies/ninja-1.10.2/ninja -C build/mini_debug_boot`.
4. Install some spell checker (optional but recommended).
