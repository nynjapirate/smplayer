/*  smplayer fork patch — Phase E: preferences page for custom move locations.
 *
 *  Lets the user pick destination folders + labels for each of the N move
 *  slots. Shortcuts are NOT edited here — they live in the standard
 *  Preferences → Keyboard editor under names "move_to_folder_1" … etc.
 */

#ifndef SMP_PREFMOVELOCATIONS_H
#define SMP_PREFMOVELOCATIONS_H

#include "prefwidget.h"
#include "custommove.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QSignalMapper;
class Preferences;

class PrefMoveLocations : public PrefWidget
{
	Q_OBJECT
public:
	PrefMoveLocations(QWidget * parent = 0, Qt::WindowFlags f = QFlag(0));
	~PrefMoveLocations();

	virtual QString sectionName();
	virtual QPixmap sectionIcon();

	void setData(Preferences * pref);
	void getData(Preferences * pref);

protected:
	virtual void retranslateStrings();
	virtual void createHelp();

private slots:
	void onBrowse(int slot_idx);

private:
	QLineEdit   * m_path_edits[CustomMoveLocations::kNumSlots];
	QLineEdit   * m_label_edits[CustomMoveLocations::kNumSlots];
	QPushButton * m_browse_btns[CustomMoveLocations::kNumSlots];
	QSignalMapper * m_browse_mapper;
	QCheckBox * m_create_shortcuts_cb;
};

#endif
