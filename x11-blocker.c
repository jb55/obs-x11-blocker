
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <pthread.h>
#include <obs/graphics/image-file.h>
#include <obs/obs-module.h>
#include <obs/util/platform.h>
#include <obs/util/dstr.h>

#include <ctype.h>
#include <sys/stat.h>

#define MAX_WINDOWS 128
#define S_WINDOWS   "windows"

struct window {
	int is_mapped;
};

struct x11_blocker_source {
	obs_source_t *source;
	gs_image_file_t image;
	pthread_t thread;
	pthread_mutex_t mutex;
	bool activated;

	float        update_time_elapsed;
	time_t       file_timestamp;
	char         *file; // image to draw over window

	DARRAY(char*) blocked_windows;

	int window_count;
	struct window windows[MAX_WINDOWS];
};


static void add_blocked_window(struct darray *array, const char *name)
{
	DARRAY(char*) new_windows;
	new_windows.da = *array;
	const char *new_window = bstrdup(name);
	da_push_back(new_windows, &new_window);
	*array = new_windows.da;
}


static const char *x11_blocker_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("X11Blocker");
}


static time_t get_modified_timestamp(const char *filename)
{
	struct stat stats;
	if (os_stat(filename, &stats) != 0)
		return -1;
	return stats.st_mtime;
}

static void *x11_process_window(struct x11_blocker_source *ctx, Window *window) {
}

static void *x11_blocker_listen(void *data)
{
	//struct x11_blocker_source *context = data;

	// TODO: close display on unload
	Display* d = XOpenDisplay(NULL);
	Window root = DefaultRootWindow(d);
	XClassHint chint;
	Window curFocus;
	int revert;

	XGetInputFocus (d, &curFocus, &revert);
	XSelectInput(d, root, SubstructureNotifyMask | VisibilityChangeMask);

	while (1)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		switch (ev.type)
		{
		case UnmapNotify:
			XGetClassHint(d, ev.xunmap.event, &chint);
			/* ev.xunmap.window */
			printf ("%s unmapped\n", chint.res_name);
			break;

		case MapNotify:
			XGetClassHint(d, ev.xmap.window, &chint);
			printf ("%s mapped\n", chint.res_name);
			break;

		case VisibilityNotify:
			XGetClassHint(d, ev.xvisibility.window, &chint);
			printf("%s visibility state: ", chint.res_name);
			switch (ev.xvisibility.state) {
			case VisibilityFullyObscured:
				printf("fully obscured\n");
				break;
			case VisibilityPartiallyObscured:
				printf("partially obscured\n");
				break;
			case VisibilityUnobscured:
				printf("unobscured\n");
				break;
			}
		
		/* default: */
		/* 	printf("%d\n", ev.type); */
		}

	}

	return NULL;
}

static void x11_blocker_start_listener(struct x11_blocker_source *context)
{
	pthread_create(&context->thread, NULL, x11_blocker_listen, context);
}

static void x11_blocker_source_load(struct x11_blocker_source *context)
{
	char *file = context->file;

	obs_enter_graphics();
	gs_image_file_free(&context->image);
	obs_leave_graphics();

	if (file && *file) {
		context->file_timestamp = get_modified_timestamp(file);
		gs_image_file_init(&context->image, file);
		context->update_time_elapsed = 0;

		obs_enter_graphics();
		gs_image_file_init_texture(&context->image);
		obs_leave_graphics();

		if (!context->image.loaded)
			printf("failed to load texture '%s'\n", file);
	}

	x11_blocker_start_listener(context);
}



static void x11_blocker_source_unload(struct x11_blocker_source *context)
{
	obs_enter_graphics();
	gs_image_file_free(&context->image);
	obs_leave_graphics();


}

// when settings are updated
static void x11_blocker_source_update(void *data, obs_data_t *settings)
{
	size_t count;
	struct x11_blocker_source *context = data;
	obs_data_array_t *array;

	array = obs_data_get_array(settings, S_WINDOWS);
	count = obs_data_array_count(array);

	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(array, i);
		const char *window = obs_data_get_string(item, "value");
		add_blocked_window(&context->blocked_windows.da, window);
	}

	/* Load the image if the source is persistent or showing */
	if (obs_source_showing(context->source))
		x11_blocker_source_load(data);
	else
		x11_blocker_source_unload(data);
}


static void *x11_blocker_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct x11_blocker_source *context = bzalloc(sizeof(struct x11_blocker_source));
	context->source = source;

	x11_blocker_source_update(context, settings);
	return context;
}


