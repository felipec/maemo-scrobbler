#include "service.h"
#include <dbus/dbus-glib-bindings.h>

static void *parent_class;

extern void scrobbler_love(gboolean on);

void
sr_service_next(struct sr_service *service)
{
	struct sr_service_class *class;
	class = g_type_class_peek(SR_SERVICE_TYPE);
	g_signal_emit(G_OBJECT(service), class->next_sig, 0, service);
}

static gboolean
sr_service_love(struct sr_service *service, gboolean on)
{
	scrobbler_love(on);
	return TRUE;
}

#include "service_glue.h"

static void
instance_init(GTypeInstance *instance,
		void *g_class)
{
	DBusGProxy *driver_proxy;
	struct sr_service_class *class;

	class = SR_SERVICE_GET_CLASS(instance);
	dbus_g_connection_register_g_object(class->connection,
			"/org/scrobbler/service", G_OBJECT(instance));

	driver_proxy = dbus_g_proxy_new_for_name(class->connection,
			"org.freedesktop.DBus",
			"/org/freedesktop/DBus",
			"org.freedesktop.DBus");

	org_freedesktop_DBus_request_name(driver_proxy,
			"org.scrobbler.service",
			0, NULL,
			NULL);
	g_object_unref(driver_proxy);
}

static void
class_init(void *g_class,
		void *class_data)
{
	struct sr_service_class *service_class = g_class;
	parent_class = g_type_class_peek_parent(g_class);

	service_class->connection = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
	dbus_g_object_type_install_info(SR_SERVICE_TYPE, &dbus_glib_sr_service_object_info);

	service_class->next_sig = g_signal_new("next", G_OBJECT_CLASS_TYPE(g_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			0, NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
}

GType
sr_service_get_type(void)
{
	static gsize init_type;

	if (g_once_init_enter(&init_type)) {
		GType type;
		GTypeInfo type_info = {
			.class_size = sizeof(struct sr_service_class),
			.class_init = class_init,
			.instance_size = sizeof(struct sr_service),
			.instance_init = instance_init,
		};

		type = g_type_register_static(G_TYPE_OBJECT, "SrService", &type_info, 0);

		g_once_init_leave(&init_type, type);
	}

	return init_type;
}
