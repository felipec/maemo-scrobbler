#include <QtCore/QCoreApplication>
#include <QVariant>

#include "m6_main.h"
#include "helper.h"

#include <signal.h>

#include <MafwShared.h>
#include <MafwRegistry.h>

Listener::Listener(QObject *parent)
{
	setParent(parent);

	registry = MafwRegistry::instance();
	shared = MafwShared::instance();
	shared->initTracking(registry);

	renderer = registry->renderer("mafw_gst_renderer");

	connect(renderer, SIGNAL(mediaChanged(int, const MafwContent&)),
			this, SLOT(next(void)));

	connect(renderer, SIGNAL(stateChanged(MafwRenderer::State)),
			this, SLOT(state_changed(MafwRenderer::State)));

	connect(renderer, SIGNAL(metadataChanged(const QString&, const QList<QVariant>&)),
			this, SLOT(metadata_changed(const QString&, const QList<QVariant>&)));
}

void Listener::next(void)
{
	hp_next();
}

void Listener::state_changed(MafwRenderer::State state)
{
	switch (state) {
	case MafwRenderer::Playing:
		hp_next();
		break;
	case MafwRenderer::Stopped:
	case MafwRenderer::Paused:
		hp_stop();
		break;
	default:
		break;
	}
}

void Listener::metadata_changed(const QString& name, const QList<QVariant>& values)
{
	QVariant value = values[0];
	if (name == "artist")
		hp_set_artist(value.toString().toUtf8());
	else if (name == "title")
		hp_set_title(value.toString().toUtf8());
	else if (name == "duration")
		hp_set_length(value.toLongLong());
	else if (name == "album")
		hp_set_album(value.toString().toUtf8());
}

static void
signal_handler(int signal)
{
	QCoreApplication::exit(0);
}

int main(int argc, char *argv[])
{
	int r;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	hp_init();

	QCoreApplication a(argc, argv);
	Listener listener(&a);

	r = a.exec();

	hp_deinit();

	return r;
};
