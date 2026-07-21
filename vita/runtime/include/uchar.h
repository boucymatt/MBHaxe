#ifndef MBHAXE_VITA_UCHAR_H
#define MBHAXE_VITA_UCHAR_H

#include <stdint.h>

/* VitaSDK's newlib does not ship the optional C11 uchar.h header. */
typedef uint16_t char16_t;
typedef uint32_t char32_t;

#endif
