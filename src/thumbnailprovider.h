/*  smplayer fork patch — Phase D: hover-thumbnail provider.
 *
 *  Persistent headless mpv subprocess held open across hovers; we drive it
 *  via JSON-RPC over a Unix domain socket. ~5–20 ms per thumbnail since the
 *  decoder, demuxer, and file are kept warm. One-slot LIFO pending: a new
 *  hover overwrites any pending request, so when the in-flight finishes we
 *  serve the user's CURRENT cursor position, not a stale one.
 */

#ifndef SMP_THUMBNAILPROVIDER_H
#define SMP_THUMBNAILPROVIDER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QHash>
#include <QByteArray>
#include <QProcess>

class QLocalSocket;
class QTimer;

class ThumbnailProvider : public QObject
{
	Q_OBJECT
public:
	static ThumbnailProvider * instance();

	void requestThumbnail(qint64 timeMs);  // async; result via thumbnailReady

	// User-configurable knobs. Persisted under [hover_thumbnails] in
	// smplayer.ini via a private QSettings (we don't want to bolt new
	// fields onto Global::pref for a fork patch).
	bool enabled() const     { return m_enabled; }
	int  thumbWidth() const  { return m_thumb_width; }
	int  thumbQuality() const{ return m_thumb_quality; }

	void setEnabled(bool b);
	void setThumbWidth(int px);    // 160 / 240 / 320 / 480
	void setThumbQuality(int q);   // 0..10 — mpv's --screenshot-jpeg-quality

public slots:
	void setCurrentFile(const QString & filename, const QString & title);
	void setDuration(double duration_sec);
	void clearCache();

signals:
	void thumbnailReady(qint64 timeMs, const QImage & img);

private slots:
	void onMpvSocketConnected();
	void onMpvSocketReadyRead();
	void onMpvSocketDisconnected();
	void onMpvProcessFinished(int exitCode, QProcess::ExitStatus status);
	void onConnectRetry();

private:
	explicit ThumbnailProvider(QObject * parent = 0);
	~ThumbnailProvider();

	void mpvStart();
	void mpvStop();
	void mpvSendCommand(const QByteArray & json);
	void mpvProcessLine(const QByteArray & line);
	void issueRequest(qint64 timeMs);
	void serveCacheHit(qint64 timeMs);

	// Round to nearest BUCKET_MS so adjacent pixel hovers coalesce.
	qint64 bucket(qint64 timeMs) const { return (timeMs / BUCKET_MS) * BUCKET_MS; }

	QString m_filename;
	double  m_duration;       // seconds
	qint64  m_inflight_ms;    // -1 = idle
	qint64  m_pending_ms;     // -1 = none. Overwritten by latest hover.
	QHash<qint64, QImage> m_cache;

	QProcess     * m_mpv;
	QLocalSocket * m_socket;
	QTimer       * m_connect_timer;
	int            m_connect_attempts;
	bool           m_socket_ready;
	bool           m_file_loaded;
	QByteArray     m_recvbuf;

	QString m_socket_path;
	QString m_screenshot_dir;
	int     m_serial;             // bumped each mpvStart for unique paths
	int     m_next_req_id;
	int     m_screenshot_req_id;  // -1 idle
	QString m_inflight_outpath;

	void loadConfig();
	void saveConfig();
	void restartIfNeeded();        // re-spawn mpv when quality changes mid-file

	bool m_enabled;
	int  m_thumb_width;            // px, applied post-screenshot
	int  m_thumb_quality;          // 0..10, passed to mpv --screenshot-jpeg-quality

	static const int   CACHE_MAX    = 200;
	static const qint64 BUCKET_MS   = 1000;  // 1 s buckets — generous given seekbar pixel resolution
};

#endif
