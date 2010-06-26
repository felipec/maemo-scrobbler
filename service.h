#ifndef SR_SERVICE_H
#define SR_SERVICE_H

#include <glib-object.h>

struct sr_service {
	GObject parent;

	struct sr_service_priv *priv;
};

struct sr_service_class {
	GObjectClass parent_class;
	void *connection;
	guint next_sig;
};

#define SR_SERVICE_TYPE (sr_service_get_type())
#define SR_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SR_SERVICE_TYPE, struct sr_service))
#define SR_SERVICE_CLASS(c) (G_TYPE_CHECK_CLASS_CAST((c), SR_SERVICE_TYPE, struct sr_service_class))
#define SR_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SR_SERVICE_TYPE, struct sr_service_class))

void sr_service_next(struct sr_service *service);

GType sr_service_get_type(void);

#endif /* SR_SERVICE_H */
