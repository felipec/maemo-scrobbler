#include <QObject>
#include <QStringList>
#include <MafwRenderer.h>
#include <MafwMediaInfo.h>

class MafwShared;
class MafwRegistry;
class MafwTrackerModelFactory;
class MafwTrackerModelConnection;

class Listener : public QObject
{
	Q_OBJECT

public:
	Listener(QObject *parent = NULL);

private slots:
	void next(void);
	void metadata_changed(const QString&, const QList<QVariant>&);
	void state_changed(MafwRenderer::State);

	void favorited(const QSet<int>& ids);
	void unfavorited(const QSet<int>& ids);
	void set_favorite(const QSet<int>& ids, bool on);
	void got_info(QList<QStringList> rows, bool foo);

private:
	MafwShared *shared;
	MafwRegistry *registry;
	MafwRenderer *renderer;

	MafwTrackerModelFactory *tk_factory;
	MafwTrackerModelConnection *tk_conn;
};
