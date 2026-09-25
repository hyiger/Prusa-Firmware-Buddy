# Pictures in firmware

Icons and pictures are stored as PNG files and packed at build time. They are
not compiled into the firmware binary. At runtime they are read from
`/internal/res/qoi.data`, which is part of the resources that the `.bbf`
installs into the printer's internal flash.

## Adding a picture

1. Save the picture as an RGBA PNG named `<name>_<width>x<height>.png` in
   `src/gui/res/png/`, e.g. `arrow_down_12x12.png`.
2. Add the file name to `src/gui/res/<PRINTER>_used_imgs.txt` for every printer
   that shows it.
3. Use it in code as `&img::<name>_<width>x<height>`, after
   `#include <img_resources.hpp>`.

## How it works

- `src/resources/QoiGenerator.cmake` runs `utils/qoi_packer.py` on
  `src/gui/res/png/`.
- **Listed pictures:** the packer QOI-encodes every picture listed in the
  printer's `_used_imgs.txt` into `qoi.data`. It also emits an
  `inline constexpr Resource <name>(offset, width, height);` into the generated
  `qoi_resources.gen`, which `src/gui/img_resources.hpp` includes inside
  `namespace img`.
- **Unlisted pictures** are only declared (`extern Resource <name>;`). Using one
  on a printer whose list doesn't contain it compiles but fails to link with an
  undefined reference to `img::<name>`. Add it to that printer's list.
- `qoi.data` goes into the resources tarball appended to the `.bbf`. At boot,
  if the installed resources don't match the firmware, the printer installs
  them from the matching `.bbf` on the USB drive, or on the host over the
  debugger (semihosting). See `src/resources/bootstrap.cpp`.

## Signature Oak (brass) variants

The `coreone_oak` build overlays `src/gui/res/png_brass/` over
`src/gui/res/png/`: a PNG with the same file name in `png_brass/` replaces the
standard one. Standard icons that contain Prusa orange need a brass
counterpart.

`utils/generate-icon-parity-report.py` lists the icons that are missing one.
CI runs it as an informational report. Icons that intentionally stay orange
belong in its `NO_BRASS_REQUIRED` set.
