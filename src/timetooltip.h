/*  smplayer fork patch — Phase D: hover-thumbnail tooltip widget.
 *
 *  Frameless top-level QWidget that floats above the seek bar showing
 *  (optionally) a video thumbnail and (always) a HH:MM:SS time string.
 *  When no thumbnail has arrived yet, only the time line shows — same
 *  footprint as Qt's stock QToolTip.
 */

#ifndef SMP_TIMETOOLTIP_H
#define SMP_TIMETOOLTIP_H

#include <QWidget>
#include <QImage>
#include <QString>

class TimeTooltip : public QWidget
{
	Q_OBJECT
public:
	explicit TimeTooltip(QWidget * parent = 0);

	// Anchor: the global-coords point on the seekbar we want to point at.
	// The tooltip places itself centred on that x, just above pos.y.
	void setAnchor(const QPoint & globalPos);
	void setTimeText(const QString & text);
	void setThumbnail(const QImage & img);
	void clearThumbnail();

protected:
	void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE;
	void showEvent(QShowEvent *) Q_DECL_OVERRIDE;

private:
	void recomputeLayout();
	void reposition();

	QPoint  m_anchor;
	QString m_time_text;
	QImage  m_thumb;
	int     m_pad;
	int     m_text_h;
	int     m_total_w;
	int     m_total_h;
};

#endif
