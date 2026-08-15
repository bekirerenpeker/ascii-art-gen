// Single translation unit that instantiates the implementation for every
// vendored stb header. Nothing else in the codebase should define these
// STB_*_IMPLEMENTATION macros or re-include these headers with them set --
// each implementation must be compiled exactly once.

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
