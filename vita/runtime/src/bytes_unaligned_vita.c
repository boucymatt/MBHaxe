#include <hlc.h>
#include <haxe/io/Bytes.h>
#include <haxe/io/Error.h>

#include <string.h>

extern venum *g$haxe_io_Error_OutsideBounds;

/*
 * HLC emits typed float/double pointer dereferences for haxe.io.Bytes. DTS
 * streams are not guaranteed to place their floating-point fields on a
 * four-byte boundary, and ARM VFP vldr/vstr instructions fault on those
 * addresses. Copy through an aligned local value while preserving the Haxe
 * standard library's bounds behavior.
 */
double haxe_io_Bytes_getDouble(haxe__io__Bytes bytes, int position) {
	double value;

	if ((unsigned int)(position + 7) >= (unsigned int)bytes->length)
		return 0.0;
	memcpy(&value, bytes->b + position, sizeof(value));
	return value;
}

double haxe_io_Bytes_getFloat(haxe__io__Bytes bytes, int position) {
	float value;

	if ((unsigned int)(position + 3) >= (unsigned int)bytes->length)
		return 0.0;
	memcpy(&value, bytes->b + position, sizeof(value));
	return (double)value;
}

void haxe_io_Bytes_setFloat(haxe__io__Bytes bytes, int position,
	double input) {
	float value;

	if ((unsigned int)(position + 3) >= (unsigned int)bytes->length)
		hl_throw((vdynamic *)g$haxe_io_Error_OutsideBounds);
	value = (float)input;
	memcpy(bytes->b + position, &value, sizeof(value));
}
