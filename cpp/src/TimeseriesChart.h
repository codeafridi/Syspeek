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
class TimeseriesChart : public QWidget {
    Q_OBJECT

public:
    explicit TimeseriesChart(QWidget *parent = nullptr);

    void setUnit(const QString &unit);
    void setSeriesName(const QString &name);
    void setSeriesNameB(const QString &nameB);
    void setCapacity(int capacity);
    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    void clear();

    void push(double value);
    void pushB(double value);

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
    QDateTime m_lastPush;
    double m_lastPushedA = 0.0;
    double m_lastPushedB = 0.0;
    bool m_paused = false;
    bool m_overlayOn = false;
    double m_overlay = 0.0;
    QColor m_overlayColor{225, 95, 80};
    bool m_barMode = false;
};
