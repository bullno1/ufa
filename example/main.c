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
} app_t;

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
		"ufa example - O: open (dump to stdout), S: save",
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
	};
}

static void
app_finish_action(app_t* app) {
	app->action = APP_IDLE;
	barena_reset(&app->arena);
}

static void
app_dump_open_file(app_t* app) {
	printf("Opened: %s\n", ufa_get_open_file_name(app->open_file));

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
			} else if (event->key.key == SDLK_S) {
				app->action = APP_SAVING;
				app->save_file = ufa_begin_save_file(app_make_ufa_config(app));
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
					ufa_end_open_file(app->open_file);
					app_finish_action(app);
					break;
				case UFA_CANCELLED:
					printf("Open cancelled\n");
					ufa_end_open_file(app->open_file);
					app_finish_action(app);
					break;
				case UFA_ERROR:
					fprintf(stderr, "Open error: %s\n", ufa_get_open_file_error(app->open_file));
					ufa_end_open_file(app->open_file);
					app_finish_action(app);
					break;
			}
			break;
		case APP_SAVING:
			switch (ufa_check_save_file(app->save_file)) {
				case UFA_PENDING:
					break;
				case UFA_OK:
					app_write_save_file(app);
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

	SDL_DestroyWindow(app->window);
	barena_pool_cleanup(&app->pool);
}