static void x11_blocker_source_destroy(void *data)
{
	struct x11_blocker_source *context = data;

	x11_blocker_source_unload(context);

	if (context->file)
		bfree(context->file);
	bfree(context);
}

static void x11_blocker_source_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

// Called when a source is visible on any display and/or on the main view.
static void x11_blocker_source_show(void *data)
{
	UNUSED_PARAMETER(data);
}

// Called when a source is no longer visible on any display and/or on the main view.
static void x11_blocker_source_hide(void *data)
{
	UNUSED_PARAMETER(data);
}


static uint32_t x11_blocker_source_getwidth(void *data)
{
	UNUSED_PARAMETER(data);
	//struct x11_blocker_source *context = data;

	// TODO: implement getwidth
	return 100;
}


static uint32_t x11_blocker_source_getheight(void *data)
{
	UNUSED_PARAMETER(data);
	//struct x11_blocker_source *context = data;
	// TODO: implement getheight
	return 100;
}


static void x11_blocker_source_render(void *data, gs_effect_t *effect)
{
	struct x11_blocker_source *context = data;

	if (!context->image.texture)
		return;

	/* gs_effect_loop(effect, "Draw"); */

	for (int i = 0; i < context->window_count; i++) {
		/* obs_source_draw(context->image, x, y, cx, cy, flip); */
	}

	/* gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), */
	/* 		      context->image.texture); */

	/* gs_draw_sprite(context->image.texture, 0, */
	/* 	       context->image.cx, context->image.cy); */
}


static void x11_blocker_source_tick(void *data, float seconds)
{
	struct x11_blocker_source *context = data;
}

static void x11_blocker_source_activate(void *data)
{
	struct x11_blocker_source *context = data;
	context->activated = true;
}


static void x11_blocker_source_deactivate(void *data)
{
	struct x11_blocker_source *context = data;
	context->activated = false;
}

static const char *image_filter =
	"All formats (*.bmp *.tga *.png *.jpeg *.jpg *.gif *.psd);;"
	"BMP Files (*.bmp);;"
	"Targa Files (*.tga);;"
	"PNG Files (*.png);;"
	"JPEG Files (*.jpeg *.jpg);;"
	"GIF Files (*.gif);;"
	"PSD Files (*.psd);;"
	"All Files (*.*)";

static obs_properties_t *x11_blocker_source_properties(void *data)
{
	struct x11_blocker_source *s = data;
	struct dstr path = {0};

	obs_properties_t *props = obs_properties_create();

	if (s && s->file && *s->file) {
		const char *slash;

		dstr_copy(&path, s->file);
		dstr_replace(&path, "\\", "/");
		slash = strrchr(path.array, '/');
		if (slash)
			dstr_resize(&path, slash - path.array + 1);
	}

	obs_properties_add_path(props,
				"file", obs_module_text("File"),
				OBS_PATH_FILE, image_filter, path.array);
	obs_properties_add_editable_list(props, S_WINDOWS, "WindowsToBlock",
					 OBS_EDITABLE_LIST_TYPE_STRINGS,
					 NULL, NULL);
	dstr_free(&path);

	return props;
}

static struct obs_source_info x11_blocker_source_info = {
	.id             = "x11_blocker_source",
	.type           = OBS_SOURCE_TYPE_INPUT,
	.output_flags   = OBS_SOURCE_VIDEO,
	.activate       = x11_blocker_source_activate,
	.create         = x11_blocker_source_create,
	.deactivate     = x11_blocker_source_deactivate,
	.destroy        = x11_blocker_source_destroy,
	.get_defaults   = x11_blocker_source_defaults,
	.get_height     = x11_blocker_source_getheight,
	.get_name       = x11_blocker_source_get_name,
	.get_width      = x11_blocker_source_getwidth,
	.hide           = x11_blocker_source_hide,
	.show           = x11_blocker_source_show,
	.update         = x11_blocker_source_update,
	.video_render   = x11_blocker_source_render,
	.video_tick     = x11_blocker_source_tick,
	.get_properties = x11_blocker_source_properties
};

OBS_DECLARE_MODULE()
	OBS_MODULE_USE_DEFAULT_LOCALE("x11-blocker-source", "en-US")
	MODULE_EXPORT const char *obs_module_description(void)
{
	return "X11 blocker source";
}

extern struct obs_source_info x11_blocker_source_info;

bool obs_module_load(void)
{
	obs_register_source(&x11_blocker_source_info);
	return true;
}

int main ()
{
	x11_blocker_listen(NULL);
}


