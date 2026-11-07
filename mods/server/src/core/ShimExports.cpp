// keeps the exported c api alive in the shared library: the objects live in the static core,
// and a linker would otherwise drop the ones nothing in this file references
#include "perlin/server.h"

extern "C" PS_API const char* ps_version_string(void) {
    return "perlin-server 0.1";
}
