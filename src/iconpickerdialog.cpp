#include "iconpickerdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QRegExp>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QVBoxLayout>

// Roles on the icon items.
static const int RoleIconName = Qt::UserRole + 1;
static const int RoleCategory = Qt::UserRole + 2;
static const int RoleIconPath = Qt::UserRole + 3;

// Lazy-rendering delegate. Only resolves QIcon::fromTheme for the cells
// that actually paint, instead of populating 4000+ pixmap engines at
// dialog construction time. Costs a `fromTheme` call per *visible* cell
// scroll position (Qt internally caches them after first load anyway).
class IconCellDelegate : public QStyledItemDelegate
{
public:
	IconCellDelegate(QObject * parent = 0) : QStyledItemDelegate(parent) {}

	void paint(QPainter * p, const QStyleOptionViewItem & option,
	           const QModelIndex & idx) const Q_DECL_OVERRIDE
	{
		// Paint background + selection without text/icon, then we draw both.
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, idx);
		opt.text.clear();
		opt.icon = QIcon();
		opt.features &= ~QStyleOptionViewItem::HasDecoration;
		const QWidget * widget = option.widget;
		QStyle * style = widget ? widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, p, widget);

		const QString name = idx.data(RoleIconName).toString();
		const QString path = idx.data(RoleIconPath).toString();
		const QRect cell = option.rect;
		const QSize iconSize(48, 48);
		const int iconX = cell.x() + (cell.width()  - iconSize.width())  / 2;
		const int iconY = cell.y() + 4;

		// Load directly from the file we found during scanThemes — bypasses
		// the active theme's index.theme registration which excludes a lot
		// of valid icon files (the "half of icons missing" bug). QIcon
		// auto-handles SVG via Qt's SVG plugin and PNG/XPM natively.
		QPixmap pm;
		if (!path.isEmpty()) {
			QIcon ic(path);
			if (!ic.isNull()) pm = ic.pixmap(iconSize);
		}
		// Fall back to fromTheme if for whatever reason the path failed.
		if (pm.isNull()) {
			QIcon ic = QIcon::fromTheme(name);
			if (!ic.isNull()) pm = ic.pixmap(iconSize);
		}
		if (!pm.isNull()) {
			const int dx = (iconSize.width()  - pm.width())  / 2;
			const int dy = (iconSize.height() - pm.height()) / 2;
			p->drawPixmap(QPoint(iconX + dx, iconY + dy), pm);
		}

		// Name below the icon.
		const int textY = iconY + iconSize.height() + 2;
		const QRect textRect(cell.x() + 2, textY,
		                     cell.width() - 4,
		                     cell.height() - (textY - cell.y()) - 2);
		const bool selected = (option.state & QStyle::State_Selected);
		p->save();
		p->setPen(option.palette.color(selected
			? QPalette::HighlightedText : QPalette::Text));
		const QString elided = p->fontMetrics().elidedText(
			name, Qt::ElideMiddle, textRect.width());
		p->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
		            elided);
		p->restore();
	}

	QSize sizeHint(const QStyleOptionViewItem & /*opt*/,
	               const QModelIndex & /*idx*/) const Q_DECL_OVERRIDE
	{
		// Match the grid the dialog set on the view.
		return QSize(96, 80);
	}
};

