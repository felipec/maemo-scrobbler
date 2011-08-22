#include <QObject>
#include <MafwRenderer.h>
#include <MafwMediaInfo.h>

class MafwShared;
class MafwRegistry;

class Listener : public QObject
{
	Q_OBJECT

public:
	Listener(QObject *parent = NULL);

private slots:
	void next(void);
	void metadata_changed(const QString&, const QList<QVariant>&);
	void state_changed(MafwRenderer::State);

private:
	MafwShared *shared;
	MafwRegistry *registry;
	MafwRenderer *renderer;
};
