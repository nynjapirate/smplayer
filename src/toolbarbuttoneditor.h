/*  smplayer fork patch — Phase G: per-button editor dialog.
 *
 *  Modal dialog for editing one QAction's icon + text override. Opened
 *  from the Toolbar Editor's "Customize…" button. Saves directly to
 *  ToolbarOverrides on accept and reapplies the override to the action so
 *  the change is visible across all toolbars/menus immediately.
 */

#ifndef SMP_TOOLBARBUTTONEDITOR_H
#define SMP_TOOLBARBUTTONEDITOR_H

#include <QDialog>
#include <QString>

class QAction;
class QLabel;
class QLineEdit;
class QPushButton;

class ToolbarButtonEditor : public QDialog
{
	Q_OBJECT
public:
	explicit ToolbarButtonEditor(QAction * action, QWidget * parent = 0);

protected slots:
	void onIconNameChanged(const QString & text);
	void onBrowseFile();
	void onChooseIcon();
	void onResetIcon();
	void onResetText();
	void onResetAll();
	void accept() Q_DECL_OVERRIDE;

private:
	void refreshPreview();

	QAction * m_action;            // the live action — we apply on accept
	QString   m_action_name;       // cached for save key
	QString   m_default_text;      // action's pre-override text (for placeholder + reset)

	QLabel    * m_preview_label;
	QLineEdit * m_icon_edit;       // theme name or absolute file path
	QLineEdit * m_text_edit;
	QPushButton * m_choose_btn;
	QPushButton * m_browse_btn;
	QPushButton * m_reset_icon_btn;
	QPushButton * m_reset_text_btn;
};

#endif
