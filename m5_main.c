/*
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the LGPLv2.1.
 */

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
#include "helper.h"

static GMainLoop *main_loop;

static struct sr_service *dbus_service;

static void
metadata_callback(MafwRenderer *self,
		const gchar *object_id,
		GHashTable *metadata,
		void *user_data,
		const GError *error)
{
	hp_submit();
	sr_service_next(dbus_service);
}

static void
metadata_changed_cb(MafwRenderer *renderer,
		const gchar *name,
		GValueArray *value_array,
		void *data)
{
	GValue *value = g_value_array_get_nth(value_array, 0);
	if (strcmp(name, "artist") == 0)
		hp_set_artist(g_value_get_string(value));
	else if (strcmp(name, "title") == 0)
		hp_set_title(g_value_get_string(value));
	else if (strcmp(name, "duration") == 0)
		hp_set_length(g_value_get_int64(value));
	else if (strcmp(name, "album") == 0)
		hp_set_album(g_value_get_string(value));
	else if (strcmp(name, "video-codec") == 0)
		/* skip */
		hp_set_title(NULL);
}

static void
state_changed_cb(MafwRenderer *renderer,
		MafwPlayState state,
		void *user_data)
{
	switch (state) {
	case Playing:
		hp_set_timestamp();
		mafw_renderer_get_current_metadata(renderer,
				metadata_callback,
				user_data);
		break;
	case Stopped:
		hp_stop();
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

static void
signal_handler(int signal)
{
	g_main_loop_quit(main_loop);
}

int main(void)
{
	GError *error = NULL;
	MafwRegistry *registry;

	hp_init();

	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	if (!registry)
		g_error("Failed to get register");

	mafw_shared_init(registry, &error);
	if (error)
		g_error("Failed to initialize the shared library");

	g_signal_connect(registry,
			"renderer-added",
			G_CALLBACK(renderer_added_cb), NULL);

	dbus_service = g_object_new(SR_SERVICE_TYPE, NULL);

	signal(SIGINT, signal_handler);

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	g_object_unref(dbus_service);

	g_main_loop_unref(main_loop);

	hp_deinit();

	return 0;
}
