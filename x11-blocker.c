
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
#include <assert.h>
#include <sys/stat.h>

#define streq(a,b) (strcmp((a),(b)) == 0)
#define MAX_WINDOWS 128
#define S_WINDOWS   "windows"
#define S_FILE      "file"

static int unmap_frames = 4;

struct window {
	int is_mapped;
	int do_unmap;
	unsigned int width, height;
	int x, y;
	unsigned int border_width;
	Window handle;
	char name[32];
};

struct x11_blocker_source {
	obs_source_t *source;
	gs_image_file_t image;
	pthread_t thread;
	pthread_mutex_t mutex;
	bool activated, listening;

	float        update_time_elapsed;
	time_t       file_timestamp;
	char         *file; // image to draw over window

	DARRAY(char*) blocked_windows;

	int window_count;
	struct window windows[MAX_WINDOWS];
};

static void print_window(struct window *window) {
	printf("%s x:%d y:%d w:%u h:%u bw:%u mapped:%d\n", window->name,
	       window->x, window->y,
	       window->width, window->height, window->border_width,
	       window->is_mapped);
}


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

static int find_window(struct x11_blocker_source *ctx, Window window) {
	for (int i = 0; i < ctx->window_count; i++) {
		if (ctx->windows[i].handle == window)
			return i;
	}

	return -1;
}

static int is_blocked_window(struct x11_blocker_source *ctx, const char *name)
{
	for (size_t i = 0; i < ctx->blocked_windows.num; i++) {
		if (streq(name, ctx->blocked_windows.array[i]))
			return 1;
	}

	return 0;
}

static void update_window(struct x11_blocker_source *ctx, Display *d,
			  struct window *window, int mapped)
{
	Window wroot;
	unsigned int bwidth,depth;

	window->is_mapped = mapped;

	XGetGeometry(d, window->handle, &wroot,
		     &window->x, &window->y,
		     &window->width, &window->height,
		     &bwidth, &depth);

}

static void map_window(struct x11_blocker_source *ctx,
		       Display *d, Window window)
{
	XClassHint chint;
	struct window *w;

	if (!XGetClassHint(d, window, &chint))
		return;

	if (!is_blocked_window(ctx, chint.res_name))
		return;

	// do we already have the window?
	int ind = find_window(ctx, window);
	int is_new_window = ind < 0;

	if (is_new_window && ctx->window_count == MAX_WINDOWS) {
		printf("WARNING: max windows reached in x11-blocker\n");
		return;
	}

	w = is_new_window ? &ctx->windows[ctx->window_count++]
		          : &ctx->windows[ind];

	if (is_new_window) {
		w->do_unmap = 0;
		strncpy(w->name, chint.res_name, sizeof(w->name));
		printf("mapping "); print_window(w);
	}

	w->handle = window;

	static const int is_mapped = 1;
	update_window(ctx, d, w, is_mapped);
	/* printf("mapping "); print_window(w); */
}

static void unmap_window(struct x11_blocker_source *ctx, Window window)
{
	struct window *w;
	int ind = find_window(ctx, window);

	if (ind == -1)
		return;

	w = &ctx->windows[ind];

	printf("unmapping "); print_window(w);
	w->do_unmap = unmap_frames;
}


static void remove_window(struct x11_blocker_source *ctx, Window window)
{
	struct window *w;
	int ind = find_window(ctx, window);

	if (ind == -1)
		return;

	w = &ctx->windows[ind];

	// remove window
	printf("removing "); print_window(w);
	memmove(w, &ctx->windows[ind+1], sizeof(*w) * (ctx->window_count - ind - 1));
}

static void *x11_blocker_listen(void *data)
{
	struct x11_blocker_source *ctx = data;

	// TODO: close display on unload
	Status res;
	Display* d = XOpenDisplay(NULL);
	Window root = DefaultRootWindow(d);
	XClassHint chint;
	Window curFocus, wroot;
	int revert;

	XGetInputFocus (d, &curFocus, &revert);
	XSelectInput(d, root, SubstructureNotifyMask | VisibilityChangeMask);

	while (1)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		switch (ev.type)
		{
		case DestroyNotify:
			remove_window(ctx, ev.xdestroywindow.window);
			break;

		case UnmapNotify:
			unmap_window(ctx, ev.xunmap.window);
			break;

		case MapNotify:
			map_window(ctx, d, ev.xmap.window);
			break;

		case ConfigureNotify:
			map_window(ctx, d, ev.xconfigure.window);
			break;

		case VisibilityNotify:
			map_window(ctx, d, ev.xvisibility.window);
			/* XGetClassHint(d, ev.xvisibility.window, &chint); */
			/* printf("%s visibility state: ", chint.res_name); */
			/* switch (ev.xvisibility.state) { */
			/* case VisibilityFullyObscured: */
			/* 	printf("fully obscured\n"); */
			/* 	break; */
			/* case VisibilityPartiallyObscured: */
			/* 	printf("partially obscured\n"); */
			/* 	break; */
			/* case VisibilityUnobscured: */
			/* 	printf("unobscured\n"); */
			/* 	break; */
			/* } */
			break;
		
		/* default: */
		/* 	printf("%d\n", ev.type); */
		}

	}

	return NULL;
}

