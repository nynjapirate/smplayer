#include "custommove.h"
#include "myaction.h"
#include "baseguiplus.h"
#include "core.h"
#include "playlist.h"
#include "mediadata.h"
#include "paths.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QMessageBox>
#include <QKeySequence>
#include <QDebug>

CustomMoveLocations * CustomMoveLocations::s_instance = 0;

static QString iniPath()
{
	const QString d = Paths::configPath();
	if (!d.isEmpty()) return d + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

void CustomMoveLocations::loadSlots(QVector<Slot> & out)
{
	out.clear();
	out.resize(kNumSlots);
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("custom_move");
	for (int i = 0; i < kNumSlots; ++i) {
		const QString g = QString("slots/%1/").arg(i + 1);
		out[i].path  = s.value(g + "path",  QString()).toString();
		out[i].label = s.value(g + "label", QString()).toString();
	}
	s.endGroup();
}

void CustomMoveLocations::saveSlots(const QVector<Slot> & in)
{
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("custom_move");
	for (int i = 0; i < kNumSlots && i < in.size(); ++i) {
		const QString g = QString("slots/%1/").arg(i + 1);
		s.setValue(g + "path",  in[i].path);
		s.setValue(g + "label", in[i].label);
	}
	s.endGroup();
}

bool CustomMoveLocations::loadCreateShortcuts()
{
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("custom_move");
	const bool v = s.value("create_shortcuts", false).toBool();
	s.endGroup();
	return v;
}

void CustomMoveLocations::saveCreateShortcuts(bool b)
{
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("custom_move");
	s.setValue("create_shortcuts", b);
	s.endGroup();
}

CustomMoveLocations::CustomMoveLocations(BaseGuiPlus * gui)
	: QObject(gui)
	, m_gui(gui)
	, m_delete_current(0)
	, m_create_shortcuts(false)
{
	s_instance = this;
	loadSlots(m_slots);
	m_create_shortcuts = loadCreateShortcuts();
	buildActions();
}

void CustomMoveLocations::buildActions()
{
	// All actions parented to the BaseGuiPlus (a QWidget) so
	// `findChildren<QAction*>()` in ActionsEditor::addActions picks them up
	// for the keyboard editor. Default shortcuts: Ctrl+Shift+1..9, +0; the
	// user can change them in Preferences → Keyboard.
	for (int i = 0; i < kNumSlots; ++i) {
		const QString name = QString("move_to_folder_%1").arg(i + 1);
		const int digit = (i + 1) % 10;  // slot 10 → digit 0
		QKeySequence ks(Qt::CTRL | Qt::SHIFT | (Qt::Key_0 + digit));

		MyAction * a = new MyAction(ks, m_gui, name.toLocal8Bit().constData());
		a->setProperty("slot_idx", i);
		connect(a, SIGNAL(triggered()), this, SLOT(onMoveTriggered()));
		m_move_actions.append(a);
	}

	m_delete_current = new MyAction(
		QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete),
		m_gui, "delete_current_file");
	connect(m_delete_current, SIGNAL(triggered()),
	        this, SLOT(onDeleteCurrentTriggered()));

	reloadFromConfig();  // sets initial text + enabled state
}

void CustomMoveLocations::reloadFromConfig()
{
	loadSlots(m_slots);
	m_create_shortcuts = loadCreateShortcuts();
	const QString verb = m_create_shortcuts ? tr("Link to") : tr("Move to");
	for (int i = 0; i < kNumSlots && i < m_move_actions.size(); ++i) {
		MyAction * a = m_move_actions[i];
		const Slot & s = m_slots[i];
		const bool ok = !s.path.isEmpty();
		a->setEnabled(ok);
		QString text;
		if (ok) {
			if (!s.label.isEmpty()) text = QString("%1 %2").arg(verb, s.label);
			else                    text = QString("%1 %2").arg(verb, s.path);
		} else {
			text = tr("%1 (slot %2, unconfigured)").arg(verb).arg(i + 1);
		}
		a->setText(text);
	}
	if (m_delete_current) m_delete_current->setText(tr("Delete current file from disk"));
}

QString CustomMoveLocations::uniquifyDestPath(const QString & dest) const
{
	if (!QFile::exists(dest)) return dest;
	const QFileInfo fi(dest);
	const QString stem  = fi.completeBaseName();
	const QString suffix = fi.suffix();
	const QString dir   = fi.absolutePath();
	for (int n = 1; n < 10000; ++n) {
		QString candidate = dir + "/" + stem
		                    + QString(" (%1)").arg(n)
		                    + (suffix.isEmpty() ? "" : "." + suffix);
		if (!QFile::exists(candidate)) return candidate;
	}
	return dest;  // give up
}

