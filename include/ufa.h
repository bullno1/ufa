#ifndef UFA_H
#define UFA_H

/**
 * Universal File Action
 *
 * A native file open and save dialog that even works on web.
 *
 * On most platforms, the dialog and file I/O is handled by SDL:
 *
 * * https://wiki.libsdl.org/SDL3/SDL_ShowOpenFileDialog
 * * https://wiki.libsdl.org/SDL3/SDL_ShowSaveFileDialog
 *
 * On the web, there is a custom implementation using JS.
 * Due to platform restriction, this is approximated:
 *
 * * Opening a file is shown as a file upload dialog.
 * * Saving uses the File System Access API (`showSaveFilePicker`) where the
 *   browser supports it, which shows a real save dialog and streams writes
 *   to disk.
 *   In other browsers, it falls back to triggering a file download.
 *
 * For the save dialog to be used, @ref ufa_begin_save_file must be called
 * during a user gesture (e.g: from a mouse or keyboard event handler) in a
 * secure context (https).
 * Otherwise, the download fallback is used.
 */

#include <stddef.h>

#ifndef UFA_ARENA_TYPE
#	define UFA_ARENA_TYPE void
#endif

/**
 * The file action status
 */
typedef enum {
	//! The user has chosen a file
	UFA_OK,
	//! The user is still deciding
	UFA_PENDING,
	//! The user has closed the dialog without making a choice
	UFA_CANCELLED,
	/**
	 * Some error happened.
	 * Use @ref ufa_get_open_file_error or @ref ufa_get_save_file_error to find out
	 */
	UFA_ERROR,
} ufa_status_t;

typedef struct ufa_open_file_s ufa_open_file_t;

typedef struct ufa_save_file_s ufa_save_file_t;

typedef UFA_ARENA_TYPE ufa_arena_t;

struct SDL_Window;

/**
 * File type filter
 */
typedef struct {
	//! Name of the filter. e.g: "Image"
	const char* name;

	/**
	 * A colon separated list of extensions or the string `"*"`
	 *
	 * For example: "png;jpg"
	 */
	const char* pattern;
} ufa_filter_t;

/**
 * Configuration for the file open or save action
 */
typedef struct {
	/**
	 * An arena allocator for the action
	 *
	 * The library will never free any memory.
	 * It is the responsibility of the caller to reset the arena after the
	 * action has finished.
	 *
	 * @see ufa_config_t::memalign
	 */
	ufa_arena_t* arena;

	/**
	 * Allocate memory from the arena.
	 *
	 * @param arena Reference to the arena
	 * @param size How many bytes to allocate
	 * @param alignment Alignment for allocated memory
	 * @return Allocated memory
	 *
	 * @see ufa_config_t::arena
	 */
	void* (*memalign)(ufa_arena_t* arena, size_t size, size_t alignment);

	/**
	 * The parent window for the open/save file dialog.
	 *
	 * This can be left as `NULL` but the dialog will show up in some OS as
	 * another top-level window instead of a sub window which can be jarring.
	 */
	struct SDL_Window* parent_window;

	//! A list of filters
	const ufa_filter_t* filters;

	//! Number of filters
	int num_filters;

	/**
	 * If set, open the file for reading or writing without a dialog.
	 *
	 * This is not supported on all platforms.
	 * On the web, it would only work for @ref ufa_begin_save_file
	 */
	const char* filename;

	/**
	 * The suggested initial directory
	 *
	 * This can be `NULL` to let the OS pick a default.
	 */
	const char* directory;

	/**
	 * The suggested file name for @ref ufa_begin_save_file
	 *
	 * Unlike @ref ufa_config_t::filename, this does not bypass the dialog.
	 * On native platforms, the save dialog starts with this name prefilled.
	 * On the web, this is used as the download name.
	 *
	 * If the name does not end with the extension of the chosen filter, the
	 * extension will be appended.
	 *
	 * This can be `NULL`.
	 * On the web, a name will be derived from the page title instead.
	 */
	const char* suggested_name;
} ufa_config_t;

/**
 * Show a file open dialog.
 *
 * This will not block the caller.
 * In the main loop of the program, use @ref ufa_check_open_file to poll for
 * the status.
 *
 * @param config Config this open action
 * @return A handle to the open file action
 *
 * @see ufa_check_open_file
 * @see ufa_read_open_file
 * @see ufa_end_open_file
 */
