#include <vitaGL.h>
#include <hl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vita_boot_log.h"

/* hlsdl normally resolves desktop GL entry points through SDL. VitaGL is a
 * static API, so the import table is intentionally empty. */
#define GL_IMPORT(function, type)

static void vita_gl_unsupported(const char *function) {
	hl_error("%s is not supported by the VitaGL backend", function);
}

static void vita_gl_draw_buffers(GLsizei count, const GLenum *buffers) {
	(void)buffers;
	if (count > 1)
		vita_gl_unsupported("glDrawBuffers (multiple render targets)");
}

static void vita_gl_renderbuffer_storage_multisample(GLenum target,
	GLsizei samples, GLenum format, GLsizei width, GLsizei height) {
	(void)samples;
	glRenderbufferStorage(target, format, width, height);
}

static void vita_gl_vertex_attrib_ipointer(GLuint index, GLint size,
	GLenum type, GLsizei stride, const void *pointer) {
	glVertexAttribPointer(index, size, type, GL_FALSE, stride, pointer);
}

static void vita_gl_enable(GLenum feature) {
	/* Desktop Heaps enables seamless cubemap filtering unconditionally.  It is
	 * not an OpenGL ES 2 feature and VitaGL reports GL_INVALID_ENUM for it. */
	if (feature != 0x884F /* GL_TEXTURE_CUBE_MAP_SEAMLESS */)
		glEnable(feature);
}

static void vita_gl_disable(GLenum feature) {
	if (feature != 0x884F /* GL_TEXTURE_CUBE_MAP_SEAMLESS */)
		glDisable(feature);
}

/* VitaGL represents buffer names as pointers to its vbo objects. Track the
 * public GL lifecycle here and fail closed before a stale name or oversized
 * upload can overwrite allocator metadata. Only the stable prefix of
 * VitaGL's private vbo layout is inspected. */
typedef struct {
	void *ptr;
	int32_t size;
	void *(*alloc_func)(size_t);
	uint32_t last_frame;
} vita_gl_vbo_prefix;

typedef struct {
	GLuint id;
	uint32_t capacity;
	GLenum storage_target;
	unsigned char alive;
	unsigned char has_storage;
} vita_gl_buffer_record;

#define VITA_GL_MAX_BUFFER_RECORDS 2048u
#define VITA_GL_MAX_VERTEX_ARRAY_RECORDS 256u
#define VITA_GL_MAX_VERTEX_ATTRIBUTES 16u
#define VITA_GL_MAX_DIAGNOSTIC_DRAWS 1024u
#define VITA_GL_MAX_ARRAY_INDEX_COUNT 0xC000u
#define VITA_GL_ISOLATED_DRAW_COUNT 6u

typedef struct {
	GLuint id;
	GLuint element_buffer;
	unsigned char alive;
} vita_gl_vertex_array_record;

static vita_gl_buffer_record vita_gl_buffers[VITA_GL_MAX_BUFFER_RECORDS];
static uint32_t vita_gl_buffer_record_count;
static vita_gl_vertex_array_record vita_gl_vertex_arrays[
	VITA_GL_MAX_VERTEX_ARRAY_RECORDS];
static uint32_t vita_gl_vertex_array_record_count;
static vita_gl_vertex_array_record vita_gl_default_vertex_array = { 0, 0, 1 };
static GLuint vita_gl_current_vertex_array;
static GLuint vita_gl_current_program;
static GLuint vita_gl_array_buffer;
static GLuint vita_gl_uniform_buffer;
static uint32_t vita_gl_draw_sequence;
static uint32_t vita_gl_present_sequence;
static uint32_t vita_gl_suppressed_predraw_finishes;
#ifdef MBHAXE_VITA_SCENE_TRACE
static uint32_t vita_gl_scene_trace_count;
#endif
static uint32_t vita_gl_isolated_draw_sync_count;

static vita_gl_vertex_array_record *vita_gl_find_vertex_array(GLuint id) {
	uint32_t index;

	if (id == 0)
		return &vita_gl_default_vertex_array;
	for (index = 0; index < vita_gl_vertex_array_record_count; ++index) {
		if (vita_gl_vertex_arrays[index].id == id)
			return &vita_gl_vertex_arrays[index];
	}
	return NULL;
}

static vita_gl_vertex_array_record *vita_gl_register_vertex_array(GLuint id) {
	vita_gl_vertex_array_record *record;
	uint32_t index;

	record = vita_gl_find_vertex_array(id);
	if (record == NULL) {
		for (index = 0; index < vita_gl_vertex_array_record_count; ++index) {
			if (!vita_gl_vertex_arrays[index].alive) {
				record = &vita_gl_vertex_arrays[index];
				break;
			}
		}
	}
	if (record == NULL && vita_gl_vertex_array_record_count <
		VITA_GL_MAX_VERTEX_ARRAY_RECORDS) {
		record = &vita_gl_vertex_arrays[vita_gl_vertex_array_record_count++];
	}
	if (record != NULL) {
		record->id = id;
		record->element_buffer = 0;
		record->alive = 1;
	}
	return record;
}

static vita_gl_vertex_array_record *vita_gl_current_vertex_array_record(void) {
	return vita_gl_find_vertex_array(vita_gl_current_vertex_array);
}

