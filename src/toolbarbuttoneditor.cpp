#include "toolbarbuttoneditor.h"
#include "toolbaroverrides.h"
#include "iconpickerdialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ToolbarButtonEditor::ToolbarButtonEditor(QAction * action, QWidget * parent)
	: QDialog(parent)
	, m_action(action)
{
	setWindowTitle(tr("Customize toolbar button"));
	setMinimumWidth(420);

	m_action_name = action ? action->objectName() : QString();
	// `m_default_text` is the action's text BEFORE any override was last
	// applied — ToolbarOverrides::applyToAction stashes that on the
	// `_smp_orig_text` property. If that's missing (action never went
	// through applyToAction), fall back to the current text.
	const QVariant orig_text = action ? action->property("_smp_orig_text") : QVariant();
	m_default_text = orig_text.isNull() ? (action ? action->text() : QString())
	                                    : orig_text.toString();

	const ToolbarOverrides::Entry e = m_action_name.isEmpty()
		? ToolbarOverrides::Entry()
		: ToolbarOverrides::load(m_action_name);

	m_preview_label = new QLabel();
	m_preview_label->setMinimumSize(48, 48);
	m_preview_label->setAlignment(Qt::AlignCenter);
	m_preview_label->setFrameShape(QFrame::StyledPanel);

	m_icon_edit = new QLineEdit();
	m_icon_edit->setPlaceholderText(tr("e.g. media-playback-start  or  /path/to/icon.png"));
	m_icon_edit->setText(e.icon);

	m_text_edit = new QLineEdit();
	m_text_edit->setPlaceholderText(m_default_text);
	m_text_edit->setText(e.text);

	m_choose_btn     = new QPushButton(tr("Choose…"));
	m_choose_btn->setToolTip(tr("Browse the system icon theme visually"));
	m_browse_btn     = new QPushButton(tr("File…"));
	m_browse_btn->setToolTip(tr("Pick an image file from disk"));
	m_reset_icon_btn = new QPushButton(tr("Reset"));
	m_reset_text_btn = new QPushButton(tr("Reset"));
	QPushButton * reset_all_btn = new QPushButton(tr("Reset both"));

	QHBoxLayout * icon_row = new QHBoxLayout();
	icon_row->addWidget(m_icon_edit, 1);
	icon_row->addWidget(m_choose_btn);
	icon_row->addWidget(m_browse_btn);
	icon_row->addWidget(m_reset_icon_btn);

	QHBoxLayout * text_row = new QHBoxLayout();
	text_row->addWidget(m_text_edit, 1);
	text_row->addWidget(m_reset_text_btn);

	QFormLayout * form = new QFormLayout();
	form->addRow(tr("Action:"),   new QLabel(m_action_name.isEmpty()
		? tr("(unnamed)") : m_action_name));
	form->addRow(tr("Preview:"),  m_preview_label);
	form->addRow(tr("Icon:"),     icon_row);
	form->addRow(tr("Text:"),     text_row);

	QLabel * note = new QLabel(
		tr("<i>Icon: type a theme icon name (uses your system icon theme — "
		   "currently \"%1\") or pick an image file. Leave blank to keep "
		   "the default.</i>").arg(QIcon::themeName().isEmpty()
			? tr("system default") : QIcon::themeName()));
	note->setWordWrap(true);
	note->setTextFormat(Qt::RichText);

	QDialogButtonBox * bb = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(bb, SIGNAL(accepted()), this, SLOT(accept()));
	connect(bb, SIGNAL(rejected()), this, SLOT(reject()));

	QHBoxLayout * reset_row = new QHBoxLayout();
	reset_row->addStretch(1);
	reset_row->addWidget(reset_all_btn);

	QVBoxLayout * outer = new QVBoxLayout(this);
	outer->addLayout(form);
	outer->addWidget(note);
	outer->addLayout(reset_row);
	outer->addWidget(bb);

	connect(m_icon_edit, SIGNAL(textChanged(const QString &)),
	        this, SLOT(onIconNameChanged(const QString &)));
	connect(m_choose_btn,     SIGNAL(clicked()), this, SLOT(onChooseIcon()));
	connect(m_browse_btn,     SIGNAL(clicked()), this, SLOT(onBrowseFile()));
	connect(m_reset_icon_btn, SIGNAL(clicked()), this, SLOT(onResetIcon()));
	connect(m_reset_text_btn, SIGNAL(clicked()), this, SLOT(onResetText()));
	connect(reset_all_btn,    SIGNAL(clicked()), this, SLOT(onResetAll()));

	refreshPreview();
}

void ToolbarButtonEditor::onIconNameChanged(const QString &) {
	refreshPreview();
}

void ToolbarButtonEditor::onBrowseFile() {
	const QString picked = QFileDialog::getOpenFileName(
		this, tr("Choose icon file"), m_icon_edit->text(),
		tr("Image files (*.png *.svg *.svgz *.xpm *.jpg *.jpeg);;All files (*)"));
	if (!picked.isEmpty()) m_icon_edit->setText(picked);
}

void ToolbarButtonEditor::onChooseIcon() {
	QString hint;
	if (!m_action_name.isEmpty()) hint = m_action_name + " " + m_default_text;
	IconPickerDialog dlg(m_icon_edit->text().trimmed(), hint, this);
	if (dlg.exec() == QDialog::Accepted) {
		const QString picked = dlg.selectedIcon();
		if (!picked.isEmpty()) m_icon_edit->setText(picked);
	}
}

void ToolbarButtonEditor::onResetIcon() {
	m_icon_edit->clear();
}

void ToolbarButtonEditor::onResetText() {
	m_text_edit->clear();
}

void ToolbarButtonEditor::onResetAll() {
	m_icon_edit->clear();
	m_text_edit->clear();
}

void ToolbarButtonEditor::refreshPreview() {
	const QString spec = m_icon_edit->text().trimmed();
	QIcon ic;
	if (spec.isEmpty()) {
		// Show the action's pre-override icon, if we have it.
		if (m_action) {
			QVariant orig = m_action->property("_smp_orig_icon");
			if (orig.isValid()) ic = qvariant_cast<QIcon>(orig);
			if (ic.isNull())    ic = m_action->icon();
		}
	} else {
		ic = ToolbarOverrides::resolveIcon(spec);
	}
	if (ic.isNull()) {
		m_preview_label->setPixmap(QPixmap());
		m_preview_label->setText(tr("(no icon)"));
	} else {
		m_preview_label->setPixmap(ic.pixmap(48, 48));
		m_preview_label->setText(QString());
	}
}

void ToolbarButtonEditor::accept() {
	if (!m_action_name.isEmpty()) {
		ToolbarOverrides::Entry e;
		e.icon = m_icon_edit->text().trimmed();
		e.text = m_text_edit->text().trimmed();
		ToolbarOverrides::save(m_action_name, e);
		ToolbarOverrides::applyToAction(m_action);
	}
	QDialog::accept();
}
