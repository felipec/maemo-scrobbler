#include <glib.h>
#include <libmafw/mafw.h>
#include <libmafw-shared/mafw-shared.h>
#include <gio/gio.h>
#include <conic.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <string.h>
#include <signal.h>
#include <stdbool.h>

#include "scrobble.h"
#include "service.h"

static GMainLoop *main_loop;
static GKeyFile *keyfile;
static sr_track_t *track;
static char *conf_file;
static char *cache_dir;
static int skip_track;
static int connected;

static struct sr_service *dbus_service;

struct service {
	const char *id;
	const char *url;
	sr_session_t *session;
	char *cache;

	/* web-service */
	char *api_url;
	char *api_key;
	char *api_secret;
	bool on;
};

static struct service services[] = {
	{ .id = "lastfm", .url = SR_LASTFM_URL,
		.api_url = SR_LASTFM_API_URL,
		.api_key = "a550e8cdf80179f749786109ae94a644",
		.api_secret = "92cb9a26e36b18031e5dad8db4edfddb", },
	{ .id = "librefm", .url = SR_LIBREFM_URL,
		.api_url = SR_LIBREFM_API_URL,
		.api_key = "a550e8cdf80179f749786109ae94a644",
		.api_secret = "92cb9a26e36b18031e5dad8db4edfddb", },
};

void scrobbler_love(gboolean on)
{
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_set_love(s->session, on);
	}
}

static void
metadata_callback(MafwRenderer *self,
		const gchar *object_id,
		GHashTable *metadata,
		void *user_data,
		const GError *error)
{
	unsigned i;
	if (skip_track) {
		skip_track = 0;
		goto clear;
	}
	if (!track->artist || !track->title)
		goto clear;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_add_track(s->session, sr_track_dup(track));
		sr_session_submit(s->session);
	}
	sr_service_next(dbus_service);
clear:
	g_free(track->artist);
	track->artist = NULL;
	g_free(track->title);
	track->title = NULL;
	track->length = 0;
	g_free(track->album);
	track->album = NULL;
}

static void
metadata_changed_cb(MafwRenderer *renderer,
		const gchar *name,
		GValueArray *value_array,
		void *data)
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
stop(void)
{
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_pause(s->session);
		sr_session_store_list(s->session, s->cache);
	}
}

static void
state_changed_cb(MafwRenderer *renderer,
		MafwPlayState state,
		void *user_data)
{
	switch (state) {
	case Playing:
		track->timestamp = time(NULL);
		mafw_renderer_get_current_metadata(renderer,
				metadata_callback,
				user_data);
		break;
	case Stopped:
		stop();
		break;
	default:
		break;
	}
}

static void
renderer_added_cb(MafwRegistry *registry,
		GObject *renderer,
		void *user_data)
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

static void scrobble_cb(sr_session_t *s)
{
	struct service *service = s->user_data;
	sr_session_store_list(s, service->cache);
}

static void session_key_cb(sr_session_t *s, const char *session_key)
{
	struct service *service = s->user_data;
	g_key_file_set_string(keyfile, service->id, "session-key", session_key);
	g_file_set_contents(conf_file,
			g_key_file_to_data(keyfile, NULL, NULL),
			-1, NULL);
}

static gboolean
authenticate_session(struct service *s)
{
	gchar *username, *password;
	gchar *session_key;
	gboolean ok;

	username = g_key_file_get_string(keyfile, s->id, "username", NULL);
	password = g_key_file_get_string(keyfile, s->id, "password", NULL);
	session_key = g_key_file_get_string(keyfile, s->id, "session-key", NULL);

	ok = username && password;
	if (!ok)
		goto leave;

	sr_session_set_cred(s->session, username, password);
	if (session_key)
		sr_session_set_session_key(s->session, session_key);
	if (connected)
		sr_session_handshake(s->session);

	s->on = true;

leave:
	g_free(username);
	g_free(password);

	return ok;
}

static void
get_session(struct service *service)
{
	sr_session_t *s;
	s = sr_session_new(service->url, "mms", "1.0");
	s->user_data = service;
	s->error_cb = error_cb;
	s->scrobble_cb = scrobble_cb;
	s->session_key_cb = session_key_cb;
	service->cache = g_build_filename(cache_dir, service->id, NULL);
	sr_session_load_list(s, service->cache);
	if (service->api_key)
		sr_session_set_api(s, service->api_url,
				service->api_key, service->api_secret);
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
		return;

	for (i = 0; i < G_N_ELEMENTS(services); i++)
		authenticate_session(&services[i]);
}

static void
conf_changed(GFileMonitor *monitor,
		GFile *file,
		GFile *other_file,
		GFileMonitorEvent event_type,
		void *user_data)
{
	if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
			event_type == G_FILE_MONITOR_EVENT_CREATED)
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
	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_store_list(s->session, s->cache);
	}
	return TRUE;
}

static void
signal_handler(int signal)
{
	g_main_loop_quit(main_loop);
}

static void
check_proxy(ConIcConnection *connection)
{
	char *url;
	unsigned i;

	if (con_ic_connection_get_proxy_mode(connection) == CON_IC_PROXY_MODE_MANUAL) {
		const char *host;
		int port;
		host = con_ic_connection_get_proxy_host(connection, CON_IC_PROXY_PROTOCOL_HTTP);
		port = con_ic_connection_get_proxy_port(connection, CON_IC_PROXY_PROTOCOL_HTTP);
		url = g_strdup_printf("http://%s:%i/", host, port);
	}
	else
		url = NULL;
	for (i = 0; i < G_N_ELEMENTS(services); i++)
		sr_session_set_proxy(services[i].session, url);
	g_free(url);
}

static void
connection_event(ConIcConnection *connection,
		ConIcConnectionEvent *event,
		void *user_data)
{
	ConIcConnectionStatus status;
	status = con_ic_connection_event_get_status(event);
	if (status == CON_IC_STATUS_CONNECTED) {
		unsigned i;
		connected = 1;
		check_proxy(connection);
		for (i = 0; i < G_N_ELEMENTS(services); i++)
			sr_session_handshake(services[i].session);
	}
	else if (status == CON_IC_STATUS_DISCONNECTING)
		connected = 0;
}

int main(void)
{
	GError *error = NULL;
	MafwRegistry *registry;
	unsigned i;
	DBusConnection *dbus_system;
	ConIcConnection *connection;

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

	dbus_system = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	dbus_connection_setup_with_g_main(dbus_system, NULL);
	connection = con_ic_connection_new();
	g_signal_connect(connection, "connection-event", G_CALLBACK(connection_event), NULL);
	g_object_set(connection, "automatic-connection-events", TRUE, NULL);

	dbus_service = g_object_new(SR_SERVICE_TYPE, NULL);

	track = sr_track_new();
	track->source = 'P';

	g_timeout_add_seconds(10 * 60, timeout, NULL);

	signal(SIGINT, signal_handler);

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	g_object_unref(dbus_service);
	g_object_unref(connection);
	dbus_connection_unref(dbus_system);

	g_main_loop_unref(main_loop);

	g_key_file_free(keyfile);

	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_pause(s->session);
		sr_session_store_list(s->session, s->cache);
	}

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
