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

class TimeseriesChart : public QWidget {
    Q_OBJECT

public:
    explicit TimeseriesChart(QWidget *parent = nullptr);

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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<double> m_valuesA;
    QList<double> m_valuesB;
    QList<double> m_forecastLow;
    QList<double> m_forecastHigh;
    QString m_unit;
    QString m_nameA;
    QString m_nameB;
    QColor m_colorA{127, 92, 255};
    QColor m_colorB{60, 202, 170};
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
};
