#include "playlistthumbdelegate.h"
#include "playlistthumbprovider.h"
#include "paths.h"

#include <QApplication>
#include <QPainter>
#include <QFontMetrics>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QWidget>

// Keep in sync with playlist.cpp's #defines.
static const int COL_NAME     = 1;
static const int COL_FILENAME = 3;

// PLItem::Role_Current — duplicated to avoid pulling playlist.h here.
static const int kRoleCurrent = Qt::UserRole + 3;

static QString smpIniPathForDelegate()
{
	const QString d = Paths::configPath();
	if (!d.isEmpty()) return d + "/smplayer.ini";
	return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
	       + "/smplayer/smplayer.ini";
}

PlaylistThumbDelegate::PlaylistThumbDelegate(QObject * parent)
	: QStyledItemDelegate(parent)
{
	QSettings s(smpIniPathForDelegate(), QSettings::IniFormat);
	s.beginGroup("playlist_colors");
	m_color_playing          = QColor(s.value("playing",          "#ffd54f").toString());  // amber
	m_color_selected         = QColor(s.value("selected",         "#4fc3f7").toString());  // light blue
	m_color_playing_selected = QColor(s.value("playing_selected", "#ff7043").toString());  // deep orange
	s.endGroup();
}

QSize PlaylistThumbDelegate::sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
{
	QSize base = QStyledItemDelegate::sizeHint(option, index);
	PlaylistThumbProvider * tp = PlaylistThumbProvider::instance();
	if (!tp->enabled()) return base;
	// Reserve enough vertical space for the thumbnail with a small pad.
	const int h = tp->rowHeight() + 4;
	if (base.height() < h) base.setHeight(h);
	return base;
}

void PlaylistThumbDelegate::paint(QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
	const bool playing  = index.data(kRoleCurrent).toBool();
	const bool selected = (option.state & QStyle::State_Selected);

	// Pick the per-state colour. Invalid (default-constructed QColor) means
	// "use the theme's existing text colour" — i.e. unselected non-playing
	// rows get whatever default smplayer normally shows.
	QColor textColor;
	if      (playing && selected) textColor = m_color_playing_selected;
	else if (playing)             textColor = m_color_playing;
	else if (selected)            textColor = m_color_selected;

	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);
	if (textColor.isValid()) {
		// Override both Text (unselected) and HighlightedText (selected)
		// roles so QStyle::CE_ItemViewItem picks our colour regardless
		// of which state Qt's drawing code thinks the item is in.
		opt.palette.setBrush(QPalette::Text,            textColor);
		opt.palette.setBrush(QPalette::HighlightedText, textColor);
	}

	PlaylistThumbProvider * tp = PlaylistThumbProvider::instance();
	const bool with_thumb = (tp->enabled() && index.column() == COL_NAME);

	const QWidget * widget = option.widget;
	QStyle * style = widget ? widget->style() : QApplication::style();

	if (!with_thumb) {
		// Plain text path. We bypass QStyledItemDelegate::paint because it
		// would re-run initStyleOption and clobber our palette override.
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
		return;
	}

	// Thumbnail-row path. Paint background+selection with text cleared,
	// draw the thumbnail, then draw the name ourselves so the ordering
	// (thumbnail-then-text) is correct.
	const QString filename = index.model()
		? index.model()->index(index.row(), COL_FILENAME).data().toString()
		: QString();

	const int h = tp->rowHeight();
	const int w = h * 16 / 9;

	QStyleOptionViewItem bgOpt = opt;
	bgOpt.text.clear();
	style->drawControl(QStyle::CE_ItemViewItem, &bgOpt, painter, widget);

	const QRect cell = option.rect;
	const QRect thumbRect(cell.x() + 2,
	                      cell.y() + (cell.height() - h) / 2,
	                      w, h);

	QPixmap pm = tp->cached(filename, h);
	if (pm.isNull()) {
		tp->request(filename, h);
		painter->save();
		painter->fillRect(thumbRect, option.palette.color(QPalette::AlternateBase));
		painter->setPen(option.palette.color(QPalette::Mid));
		painter->drawRect(thumbRect.adjusted(0, 0, -1, -1));
		painter->restore();
	} else {
		const QPixmap scaled = pm.scaled(thumbRect.size(),
		                                 Qt::KeepAspectRatio,
		                                 Qt::SmoothTransformation);
		const int x = thumbRect.x() + (thumbRect.width()  - scaled.width())  / 2;
		const int y = thumbRect.y() + (thumbRect.height() - scaled.height()) / 2;
		painter->drawPixmap(QPoint(x, y), scaled);
	}

	// Custom text painting — picks our colour if set, else falls back to
	// the theme default for whichever selection state we're in.
	const QString name = index.data(Qt::DisplayRole).toString();
	QRect textRect = cell.adjusted(w + 8, 0, -2, 0);
	painter->save();
	if (textColor.isValid()) {
		painter->setPen(textColor);
	} else {
		painter->setPen(option.palette.color(
			selected ? QPalette::HighlightedText : QPalette::Text));
	}
	painter->setFont(opt.font);  // honours bold-when-current from PLItem::setCurrent
	const int flags = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine;
	const QString elided = painter->fontMetrics()
		.elidedText(name, Qt::ElideRight, textRect.width());
	painter->drawText(textRect, flags, elided);
	painter->restore();
}
