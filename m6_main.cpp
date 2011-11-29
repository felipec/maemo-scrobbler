#include <QtCore/QCoreApplication>
#include <QVariant>

#include "m6_main.h"
#include "helper.h"

#include <signal.h>

#include <MafwShared.h>
#include <MafwRegistry.h>
#include <MafwTrackerModelFactory.h>
#include <MafwTrackerModelConnection.h>

Listener::Listener(QObject *parent)
{
	setParent(parent);
}

bool Listener::init(void)
{
	registry = MafwRegistry::instance();
	shared = MafwShared::instance();
	shared->initTracking(registry);

	renderer = registry->renderer("mafw_gst_renderer");

	if (!connect(renderer, SIGNAL(mediaChanged(int, const MafwContent&)),
			this, SLOT(next(void))))
		goto fail;

	if (!connect(renderer, SIGNAL(stateChanged(MafwRenderer::State)),
			this, SLOT(state_changed(MafwRenderer::State))))
		goto fail;

	if (!connect(renderer, SIGNAL(metadataChanged(const QString&, const QList<QVariant>&)),
			this, SLOT(metadata_changed(const QString&, const QList<QVariant>&))))
		goto fail;

	tk_factory = new MafwTrackerModelFactory();
	tk_factory->init();

	tk_conn = tk_factory->trackerConnection();
	if (!connect(tk_conn, SIGNAL(musicFavorited(const QSet<int>&)),
			this, SLOT(favorited(const QSet<int>&))))
		goto tk_fail;

	if (!connect(tk_conn, SIGNAL(musicUnfavorited(const QSet<int>&)),
			this, SLOT(unfavorited(const QSet<int>&))))
		goto tk_fail;

	return true;
fail:
	QCoreApplication::exit(-1);
	return false;
tk_fail:
	return false;
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

static const QString ID_QUERY =
	"SELECT nmm:artistName(nmm:performer(?song)) nie:title(?song) %1 "
	"WHERE { ?song a nmm:MusicPiece . FILTER( tracker:id(?song) IN (%2) ) }";

void Listener::set_favorite(const QSet<int>& ids, bool on)
{
	QStringList list;

	foreach(int id, ids) {
		list << QString::number(id);
	}

	tk_conn->queueQuery(ID_QUERY.arg(QString::number(on), list.join(",")),
                       3, this, SLOT(got_info(QList<QStringList>,bool)), NULL);
}

void Listener::favorited(const QSet<int>& ids)
{
	set_favorite(ids, true);
}

void Listener::unfavorited(const QSet<int>& ids)
{
	set_favorite(ids, false);
}

void Listener::got_info(QList<QStringList> rows, bool foo)
{
	foreach(QStringList row, rows) {
		hp_love(row[0].toUtf8(), row[1].toUtf8(), row[2].toInt());
	}
}

static void signal_handler(int signal)
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

	if (!listener.init()) {
		r = -1;
		goto leave;
	}

	r = a.exec();

leave:
	hp_deinit();

	return r;
};
