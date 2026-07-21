if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
	message(FATAL_ERROR "Vita LiveArea icon is missing: ${INPUT}")
endif()

# The Vita application promoter rejects otherwise valid VPKs when icon0.png is
# a normal RGB/RGBA PNG.  Validate the fields in PNG's fixed IHDR header so the
# package cannot silently regress to an uninstallable icon.
file(READ "${INPUT}" PNG_SIGNATURE OFFSET 0 LIMIT 8 HEX)
file(READ "${INPUT}" PNG_DIMENSIONS OFFSET 16 LIMIT 8 HEX)
file(READ "${INPUT}" PNG_FORMAT OFFSET 24 LIMIT 2 HEX)
string(TOLOWER "${PNG_SIGNATURE}" PNG_SIGNATURE)
string(TOLOWER "${PNG_DIMENSIONS}" PNG_DIMENSIONS)
string(TOLOWER "${PNG_FORMAT}" PNG_FORMAT)

if(NOT PNG_SIGNATURE STREQUAL "89504e470d0a1a0a")
	message(FATAL_ERROR "Vita LiveArea icon is not a PNG: ${INPUT}")
endif()
if(NOT PNG_DIMENSIONS STREQUAL "0000008000000080")
	message(FATAL_ERROR
		"Vita icon0.png must be exactly 128x128 pixels: ${INPUT}")
endif()
if(NOT PNG_FORMAT STREQUAL "0803")
	message(FATAL_ERROR
		"Vita icon0.png must be an 8-bit indexed PNG (IHDR 08/03): ${INPUT}")
endif()

file(SIZE "${INPUT}" PNG_SIZE)
if(PNG_SIZE GREATER 430080)
	message(FATAL_ERROR
		"Vita icon0.png must not exceed 420 KiB: ${INPUT} (${PNG_SIZE} bytes)")
endif()
