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

#include "timeslider.h"
#include "helper.h"
#include "global.h"
#include "preferences.h"
#include "timetooltip.h"
#include "thumbnailprovider.h"

#include <QCursor>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QTimer>
#include <QToolTip>
#include <QStyleOption>
#include <QStylePainter>
#include <QPaintEvent>
#include <QDebug>

using Global::pref;

#define DEBUG 0

TimeSlider::TimeSlider( QWidget * parent ) : MySlider(parent)
	, dont_update(false)
	, position(0)
	, start_drag_pos(-1)
	, slider_has_moved(false)
	, total_time(0)
	, thumb_tooltip(0)
	, last_hover_bucket_ms(-1)
{
	setMouseTracking(true);
	connect(ThumbnailProvider::instance(),
	        SIGNAL(thumbnailReady(qint64, QImage)),
	        this, SLOT(onThumbnailReady(qint64, QImage)));
	setMinimum(0);
#ifdef SEEKBAR_RESOLUTION
	setMaximum(SEEKBAR_RESOLUTION);
#else
	setMaximum(100);
#endif

	setFocusPolicy( Qt::NoFocus );
	setSizePolicy( QSizePolicy::Expanding , QSizePolicy::Fixed );

	connect( this, SIGNAL( sliderPressed() ), this, SLOT( sliderPressed_slot() ) );
	connect( this, SIGNAL( sliderReleased() ), this, SLOT( sliderReleased_slot() ) );
	connect( this, SIGNAL( valueChanged(int) ), this, SLOT( valueChanged_slot(int) ) );
#if ENABLE_DELAYED_DRAGGING
	connect( this, SIGNAL(draggingPos(int) ), this, SLOT(checkDragging(int)) );
	
	last_pos_to_send = -1;
	timer = new QTimer(this);
	connect( timer, SIGNAL(timeout()), this, SLOT(sendDelayedPos()) );
	timer->start(200);
#endif
}

TimeSlider::~TimeSlider() {
}

void TimeSlider::sliderPressed_slot() {
	#if DEBUG
	qDebug("TimeSlider::sliderPressed_slot");
	#endif
	dont_update = true;
	start_drag_pos = pos();
	slider_has_moved = false;
}

void TimeSlider::sliderReleased_slot() {
	#if DEBUG
	qDebug("TimeSlider::sliderReleased_slot");
	#endif
	dont_update = false;
	if (slider_has_moved) {
		// Only emit a video seek action when the slider actually
		// moved during mouse drag. Otherwise, on mouse release,
		// we would spuriously seek to the same position we are in,
		// causing seeker judder and video jitter during playback.
		if (!pref->update_while_seeking) {
			// It only makes sense to emit a video seek action when
			// "Seek to position when released" is active. When we
			// "Seek to position while dragging", this event is
			// spurious and should be skipped because the desired
			// video seeking was already done when the slider moved.
			emit posChanged( value() );
		}
	}
	start_drag_pos = -1;
	slider_has_moved = false;
}

void TimeSlider::valueChanged_slot(int v) {
	#if DEBUG
	qDebug("TimeSlider::changedValue_slot: %d", v);
	#endif

	// Only to make things clear:
	bool dragging = dont_update;
	if (!dragging) {
		if (v!=position) {
			#if DEBUG
			qDebug(" emitting posChanged");
			#endif
			emit posChanged(v);
		}
	} else {
		if ( start_drag_pos != -1 && v != start_drag_pos ) {
			slider_has_moved = true;
		}
		#if DEBUG
		qDebug(" emitting draggingPos");
		#endif
		emit draggingPos(v);
	}
}

#if ENABLE_DELAYED_DRAGGING
void TimeSlider::setDragDelay(int d) {
	qDebug("TimeSlider::setDragDelay: %d", d);
	timer->setInterval(d);
}

int TimeSlider::dragDelay() {
	return timer->interval();
}

void TimeSlider::checkDragging(int v) {
	qDebug("TimeSlider::checkDragging: %d", v);
	last_pos_to_send = v;
}

