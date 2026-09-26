### Development using Vim

1. Configure your [Neo]Vim to support Language Server Protocol -- I recommend (early 2020) [coc.nvim](https://github.com/neoclide/coc.nvim).
2. Install [ccls](https://github.com/MaskRay/ccls) on your system.
3. Configure your LSP plugin in Vim to use `ccls`. In case of `coc.nvim`, the configuration looks as follows:

    ```JSON
    "languageserver": {
        "ccls": {
            "command": "ccls",
            "filetypes": [
                "c",
                "cpp"
            ],
            "settings": {},
            "rootPatterns": [
                ".ccls",
                "compile_commands.json",
                ".vim/",
                ".git/",
                ".hg/"
            ],
            "initializationOptions": {
                "cache": {
                    "directory": "/tmp/ccls"
                },
                "highlight": {
                    "lsRanges": true
                }
            }
    ```
4. Download the dependencies and configure and build the project with a CMake preset (from the repository root; see [LSP-based IDEs](lsp-based-ides.md) for the preset names):
    ```bash
    $ python utils/bootstrap.py
    $ .dependencies/cmake-3.28.3/bin/cmake --preset mini_debug_boot
    $ .dependencies/ninja-1.10.2/ninja -C build/mini_debug_boot
    ```
5. Locate a `compile_commands.json` file for `ccls`. By default, `ccls` searches in project's root directory, but CMake generates it in the build folder. There are two options:
    - Update `ccls` config to let `ccls` know, that it should search for it in the `build/mini_debug_boot` subfolder, or
    - make a symbolic link in project's root directory: `ln -s build/mini_debug_boot/compile_commands.json`.
6. Install some spell checker (optional but recommended).
7. Get to work! 💪
