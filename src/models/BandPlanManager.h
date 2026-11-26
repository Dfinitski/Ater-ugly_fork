#pragma once

#include <QObject>
#include <QColor>
#include <QMap>
#include <QString>
#include <QVector>

namespace AetherSDR {

// Manages selectable band plan overlays for the spectrum display.
// Plans are loaded from bundled Qt resource JSON files.
// Replaces the compile-time kBandPlan[]/kBandSpots[] arrays (#425).
class BandPlanManager : public QObject {
    Q_OBJECT

public:
    struct Segment {
        double lowMhz;
        double highMhz;
        QString label;      // preferred mode — painted on the band strip
        QString license;
        QColor color;
        // Optional usage annotation (the IARU chart's second column, e.g.
        // "Digimodes, automatically controlled data stations (unattended)").
        // Shown on hover only. Never feeds isVoiceSegmentLabel(), which
        // keys the S-History voice mask off `label` alone. Last so existing
        // positional initialisers stay valid. (#4667)
        QString usage{};
    };

    struct Spot {
        double freqMhz;
        QString label;
    };

    struct KiwiDxSpot {
        double freqMhz{0.0};
        QString mode;
        QString name;
        QString notes;
        int loOffsetHz{0};
        int hiOffsetHz{0};
        int timeBegin{0};  // b0 (HHMM UTC)
        int timeEnd{0};    // e0 (HHMM UTC)
        int dowMask{0};    // d0
        QString extParam;  // p
    };

    // A contiguous, gap-free [low, high] band region produced by merging the
    // active plan's segments. Discrete-channel bands (e.g. US 60 m: 5
    // channels separated by tens of kHz of regulatory gap) collapse to one
    // Region per legal channel; continuous bands collapse to a single
    // Region spanning the band's regulatory edges. Consumers that need
    // gap-aware TX walking (ATU pre-tune, SWR sweep) read these instead of
    // a naive min/max across segments(). (#2822)
    struct Region {
        double lowMhz;
        double highMhz;
    };

    explicit BandPlanManager(QObject* parent = nullptr);

    // Load all bundled plans from Qt resources
    void loadPlans();

    // Active plan
    QString activePlanName() const { return m_activeName; }
    void setActivePlan(const QString& name);
    const QVector<Segment>& segments() const { return m_segments; }
    const QVector<Spot>& spots() const { return m_spots; }

    // Active plan's optional license-class table: code → human label
    // (e.g. {"T": "Technician", "G": "General", "E": "Extra"} for ARRL US).
    // Empty when the plan declares no class structure — callers should hide
    // class-filter UI entirely in that case. (#2649)
    const QMap<QString, QString>& licenseClasses() const { return m_licenseClasses; }

    // Available plans (display names)
    QStringList availablePlans() const;

    // ANAN fork build 790: user colours for the band plan (View > Band Plan >
    // Colors...). A plan's segments are coloured by group (every segment of a
    // group carries the same colour in the JSON), so an override replaces one
    // ORIGINAL colour of the active plan with the user's one; it is kept per
    // plan. segments() returns the overridden colours, so the spectrum strip
    // and the quick-memory buttons follow.
    struct ColorGroup {
        QColor original;     // the colour in the plan's JSON
        QColor current;      // the override, or the original
        QStringList labels;  // the segment labels of the group
    };
    QVector<ColorGroup> colorGroups() const;          // active plan, JSON order
    void setColorOverride(const QColor& original, const QColor& color); // invalid = reset
    void resetColorOverrides();                       // active plan
    // The colour of a frequency outside every segment (quick-memory buttons).
    static QColor defaultOutOfPlanColor() { return QColor(0x30, 0x40, 0x50); }
    QColor outOfPlanColor() const { return m_outOfPlanColor; }
    void setOutOfPlanColor(const QColor& color);      // invalid = default

    // Test hook: replace the active plan's segments without going through
    // resource loading / AppSettings. Used by the license-filter unit tests
    // (tests/band_plan_license_filter_test.cpp). (#3060)
    void setSegmentsForTest(const QVector<Segment>& segs) { m_segments = segs; }

    // Walk the active plan's segments whose midpoint falls inside
    // [searchLowMhz, searchHighMhz], sort by low edge, and merge adjacent
    // or overlapping segments (1 Hz adjacency tolerance) into contiguous
    // regions. Returns regions sorted by ascending lowMhz. Empty if no
    // segment matches. (#2822)
    QVector<Region> contiguousRegionsForBand(double searchLowMhz,
                                             double searchHighMhz) const;

    // License-class-aware overload. When allowedClass is non-empty, segments
    // whose license field is non-empty and does NOT contain allowedClass are
    // dropped BEFORE the sort+merge — adjacent allowed and disallowed
    // segments stay separate regions instead of merging into one. Segments
    // with empty license (BCN markers, general-purpose) always pass through.
    // Plans that don't carry license markings collapse to the two-arg
    // behavior. (#2649)
    QVector<Region> contiguousRegionsForBand(double searchLowMhz,
                                             double searchHighMhz,
                                             const QString& allowedClass) const;

    // KiwiSDR Community DX database
    void loadKiwiDxSpots();
    const QVector<KiwiDxSpot>& kiwiDxSpots() const { return m_kiwiDxSpots; }

    // Normalizes spot mode strings (e.g. CWN->CW, AMN->AM, SAL/SAU->SAM, NBFM->FM)
    // to a valid SmartSDR mode. Returns empty QString for unrecognized or unsupported modes.
    static QString normalizeSpotMode(const QString& rawMode);

signals:
    void planChanged();
    void kiwiDxSpotsChanged();
    void colorsChanged();   // build 790: a user colour changed (same plan)

private:
    struct PlanData {
        QString name;
        QVector<Segment> segments;
        QVector<Spot> spots;
        QMap<QString, QString> licenseClasses;  // code → human label (#2649)
    };

    bool loadPlanFromJson(const QString& path, PlanData& out);
    void applyColorOverrides();     // build 790: the overrides onto m_segments
    void setActivePlanColorsOnly(); // build 790: re-colour, emit colorsChanged

    QVector<PlanData> m_plans;
    QString m_activeName;
    QVector<Segment> m_segments;  // active plan's segments
    QVector<Spot> m_spots;        // active plan's spots
    QMap<QString, QString> m_licenseClasses;  // active plan's class table (#2649)
    QVector<KiwiDxSpot> m_kiwiDxSpots;        // loaded KiwiSDR DX Community spots
    QColor m_outOfPlanColor{defaultOutOfPlanColor()};  // build 790
    bool m_outOfPlanColorLoaded{false};
};

} // namespace AetherSDR