static GLuint *vita_gl_binding_for_target(GLenum target) {
	vita_gl_vertex_array_record *vertex_array;

	switch (target) {
	case GL_ARRAY_BUFFER:
		return &vita_gl_array_buffer;
	case GL_ELEMENT_ARRAY_BUFFER:
		vertex_array = vita_gl_current_vertex_array_record();
		return vertex_array == NULL ? NULL : &vertex_array->element_buffer;
	case GL_UNIFORM_BUFFER:
		return &vita_gl_uniform_buffer;
	default:
		return NULL;
	}
}

static vita_gl_buffer_record *vita_gl_find_buffer(GLuint id) {
	uint32_t index;

	for (index = 0; index < vita_gl_buffer_record_count; ++index) {
		if (vita_gl_buffers[index].id == id)
			return &vita_gl_buffers[index];
	}
	return NULL;
}

static vita_gl_buffer_record *vita_gl_register_buffer(GLuint id) {
	vita_gl_buffer_record *record;
	uint32_t index;

	record = vita_gl_find_buffer(id);
	if (record == NULL) {
		for (index = 0; index < vita_gl_buffer_record_count; ++index) {
			if (!vita_gl_buffers[index].alive) {
				record = &vita_gl_buffers[index];
				break;
			}
		}
	}
	if (record == NULL &&
		vita_gl_buffer_record_count < VITA_GL_MAX_BUFFER_RECORDS) {
		record = &vita_gl_buffers[vita_gl_buffer_record_count++];
	}
	if (record != NULL) {
		record->id = id;
		record->capacity = 0;
		record->storage_target = 0;
		record->alive = 1;
		record->has_storage = 0;
	}
	return record;
}

static void vita_gl_log_violation(const char *reason, GLenum target,
	GLuint id, const vita_gl_vbo_prefix *actual, uint32_t tracked_capacity,
	GLintptr offset, GLsizeiptr size) {
	char line[256];
	unsigned int backing = actual == NULL ? 0u :
		(unsigned int)(uintptr_t)actual->ptr;
	int actual_size = actual == NULL ? -1 : actual->size;

	(void)snprintf(line, sizeof(line),
		"GLBUF violation=%s target=%04x id=%08x ptr=%08x "
		"tracked=%u actual=%d off=%ld bytes=%ld",
		reason, (unsigned int)target, (unsigned int)id, backing,
		(unsigned int)tracked_capacity, actual_size, (long)offset,
		(long)size);
	mbhaxe_vita_boot_log_write(line);
}

static void vita_gl_log_element_upload(const char *operation, GLenum target,
	GLuint id, const vita_gl_vbo_prefix *actual, uint32_t capacity,
	size_t usable, GLintptr offset, GLsizeiptr size, const void *data) {
	char line[256];
	uint16_t first = 0;
	uint16_t last = 0;
	unsigned int backing = actual == NULL ? 0u :
		(unsigned int)(uintptr_t)actual->ptr;

	if (data != NULL && size >= (GLsizeiptr)sizeof(uint16_t)) {
		memcpy(&first, data, sizeof(first));
		memcpy(&last, (const unsigned char *)data + size - sizeof(last),
			sizeof(last));
		(void)snprintf(line, sizeof(line),
			"GLBUF %s target=%04x id=%08x ptr=%08x cap=%u usable=%lu "
			"off=%ld bytes=%ld u16=%04x..%04x",
			operation, (unsigned int)target, (unsigned int)id, backing,
			(unsigned int)capacity, (unsigned long)usable, (long)offset,
			(long)size,
			(unsigned int)first, (unsigned int)last);
	} else {
		(void)snprintf(line, sizeof(line),
			"GLBUF %s target=%04x id=%08x ptr=%08x cap=%u usable=%lu "
			"off=%ld bytes=%ld u16=none",
			operation, (unsigned int)target, (unsigned int)id, backing,
			(unsigned int)capacity, (unsigned long)usable, (long)offset,
			(long)size);
	}
	mbhaxe_vita_boot_log_write(line);
}

static size_t vita_gl_backing_usable(const vita_gl_vbo_prefix *actual) {
	if (actual == NULL || actual->ptr == NULL)
		return 0;
	return vglMallocUsableSize(actual->ptr);
}

static void vita_gl_tracked_gen_vertex_arrays(GLsizei count, GLuint *arrays) {
	GLsizei index;

	glGenVertexArrays(count, arrays);
	for (index = 0; index < count; ++index) {
		if (arrays[index] != 0 &&
			vita_gl_register_vertex_array(arrays[index]) == NULL) {
			vita_gl_log_violation("vao-registry-full", 0, arrays[index],
				NULL, 0, 0, 0);
		}
	}
}

static void vita_gl_tracked_bind_vertex_array(GLuint id) {
	vita_gl_vertex_array_record *record = vita_gl_find_vertex_array(id);

	if (record == NULL || !record->alive) {
		vita_gl_log_violation("bind-dead-vao", 0, id, NULL, 0, 0, 0);
		return;
	}
	vita_gl_current_vertex_array = id;
	glBindVertexArray(id);
}

static void vita_gl_tracked_delete_vertex_arrays(GLsizei count,
	const GLuint *arrays) {
	GLsizei index;

	for (index = 0; index < count; ++index) {
		GLuint id = arrays[index];
		vita_gl_vertex_array_record *record;

		if (id == 0)
			continue;
		record = vita_gl_find_vertex_array(id);
		if (record == NULL || !record->alive) {
			vita_gl_log_violation("delete-dead-vao", 0, id, NULL, 0,
				0, 0);
			continue;
		}
		if (vita_gl_current_vertex_array == id) {
			glBindVertexArray(0);
			vita_gl_current_vertex_array = 0;
		}
		record->alive = 0;
		record->element_buffer = 0;
		glDeleteVertexArrays(1, &id);
	}
}