static void x11_blocker_start_listener(struct x11_blocker_source *context)
{
	printf("x11-blocker-start-listener\n");
	context->listening = true;
	pthread_create(&context->thread, NULL, x11_blocker_listen, context);
}

static inline void fill_texture(uint32_t *pixels, uint32_t pixel)
{
	size_t x, y;

	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			pixels[y*32 + x] = pixel;
		}
	}
}

static void x11_blocker_source_load(struct x11_blocker_source *context)
{
	char *file = context->file;
	printf("x11-blocker-load\n");

	if (file && *file) {
		context->file_timestamp = get_modified_timestamp(file);
		gs_image_file_init(&context->image, file);
		context->update_time_elapsed = 0;

		obs_enter_graphics();
		printf("loading x11-blocker-texture\n");
		gs_image_file_init_texture(&context->image);
		obs_leave_graphics();

		if (!context->image.loaded)
			printf("failed to load texture '%s'\n", file);
	}
	else {
		uint8_t *ptr;
		uint32_t linesize;
		obs_enter_graphics();
		gs_texture_create(32, 32, GS_RGBA, 1, NULL, GS_DYNAMIC);
		if (gs_texture_map(context->image.texture, &ptr, &linesize)) {
			fill_texture((uint32_t*)ptr, 0xFF000000);
			gs_texture_unmap(context->image.texture);
		}
		obs_leave_graphics();

		if (!context->image.loaded)
			printf("failed to load black texture\n");
	}

	if (!context->listening)
		x11_blocker_start_listener(context);
}



static void x11_blocker_source_unload(struct x11_blocker_source *context)
{
	printf("x11-blocker-unload\n");
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

	printf("x11-blocker-update\n");

	context->file = obs_data_get_string(settings, S_FILE);
	array = obs_data_get_array(settings, S_WINDOWS);
	count = obs_data_array_count(array);
	context->blocked_windows.num = 0;

	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(array, i);
		const char *window = obs_data_get_string(item, "value");
		add_blocked_window(&context->blocked_windows.da, window);
	}

	/* Load the image if the source is persistent or showing */
	x11_blocker_source_unload(data);
	x11_blocker_source_load(data);
}


static void *x11_blocker_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct x11_blocker_source *context = bzalloc(sizeof(struct x11_blocker_source));

	printf("x11-blocker-create\n");

	context->source = source;
	context->window_count = 0;
	context->thread = 0;
	context->listening = 0;

	x11_blocker_source_update(context, settings);
	return context;
}


static void x11_blocker_source_destroy(void *data)
{
	struct x11_blocker_source *context = data;

	x11_blocker_source_unload(context);

	if (context->thread)
		pthread_cancel(context->thread);
	context->listening = false;

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
	struct window *w;
	struct x11_blocker_source *context = data;

	if (!context->image.texture)
		return;

	/* gs_effect_loop(effect, "Draw"); */
	static const int buffer = 50;

	for (int i = 0; i < context->window_count; i++) {
		w = &context->windows[i];

		// take a frame to unmap
		if (w->do_unmap == 0) {
			w->is_mapped = 0;
			w->do_unmap = unmap_frames;
		}
		else if (w->is_mapped) {
			obs_source_draw(context->image.texture,
					w->x - buffer, w->y - buffer,
					w->width + buffer * 2, w->height + buffer * 2, 0);
		}
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

	obs_properties_add_path(props, S_FILE, obs_module_text("File"),
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
	struct x11_blocker_source ctx;
	da_init(ctx.blocked_windows);
	const char *signal = "signal";
	const char *skype  = "skype";
	const char *skype2  = "skypeforlinux";

	da_push_back(ctx.blocked_windows, &skype);
	da_push_back(ctx.blocked_windows, &skype2);
	ctx.window_count = 0;
	x11_blocker_listen(&ctx);
}


