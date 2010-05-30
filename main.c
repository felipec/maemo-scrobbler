#include <glib.h>
#include <libmafw/mafw.h>
#include <libmafw-shared/mafw-shared.h>
#include <gio/gio.h>

#include <string.h>

#include "scrobble.h"

static GMainLoop *main_loop;
static GKeyFile *keyfile;
static sr_track_t *track;
static char *conf_file;
static char *cache_dir;
static int skip_track;

struct service {
	const char *id;
	const char *url;
	sr_session_t *session;
	char *cache;
};

static struct service services[] = {
	{ .id = "lastfm", .url = SR_LASTFM_URL },
	{ .id = "librefm", .url = SR_LIBREFM_URL },
};

static void
metadata_callback(MafwRenderer *self,
		  const gchar *object_id,
		  GHashTable *metadata,
		  gpointer user_data,
		  const GError *error)
{
	unsigned i;
	if (skip_track) {
		skip_track = 0;
		return;
	}
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		sr_session_add_track(s->session, sr_track_dup(track));
		sr_session_submit(s->session);
	}
}

static void
metadata_changed_cb(MafwRenderer *renderer,
		    const gchar *name,
		    GValueArray *value_array,
		    gpointer data)
{
	GValue *value = g_value_array_get_nth(value_array, 0);
	if (strcmp(name, "artist") == 0) {
		g_free(track->artist);
		track->artist = g_value_dup_string(value);
	}
	else if (strcmp(name, "title") == 0) {
		g_free(track->title);
		track->title = g_value_dup_string(value);
	}
	else if (strcmp(name, "duration") == 0) {
		track->length = (int) g_value_get_int64(value);
	}
	else if (strcmp(name, "album") == 0) {
		g_free(track->album);
		track->album = g_value_dup_string(value);
	}
	else if (strcmp(name, "video-codec") == 0)
		skip_track = 1;
}

static void
store(void)
{
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		sr_session_store_list(s->session, s->cache);
	}
}

static void
state_changed_cb(MafwRenderer *renderer,
		 MafwPlayState state,
		 gpointer user_data)
{
	switch (state) {
	case Playing:
		track->timestamp = time(NULL);
		mafw_renderer_get_current_metadata(renderer,
						   metadata_callback,
						   user_data);
		break;
	case Stopped:
		{
			unsigned i;
			for (i = 0; i < G_N_ELEMENTS(services); i++)
				sr_session_pause(services[i].session);
		}
		store();
		break;
	default:
		break;
	}
}

static void
renderer_added_cb(MafwRegistry *registry,
		  GObject *renderer,
		  gpointer user_data)
{
	const gchar *name;

	if (!MAFW_IS_RENDERER(renderer))
		return;

	name = mafw_extension_get_name(MAFW_EXTENSION(renderer));

	if (strcmp(name, "Mafw-Gst-Renderer") != 0)
		return;

	g_signal_connect(renderer,
			 "state-changed",
			 G_CALLBACK(state_changed_cb),
			 user_data);
	g_signal_connect(renderer,
			 "metadata-changed",
			 G_CALLBACK(metadata_changed_cb),
			 user_data);
}

static void error_cb(sr_session_t *s,
		     int fatal,
		     const char *msg)
{
	g_warning(msg);
}

static gboolean
authenticate_session(struct service *s)
{
	gchar *username, *password;
	gboolean ok;

	username = g_key_file_get_string(keyfile, s->id, "username", NULL);
	password = g_key_file_get_string(keyfile, s->id, "password", NULL);

	ok = username && password;
	if (!ok)
		goto leave;

	sr_session_set_cred(s->session, username, password);
	sr_session_handshake(s->session);

leave:
	g_free(username);
	g_free(password);

	return ok;
}

static void
get_session(struct service *service)
{
	sr_session_t *s;
	s = sr_session_new(service->url, "tst", "1.0");
	s->error_cb = error_cb;
	service->cache = g_build_filename(cache_dir, service->id, NULL);
	sr_session_load_list(s, service->cache);
	service->session = s;
}

static void
authenticate(void)
{
	gboolean ok;
	unsigned i;

	keyfile = g_key_file_new();

	ok = g_key_file_load_from_file(keyfile, conf_file, G_KEY_FILE_NONE, NULL);
	if (!ok)
		goto leave;

	for (i = 0; i < G_N_ELEMENTS(services); i++)
		authenticate_session(&services[i]);

leave:
	g_key_file_free(keyfile);
}

static void
conf_changed(GFileMonitor *monitor,
	     GFile *file,
	     GFile *other_file,
	     GFileMonitorEvent event_type,
	     gpointer user_data)
{
	if (event_type == G_FILE_MONITOR_EVENT_CHANGED)
		authenticate();
}

static void
monitor_conf(void)
{
	GFile *file;
	GFileMonitor *monitor;

	file = g_file_new_for_path(conf_file);
	monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
	g_signal_connect(monitor, "changed", G_CALLBACK(conf_changed), NULL);
	g_object_unref(file);
}

static gboolean
timeout(void *data)
{
	store();
	return TRUE;
}

int main(void)
{
	GError *error = NULL;
	MafwRegistry *registry;
	unsigned i;

	g_type_init();
	if (!g_thread_supported())
		g_thread_init(NULL);

	conf_file = g_build_filename(g_get_home_dir(), ".osso", "scrobbler", NULL);
	cache_dir = g_build_filename(g_get_user_cache_dir(), "scrobbler", NULL);

	g_mkdir_with_parents(cache_dir, 0755);

	for (i = 0; i < G_N_ELEMENTS(services); i++)
		get_session(&services[i]);

	authenticate();
	monitor_conf();

	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	if (!registry)
		g_error("Failed to get register");

	mafw_shared_init(registry, &error);
	if (error)
		g_error("Failed to initialize the shared library");

	g_signal_connect(registry,
			 "renderer-added",
			 G_CALLBACK(renderer_added_cb), NULL);

	track = sr_track_new();
	track->source = 'P';

	g_timeout_add_seconds(10 * 60, timeout, NULL);

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	store();

	sr_track_free(track);

	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		g_free(s->cache);
		sr_session_free(s->session);
	}

	g_free(cache_dir);
	g_free(conf_file);
	return 0;
}