static void vita_gl_tracked_gen_buffers(GLsizei count, GLuint *buffers) {
	GLsizei index;

	glGenBuffers(count, buffers);
	for (index = 0; index < count; ++index) {
		if (buffers[index] != 0 &&
			vita_gl_register_buffer(buffers[index]) == NULL) {
			vita_gl_log_violation("registry-full", 0, buffers[index],
				NULL, 0, 0, 0);
		}
	}
}

static void vita_gl_tracked_bind_buffer(GLenum target, GLuint id) {
	GLuint *binding = vita_gl_binding_for_target(target);
	vita_gl_buffer_record *record;

	if (binding == NULL) {
		vita_gl_log_violation("unsupported-bind-target", target, id,
			NULL, 0, 0, 0);
		return;
	}
	if (id != 0) {
		record = vita_gl_find_buffer(id);
		if (record == NULL || !record->alive) {
			vita_gl_log_violation("bind-dead-id", target, id, NULL, 0,
				0, 0);
			return;
		}
	}
	*binding = id;
	glBindBuffer(target, id);
}

static void vita_gl_tracked_bind_buffer_base(GLenum target, GLuint index,
	GLuint id) {
	vita_gl_buffer_record *record;

	if (target != GL_UNIFORM_BUFFER) {
		vita_gl_log_violation("unsupported-bind-base-target", target, id,
			NULL, 0, 0, 0);
		return;
	}
	if (id != 0) {
		record = vita_gl_find_buffer(id);
		if (record == NULL || !record->alive) {
			vita_gl_log_violation("bind-base-dead-id", target, id, NULL,
				0, 0, 0);
			return;
		}
	}
	glBindBufferBase(target, index, id);
}

static vita_gl_buffer_record *vita_gl_bound_record(GLenum target,
	GLuint *id_out) {
	GLuint *binding = vita_gl_binding_for_target(target);
	vita_gl_buffer_record *record;

	if (binding == NULL) {
		vita_gl_log_violation("unsupported-data-target", target, 0, NULL,
			0, 0, 0);
		return NULL;
	}
	*id_out = *binding;
	record = vita_gl_find_buffer(*binding);
	if (*binding == 0 || record == NULL || !record->alive) {
		vita_gl_log_violation("data-without-live-binding", target,
			*binding, NULL, 0, 0, 0);
		return NULL;
	}
	return record;
}

static void vita_gl_tracked_buffer_data(GLenum target, GLsizeiptr size,
	const void *data, GLenum usage) {
	GLuint id = 0;
	vita_gl_buffer_record *record = vita_gl_bound_record(target, &id);
	vita_gl_vbo_prefix *actual;
	size_t usable = 0;

	if (record == NULL)
		return;
	if (size <= 0) {
		vita_gl_log_violation("invalid-data-size", target, id, NULL,
			record->capacity, 0, size);
		return;
	}

	glBufferData(target, size, data, usage);
	actual = (vita_gl_vbo_prefix *)(uintptr_t)id;
	record->storage_target = target;
	record->capacity = (uint32_t)size;
	record->has_storage = actual->ptr != NULL && actual->size == size;
	if (target == GL_ELEMENT_ARRAY_BUFFER) {
		usable = vita_gl_backing_usable(actual);
		vita_gl_log_element_upload("Data", target, id, actual,
			record->capacity, usable, 0, size, data);
	}

	if (!record->has_storage) {
		vita_gl_log_violation("data-allocation-mismatch", target, id,
			actual, record->capacity, 0, size);
	} else if (target == GL_ELEMENT_ARRAY_BUFFER &&
		usable < record->capacity) {
		record->has_storage = 0;
		vita_gl_log_violation("data-backing-too-small", target, id,
			actual, record->capacity, 0, size);
	}
}

static void vita_gl_tracked_buffer_sub_data(GLenum target, GLintptr offset,
	GLsizeiptr size, const void *data) {
	GLuint id = 0;
	vita_gl_buffer_record *record = vita_gl_bound_record(target, &id);
	vita_gl_vbo_prefix *actual;
	uint64_t end;
	size_t usable = 0;

	if (record == NULL)
		return;
	actual = (vita_gl_vbo_prefix *)(uintptr_t)id;
	if (offset < 0 || size <= 0) {
		vita_gl_log_violation("invalid-subdata-range", target, id, actual,
			record->capacity, offset, size);
		return;
	}
	if (data == NULL) {
		vita_gl_log_violation("null-subdata-source", target, id, actual,
			record->capacity, offset, size);
		return;
	}
	end = (uint64_t)offset + (uint64_t)size;
	if (!record->has_storage || actual->ptr == NULL) {
		vita_gl_log_violation("subdata-without-storage", target, id,
			actual, record->capacity, offset, size);
		return;
	}
	if (actual->size <= 0 || (uint32_t)actual->size != record->capacity) {
		vita_gl_log_violation("subdata-capacity-mismatch", target, id,
			actual, record->capacity, offset, size);
		return;
	}
	if (end > record->capacity) {
		vita_gl_log_violation("subdata-out-of-bounds", target, id, actual,
			record->capacity, offset, size);
		return;
	}

	if (target == GL_ELEMENT_ARRAY_BUFFER) {
		usable = vita_gl_backing_usable(actual);
		vita_gl_log_element_upload("SubData", target, id, actual,
			record->capacity, usable, offset, size, data);
		if (usable < record->capacity) {
			record->has_storage = 0;
			vita_gl_log_violation("subdata-backing-too-small", target,
				id, actual, record->capacity, offset, size);
			return;
		}
	}
	glBufferSubData(target, offset, size, data);
}

