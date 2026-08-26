// vim: set foldmethod=marker foldlevel=0:
#include "ufa.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Shared {{{

static bool
ufa_str_ends_with(const char *str, const char *suffix) {
	if (!str || !suffix) { return false; }

	size_t str_len = strlen(str);
	size_t suffix_len = strlen(suffix);

	if (suffix_len > str_len) { return false; }

	return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * Return the first extension of the chosen filter or `NULL` if there is none.
 *
 * A pattern like "png;jpg" gives "png".
 * The wildcard pattern "*" gives `NULL`.
 */
static const char*
ufa_first_extension(
	void* (*memalign)(ufa_arena_t* arena, size_t size, size_t alignment),
	ufa_arena_t* arena,
	const ufa_filter_t* filters,
	int num_filters,
	int filter_index
) {
	// Filters are optional
	if (num_filters <= 0) { return NULL; }
	// SDL passes -1 when the platform does not report the chosen filter
	if (filter_index < 0) { filter_index = 0; }

	const char* pattern = filters[filter_index].pattern;
	if (strcmp(pattern, "*") == 0) { return NULL; }

	size_t len = strcspn(pattern, ";");
	char* extension = memalign(arena, len + 1, _Alignof(char));
	memcpy(extension, pattern, len);
	extension[len] = '\0';
	return extension;
}

// }}}

#ifndef __EMSCRIPTEN__

// Common {{{

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

_Static_assert(sizeof(SDL_DialogFileFilter) == sizeof(ufa_filter_t), "SDL_DialogFileFilter has a different size");
_Static_assert(offsetof(SDL_DialogFileFilter, name) == offsetof(ufa_filter_t, name), "SDL_DialogFileFilter.name has a different offset");
_Static_assert(offsetof(SDL_DialogFileFilter, pattern) == offsetof(ufa_filter_t, pattern), "SDL_DialogFileFilter.pattern has a different offset");

typedef struct {
	ufa_arena_t* arena;
	void* (*memalign)(ufa_arena_t* arena, size_t size, size_t alignment);

	const ufa_filter_t* filters;
	int num_filters;
	const char* filename;
	const char* error;
	ufa_status_t status;
	SDL_IOStream* stream;
	const char* mode;
} ufa_t;

// A ref is just the copied path.
// C forbids a struct with only a flexible array member so the string is used
// directly instead of being wrapped.
static ufa_file_ref_t*
ufa_ref_file(ufa_t* ufa) {
	if (ufa->status != UFA_OK || ufa->filename == NULL) { return NULL; }

	return (ufa_file_ref_t*)SDL_strdup(ufa->filename);
}

/**
 * Snapshot SDL_GetError into the arena.
 *
 * SDL's error buffer is overwritten by subsequent SDL calls so it cannot be
 * returned directly.
 */
static void
ufa_set_error(ufa_t* ufa) {
	const char* error = SDL_GetError();
	size_t len = strlen(error);
	char* copy = ufa->memalign(ufa->arena, len + 1, _Alignof(char));
	memcpy(copy, error, len + 1);
	ufa->error = copy;
}

static void
ufa_open_file(ufa_t* ufa, const char* filename) {
	size_t len = strlen(filename);
	char* filename_copy = ufa->memalign(ufa->arena, len + 1, _Alignof(char));
	memcpy(filename_copy, filename, len + 1);
	ufa->filename = filename_copy;

	ufa->stream = SDL_IOFromFile(filename, ufa->mode);
	if (ufa->stream != NULL) {
		ufa->status = UFA_OK;
	} else {
		ufa_set_error(ufa);
		ufa->status = UFA_ERROR;
	}
}

static void SDLCALL
ufa_file_callback(void* userdata, const char* const* filelist, int filter) {
	ufa_t* ufa = userdata;

	if (filelist == NULL) {
		ufa_set_error(ufa);
		ufa->status = UFA_ERROR;
	} else if (*filelist == NULL) {
		ufa->status = UFA_CANCELLED;
	} else {
		const char* filename = *filelist;
		const char* extension = ufa_first_extension(
			ufa->memalign, ufa->arena,
			ufa->filters, ufa->num_filters,
			filter
		);
		if (
			ufa->mode[0] == 'w'
			&& extension != NULL
			&& !ufa_str_ends_with(filename, extension)
		) {
			size_t name_buf_len = strlen(filename) + strlen(extension) + 2;
			char* name_buf = ufa->memalign(ufa->arena, name_buf_len, _Alignof(char));
			snprintf(name_buf, name_buf_len, "%s.%s", filename, extension);
			filename = name_buf;
		}

		ufa_open_file(ufa, filename);
	}
}

// }}}

// Open {{{

ufa_open_file_t*
ufa_begin_open_file(ufa_config_t config) {
	ufa_t* ufa = config.memalign(config.arena, sizeof(ufa_t), _Alignof(ufa_t));
	*ufa = (ufa_t){
		.arena = config.arena,
		.memalign = config.memalign,
		.filters = config.filters,
		.num_filters = config.num_filters,
		.status = UFA_PENDING,
		.mode = "r",
	};

	if (config.file_ref != NULL) {
		ufa_open_file(ufa, ufa_file_ref_name(config.file_ref));
	} else if (config.filename != NULL) {
		ufa_open_file(ufa, config.filename);
	} else {
		SDL_ShowOpenFileDialog(
			ufa_file_callback, ufa,
			config.parent_window,
			(const SDL_DialogFileFilter*)config.filters,
			config.num_filters,
			config.directory,
			false
		);
	}

	return (ufa_open_file_t*)ufa;
}

ufa_status_t
ufa_check_open_file(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->status;
}

const char*
ufa_get_open_file_error(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->error;
}

const char*
ufa_get_open_file_name(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->filename;
}

ufa_status_t
ufa_read_open_file(ufa_open_file_t* open_file, void* buf, size_t* size) {
	ufa_t* ufa = (ufa_t*)open_file;
	if (ufa->stream == NULL) {
		ufa->error = "The file is not open";
		*size = 0;
		return UFA_ERROR;
	}

	*size = SDL_ReadIO(ufa->stream, buf, *size);
	SDL_IOStatus status = SDL_GetIOStatus(ufa->stream);
	if (status == SDL_IO_STATUS_READY || status == SDL_IO_STATUS_EOF) {
		return UFA_OK;
	} else {
		ufa_set_error(ufa);
		return UFA_ERROR;
	}
}

void
ufa_end_open_file(ufa_open_file_t* open_file) {
	ufa_t* ufa = (ufa_t*)open_file;
	if (ufa->stream) {
		SDL_CloseIO(ufa->stream);
		ufa->stream = NULL;
	}
}

// }}}

// Save {{{

ufa_save_file_t*
ufa_begin_save_file(ufa_config_t config) {
	ufa_t* ufa = config.memalign(config.arena, sizeof(ufa_t), _Alignof(ufa_t));
	*ufa = (ufa_t){
		.arena = config.arena,
		.memalign = config.memalign,
		.filters = config.filters,
		.num_filters = config.num_filters,
		.status = UFA_PENDING,
		.mode = "w",
	};

	if (config.file_ref != NULL) {
		ufa_open_file(ufa, ufa_file_ref_name(config.file_ref));
	} else if (config.filename != NULL) {
		ufa_open_file(ufa, config.filename);
	} else {
		// SDL_ShowSaveFileDialog accepts a file path as default_location to
		// prefill the name, not just a folder
		const char* default_location = config.directory;
		if (config.suggested_name != NULL) {
			if (config.directory != NULL) {
				size_t len = strlen(config.directory) + strlen(config.suggested_name) + 2;
				char* buf = config.memalign(config.arena, len, _Alignof(char));
				snprintf(buf, len, "%s/%s", config.directory, config.suggested_name);
				default_location = buf;
			} else {
				default_location = config.suggested_name;
			}
		}

		SDL_ShowSaveFileDialog(
			ufa_file_callback, ufa,
			config.parent_window,
			(const SDL_DialogFileFilter*)config.filters,
			config.num_filters,
			default_location
		);
	}

	return (ufa_save_file_t*)ufa;
}

ufa_status_t
ufa_check_save_file(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->status;
}

const char*
ufa_get_save_file_error(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->error;
}

const char*
ufa_get_save_file_name(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->filename;
}

ufa_status_t
ufa_write_save_file(ufa_save_file_t* save_file, const void* buf, size_t* size) {
	ufa_t* ufa = (ufa_t*)save_file;
	if (ufa->stream == NULL) {
		ufa->error = "The file is not open";
		*size = 0;
		return UFA_ERROR;
	}

	*size = SDL_WriteIO(ufa->stream, buf, *size);
	SDL_IOStatus status = SDL_GetIOStatus(ufa->stream);
	if (status == SDL_IO_STATUS_READY || status == SDL_IO_STATUS_EOF) {
		return UFA_OK;
	} else {
		ufa_set_error(ufa);
		return UFA_ERROR;
	}
}

void
ufa_end_save_file(ufa_save_file_t* save_file) {
	ufa_t* ufa = (ufa_t*)save_file;
	if (ufa->stream) {
		SDL_CloseIO(ufa->stream);
		ufa->stream = NULL;
	}
}

// }}}

// Ref {{{

ufa_file_ref_t*
ufa_ref_open_file(ufa_open_file_t* open_file) {
	return ufa_ref_file((ufa_t*)open_file);
}

ufa_file_ref_t*
ufa_ref_save_file(ufa_save_file_t* save_file) {
	return ufa_ref_file((ufa_t*)save_file);
}

const char*
ufa_file_ref_name(const ufa_file_ref_t* ref) {
	return (const char*)ref;
}

void
ufa_release_file_ref(ufa_file_ref_t* ref) {
	SDL_free(ref);
}

// }}}

#else

// Common {{{

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/em_macros.h>

typedef struct {
	ufa_arena_t* arena;
	void* (*memalign)(ufa_arena_t* arena, size_t size, size_t alignment);

	const char* filename;
	const char* error;
	uint8_t status;
	int handle;
} ufa_t;

struct ufa_file_ref_s {
	int js_handle;
	char name[];
};

EMSCRIPTEN_KEEPALIVE void*
ufa_web_malloc(void* ctx, size_t size) {
	ufa_t* ufa = ctx;
	return ufa->memalign(ufa->arena, size, _Alignof(max_align_t));
}

/**
 * Build a comma separated list of extensions (".png,.jpg") from the filters.
 *
 * This is the format of the `accept` attribute of `<input type="file">`.
 * Multi-extension patterns ("png;jpg") are split into separate entries.
 * Wildcards are dropped.
 * Returns an empty string if no concrete extension remains.
 */
static const char*
ufa_web_build_accept(const ufa_config_t* config) {
	// Worst case per pattern: every token gains a ",." prefix
	size_t cap = 1;
	for (int i = 0; i < config->num_filters; ++i) {
		cap += strlen(config->filters[i].pattern) * 2 + 2;
	}

	char* accept = config->memalign(config->arena, cap, _Alignof(char));
	char* itr = accept;
	bool is_first = true;
	for (int i = 0; i < config->num_filters; ++i) {
		const char* pattern = config->filters[i].pattern;
		if (strcmp(pattern, "*") == 0) { continue; }

		while (*pattern != '\0') {
			size_t len = strcspn(pattern, ";");
			if (len > 0) {
				if (!is_first) { *itr++ = ','; }
				*itr++ = '.';
				memcpy(itr, pattern, len);
				itr += len;
				is_first = false;
			}

			pattern += len;
			if (*pattern == ';') { ++pattern; }
		}
	}
	*itr = '\0';

	return accept;
}

// }}}

// Open {{{

extern int
ufa_web_begin_open_file(
	const char* filter,
	int file_ref,
	void* alloc_ctx,
	const char** filename_ptr,
	uint8_t* status_ptr,
	const char** error_ptr
);

extern void
ufa_web_end_open_file(int handle);

extern int
ufa_web_read_open_file(int handle, void* buf, size_t* size);

ufa_open_file_t*
ufa_begin_open_file(ufa_config_t config) {
	ufa_t* ufa = config.memalign(config.arena, sizeof(ufa_t), _Alignof(ufa_t));
	*ufa = (ufa_t){
		.arena = config.arena,
		.memalign = config.memalign,

		.status = UFA_PENDING,
	};

	// With a file_ref, JS reopens the referenced file without a dialog,
	// reading its current content.
	ufa->handle = ufa_web_begin_open_file(
		ufa_web_build_accept(&config),
		config.file_ref != NULL ? config.file_ref->js_handle : 0,
		ufa,
		&ufa->filename,
		&ufa->status,
		&ufa->error
	);

	return (ufa_open_file_t*)ufa;
}

ufa_status_t
ufa_check_open_file(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->status;
}

const char*
ufa_get_open_file_error(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->error;
}

const char*
ufa_get_open_file_name(ufa_open_file_t* open_file) {
	return ((ufa_t*)open_file)->filename;
}

ufa_status_t
ufa_read_open_file(ufa_open_file_t* open_file, void* buf, size_t* size) {
	return ufa_web_read_open_file(((ufa_t*)open_file)->handle, buf, size);
}

void
ufa_end_open_file(ufa_open_file_t* open_file) {
	ufa_web_end_open_file(((ufa_t*)open_file)->handle);
}

// }}}

// Save {{{

extern int
ufa_web_begin_save_file(
	const char* filename,
	const char* accept,
	int show_dialog,
	int file_ref,
	void* alloc_ctx,
	const char** filename_ptr,
	uint8_t* status_ptr,
	const char** error_ptr
);

extern const char*
ufa_web_get_default_name(void* alloc_ctx);

extern int
ufa_web_write_save_file(int handle, const void* buf, size_t* size);

extern void
ufa_web_end_save_file(int handle);

ufa_save_file_t*
ufa_begin_save_file(ufa_config_t config) {
	ufa_t* ufa = config.memalign(config.arena, sizeof(ufa_t), _Alignof(ufa_t));
	*ufa = (ufa_t){
		.arena = config.arena,
		.memalign = config.memalign,
		.status = UFA_PENDING,
		.filename = config.filename,
	};

	if (config.file_ref != NULL) {
		ufa->filename = config.file_ref->name;
	} else if (ufa->filename == NULL) {
		const char* base = config.suggested_name;
		if (base == NULL) { base = ufa_web_get_default_name(ufa); }

		const char* extension = ufa_first_extension(
			config.memalign, config.arena,
			config.filters, config.num_filters,
			0
		);
		if (extension != NULL && !ufa_str_ends_with(base, extension)) {
			int size = snprintf(NULL, 0, "%s.%s", base, extension);
			char* filename = config.memalign(config.arena, size + 1, _Alignof(char));
			snprintf(filename, size + 1, "%s.%s", base, extension);
			base = filename;
		}

		ufa->filename = base;
	}

	// With a file_ref, JS writes back to the referenced file (asking for
	// permission first), falling back to a download named after it.
	// With an explicit filename, the dialog is bypassed and the save is always
	// a plain download.
	// Otherwise, JS will use the save dialog where the browser supports it,
	// updating status and filename asynchronously like the open path.
	// On the download fallback, status is set to UFA_OK before this call
	// returns.
	ufa->handle = ufa_web_begin_save_file(
		ufa->filename,
		ufa_web_build_accept(&config),
		config.file_ref == NULL && config.filename == NULL,
		config.file_ref != NULL ? config.file_ref->js_handle : 0,
		ufa,
		&ufa->filename,
		&ufa->status,
		&ufa->error
	);

	return (ufa_save_file_t*)ufa;
}

ufa_status_t
ufa_check_save_file(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->status;
}

const char*
ufa_get_save_file_error(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->error;
}

const char*
ufa_get_save_file_name(ufa_save_file_t* save_file) {
	return ((ufa_t*)save_file)->filename;
}

ufa_status_t
ufa_write_save_file(ufa_save_file_t* save_file, const void* buf, size_t* size) {
	return ufa_web_write_save_file(((ufa_t*)save_file)->handle, buf, size);
}

void
ufa_end_save_file(ufa_save_file_t* save_file) {
	ufa_web_end_save_file(((ufa_t*)save_file)->handle);
}

// }}}

// Ref {{{

extern int
ufa_web_ref_file(int handle);

extern void
ufa_web_release_file_ref(int ref);

static ufa_file_ref_t*
ufa_ref_file(ufa_t* ufa) {
	if (ufa->status != UFA_OK || ufa->filename == NULL) { return NULL; }

	int js_handle = ufa_web_ref_file(ufa->handle);
	if (js_handle == 0) { return NULL; }

	size_t name_len = strlen(ufa->filename);
	ufa_file_ref_t* ref = malloc(sizeof(ufa_file_ref_t) + name_len + 1);
	if (ref == NULL) {
		ufa_web_release_file_ref(js_handle);
		return NULL;
	}

	ref->js_handle = js_handle;
	memcpy(ref->name, ufa->filename, name_len + 1);

	return ref;
}

ufa_file_ref_t*
ufa_ref_open_file(ufa_open_file_t* open_file) {
	return ufa_ref_file((ufa_t*)open_file);
}

ufa_file_ref_t*
ufa_ref_save_file(ufa_save_file_t* save_file) {
	return ufa_ref_file((ufa_t*)save_file);
}

const char*
ufa_file_ref_name(const ufa_file_ref_t* ref) {
	return ref->name;
}

void
ufa_release_file_ref(ufa_file_ref_t* ref) {
	if (ref == NULL) { return; }

	ufa_web_release_file_ref(ref->js_handle);
	free(ref);
}

// }}}

#endif
