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

#include "toolbareditor.h"
#include "iconpickerdialog.h"
#include "toolbaroverrides.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include "images.h"

ToolbarEditor::ToolbarEditor( QWidget* parent, Qt::WindowFlags f )
	: QDialog(parent, f)
{
	setupUi(this);

	up_button->setIcon(Images::icon("up"));
	down_button->setIcon(Images::icon("down"));

	QTransform transform;
	transform.rotate(90);

	right_button->setIcon( Images::icon("up").transformed(transform) );
	left_button->setIcon( Images::icon("down").transformed(transform) );

	QPushButton * restore = buttonBox->button(QDialogButtonBox::RestoreDefaults);
	connect(restore, SIGNAL(clicked()), this, SLOT(restoreDefaults()));

	connect(all_actions_list, SIGNAL(currentRowChanged(int)),
            this, SLOT(checkRowsAllList(int)));
	connect(active_actions_list, SIGNAL(currentRowChanged(int)),
            this, SLOT(checkRowsActiveList(int)));

#if QT_VERSION >= 0x040600
	all_actions_list->setSelectionMode(QAbstractItemView::SingleSelection);
	all_actions_list->setDragEnabled(true);
	all_actions_list->viewport()->setAcceptDrops(true);
	all_actions_list->setDropIndicatorShown(true);
	all_actions_list->setDefaultDropAction(Qt::MoveAction); // Qt 4.6
	//all_actions_list->setDragDropMode(QAbstractItemView::InternalMove);

	active_actions_list->setSelectionMode(QAbstractItemView::SingleSelection);
	active_actions_list->setDragEnabled(true);
	active_actions_list->viewport()->setAcceptDrops(true);
	active_actions_list->setDropIndicatorShown(true);
	active_actions_list->setDefaultDropAction(Qt::MoveAction); // Qt 4.6
	//active_actions_list->setDragDropMode(QAbstractItemView::InternalMove);
#endif

	// Phase G fork patch: per-button override controls — direct buttons,
	// no in-between dialog. Each opens its own targeted editor (icon
	// picker, text input, or reset).
	change_icon_button = new QPushButton(tr("Change Icon…"), this);
	change_icon_button->setToolTip(tr("Pick a different icon for the selected button"));
	change_icon_button->setEnabled(false);
	connect(change_icon_button, SIGNAL(clicked()), this, SLOT(onChangeIconClicked()));

	change_text_button = new QPushButton(tr("Change Text…"), this);
	change_text_button->setToolTip(tr("Override the displayed text of the selected button"));
	change_text_button->setEnabled(false);
	connect(change_text_button, SIGNAL(clicked()), this, SLOT(onChangeTextClicked()));

	reset_overrides_button = new QPushButton(tr("Reset Button"), this);
	reset_overrides_button->setToolTip(tr("Restore the selected button's default icon and text"));
	reset_overrides_button->setEnabled(false);
	connect(reset_overrides_button, SIGNAL(clicked()), this, SLOT(onResetOverridesClicked()));

	icon_only_checkbox = new QCheckBox(tr("Icon Only (fallback to text for buttons without icons)"), this);
	icon_only_checkbox->setChecked(false);

	horizontalLayout->addWidget(change_icon_button);
	horizontalLayout->addWidget(change_text_button);
	horizontalLayout->addWidget(reset_overrides_button);
	horizontalLayout->addWidget(icon_only_checkbox);
}

ToolbarEditor::~ToolbarEditor() {
}

void ToolbarEditor::setIconSize(int size) {
	iconsize_spin->setValue(size);
}

int ToolbarEditor::iconSize() {
	return iconsize_spin->value();
}