IconPickerDialog::IconPickerDialog(const QString & initial,
                                   const QString & hint,
                                   QWidget * parent)
	: QDialog(parent)
	, m_hint(hint)
{
	setWindowTitle(tr("Choose icon"));
	resize(720, 520);

	// --- top row: search + category filter ---
	m_search_edit = new QLineEdit();
	m_search_edit->setPlaceholderText(tr("Search icons…"));
	m_search_edit->setClearButtonEnabled(true);

	m_category_combo = new QComboBox();
	m_category_combo->addItem(tr("All categories"), QString());
	// Common icon-theme categories per the freedesktop spec — we add the
	// stock set up front, then top up with anything else we encounter
	// during the scan that isn't in this list.
	const char * stock_cats[] = {
		"actions", "applications", "apps", "categories", "devices",
		"emblems", "emotes", "mimetypes", "places", "status",
		"animations", "intl", "stock"
	};
	for (size_t i = 0; i < sizeof(stock_cats)/sizeof(stock_cats[0]); ++i)
		m_category_combo->addItem(QLatin1String(stock_cats[i]), QString::fromLatin1(stock_cats[i]));

	QHBoxLayout * top_row = new QHBoxLayout();
	top_row->addWidget(new QLabel(tr("Filter:")));
	top_row->addWidget(m_search_edit, 1);
	top_row->addWidget(new QLabel(tr("Category:")));
	top_row->addWidget(m_category_combo);

	// --- main grid view ---
	m_model = new QStandardItemModel(this);
	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_proxy->setFilterRole(RoleIconName);

	m_view = new QListView();
	m_view->setModel(m_proxy);
	m_view->setViewMode(QListView::IconMode);
	m_view->setIconSize(QSize(48, 48));
	m_view->setGridSize(QSize(96, 80));
	m_view->setMovement(QListView::Static);
	m_view->setResizeMode(QListView::Adjust);
	m_view->setUniformItemSizes(true);
	m_view->setWrapping(true);
	m_view->setSelectionMode(QAbstractItemView::SingleSelection);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_view->setWordWrap(true);
	// Phase G fork patch: the user's system theme paints item backgrounds
	// as #272727 which fights against partially-transparent icons. Force
	// the listview's item-background to black so icons show cleanly. We
	// keep a visible selection highlight via QPalette::Highlight.
	m_view->setStyleSheet(
		"QListView { background-color: #000000; }"
		"QListView::item { background-color: #000000; }"
		"QListView::item:selected { background-color: palette(highlight); "
		"color: palette(highlighted-text); }");
	// Lazy icon paint — see IconCellDelegate at top of this file.
	m_view->setItemDelegate(new IconCellDelegate(m_view));

	// --- bottom row: preview + name readout + browse + status ---
	m_preview_label = new QLabel();
	m_preview_label->setMinimumSize(48, 48);
	m_preview_label->setMaximumSize(48, 48);
	m_preview_label->setAlignment(Qt::AlignCenter);
	m_preview_label->setFrameShape(QFrame::StyledPanel);

	m_status_label = new QLabel();
	m_status_label->setText(tr("(none selected)"));
	m_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

	QPushButton * browse_btn = new QPushButton(tr("Browse File…"));
	browse_btn->setToolTip(tr("Pick any image file on disk"));

	QHBoxLayout * bottom_row = new QHBoxLayout();
	bottom_row->addWidget(m_preview_label);
	bottom_row->addWidget(m_status_label, 1);
	bottom_row->addWidget(browse_btn);

	// --- buttons ---
	QDialogButtonBox * bb = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	// --- assemble ---
	QVBoxLayout * outer = new QVBoxLayout(this);
	outer->addLayout(top_row);
	outer->addWidget(m_view, 1);
	outer->addLayout(bottom_row);
	outer->addWidget(bb);

	// --- wiring ---
	connect(m_search_edit, SIGNAL(textChanged(const QString &)),
	        this, SLOT(onSearchChanged(const QString &)));
	connect(m_category_combo, SIGNAL(currentIndexChanged(int)),
	        this, SLOT(onCategoryChanged(int)));
	connect(m_view->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)),
	        this, SLOT(onSelectionChanged()));
	connect(m_view, SIGNAL(activated(const QModelIndex &)),
	        this, SLOT(onItemActivated(const QModelIndex &)));
	connect(browse_btn, SIGNAL(clicked()), this, SLOT(onBrowseFile()));
	connect(bb, SIGNAL(accepted()), this, SLOT(accept()));
	connect(bb, SIGNAL(rejected()), this, SLOT(reject()));

	// Populate.
	scanThemes();
	populateModel();

	// If the caller passed an initial value, pre-select it (when it's a
	// theme name we can find) or just stash it for "Cancel" to preserve.
	if (!initial.isEmpty()) {
		m_selected = initial;
		m_status_label->setText(initial);
		QIcon ic = (initial.startsWith('/') || initial.startsWith("file://"))
			? QIcon(initial)
			: QIcon::fromTheme(initial);
		if (!ic.isNull()) m_preview_label->setPixmap(ic.pixmap(48, 48));

		// Try to scroll to the item matching this name in the grid.
		for (int r = 0; r < m_proxy->rowCount(); ++r) {
			const QModelIndex idx = m_proxy->index(r, 0);
			if (idx.data(RoleIconName).toString() == initial) {
				m_view->setCurrentIndex(idx);
				m_view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
				break;
			}
		}
	}
}

