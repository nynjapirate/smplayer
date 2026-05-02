/*  smplayer fork patch — Phase E: custom move-to-folder feature.
 *
 *  Owns N MyAction slots (objectName="move_to_folder_1".."N") plus a
 *  "delete_current_file" action. Each move slot is configured with a
 *  destination folder + label in smplayer.ini under [custom_move]. When
 *  triggered, the currently-playing file is renamed into the slot's folder,
 *  the playlist row is dropped, and an OSD blurb confirms — but mpv keeps
 *  playing the in-flight file (Linux file handles survive the rename).
 *
 *  Action shortcuts are managed by SMPlayer's normal ActionsEditor flow —
 *  every slot action is parented to BaseGuiPlus so findChildren<QAction*>()
 *  picks them up automatically. The user assigns shortcuts in
 *  Preferences → Keyboard and mouse → Keyboard.
 */

#ifndef SMP_CUSTOMMOVE_H
#define SMP_CUSTOMMOVE_H

#include <QObject>
#include <QString>
#include <QVector>

class BaseGuiPlus;
class MyAction;

class CustomMoveLocations : public QObject
{
	Q_OBJECT
public:
	static const int kNumSlots = 10;

	struct Slot {
		QString path;   // absolute destination folder
		QString label;  // user-friendly name (used in OSD + future menu)
	};

	explicit CustomMoveLocations(BaseGuiPlus * gui);

	// Pref page accesses this to push edits back into the running actions.
	static CustomMoveLocations * instance() { return s_instance; }

	// Persistence helpers exposed for the pref page.
	static void loadSlots(QVector<Slot> & out);
	static void saveSlots(const QVector<Slot> & in);
	static bool loadCreateShortcuts();
	static void saveCreateShortcuts(bool b);

	bool createShortcuts() const { return m_create_shortcuts; }

public slots:
	// Re-read [custom_move] from smplayer.ini and update action enabled
	// state + display labels. Called after the pref page Apply.
	void reloadFromConfig();

private slots:
	void onMoveTriggered();
	void onDeleteCurrentTriggered();

private:
	void buildActions();
	void doMove(int slot_idx);
	QString uniquifyDestPath(const QString & dest) const;

	BaseGuiPlus * m_gui;
	QVector<MyAction*> m_move_actions;  // size kNumSlots
	MyAction * m_delete_current;
	QVector<Slot> m_slots;
	bool m_create_shortcuts;

	static CustomMoveLocations * s_instance;
};

#endif
