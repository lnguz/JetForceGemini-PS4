// FreeBSD's <machine/param.h> defines PAGESIZE, which slot_map uses as a template parameter name.
#pragma once
#undef PAGESIZE
#include_next <slot_map.h>