static void vita_gl_tracked_delete_buffers(GLsizei count,
	const GLuint *buffers) {
	GLsizei index;

	for (index = 0; index < count; ++index) {
		GLuint id = buffers[index];
		vita_gl_buffer_record *record;
		vita_gl_vbo_prefix *actual;

		if (id == 0)
			continue;
		record = vita_gl_find_buffer(id);
		if (record == NULL || !record->alive) {
			vita_gl_log_violation("delete-dead-id", 0, id, NULL, 0, 0, 0);
			continue;
		}
		actual = (vita_gl_vbo_prefix *)(uintptr_t)id;
		if (record->storage_target == GL_ELEMENT_ARRAY_BUFFER) {
			vita_gl_log_element_upload("Delete",
				record->storage_target, id, actual, record->capacity,
				vita_gl_backing_usable(actual), 0, 0, NULL);
		}
		if (vita_gl_array_buffer == id) {
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			vita_gl_array_buffer = 0;
		}
		if (vita_gl_uniform_buffer == id) {
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
			vita_gl_uniform_buffer = 0;
		}
		/* Element bindings live in VAOs.  Clear every reference through
		 * vitaGL before freeing the pointer-backed buffer name; otherwise a
		 * later VAO bind resurrects a dangling EBO. */
		{
			GLuint saved_vertex_array = vita_gl_current_vertex_array;
			uint32_t vao_index;

			if (vita_gl_default_vertex_array.element_buffer == id) {
				glBindVertexArray(0);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				vita_gl_default_vertex_array.element_buffer = 0;
			}
			for (vao_index = 0;
				vao_index < vita_gl_vertex_array_record_count; ++vao_index) {
				vita_gl_vertex_array_record *vertex_array =
					&vita_gl_vertex_arrays[vao_index];
				if (vertex_array->alive &&
					vertex_array->element_buffer == id) {
					glBindVertexArray(vertex_array->id);
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
					vertex_array->element_buffer = 0;
				}
			}
			glBindVertexArray(saved_vertex_array);
		}
		record->alive = 0;
		record->has_storage = 0;
		glDeleteBuffers(1, &id);
	}
}

static int vita_gl_valid_draw_mode(GLenum mode) {
	switch (mode) {
	case GL_POINTS:
	case GL_LINES:
	case GL_LINE_LOOP:
	case GL_LINE_STRIP:
	case GL_TRIANGLES:
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
	case GL_QUADS:
	case GL_QUAD_STRIP:
	case GL_POLYGON:
		return 1;
	default:
		return 0;
	}
}

static uint32_t vita_gl_index_width(GLenum type) {
	switch (type) {
	case GL_UNSIGNED_BYTE:
		return 1;
	case GL_UNSIGNED_SHORT:
		return 2;
	case GL_UNSIGNED_INT:
		return 4;
	default:
		return 0;
	}
}

static uint32_t vita_gl_attribute_width(GLenum type) {
	switch (type) {
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
		return 1;
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_HALF_FLOAT:
	case GL_HALF_FLOAT_OES:
		return 2;
	case GL_INT:
	case GL_UNSIGNED_INT:
	case GL_FLOAT:
	case GL_FIXED:
		return 4;
	default:
		return 0;
	}
}

static void vita_gl_log_vertex_attributes(uint32_t draw_sequence,
	uint32_t maximum_index) {
#ifdef MBHAXE_VITA_DRAW_SYNC_DIAGNOSTIC
	GLuint attribute;

	if (vita_gl_present_sequence != 0 ||
		draw_sequence > VITA_GL_ISOLATED_DRAW_COUNT)
		return;
	for (attribute = 0; attribute < VITA_GL_MAX_VERTEX_ATTRIBUTES;
		++attribute) {
		GLint enabled = GL_FALSE;
		GLint buffer_id = 0;
		GLint components = 0;
		GLint stride = 0;
		GLint type = 0;
		GLint normalized = GL_FALSE;
		void *pointer = NULL;
		vita_gl_buffer_record *record;
		uint32_t width;
		uint64_t required_end = 0;
		int valid = 0;
		char line[256];

		glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
			&enabled);
		if (!enabled)
			continue;
		glGetVertexAttribiv(attribute,
			GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buffer_id);
		glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_SIZE,
			&components);
		glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
			&stride);
		glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
		glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
			&normalized);
		glGetVertexAttribPointerv(attribute,
			GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);

		record = vita_gl_find_buffer((GLuint)buffer_id);
		width = vita_gl_attribute_width((GLenum)type);
		if (components > 0 && components <= 4 && stride > 0 && width > 0) {
			required_end = (uint64_t)(uintptr_t)pointer +
				(uint64_t)maximum_index * (uint32_t)stride +
				(uint32_t)components * width;
			valid = record != NULL && record->alive && record->has_storage &&
				required_end <= record->capacity;
		}
		(void)snprintf(line, sizeof(line),
			"GLATTR draw=%lu slot=%lu vbo=%08x cap=%u "
			"off=%lu stride=%ld comps=%ld type=%04x norm=%ld "
			"max=%lu end=%lu valid=%d",
			(unsigned long)draw_sequence, (unsigned long)attribute,
			(unsigned int)buffer_id,
			record == NULL ? 0u : record->capacity,
			(unsigned long)(uintptr_t)pointer, (long)stride,
			(long)components, (unsigned int)type, (long)normalized,
			(unsigned long)maximum_index, (unsigned long)required_end, valid);
		mbhaxe_vita_boot_log_write(line);
	}