void ToolbarEditor::populateList(QListWidget * w, QList<QAction *> actions_list, bool add_separators) {
	w->clear();

	QAction * action;
	for (int n = 0; n < actions_list.count(); n++) {
		action = static_cast<QAction*> (actions_list[n]);
		if (action) {
			if (!action->objectName().isEmpty()) {
				QListWidgetItem * i = new QListWidgetItem;
				QString text = fixname(action->text(), action->objectName());
				i->setText(text + " ("+ action->objectName() +")");
				QIcon icon = action->icon();
				if (icon.isNull()) {
					icon = Images::icon("empty_icon");
				}
				i->setIcon(icon);
				i->setData(Qt::UserRole, action->objectName());
				w->addItem(i);
			}
			else
			if ((action->isSeparator()) && (add_separators)) {
				QListWidgetItem * i = new QListWidgetItem;
				//i->setText(tr("(separator)"));
				i->setText("---------");
				i->setData(Qt::UserRole, "separator");
				i->setIcon(Images::icon("empty_icon"));
				w->addItem(i);
			}
		}
	}
}

void ToolbarEditor::setAllActions(QList<QAction *> actions_list) {
	populateList(all_actions_list, actions_list, false);
	all_actions_copy = actions_list;
}

void ToolbarEditor::setActiveActions(QList<QAction *> actions_list) {
	populateList(active_actions_list, actions_list, true);

	// Delete actions from the "all list" which are in the active list
	for (int n = 0; n < active_actions_list->count(); n++) {
		int row = findItem( active_actions_list->item(n)->data(Qt::UserRole).toString(), all_actions_list );
		if (row > -1) {
			qDebug("found: %s", active_actions_list->item(n)->data(Qt::UserRole).toString().toUtf8().constData());
			all_actions_list->takeItem(row);
		}
	}
}

int ToolbarEditor::findItem(const QString & action_name, QListWidget * w) {
	for (int n = 0; n < w->count(); n++) {
		if (w->item(n)->data(Qt::UserRole).toString() == action_name) {
			return n;
		}
	}
	return -1;
}

void ToolbarEditor::on_up_button_clicked() {
	int row = active_actions_list->currentRow();
	qDebug("ToolbarEditor::on_up_button_clicked: current_row: %d", row);

	if (row == 0) return;

	QListWidgetItem * current = active_actions_list->takeItem(row);
	active_actions_list->insertItem(row-1, current);
	active_actions_list->setCurrentRow(row-1);
}

void ToolbarEditor::on_down_button_clicked() {
	int row = active_actions_list->currentRow();
	qDebug("ToolbarEditor::on_down_button_clicked: current_row: %d", row);

	if ((row+1) >= active_actions_list->count()) return;

	QListWidgetItem * current = active_actions_list->takeItem(row);
	active_actions_list->insertItem(row+1, current);
	active_actions_list->setCurrentRow(row+1);
}

void ToolbarEditor::on_right_button_clicked() {
	int row = all_actions_list->currentRow();
	qDebug("ToolbarEditor::on_right_button_clicked: current_row: %d", row);

	if (row > -1) {
		QListWidgetItem * current = all_actions_list->takeItem(row);
		int dest_row = active_actions_list->currentRow();
		if (dest_row > -1) {
			active_actions_list->insertItem(dest_row+1, current);
		} else {
			active_actions_list->addItem(current);
		}
	}
}

void ToolbarEditor::on_left_button_clicked() {
	int row = active_actions_list->currentRow();
	qDebug("ToolbarEditor::on_left_button_clicked: current_row: %d", row);

	if (row > -1) {
		QListWidgetItem * current = active_actions_list->takeItem(row);
		if (current->data(Qt::UserRole).toString() != "separator") {
			int dest_row = all_actions_list->currentRow();
			if (dest_row > -1) {
				all_actions_list->insertItem(dest_row+1, current);
			} else {
				all_actions_list->addItem(current);
			}
		}
	}
}

void ToolbarEditor::on_separator_button_clicked() {
	qDebug("ToolbarEditor::on_separator_button_clicked");

	QListWidgetItem * i = new QListWidgetItem;
	//i->setText(tr("(separator)"));
	i->setText("---------");
	i->setData(Qt::UserRole, "separator");
	i->setIcon(Images::icon("empty_icon"));

	int row = active_actions_list->currentRow();
	if (row > -1) {
		active_actions_list->insertItem(row+1, i);
	} else {
		active_actions_list->addItem(i);
	}
}

