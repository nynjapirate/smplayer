/*  smplayer fork patch — Phase G: graphical icon picker.
 *
 *  Dolphin/KIconDialog-style picker. Walks the system icon-theme search
 *  paths, builds a flat de-duplicated list of available icon names, and
 *  presents them in an icon-mode QListView with search + category filter
 *  + a Browse File… escape hatch for arbitrary images on disk.
 *
 *  Returns either an icon-theme name (e.g. "media-playback-start") or an
 *  absolute filesystem path. Caller passes the result back to
 *  ToolbarOverrides via the same icon spec channel as the text field.
 */

#ifndef SMP_ICONPICKERDIALOG_H
#define SMP_ICONPICKERDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QHash>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStandardItemModel;

class IconPickerDialog : public QDialog
{
	Q_OBJECT
public:
	// `hint` is free-form text (typically action's objectName + text); the
	// picker tokenises it and pushes matching icons to the top of the grid.
	explicit IconPickerDialog(const QString & initial = QString(),
	                          const QString & hint = QString(),
	                          QWidget * parent = 0);

	QString selectedIcon() const { return m_selected; }

protected slots:
	void onSearchChanged(const QString &);
	void onCategoryChanged(int);
	void onSelectionChanged();
	void onItemActivated(const QModelIndex &);
	void onBrowseFile();
	void accept() Q_DECL_OVERRIDE;

private:
	void scanThemes();
	void populateModel();

	QString m_selected;
	QListView    * m_view;
	QStandardItemModel    * m_model;
	QSortFilterProxyModel * m_proxy;
	QLineEdit    * m_search_edit;
	QComboBox    * m_category_combo;
	QLabel       * m_preview_label;
	QLabel       * m_status_label;

	QStringList m_all_icons;                 // ordered: hint-matches first, alpha within
	QHash<QString, QString> m_icon_category; // icon name → category folder
	QHash<QString, QString> m_icon_path;     // icon name → best filesystem path
	QString m_hint;
};

#endif