// Score a candidate icon file higher = better. Vector beats any raster size.
static int scoreIconPath(const QFileInfo & fi, const QStringList & path_parts)
{
	const QString suffix = fi.suffix().toLower();
	if (suffix == "svg" || suffix == "svgz") return 100000;
	// Look for a NxN size dir or "scalable" in the path.
	for (const QString & part : path_parts) {
		if (part.compare("scalable", Qt::CaseInsensitive) == 0) return 99000;
		const int x = part.indexOf('x');
		if (x > 0) {
			bool ok = false;
			int sz = part.left(x).toInt(&ok);
			if (ok && sz > 0) return sz;  // raster: score = pixel size
		}
	}
	return 1;  // unknown structure
}

void IconPickerDialog::scanThemes()
{
	QHash<QString, int> best_score;

	QStringList theme_paths = QIcon::themeSearchPaths();
	// Top up with common Linux paths in case Qt didn't pick them up.
	const QStringList extras = {
		QStringLiteral("/usr/share/icons"),
		QStringLiteral("/usr/local/share/icons"),
		QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/icons",
		QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.icons",
	};
	for (const QString & e : extras)
		if (!theme_paths.contains(e) && QDir(e).exists()) theme_paths << e;

	// Themes to scan: the active theme, plus hicolor (the freedesktop
	// fallback that nearly every theme inherits).
	QStringList theme_names;
	if (!QIcon::themeName().isEmpty())
		theme_names << QIcon::themeName();
	if (!QIcon::fallbackThemeName().isEmpty()
	    && !theme_names.contains(QIcon::fallbackThemeName()))
		theme_names << QIcon::fallbackThemeName();
	if (!theme_names.contains("hicolor")) theme_names << "hicolor";

	for (const QString & base : theme_paths) {
		for (const QString & theme : theme_names) {
			const QString theme_dir = base + "/" + theme;
			if (!QDir(theme_dir).exists()) continue;

			QDirIterator it(theme_dir,
				QStringList() << "*.png" << "*.svg" << "*.svgz" << "*.xpm",
				QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext()) {
				it.next();
				const QFileInfo fi(it.filePath());
				const QString name = fi.completeBaseName();
				if (name.isEmpty()) continue;

				// Category = the immediate parent of the size dir, e.g.
				// /usr/share/icons/<theme>/scalable/actions/foo.svg → "actions".
				const QString relative = fi.absolutePath().mid(theme_dir.length() + 1);
				const QStringList parts = relative.split('/', Qt::SkipEmptyParts);
				if (!parts.isEmpty() && !m_icon_category.contains(name)) {
					m_icon_category[name] = parts.last();
				}

				// Track the best (highest-scoring) file we've seen for this
				// name. This dedupes across themes/sizes while ensuring we
				// remember an actual usable file path — bypasses the broken
				// theme-registration lookup.
				const int sc = scoreIconPath(fi, parts);
				if (sc > best_score.value(name, -1)) {
					best_score[name] = sc;
					m_icon_path[name] = it.filePath();
				}
			}
		}
	}

	m_all_icons = m_icon_path.keys();
	m_all_icons.sort(Qt::CaseInsensitive);

	// Reorder so icons whose name contains any token from the caller's
	// hint appear first (in their original alpha order). Tokens are
	// derived by splitting the hint on word separators. Skip tokens that
	// are too short — "to" / "a" would match everything.
	if (!m_hint.isEmpty()) {
		QString h = m_hint.toLower();
		h.replace(QRegExp("[^a-z0-9]+"), " ");
		const QStringList raw_tokens = h.split(' ', Qt::SkipEmptyParts);
		QStringList tokens;
		for (const QString & t : raw_tokens) if (t.length() >= 3) tokens << t;

		if (!tokens.isEmpty()) {
			QStringList matched, unmatched;
			for (const QString & name : m_all_icons) {
				const QString lname = name.toLower();
				bool hit = false;
				for (const QString & t : tokens) {
					if (lname.contains(t)) { hit = true; break; }
				}
				if (hit) matched << name; else unmatched << name;
			}
			m_all_icons = matched + unmatched;
		}
	}

	// Add any extra categories we found that weren't in the stock list.
	QSet<QString> existing_cats;
	for (int i = 0; i < m_category_combo->count(); ++i)
		existing_cats.insert(m_category_combo->itemData(i).toString());
	QSet<QString> seen_cats;
	for (auto it = m_icon_category.constBegin(); it != m_icon_category.constEnd(); ++it)
		seen_cats.insert(it.value());
	for (const QString & cat : seen_cats) {
		if (!existing_cats.contains(cat))
			m_category_combo->addItem(cat, cat);
	}
}