void ToolbarEditor::restoreDefaults() {
	qDebug("ToolbarEditor::restoreDefaults");
	populateList(all_actions_list, all_actions_copy, false);

	// Create list of actions
	QList<QAction *> actions;
	QAction * a = 0;
	for (int n = 0; n < default_actions.count(); n++) {
		if (default_actions[n] == "separator") {
			QAction * sep = new QAction(this);
			sep->setSeparator(true);
			actions.push_back(sep);
		} else {
			a = findAction(default_actions[n], all_actions_copy);
			if (a) actions.push_back(a);
		}
	}
	setActiveActions(actions);
}

QStringList ToolbarEditor::activeActionsToStringList() {
	QStringList o;
	for (int n = 0; n < active_actions_list->count(); n++) {
		o << active_actions_list->item(n)->data(Qt::UserRole).toString();
	}
	return o;
}

void ToolbarEditor::checkRowsAllList(int currentRow) {
	qDebug("ToolbarEditor::checkRowsAllList: current row: %d", currentRow);
	right_button->setEnabled(currentRow > -1);
}

void ToolbarEditor::checkRowsActiveList(int currentRow) {
	qDebug("ToolbarEditor::checkRowsActiveList: current row: %d", currentRow);
	left_button->setEnabled(currentRow > -1);
	bool customize_ok = false;
	if (currentRow == -1) {
		up_button->setEnabled(false);
		down_button->setEnabled(false);
	} else {
		up_button->setEnabled((currentRow > 0));
		down_button->setEnabled((currentRow < active_actions_list->count()-1));
		const QString name = active_actions_list->item(currentRow)->data(Qt::UserRole).toString();
		customize_ok = (name != "separator" && !name.isEmpty());
	}
	change_icon_button->setEnabled(customize_ok);
	change_text_button->setEnabled(customize_ok);
	reset_overrides_button->setEnabled(customize_ok);
}

void ToolbarEditor::setIconOnlyMode(bool b)  { icon_only_checkbox->setChecked(b); }
bool ToolbarEditor::iconOnlyMode() const     { return icon_only_checkbox->isChecked(); }

static QAction * resolveSelectedAction(QListWidget * list, const QList<QAction*> & all)
{
	const int row = list->currentRow();
	if (row < 0) return 0;
	const QString name = list->item(row)->data(Qt::UserRole).toString();
	if (name.isEmpty() || name == "separator") return 0;
	for (QAction * a : all) if (a && a->objectName() == name) return a;
	return 0;
}

void ToolbarEditor::onChangeIconClicked() {
	QAction * action = resolveSelectedAction(active_actions_list, all_actions_copy);
	if (!action) return;

	// Build a hint from the action's identity so the picker can surface
	// likely-related icons at the top.
	QString hint = action->objectName();
	if (!action->text().isEmpty()) hint += " " + QString(action->text()).remove('&');

	ToolbarOverrides::Entry e = ToolbarOverrides::load(action->objectName());
	IconPickerDialog dlg(e.icon, hint, this);
	if (dlg.exec() == QDialog::Accepted) {
		const QString picked = dlg.selectedIcon();
		// Empty result = user clicked OK without picking; treat as a reset
		// of just the icon override.
		e.icon = picked;
		ToolbarOverrides::save(action->objectName(), e);
		ToolbarOverrides::applyToAction(action);
		refreshActiveListLabels();
	}
}

void ToolbarEditor::onChangeTextClicked() {
	QAction * action = resolveSelectedAction(active_actions_list, all_actions_copy);
	if (!action) return;

	ToolbarOverrides::Entry e = ToolbarOverrides::load(action->objectName());
	QString current = e.text;
	if (current.isEmpty()) {
		const QVariant orig = action->property("_smp_orig_text");
		current = orig.isValid() ? orig.toString() : action->text();
		current.remove('&');
	}

	bool ok = false;
	const QString new_text = QInputDialog::getText(
		this,
		tr("Change button text"),
		tr("Display text for <b>%1</b>:").arg(action->objectName()),
		QLineEdit::Normal, current, &ok);
	if (!ok) return;

	e.text = new_text.trimmed();
	ToolbarOverrides::save(action->objectName(), e);
	ToolbarOverrides::applyToAction(action);
	refreshActiveListLabels();
}

