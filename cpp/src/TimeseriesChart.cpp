#include "TimeseriesChart.h"
#include "Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace {

// Tick vocabulary in the DD dashboard spirit (1, 2, 2.5, 5 x 10^k),
// aiming for ~4-6 gridlines like the reference panels.
double niceStep(double span) {
    constexpr double kMaxLines = 6.0;
    if (span <= 0.0) {
        return 1.0;
    }
    const double rough = span / 4.0;
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (std::ceil(span / (m * mag)) <= kMaxLines) {
            return m * mag;
        }
    }
    return 10.0 * mag;
}

QString tickLabel(double v) {
    double a = std::abs(v);
    if (a >= 1e6) {
        return QString::number(v / 1e6, 'g', 3) + QStringLiteral("M");
    }
    if (a >= 1e3) {
        return QString::number(v / 1e3, 'g', 3) + QStringLiteral("k");
    }
    QString number = QString::number(v, 'g', 4);
    return number;
}

} // namespace

namespace {
constexpr int kMaxSamples = 1800;
} // namespace

TimeseriesChart::TimeseriesChart(QWidget *parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(96);
    m_lastPush = QDateTime::currentDateTime();
}

void TimeseriesChart::setUnit(const QString &unit) {
    m_unit = unit;
    update();
}

void TimeseriesChart::setSeriesName(const QString &name) {
    m_nameA = name;
    update();
}

void TimeseriesChart::setSeriesNameB(const QString &nameB) {
    m_nameB = nameB;
    update();
}

void TimeseriesChart::setSeriesColors(const QColor &a, const QColor &b) {
    m_colorA = a;
    m_colorB = b;
    update();
}

void TimeseriesChart::setCapacity(int capacity) {
    while (m_valuesA.size() > capacity) {
        m_valuesA.removeFirst();
    }
    while (m_valuesB.size() > capacity) {
        m_valuesB.removeFirst();
    }
}

void TimeseriesChart::setPaused(bool paused) {
    m_paused = paused;
    if (paused) {
        m_lastPush = QDateTime::currentDateTime();
    }
    update();
}

void TimeseriesChart::clear() {
    m_valuesA.clear();
    m_valuesB.clear();
    m_forecastLow.clear();
    m_forecastHigh.clear();
    update();
}

void TimeseriesChart::setValues(const QList<double> &valuesA,
                               const QList<double> &valuesB) {
    m_valuesA = valuesA;
    while (m_valuesA.size() > kMaxSamples) {
        m_valuesA.removeFirst();
    }
    m_valuesB = valuesB;
    while (m_valuesB.size() > kMaxSamples) {
        m_valuesB.removeFirst();
    }
    m_lastPush = QDateTime::currentDateTime();
    update();
}

void TimeseriesChart::push(double value) {
    if (m_paused) {
        return;
    }
    m_valuesA.append(value);
    if (m_valuesA.size() > kMaxSamples) {
        m_valuesA.removeFirst();
    }
    m_lastPushedA = value;
    m_lastPush = QDateTime::currentDateTime();
    update();
}

void TimeseriesChart::pushB(double value) {
    if (m_paused) {
        return;
    }
    m_valuesB.append(value);
    if (m_valuesB.size() > kMaxSamples) {
        m_valuesB.removeFirst();
    }
    m_lastPushedB = value;
    update();
}

void TimeseriesChart::setOverlayOn(bool on) {
    m_overlayOn = on;
    update();
}

void TimeseriesChart::setOverlay(double value) {
    m_overlay = value;
    update();
}

void TimeseriesChart::setOverlayColor(const QColor &color) {
    m_overlayColor = color;
    update();
}

void TimeseriesChart::setBarMode(bool bars) {
    m_barMode = bars;
    update();
}

void TimeseriesChart::setForecast(const QList<double> &low,
                                  const QList<double> &high) {
    m_forecastLow = low;
    m_forecastHigh = high;
    update();
}

