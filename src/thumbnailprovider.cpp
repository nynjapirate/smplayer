/*  smplayer fork patch — Phase D: hover-thumbnail provider implementation.
 *
 *  Lifecycle:
 *    setCurrentFile(path)
 *      → mpvStop() then mpvStart() spawning a fresh idle mpv with
 *        --idle=yes --vo=null + --input-ipc-server=<socket> + filePath
 *      → poll for socket, connectToServer
 *      → wait for file-loaded event over IPC
 *      → ready
 *
 *    requestThumbnail(timeMs)
 *      → bucket the timestamp; if cached, emit synchronously
 *      → if mpv idle: issueRequest (seek + screenshot-to-file)
 *      → else: drop into pending slot (overwrite — latest wins)
 *
 *    on screenshot result line:
 *      → load file from outPath, emit thumbnailReady, drop into cache
 *      → if pending slot has a value, dispatch it next
 */

#include "thumbnailprovider.h"

#include "paths.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QLocalSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

ThumbnailProvider * ThumbnailProvider::instance()
{
	static ThumbnailProvider * inst = 0;
	if (!inst) inst = new ThumbnailProvider();  // app-lifetime
	return inst;
}

ThumbnailProvider::ThumbnailProvider(QObject * parent)
	: QObject(parent)
	, m_duration(0)
	, m_inflight_ms(-1)
	, m_pending_ms(-1)
	, m_mpv(0)
	, m_socket(0)
	, m_connect_timer(0)
	, m_connect_attempts(0)
	, m_socket_ready(false)
	, m_file_loaded(false)
	, m_serial(0)
	, m_next_req_id(1)
	, m_screenshot_req_id(-1)
	, m_enabled(true)
	, m_thumb_width(240)
	, m_thumb_quality(85)
{
	QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if (runtime.isEmpty()) runtime = QDir::tempPath();
	m_screenshot_dir = runtime;
	loadConfig();
}

static QString smpIniPath()
{
	// Sit alongside the rest of smplayer's settings — respects -config-path.
	const QString dir = Paths::configPath();
	if (!dir.isEmpty()) return dir + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

void ThumbnailProvider::loadConfig()
{
	QSettings s(smpIniPath(), QSettings::IniFormat);
	s.beginGroup("hover_thumbnails");
	m_enabled       = s.value("enabled",  m_enabled).toBool();
	m_thumb_width   = s.value("width_px", m_thumb_width).toInt();
	m_thumb_quality = s.value("quality",  m_thumb_quality).toInt();
	s.endGroup();
	if (m_thumb_width < 80)   m_thumb_width = 80;
	if (m_thumb_width > 640)  m_thumb_width = 640;
	if (m_thumb_quality < 0)  m_thumb_quality = 0;
	if (m_thumb_quality > 100) m_thumb_quality = 100;
}

void ThumbnailProvider::saveConfig()
{
	QSettings s(smpIniPath(), QSettings::IniFormat);
	s.beginGroup("hover_thumbnails");
	s.setValue("enabled",  m_enabled);
	s.setValue("width_px", m_thumb_width);
	s.setValue("quality",  m_thumb_quality);
	s.endGroup();
}

void ThumbnailProvider::setEnabled(bool b)
{
	if (m_enabled == b) return;
	m_enabled = b;
	saveConfig();
	if (!m_enabled) mpvStop();
	else if (!m_filename.isEmpty()) {
		QFileInfo fi(m_filename);
		if (fi.exists() && fi.isFile()) mpvStart();
	}
}

void ThumbnailProvider::setThumbWidth(int px)
{
	if (px < 80)  px = 80;
	if (px > 640) px = 640;
	if (m_thumb_width == px) return;
	m_thumb_width = px;
	saveConfig();
	// Cached thumbs are at the old size — drop them so the next hover
	// regenerates at the new width.
	m_cache.clear();
}

void ThumbnailProvider::setThumbQuality(int q)
{
	if (q < 0)   q = 0;
	if (q > 100) q = 100;
	if (m_thumb_quality == q) return;
	m_thumb_quality = q;
	saveConfig();
	m_cache.clear();
	restartIfNeeded();  // mpv reads --screenshot-jpeg-quality at start
}

void ThumbnailProvider::restartIfNeeded()
{
	if (m_filename.isEmpty()) return;
	QFileInfo fi(m_filename);
	if (!fi.exists() || !fi.isFile()) return;
	mpvStop();
	if (m_enabled) mpvStart();
}

ThumbnailProvider::~ThumbnailProvider()
{
	mpvStop();
}

void ThumbnailProvider::setCurrentFile(const QString & filename, const QString & /*title*/)
{
	if (filename == m_filename) return;
	m_filename = filename;
	m_cache.clear();
	m_inflight_ms = -1;
	m_pending_ms = -1;
	m_screenshot_req_id = -1;
	m_inflight_outpath.clear();

	mpvStop();
	if (m_filename.isEmpty() || !m_enabled) return;

	// Only handle local files. Streams (http, ytdl, etc.) skipped — mpv
	// would re-open them in the worker process which is wasteful.
	QFileInfo fi(m_filename);
	if (!fi.exists() || !fi.isFile()) return;

	mpvStart();
}

void ThumbnailProvider::setDuration(double duration_sec)
{
	m_duration = duration_sec;
}

void ThumbnailProvider::clearCache()
{
	m_cache.clear();
}

void ThumbnailProvider::requestThumbnail(qint64 timeMs)
{
	if (!m_enabled) return;
	if (m_filename.isEmpty()) return;
	if (m_duration > 0 && timeMs > qint64(m_duration * 1000)) timeMs = qint64(m_duration * 1000);
	if (timeMs < 0) timeMs = 0;

	const qint64 b = bucket(timeMs);
	if (m_cache.contains(b)) {
		serveCacheHit(b);
		return;
	}

	if (!m_socket_ready || !m_file_loaded) {
		// Stash; we'll fire on file-loaded.
		m_pending_ms = b;
		return;
	}

	if (m_inflight_ms >= 0) {
		// Latest-wins: overwrite the pending slot. The current in-flight
		// will finish, then we'll dispatch this one.
		m_pending_ms = b;
		return;
	}

	issueRequest(b);
}

void ThumbnailProvider::serveCacheHit(qint64 b)
{
	// Defer to next event-loop tick so callers can connect their handler
	// after calling requestThumbnail without missing the signal.
	QImage img = m_cache.value(b);
	QMetaObject::invokeMethod(this, "thumbnailReady", Qt::QueuedConnection,
		Q_ARG(qint64, b), Q_ARG(QImage, img));
}

void ThumbnailProvider::mpvStart()
{
	++m_serial;
	m_socket_path = QString("%1/smplayer-thumb-%2-%3.sock")
	                .arg(m_screenshot_dir).arg(QCoreApplication::applicationPid()).arg(m_serial);
	QFile::remove(m_socket_path);

	m_socket_ready = false;
	m_file_loaded = false;
	m_recvbuf.clear();
	m_screenshot_req_id = -1;

	m_mpv = new QProcess(this);
	m_mpv->setProcessChannelMode(QProcess::SeparateChannels);
	connect(m_mpv, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onMpvProcessFinished(int, QProcess::ExitStatus)));

	QStringList args;
	args << "--idle=yes"
	     << "--no-config"
	     << "--vid=auto"
	     << "--vo=null"
	     << "--no-audio"
	     << "--no-input-terminal"
	     << "--quiet"
	     << "--no-osc"
	     << QString("--input-ipc-server=%1").arg(m_socket_path)
	     << "--screenshot-format=jpg"
	     << QString("--screenshot-jpeg-quality=%1").arg(m_thumb_quality)
	     << m_filename;
	m_mpv->start("mpv", args);

	if (!m_connect_timer) {
		m_connect_timer = new QTimer(this);
		m_connect_timer->setInterval(50);
		connect(m_connect_timer, SIGNAL(timeout()), this, SLOT(onConnectRetry()));
	}
	m_connect_attempts = 0;
	m_connect_timer->start();
}