void IconPickerDialog::populateModel()
{
	m_model->clear();
	for (const QString & name : m_all_icons) {
		QStandardItem * item = new QStandardItem();
		item->setText(QString());        // delegate draws name itself
		item->setToolTip(name);
		item->setData(name, RoleIconName);
		item->setData(m_icon_category.value(name), RoleCategory);
		item->setData(m_icon_path.value(name),     RoleIconPath);
		m_model->appendRow(item);
	}
	m_status_label->setText(tr("%1 icons available").arg(m_model->rowCount()));
}

void IconPickerDialog::onSearchChanged(const QString & text)
{
	m_proxy->setFilterFixedString(text);
}

void IconPickerDialog::onCategoryChanged(int idx)
{
	const QString cat = m_category_combo->itemData(idx).toString();
	if (cat.isEmpty()) {
		// "All" — repopulate the source model from the full icon list.
		populateModel();
		m_proxy->setFilterFixedString(m_search_edit->text());
	} else {
		m_model->clear();
		for (const QString & name : m_all_icons) {
			if (m_icon_category.value(name) != cat) continue;
			QStandardItem * item = new QStandardItem();
			item->setText(QString());
			item->setToolTip(name);
			item->setData(name, RoleIconName);
			item->setData(cat,  RoleCategory);
			item->setData(m_icon_path.value(name), RoleIconPath);
			m_model->appendRow(item);
		}
		m_status_label->setText(tr("%1 icons in %2").arg(m_model->rowCount()).arg(cat));
		m_proxy->setFilterFixedString(m_search_edit->text());
	}
}

void IconPickerDialog::onSelectionChanged()
{
	const QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
	if (sel.isEmpty()) return;
	const QString name = sel.first().data(RoleIconName).toString();
	const QString path = sel.first().data(RoleIconPath).toString();
	m_selected = name;
	m_status_label->setText(name);
	QPixmap pm;
	if (!path.isEmpty()) {
		QIcon ic(path);
		if (!ic.isNull()) pm = ic.pixmap(48, 48);
	}
	if (pm.isNull()) {
		QIcon ic = QIcon::fromTheme(name);
		if (!ic.isNull()) pm = ic.pixmap(48, 48);
	}
	m_preview_label->setPixmap(pm);
}

void IconPickerDialog::onItemActivated(const QModelIndex & idx)
{
	if (!idx.isValid()) return;
	m_selected = idx.data(RoleIconName).toString();
	accept();
}

void IconPickerDialog::onBrowseFile()
{
	const QString picked = QFileDialog::getOpenFileName(
		this, tr("Choose icon file"), QString(),
		tr("Image files (*.png *.svg *.svgz *.xpm *.jpg *.jpeg);;All files (*)"));
	if (picked.isEmpty()) return;
	m_selected = picked;
	accept();
}

void IconPickerDialog::accept()
{
	// If selectedIcon stayed empty (user clicked OK with nothing chosen),
	// just leave m_selected as-is and let the caller see an empty string.
	QDialog::accept();
}