#else
	(void)draw_sequence;
	(void)maximum_index;
#endif
}

static int vita_gl_validate_element_draw(GLenum mode, GLsizei count,
	GLenum type, const void *indices, vita_gl_buffer_record **record_out,
	vita_gl_vbo_prefix **actual_out, size_t *usable_out,
	uint32_t *min_index_out, uint32_t *max_index_out) {
	vita_gl_vertex_array_record *vertex_array =
		vita_gl_current_vertex_array_record();
	vita_gl_buffer_record *record;
	vita_gl_vbo_prefix *actual = NULL;
	GLuint id = vertex_array == NULL ? 0 : vertex_array->element_buffer;
	uint32_t width = vita_gl_index_width(type);
	uintptr_t offset = (uintptr_t)indices;
	uint64_t byte_count;
	uint64_t end;
	size_t usable = 0;
	uint32_t minimum = UINT32_MAX;
	uint32_t maximum = 0;
	GLsizei index;

	if (!vita_gl_valid_draw_mode(mode)) {
		vita_gl_log_violation("draw-invalid-mode", GL_ELEMENT_ARRAY_BUFFER,
			id, NULL, 0, (GLintptr)offset, count);
		return 0;
	}
	if (count <= 0) {
		vita_gl_log_violation("draw-invalid-count", GL_ELEMENT_ARRAY_BUFFER,
			id, NULL, 0, (GLintptr)offset, count);
		return 0;
	}
	if (width == 0) {
		vita_gl_log_violation("draw-invalid-index-type",
			GL_ELEMENT_ARRAY_BUFFER, id, NULL, 0, (GLintptr)offset, count);
		return 0;
	}
	if ((offset & (width - 1u)) != 0) {
		vita_gl_log_violation("draw-misaligned-index-offset",
			GL_ELEMENT_ARRAY_BUFFER, id, NULL, 0, (GLintptr)offset, count);
		return 0;
	}
	if (vertex_array == NULL || !vertex_array->alive || id == 0) {
		vita_gl_log_violation("draw-without-element-buffer",
			GL_ELEMENT_ARRAY_BUFFER, id, NULL, 0, (GLintptr)offset, count);
		return 0;
	}
	record = vita_gl_find_buffer(id);
	/* OpenGL buffer storage is target-agnostic.  A buffer can be allocated
	 * through GL_ARRAY_BUFFER and later become this VAO's element buffer. */
	if (record == NULL || !record->alive || !record->has_storage) {
		vita_gl_log_violation("draw-dead-element-buffer",
			GL_ELEMENT_ARRAY_BUFFER, id, NULL,
			record == NULL ? 0 : record->capacity,
			(GLintptr)offset, count);
		return 0;
	}
	actual = (vita_gl_vbo_prefix *)(uintptr_t)id;
	usable = vita_gl_backing_usable(actual);
	byte_count = (uint64_t)(uint32_t)count * width;
	end = (uint64_t)offset + byte_count;
	if (actual->ptr == NULL || actual->size <= 0 ||
		(uint32_t)actual->size != record->capacity ||
		end > record->capacity || end > usable) {
		vita_gl_log_violation("draw-index-range-out-of-bounds",
			GL_ELEMENT_ARRAY_BUFFER, id, actual, record->capacity,
			(GLintptr)offset, (GLsizeiptr)byte_count);
		return 0;
	}

	for (index = 0; index < count; ++index) {
		const unsigned char *source = (const unsigned char *)actual->ptr +
			offset + (uint32_t)index * width;
		uint32_t value = 0;

		memcpy(&value, source, width);
		if (value < minimum)
			minimum = value;
		if (value > maximum)
			maximum = value;
	}
	*record_out = record;
	*actual_out = actual;
	*usable_out = usable;
	*min_index_out = minimum;
	*max_index_out = maximum;
	return 1;
}

static int vita_gl_validate_array_draw(GLenum mode, GLint first,
	GLsizei count, GLsizei instance_count, uint32_t *submitted_out) {
	uint64_t submitted;

	if (!vita_gl_valid_draw_mode(mode) || first < 0 || count <= 0 ||
		instance_count <= 0) {
		vita_gl_log_violation("draw-invalid-array-range", 0,
			vita_gl_current_vertex_array, NULL, 0, first, count);
		return 0;
	}
	submitted = (uint32_t)count;
	switch (mode) {
	case GL_QUADS:
		submitted = ((uint64_t)(uint32_t)count / 2u) * 3u;
		break;
	case GL_LINE_STRIP:
		submitted = ((uint64_t)(uint32_t)count - 1u) * 2u;
		break;
	case GL_LINE_LOOP:
		submitted = (uint64_t)(uint32_t)count * 2u;
		break;
	default:
		break;
	}
	if (submitted == 0 || submitted > VITA_GL_MAX_ARRAY_INDEX_COUNT ||
		(uint64_t)(uint32_t)first + (uint32_t)count > INT32_MAX ||
		submitted * (uint32_t)instance_count > UINT32_MAX) {
		vita_gl_log_violation("draw-array-index-limit", 0,
			vita_gl_current_vertex_array, NULL,
			VITA_GL_MAX_ARRAY_INDEX_COUNT, first, count);
		return 0;
	}
	*submitted_out = (uint32_t)submitted;
	return 1;
}