void TimeSlider::sendDelayedPos() {
	if (last_pos_to_send != -1) {
		qDebug("TimeSlider::sendDelayedPos: %d", last_pos_to_send);
		emit delayedDraggingPos(last_pos_to_send);
		last_pos_to_send = -1;
	}
}
#endif

void TimeSlider::setPos(int v) {
	#if DEBUG
	qDebug("TimeSlider::setPos: %d", v);
	qDebug(" dont_update: %d", dont_update);
	#endif

	if (v!=pos()) {
		if (!dont_update) {
			position = v;
			setValue(v);
		}
	}
}

int TimeSlider::pos() {
	return position;
}

void TimeSlider::wheelEvent(QWheelEvent * e) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
	qDebug("TimeSlider::wheelEvent: delta: %d", e->angleDelta().y());
	e->accept();
	if (e->angleDelta().y() >= 0) emit wheelUp(); else emit wheelDown();
#else
	qDebug("TimeSlider::wheelEvent: delta: %d", e->delta());
	e->accept();
	if (e->orientation() == Qt::Vertical) {
		if (e->delta() >= 0) emit wheelUp(); else emit wheelDown();
	}
#endif
}

bool TimeSlider::event(QEvent *event) {
	if (event->type() == QEvent::ToolTip) {
		// We handle the tooltip ourselves via mouseMoveEvent + thumb_tooltip,
		// so the stock tooltip stays out of the way. Returning true tells
		// Qt the event has been consumed.
		event->ignore();
		return true;
	}
	return QWidget::event(event);
}

void TimeSlider::leaveEvent(QEvent * event) {
	if (thumb_tooltip) thumb_tooltip->hide();
	last_hover_bucket_ms = -1;
	MySlider::leaveEvent(event);
}

void TimeSlider::mouseMoveEvent(QMouseEvent * event) {
	MySlider::mouseMoveEvent(event);
	if (total_time <= 0) return;
	updateHoverPreview(event->x());
}

QSize TimeSlider::sizeHint() const {
	QSize s = MySlider::sizeHint();
	s.setHeight(s.height() * 2);
	return s;
}

QSize TimeSlider::minimumSizeHint() const {
	QSize s = MySlider::minimumSizeHint();
	s.setHeight(s.height() * 2);
	return s;
}

QRect TimeSlider::naturalVisualRect() const {
	// The vertically centred sub-rect at the slider's natural height —
	// where we paint the groove + handle and where mouse-on-handle is
	// detected for click-to-seek.
	const int natural_h = MySlider::sizeHint().height();
	if (height() <= natural_h) return rect();
	const int top = (height() - natural_h) / 2;
	return QRect(0, top, width(), natural_h);
}

void TimeSlider::paintEvent(QPaintEvent * /*event*/) {
	QStylePainter p(this);
	QStyleOptionSlider opt;
	initStyleOption(&opt);

	// QSlider::paintEvent normally sets these; we must too — without them
	// the style draws nothing.
	opt.subControls = QStyle::SC_SliderGroove | QStyle::SC_SliderHandle;
	if (tickPosition() != NoTicks) opt.subControls |= QStyle::SC_SliderTickmarks;

	opt.rect = naturalVisualRect();
	p.drawComplexControl(QStyle::CC_Slider, opt);
}

