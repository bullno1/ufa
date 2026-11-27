#define BARENA_IMPLEMENTATION
#include "barena.h"

#define UFA_ARENA_TYPE barena_t
#include <ufa.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

static const ufa_filter_t FILTERS[] = {
	{ .name = "Text", .pattern = "txt" },
	{ .name = "All", .pattern = "*" },
};

static const char SAMPLE_CONTENT[] = "Hello from ufa!\n";

typedef enum {
	APP_IDLE,
	APP_OPENING,
	APP_SAVING,
} app_action_t;

typedef struct {
	SDL_Window* window;

	barena_pool_t pool;
	barena_t arena;

	app_action_t action;
	union {
		ufa_open_file_t* open_file;
		ufa_save_file_t* save_file;
	};
	// Which of the chosen files is being read
	int file_index;

	ufa_file_ref_t* file_ref;
} app_t;

static void
app_retain_file_ref(app_t* app, ufa_file_ref_t* ref) {
	if (ref == NULL) { return; }

	if (app->file_ref != NULL) { ufa_release_file_ref(app->file_ref); }
	app->file_ref = ref;
	printf("Ref: %s\n", ufa_file_ref_name(ref));
}

SDL_AppResult
SDL_AppInit(void** userdata, int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	static app_t app = { 0 };

	app.window = SDL_CreateWindow(
		"ufa example - O: open (dump to stdout), M: open many, R: reload, S: save (back)",
		640, 200,
		0
	);
	if (app.window == NULL) {
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	barena_pool_init(&app.pool, 1024 * 1024);
	barena_init(&app.arena, &app.pool);

	*userdata = &app;
	return SDL_APP_CONTINUE;
}

static ufa_config_t
app_make_ufa_config(app_t* app) {
	return (ufa_config_t){
		.arena = &app->arena,
		.memalign = barena_memalign,
		.parent_window = app->window,
		.filters = FILTERS,
		.num_filters = sizeof(FILTERS) / sizeof(FILTERS[0]),
		// Only used by ufa_begin_save_file
		.suggested_name = "hello.txt",
	};
}

static void
app_finish_action(app_t* app) {
	app->action = APP_IDLE;
	app->file_index = 0;
	barena_reset(&app->arena);
}

/**
 * Move on to the next chosen file or finish the action if there is none left.
 *
 * Selecting a file may take a while (UFA_PENDING) so its result is picked up
 * by the same ufa_check_open_file polling as the dialog.
 */
static void
app_next_open_file(app_t* app) {
	int next_index = app->file_index + 1;
	if (next_index < ufa_get_open_file_count(app->open_file)) {
		app->file_index = next_index;
		ufa_select_open_file(app->open_file, next_index);
	} else {
		ufa_end_open_file(app->open_file);
		app_finish_action(app);
	}
}

static void
app_dump_open_file(app_t* app) {
	int num_files = ufa_get_open_file_count(app->open_file);
	// The names are available up front, before the files are read
	if (app->file_index == 0 && num_files > 1) {
		printf("Chosen %d files:\n", num_files);
		for (int i = 0; i < num_files; ++i) {
			printf("  %s\n", ufa_get_open_file_name_at(app->open_file, i));
		}
	}

	printf(
		"Opened (%d/%d): %s (%zu bytes)\n",
		app->file_index + 1, num_files,
		ufa_get_open_file_name(app->open_file),
		ufa_get_open_file_size(app->open_file)
	);

	char buf[4096];
	while (true) {
		size_t size = sizeof(buf);
		if (ufa_read_open_file(app->open_file, buf, &size) != UFA_OK) {
			fprintf(stderr, "Read error: %s\n", ufa_get_open_file_error(app->open_file));
			break;
		}
		if (size == 0) { break; }

		fwrite(buf, 1, size, stdout);
	}
	fflush(stdout);
}

static void
app_write_save_file(app_t* app) {
	printf("Saving: %s\n", ufa_get_save_file_name(app->save_file));

	const char* itr = SAMPLE_CONTENT;
	size_t remaining = sizeof(SAMPLE_CONTENT) - 1;
	while (remaining > 0) {
		size_t size = remaining;
		if (ufa_write_save_file(app->save_file, itr, &size) != UFA_OK) {
			fprintf(stderr, "Write error: %s\n", ufa_get_save_file_error(app->save_file));
			return;
		}

		itr += size;
		remaining -= size;
	}
}

SDL_AppResult
SDL_AppEvent(void* userdata, SDL_Event* event) {
	app_t* app = userdata;

	switch (event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;
		case SDL_EVENT_KEY_DOWN:
			if (app->action != APP_IDLE) { break; }

			if (event->key.key == SDLK_O) {
				app->action = APP_OPENING;
				app->open_file = ufa_begin_open_file(app_make_ufa_config(app));
			} else if (event->key.key == SDLK_M) {
				app->action = APP_OPENING;
				ufa_config_t config = app_make_ufa_config(app);
				config.allow_multiple = true;
				app->open_file = ufa_begin_open_file(config);
			} else if (event->key.key == SDLK_R && app->file_ref != NULL) {
				app->action = APP_OPENING;
				ufa_config_t config = app_make_ufa_config(app);
				// Reload the previously opened/saved file
				config.file_ref = app->file_ref;
				app->open_file = ufa_begin_open_file(config);
			} else if (event->key.key == SDLK_S) {
				app->action = APP_SAVING;
				ufa_config_t config = app_make_ufa_config(app);
				// Save back to the previously opened/saved file, if any
				config.file_ref = app->file_ref;
				app->save_file = ufa_begin_save_file(config);
			}
			break;
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppIterate(void* userdata) {
	app_t* app = userdata;

	switch (app->action) {
		case APP_IDLE:
			break;
		case APP_OPENING:
			switch (ufa_check_open_file(app->open_file)) {
				case UFA_PENDING:
					break;
				case UFA_OK:
					app_dump_open_file(app);
					app_retain_file_ref(app, ufa_ref_open_file(app->open_file));
					app_next_open_file(app);
					break;
				case UFA_CANCELLED:
					printf("Open cancelled\n");
					ufa_end_open_file(app->open_file);
					app_finish_action(app);
					break;
				case UFA_ERROR:
					// With several files, this only concerns the current one
					fprintf(stderr, "Open error: %s\n", ufa_get_open_file_error(app->open_file));
					app_next_open_file(app);
					break;
			}
			break;
		case APP_SAVING:
			switch (ufa_check_save_file(app->save_file)) {
				case UFA_PENDING:
					break;
				case UFA_OK:
					app_write_save_file(app);
					if (app->file_ref == NULL) {
						app_retain_file_ref(app, ufa_ref_save_file(app->save_file));
					}
					ufa_end_save_file(app->save_file);
					app_finish_action(app);
					break;
				case UFA_CANCELLED:
					printf("Save cancelled\n");
					ufa_end_save_file(app->save_file);
					app_finish_action(app);
					break;
				case UFA_ERROR:
					fprintf(stderr, "Save error: %s\n", ufa_get_save_file_error(app->save_file));
					ufa_end_save_file(app->save_file);
					app_finish_action(app);
					break;
			}
			break;
	}

	return SDL_APP_CONTINUE;
}

void
SDL_AppQuit(void* userdata, SDL_AppResult result) {
	(void)result;
	app_t* app = userdata;
	if (app == NULL) { return; }

	if (app->file_ref != NULL) { ufa_release_file_ref(app->file_ref); }
	SDL_DestroyWindow(app->window);
	barena_pool_cleanup(&app->pool);
}