static int vita_gl_draw_diagnostic_active(uint32_t sequence) {
#ifdef MBHAXE_VITA_SCENE_TRACE
	/* The crash consistently happens within the first two frames. Log both
	 * without inserting glFinish or otherwise changing GXM scene lifetime. */
	if (vita_gl_present_sequence == 0 &&
		vita_gl_scene_trace_count++ < 128)
		return 1;
#endif
#ifdef MBHAXE_VITA_DRAW_SYNC_DIAGNOSTIC
	return vita_gl_present_sequence < 2 &&
		sequence <= VITA_GL_MAX_DIAGNOSTIC_DRAWS;
#else
	(void)sequence;
	return 0;
#endif
}

static void vita_gl_log_draw_return(uint32_t sequence, int diagnostic) {
	char line[128];

	if (!diagnostic)
		return;
	(void)snprintf(line, sizeof(line),
		"GLDRAW return seq=%lu frame=%lu",
		(unsigned long)sequence, (unsigned long)vita_gl_present_sequence);
	mbhaxe_vita_boot_log_write(line);
#ifdef MBHAXE_VITA_DRAW_SYNC_DIAGNOSTIC
	if (vita_gl_present_sequence == 0 &&
		sequence <= VITA_GL_ISOLATED_DRAW_COUNT) {
		GLint framebuffer = 0;

		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
		if (framebuffer != 0) {
			(void)snprintf(line, sizeof(line),
				"GLDRAW sync skipped seq=%lu framebuffer=%08x",
				(unsigned long)sequence, (unsigned int)framebuffer);
			mbhaxe_vita_boot_log_write(line);
			return;
		}
		(void)snprintf(line, sizeof(line),
			"GLDRAW sync begin seq=%lu", (unsigned long)sequence);
		mbhaxe_vita_boot_log_write(line);
		glFinish();
		++vita_gl_isolated_draw_sync_count;
		(void)snprintf(line, sizeof(line),
			"GLDRAW sync complete seq=%lu", (unsigned long)sequence);
		mbhaxe_vita_boot_log_write(line);
	}
#endif
}

static void vita_gl_tracked_draw_elements(GLenum mode, GLsizei count,
	GLenum type, const void *indices) {
	vita_gl_buffer_record *record;
	vita_gl_vbo_prefix *actual;
	size_t usable;
	uint32_t minimum;
	uint32_t maximum;
	uint32_t sequence = ++vita_gl_draw_sequence;
	int diagnostic = vita_gl_draw_diagnostic_active(sequence);
	char line[320];

	if (!vita_gl_validate_element_draw(mode, count, type, indices, &record,
		&actual, &usable, &minimum, &maximum))
		return;
	if (diagnostic) {
		(void)snprintf(line, sizeof(line),
			"GLDRAW begin seq=%lu frame=%lu kind=E vao=%08x prog=%08x "
			"mode=%04x count=%ld type=%04x off=%lu ebo=%08x ptr=%08x "
			"cap=%u usable=%lu idx=%lu..%lu last=%lu",
			(unsigned long)sequence, (unsigned long)vita_gl_present_sequence,
			(unsigned int)vita_gl_current_vertex_array,
			(unsigned int)vita_gl_current_program, (unsigned int)mode,
			(long)count, (unsigned int)type,
			(unsigned long)(uintptr_t)indices, (unsigned int)record->id,
			(unsigned int)(uintptr_t)actual->ptr, record->capacity,
			(unsigned long)usable, (unsigned long)minimum,
			(unsigned long)maximum, (unsigned long)actual->last_frame);
		mbhaxe_vita_boot_log_write(line);
		vita_gl_log_vertex_attributes(sequence, maximum);
	}
	glDrawElements(mode, count, type, indices);
	vita_gl_log_draw_return(sequence, diagnostic);
}