void TimeSlider::mousePressEvent(QMouseEvent * event) {
	// Replicate MySlider's click-to-seek but hit-test against the visual
	// handle rect (the natural-sized one), not the full 2× widget. So a
	// click in the empty band above/below the painted handle still seeks.
	if (event->button() == Qt::LeftButton) {
		QStyleOptionSlider opt;
		initStyleOption(&opt);
		opt.rect = naturalVisualRect();
		const QRect handleRect = style()->subControlRect(
			QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
		const QPoint centre = handleRect.center() - handleRect.topLeft();
		if (!handleRect.contains(event->pos())) {
			setSliderPosition(pixelPosToRangeValue(event->x() - centre.x()));
			triggerAction(SliderMove);
			setRepeatAction(SliderNoAction);
		}
		QSlider::mousePressEvent(event);
	} else {
		QSlider::mousePressEvent(event);
	}
}

void TimeSlider::updateHoverPreview(int xLocal) {
	QStyleOptionSlider opt;
	initStyleOption(&opt);
	opt.rect = naturalVisualRect();
	const QRect handleRect = style()->subControlRect(
		QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
	const QPoint centre = handleRect.center() - handleRect.topLeft();

	const int value = pixelPosToRangeValue(xLocal - centre.x());
	const int range = maximum() - minimum();
	if (range <= 0) return;
	const qreal time_sec = value * total_time / range;
	if (time_sec < 0 || time_sec > total_time) return;

	if (!thumb_tooltip) {
		thumb_tooltip = new TimeTooltip(this);
	}

	// Anchor at the top of the *visual* slider band (not the widget's
	// physical top, which is now padded by the extended height).
	const QRect vis = naturalVisualRect();
	const QPoint anchor = mapToGlobal(QPoint(xLocal, vis.top()));
	thumb_tooltip->setAnchor(anchor);
	thumb_tooltip->setTimeText(Helper::formatTime(time_sec));

	const qint64 timeMs = qint64(time_sec * 1000.0);
	const qint64 bkt = (timeMs / 1000) * 1000;
	if (bkt != last_hover_bucket_ms) {
		last_hover_bucket_ms = bkt;
		ThumbnailProvider::instance()->requestThumbnail(timeMs);
	}

	if (!thumb_tooltip->isVisible()) thumb_tooltip->show();
}

void TimeSlider::onThumbnailReady(qint64 /*timeMs*/, const QImage & img) {
	if (!thumb_tooltip || !thumb_tooltip->isVisible()) return;
	thumb_tooltip->setThumbnail(img);
}

void TimeSlider::contextMenuEvent(QContextMenuEvent * event) {
	// Right-click on the seek bar: quick toggles for the hover-thumbnail
	// feature. Same shape as vlc-reborn's seekbar context menu.
	ThumbnailProvider * tp = ThumbnailProvider::instance();

	if (thumb_tooltip) {
		thumb_tooltip->hide();
		thumb_tooltip->clearThumbnail();
	}

	QMenu menu(this);
	QAction * header = menu.addAction(tr("Hover Thumbnails"));
	header->setEnabled(false);
	menu.addSeparator();

	QAction * toggle = menu.addAction(tr("Show on hover"));
	toggle->setCheckable(true);
	toggle->setChecked(tp->enabled());

	QMenu * sizeMenu = menu.addMenu(tr("Size"));
	const int currentW = tp->thumbWidth();
	const struct { const char * label; int w; } sizes[] = {
		{ "Small (160 px)",  160 },
		{ "Medium (240 px)", 240 },
		{ "Large (320 px)",  320 },
		{ "XL (480 px)",     480 },
	};
	QActionGroup * sizeGroup = new QActionGroup(&menu);
	for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
		QAction * a = sizeMenu->addAction(tr(sizes[i].label));
		a->setCheckable(true);
		a->setChecked(currentW == sizes[i].w);
		a->setData(sizes[i].w);
		sizeGroup->addAction(a);
	}

	QMenu * qMenu = menu.addMenu(tr("Quality"));
	const int currentQ = tp->thumbQuality();
	const struct { const char * label; int q; } qualities[] = {
		{ "Low (60)",     60 },
		{ "Medium (85)",  85 },
		{ "High (95)",    95 },
	};
	QActionGroup * qGroup = new QActionGroup(&menu);
	for (size_t i = 0; i < sizeof(qualities)/sizeof(qualities[0]); ++i) {
		QAction * a = qMenu->addAction(tr(qualities[i].label));
		a->setCheckable(true);
		a->setChecked(currentQ == qualities[i].q);
		a->setData(qualities[i].q);
		qGroup->addAction(a);
	}

	QAction * picked = menu.exec(event->globalPos());
	if (!picked) return;
	if (picked == toggle) {
		tp->setEnabled(!tp->enabled());
	} else if (sizeGroup->actions().contains(picked)) {
		tp->setThumbWidth(picked->data().toInt());
	} else if (qGroup->actions().contains(picked)) {
		tp->setThumbQuality(picked->data().toInt());
	}
	last_hover_bucket_ms = -1;  // force the next hover to re-request
}

#include "moc_timeslider.cpp"
