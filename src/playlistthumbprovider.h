/*  smplayer fork patch — Phase C4: per-file thumbnail provider for the
 *  playlist's thumbnail-row view mode.
 *
 *  Lazy + cached: a row only fetches its thumbnail when the delegate paints
 *  it. Results land on disk under ~/.cache/smplayer/thumbnails/<hash>.jpg
 *  so they survive restarts. Up to 2 concurrent ffmpegthumbnailer workers.
 *
 *  Different from ThumbnailProvider (Phase D), which holds one persistent
 *  mpv per current file for fast hover seeking. Here we have many files
 *  but only one position each, so spawn-per-file is cheaper.
 */

#ifndef SMP_PLAYLISTTHUMBPROVIDER_H
#define SMP_PLAYLISTTHUMBPROVIDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPixmap>
#include <QHash>
#include <QSet>
#include <QList>

class QProcess;

class PlaylistThumbProvider : public QObject
{
	Q_OBJECT
public:
	static PlaylistThumbProvider * instance();

	// Synchronous cache lookup. Returns a null QPixmap if not yet fetched
	// — caller should still call request() to trigger async generation.
	QPixmap cached(const QString & filename, int height_px) const;

	// Queue up an async fetch. Idempotent — duplicate requests for the
	// same (filename, size) are coalesced. Emits thumbnailReady when done.
	void request(const QString & filename, int height_px);

	bool enabled() const { return m_enabled; }
	int  rowHeight() const { return m_row_height; }

	void setEnabled(bool b);
	void setRowHeight(int px);    // 64..256

public slots:
	void clearCache();

signals:
	void thumbnailReady(const QString & filename);

private slots:
	void onProcFinished(int exitCode);
	void onProcError();

private:
	explicit PlaylistThumbProvider(QObject * parent = 0);

	void loadConfig();
	void saveConfig();
	QString diskCachePath(const QString & filename, int height_px) const;
	void pumpQueue();
	void retire(QProcess * p, bool succeeded);   // shared cleanup path

	struct PendingItem {
		QString filename;
		int height;
	};
	QList<PendingItem> m_queue;
	QSet<QString> m_pending_keys;        // "filename|size" already queued or in flight
	QSet<QString> m_failed_keys;         // gave up — don't requeue this session
	QHash<QProcess*, PendingItem> m_active;

	mutable QHash<QString, QPixmap> m_mem_cache;  // keyed "filename|size"

	bool m_enabled;
	int  m_row_height;

	static const int MAX_CONCURRENT = 2;
};

#endif