void ToolbarEditor::onResetOverridesClicked() {
	QAction * action = resolveSelectedAction(active_actions_list, all_actions_copy);
	if (!action) return;
	ToolbarOverrides::clear(action->objectName());
	ToolbarOverrides::applyToAction(action);
	refreshActiveListLabels();
}

void ToolbarEditor::refreshActiveListLabels() {
	for (int i = 0; i < active_actions_list->count(); ++i) {
		QListWidgetItem * item = active_actions_list->item(i);
		const QString name = item->data(Qt::UserRole).toString();
		if (name == "separator" || name.isEmpty()) continue;
		QAction * a = findAction(name, all_actions_copy);
		if (!a) continue;
		const QString text = fixname(a->text(), a->objectName());
		item->setText(text + " (" + a->objectName() + ")");
		QIcon ic = a->icon();
		if (ic.isNull()) ic = Images::icon("empty_icon");
		item->setIcon(ic);
	}
}

QString ToolbarEditor::fixname(const QString & name, const QString & action_name) {
	QString s = name;
	s = s.replace("&", "");
	if (action_name == "timeslider_action") s = tr("Time slider");
	else
	if (action_name == "volumeslider_action") s = tr("Volume slider");
	else
	if (action_name == "timelabel_action") s = tr("Display time");
	else
	if (action_name == "current_timelabel_action") s = tr("Current time");
	else
	if (action_name == "total_timelabel_action") s = tr("Total time");
	else
	if (action_name == "remaining_timelabel_action") s = tr("Remaining time");
	else
	if (action_name == "rewindbutton_action") s = tr("3 in 1 rewind");
	else
	if (action_name == "forwardbutton_action") s = tr("3 in 1 forward");
	else
	if (action_name == "quick_access_menu") s = tr("Quick access menu");
	return s;
}

QStringList ToolbarEditor::save(QWidget * w) {
	qDebug("ToolbarEditor::save: '%s'", w->objectName().toUtf8().data());

	QList<QAction *> list = w->actions();
	QStringList o;
	QAction * action;

	for (int n = 0; n < list.count(); n++) {
		action = static_cast<QAction*> (list[n]);
		if (action->isSeparator()) {
			o << "separator";
		}
		else
		if (!action->objectName().isEmpty()) {
			o << action->objectName();
		}
		else
		qWarning("ToolbarEditor::save: unknown action at pos %d", n);
	}

	return o;
}

void ToolbarEditor::load(QWidget *w, QStringList l, QList<QAction *> actions_list)
{
	qDebug("ToolbarEditor::load: '%s'", w->objectName().toUtf8().data());

	QAction * action;

	for (int n = 0; n < l.count(); n++) {
		qDebug("ToolbarEditor::load: loading action %s", l[n].toUtf8().data());

		if (l[n] == "separator") {
			qDebug("ToolbarEditor::load: adding separator");
			QAction * sep = new QAction(w);
			sep->setSeparator(true);
			w->addAction(sep);
		} else {
			action = findAction(l[n], actions_list);
			if (action) {
				w->addAction(action);
				if (action->objectName().endsWith("_menu")) {
					// If the action is a menu and is in a toolbar, as a toolbutton, change some of its properties
					QToolBar * toolbar = qobject_cast<QToolBar *>(w);
					if (toolbar) {
						QToolButton * button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
						if (button) {
							//qDebug("ToolbarEditor::load: action %s is a toolbutton", action->objectName().toUtf8().constData());
							button->setPopupMode(QToolButton::InstantPopup);
							//button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
						}
					}
				}
			} else {
				qWarning("ToolbarEditor::load: action %s not found", l[n].toUtf8().data());
			}
		}
	}
}

QAction * ToolbarEditor::findAction(QString s, QList<QAction *> actions_list) {
	QAction * action;

	for (int n = 0; n < actions_list.count(); n++) {
		action = static_cast<QAction*> (actions_list[n]);
		if (action->objectName() == s) return action;
	}

	return 0;
}

#include "moc_toolbareditor.cpp"