void TimeseriesChart::paintEvent(QPaintEvent *event) {
    static_cast<void>(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal w = static_cast<qreal>(width());
    const qreal h = static_cast<qreal>(height());

    QFont tickFont;
    tickFont.setPixelSize(9);
    painter.setFont(tickFont);
    const QFontMetrics fm(tickFont);

    // Layout: left gutter for tick labels + rotated unit, bottom strips
    // for x time labels and the legend row (name + min/max/avg/current).
    const int tickColWidth = 34;
    const int unitColWidth = m_unit.isEmpty() ? 2 : 14;
    const int axisW = tickColWidth + unitColWidth;
    const bool hasLegend = !m_nameA.isEmpty();
    const int legendH = hasLegend ? 13 : 0;
    const int legendH2 = !m_nameB.isEmpty() ? 13 : 0;
    const int timeH = m_valuesA.size() >= 2 ? 14 : 2;
    const qreal plotX = axisW;
    const qreal plotY = 2.0;
    const qreal plotW = w - axisW - 4.0;
    const qreal plotH = h - plotY - timeH - legendH - legendH2 - 2.0;
    const QRectF plot(plotX, plotY, plotW, plotH);
    if (plotW < 24 || plotH < 10) {
        return;
    }

    // Range over both series and the forecast band.
    double low = 0.0;
    double high = 0.0;
    bool any = false;
    for (double v : m_valuesA) {
        low = std::min(low, v);
        high = std::max(high, v);
        any = true;
    }
    for (double v : m_valuesB) {
        low = std::min(low, v);
        high = std::max(high, v);
        any = true;
    }
    for (double v : m_forecastLow) {
        low = std::min(low, v);
        high = std::max(high, v);
    }
    for (double v : m_forecastHigh) {
        low = std::min(low, v);
        high = std::max(high, v);
    }
    if (!any) {
        // Empty panel: draw grid only.
    }
    const double span = std::max(high - low, 1e-9);
    const double step = niceStep(span);
    double top = std::ceil((high + 0.02 * span) / step) * step;
    if (top <= 0.0) {
        top = step;
    }
    const double bottom = (low >= 0.0 && low < step * 0.25) ? 0.0 : low;

    // Horizontal gridlines + y tick labels + rotated unit title.
    QPen gridPen{QColor(ui_theme::grid())};
    gridPen.setWidthF(1.0);
    QPen tickPen{QColor(ui_theme::dim())};
    painter.setPen(tickPen);
    for (double t = bottom; t <= top + 1e-12; t += step) {
        const qreal y = plot.bottom() - (t - bottom) / (top - bottom) * plot.height();
        painter.setPen(QPen(QColor(ui_theme::muted()), 1.0));
        painter.drawText(QRectF(0.0, y - 6.0, tickColWidth - 3.0, 12.0),
                         Qt::AlignRight | Qt::AlignVCenter, tickLabel(t));
        painter.setPen(gridPen);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    if (!m_unit.isEmpty()) {
        painter.save();
        painter.translate(8.0, plot.center().y());
        painter.rotate(-90);
        painter.setPen(QPen(QColor(ui_theme::muted()), 1.0));
        painter.drawText(QRectF(-plot.height() / 2.0, -4.0, plot.height(),
                                static_cast<qreal>(fm.height())),
                         Qt::AlignCenter, m_unit);
        painter.restore();
    }

    // X time labels: like DD, four stops across the window mirroring
    // real wall-clock spacing of the pushed samples.
    const QDateTime now = m_paused ? m_lastPush : QDateTime::currentDateTime();
    if (m_valuesA.size() >= 2) {
        painter.setPen(QPen(QColor(ui_theme::muted()), 1.0));
        for (int i = 0; i <= 3; ++i) {
            const qreal frac = static_cast<qreal>(i) / 3.0;
            const QDateTime stamp = now.addSecs(
                static_cast<qint64>(-2.0 * m_valuesA.size() * (1.0 - frac)));
            const QString label = stamp.toString(QStringLiteral("HH:mm"));
            painter.drawText(QRectF(plot.left() - 24.0 + frac * plot.width(),
                                    plot.bottom() + 2.0, 48.0, 12.0),
                             Qt::AlignCenter, label);
        }
    }

    // Legend rows (img2): colored dash + series name left, min/max/avg/
    // current numeric readouts right-aligned per column.
    const qreal legendTop = plot.bottom() + static_cast<qreal>(timeH);
    auto legendRow = [&](const QString &name, const QColor &color,
                         const QList<double> &values, qreal y) {
        painter.setPen(QPen(color, 2.0));
        painter.drawLine(QPointF(axisW, y + 6.0), QPointF(axisW + 10.0, y + 6.0));
        const qreal cellW = 66.0;
        const qreal nameW = w - axisW - 15.0 - cellW * 4;
        if (nameW > 40.0) {
            painter.setPen(QPen(QColor(ui_theme::muted()), 1.0));
            painter.drawText(
                QRectF(axisW + 15.0, y, nameW, 12.0),
                Qt::AlignLeft | Qt::AlignVCenter,
                fm.elidedText(name, Qt::ElideMiddle, static_cast<int>(nameW)));
        }
        if (values.isEmpty()) {
            return;
        }
        double minV = values.first();
        double maxV = values.first();
        double sum = 0.0;
        for (double v : values) {
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
            sum += v;
        }
        const double avg = sum / static_cast<double>(values.size());
        const double cur = values.last();
        const struct {
            const char *head;
            double value;
        } cells[4] = {
            {"min", minV}, {"avg", avg}, {"max", maxV}, {"cur", cur}
        };
        for (int i = 0; i < 4; ++i) {
            const qreal right = w - 4.0 - cellW * (4 - i);
            painter.setPen(QPen(QColor(ui_theme::muted()), 1.0));
            painter.drawText(QRectF(right, y, 26.0, 12.0),
                             Qt::AlignLeft | Qt::AlignVCenter, cells[i].head);
            painter.setPen(QPen(QColor(ui_theme::dim()), 1.0));
            painter.drawText(QRectF(right + 30.0, y, cellW - 34.0, 12.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             tickLabel(cells[i].value));
        }
    };
    if (hasLegend) {
        legendRow(m_nameA, m_colorA, m_valuesA, legendTop);
    }
    if (!m_nameB.isEmpty()) {
        legendRow(m_nameB, m_colorB, m_valuesB,
                  legendTop + static_cast<qreal>(legendH));
    }

    if (!any) {
        return;
    }

    // Series lines (area-filled): the panel's palette (purple/cyan per
    // the DD reference unless overridden).
    const QColor colorA = m_colorA;
    const QColor colorB = m_colorB;
    auto seriesPath = [&](const QList<double> &values, const QRectF &rect,
                          double lo, double hi, bool fill) {
        QPainterPath path;
        const int n = values.size();
        if (n < 2) {
            return path;
        }
        for (int i = 0; i < n; ++i) {
            const qreal x = rect.left() + rect.width() * i / (n - 1);
            const double v = std::clamp(values.at(i), lo, hi);
            const qreal y = rect.bottom() - (v - lo) / (hi - lo) * rect.height();
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        if (fill) {
            path.lineTo(rect.right(), rect.bottom());
            path.lineTo(rect.left(), rect.bottom());
            path.closeSubpath();
        }
        return path;
    };

    const QRectF seriesRect = plot;
    QColor fillA = colorA;
    fillA.setAlpha(38);
    QColor fillB = colorB;
    fillB.setAlpha(28);

    if (m_barMode) {
        const int n = m_valuesA.size();
        if (n >= 2) {
            const qreal bw = std::max<qreal>(1.5, plot.width() / n * 0.7);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_valuesB.isEmpty() ? colorA : fillA);
            for (int i = 0; i < n; ++i) {
                const qreal x = plot.left() + plot.width() * i / (n - 1);
                const double v = std::clamp(m_valuesA.at(i), bottom, top);
                const qreal y = plot.bottom() -
                    (v - bottom) / (top - bottom) * plot.height();
                painter.drawRect(QRectF(x - bw / 2.0, y, bw,
                                        plot.bottom() - y));
            }
            if (!m_valuesB.isEmpty()) {
                const int m = m_valuesB.size();
                const qreal bwB = std::max<qreal>(1.5, plot.width() / m * 0.5);
                painter.setBrush(colorB);
                for (int i = 0; i < m; ++i) {
                    const qreal x = plot.left() + plot.width() * i / (m - 1);
                    const double v = std::clamp(m_valuesB.at(i), bottom, top);
                    const qreal y = plot.bottom() -
                        (v - bottom) / (top - bottom) * plot.height();
                    painter.drawRect(QRectF(x - bwB / 2.0, y, bwB,
                                            plot.bottom() - y));
                }
            }
        }
    } else {
        if (!m_valuesB.isEmpty()) {
            const int shift = m_valuesA.size() - m_valuesB.size();
            const QRectF rectB = plot.adjusted(
                shift > 0
                    ? plot.width() * shift /
                          std::max<qlonglong>(1, static_cast<qlonglong>(m_valuesA.size() - 1))
                    : 0.0,
                0.0, 0.0, 0.0);
            painter.setPen(QPen(colorB, 2.0, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(fillB);
            painter.drawPath(seriesPath(m_valuesB, rectB, bottom, top, true));
            painter.setBrush(Qt::NoBrush);
        }
        if (m_valuesA.size() >= 2) {
            painter.setPen(QPen(colorA, 2.0, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(fillA);
            painter.drawPath(seriesPath(m_valuesA, seriesRect, bottom, top,
                                        true));
            painter.setBrush(Qt::NoBrush);
        }
    }

    // Forecast band after "now".
    if (!m_forecastLow.isEmpty()
            && m_forecastLow.size() == m_forecastHigh.size()
            && !m_valuesA.isEmpty()) {
        const int n = m_valuesA.size() - 1;
        const qreal startX = plot.left() + plot.width() * n /
            std::max<qlonglong>(1, static_cast<qlonglong>(m_valuesA.size() - 1));
        const double spanF = std::max(1, static_cast<int>(
            m_forecastLow.size()));
        const qreal widthF = plot.width() * spanF /
            (m_valuesA.size() - 1 + spanF);
        QPainterPath band;
        for (int i = 0; i < m_forecastHigh.size(); ++i) {
            const qreal x = startX + widthF * i /
                std::max<qreal>(1.0, static_cast<qreal>(spanF - 1));
            const double v = std::clamp(m_forecastHigh.at(i), bottom, top);
            const qreal y = plot.bottom() - (v - bottom) / (top - bottom) *
                plot.height();
            if (i == 0) {
                band.moveTo(x, y);
            } else {
                band.lineTo(x, y);
            }
        }
        for (int i = m_forecastLow.size() - 1; i >= 0; --i) {
            const qreal x = startX + widthF * i /
                std::max<qreal>(1.0, static_cast<qreal>(spanF - 1));
            const double v = std::clamp(m_forecastLow.at(i), bottom, top);
            const qreal y = plot.bottom() - (v - bottom) / (top - bottom) *
                plot.height();
            band.lineTo(x, y);
        }
        band.closeSubpath();
        QColor fc{QColor(ui_theme::ok())};
        fc.setAlpha(46);
        painter.setPen(QPen(QColor(ui_theme::ok()), 2.0, Qt::DotLine));
        painter.setBrush(fc);
        painter.drawPath(band);
        painter.setBrush(Qt::NoBrush);
        // "Now" marker at the boundary.
        painter.setPen(QPen(QColor(ui_theme::muted()), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(startX, plot.top()), QPointF(startX, plot.bottom()));
    }

    // Centered big-value overlay.
    if (m_overlayOn && plot.height() > 40) {
        QFont big = QFont();
        big.setPixelSize(static_cast<int>(plot.height() * 0.52));
        big.setBold(true);
        painter.setFont(big);
        painter.setPen(QPen(m_overlayColor));
        painter.drawText(plot, Qt::AlignCenter, tickLabel(m_overlay));
    }
}
