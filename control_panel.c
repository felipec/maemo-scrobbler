/**
 * Copyright (C) 2009-2010 Claudio Saavedra
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the GPLv2.
 */

#include <hildon-cp-plugin/hildon-cp-plugin-interface.h>
#include <hildon/hildon.h>
#include <gtk/gtk.h>
#include <libintl.h>

static gchar *file;
static GKeyFile *keyfile;

struct service {
	const char *id;
	GtkEntry *username;
	GtkEntry *password;
};

static struct service services[] = {
	{ .id = "lastfm" },
	{ .id = "librefm" },
};

static void
save_credentials(void)
{

	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		g_key_file_set_string(keyfile, s->id, "username", gtk_entry_get_text(s->username));
		g_key_file_set_string(keyfile, s->id, "password", gtk_entry_get_text(s->password));
	}

	g_file_set_contents(file,
			g_key_file_to_data(keyfile, NULL, NULL),
			-1, NULL);
}

static void
on_dialog_response(GtkDialog *dialog,
		gint id,
		void *user_data)
{
	if (id == GTK_RESPONSE_OK)
		save_credentials();

	gtk_widget_hide(GTK_WIDGET(dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));

	g_key_file_free(keyfile);
	g_free(file);
}

static void
load_credentials(void)
{
	gboolean ok;

	file = g_build_filename(g_get_home_dir(), ".osso", "scrobbler", NULL);

	keyfile = g_key_file_new();
	ok = g_key_file_load_from_file(keyfile, file, G_KEY_FILE_NONE, NULL);
	if (!ok)
		return;

	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		struct service *s = &services[i];
		gchar *v_username, *v_password;

		v_username = g_key_file_get_string(keyfile, s->id, "username", NULL);
		v_password = g_key_file_get_string(keyfile, s->id, "password", NULL);
		gtk_entry_set_text(s->username, v_username);
		gtk_entry_set_text(s->password, v_password);
		gtk_editable_select_region(GTK_EDITABLE(s->username), 0, -1);
		g_free(v_username);
		g_free(v_password);
	}
}

static GtkWidget *
build_service(struct service *service)
{
	GtkWidget *hbox;
	GtkWidget *frame;
	GtkWidget *fvbox;

	frame = gtk_frame_new(service->id);
	fvbox = gtk_vbox_new(TRUE, 0);

	{
		GtkWidget *label_username, *entry_username;

		hbox = gtk_hbox_new(FALSE, 0);

		label_username = gtk_label_new("Username:");
		gtk_misc_set_alignment(GTK_MISC(label_username), 0.0, 0.5);
		entry_username = hildon_entry_new(HILDON_SIZE_AUTO | HILDON_SIZE_FINGER_HEIGHT);

		gtk_box_pack_start(GTK_BOX(hbox), label_username, TRUE, TRUE, 20);
		gtk_box_pack_start(GTK_BOX(hbox), entry_username, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(fvbox), hbox, FALSE, FALSE, 0);

		service->username = GTK_ENTRY(entry_username);
	}

	{
		GtkWidget *label_password, *entry_password;

		hbox = gtk_hbox_new(FALSE, 0);

		label_password = gtk_label_new("Password:");
		gtk_misc_set_alignment(GTK_MISC(label_password), 0.0, 0.5);
		entry_password = hildon_entry_new(HILDON_SIZE_AUTO | HILDON_SIZE_FINGER_HEIGHT);
		hildon_gtk_entry_set_input_mode(GTK_ENTRY(entry_password),
				HILDON_GTK_INPUT_MODE_FULL |
				HILDON_GTK_INPUT_MODE_INVISIBLE);

		gtk_box_pack_start(GTK_BOX(hbox), label_password, TRUE, TRUE, 20);
		gtk_box_pack_start(GTK_BOX(hbox), entry_password, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(fvbox), hbox, FALSE, FALSE, 0);

		service->password = GTK_ENTRY(entry_password);
	}

	gtk_container_add(GTK_CONTAINER(frame), fvbox);
	return frame;
}

osso_return_t
execute(osso_context_t *osso, void *data, gboolean user_activated)
{
	GtkWidget *dialog;
	GtkWidget *vbox;

	dialog = gtk_dialog_new_with_buttons("Scrobbler settings",
			GTK_WINDOW(data),
			GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
			dgettext("hildon-libs", "wdgt_bd_done"),
			GTK_RESPONSE_OK,
			NULL);
	vbox = gtk_vbox_new(TRUE, 0);

	unsigned i;
	for (i = 0; i < G_N_ELEMENTS(services); i++) {
		GtkWidget *frame;
		frame = build_service(&services[i]);
		gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);
	}

	load_credentials();

	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
			vbox);

	g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);

	gtk_widget_show_all(dialog);

	return OSSO_OK;
}

osso_return_t
save_state(osso_context_t *osso, void *data)
{
	return OSSO_OK;
}
