cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "/usr/local/vitasdk/share/vita.toolchain.cmake"
		CACHE FILEPATH "VitaSDK toolchain")
endif()

project(mbhaxe_vita LANGUAGES C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(GAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(HASHLINK_ROOT "${GAME_ROOT}/../deps/hashlink" CACHE PATH
	"HashLink source checkout")
set(HLC_ROOT "${GAME_ROOT}/vita/native")
set(APP_VERSION "01.27")
option(MBHAXE_VITA_GPU_DIAGNOSTICS
	"Synchronize early VitaGL draws to isolate GPU command-stream failures" OFF)

if(NOT EXISTS "${HLC_ROOT}/marblegame.c")
	message(FATAL_ERROR
		"Missing HLC output. Run 'haxe compile-vita.hxml' from ${GAME_ROOT} first.")
endif()
if(NOT EXISTS "${HASHLINK_ROOT}/src/hl.h")
	message(FATAL_ERROR "HASHLINK_ROOT does not contain a HashLink checkout")
endif()

file(GLOB_RECURSE HLC_SOURCES CONFIGURE_DEPENDS "${HLC_ROOT}/*.c")

set(HASHLINK_SOURCES
	"${HASHLINK_ROOT}/src/gc.c"
	"${HASHLINK_ROOT}/src/std/array.c"
	"${HASHLINK_ROOT}/src/std/buffer.c"
	"${HASHLINK_ROOT}/src/std/bytes.c"
	"${HASHLINK_ROOT}/src/std/cast.c"
	"${HASHLINK_ROOT}/src/std/date.c"
	"${HASHLINK_ROOT}/src/std/error.c"
	"${HASHLINK_ROOT}/src/std/file.c"
	"${HASHLINK_ROOT}/src/std/fun.c"
	"${HASHLINK_ROOT}/src/std/maps.c"
	"${HASHLINK_ROOT}/src/std/math.c"
	"${HASHLINK_ROOT}/src/std/obj.c"
	"${HASHLINK_ROOT}/src/std/random.c"
	"${HASHLINK_ROOT}/src/std/regexp.c"
	"${HASHLINK_ROOT}/src/std/string.c"
	"${HASHLINK_ROOT}/src/std/track.c"
	"${HASHLINK_ROOT}/src/std/types.c"
	"${HASHLINK_ROOT}/src/std/ucs2.c"
	"${GAME_ROOT}/vita/runtime/src/sys_vita.c"
	"${GAME_ROOT}/vita/runtime/src/thread_vita.c"
	"${GAME_ROOT}/vita/runtime/src/gc_memory_vita.c"
)

set(NATIVE_BINDING_SOURCES
	"${GAME_ROOT}/vita/runtime/src/vita_main.c"
	"${GAME_ROOT}/vita/runtime/src/bytes_unaligned_vita.c"
	"${HASHLINK_ROOT}/libs/fmt/fmt.c"
	"${HASHLINK_ROOT}/libs/fmt/sha1.c"
	"${HASHLINK_ROOT}/libs/fmt/dxt.c"
	"${HASHLINK_ROOT}/libs/fmt/mikkt.c"
	"${HASHLINK_ROOT}/include/mikktspace/mikktspace.c"
	"${GAME_ROOT}/vita/runtime/src/openal_vita.c"
	"${HASHLINK_ROOT}/libs/ui/ui_stub.c"
	"${GAME_ROOT}/vita/runtime/src/uv_stub.c"
	"${GAME_ROOT}/vita/runtime/src/sdl_vita.c"
	"${GAME_ROOT}/vita/runtime/src/gl_vita.c"
	"${GAME_ROOT}/vita/runtime/src/vitagl_alloc_vita.c"
)

# Keep the vitaGL implementation and its private structure ABI on one pinned
# revision. The previously installed archive predates the unconditional FBO
# color-surface/texture coherency repair and cannot safely be mixed with newer
# translation units. The vendored glsl_utils.c also includes the local bounds
# fix that gives macro expansion enough output space.
set(VITAGL_VENDOR_ROOT
	"${GAME_ROOT}/vita/vendor/vitaGL-ed51b21")
file(GLOB VITAGL_C_SOURCES CONFIGURE_DEPENDS
	"${VITAGL_VENDOR_ROOT}/source/*.c"
	"${VITAGL_VENDOR_ROOT}/source/utils/*.c"
)
file(GLOB VITAGL_CXX_SOURCES CONFIGURE_DEPENDS
	"${VITAGL_VENDOR_ROOT}/source/*.cpp"
	"${VITAGL_VENDOR_ROOT}/source/utils/*.cpp"
	"${VITAGL_VENDOR_ROOT}/source/utils/preprocessor/*.cpp"
)
add_library(mbhaxe_vitagl STATIC
	${VITAGL_C_SOURCES}
	${VITAGL_CXX_SOURCES}
)
set_target_properties(mbhaxe_vitagl PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED YES
)
target_compile_definitions(mbhaxe_vitagl PRIVATE
	HAVE_GLSL_TEXTURE_SIZE
	HAVE_GLSL_UBOS
	STORE_DEPTH_STENCIL
	STRICT_DRAW_COMPLIANCE
	STRICT_UNIFORMS_COMPLIANCE
	SKIP_SPLASHSCREEN
	MBHAXE_VITA_SCENE_TRACE
	MBHAXE_VITA_SHADER_TRACE
	VGL_GIT_HASH=\"ed51b21\"
)
target_compile_options(mbhaxe_vitagl PRIVATE
	-O3
	-ffast-math
	-mtune=cortex-a9
	-mfpu=neon
	-mfp16-format=ieee
	$<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>
	-Wno-stringop-overflow
)
target_include_directories(mbhaxe_vitagl PRIVATE
	"${VITAGL_VENDOR_ROOT}/source"
	"${VITASDK}/arm-vita-eabi/include"
)

set_source_files_properties(
	"${GAME_ROOT}/vita/runtime/src/openal_vita.c"
	PROPERTIES COMPILE_DEFINITIONS
		"MBHAXE_HASHLINK_OPENAL_SOURCE=\"${HASHLINK_ROOT}/libs/openal/openal.c\""
)
set_source_files_properties(
	"${GAME_ROOT}/vita/runtime/src/sys_vita.c"
	PROPERTIES COMPILE_DEFINITIONS
		"MBHAXE_HASHLINK_SYS_SOURCE=\"${HASHLINK_ROOT}/src/std/sys.c\""
)
set_source_files_properties(
	"${GAME_ROOT}/vita/runtime/src/thread_vita.c"
	PROPERTIES COMPILE_DEFINITIONS
		"MBHAXE_HASHLINK_THREAD_SOURCE=\"${HASHLINK_ROOT}/src/std/thread.c\""
)
set_source_files_properties(
	"${HASHLINK_ROOT}/src/gc.c"
	PROPERTIES COMPILE_DEFINITIONS "GC_MAX_MARK_THREADS=1;HL_CONSOLE"
)
set_source_files_properties(
	"${GAME_ROOT}/vita/runtime/src/sdl_vita.c"
	PROPERTIES COMPILE_DEFINITIONS
		"MBHAXE_HASHLINK_SDL_SOURCE=\"${HASHLINK_ROOT}/libs/sdl/sdl.c\""
)
set_source_files_properties(
	"${GAME_ROOT}/vita/runtime/src/gl_vita.c"
	PROPERTIES COMPILE_DEFINITIONS
		"MBHAXE_HASHLINK_GL_SOURCE=\"${HASHLINK_ROOT}/libs/sdl/gl.c\""
)

# Compile the unsafe generated definitions under private names. The Vita-safe
# implementations above retain the public HLC symbols used by generated code.
set_source_files_properties(
	"${HLC_ROOT}/haxe/io/Bytes.c"
	PROPERTIES COMPILE_DEFINITIONS
		"haxe_io_Bytes_getDouble=mbhaxe_hlc_unsafe_getDouble;haxe_io_Bytes_getFloat=mbhaxe_hlc_unsafe_getFloat;haxe_io_Bytes_setFloat=mbhaxe_hlc_unsafe_setFloat"
)

add_executable(marblegame
	${HLC_SOURCES}
	${HASHLINK_SOURCES}
	${NATIVE_BINDING_SOURCES}
)

set_target_properties(marblegame PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	LINKER_LANGUAGE CXX
)

target_compile_definitions(marblegame PRIVATE
	HL_MAKE
	HL_VITA
	HL_MOBILE
	LIBHL_STATIC
	PCRE2_CODE_UNIT_WIDTH=16
	PCRE2_STATIC
	AL_LIBTYPE_STATIC
	MBHAXE_APP_VERSION=\"${APP_VERSION}\"
	MBHAXE_VITA_SCENE_TRACE
)

if(MBHAXE_VITA_GPU_DIAGNOSTICS)
	target_compile_definitions(marblegame PRIVATE
		MBHAXE_VITA_DRAW_SYNC_DIAGNOSTIC
	)
endif()

target_compile_options(marblegame PRIVATE
	-ffunction-sections
	-fdata-sections
	-fno-strict-aliasing
	-Wno-unused-parameter
)

target_include_directories(marblegame PRIVATE
	"${GAME_ROOT}/vita/runtime/include"
	"${VITAGL_VENDOR_ROOT}/source"
	"${HLC_ROOT}"
	"${HASHLINK_ROOT}/src"
	"${HASHLINK_ROOT}/include"
	"${HASHLINK_ROOT}/include/minimp3"
	"${HASHLINK_ROOT}/include/mikktspace"
	"${HASHLINK_ROOT}/libs/sdl"
	"${HASHLINK_ROOT}/libs/fmt"
	"${VITASDK}/arm-vita-eabi/include"
	"${VITASDK}/arm-vita-eabi/include/SDL2"
	"${VITASDK}/arm-vita-eabi/include/AL"
	"${VITASDK}/arm-vita-eabi/include/libpng16"
)

target_link_options(marblegame PRIVATE
	-Wl,--gc-sections
	-Wl,--undefined=sceUserMainThreadStackSize
	-Wl,--wrap=vglMalloc
	-Wl,--wrap=vglCalloc
	-Wl,--wrap=vglRealloc
)

target_link_libraries(marblegame PRIVATE
	pcre2-16
	png16
	turbojpeg
	vorbisfile
	vorbis
	ogg
	z
	openal
	SDL2
	mbhaxe_vitagl
	mathneon
	vitashark
	SceShaccCgExt
	SceShaccCg_stub
	taihen_stub
	SceGxm_stub
	SceDisplay_stub
	SceCtrl_stub
	SceAppMgr_stub
	SceAppUtil_stub
	SceAudio_stub
	SceAudioIn_stub
	SceSysmodule_stub
	SceIofilemgr_stub
	SceCommonDialog_stub
	SceTouch_stub
	SceHid_stub
	SceMotion_stub
	ScePower_stub
	SceProcessmgr_stub
	SceKernelDmacMgr_stub
	SceRtc_stub
	SceLibKernel_stub
	pthread
	stdc++
	m
)

vita_create_self(eboot.bin marblegame STRIPPED)

# vita_create_self uses an explicit custom target on modern CMake and exposes
# eboot.bin only as its byproduct. Depend on the target (rather than treating
# eboot.bin as an independently generated source file) so parallel Make builds
# cannot start VPK packaging before the SELF has been copied into place.
if(TARGET eboot.bin-self)
	set(SELF_TARGET eboot.bin-self)
else()
	set(SELF_TARGET eboot.bin)
endif()
set(SELF_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/eboot.bin.out")

set(TITLE_ID "MBHV00001")
set(PACKAGE_NAME "Marble Blast Ultra")
set(PARAM_SFO "${CMAKE_CURRENT_BINARY_DIR}/param.sfo")
set(VPK_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mbhaxe-vita.vpk")
set(VITA_ICON "${GAME_ROOT}/vita/assets/icon0.png")
set(VITA_ICON_VALIDATOR
	"${CMAKE_CURRENT_LIST_DIR}/cmake/ValidateLiveAreaIcon.cmake")

file(GLOB_RECURSE GAME_ASSETS CONFIGURE_DEPENDS LIST_DIRECTORIES false
	RELATIVE "${GAME_ROOT}" "${GAME_ROOT}/data/*")
set(VPK_ASSET_ARGUMENTS)
set(VPK_ASSET_DEPENDS)
foreach(ASSET IN LISTS GAME_ASSETS)
	list(APPEND VPK_ASSET_ARGUMENTS -a "${GAME_ROOT}/${ASSET}=${ASSET}")
	list(APPEND VPK_ASSET_DEPENDS "${GAME_ROOT}/${ASSET}")
endforeach()

list(APPEND VPK_ASSET_ARGUMENTS -a "${VITA_ICON}=sce_sys/icon0.png")
list(APPEND VPK_ASSET_DEPENDS "${VITA_ICON}" "${VITA_ICON_VALIDATOR}")

add_custom_command(
	OUTPUT "${PARAM_SFO}"
	COMMAND "${VITA_MKSFOEX}" -s "TITLE_ID=${TITLE_ID}" -s "APP_VER=${APP_VERSION}"
		"${PACKAGE_NAME}" "${PARAM_SFO}"
	DEPENDS "${CMAKE_CURRENT_LIST_FILE}"
	VERBATIM
)

add_custom_command(
	OUTPUT "${VPK_OUTPUT}"
	COMMAND "${CMAKE_COMMAND}" "-DINPUT=${VITA_ICON}"
		-P "${VITA_ICON_VALIDATOR}"
	COMMAND "${VITA_PACK_VPK}" -s "${PARAM_SFO}" -b
		"${CMAKE_CURRENT_BINARY_DIR}/eboot.bin" ${VPK_ASSET_ARGUMENTS}
		"${VPK_OUTPUT}"
	DEPENDS "${PARAM_SFO}" "${SELF_OUTPUT}" ${VPK_ASSET_DEPENDS}
	COMMENT "Building ${VPK_OUTPUT}"
	VERBATIM
)

add_custom_target(vpk ALL DEPENDS "${VPK_OUTPUT}")
add_dependencies(vpk ${SELF_TARGET})
