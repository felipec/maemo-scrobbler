#include "widget.h"

#include <gdk/gdk.h>
#include <math.h>

static GType type_id;
static int loved;
static DBusGProxy *sr_service;

gboolean
love_cb(GtkWidget *widget,
	GdkEventButton *event,
	gpointer user_data)
{
	struct sr_widget *self = user_data;
	loved = ~loved;
	gtk_widget_queue_draw(GTK_WIDGET(self));
	dbus_g_proxy_call(sr_service, "Love", NULL,
			  G_TYPE_BOOLEAN, loved, G_TYPE_INVALID,
			  G_TYPE_INVALID);
	return TRUE;
}

static void
next_cb(DBusGProxy *proxy, gpointer user_data)
{
	loved = 0;
	gtk_widget_queue_draw(GTK_WIDGET(user_data));
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

	dbus_g_proxy_connect_signal(sr_service, "Next", G_CALLBACK(next_cb), instance, NULL);
}

static void *parent_class;

static void
realize(GtkWidget *widget)
{
	GdkScreen *screen = gtk_widget_get_screen(widget);
	gtk_widget_set_colormap(widget, gdk_screen_get_rgba_colormap(screen));
	gtk_widget_set_app_paintable(widget, TRUE);
	GTK_WIDGET_CLASS(parent_class)->realize(widget);
}

static gboolean
expose_event(GtkWidget *widget,
	     GdkEventExpose *event)
{
	cairo_t *cr;
	GdkColor color;

	cr = gdk_cairo_create(GDK_DRAWABLE(widget->window));
	gdk_cairo_region(cr, event->region);
	cairo_clip(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
	cairo_paint(cr);

	if (!loved) {
		gtk_style_lookup_color(gtk_rc_get_style(widget), "DefaultBackgroundColor", &color);
		cairo_set_source_rgba(cr, color.red / 65535.0, color.green / 65335.0, color.blue / 65535.0, 0.75);
	}
	else {
		gtk_style_lookup_color(gtk_rc_get_style(widget), "SelectionColor", &color);
		cairo_set_source_rgba(cr, color.red / 65535.0, color.green/ 65335.0, color.blue / 65535.0, 0.75);
	}

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	double x = 0.0,
	       y = 0.0,
	       width = 96,
	       height = 96;
	double radius = 4.0;
	double degrees = M_PI / 180.0;

	cairo_new_sub_path(cr);
	cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
	cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
	cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
	cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
	cairo_close_path(cr);

	cairo_fill_preserve(cr);

	cairo_destroy(cr);

	return GTK_WIDGET_CLASS(parent_class)->expose_event(widget, event);
}

static void
class_init(void *g_class,
	   void *class_data)
{
	GtkWidgetClass *widget_class;
	DBusGConnection *bus;

	parent_class = g_type_class_peek_parent(g_class);
	widget_class = GTK_WIDGET_CLASS(g_class);

	widget_class->realize = realize;
	widget_class->expose_event = expose_event;

	bus = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
	sr_service = dbus_g_proxy_new_for_name(bus,
					       "org.scrobbler.service",
					       "/org/scrobbler/service",
					       "org.scrobbler.service");
	dbus_g_proxy_add_signal(sr_service, "Next", G_TYPE_INVALID, G_TYPE_INVALID);
	dbus_g_connection_unref(bus);
}

static void
class_finalize(void *g_class,
	       void *class_data)
{
	g_object_unref(sr_service);
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
		.class_finalize = class_finalize,
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
