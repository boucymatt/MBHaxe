#ifndef MBHAXE_GL_VITA_H
#define MBHAXE_GL_VITA_H

#include <stdint.h>

/* Associates subsequent GL draw breadcrumbs with the present being built. */
void mbhaxe_vita_gl_present_begin(uint32_t sequence);

#endif
