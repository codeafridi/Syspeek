#pragma once

#include <QColor>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QWidget>

// Datadog-style time-series panel: bold title area handled by the parent
// card; this widget paints the plot with y ticks + rotated unit title,
// horizontal gridlines, x time labels, an area-filled series line
// (optionally a second series), a legend footer with min/max/avg/current
// readouts, an optional centered big-value overlay, an optional bar
// (histogram) mode and an optional forecast band appended after "now".
// Threshold band drawn on the plot (Grafana "line+area" style, absolute
// mode). Grounded on the Grafana panel schema: Threshold{value,color,state},
// ThresholdsConfig{steps sorted by value}, GraphThresholdsStyleMode.
struct ChartBand {
    double value = 0.0;
    QColor color;
    QString label;
    bool above = true; // shade the breaching side: above the line, or below
};

enum class ChartBandStyle { Off, Line, LineAndArea };

// A point in time that something happened, drawn as a hairline tick over the
// plot: Grafana's annotation overlay (an annotation carries epoch/epochEnd)
// reduced to what a HUD can render. severity: 1 info, 2 warning, 3 error.
struct ChartEvent {
    qint64 atMs = 0;
    int severity = 1;
    QString label;
};

class TimeseriesChart : public QWidget {
    Q_OBJECT

public:
    explicit TimeseriesChart(QWidget *parent = nullptr);

    // Event ticks. The panel replaces the whole set on each tick, so the
    // chart never accumulates history it does not need to paint.
    void setEvents(const QList<ChartEvent> &events);
    QList<ChartEvent> events() const { return m_events; }
    int eventCount() const { return int(m_events.size()); }
    qint64 lastSampleMs() const {
        return m_sampleMs.isEmpty() ? 0 : m_sampleMs.constLast();
    }

    void setUnit(const QString &unit);
    void setSeriesName(const QString &name);
    void setSeriesNameB(const QString &nameB);
    void setSeriesColors(const QColor &a, const QColor &b);
    void setCapacity(int capacity);
    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    void clear();

    void push(double value);
    void pushB(double value);
    // Replaces the whole series in one shot (history rewind: the stored
    // window is rendered as-is instead of being appended tick by tick).
    void setValues(const QList<double> &valuesA,
                   const QList<double> &valuesB = QList<double>());
    // Number of plotted samples (tests + legend honesty checks).
    int sampleCount() const { return int(m_valuesA.size()); }

    // Monitor bands: the axis grows to include them so a threshold above the
    // observed data still renders instead of silently falling off the plot.
    void setBands(const QList<ChartBand> &bands,
                  ChartBandStyle style = ChartBandStyle::LineAndArea);
    QList<ChartBand> bands() const { return m_bands; }

    // Center big-number overlay (Grafana style, img2 "Containers Running").
    void setOverlayOn(bool on);
    void setOverlay(double value);
    void setOverlayColor(const QColor &color);
    void setBarMode(bool bars);

    // Forecast band: same length as the series, plotted after the last
    // sample with "now" marked (img4 "Forecast (1h)").
    void setForecast(const QList<double> &low, const QList<double> &high);

signals:
    // A tick was clicked: "why did this spike" narrows the timeline to it.
    void eventActivated(qint64 atMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Pixel position of a sample index inside the plot (the same mapping the
    // series path uses) and back, for hit testing event ticks.
    int eventIndexAtX(qreal x) const;
    int nearestSampleForMs(qint64 atMs) const;
    QList<double> m_valuesA;
    QList<double> m_valuesB;
    QList<double> m_forecastLow;
    QList<double> m_forecastHigh;
    QString m_unit;
    QString m_nameA;
    QString m_nameB;
    QColor m_colorA{77, 143, 214};
    QColor m_colorB{79, 157, 111};
    QDateTime m_lastPush;
    QList<ChartBand> m_bands;
    ChartBandStyle m_bandStyle = ChartBandStyle::LineAndArea;
    double m_lastPushedA = 0.0;
    double m_lastPushedB = 0.0;
    bool m_paused = false;
    bool m_overlayOn = false;
    double m_overlay = 0.0;
    QColor m_overlayColor{225, 95, 80};
    bool m_barMode = false;
    // One wall-clock stamp per plotted sample (the x axis is index based, so
    // this is what lets an event at a given instant land on the right pixel).
    QList<qint64> m_sampleMs;
    QList<ChartEvent> m_events;
    QRectF m_plotRect;
    int m_hoverEvent = -1;
};