void CustomMoveLocations::onMoveTriggered()
{
	QObject * s = sender();
	if (!s) return;
	const int idx = s->property("slot_idx").toInt();
	if (idx < 0 || idx >= kNumSlots) return;
	doMove(idx);
}

void CustomMoveLocations::doMove(int slot_idx)
{
	if (!m_gui) return;
	Core * core = m_gui->getCore();
	Playlist * playlist = m_gui->getPlaylist();
	if (!core) return;

	auto osd = [core](const QString & msg) {
		core->displayTextOnOSD(msg, 3000, 1);
	};

	if (slot_idx < 0 || slot_idx >= m_slots.size()) {
		osd(tr("Move slot out of range"));
		return;
	}
	const Slot & slot = m_slots[slot_idx];
	if (slot.path.isEmpty()) {
		osd(tr("Move slot %1 not configured").arg(slot_idx + 1));
		return;
	}

	if (core->mdat.type != TYPE_FILE) {
		osd(tr("Cannot move (not a local file)"));
		return;
	}
	const QString src = core->mdat.filename;
	if (src.isEmpty()) { osd(tr("No file is playing")); return; }
	if (src.contains("://"))  { osd(tr("Cannot move (not a local file)")); return; }

	QFileInfo si(src);
	if (!si.exists() || !si.isFile()) {
		osd(tr("Source file not found"));
		return;
	}
	if (!si.isReadable()) { osd(tr("Source file not readable")); return; }

	QFileInfo di(slot.path);
	if (!di.exists()) {
		// Try to create it on the fly — common case where the user
		// pre-configured a folder that doesn't exist yet.
		if (!QDir().mkpath(slot.path)) {
			osd(tr("Destination folder doesn't exist and couldn't be created: %1").arg(slot.path));
			return;
		}
		di.setFile(slot.path);
	}
	if (!di.isDir()) { osd(tr("Destination is not a folder: %1").arg(slot.path)); return; }
	if (!di.isWritable()) {
		osd(tr("Destination not writable: %1").arg(slot.path));
		return;
	}

	QString dest = QDir(slot.path).filePath(si.fileName());
	dest = uniquifyDestPath(dest);

	if (m_create_shortcuts) {
		// Symlink at dest → src. Original stays in place; playlist row
		// stays too (the entry still points at a valid file).
		if (!QFile::link(si.absoluteFilePath(), dest)) {
			osd(tr("Link failed"));
			return;
		}
		osd(tr("Linked to %1").arg(QFileInfo(dest).absolutePath()));
		return;
	}

	bool moved = QFile::rename(src, dest);
	if (!moved) {
		// Cross-filesystem rename fails on Linux; fall back to copy+remove.
		if (QFile::copy(src, dest)) {
			if (QFile::remove(src)) moved = true;
			else QFile::remove(dest);  // partial — clean up
		}
	}
	if (!moved) {
		osd(tr("Move failed"));
		return;
	}

	// Drop the row but don't stop mpv — the file handle is still valid.
	if (playlist) playlist->removeRowByFilename(src);

	// Update Core's notion of the current filename so anything that reads
	// it later (recents, history) sees the new path.
	core->mdat.filename = dest;

	osd(tr("Moved to %1").arg(QFileInfo(dest).absolutePath()));
}

void CustomMoveLocations::onDeleteCurrentTriggered()
{
	if (!m_gui) return;
	Core * core = m_gui->getCore();
	Playlist * playlist = m_gui->getPlaylist();
	if (!core) return;

	auto osd = [core](const QString & msg) {
		core->displayTextOnOSD(msg, 3000, 1);
	};

	if (core->mdat.type != TYPE_FILE) { osd(tr("Cannot delete (not a local file)")); return; }
	const QString src = core->mdat.filename;
	if (src.isEmpty() || src.contains("://")) { osd(tr("Cannot delete (not a local file)")); return; }

	QFileInfo fi(src);
	if (!(fi.exists() && fi.isFile() && fi.isWritable())) {
		osd(tr("File can't be deleted"));
		return;
	}

	const int res = QMessageBox::question(m_gui, tr("Confirm deletion"),
		tr("DELETE the currently-playing file '%1' from your drive?\n"
		   "This action cannot be undone.").arg(src),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (res != QMessageBox::Yes) return;

	if (!QFile::remove(src)) {
		osd(tr("Delete failed"));
		return;
	}
	if (playlist) playlist->removeRowByFilename(src);
	osd(tr("Deleted %1").arg(fi.fileName()));
}

#include "moc_custommove.cpp"
