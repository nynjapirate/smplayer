#include "timetooltip.h"

#include <QPainter>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>

TimeTooltip::TimeTooltip(QWidget * parent)
	: QWidget(parent,
	          Qt::ToolTip | Qt::FramelessWindowHint |
	          Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint)
	, m_pad(6)
	, m_text_h(0)
	, m_total_w(0)
	, m_total_h(0)
{
	setAttribute(Qt::WA_ShowWithoutActivating);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_TranslucentBackground, false);
	setFocusPolicy(Qt::NoFocus);
	recomputeLayout();
}

void TimeTooltip::setAnchor(const QPoint & globalPos)
{
	m_anchor = globalPos;
	if (isVisible()) reposition();
}

void TimeTooltip::setTimeText(const QString & text)
{
	if (text == m_time_text) return;
	m_time_text = text;
	recomputeLayout();
	if (isVisible()) {
		reposition();
		update();
	}
}

void TimeTooltip::setThumbnail(const QImage & img)
{
	m_thumb = img;
	recomputeLayout();
	if (isVisible()) {
		reposition();
		update();
	}
}

void TimeTooltip::clearThumbnail()
{
	if (m_thumb.isNull()) return;
	m_thumb = QImage();
	recomputeLayout();
	if (isVisible()) {
		reposition();
		update();
	}
}

void TimeTooltip::recomputeLayout()
{
	const QFontMetrics fm(font());
	const int text_w = fm.horizontalAdvance(m_time_text.isEmpty() ? "00:00" : m_time_text);
	m_text_h = fm.height();

	int w = text_w;
	int h = m_text_h;
	if (!m_thumb.isNull()) {
		w = qMax(w, m_thumb.width());
		h = m_thumb.height() + m_pad + m_text_h;
	}
	m_total_w = w + 2 * m_pad;
	m_total_h = h + 2 * m_pad;
	resize(m_total_w, m_total_h);
}

void TimeTooltip::reposition()
{
	// Centre on anchor.x, sit above anchor.y with a small gap.
	const int gap = 8;
	int x = m_anchor.x() - m_total_w / 2;
	int y = m_anchor.y() - m_total_h - gap;

	// Keep on-screen.
	const QScreen * screen = QGuiApplication::screenAt(m_anchor);
	if (!screen) screen = QGuiApplication::primaryScreen();
	if (screen) {
		const QRect r = screen->geometry();
		if (x < r.left() + 4)              x = r.left() + 4;
		if (x + m_total_w > r.right() - 4) x = r.right() - m_total_w - 4;
		if (y < r.top() + 4)               y = r.top() + 4;
	}
	move(x, y);
}

void TimeTooltip::showEvent(QShowEvent * e)
{
	reposition();
	QWidget::showEvent(e);
}

void TimeTooltip::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	// Match Qt's tooltip palette so it integrates with the system theme.
	const QPalette pal = palette();
	const QColor bg = pal.color(QPalette::ToolTipBase);
	const QColor fg = pal.color(QPalette::ToolTipText);
	const QColor border = fg; // simple 1px outline

	const QRect rect(0, 0, m_total_w - 1, m_total_h - 1);
	p.setPen(border);
	p.setBrush(bg);
	p.drawRoundedRect(rect, 3, 3);

	int y = m_pad;
	if (!m_thumb.isNull()) {
		// Centre the thumbnail horizontally inside the inner rect.
		int x = (m_total_w - m_thumb.width()) / 2;
		p.drawImage(QPoint(x, y), m_thumb);
		y += m_thumb.height() + m_pad;
	}

	p.setPen(fg);
	const QRect text_rect(m_pad, y, m_total_w - 2 * m_pad, m_text_h);
	p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignVCenter, m_time_text);
}