static void vita_gl_tracked_draw_elements_instanced(GLenum mode,
	GLsizei count, GLenum type, const void *indices, GLsizei instance_count) {
	vita_gl_buffer_record *record;
	vita_gl_vbo_prefix *actual;
	size_t usable;
	uint32_t minimum;
	uint32_t maximum;
	uint32_t sequence = ++vita_gl_draw_sequence;
	int diagnostic = vita_gl_draw_diagnostic_active(sequence);
	char line[320];

	if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
		vita_gl_log_violation("draw-instanced-index-type",
			GL_ELEMENT_ARRAY_BUFFER, 0, NULL, 0, 0, type);
		return;
	}
	if (instance_count <= 0 ||
		(uint64_t)(uint32_t)count * (uint32_t)instance_count > UINT32_MAX) {
		vita_gl_log_violation("draw-invalid-instance-count",
			GL_ELEMENT_ARRAY_BUFFER, 0, NULL, 0, instance_count, count);
		return;
	}
	if (!vita_gl_validate_element_draw(mode, count, type, indices, &record,
		&actual, &usable, &minimum, &maximum))
		return;
	if (diagnostic) {
		(void)snprintf(line, sizeof(line),
			"GLDRAW begin seq=%lu frame=%lu kind=EI vao=%08x prog=%08x "
			"mode=%04x count=%ld inst=%ld type=%04x off=%lu ebo=%08x "
			"ptr=%08x cap=%u usable=%lu idx=%lu..%lu last=%lu",
			(unsigned long)sequence, (unsigned long)vita_gl_present_sequence,
			(unsigned int)vita_gl_current_vertex_array,
			(unsigned int)vita_gl_current_program, (unsigned int)mode,
			(long)count, (long)instance_count, (unsigned int)type,
			(unsigned long)(uintptr_t)indices, (unsigned int)record->id,
			(unsigned int)(uintptr_t)actual->ptr, record->capacity,
			(unsigned long)usable, (unsigned long)minimum,
			(unsigned long)maximum, (unsigned long)actual->last_frame);
		mbhaxe_vita_boot_log_write(line);
		vita_gl_log_vertex_attributes(sequence, maximum);
	}
	glDrawElementsInstanced(mode, count, type, indices, instance_count);
	vita_gl_log_draw_return(sequence, diagnostic);
}

static void vita_gl_tracked_draw_arrays(GLenum mode, GLint first,
	GLsizei count) {
	uint32_t submitted;
	uint32_t sequence = ++vita_gl_draw_sequence;
	int diagnostic = vita_gl_draw_diagnostic_active(sequence);
	char line[224];

	if (!vita_gl_validate_array_draw(mode, first, count, 1, &submitted))
		return;
	if (diagnostic) {
		(void)snprintf(line, sizeof(line),
			"GLDRAW begin seq=%lu frame=%lu kind=A vao=%08x prog=%08x "
			"mode=%04x first=%ld count=%ld submitted=%lu array_binding=%08x",
			(unsigned long)sequence, (unsigned long)vita_gl_present_sequence,
			(unsigned int)vita_gl_current_vertex_array,
			(unsigned int)vita_gl_current_program, (unsigned int)mode,
			(long)first, (long)count, (unsigned long)submitted,
			(unsigned int)vita_gl_array_buffer);
		mbhaxe_vita_boot_log_write(line);
		vita_gl_log_vertex_attributes(sequence,
			(uint32_t)first + (uint32_t)count - 1u);
	}
	glDrawArrays(mode, first, count);
	vita_gl_log_draw_return(sequence, diagnostic);
}

static void vita_gl_tracked_draw_arrays_instanced(GLenum mode, GLint first,
	GLsizei count, GLsizei instance_count) {
	uint32_t submitted;
	uint32_t sequence = ++vita_gl_draw_sequence;
	int diagnostic = vita_gl_draw_diagnostic_active(sequence);
	char line[240];

	if (!vita_gl_validate_array_draw(mode, first, count, instance_count,
		&submitted))
		return;
	if (diagnostic) {
		(void)snprintf(line, sizeof(line),
			"GLDRAW begin seq=%lu frame=%lu kind=AI vao=%08x prog=%08x "
			"mode=%04x first=%ld count=%ld inst=%ld submitted=%lu "
			"array_binding=%08x",
			(unsigned long)sequence, (unsigned long)vita_gl_present_sequence,
			(unsigned int)vita_gl_current_vertex_array,
			(unsigned int)vita_gl_current_program, (unsigned int)mode,
			(long)first, (long)count, (long)instance_count,
			(unsigned long)submitted, (unsigned int)vita_gl_array_buffer);
		mbhaxe_vita_boot_log_write(line);
		vita_gl_log_vertex_attributes(sequence,
			(uint32_t)first + (uint32_t)count - 1u);
	}
	glDrawArraysInstanced(mode, first, count, instance_count);
	vita_gl_log_draw_return(sequence, diagnostic);
}

static void vita_gl_tracked_clear(GLbitfield mask) {
	uint32_t sequence = ++vita_gl_draw_sequence;
	int diagnostic = vita_gl_draw_diagnostic_active(sequence);
	char line[160];

	if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
		GL_STENCIL_BUFFER_BIT)) != 0) {
		vita_gl_log_violation("clear-invalid-mask", 0, 0, NULL, 0, 0,
			mask);
		return;
	}
	if (diagnostic) {
		(void)snprintf(line, sizeof(line),
			"GLDRAW begin seq=%lu frame=%lu kind=C vao=%08x prog=%08x mask=%08x",
			(unsigned long)sequence, (unsigned long)vita_gl_present_sequence,
			(unsigned int)vita_gl_current_vertex_array,
			(unsigned int)vita_gl_current_program, (unsigned int)mask);
		mbhaxe_vita_boot_log_write(line);
	}
	glClear(mask);
	vita_gl_log_draw_return(sequence, diagnostic);
}

static void vita_gl_tracked_use_program(GLuint program) {
	vita_gl_current_program = program;
	glUseProgram(program);
}

