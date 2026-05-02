/*  smplayer fork patch — Phase C3: thumbnail-row delegate.
 *
 *  Drop-in replacement for the COL_NAME column delegate. When the global
 *  PlaylistThumbProvider is enabled, paints a 16:9 thumbnail to the left
 *  of the name text; otherwise falls through to default text rendering.
 *  Idempotent — leaving the provider disabled means this delegate behaves
 *  like the stock one.
 */

#ifndef SMP_PLAYLISTTHUMBDELEGATE_H
#define SMP_PLAYLISTTHUMBDELEGATE_H

#include <QStyledItemDelegate>
#include <QColor>

class PlaylistThumbDelegate : public QStyledItemDelegate
{
	Q_OBJECT
public:
	explicit PlaylistThumbDelegate(QObject * parent = 0);

	QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const Q_DECL_OVERRIDE;
	void  paint(QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const Q_DECL_OVERRIDE;

private:
	// Phase C2 fork patch: per-state row colours.
	// Read from [playlist_colors] in smplayer.ini at construction;
	// defaults chosen to be distinct under both light and dark themes.
	QColor m_color_playing;
	QColor m_color_selected;
	QColor m_color_playing_selected;
};

#endif