ufa_open_file_t*
ufa_begin_open_file(ufa_config_t config);

/**
 * Check the status of an open file action
 */
ufa_status_t
ufa_check_open_file(ufa_open_file_t* open_file);

/**
 * Get the error during the open file action.
 *
 * If any function returns @ref UFA_ERROR, the reason can be retrieved here.
 * The returned string remains valid until the next `ufa_` function is called.
 */
const char*
ufa_get_open_file_error(ufa_open_file_t* open_file);

/**
 * Return the name of the opened file.
 *
 * This would return `NULL` if @ref ufa_check_open_file returns anything other
 * than @ref UFA_OK
 */
const char*
ufa_get_open_file_name(ufa_open_file_t* open_file);

/**
 * Read the content of an opened file.
 *
 * This generally behaves like `fread`.
 * This function should only be used when @ref ufa_check_open_file has returned
 * @ref UFA_OK.
 * Using this in other situations will always return @ref UFA_ERROR.
 *
 * @param open_file The file returned by @ref ufa_begin_open_file
 * @param buf The buffer to read into
 * @param size How many bytes to read. The number of bytes read will be written back here.
 * @return Status of the read operation
 *
 * @remark Just like fread, short read can happen.
 *   Always use a loop if you expect to read an exact number of bytes.
 *   At the end of a file, `size` will always be 0.
 */
ufa_status_t
ufa_read_open_file(ufa_open_file_t* open_file, void* buf, size_t* size);

/**
 * Close an opened file and free all managed resources.
 *
 * Further interaction with the handle is undefined behaviour.
 *
 * @param open_file The opened file handle to close.
 *
 * @remark Memory allocated through @ref ufa_config_t::memalign will not be freed.
 * It is up to the caller to reset the arena after this call.
 */
void
ufa_end_open_file(ufa_open_file_t* open_file);

/**
 * Show a file save dialog.
 *
 * This will not block the caller.
 * In the main loop of the program, use @ref ufa_check_save_file to poll for
 * the status.
 *
 * @param config Config this save action
 * @return A handle to the save file action
 *
 * @remark On the web, call this during a user gesture (e.g: from a mouse or
 *   keyboard event handler) so that the browser's save dialog can be used
 *   where supported.
 *   Outside of a user gesture, or in browsers without the File System Access
 *   API, the save silently falls back to a file download named after
 *   @ref ufa_config_t::suggested_name.
 *
 * @see ufa_check_save_file
 * @see ufa_write_save_file
 * @see ufa_end_save_file
 */
ufa_save_file_t*
ufa_begin_save_file(ufa_config_t config);

/**
 * Check the status of a save file action
 */
ufa_status_t
ufa_check_save_file(ufa_save_file_t* save_file);

/**
 * Get the error during the save file action.
 *
 * If any function returns @ref UFA_ERROR, the reason can be retrieved here.
 * The returned string remains valid until the next `ufa_` function is called.
 */
const char*
ufa_get_save_file_error(ufa_save_file_t* save_file);

/**
 * Return the name of the saved file.
 *
 * This would return `NULL` if @ref ufa_check_save_file returns anything other
 * than @ref UFA_OK
 */
const char*
ufa_get_save_file_name(ufa_save_file_t* save_file);

/**
 * Write content to a file being saved.
 *
 * This generally behaves like `fwrite`.
 * This function should only be used when @ref ufa_check_save_file has returned
 * @ref UFA_OK.
 * Using this in other situations will always return @ref UFA_ERROR.
 *
 * @param save_file The file returned by @ref ufa_begin_save_file
 * @param buf The buffer to write from
 * @param size How many bytes to write. The number of bytes written will be written back here.
 * @return Status of the write operation
 *
 * @remark Just like fwrite, short write can happen.
 *   Always use a loop if you expect to write an exact number of bytes.
 */
ufa_status_t
ufa_write_save_file(ufa_save_file_t* save_file, const void* buf, size_t* size);

/**
 * Close a saved file and free all managed resources.
 *
 * Further interaction with the handle is undefined behaviour.
 *
 * @param save_file The save file handle to close.
 *
 * @remark Memory allocated through @ref ufa_config_t::memalign will not be freed.
 * It is up to the caller to reset the arena after this call.
 */
void
ufa_end_save_file(ufa_save_file_t* save_file);

#endif
