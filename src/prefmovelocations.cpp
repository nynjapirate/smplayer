#include "prefmovelocations.h"
#include "custommove.h"
#include "filedialog.h"
#include "images.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalMapper>
#include <QVBoxLayout>

PrefMoveLocations::PrefMoveLocations(QWidget * parent, Qt::WindowFlags f)
	: PrefWidget(parent, f)
{
	m_browse_mapper = new QSignalMapper(this);
	connect(m_browse_mapper, SIGNAL(mapped(int)), this, SLOT(onBrowse(int)));

	QGridLayout * grid = new QGridLayout();
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(8);

	// Header row.
	grid->addWidget(new QLabel(tr("Slot")),   0, 0);
	grid->addWidget(new QLabel(tr("Folder")), 0, 1);
	grid->addWidget(new QLabel(tr("Label")),  0, 3);

	for (int i = 0; i < CustomMoveLocations::kNumSlots; ++i) {
		QLabel * num = new QLabel(QString::number(i + 1) + ".");
		grid->addWidget(num, i + 1, 0);

		m_path_edits[i] = new QLineEdit();
		m_path_edits[i]->setPlaceholderText(tr("(unconfigured)"));
		grid->addWidget(m_path_edits[i], i + 1, 1);

		m_browse_btns[i] = new QPushButton(tr("…"));
		m_browse_btns[i]->setMaximumWidth(36);
		grid->addWidget(m_browse_btns[i], i + 1, 2);
		m_browse_mapper->setMapping(m_browse_btns[i], i);
		connect(m_browse_btns[i], SIGNAL(clicked()),
		        m_browse_mapper, SLOT(map()));

		m_label_edits[i] = new QLineEdit();
		m_label_edits[i]->setPlaceholderText(tr("e.g. 5 stars"));
		grid->addWidget(m_label_edits[i], i + 1, 3);
	}
	grid->setColumnStretch(1, 3);
	grid->setColumnStretch(3, 2);

	QGroupBox * group = new QGroupBox(tr("Destination folders"));
	group->setLayout(grid);

	QLabel * note = new QLabel(
		tr("<b>Tip:</b> assign keyboard shortcuts in "
		   "<i>Keyboard and mouse → Keyboard</i> by searching for "
		   "<tt>move_to_folder_1</tt> through "
		   "<tt>move_to_folder_%1</tt>, plus "
		   "<tt>delete_current_file</tt>.").arg(CustomMoveLocations::kNumSlots));
	note->setWordWrap(true);
	note->setTextFormat(Qt::RichText);

	m_create_shortcuts_cb = new QCheckBox(
		tr("Create shortcuts (symbolic links) instead of moving files"));
	m_create_shortcuts_cb->setToolTip(
		tr("When checked, the move shortcuts create a symlink in the "
		   "destination folder pointing back to the source file. The "
		   "original file stays in place and the playlist row is kept."));

	QVBoxLayout * outer = new QVBoxLayout(this);
	outer->addWidget(group);
	outer->addSpacing(8);
	outer->addWidget(m_create_shortcuts_cb);
	outer->addSpacing(4);
	outer->addWidget(note);
	outer->addStretch(1);

	retranslateStrings();
}

PrefMoveLocations::~PrefMoveLocations() {
}

QString PrefMoveLocations::sectionName() {
	return tr("Custom move locations");
}

QPixmap PrefMoveLocations::sectionIcon() {
	return Images::icon("pref_advanced", 22);  // reuse a generic icon
}

void PrefMoveLocations::retranslateStrings() {
	// Most strings come from constructor literals which Qt's tr() catches
	// at runtime; nothing else to do here.
}

void PrefMoveLocations::onBrowse(int slot_idx) {
	if (slot_idx < 0 || slot_idx >= CustomMoveLocations::kNumSlots) return;
	QString start = m_path_edits[slot_idx]->text();
	QString picked = MyFileDialog::getExistingDirectory(
		this, tr("Choose destination folder"), start);
	if (!picked.isEmpty()) m_path_edits[slot_idx]->setText(picked);
}

void PrefMoveLocations::setData(Preferences * /*pref*/) {
	// `slots` would clash with Qt's signals/slots macro — use a different name.
	QVector<CustomMoveLocations::Slot> entries;
	CustomMoveLocations::loadSlots(entries);
	for (int i = 0; i < CustomMoveLocations::kNumSlots; ++i) {
		m_path_edits[i]->setText(entries[i].path);
		m_label_edits[i]->setText(entries[i].label);
	}
	m_create_shortcuts_cb->setChecked(CustomMoveLocations::loadCreateShortcuts());
}

void PrefMoveLocations::getData(Preferences * /*pref*/) {
	QVector<CustomMoveLocations::Slot> entries(CustomMoveLocations::kNumSlots);
	for (int i = 0; i < CustomMoveLocations::kNumSlots; ++i) {
		entries[i].path  = m_path_edits[i]->text().trimmed();
		entries[i].label = m_label_edits[i]->text().trimmed();
	}
	CustomMoveLocations::saveSlots(entries);
	CustomMoveLocations::saveCreateShortcuts(m_create_shortcuts_cb->isChecked());
	if (CustomMoveLocations * cm = CustomMoveLocations::instance()) {
		cm->reloadFromConfig();
	}
}

void PrefMoveLocations::createHelp() {
	clearHelp();
	addSectionTitle(tr("Custom move locations"));
}

#include "moc_prefmovelocations.cpp"
