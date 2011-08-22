/*
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the LGPLv2.1.
 */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <conic.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <stdbool.h>

#include "helper.h"
#include "scrobble.h"

static sr_track_t *track;

static GKeyFile *keyfile;
static char *conf_file;
static char *cache_dir;
static int connected;

static DBusConnection *dbus_system;
static ConIcConnection *connection;
static int next_timer;

struct service {
	const char *id;
	const char *url;
	sr_session_t *session;
	char *cache;

	/* web-service */
	const char *api_url;
	const char *api_key;
	const char *api_secret;
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
	gboolean ok = true;

	username = g_key_file_get_string(keyfile, s->id, "username", NULL);
	password = g_key_file_get_string(keyfile, s->id, "password", NULL);
	session_key = g_key_file_get_string(keyfile, s->id, "session-key", NULL);

	if (!username || !username[0])
		ok = false;
	if (!password || !password[0])
		ok = false;
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

void hp_init(void)
{
	g_type_init();
	if (!g_thread_supported())
		g_thread_init(NULL);

#ifdef MAEMO5
	conf_file = g_build_filename(g_get_home_dir(), ".osso", "scrobbler", NULL);
#else
	conf_file = g_build_filename(g_get_user_config_dir(), "scrobbler", NULL);
#endif
	cache_dir = g_build_filename(g_get_user_cache_dir(), "scrobbler", NULL);

	g_mkdir_with_parents(cache_dir, 0755);

	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++)
		get_session(&services[i]);

	authenticate();
	monitor_conf();

	dbus_system = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	dbus_connection_setup_with_g_main(dbus_system, NULL);
	connection = con_ic_connection_new();
	g_signal_connect(connection, "connection-event", G_CALLBACK(connection_event), NULL);
	g_object_set(connection, "automatic-connection-events", TRUE, NULL);

	track = sr_track_new();
	track->source = 'P';

	g_timeout_add_seconds(10 * 60, timeout, NULL);
}

void hp_deinit(void)
{
	g_object_unref(connection);
	dbus_connection_unref(dbus_system);

	g_key_file_free(keyfile);

	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_pause(s->session);
		sr_session_store_list(s->session, s->cache);
	}

	sr_track_free(track);

	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		g_free(s->cache);
		sr_session_free(s->session);
	}

	g_free(cache_dir);
	g_free(conf_file);

	if (next_timer) {
		g_source_remove(next_timer);
		next_timer = 0;
	}
}

void hp_submit(void)
{
	unsigned i;
	if (!track->artist || !track->title)
		goto clear;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_add_track(s->session, sr_track_dup(track));
		sr_session_submit(s->session);
	}
clear:
	g_free(track->artist);
	track->artist = NULL;
	g_free(track->title);
	track->title = NULL;
	track->length = 0;
	g_free(track->album);
	track->album = NULL;
}

void hp_love_current(bool on)
{
	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_set_love(s->session, on);
	}
}

void hp_love(const char *artist, const char *title, bool on)
{
	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_love(s->session, artist, title, on);
	}
}

void hp_stop(void)
{
	for (unsigned i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		if (!s->on)
			continue;
		sr_session_pause(s->session);
		sr_session_store_list(s->session, s->cache);
	}
}

void hp_set_artist(const char *value)
{
	g_free(track->artist);
	track->artist = g_strdup(value);
}

void hp_set_title(const char *value)
{
	g_free(track->title);
	track->title = g_strdup(value);
}

void hp_set_length(int value)
{
	track->length = value;
}

void hp_set_album(const char *value)
{
	g_free(track->album);
	track->album = g_strdup(value);
}

void hp_set_timestamp(void)
{
	track->timestamp = time(NULL);
}

static gboolean do_next(void *data)
{
	next_timer = 0;
	hp_submit();
	return FALSE;
}

void hp_next(void)
{
	hp_set_timestamp();

	if (next_timer)
		g_source_remove(next_timer);

	next_timer = g_timeout_add_seconds(10, do_next, NULL);
}
