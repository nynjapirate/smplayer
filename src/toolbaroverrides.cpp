#include "toolbaroverrides.h"
#include "paths.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

static QString iniPath()
{
	const QString d = Paths::configPath();
	if (!d.isEmpty()) return d + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

ToolbarOverrides::Entry ToolbarOverrides::load(const QString & action_name)
{
	Entry e;
	if (action_name.isEmpty()) return e;
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("toolbar_action_overrides");
	s.beginGroup(action_name);
	e.icon = s.value("icon").toString();
	e.text = s.value("text").toString();
	s.endGroup();
	s.endGroup();
	return e;
}

void ToolbarOverrides::save(const QString & action_name, const Entry & e)
{
	if (action_name.isEmpty()) return;
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("toolbar_action_overrides");
	s.beginGroup(action_name);
	if (e.icon.isEmpty()) s.remove("icon"); else s.setValue("icon", e.icon);
	if (e.text.isEmpty()) s.remove("text"); else s.setValue("text", e.text);
	s.endGroup();
	s.endGroup();
}

void ToolbarOverrides::clear(const QString & action_name)
{
	if (action_name.isEmpty()) return;
	QSettings s(iniPath(), QSettings::IniFormat);
	s.beginGroup("toolbar_action_overrides");
	s.remove(action_name);
	s.endGroup();
}

QIcon ToolbarOverrides::resolveIcon(const QString & spec)
{
	if (spec.isEmpty()) return QIcon();
	// If it's an absolute path (or starts with file://), load directly.
	QString path = spec;
	if (path.startsWith("file://")) path.remove(0, 7);
	if (path.startsWith('/') || path.startsWith('~')) {
		if (path.startsWith('~')) path = QDir::homePath() + path.mid(1);
		QFileInfo fi(path);
		if (fi.exists() && fi.isFile()) {
			QIcon i(path);
			if (!i.isNull()) return i;
		}
		return QIcon();
	}
	// Theme name. QIcon::fromTheme uses the system theme first, then any
	// fallbacks Qt knows about. Empty result = no resolution.
	QIcon themed = QIcon::fromTheme(spec);
	return themed;
}

static const char * kPropOriginalIcon = "_smp_orig_icon";
static const char * kPropOriginalText = "_smp_orig_text";

static void stashOriginals(QAction * a) {
	// Stash once. We compare on the property itself rather than always
	// overwriting because reapply may run multiple times during the session.
	if (a->property(kPropOriginalIcon).isNull()) {
		a->setProperty(kPropOriginalIcon, QVariant::fromValue(a->icon()));
	}
	if (a->property(kPropOriginalText).isNull()) {
		a->setProperty(kPropOriginalText, a->text());
	}
}

void ToolbarOverrides::applyToAction(QAction * a)
{
	if (!a || a->objectName().isEmpty()) return;
	stashOriginals(a);

	const Entry e = load(a->objectName());

	// Icon: override with the resolved one, otherwise fall back to the
	// action's stashed original.
	if (!e.icon.isEmpty()) {
		QIcon ic = resolveIcon(e.icon);
		if (!ic.isNull()) a->setIcon(ic);
		else              a->setIcon(qvariant_cast<QIcon>(a->property(kPropOriginalIcon)));
	} else {
		a->setIcon(qvariant_cast<QIcon>(a->property(kPropOriginalIcon)));
	}

	// Text: override or restore.
	if (!e.text.isEmpty()) a->setText(e.text);
	else                   a->setText(a->property(kPropOriginalText).toString());
}

void ToolbarOverrides::applyToActions(const QList<QAction*> & actions)
{
	for (QAction * a : actions) applyToAction(a);
}
