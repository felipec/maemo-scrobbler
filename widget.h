#ifndef SR_WIDGET_H
#define SR_WIDGET_H

#include <glib-object.h>
#include <libhildondesktop/libhildondesktop.h>

struct sr_widget {
	HDHomePluginItem parent;

	struct sr_widget_priv *priv;
};

struct sr_widget_class {
	HDHomePluginItemClass parent_class;
};

#define SR_WIDGET_TYPE (sr_widget_get_type())
#define SR_WIDGET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SR_WIDGET_TYPE, struct sr_widget))
#define SR_WIDGET_CLASS(c) (G_TYPE_CHECK_CLASS_CAST((c), SR_WIDGET_TYPE, struct sr_widget_class))
#define SR_WIDGET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SR_WIDGET_TYPE, struct sr_widget_class))

GType sr_widget_get_type(void);

#endif /* SR_WIDGET_H */
