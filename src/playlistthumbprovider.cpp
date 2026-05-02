#include "playlistthumbprovider.h"
#include "paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDebug>

PlaylistThumbProvider * PlaylistThumbProvider::instance()
{
	static PlaylistThumbProvider * inst = 0;
	if (!inst) inst = new PlaylistThumbProvider();
	return inst;
}

PlaylistThumbProvider::PlaylistThumbProvider(QObject * parent)
	: QObject(parent)
	, m_enabled(false)
	, m_row_height(96)
{
	loadConfig();
	// Ensure cache dir exists.
	QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
	            + "/smplayer/thumbnails";
	QDir().mkpath(d);
}

static QString smpIniPath2()
{
	const QString d = Paths::configPath();
	if (!d.isEmpty()) return d + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

void PlaylistThumbProvider::loadConfig()
{
	QSettings s(smpIniPath2(), QSettings::IniFormat);
	s.beginGroup("playlist_thumbnails");
	m_enabled    = s.value("enabled",    m_enabled).toBool();
	m_row_height = s.value("row_height", m_row_height).toInt();
	s.endGroup();
	if (m_row_height < 32)  m_row_height = 32;
	if (m_row_height > 256) m_row_height = 256;
}

void PlaylistThumbProvider::saveConfig()
{
	QSettings s(smpIniPath2(), QSettings::IniFormat);
	s.beginGroup("playlist_thumbnails");
	s.setValue("enabled",    m_enabled);
	s.setValue("row_height", m_row_height);
	s.endGroup();
}

void PlaylistThumbProvider::setEnabled(bool b)
{
	if (m_enabled == b) return;
	m_enabled = b;
	saveConfig();
}

void PlaylistThumbProvider::setRowHeight(int px)
{
	if (px < 32)  px = 32;
	if (px > 256) px = 256;
	if (m_row_height == px) return;
	m_row_height = px;
	saveConfig();
}

void PlaylistThumbProvider::clearCache()
{
	m_mem_cache.clear();
	const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
	                  + "/smplayer/thumbnails";
	QDir(d).removeRecursively();
	QDir().mkpath(d);
}

QString PlaylistThumbProvider::diskCachePath(const QString & filename, int height_px) const
{
	QFileInfo fi(filename);
	// Hash the absolute path + size + mtime so the cache invalidates if
	// the user replaces the file with a different one of the same name.
	QByteArray seed = fi.absoluteFilePath().toUtf8()
	                  + ":" + QByteArray::number(fi.size())
	                  + ":" + QByteArray::number(fi.lastModified().toSecsSinceEpoch())
	                  + ":h" + QByteArray::number(height_px);
	const QByteArray h = QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex();
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
	       + "/smplayer/thumbnails/" + QString::fromLatin1(h) + ".jpg";
}

QPixmap PlaylistThumbProvider::cached(const QString & filename, int height_px) const
{
	const QString k = filename + "|" + QString::number(height_px);
	auto it = m_mem_cache.find(k);
	if (it != m_mem_cache.end()) return it.value();

	const QString p = diskCachePath(filename, height_px);
	if (QFile::exists(p)) {
		QPixmap pm(p);
		if (!pm.isNull()) {
			m_mem_cache.insert(k, pm);
			return pm;
		}
	}
	return QPixmap();
}

void PlaylistThumbProvider::request(const QString & filename, int height_px)
{
	if (!m_enabled) return;
	if (filename.isEmpty()) return;

	// Already cached → nothing to do; caller will pull via cached().
	const QString k = filename + "|" + QString::number(height_px);
	if (m_mem_cache.contains(k)) return;
	if (QFile::exists(diskCachePath(filename, height_px))) return;
	if (m_pending_keys.contains(k)) return;
	if (m_failed_keys.contains(k)) return;  // tried and failed already

	// Only fetch local files that exist.
	QFileInfo fi(filename);
	if (!fi.exists() || !fi.isFile()) return;

	PendingItem it;
	it.filename = filename;
	it.height = height_px;
	m_queue.append(it);
	m_pending_keys.insert(k);
	pumpQueue();
}

void PlaylistThumbProvider::pumpQueue()
{
	while (m_active.size() < MAX_CONCURRENT && !m_queue.isEmpty()) {
		PendingItem it = m_queue.takeFirst();
		QString out = diskCachePath(it.filename, it.height);

		QProcess * p = new QProcess(this);
		m_active.insert(p, it);

		connect(p, static_cast<void (QProcess::*)(int)>(&QProcess::finished),
		        this, &PlaylistThumbProvider::onProcFinished);
		connect(p, &QProcess::errorOccurred,
		        this, &PlaylistThumbProvider::onProcError);

		QStringList args;
		args << "-i" << it.filename
		     << "-o" << out
		     << "-s" << QString::number(it.height * 16 / 9)
		     << "-t" << "10%"
		     << "-q" << "8"
		     << "-c" << "jpeg";
		p->start("ffmpegthumbnailer", args);
	}
}

void PlaylistThumbProvider::retire(QProcess * p, bool /*succeeded*/)
{
	auto it = m_active.find(p);
	if (it == m_active.end()) { p->deleteLater(); pumpQueue(); return; }
	const PendingItem item = it.value();
	m_active.erase(it);

	const QString k = item.filename + "|" + QString::number(item.height);
	m_pending_keys.remove(k);
	const QString out = diskCachePath(item.filename, item.height);

	bool produced = false;
	if (QFile::exists(out)) {
		QPixmap pm(out);
		if (!pm.isNull()) {
			m_mem_cache.insert(k, pm);
			emit thumbnailReady(item.filename);
			produced = true;
		}
	}
	if (!produced) {
		// ffmpegthumbnailer either crashed (errorOccurred) or exited
		// without producing a file (corrupt input, unsupported codec,
		// permission issue). Deny-list this key so the delegate doesn't
		// loop on every paint trying to refetch.
		m_failed_keys.insert(k);
	}

	p->deleteLater();
	pumpQueue();
}

void PlaylistThumbProvider::onProcFinished(int /*exitCode*/)
{
	if (QProcess * p = qobject_cast<QProcess*>(sender())) retire(p, true);
}

void PlaylistThumbProvider::onProcError()
{
	if (QProcess * p = qobject_cast<QProcess*>(sender())) retire(p, false);
}
