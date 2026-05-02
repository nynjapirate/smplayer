/*  smplayer fork patch — Phase G: per-action icon/text overrides for toolbar buttons.
 *
 *  Persists user-chosen icon and/or text per QAction objectName. Applied
 *  to live actions at startup (from BaseGui::loadActions) and after the
 *  toolbar editor finishes. Storage: smplayer.ini section
 *  [toolbar_action_overrides] with sub-keys
 *      <action_name>/icon  → either an icon-theme name (e.g. "media-playback-start")
 *                            or an absolute filesystem path to an image file.
 *      <action_name>/text  → user-overridden display text.
 *  Empty value or missing key means "use the action's default".
 */

#ifndef SMP_TOOLBAROVERRIDES_H
#define SMP_TOOLBAROVERRIDES_H

#include <QString>
#include <QIcon>
#include <QList>

class QAction;

class ToolbarOverrides
{
public:
	struct Entry {
		QString icon;  // theme name OR absolute file path; empty = no override
		QString text;  // empty = no override
	};

	// Read/write entries by action objectName.
	static Entry load(const QString & action_name);
	static void  save(const QString & action_name, const Entry & e);
	static void  clear(const QString & action_name);  // remove the override

	// Resolve an icon spec ("theme:name", absolute path, or bare theme name)
	// into a QIcon. Returns null QIcon when no resolution succeeds.
	static QIcon resolveIcon(const QString & spec);

	// Apply persisted overrides to every action in `actions`. Stash the
	// action's original icon + text on first apply so a future "Reset" can
	// restore. Idempotent.
	static void applyToActions(const QList<QAction*> & actions);

	// Reapply overrides for a single action — used after the per-button
	// editor dialog edits one entry.
	static void applyToAction(QAction * action);
};

#endif