void ThumbnailProvider::onConnectRetry()
{
	if (!QFileInfo::exists(m_socket_path)) {
		if (++m_connect_attempts > 60) {  // ~3 s
			m_connect_timer->stop();
			qWarning("ThumbnailProvider: mpv socket never appeared, giving up");
		}
		return;
	}
	m_connect_timer->stop();

	if (!m_socket) {
		m_socket = new QLocalSocket(this);
		connect(m_socket, SIGNAL(connected()),    this, SLOT(onMpvSocketConnected()));
		connect(m_socket, SIGNAL(readyRead()),    this, SLOT(onMpvSocketReadyRead()));
		connect(m_socket, SIGNAL(disconnected()), this, SLOT(onMpvSocketDisconnected()));
	}
	m_socket->connectToServer(m_socket_path);
}

void ThumbnailProvider::onMpvSocketConnected()
{
	m_socket_ready = true;

	// mpv emits the `file-loaded` event during its own startup, which
	// happens BEFORE the IPC socket is exposed. We always miss that
	// initial event. Since we always pass the file on the command line,
	// we can optimistically assume the file is loaded by the time we get
	// here. If a screenshot races the actual load, mpv returns an error
	// and the next hover triggers a fresh attempt.
	m_file_loaded = true;

	// Drain any request the user already made while we were spinning up.
	if (m_pending_ms >= 0 && m_inflight_ms < 0) {
		qint64 t = m_pending_ms;
		m_pending_ms = -1;
		issueRequest(t);
	}
}

void ThumbnailProvider::onMpvSocketDisconnected()
{
	m_socket_ready = false;
}

void ThumbnailProvider::onMpvProcessFinished(int /*code*/, QProcess::ExitStatus /*status*/)
{
	if (m_mpv) { m_mpv->deleteLater(); m_mpv = 0; }
	if (m_socket) { m_socket->deleteLater(); m_socket = 0; }
	if (m_connect_timer) m_connect_timer->stop();
	m_socket_ready = false;
	m_file_loaded = false;
	m_inflight_ms = -1;
	m_screenshot_req_id = -1;
	if (!m_inflight_outpath.isEmpty()) QFile::remove(m_inflight_outpath);
}