static void vita_gl_tracked_finish(void) {
	/* Heaps calls GL.finish() while growing its CPU-side upload stream and once
	 * during driver initialization. Before the first draw there is no submitted
	 * GPU work to wait for, but vitaGL's glFinish() still begins and ends a GXM
	 * display scene. MBHaxe performs enough initial uploads to exceed GXM's
	 * eight-scenes-per-render-target budget before the first swap, leaving the
	 * completed display surface untouched. Defer only these provably empty
	 * pre-draw finishes; once any draw has been submitted, retain the original
	 * synchronization contract used to protect Heaps' recycled upload memory. */
	if (vita_gl_present_sequence == 0 && vita_gl_draw_sequence == 0) {
		char line[112];

		++vita_gl_suppressed_predraw_finishes;
		if (vita_gl_suppressed_predraw_finishes <= 16) {
			(void)snprintf(line, sizeof(line),
				"GLFINISH deferred pre-draw count=%lu",
				(unsigned long)vita_gl_suppressed_predraw_finishes);
			mbhaxe_vita_boot_log_write(line);
		}
		return;
	}
	glFinish();
}

void mbhaxe_vita_gl_present_begin(uint32_t sequence) {
	char line[128];
	GLint framebuffer = 0;

	vita_gl_present_sequence = sequence;
#ifdef MBHAXE_VITA_DRAW_SYNC_DIAGNOSTIC
	if (sequence <= 2) {
		/* When every first-frame operation was already isolated, another
		 * finish would add no information and consume the eighth scene. */
		if (sequence == 1 && vita_gl_draw_sequence > 0 &&
			vita_gl_draw_sequence <= VITA_GL_ISOLATED_DRAW_COUNT &&
			vita_gl_isolated_draw_sync_count == vita_gl_draw_sequence) {
			(void)snprintf(line, sizeof(line),
				"GLFRAME sync covered seq=%lu draws=%lu",
				(unsigned long)sequence,
				(unsigned long)vita_gl_draw_sequence);
			mbhaxe_vita_boot_log_write(line);
			return;
		}
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
		if (framebuffer != 0) {
			(void)snprintf(line, sizeof(line),
				"GLFRAME sync skipped seq=%lu framebuffer=%08x",
				(unsigned long)sequence, (unsigned int)framebuffer);
			mbhaxe_vita_boot_log_write(line);
			return;
		}
		(void)snprintf(line, sizeof(line),
			"GLFRAME sync begin seq=%lu draws=%lu",
			(unsigned long)sequence, (unsigned long)vita_gl_draw_sequence);
		mbhaxe_vita_boot_log_write(line);
		/* The display target reserves eight scenes in this diagnostic build.
		 * At most six isolated finishes plus this boundary finish are issued,
		 * leaving the final scene for vglSwapBuffers. */
		glFinish();
		(void)snprintf(line, sizeof(line),
			"GLFRAME sync complete seq=%lu draws=%lu",
			(unsigned long)sequence, (unsigned long)vita_gl_draw_sequence);
		mbhaxe_vita_boot_log_write(line);
	}
#else
	(void)line;
	(void)framebuffer;
#endif
}

#define glBindFragDataLocation(...) ((void)0)
#define glBindVertexArray vita_gl_tracked_bind_vertex_array
#define glBindBuffer vita_gl_tracked_bind_buffer
#define glBindBufferBase vita_gl_tracked_bind_buffer_base
#define glBindImageTexture(...) vita_gl_unsupported("glBindImageTexture")
#define glBufferData vita_gl_tracked_buffer_data
#define glBufferSubData vita_gl_tracked_buffer_sub_data
#define glClear vita_gl_tracked_clear
#define glColorMaski(index, red, green, blue, alpha) \
	glColorMask(red, green, blue, alpha)
#define glCompressedTexImage3D(...) vita_gl_unsupported("glCompressedTexImage3D")
#define glCompressedTexSubImage2D(...) vita_gl_unsupported("glCompressedTexSubImage2D")
#define glCompressedTexSubImage3D(...) vita_gl_unsupported("glCompressedTexSubImage3D")
#define glDispatchCompute(...) vita_gl_unsupported("glDispatchCompute")
#define glDeleteBuffers vita_gl_tracked_delete_buffers
#define glDeleteVertexArrays vita_gl_tracked_delete_vertex_arrays
#define glDrawBuffers vita_gl_draw_buffers
#define glDrawArrays vita_gl_tracked_draw_arrays
#define glDrawArraysInstanced vita_gl_tracked_draw_arrays_instanced
#define glDrawElements vita_gl_tracked_draw_elements
#define glDrawElementsInstanced vita_gl_tracked_draw_elements_instanced
#define glDisable vita_gl_disable
#define glEnable vita_gl_enable
#define glFramebufferTextureLayer(...) vita_gl_unsupported("glFramebufferTextureLayer")
#define glFinish vita_gl_tracked_finish
#define glMemoryBarrier(...) vita_gl_unsupported("glMemoryBarrier")
#define glGenBuffers vita_gl_tracked_gen_buffers
#define glGenVertexArrays vita_gl_tracked_gen_vertex_arrays
#define glRenderbufferStorageMultisample vita_gl_renderbuffer_storage_multisample
#define glTexImage2DMultisample(...) vita_gl_unsupported("glTexImage2DMultisample")
#define glTexImage3D(...) vita_gl_unsupported("glTexImage3D")
#define glTexSubImage3D(...) vita_gl_unsupported("glTexSubImage3D")
#define glUseProgram vita_gl_tracked_use_program
#define glVertexAttribIPointer vita_gl_vertex_attrib_ipointer

#undef HL_NAME
#ifndef MBHAXE_HASHLINK_GL_SOURCE
#error "MBHAXE_HASHLINK_GL_SOURCE must name the upstream HashLink GL binding"
#endif
#include MBHAXE_HASHLINK_GL_SOURCE
