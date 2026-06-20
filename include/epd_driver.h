#pragma once

// Compatibility shim for the diy-esp32-epub-reader submodule (lib/diy-esp32-epub-reader),
// which targets the pre-2.0 epdiy API where the umbrella header was named epd_driver.h and
// EPD_WIDTH/EPD_HEIGHT were compile-time macros. epdiy 2.0 (vendored here as lib/epdiy)
// renamed the header to epdiy.h and replaced those macros with runtime accessors, since
// epdiy 2.0 supports multiple panel sizes/rotations. PlatformIO's project-wide include/
// directory lets us satisfy the submodule's #include <epd_driver.h> without forking it.
#include <epdiy.h>

#define EPD_WIDTH epd_rotated_display_width()
#define EPD_HEIGHT epd_rotated_display_height()
