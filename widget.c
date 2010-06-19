#include "widget.h"

#include <gdk/gdk.h>
#include <math.h>

static GType type_id;
static int loved;

gboolean
love_cb(GtkWidget *widget,
	GdkEventButton *event,
	gpointer user_data)
{
	struct sr_widget *self = user_data;
	loved = ~loved;
	gtk_widget_queue_draw(GTK_WIDGET(self));
	return TRUE;
}

static GtkWidget *
build_ui(struct sr_widget *widget)
{
	GtkWidget *fixed;
	GtkWidget *love;
	GtkWidget *event_box;

	fixed = gtk_fixed_new();
	love = gtk_image_new_from_file("/usr/share/scrobbler/love.png");
	event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
	gtk_container_add(GTK_CONTAINER(event_box), love);
	gtk_fixed_put(GTK_FIXED(fixed), event_box, 16, 16);
	g_signal_connect(event_box, "button-release-event", G_CALLBACK(love_cb), widget);
	gtk_widget_show_all(fixed);

	return fixed;
}

static void
instance_init(GTypeInstance *instance,
	      void *g_class)
{
	GtkWidget *contents = build_ui(SR_WIDGET(instance));
	gtk_window_set_default_size(GTK_WINDOW(instance), 96, 96);
	gtk_container_add(GTK_CONTAINER(instance), contents);
}

static void *parent_class;

static void
class_init(void *g_class,
	   void *class_data)
{
	parent_class = g_type_class_peek_parent(g_class);
}

GType
sr_widget_get_type(void)
{
	return type_id;
}

static void
register_type(GTypeModule *type_module)
{
	GTypeInfo type_info = {
		.class_size = sizeof(struct sr_widget_class),
		.class_init = class_init,
		.instance_size = sizeof(struct sr_widget),
		.instance_init = instance_init,
	};

	type_id = g_type_module_register_type(type_module, HD_TYPE_HOME_PLUGIN_ITEM,
					      "SrWidget", &type_info, 0);
}

G_MODULE_EXPORT void hd_plugin_module_load(HDPluginModule *plugin)
{
	register_type(G_TYPE_MODULE(plugin));
	hd_plugin_module_add_type(plugin, SR_WIDGET_TYPE);
}

G_MODULE_EXPORT void hd_plugin_module_unload(HDPluginModule *plugin)
{
}
