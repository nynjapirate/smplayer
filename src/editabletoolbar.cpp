/*  smplayer, GUI front-end for mplayer.
    Copyright (C) 2006-2023 Ricardo Villalba <ricardo@smplayer.info>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "editabletoolbar.h"
#include "toolbareditor.h"
#include "toolbaroverrides.h"
#include "paths.h"

#include <QAction>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>

EditableToolbar::EditableToolbar(QWidget * parent) : QToolBar(parent)
	, icon_only_mode(false)
{
	widget = parent;
}

EditableToolbar::~EditableToolbar() {
}

static QString smpIniPath() {
	const QString d = Paths::configPath();
	if (!d.isEmpty()) return d + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

void EditableToolbar::setIconOnlyMode(bool b) {
	icon_only_mode = b;
	saveIconOnlyMode();
	applyButtonStyles();
}

void EditableToolbar::saveIconOnlyMode() {
	if (objectName().isEmpty()) return;
	QSettings s(smpIniPath(), QSettings::IniFormat);
	s.beginGroup("toolbar_settings");
	s.beginGroup(objectName());
	s.setValue("icon_only_mode", icon_only_mode);
	s.endGroup();
	s.endGroup();
}

void EditableToolbar::loadIconOnlyMode() {
	if (objectName().isEmpty()) return;
	QSettings s(smpIniPath(), QSettings::IniFormat);
	s.beginGroup("toolbar_settings");
	s.beginGroup(objectName());
	icon_only_mode = s.value("icon_only_mode", false).toBool();
	s.endGroup();
	s.endGroup();
}

void EditableToolbar::applyButtonStyles() {
	// Toolbar-wide default. Mostly cosmetic — the per-button settings
	// below are explicit and win regardless.
	setToolButtonStyle(icon_only_mode ? Qt::ToolButtonIconOnly
	                                  : Qt::ToolButtonFollowStyle);

	// Per-button styling. ALL three branches set an *explicit* style —
	// never `FollowStyle` for the icon-only case, because FollowStyle on
	// a button means "follow the platform style hint" (typically the
	// system's default for QStyle::SH_ToolButtonStyle), NOT "inherit from
	// the parent toolbar". So previously when icon_only_mode was on we
	// ended up at the platform default, which on most desktops looked
	// identical to the icon_only_mode-off state.
	const QList<QAction*> acts = actions();
	for (QAction * a : acts) {
		// Defensive: re-apply the persisted override to the action
		// itself before computing the button's style. Without this,
		// other smplayer code paths (retranslateStrings, change-on-state
		// updates) sometimes wipe the action's text or icon between when
		// the override was saved and when applyButtonStyles runs.
		ToolbarOverrides::applyToAction(a);

		QToolButton * btn = qobject_cast<QToolButton*>(widgetForAction(a));
		if (!btn) continue;

		const ToolbarOverrides::Entry ovr = a->objectName().isEmpty()
			? ToolbarOverrides::Entry()
			: ToolbarOverrides::load(a->objectName());
		const bool has_text_override = !ovr.text.isEmpty();
		const bool has_icon          = !a->icon().isNull();

		Qt::ToolButtonStyle style;
		if (has_text_override) {
			// User explicitly set custom text — must show it.
			style = has_icon ? Qt::ToolButtonTextBesideIcon
			                 : Qt::ToolButtonTextOnly;
		} else if (icon_only_mode) {
			// Icon-only mode forces icon visibility, with text fallback
			// for buttons that don't actually have an icon.
			style = has_icon ? Qt::ToolButtonIconOnly
			                 : Qt::ToolButtonTextOnly;
		} else {
			// Neither overridden nor icon-only — defer to platform.
			style = Qt::ToolButtonFollowStyle;
		}
		btn->setToolButtonStyle(style);
	}
}

QList<QAction *> EditableToolbar::allActions() {
	if (!all_actions.isEmpty()) return all_actions;

	QList<QAction *> actions;
	if (widget) {
		actions = widget->findChildren<QAction *>();
	}
	return actions;
}

void EditableToolbar::setActionsFromStringList(QStringList action_names) {
	clear();
	ToolbarEditor::load(this, action_names, allActions());
	// First-time-this-session load of the persisted icon_only_mode for
	// this toolbar. setObjectName is set externally before this runs, so
	// the per-toolbar key is resolvable. Idempotent: if the toolbar gets
	// rebuilt (after editing) we just re-read the same value.
	loadIconOnlyMode();
	// Per-button styles handle icon-only mode + null-icon fallback +
	// the text-override case.
	applyButtonStyles();
}

QStringList EditableToolbar::actionsToStringList() {
	return ToolbarEditor::save(this);
}

void EditableToolbar::edit() {
	qDebug("EditableToolbar::edit");

	ToolbarEditor e(widget);
	e.setAllActions(allActions());
	e.setActiveActions(this->actions());
	e.setDefaultActions(defaultActions());
	e.setIconSize(iconSize().width());
	e.setIconOnlyMode(icon_only_mode);

	if (e.exec() == QDialog::Accepted) {
		QStringList r = e.activeActionsToStringList();
		qDebug("EditableToolbar::edit: list: %s", r.join(",").toUtf8().constData());
		setActionsFromStringList(r);
		resize(width(), e.iconSize());
		setIconSize(QSize(e.iconSize(), e.iconSize()));
		setIconOnlyMode(e.iconOnlyMode());
	}
}

#include "moc_editabletoolbar.cpp"