void ThumbnailProvider::mpvStop()
{
	if (m_connect_timer) m_connect_timer->stop();
	if (m_socket) {
		disconnect(m_socket, 0, this, 0);
		m_socket->abort();
		delete m_socket;
		m_socket = 0;
	}
	if (m_mpv) {
		disconnect(m_mpv, 0, this, 0);
		m_mpv->terminate();
		if (!m_mpv->waitForFinished(500)) {
			m_mpv->kill();
			m_mpv->waitForFinished(500);
		}
		delete m_mpv;
		m_mpv = 0;
	}
	if (!m_socket_path.isEmpty()) QFile::remove(m_socket_path);
	if (!m_inflight_outpath.isEmpty()) QFile::remove(m_inflight_outpath);
	m_socket_ready = false;
	m_file_loaded = false;
	m_recvbuf.clear();
	m_screenshot_req_id = -1;
	m_inflight_ms = -1;
	m_inflight_outpath.clear();
}

void ThumbnailProvider::mpvSendCommand(const QByteArray & json)
{
	if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) return;
	m_socket->write(json);
	if (!json.endsWith('\n')) m_socket->write("\n");
	m_socket->flush();
}

void ThumbnailProvider::issueRequest(qint64 timeMs)
{
	if (!m_socket_ready || !m_file_loaded) {
		m_pending_ms = timeMs;
		return;
	}
	const double secs = double(timeMs) / 1000.0;

	// seek absolute+exact: mpv waits for the target frame to decode before
	// the next command runs. Without "exact", screenshot would capture
	// the pre-seek frame.
	{
		QJsonObject seek;
		seek["command"] = QJsonArray{ "seek", secs, "absolute+exact" };
		seek["request_id"] = m_next_req_id++;
		mpvSendCommand(QJsonDocument(seek).toJson(QJsonDocument::Compact));
	}

	int reqId = m_next_req_id++;
	QString outPath = QString("%1/smplayer-thumb-%2-%3-%4.jpg")
	                  .arg(m_screenshot_dir)
	                  .arg(QCoreApplication::applicationPid())
	                  .arg(m_serial)
	                  .arg(reqId);

	{
		QJsonObject shot;
		shot["command"] = QJsonArray{ "screenshot-to-file", outPath, "video" };
		shot["request_id"] = reqId;
		mpvSendCommand(QJsonDocument(shot).toJson(QJsonDocument::Compact));
	}

	m_inflight_ms = timeMs;
	m_screenshot_req_id = reqId;
	m_inflight_outpath = outPath;
}

void ThumbnailProvider::onMpvSocketReadyRead()
{
	m_recvbuf.append(m_socket->readAll());
	int nl;
	while ((nl = m_recvbuf.indexOf('\n')) >= 0) {
		QByteArray line = m_recvbuf.left(nl);
		m_recvbuf.remove(0, nl + 1);
		if (!line.isEmpty()) mpvProcessLine(line);
	}
}

void ThumbnailProvider::mpvProcessLine(const QByteArray & line)
{
	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(line, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
	const QJsonObject obj = doc.object();

	if (obj.contains("event")) {
		const QString ev = obj["event"].toString();
		if (ev == QLatin1String("file-loaded")) {
			m_file_loaded = true;
			if (m_pending_ms >= 0 && m_inflight_ms < 0) {
				qint64 t = m_pending_ms;
				m_pending_ms = -1;
				issueRequest(t);
			}
		}
		return;
	}

	// Reply to a request. Only the screenshot reply matters to us.
	if (!obj.contains("request_id")) return;
	const int req_id = obj["request_id"].toInt();
	if (req_id != m_screenshot_req_id) return;

	const bool ok = (obj["error"].toString() == QLatin1String("success"));
	const qint64 t = m_inflight_ms;
	const QString outPath = m_inflight_outpath;
	m_inflight_ms = -1;
	m_screenshot_req_id = -1;
	m_inflight_outpath.clear();

	if (ok) {
		QImage img(outPath);
		QFile::remove(outPath);
		if (!img.isNull()) {
			if (img.width() > m_thumb_width) {
				img = img.scaledToWidth(m_thumb_width, Qt::SmoothTransformation);
			}
			if (m_cache.size() >= CACHE_MAX) {
				// Trim ~20% — cheap, no real LRU but acceptable for
				// hover scrubbing patterns.
				int drop = m_cache.size() / 5;
				auto it = m_cache.begin();
				while (drop-- > 0 && it != m_cache.end()) it = m_cache.erase(it);
			}
			m_cache.insert(t, img);
			emit thumbnailReady(t, img);
		}
	} else if (!outPath.isEmpty()) {
		QFile::remove(outPath);
	}

	// Pending slot may have been overwritten while we were busy — serve it.
	if (m_pending_ms >= 0) {
		qint64 next = m_pending_ms;
		m_pending_ms = -1;
		issueRequest(next);
	}
}
