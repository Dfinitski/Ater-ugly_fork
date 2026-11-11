// Build 237 (ANAN port): the DRM applet wired to the DRM decoder in the ANAN
// backend. The decoder (Dream's receiver core, third_party/dream) runs on one
// slice: that slice is switched to DRM mode, so WDSP runs no demodulator and
// the DRM filter on the panadapter (+-5 kHz by default) selects the station;
// the backend feeds the filtered I/Q to the decoder and plays its audio.

#include "MainWindow.h"

#include "AppletPanel.h"
#include "DrmApplet.h"
#include "core/AppSettings.h"
#include "core/backends/hl2/Hl2Backend.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QImage>
#include <QLoggingCategory>
#include <QVariantMap>

namespace AetherSDR {

namespace {

QVariantMap drmSettingsArg(const DrmApplet::Settings& s)
{
    QVariantMap m;
    m.insert(QStringLiteral("freqInt"), s.freqInterpolation);
    m.insert(QStringLiteral("timeInt"), s.timeInterpolation);
    m.insert(QStringLiteral("mlcIter"), s.mlcIterations);
    m.insert(QStringLiteral("flip"), s.flipSpectrum);
    return m;
}

DrmStatus::Light drmLight(int v)
{
    switch (v) {
    case 1: return DrmStatus::Light::Ok;
    case 2: return DrmStatus::Light::Error;
    case 3: return DrmStatus::Light::Absent;
    default: return DrmStatus::Light::Off;
    }
}

} // namespace

void MainWindow::setupDrmApplet()
{
    DrmApplet* a = m_appletPanel ? m_appletPanel->drmApplet() : nullptr;
    if (!a)
        return;

    const auto refreshSlices = [this, a] {
        QStringList letters;
        for (SliceModel* s : m_radioModel.slices())
            letters << s->letter();
        a->setSliceLetters(letters);
        a->setDecoderAvailable(m_radioModel.isConnected()
                               && m_radioModel.backendDeclaresExtension(QStringLiteral("hl2")));
    };
    connect(&m_radioModel, &RadioModel::sliceAdded, a, [refreshSlices](SliceModel*) { refreshSlices(); });
    connect(&m_radioModel, &RadioModel::sliceRemoved, a, [this, a, refreshSlices](int id) {
        if (id == m_drmSliceId) {
            m_drmSliceId = -1;
            disconnect(m_drmModeConn);
            a->forceOff();
        }
        refreshSlices();
    });
    connect(&m_radioModel, &RadioModel::connectionStateChanged, a, [this, a, refreshSlices](bool on) {
        if (!on && m_drmSliceId >= 0) {
            m_drmSliceId = -1;
            disconnect(m_drmModeConn);
            a->forceOff();
        }
        refreshSlices();
    });

    connect(a, &DrmApplet::enableChanged, this, [this](bool on) {
        if (on)
            drmStart();
        else
            drmStop(true);
    });
    connect(a, &DrmApplet::sliceChanged, this, [this](const QString&) {
        if (m_drmSliceId >= 0) {
            drmStop(true);
            drmStart();
        }
    });
    connect(a, &DrmApplet::serviceSelected, this, [this](int index) {
        if (m_drmSliceId < 0)
            return;
        QVariantMap m;
        m.insert(QStringLiteral("slice"), m_drmSliceId);
        m.insert(QStringLiteral("action"), QStringLiteral("service"));
        m.insert(QStringLiteral("index"), index);
        m_radioModel.invokeBackendExtension(QStringLiteral("hl2"), QStringLiteral("drm"), 0, m);
    });
    connect(a, &DrmApplet::settingsChanged, this, [this, a] {
        if (m_drmSliceId < 0)
            return;
        QVariantMap m = drmSettingsArg(a->settings());
        m.insert(QStringLiteral("slice"), m_drmSliceId);
        m.insert(QStringLiteral("action"), QStringLiteral("settings"));
        m_radioModel.invokeBackendExtension(QStringLiteral("hl2"), QStringLiteral("drm"), 0, m);
    });
    connect(a, &DrmApplet::journalinePageRequested, this, [this](int page) {
        if (m_drmSliceId < 0)
            return;
        QVariantMap m;
        m.insert(QStringLiteral("slice"), m_drmSliceId);
        m.insert(QStringLiteral("action"), QStringLiteral("journaline"));
        m.insert(QStringLiteral("page"), page);
        m_radioModel.invokeBackendExtension(QStringLiteral("hl2"), QStringLiteral("drm"), 0, m);
    });
    refreshSlices();
}

void MainWindow::wireDrmBackend(QObject* backend)
{
    auto* hl2 = qobject_cast<hl2::Hl2Backend*>(backend);
    if (!hl2)
        return;
    disconnect(hl2, &hl2::Hl2Backend::drmStatusReady, this, nullptr);
    disconnect(hl2, &hl2::Hl2Backend::drmSlideReady, this, nullptr);
    connect(hl2, &hl2::Hl2Backend::drmStatusReady, this,
            [this](int sliceId, const QVariantMap& st) { onDrmStatus(sliceId, st); });
    connect(hl2, &hl2::Hl2Backend::drmSlideReady, this,
            [this](int sliceId, const QByteArray& img, const QString& name) { onDrmSlide(sliceId, img, name); });
}

void MainWindow::drmStart()
{
    DrmApplet* a = m_appletPanel ? m_appletPanel->drmApplet() : nullptr;
    if (!a)
        return;
    SliceModel* slice = nullptr;
    for (SliceModel* s : m_radioModel.slices())
        if (s->letter() == a->sliceLetter())
            slice = s;
    if (!slice || !m_radioModel.backendDeclaresExtension(QStringLiteral("hl2"))) {
        a->forceOff();
        return;
    }
    m_drmSliceId = slice->sliceId();
    m_drmPrevMode = slice->mode() == QLatin1String("DRM") ? QStringLiteral("AM") : slice->mode();
    if (slice->mode() != QLatin1String("DRM"))
        slice->setMode(QStringLiteral("DRM"));
    a->clearSlide();

    // Leaving DRM mode on the slice (mode button, band button) ends decoding.
    disconnect(m_drmModeConn);
    m_drmModeConn = connect(slice, &SliceModel::modeChanged, this, [this, a](const QString& mode) {
        if (m_drmSliceId >= 0 && mode != QLatin1String("DRM")) {
            drmStop(false);
            a->forceOff();
        }
    });

    QVariantMap m;
    m.insert(QStringLiteral("slice"), m_drmSliceId);
    m.insert(QStringLiteral("action"), QStringLiteral("start"));
    m.insert(QStringLiteral("settings"), drmSettingsArg(a->settings()));
    m_radioModel.invokeBackendExtension(QStringLiteral("hl2"), QStringLiteral("drm"), 0, m);
}

void MainWindow::drmStop(bool restoreMode)
{
    if (m_drmSliceId < 0)
        return;
    const int id = m_drmSliceId;
    m_drmSliceId = -1;
    disconnect(m_drmModeConn);
    QVariantMap m;
    m.insert(QStringLiteral("slice"), id);
    m.insert(QStringLiteral("action"), QStringLiteral("stop"));
    m_radioModel.invokeBackendExtension(QStringLiteral("hl2"), QStringLiteral("drm"), 0, m);
    if (restoreMode) {
        if (SliceModel* s = m_radioModel.slice(id);
            s && s->mode() == QLatin1String("DRM") && !m_drmPrevMode.isEmpty())
            s->setMode(m_drmPrevMode);
    }
}

void MainWindow::onDrmStatus(int sliceId, const QVariantMap& m)
{
    DrmApplet* a = m_appletPanel ? m_appletPanel->drmApplet() : nullptr;
    if (!a || sliceId != m_drmSliceId)
        return;
    DrmStatus st;
    st.decoderAvailable = true;
    st.running = m.value(QStringLiteral("running")).toBool();
    const QVariantList lights = m.value(QStringLiteral("lights")).toList();
    if (lights.size() == 6) {
        st.io = drmLight(lights[0].toInt());
        st.timeSync = drmLight(lights[1].toInt());
        st.freqSync = drmLight(lights[2].toInt());
        st.fac = drmLight(lights[3].toInt());
        st.sdc = drmLight(lights[4].toInt());
        st.msc = drmLight(lights[5].toInt());
    }
    const bool synced = m.value(QStringLiteral("synced")).toBool();
    if (synced) {
        st.snrDb = m.value(QStringLiteral("snr")).toDouble();
        st.freqOffsetHz = m.value(QStringLiteral("offsetHz")).toDouble();
        const QString rob = m.value(QStringLiteral("robustness")).toString();
        st.robustness = rob.isEmpty() ? QChar() : rob.front();
        st.bandwidthKHz = m.value(QStringLiteral("bandwidthKHz")).toDouble();
        st.interleaver = m.value(QStringLiteral("interleaver")).toString();
        st.mscQam = m.value(QStringLiteral("mscQam")).toString();
        st.sdcQam = m.value(QStringLiteral("sdcQam")).toString();
    }
    for (const QVariant& v : m.value(QStringLiteral("services")).toList()) {
        const QVariantMap o = v.toMap();
        DrmStatus::Service sv;
        sv.present = o.value(QStringLiteral("present")).toBool();
        sv.audio = o.value(QStringLiteral("audio")).toBool();
        sv.id = o.value(QStringLiteral("id")).toUInt();
        sv.label = o.value(QStringLiteral("label")).toString();
        sv.codec = o.value(QStringLiteral("codec")).toString();
        sv.bitrateKbps = o.value(QStringLiteral("kbps")).toDouble();
        sv.language = o.value(QStringLiteral("language")).toString();
        sv.country = o.value(QStringLiteral("country")).toString();
        sv.decodable = o.value(QStringLiteral("decodable")).toBool();
        st.services << sv;
    }
    // The applet highlights the service being listened to.
    st.selectedService = m.value(QStringLiteral("audioService"), -1).toInt();
    st.textMessage = m.value(QStringLiteral("text")).toString();
    st.stationTime = m.value(QStringLiteral("time")).toString();
    st.journaline = m.value(QStringLiteral("journaline")).toStringList().join(QLatin1Char('\n'));

    if (!st.running)
        st.note = QStringLiteral("Starting the DRM decoder...");
    else if (!m.value(QStringLiteral("fdk")).toBool())
#ifdef Q_OS_WIN
        st.note = QStringLiteral("No AAC decoder: put fdk-aac.dll next to AetherSDR.exe for audio.");
#elif defined(Q_OS_MACOS)
        st.note = QStringLiteral("No AAC decoder: install fdk-aac (brew install fdk-aac) for audio.");
#else
        st.note = QStringLiteral("No AAC decoder: libfdk-aac.so.2 not found (package libfdk-aac2) - no audio.");
#endif
    else if (lights.size() == 6 && st.io != DrmStatus::Light::Ok)
        st.note = QStringLiteral("No signal from the slice.");
    else if (!synced)
        st.note = QStringLiteral("Searching... centre the DRM filter on the station.");
    a->setStatus(st);
}

void MainWindow::onDrmSlide(int sliceId, const QByteArray& image, const QString& name)
{
    DrmApplet* a = m_appletPanel ? m_appletPanel->drmApplet() : nullptr;
    if (!a || sliceId != m_drmSliceId)
        return;
    QImage img;
    if (img.loadFromData(image))
        a->setSlide(img, name);
}

} // namespace AetherSDR
