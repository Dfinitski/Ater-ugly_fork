#include "core/backends/anan/P2Transport.h"
#include "core/backends/anan/AnanNativeUdp.h"

#include <QStringList>
#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cstring>

#ifdef Q_OS_WIN
// windows.h's min/max macros break std::min/std::max in this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>   // timeBeginPeriod (winmm, linked by aethercore)
#endif

Q_LOGGING_CATEGORY(lcAnanP2T, "aether.anan.p2t")

namespace AetherSDR::anan {

namespace {

template <std::size_t N>
qint64 sendDatagram(QUdpSocket& s, const std::array<std::uint8_t, N>& buf,
                    const QHostAddress& host, quint16 port, quint64& txBytes,
                    quint64& txPackets)
{
    const qint64 sent = s.writeDatagram(reinterpret_cast<const char*>(buf.data()),
                                        static_cast<qint64>(N), host, port);
    if (sent > 0) {
        txBytes += static_cast<quint64>(sent);
        ++txPackets;
    }
    return sent;
}

// Monotonic milliseconds (a wall-clock step must not start or stop the
// host-clock stand-in).
qint64 steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::span<const std::uint8_t> asBytes(const QByteArray& b)
{
    return {reinterpret_cast<const std::uint8_t*>(b.constData()),
            static_cast<std::size_t>(b.size())};
}

}  // namespace

P2Transport::P2Transport(QObject* parent) : hl2::MetisClient(parent)
{
    m_keepalive = new QTimer(this);
    m_keepalive->setInterval(kKeepaliveMs);
    connect(m_keepalive, &QTimer::timeout, this, &P2Transport::onKeepaliveTick);

    m_boardWait = new QTimer(this);
    m_boardWait->setSingleShot(true);
    connect(m_boardWait, &QTimer::timeout, this, &P2Transport::onBoardWaitTimeout);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, [this] {
        if (!m_up)
            emit connectFailed(QStringLiteral("no IQ stream from the radio within %1 ms of start")
                                   .arg(kConnectTimeoutMs));
    });

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(kWatchdogTickMs);
    connect(m_watchdog, &QTimer::timeout, this, &P2Transport::onWatchdogTick);

    m_spkTimer = new QTimer(this);
    m_spkTimer->setInterval(2);
    m_spkTimer->setTimerType(Qt::PreciseTimer);
    connect(m_spkTimer, &QTimer::timeout, this, &P2Transport::onSpeakerPacerTick);

    m_txTimer = new QTimer(this);
    m_txTimer->setInterval(2);   // host stand-in only (onTxPacerTick)
    m_txTimer->setTimerType(Qt::PreciseTimer);
    connect(m_txTimer, &QTimer::timeout, this, &P2Transport::onTxPacerTick);

    m_paddleHangTimer = new QTimer(this);
    m_paddleHangTimer->setSingleShot(true);
    m_paddleHangTimer->setTimerType(Qt::PreciseTimer);
    connect(m_paddleHangTimer, &QTimer::timeout, this, [this] {
        m_paddleHold = false;
        updateRadioPtt();
    });

    // The speaker sender: its own thread, time critical (not a QObject child:
    // it is joined in the destructor).
    m_spkThread = QThread::create([this] { speakerSenderLoop(); });
    m_spkThread->setObjectName(QStringLiteral("anan-spk"));
    m_spkThread->start(QThread::TimeCriticalPriority);
}

P2Transport::~P2Transport()
{
    stop();
    if (m_spkThread) {
        {
            std::lock_guard<std::mutex> lock(m_spkMutex);
            m_spkQuit = true;
        }
        m_spkCv.notify_all();
        m_spkThread->wait();
        delete m_spkThread;
        m_spkThread = nullptr;
    }
}

int P2Transport::attenuationForLnaGainDb(int gainDb) noexcept
{
    return std::clamp(-gainDb, 0, kMaxStepAttenuationDb);
}

int P2Transport::receiversForBoard(const BoardProfile& profile, int numDdc) noexcept
{
    const int room = numDdc > 0 ? numDdc - profile.rxDdcBase : kMaxDdcs;
    return std::clamp(room, 1, kMaxDdcs);
}

int P2Transport::rateKsps() const noexcept
{
    return hl2::sampleRateHz(m_p.sampleRate) / 1000;
}

// ---- discovery -------------------------------------------------------------

QList<hl2::MetisClient::Discovered> P2Transport::discover(int timeoutMs,
                                                          const QHostAddress& target,
                                                          quint16 port)
{
    QList<Discovered> out;
    QUdpSocket s;
    if (!s.bind(QHostAddress::AnyIPv4, 0))
        return out;
    const auto pkt = buildDiscovery();
    s.writeDatagram(reinterpret_cast<const char*>(pkt.data()),
                    static_cast<qint64>(pkt.size()), target,
                    port ? port : kRadioPort);

    QDeadlineTimer deadline(timeoutMs);
    QSet<QString> seen;
    while (!deadline.hasExpired()) {
        if (!s.waitForReadyRead(static_cast<int>(std::max<qint64>(1, deadline.remainingTime()))))
            break;
        while (s.hasPendingDatagrams()) {
            const QNetworkDatagram dg = s.receiveDatagram();
            const QByteArray data = dg.data();
            const auto reply = parseDiscoveryReply(asBytes(data));
            if (!reply || !reply->isSupportedAnan())
                continue;
            const QString key = dg.senderAddress().toString();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            const BoardProfile profile =
                boardProfileFor(reply->boardId).value_or(defaultBoardProfile());
            Discovered d;
            d.reply.mac = reply->mac;
            d.reply.gatewareVersion = reply->firmwareVer;
            d.reply.boardId = reply->boardId;
            d.reply.streaming = reply->streaming;
            d.reply.numRx = static_cast<std::uint8_t>(receiversForBoard(profile, reply->numDdc));
            d.address = dg.senderAddress();
            // localAddress left empty: the session socket binds the wildcard.
            out.append(d);
        }
    }
    return out;
}

// ---- session ---------------------------------------------------------------

bool P2Transport::start(const hl2::MetisClient::Params& params)
{
    stop();
    m_p = params;
    m_numRx = effectiveNumRx(params);
    m_numRx = std::clamp(m_numRx, 1, kMaxDdcs);
    m_rxHz.fill(params.rxFrequencyHz);
    m_dither = params.ditherBit;
    m_random = params.randomBit;
    m_profile = defaultBoardProfile();
    m_configured = false;
    m_up = false;
    m_discoveredNumDdc = 0;
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
    m_counters = {};
    m_micSeqValid = false;
    m_fpgaBuf = FpgaBuffers{};   // the radio resets its counters when we stop it
    m_bufReported = false;
    m_bufTxEmpty = m_bufTxOvf = m_bufSpkEmpty = m_bufSpkOvf = 0;
    m_linkReported = false;
    {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        m_statusSnapValid = false;
    }
    m_phyClkDev = INT_MIN;
    m_eeeAdv = -1;
    m_gbStatus = -1;
    for (auto& v : m_link)
        v = 0;
    m_dropTotal = 0;
    m_warnedPorts.clear();
    m_warnedTx = false;
    m_diagClock.invalidate();
    m_diagSamples = m_diagGaps = m_diagLost = 0;
    m_adc0Overload = m_overloadInWindow = m_radioPtt = false;
    m_pttLine = m_paddleDown = m_paddleHold = false;
    m_paddleHangTimer->stop();
    m_telemetryClock.invalidate();
    m_statusPackets = 0;
    m_diagStatusSeen = 0;

    // Wildcard bind, exactly as P2Client (verified on the ANAN-100D): binding
    // to Params::localAddress left the session socket deaf to the radio on a
    // multi-interface PC (the first field run saw no Discovery reply).
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::AnyIPv4, 0)) {
        m_sock->deleteLater();
        m_sock = nullptr;
        return false;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &P2Transport::onReadyRead);
    // ANAN-100D port (operator, 31.10: "packets to the radio should leave at
    // once"): UDP has no send buffering of its own (no Nagle; writeDatagram
    // hands the datagram to the stack, which sends it at once). What can hold
    // a packet back is our own pacing -- the 1 ms TX/speaker timers ran on
    // Windows' default 15.6 ms timer tick -- so the system timer runs at 1 ms
    // for the session, and the datagrams are marked DSCP EF (expedited) for
    // any switch or NIC queue that honours it.
#ifdef Q_OS_WIN
    if (!m_timerPeriodSet && timeBeginPeriod(1) == TIMERR_NOERROR)
        m_timerPeriodSet = true;
#endif
    m_sock->setSocketOption(QAbstractSocket::TypeOfServiceOption, 0xB8);   // DSCP 46 (EF)
    // ANAN-100D port (log 02.11 19:03:15: 25 DDC + 22 mic packets lost in
    // one go -- the I/O thread late by ~30 ms, and Windows' default 64 KB
    // receive buffer holds only ~30 ms of this stream): 8 MB holds seconds.
    m_sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);
    m_sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 1024 * 1024);
    qCInfo(lcAnanP2T) << "ANAN P2 transport: socket buffers rx"
                      << m_sock->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt() << "tx"
                      << m_sock->socketOption(QAbstractSocket::SendBufferSizeSocketOption).toInt() << "bytes";
    {
        std::unique_lock<std::shared_mutex> guard(m_sendGuard);
        m_nativeFd = m_sock->socketDescriptor();
        m_nativeIp = m_p.host.toIPv4Address();
    }
    m_run = true;

    // Discovery first, from this socket: the radio streams to the port it
    // came from. General / DDC-Specific / High Priority follow once the reply
    // names the board (configureSession), or after kBoardWaitMs.
    const qint64 sent = sendDatagram(*m_sock, buildDiscovery(), m_p.host, kRadioPort,
                                     m_counters.txBytes, m_counters.txPackets);
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: Discovery to " << m_p.host.toString()
                                << ":" << kRadioPort << " from local port " << m_sock->localPort()
                                << (sent > 0 ? "" : " -- SEND FAILED: ") << (sent > 0 ? QString() : m_sock->errorString());
    m_boardWait->start(kBoardWaitMs);
    m_connectTimer->start(kConnectTimeoutMs);
    m_linkClock.start();
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: starting " << m_numRx
                                << " receiver(s) at " << rateKsps() << " ksps, "
                                << m_rxHz[0] << " Hz, ATT ADC1 " << m_adcAtt[0] << " dB, ADC2 " << m_adcAtt[1] << " dB";
    return true;
}

void P2Transport::configureSession(const BoardProfile& profile, bool fromReply)
{
    if (!m_run || !m_sock || m_configured)
        return;
    m_boardWait->stop();
    m_profile = profile;
    m_configured = true;
    if (!m_profile.psSupported && m_psEnabled) {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_psEnabled = false;
        qCWarning(lcAnanP2T) << "ANAN: PureSignal is not available on the" << m_profile.modelName << "-- off";
    }
    if (m_discoveredNumDdc > 0)
        m_numRx = std::min(m_numRx, receiversForBoard(m_profile, m_discoveredNumDdc));
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: " << m_profile.modelName << " (board " << int(m_profile.boardId)
        << (fromReply ? ", from Discovery" : ", assumed") << "), receivers on DDC"
        << m_profile.rxDdcBase << ".." << (m_profile.rxDdcBase + m_numRx - 1);
    sendGeneral();
    sendTransmitSpecific();
    sendDdcSpecific();
    sendHighPriority(true);
    m_keepalive->start();
    m_watchdog->start();
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.clear();
    }
    m_sessionUp.store(true, std::memory_order_release);
    m_spkPrimed = false;
    m_spkLeadSent = false;
    m_spkPacketsSent = 0;
    m_spkClock.start();
}

void P2Transport::onBoardWaitTimeout()
{
    if (!m_run || m_configured)
        return;
    qCWarning(lcAnanP2T) << "ANAN P2 transport: no Discovery reply within" << kBoardWaitMs
                         << "ms; assuming" << defaultBoardProfile().modelName;
    configureSession(defaultBoardProfile(), false);
}

void P2Transport::stop()
{
#ifdef Q_OS_WIN
    if (m_timerPeriodSet) {
        timeEndPeriod(1);
        m_timerPeriodSet = false;
    }
#endif
    if (m_keepalive)
        m_keepalive->stop();
    if (m_boardWait)
        m_boardWait->stop();
    if (m_connectTimer)
        m_connectTimer->stop();
    if (m_watchdog)
        m_watchdog->stop();
    if (m_txTimer)
        m_txTimer->stop();
    if (m_spkTimer)
        m_spkTimer->stop();
    m_sessionUp.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_mox = false;
        m_moxAtomic.store(false, std::memory_order_relaxed);
        m_txQueue.clear();
    }
    if (m_sock) {
        if (m_configured)
            sendHighPriority(false);
        {
            // No native send may be in flight on this descriptor once it closes.
            std::unique_lock<std::shared_mutex> guard(m_sendGuard);
            m_nativeFd = -1;
        }
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    const bool wasUp = m_up;
    m_run = false;
    m_up = false;
    m_configured = false;
    if (wasUp)
        emit linkDown();
}

void P2Transport::sendGeneral()
{
    sendDatagram(*m_sock, buildGeneral(m_profile.alexEnableMask, paEffective(), m_wb), m_p.host, kRadioPort,
                 m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::sendDdcSpecific()
{
    std::vector<DdcConfig> ddcs;
    // A single-ADC board (Hermes, G2E) has only ADC0 to give.
    const int lastAdc = std::max(0, m_profile.numAdcs - 1);
    for (int i = 0; i < m_numRx; ++i)
        ddcs.push_back(DdcConfig{rateKsps(), std::min(m_rxAdc[static_cast<std::size_t>(i)], lastAdc)});
    sendDatagram(*m_sock,
                 buildDdcSpecific(ddcs, m_profile.numAdcs, m_dither, m_random, m_profile.rxDdcBase,
                                  PsDdcOptions{m_psEnabled && m_mox,
                                               m_psLoopback ? m_profile.numAdcs : m_psAdc}),
                 m_p.host, kDdcSpecificPort, m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::sendHighPriority(bool run)
{
    if (!m_sock)
        return;
    std::array<std::uint32_t, kMaxDdcs> words{};
    for (int i = 0; i < m_numRx; ++i)
        words[static_cast<std::size_t>(i)] = phaseWord(static_cast<double>(m_rxHz[static_cast<std::size_t>(i)]));

    HighPriorityOptions o;
    o.hwDdcBase = m_profile.rxDdcBase;
    o.alexScheme = m_profile.alexScheme;
    o.rxLpfFrequencyHz = static_cast<double>(m_rxHz[0]);
    o.boardHasSpeakerAmpMute = m_profile.hasSpeakerAmpMute;
    o.boardHasAdc1FilterBank = m_profile.hasAdc1FilterBank;
    o.mirrorRxWordsBelowBase = m_profile.rxDdcBase >= 2;
    // Transmit state (DUC0, PTT0, drive, Alex TX LPF / relay).
    o.transmitting = run && m_mox;
    o.txFrequencyHz = static_cast<double>(m_txHz ? m_txHz : m_rxHz[0]);
    o.ducPhaseWord = phaseWord(o.txFrequencyHz);
    o.driveLevel = cwDemoActive() ? 0 : m_drive;   // CW demo: no RF
    o.paEnabled = paEffective();
    o.rxAntenna = m_rxAnt;
    o.tunerStart = m_tunerStart;
    o.alexPreamp = m_lna;
    o.txAntenna = m_txAnt;
    o.psEnabled = m_psEnabled;
    o.psFeedbackSource = m_psSource;
    // The ADC0 (Ant1/2/3 path) filters follow the first receiver listening
    // to ADC0 -- receiver 0 unless it was moved to ADC2; a stopping radio is
    // handed back bypassed.
    std::size_t filterRx = 0;
    for (int i = 0; i < m_numRx; ++i) {
        if (m_rxAdc[static_cast<std::size_t>(i)] == 0) {
            filterRx = static_cast<std::size_t>(i);
            break;
        }
    }
    o.rxLpfFrequencyHz = static_cast<double>(m_rxHz[filterRx]);
    // User Out: RX row of the receive band / TX row of the transmit band
    // (Thetis Penny.cs); all off while stopping (the radio does the same in
    // standby).
    o.openCollector = run ? openCollectorBits(m_oc, static_cast<double>(m_rxHz[filterRx]),
                                              o.txFrequencyHz, o.transmitting, m_tunerStart)
                          : std::uint8_t{0};
    if (o.openCollector != m_ocLast) {
        m_ocLast = o.openCollector;
        qCInfo(lcAnanP2T) << "ANAN: User Out" << QString::number(o.openCollector, 2).rightJustified(7, QLatin1Char('0'))
                          << (o.transmitting ? (m_tunerStart ? "TUNE" : "TX") : "RX");
    }
    o.adc1GroundOnTx = m_adc1GndOnTx;
    o.psFeedbackOnAdc1 = m_psAdc == 1;
    if (run) {
        if (m_profile.alexScheme == AlexScheme::LegacyHighPassLowPass) {
            o.adc0Filter = legacyAlexHpfFor(static_cast<double>(m_rxHz[filterRx]));
        } else if (m_profile.alexScheme == AlexScheme::Orion2BandPass) {
            // Each ADC's bank follows the first receiver listening to it
            // (deskHPSDR); ADC1 with no receiver of its own takes RX1's band.
            std::size_t adc1Rx = filterRx;
            for (int i = 0; i < m_numRx; ++i) {
                if (m_rxAdc[static_cast<std::size_t>(i)] == 1) {
                    adc1Rx = static_cast<std::size_t>(i);
                    break;
                }
            }
            o.adc0Filter = alexRxFilterFor(static_cast<double>(m_rxHz[filterRx]));
            o.adc1Filter = alexRxFilterFor(static_cast<double>(m_rxHz[adc1Rx]));
        } else {
            o.adc0Filter = alexRxFilterFor(static_cast<double>(m_rxHz[0]));
            o.adc1Filter = o.adc0Filter;
        }
    }
    sendDatagram(*m_sock,
                 buildHighPriority(run,
                                   std::span<const std::uint32_t>(words.data(),
                                                                  static_cast<std::size_t>(m_numRx)),
                                   /*bypassAdc0Filters=*/!run, /*bypassAdc1Filters=*/!run,
                                   run ? ((m_mox && m_psEnabled && m_psAdc == 0) ? m_psAttDb : m_adcAtt[0]) : 0,
                                   run ? ((m_mox && m_psEnabled && m_psAdc == 1) ? m_psAttDb : m_adcAtt[1]) : 0, o),
                 m_p.host, kHighPriorityPort, m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::onKeepaliveTick()
{
    // Radio PTT released without a final "PTT off" status packet.
    if (m_radioPtt && !m_mox && m_sinceStatus.isValid()
        && m_sinceStatus.elapsed() > kPttStaleMs) {
        m_pttLine = false;
        m_paddleDown = false;
        if (m_paddleHold && !m_paddleHangTimer->isActive())
            m_paddleHangTimer->start(std::max(10, m_cwKeyer.hangMs));
        if (!m_paddleHold) {
            m_radioPtt = false;
            qCInfo(lcAnanP2T) << "ANAN P2 transport: radio PTT off (status stopped)";
            emit radioPttChanged(false);
        }
    }
    if (m_run && m_configured)
        sendHighPriority(true);
    publishLinkCounters();
    publishTelemetry();
}

void P2Transport::handleBufferStatus(std::span<const std::uint8_t> b)
{
    // Odyssey firmware (02.11): TX FIFO 8192 samples, starts at 6144 (3/4),
    // zeros + refill to 3/4 after running empty, overflow counted; speaker
    // FIFO 2048. Counters are 16 bit, wrap, reset when the host stops the
    // radio -- differences are what count.
    // Layout version in byte 47: 1 = buffers (bytes 32-46), 3 = also the
    // link counters (bytes 16-27, test firmware 02.11).
    if (b.size() < 48 || b[47] == 0x00 || b[47] > 0x0f)
        return;
    const int layout = b[47];
    auto be16 = [&](std::size_t i) { return std::uint16_t((std::uint16_t(b[i]) << 8) | b[i + 1]); };
    auto& f = m_fpgaBuf;
    const int txLevel = be16(32);
    const std::uint16_t txEmpty = be16(34), txOvf = be16(36);
    const int spkLevel = be16(38);
    const std::uint16_t spkFade = be16(40), spkEmptyRd = be16(42), spkOvf = be16(44);
    f.flags = b[46];
    if (!f.seen) {
        f.seen = true;
        m_bufReported = true;
        qCInfo(lcAnanP2T) << "ANAN P2 transport: radio reports its TX / speaker buffers (status layout v"
                          << layout << ")";
    }
    if (layout >= 3) {
        // 16-17 command packets not applied, 18-19 audio packets dropped,
        // 20-21 I/Q packets dropped, 24-25 frames with a bad FCS (PHY quiet),
        // 26-27 frames with PHY RXER. 16 bit, since power-on: differences.
        static constexpr std::size_t kAt[5] = {16, 18, 20, 24, 26};
        static const char* kName[5] = {"command packets not applied", "audio packets dropped",
                                       "I/Q packets dropped", "frames with a bad FCS",
                                       "frames with PHY RXER"};
        std::array<std::uint16_t, 5> now{};
        for (int k = 0; k < 5; ++k)
            now[std::size_t(k)] = be16(kAt[k]);
        if (layout >= 7) {
            // v7: the PHY's crystal against C122 (122.88 MHz, locked to the
            // 10 MHz reference when present), Hz / 8, signed.
            const int dev = int(std::int16_t(be16(53)));
            const int was = m_phyClkDev.exchange(dev, std::memory_order_relaxed);
            f.clkDevMin = std::min(f.clkDevMin, dev);
            f.clkDevMax = std::max(f.clkDevMax, dev);
            if (was == INT_MIN)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY crystal (PHY_CLK125 vs C122): "
                    << QString::number(dev * 8.0, 'f', 0) << " Hz = " << QString::number(dev * 8.0 / 125.0, 'f', 2)
                    << " ppm (1000BASE-T allows +-50 ppm)";
        }
        if (layout >= 6) {
            // Firmware v6: the PHY's own view, read once a second. 8-9 the
            // 1000BASE-T idle error count (IEEE reg 10, accumulated), 10-11
            // the KSZ9031 receive error counter (0x15), 12 the PC's EEE
            // advertisement, 13 the 1000BASE-T status (master, local / remote
            // receiver OK).
            const std::uint16_t idle = be16(8), rxe = be16(10);
            const int eee = b[12], gs = b[13];
            const int eeeWas = m_eeeAdv.exchange(eee, std::memory_order_relaxed);
            const int gsWas = m_gbStatus.exchange(gs, std::memory_order_relaxed);
            if (eee != eeeWas)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY: the PC's network card advertises EEE: "
                    << ((eee & 0x06) ? QStringLiteral("%1%2 -- EEE is ON at the PC")
                                           .arg((eee & 0x04) ? QStringLiteral("1000BASE-T ") : QString())
                                           .arg((eee & 0x02) ? QStringLiteral("100BASE-TX") : QString())
                                     : QStringLiteral("none"))
                    << " (byte 12 = 0x" << QString::number(eee, 16) << ")";
            if (gs != gsWas)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY 1000BASE-T status: radio is clock " << ((gs & 0x40) ? "MASTER" : "slave")
                    << ", local receiver " << ((gs & 0x20) ? "OK" : "NOT OK") << ", remote receiver "
                    << ((gs & 0x10) ? "OK" : "NOT OK") << " (byte 13 = 0x" << QString::number(gs, 16) << ")";
            if (!f.phyBase) {
                f.phyBase = true;
                qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: PHY counters so far: 1000BASE-T idle errors " << idle
                                            << ", KSZ9031 receive errors " << rxe;
            } else {
                const unsigned di = unsigned(std::uint16_t(idle - f.phyIdle));
                const unsigned dr = unsigned(std::uint16_t(rxe - f.phyRx));
                f.dPhyIdle += di;
                f.dPhyRx += dr;
                if (di || dr)
                    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: PHY: +" << di << " 1000BASE-T idle errors, +" << dr
                                                << " KSZ9031 receive errors" << (m_mox ? " (transmitting)" : " (receiving)");
            }
            f.phyIdle = idle;
            f.phyRx = rxe;
        }
        if (!f.linkBase) {
            f.linkBase = true;
            m_linkReported.store(true, std::memory_order_relaxed);
            qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: radio link counters since its power-on: "
                                        << kName[0] << " " << now[0] << ", " << kName[1] << " " << now[1]
                                        << ", " << kName[2] << " " << now[2] << ", " << kName[3] << " "
                                        << now[3] << ", " << kName[4] << " " << now[4];
        } else {
            QStringList inc;
            for (int k = 0; k < 5; ++k) {
                const unsigned dk = unsigned(std::uint16_t(now[std::size_t(k)] - f.link[std::size_t(k)]));
                if (!dk)
                    continue;
                f.dLink[std::size_t(k)] += dk;
                m_link[std::size_t(k)].fetch_add(dk, std::memory_order_relaxed);
                inc << QStringLiteral("+%1 %2").arg(dk).arg(QLatin1String(kName[k]));
            }
            if (!inc.isEmpty())
                qCInfo(lcAnanP2T).noquote().nospace()
                    << "ANAN P2 transport: radio link: " << inc.join(QStringLiteral(", "))
                    << (m_mox ? QStringLiteral(" (transmitting, TX buffer %1 of 8192)").arg(txLevel)
                              : QStringLiteral(" (receiving)"));
        }
        f.link = now;
    }
    if (f.haveBase) {
        const auto d = [](std::uint16_t now, std::uint16_t was) { return unsigned(std::uint16_t(now - was)); };
        const unsigned dEmpty = d(txEmpty, f.txEmpty);
        const unsigned dTxOvf = d(txOvf, f.txOvf);
        const unsigned dSpkFade = d(spkFade, f.spkFade);
        const unsigned dSpkOvf = d(spkOvf, f.spkOvf);
        f.dTxEmpty += dEmpty;
        f.dTxOvf += dTxOvf;
        f.dSpkFade += dSpkFade;
        f.dSpkEmptyRd += d(spkEmptyRd, f.spkEmptyRd);
        f.dSpkOvf += dSpkOvf;
        if (m_mox) {   // the over's end (we stop feeding) is not an error
            m_bufTxEmpty.fetch_add(dEmpty, std::memory_order_relaxed);
            m_bufTxOvf.fetch_add(dTxOvf, std::memory_order_relaxed);
        }
        m_bufSpkEmpty.fetch_add(dSpkFade, std::memory_order_relaxed);
        m_bufSpkOvf.fetch_add(dSpkOvf, std::memory_order_relaxed);
        if (dTxOvf && m_mox)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: the radio's TX buffer OVERFLOWED (+" << dTxOvf << "samples)";
        if (dEmpty && m_mox) {
            const qint64 now = steadyMs();
            if (now - f.lastTxEmptyLogMs >= 1000) {
                f.lastTxEmptyLogMs = now;
                qCInfo(lcAnanP2T) << "ANAN P2 transport: the radio's TX buffer ran EMPTY while transmitting (+"
                                  << dEmpty << ") -- DUC packets arrived late";
            }
        }
    }
    f.haveBase = true;
    f.txEmpty = txEmpty;
    f.txOvf = txOvf;
    f.spkFade = spkFade;
    f.spkEmptyRd = spkEmptyRd;
    f.spkOvf = spkOvf;
    m_radioTxLevelSeq.fetch_add(1, std::memory_order_release);
    if (f.flags & 0x01) {   // TX buffer running
        f.txMin = std::min(f.txMin, txLevel);
        f.txMax = std::max(f.txMax, txLevel);
    }
    if (f.flags & 0x02) {   // speaker playing
        f.spkMin = std::min(f.spkMin, spkLevel);
        f.spkMax = std::max(f.spkMax, spkLevel);
    }
}

void P2Transport::handleStatus(std::span<const std::uint8_t> b)
{
    ++m_statusPackets;
    m_sinceStatus.start();
    if (b.size() >= 60) {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        std::copy(b.begin(), b.begin() + 60, m_statusSnap.begin());
        m_statusSnapValid = true;
        m_statusSnapMs = steadyMs();
    }
    auto be16 = [&](std::size_t i) { return (int(b[i]) << 8) | int(b[i + 1]); };
    m_excRaw = be16(6);
    m_fwdRaw = be16(14);
    m_fwdAtomic.store(m_fwdRaw, std::memory_order_relaxed);
    m_driveAtomic.store(m_drive, std::memory_order_relaxed);
    m_revRaw = be16(22);
    m_supplyRaw = be16(49);   // supply volts ADC (spec p.40)
    m_userAdc1Raw = be16(55); // User ADC1: PA drain current (Thetis getUserADC1)
    m_userAdc0Raw = be16(57); // User ADC0: PA volts (Thetis getUserADC0)
    if (m_profile.odysseyStatus)
        handleBufferStatus(b);
    const bool overload = (b[5] & 0x01) != 0;   // ADC0
    const bool ptt = (b[4] & 0x01) != 0;
    const bool paddle = m_cwKeyer.enabled && (b[4] & 0x06) != 0;   // dot / dash
    if (overload)
        m_overloadInWindow = true;
    if (const unsigned ovl = b[5] & 0x03u)   // ADC0 / ADC1 overload (spec p.40)
        m_ovlLatch.fetch_or(ovl, std::memory_order_acq_rel);
    if (overload != m_adc0Overload)
        qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC0 overload" << (overload ? "ON" : "off");
    m_adc0Overload = overload;
    m_pttLine = ptt;
    if (paddle != m_paddleDown) {
        m_paddleDown = paddle;
        if (paddle) {
            m_paddleHangTimer->stop();
            m_paddleHold = true;
        } else if (m_paddleHold) {
            m_paddleHangTimer->start(std::max(10, m_cwKeyer.hangMs));
        }
    }
    updateRadioPtt();
}

void P2Transport::timelineTick()
{
    if (m_tlPressMs < 0)
        return;
    const qint64 now = m_tlClock.elapsed();
    if (now < m_tlPressMs + 400)
        return;
    // Peak per 5 ms bucket, -50 .. +400 ms around the press.
    QString line;
    for (qint64 b = -50; b < 400; b += 5) {
        float pk = 0.0f;
        bool any = false;
        for (const auto& [t, v] : m_tlIq) {
            const qint64 rel = t - m_tlPressMs;
            if (rel >= b && rel < b + 5) {
                pk = std::max(pk, v);
                any = true;
            }
        }
        line += any ? QString::number(20.0 * std::log10(std::max(pk, 1e-9f)), 'f', 0)
                    : QStringLiteral("_");
        line += (b == -5) ? QStringLiteral(" | ") : QStringLiteral(" ");
    }
    qCInfo(lcAnanP2T).nospace()
        << "ANAN PTT timeline: host MOX at "
        << (m_tlMoxMs >= 0 ? QString::number(m_tlMoxMs - m_tlPressMs) : QStringLiteral("-"))
        << " ms after the radio's PTT; RX1 IQ peak dBFS per 5 ms from -50 ms (| = press): " << line;
    m_tlPressMs = -1;
    m_tlMoxMs = -1;
}

void P2Transport::updateRadioPtt()
{
    const bool keyed = m_pttLine || m_paddleDown || m_paddleHold;
    if (keyed == m_radioPtt)
        return;
    m_radioPtt = keyed;
    if (keyed && m_radioPttPressHook)
        m_radioPttPressHook();
    if (keyed && m_tlPressMs < 0 && m_tlClock.isValid()) {
        m_tlPressMs = m_tlClock.elapsed();
        m_tlMoxMs = m_mox ? m_tlPressMs : -1;
    }
    qCInfo(lcAnanP2T) << "ANAN P2 transport: radio PTT" << (keyed ? "ON" : "off")
                      << (m_paddleDown || m_paddleHold ? "(keyer)" : "");
    emit radioPttChanged(keyed);
}

void P2Transport::setCwKeyer(const CwKeyerOptions& k)
{
    if (k == m_cwKeyer)
        return;
    const bool paWas = paEffective();
    const bool demoWas = cwDemoActive();
    m_cwKeyer = k;
    m_rfDelayMs.store(k.rfDelayMs, std::memory_order_relaxed);   // read by the DUC sender
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: CW keyer " << (k.enabled ? "on" : "off") << " mode " << k.mode
        << " " << k.speedWpm << " WPM weight " << k.weight << " swap " << k.paddleSwap
        << " spacing " << k.strictSpacing << " sidetone " << k.sidetoneLevel << "/" << k.sidetoneHz
        << " Hz hang " << k.hangMs << " ms demo " << k.demo;
    if (!k.enabled && (m_paddleDown || m_paddleHold)) {
        m_paddleDown = m_paddleHold = false;
        m_paddleHangTimer->stop();
        updateRadioPtt();
    }
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (paWas != paEffective())
        sendGeneral();
    if (demoWas != cwDemoActive())
        sendHighPriority(true);
}

void P2Transport::publishTelemetry()
{
    // Hl2Telemetry in Protocol 1's shape: the auto-gain loop counts overload
    // observations per window. One observation per millisecond, all of them
    // overloaded when the bit was set at any point in the window.
    if (!m_up)
        return;
    if (!m_telemetryClock.isValid()) {
        m_telemetryClock.start();
        return;
    }
    const int ms = static_cast<int>(m_telemetryClock.restart());
    const bool hot = m_overloadInWindow || m_adc0Overload;
    hl2::Hl2Telemetry t;
    t.adcOverload = hot;
    t.ptt = m_radioPtt;
    t.adcSamples = std::max(1, ms);
    t.adcOverloadSamples = hot ? t.adcSamples : 0;
    t.adcWindowMs = ms;
    // Directional coupler counts (Alex0). The watts/SWR conversion upstream
    // is the HL2's; ANAN calibration is a TODO (see the port journal).
    if (m_mox) {
        t.forwardPowerRaw = m_fwdRaw;
        t.reversePowerRaw = m_revRaw;
        t.forwardPowerPeakRaw = m_fwdRaw;
        t.forwardPowerSamples = 1;
    }
    if (m_statusPackets > 0) {
        t.supplyVoltsRaw = m_supplyRaw;
        t.paVoltsRaw = m_userAdc0Raw;
        t.paAmpsRaw = m_userAdc1Raw;
    }
    m_overloadInWindow = false;
    emit telemetryUpdated(t);
}

void P2Transport::onWatchdogTick()
{
    if (m_up && m_sinceIq.isValid() && m_sinceIq.elapsed() > kSilenceTimeoutMs) {
        // Like MetisClient: the session stays running, and linkUp is emitted
        // again when IQ resumes.
        m_up = false;
        qCWarning(lcAnanP2T) << "ANAN P2 transport: no IQ for" << kSilenceTimeoutMs << "ms";
        // 03.11 (log 20:40:52): the host's unkey at the link loss is refused by
        // the TX coordinator (a connection reset ends the operation, cleanup
        // included), so MOX stayed set here and the radio transmitted again
        // the moment the link came back -- 14 s at the reset drive. A lost
        // link ends the transmission at the transport, always.
        unkeyOnLinkLoss();
        emit linkDown();
    }
}

// ---- receive ---------------------------------------------------------------

void P2Transport::onReadyRead()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_sock->receiveDatagram();
        if (!dg.senderAddress().isEqual(m_p.host, QHostAddress::TolerantConversion)) {
            if (!m_warnedPorts.contains(0)) {
                m_warnedPorts.insert(0);
                qCWarning(lcAnanP2T) << "ANAN P2 transport: ignoring datagram from"
                                     << dg.senderAddress().toString() << "(radio is"
                                     << m_p.host.toString() << ")";
            }
            continue;
        }
        const QByteArray data = dg.data();
        m_counters.rxBytes += static_cast<quint64>(data.size());
        ++m_counters.rxPackets;
        handleDatagram(asBytes(data), static_cast<quint16>(dg.senderPort()));
    }
}

void P2Transport::handleDatagram(std::span<const std::uint8_t> bytes, quint16 senderPort)
{
    const auto frame = parseDdcFrame(bytes);
    if (!frame) {
        if (const auto reply = senderPort == kRadioPort ? parseDiscoveryReply(bytes)
                                                        : std::nullopt) {
            if (!m_configured) {
                m_discoveredNumDdc = reply->numDdc;
                const auto profile = boardProfileFor(reply->boardId);
                configureSession(profile.value_or(defaultBoardProfile()), profile.has_value());
            }
        }
        if (senderPort == 1025 && bytes.size() == kHighPriorityStatusBytes)
            handleStatus(bytes);
        if ((senderPort == kWidebandBasePort || senderPort == kWidebandBasePort + 1)
            && bytes.size() == static_cast<std::size_t>(kWidebandPacketBytes)) {
            ++m_diagWbPkts[static_cast<std::size_t>(senderPort - kWidebandBasePort)];
            handleWideband(senderPort - kWidebandBasePort, bytes);
            return;
        }
        if (bytes.size() == static_cast<std::size_t>(kWidebandPacketBytes)) {
            ++m_diagWbOther;   // wideband-sized, from an unexpected port
            m_diagWbOtherPort = senderPort;
        }
        if (senderPort == kMicFromRadioPort && bytes.size() == kMicPacketBytes) {
            m_sinceMic.start();
            ++m_diagMicPkts;
            auto mic = parseMicPacket(bytes);
            if (!mic.empty()) {
                // The mic stream is the transmitter's clock: a lost packet
                // still stands for 1.33 ms of the radio's time. Up to 16 in a
                // row are replaced by silence (time kept, TXA in step); a
                // larger jump is a restart, not a loss.
                const std::uint32_t seq = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
                                          | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
                if (m_micSeqValid) {
                    const std::uint32_t lost = seq - m_micSeqNext;
                    if (lost > 0 && lost <= 16) {
                        m_diagMicLost += lost;
                        mic.insert(mic.begin(), std::size_t(lost) * kCodecFramesPerPacket, 0.0f);
                    }
                }
                m_micSeqNext = seq + 1;
                m_micSeqValid = true;
                m_micArrived.fetch_add(mic.size() / kCodecFramesPerPacket, std::memory_order_relaxed);
                emit codecMicReady(mic);
            }
        }
        return;
    }

    // PureSignal: the synchronised DDC0/DDC1 pairs (feedback + TX DAC).
    if (senderPort == kDdc0DefaultPort && m_profile.rxDdcBase > 0) {
        if (m_psEnabled && m_mox && m_psSink) {
            m_psTxBuf.clear();
            m_psRxBuf.clear();
            if (decodePsPairs(bytes, m_psTxBuf, m_psRxBuf) && !m_psRxBuf.empty()) {
                ++m_psPackets;
                // ANAN-100D port (01.11): used as received. The PS DDC pairs
                // already have the host TXA IQ's handedness -- on a working
                // radio the two-tone lies on the same side in both, and
                // PureSignal reaches -45 dBc; conjugating them (anan114) gave
                // -32 (AM/PM learnt with the wrong sign).
                double hwPeak = -1.0;
                const int pairs = int(m_psRxBuf.size() / 2);
                for (int i = 0; i < pairs; ++i) {
                    const double p = m_psTxBuf[std::size_t(2 * i)] * m_psTxBuf[std::size_t(2 * i)]
                                     + m_psTxBuf[std::size_t(2 * i + 1)] * m_psTxBuf[std::size_t(2 * i + 1)];
                    m_hwFbPk = std::max(m_hwFbPk, p);
                    const double pr = m_psRxBuf[std::size_t(2 * i)] * m_psRxBuf[std::size_t(2 * i)]
                                      + m_psRxBuf[std::size_t(2 * i + 1)] * m_psRxBuf[std::size_t(2 * i + 1)];
                    m_scopeFbPk = std::max(m_scopeFbPk, pr);
                    if (++m_hwFbN == kPsFeedbackRateKsps * 500) {   // 0.5 s
                        // Only on a strong signal (>= -6 dBFS sent), so the
                        // ratio is not noise over noise.
                        const double ducLast = m_hwDucLast.load(std::memory_order_relaxed);
                        if (ducLast >= 0.25) {
                            hwPeak = std::sqrt(m_hwFbPk / ducLast);
                            if (m_scopeFbPk > 1e-12)
                                m_scopeFbGain = std::sqrt(ducLast / m_scopeFbPk);
                        }
                        m_hwFbPk = 0.0;
                        m_scopeFbPk = 0.0;
                        m_hwFbN = 0;
                    }
                }
                m_psSink(m_psTxBuf.data(), m_psRxBuf.data(), pairs, hwPeak);
                // ANAN-100D port (operator, 01.11: two tones "with intermods"
                // on the TX panorama): the DUC IQ carries the PureSignal
                // predistortion -- by design it holds the products that cancel
                // the amplifier's. While PureSignal feeds, the TX panorama
                // shows the coupler instead (DDC0, the RF actually sent), at
                // the DUC level (the peaks matched every 0.5 s).
                if (m_scopeFbGain > 0.0) {
                    m_scopeFbBuf.resize(std::size_t(pairs));
                    const float g = static_cast<float>(m_scopeFbGain);
                    for (int i = 0; i < pairs; ++i)
                        m_scopeFbBuf[std::size_t(i)] = {g * float(m_psRxBuf[std::size_t(2 * i)]),
                                                        g * float(m_psRxBuf[std::size_t(2 * i + 1)])};
                    m_scopeFromFb.store(true, std::memory_order_relaxed);
                    m_scopeFbMs.store(steadyMs(), std::memory_order_relaxed);
                    std::lock_guard<std::mutex> txLock(m_txMutex);   // m_txScope is TX state
                    if (m_txScope.push(m_scopeFbBuf.data(), pairs, m_txScopeBins)) {
                        emit txScopeFrameReady(QList<float>(m_txScopeBins.begin(), m_txScopeBins.end()),
                                               static_cast<double>(m_txHz ? m_txHz : m_rxHz[0]));
                    }
                }
            }
        }
        return;
    }
    const std::uint16_t basePort = static_cast<std::uint16_t>(kDdc0DefaultPort + m_profile.rxDdcBase);
    const auto rx = ddcIndexForSenderPort(senderPort, m_numRx, basePort);
    if (!rx) {
        if (!m_warnedPorts.contains(senderPort)) {
            m_warnedPorts.insert(senderPort);
            qCWarning(lcAnanP2T) << "ANAN P2 transport: ignoring IQ from port" << senderPort;
        }
        return;
    }
    const std::size_t slot = static_cast<std::size_t>(*rx);

    if (m_expectedSeq[slot] && frame->seq != *m_expectedSeq[slot]) {
        const std::uint32_t lost = frame->seq - *m_expectedSeq[slot];
        ++m_dropTotal;
        ++m_diagGaps;
        if (lost < 100000)
            m_diagLost += lost;
        emit dropsUpdated(m_dropTotal);
        emit rxSequenceGap(lost);
    }
    m_expectedSeq[slot] = frame->seq + 1;

    m_scratch.clear();
    decodeIq(*frame, m_scratch);
    if (slot == 0) {
        m_diagSamples += m_scratch.size();
        diagTick();
        float pk = 0.0f;
        for (const auto& v : m_scratch)
            pk = std::max(pk, std::abs(v));
        if (!m_tlClock.isValid())
            m_tlClock.start();
        const qint64 now = m_tlClock.elapsed();
        m_tlIq.emplace_back(now, pk);
        while (!m_tlIq.empty() && m_tlIq.front().first < now - 1000)
            m_tlIq.pop_front();
        timelineTick();
    }
    m_pending[slot].push_back(m_scratch);
    if (m_pending[slot].size() > kMaxAheadPackets)
        m_pending[slot].pop_front();

    m_sinceIq.restart();
    if (!m_up && slot == 0) {
        m_up = true;
        m_connectTimer->stop();
        emit linkUp();
    }
    deliverAlignedBlocks();
}

void P2Transport::deliverAlignedBlocks()
{
    // One block per receiver per emission, as Hl2Backend expects from EP6.
    // The DDCs run at one rate, so their packets arrive in lockstep.
    for (;;) {
        for (int i = 0; i < m_numRx; ++i) {
            if (m_pending[static_cast<std::size_t>(i)].empty())
                return;
        }
        m_out.resize(static_cast<std::size_t>(m_numRx));
        for (int i = 0; i < m_numRx; ++i) {
            auto& q = m_pending[static_cast<std::size_t>(i)];
            m_out[static_cast<std::size_t>(i)] = std::move(q.front());
            q.pop_front();
        }
        emit iqBlockReady(m_out[0]);
        emit iqBlocksReady(m_out);
    }
}

void P2Transport::publishLinkCounters()
{
    if (!m_linkClock.isValid() || m_linkClock.elapsed() < kLinkPublishMs)
        return;
    m_linkClock.restart();
    m_counters.drops = m_dropTotal;
    // DUC and speaker packets leave from their own threads (sendNative).
    m_counters.txBytes += m_nativeTxBytes.exchange(0, std::memory_order_relaxed);
    m_counters.txPackets += m_nativeTxPackets.exchange(0, std::memory_order_relaxed);
    if (m_sock)
        m_counters.localEndpoint = QStringLiteral("%1:%2")
                                       .arg(m_sock->localAddress().toString())
                                       .arg(m_sock->localPort());
    emit linkCountersUpdated(m_counters);
}

void P2Transport::diagTick()
{
    if (!m_diagClock.isValid()) {
        m_diagClock.start();
        return;
    }
    const qint64 ms = m_diagClock.elapsed();
    if (ms < kDiagPeriodMs)
        return;
    qCInfo(lcAnanP2T).nospace().noquote()
        << "ANAN diag: DDC" << m_profile.rxDdcBase << " " << qRound(m_diagSamples * 1000.0 / ms)
        << " samples/s (configured " << rateKsps() * 1000 << "), " << m_diagGaps
        << " gaps (" << m_diagLost << " packets lost), " << m_numRx << " rx, RX1 "
        << m_rxHz[0] << " Hz, att " << m_adcAtt[0] << "/" << m_adcAtt[1] << " dB, status packets "
        << m_statusPackets << (m_adc0Overload ? ", ADC0 OVERLOAD" : "")
        << "; codec: mic " << m_diagMicPkts << " pkts, speaker " << m_diagSpkPkts.load() << " pkts ("
        << m_diagSpkSilent << " silent), " << m_diagSpkSubmits << " blocks in, peak "
        << m_diagSpkPeak << ", mic lost " << m_diagMicLost << ", native send fails "
        << m_nativeSendFails.exchange(0, std::memory_order_relaxed) << ", queue " << [this] {
               std::lock_guard<std::mutex> lock(m_spkMutex);
               return m_spkQueue.size();
           }()
        << (m_wb.enableMask || m_diagWbPkts[0] || m_diagWbPkts[1] || m_diagWbOther
                ? QStringLiteral("; wideband mask %1: port 1027 (ADC1) %2 pkts / %3 frames, "
                                 "port 1028 (ADC2) %4 pkts / %5 frames, other ports %6 (last %7)")
                      .arg(int(m_wb.enableMask)).arg(m_diagWbPkts[0]).arg(m_diagWbFrames[0])
                      .arg(m_diagWbPkts[1]).arg(m_diagWbFrames[1]).arg(m_diagWbOther).arg(m_diagWbOtherPort)
                : QString());
    // 03.11: the radio sends no status while receiving (log 18:47: none
    // before the first MOX, none after the last) -- then say so instead of
    // a line of zeros that looks like a clean link.
    const bool statusNow = m_statusPackets != m_diagStatusSeen;
    m_diagStatusSeen = m_statusPackets;
    if (m_fpgaBuf.seen && !statusNow) {
        qCInfo(lcAnanP2T) << "ANAN diag radio buffers: no status packets from the radio in the last 5 s"
                          << "(none while receiving) -- no link / PHY counters for this period";
    } else if (m_fpgaBuf.seen) {
        auto& f = m_fpgaBuf;
        qCInfo(lcAnanP2T).nospace().noquote()
            << "ANAN diag radio buffers: TX "
            << (f.txMax >= 0 ? QStringLiteral("%1..%2").arg(f.txMin).arg(f.txMax) : QStringLiteral("idle"))
            << " of 8192, ran empty " << f.dTxEmpty << ", overflow " << f.dTxOvf << " samples; speaker "
            << (f.spkMax >= 0 ? QStringLiteral("%1..%2").arg(f.spkMin).arg(f.spkMax) : QStringLiteral("idle"))
            << " of 2048, fades " << f.dSpkFade << ", empty reads " << f.dSpkEmptyRd << ", overflow " << f.dSpkOvf
            << (f.linkBase ? QStringLiteral("; link: commands not applied %1, audio dropped %2, I/Q dropped %3, "
                                            "bad FCS %4, PHY RXER %5")
                                 .arg(f.dLink[0]).arg(f.dLink[1]).arg(f.dLink[2]).arg(f.dLink[3]).arg(f.dLink[4])
                           : QString())
            << (f.phyBase ? QStringLiteral("; PHY: idle errors %1, receive errors %2").arg(f.dPhyIdle).arg(f.dPhyRx)
                          : QString())
            << (f.clkDevMax != INT_MIN ? QStringLiteral("; PHY crystal %1..%2 ppm")
                                             .arg(f.clkDevMin * 8.0 / 125.0, 0, 'f', 2)
                                             .arg(f.clkDevMax * 8.0 / 125.0, 0, 'f', 2)
                                       : QString());
        f.clkDevMin = INT_MAX;
        f.clkDevMax = INT_MIN;
        f.dLink = {};
        f.dPhyIdle = f.dPhyRx = 0;
        f.txMin = f.spkMin = 1 << 30;
        f.txMax = f.spkMax = -1;
        f.dTxEmpty = f.dTxOvf = f.dSpkFade = f.dSpkEmptyRd = f.dSpkOvf = 0;
    }
    m_diagWbPkts = {{0, 0}};
    m_diagWbFrames = {{0, 0}};
    m_diagWbOther = 0;
    m_diagMicPkts = m_diagSpkPkts = m_diagSpkSilent = m_diagSpkSubmits = 0;
    m_diagSpkPeak = 0;
    m_diagClock.restart();
    m_diagSamples = m_diagGaps = m_diagLost = 0;
}

// ---- receiver commands -----------------------------------------------------

void P2Transport::setRxFrequencyHz(std::uint32_t hz)
{
    setRxFrequencyHz(0, hz);
}

void P2Transport::setRxFrequencyHz(int rxIndex, std::uint32_t hz)
{
    if (rxIndex < 0 || rxIndex >= kMaxDdcs)
        return;
    m_rxHz[static_cast<std::size_t>(rxIndex)] = hz;
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setSampleRate(hl2::SampleRate rate)
{
    m_p.sampleRate = rate;
    if (m_run && m_configured)
        sendDdcSpecific();
    // The stream's cadence changes underneath the receivers.
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
}

void P2Transport::setReceiverCount(int count)
{
    hl2::MetisClient::Params p = m_p;
    p.numRx = count;
    int n = std::clamp(effectiveNumRx(p), 1, kMaxDdcs);
    if (m_discoveredNumDdc > 0)
        n = std::min(n, receiversForBoard(m_profile, m_discoveredNumDdc));
    if (n == m_numRx)
        return;
    // New receivers start on receiver 0's frequency until Hl2Backend tunes them.
    for (int i = m_numRx; i < n; ++i)
        m_rxHz[static_cast<std::size_t>(i)] = m_rxHz[0];
    m_numRx = n;
    m_p.numRx = n;
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
    if (m_run && m_configured) {
        sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::setLnaGainDb(int db)
{
    m_p.lnaGainDb = db;
    // ANAN-100D port: the attenuators are per ADC now (setAdcAttenuation);
    // the HL2 "LNA gain" no longer drives them.
}

void P2Transport::setBandFilter(int ocFilterByte)
{
    // N2ADR filter board byte: no meaning on ANAN; Alex follows the frequency.
    m_p.ocFilterByte = static_cast<std::uint8_t>(ocFilterByte);
}

void P2Transport::setDitherRandomBits(bool dither, bool random)
{
    m_dither = dither;
    m_random = random;
    if (m_run && m_configured)
        sendDdcSpecific();
}

void P2Transport::setReceiverAdc(int rx, int adc)
{
    if (rx < 0 || rx >= kMaxDdcs)
        return;
    adc = adc == 1 ? 1 : 0;
    if (m_rxAdc[static_cast<std::size_t>(rx)] == adc)
        return;
    m_rxAdc[static_cast<std::size_t>(rx)] = adc;
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: RX" << (rx + 1) << " <- ADC" << (adc + 1);
    if (m_run && m_configured)
        sendDdcSpecific();
}

void P2Transport::setRxAntenna(int antenna)
{
    antenna = std::clamp(antenna, 0, 2);
    if (antenna == m_rxAnt)
        return;
    m_rxAnt = antenna;
    qCInfo(lcAnanP2T).noquote() << QStringLiteral("ANAN: RX antenna ANT%1").arg(antenna + 1);
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTunerStart(bool on)
{
    if (on == m_tunerStart)
        return;
    m_tunerStart = on;
    qCInfo(lcAnanP2T) << "ANAN: tuner start bit" << (on ? "ON" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setPsAttFollowsDrive(bool on)
{
    if (on == m_psAttFollowDrive)
        return;
    m_psAttFollowDrive = on;
    m_psDriveRef = on ? double(m_drive) : 0.0;
}

void P2Transport::setPureSignal(bool enabled, int feedbackSource, int attDb)
{
    feedbackSource = std::clamp(feedbackSource, 0, 2);
    attDb = std::clamp(attDb, 0, 31);
    // Not on the boards whose receivers sit on DDC0/DDC1 (BoardProfile::
    // psSupported); the profile is the default ANGELIA one until the
    // Discovery reply has configured the session.
    if (enabled && m_configured && !m_profile.psSupported) {
        qCWarning(lcAnanP2T) << "ANAN: PureSignal is not available on the" << m_profile.modelName;
        enabled = false;
    }
    if (enabled == m_psEnabled && feedbackSource == m_psSource && attDb == m_psAttDb)
        return;
    const bool changedOnOff = enabled != m_psEnabled;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);   // read by the DUC sender
        m_psEnabled = enabled;
    }
    m_psSource = feedbackSource;
    m_psAttDb = attDb;
    m_psAttNow.store(attDb, std::memory_order_relaxed);
    if (changedOnOff)
        qCInfo(lcAnanP2T) << "ANAN: PureSignal" << (enabled ? "on" : "off") << "feedback"
                          << feedbackSource << "att" << attDb << "dB";
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (changedOnOff && m_mox)
        sendDdcSpecific();
    sendHighPriority(true);
}

bool P2Transport::ducSnapshot(std::vector<double>& iq) const
{
    std::lock_guard<std::mutex> lock(m_ducSnapMutex);
    if (m_ducSnapFill < kDucSnap)
        return false;
    iq.resize(std::size_t(2 * kDucSnap));
    for (int i = 0; i < kDucSnap; ++i) {
        const int k = (m_ducSnapPos + i) % kDucSnap;
        iq[std::size_t(2 * i)] = m_ducSnap[std::size_t(2 * k)];
        iq[std::size_t(2 * i + 1)] = m_ducSnap[std::size_t(2 * k + 1)];
    }
    return true;
}

void P2Transport::setPsLoopbackTest(bool on)
{
    if (on == m_psLoopback)
        return;
    m_psLoopback = on;
    qCInfo(lcAnanP2T) << "ANAN: PureSignal loopback test" << (on ? "ON: DDC0 = DAC (ADC 2), DDC1 = DAC"
                                                                 : "off: DDC0 = feedback ADC");
    if (m_run && m_configured && m_mox && m_psEnabled)
        sendDdcSpecific();
}

void P2Transport::setPsFeedbackAdc(int adc)
{
    adc = std::clamp(adc, 0, 1);
    if (adc == m_psAdc)
        return;
    m_psAdc = adc;
    qCInfo(lcAnanP2T) << "ANAN: PureSignal feedback ADC" << adc + 1;
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (m_mox && m_psEnabled)
        sendDdcSpecific();
    sendHighPriority(true);
}

P2Transport::StatusSnapshot P2Transport::statusSnapshot() const
{
    StatusSnapshot s;
    {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        s.valid = m_statusSnapValid;
        s.b = m_statusSnap;
        if (m_statusSnapValid)
            s.ageMs = steadyMs() - m_statusSnapMs;
    }
    s.count = m_radioTxLevelSeq.load(std::memory_order_acquire);
    s.ducPacketsSent = m_ducSentTotal.load(std::memory_order_relaxed);
    s.mox = m_moxAtomic.load(std::memory_order_relaxed);
    s.drive = m_driveAtomic.load(std::memory_order_relaxed);
    return s;
}

void P2Transport::setWideband(int adcMask, int updateMs)
{
    WidebandOptions wb;
    wb.enableMask = static_cast<std::uint8_t>(adcMask & 0x03);
    wb.updateMs = static_cast<std::uint8_t>(std::clamp(updateMs, 20, 255));
    wb.packetsPerFrame = 32;
    if (wb.enableMask == m_wb.enableMask && wb.updateMs == m_wb.updateMs)
        return;
    m_wb = wb;
    for (auto& n : m_wbNext)
        n = -1;
    qCInfo(lcAnanP2T) << "ANAN: wideband" << (wb.enableMask ? "on" : "off")
                      << "mask" << int(wb.enableMask) << "every" << int(wb.updateMs) << "ms";
    if (m_run && m_configured)
        sendGeneral();
}

void P2Transport::handleWideband(int adc, std::span<const std::uint8_t> bytes)
{
    if (adc < 0 || adc > 1 || !(m_wb.enableMask & (1u << adc)))
        return;
    std::uint32_t seq = 0;
    if (!parseWidebandPacket(bytes, seq, m_wbScratch.data()))
        return;
    if (!m_wbLogged[static_cast<std::size_t>(adc)]) {
        m_wbLogged[static_cast<std::size_t>(adc)] = true;
        qCInfo(lcAnanP2T) << "ANAN: wideband packets arriving from ADC" << adc + 1 << "(first seq" << seq << ")";
    }
    const int ppf = std::max<int>(1, m_wb.packetsPerFrame);
    auto& buf = m_wbBuf[static_cast<std::size_t>(adc)];
    auto& next = m_wbNext[static_cast<std::size_t>(adc)];
    // A frame is ppf packets of contiguous samples; it starts at sequence 0
    // (Thetis network.c: the radio restarts the count every frame) -- or at
    // a multiple of ppf, should a board count on. A gap drops the frame.
    if (seq == 0 || seq % static_cast<std::uint32_t>(ppf) == 0) {
        buf.clear();
        buf.reserve(ppf * kWidebandSamplesPerPacket);
        next = static_cast<qint64>(seq);
    }
    if (next < 0 || static_cast<qint64>(seq) != next) {
        next = -1;
        return;
    }
    for (float v : m_wbScratch)
        buf.push_back(v);
    ++next;
    if (buf.size() >= ppf * kWidebandSamplesPerPacket) {
        ++m_diagWbFrames[static_cast<std::size_t>(adc)];
        emit widebandFrame(adc, buf);
        buf.clear();
        next = -1;
    }
}

void P2Transport::setOpenCollectorMatrix(const QByteArray& rx, const QByteArray& tx,
                                         const QByteArray& actions)
{
    OcMatrix m;
    for (int b = 0; b < kOcBandCount; ++b) {
        m.rx[b] = b < rx.size() ? std::uint8_t(rx.at(b) & 0x7f) : std::uint8_t{0};
        m.tx[b] = b < tx.size() ? std::uint8_t(tx.at(b) & 0x7f) : std::uint8_t{0};
    }
    for (int pin = 0; pin < kOcPinCount; ++pin) {
        const int a = pin < actions.size() ? int(std::uint8_t(actions.at(pin))) : 2;
        m.action[pin] = a == 0 ? OcTxAction::Mox : a == 1 ? OcTxAction::Tune : OcTxAction::MoxTune;
    }
    m_oc = m;
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setLna(bool on)
{
    if (on == m_lna)
        return;
    m_lna = on;
    qCInfo(lcAnanP2T) << "ANAN: LNA (Alex preamp bit)" << (on ? "ON" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTxAntenna(int antenna)
{
    antenna = std::clamp(antenna, 0, 2);
    if (antenna == m_txAnt)
        return;
    m_txAnt = antenna;
    qCInfo(lcAnanP2T).noquote() << QStringLiteral("ANAN: TX antenna ANT%1").arg(antenna + 1);
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setAdcAttenuation(int adc, int db)
{
    if (adc < 0 || adc > 1)
        return;
    db = std::clamp(db, 0, kMaxStepAttenuationDb);
    if (m_adcAtt[static_cast<std::size_t>(adc)] == db)
        return;
    m_adcAtt[static_cast<std::size_t>(adc)] = db;
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setLocalCodec(bool) {}

void P2Transport::submitSpeakerAudio(const QByteArray& interleavedInt16)
{
    // 48 kHz stereo int16 from Hl2Backend's codec mixer (I/O thread). As
    // Thetis does (ChannelMaster obbuffs -> network.c), a 64-frame packet
    // leaves as soon as it is complete, from the speaker sender thread; the
    // audio is produced from the radio's own IQ stream, so the rate is the
    // radio's and the codec FIFO in the FPGA absorbs the bursts.
    const auto* s = reinterpret_cast<const std::int16_t*>(interleavedInt16.constData());
    const std::size_t n = static_cast<std::size_t>(interleavedInt16.size()) / sizeof(std::int16_t);
    ++m_diagSpkSubmits;
    for (std::size_t i = 0; i < n; ++i)
        m_diagSpkPeak = std::max(m_diagSpkPeak, std::abs(int(s[i])));
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.insert(m_spkQueue.end(), s, s + (n & ~std::size_t{1}));
        while (m_spkQueue.size() > kSpkQueueMax)
            m_spkQueue.pop_front();
    }
    m_spkCv.notify_one();
}

void P2Transport::clearSpeakerAudio()
{
    std::lock_guard<std::mutex> lock(m_spkMutex);
    m_spkQueue.clear();
    m_spkPrimed = false;
}

qint64 P2Transport::sendNative(const std::uint8_t* data, std::size_t len, quint16 port)
{
    std::shared_lock<std::shared_mutex> guard(m_sendGuard);
    if (m_nativeFd < 0)
        return -1;
    const qint64 sent = nativeUdpSendTo(m_nativeFd, m_nativeIp, port, data, len);
    if (sent > 0) {
        m_nativeTxBytes.fetch_add(static_cast<quint64>(sent), std::memory_order_relaxed);
        m_nativeTxPackets.fetch_add(1, std::memory_order_relaxed);
    }
    return sent;
}

void P2Transport::speakerSenderLoop()
{
    // Speaker sender thread ("anan-spk"): time critical (MMCSS Pro Audio on
    // Windows). Waits for complete 64-frame packets and sends each at once.
    raiseCurrentThreadToTimeCritical();
    std::int16_t buf[2 * kCodecFramesPerPacket];
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_spkMutex);
            constexpr std::size_t kPkt = 2 * kCodecFramesPerPacket;
            m_spkCv.wait(lock, [this] { return m_spkQuit || m_spkQueue.size() >= kPkt; });
            if (m_spkQuit)
                return;
            for (auto& v : buf) {
                v = m_spkQueue.front();
                m_spkQueue.pop_front();
            }
        }
        if (!m_sessionUp.load(std::memory_order_acquire))
            continue;
        const auto pkt = buildSpeakerAudio(m_spkSeq++, std::span<const std::int16_t>(buf, std::size(buf)));
        if (sendNative(pkt.data(), pkt.size(), kSpeakerAudioPort) < 0)
            m_nativeSendFails.fetch_add(1, std::memory_order_relaxed);
        m_diagSpkPkts.fetch_add(1, std::memory_order_relaxed);
    }
}

void P2Transport::onSpeakerPacerTick()
{
    // ANAN-100D port: unused -- speaker packets leave from the speaker sender
    // thread as they are produced (Thetis behaviour); the timer never starts.
}

void P2Transport::setTxAttenuation(int db)
{
    db = std::clamp(db, 0, 31);
    if (db == m_txAttDb)
        return;
    m_txAttDb = db;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: RX attenuation while transmitting" << db << "dB";
    if (m_run && m_configured)
        sendTransmitSpecific();
}

void P2Transport::setMicBoost(bool on)
{
    if (on == m_micBoost)
        return;
    m_micBoost = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: mic boost" << (on ? "+20 dB" : "off");
    if (m_run && m_configured)
        sendTransmitSpecific();
}
void P2Transport::setMicControl(int bits)
{
    const auto b = static_cast<std::uint8_t>(bits & (kMicPttDisabled | kMicTipIsPtt | kMicBiasOn | kMicXlr));
    if (b == m_micControl)
        return;
    m_micControl = b;
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: mic PTT " << ((b & kMicPttDisabled) ? "off" : "on")
                                << ", PTT on " << ((b & kMicTipIsPtt) ? "tip" : "ring")
                                << ", bias " << ((b & kMicBiasOn) ? "on" : "off")
                                << ", input " << ((b & kMicXlr) ? "XLR" : "3.5 mm");
    if (m_run && m_configured)
        sendTransmitSpecific();
}

void P2Transport::setAdc1GroundOnTx(bool on)
{
    if (on == m_adc1GndOnTx)
        return;
    m_adc1GndOnTx = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC2 input grounded on TX" << (on ? "on" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setCl1RefClock(bool) {}
void P2Transport::setAtuTuneRequest(bool) {}
void P2Transport::setIoBoardTxFrequencyHz(quint64) {}

void P2Transport::requestPipelineReset()
{
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
}

bool P2Transport::requestRegister(int addr, quint32, bool)
{
    // HL2 I2C/register access has no Protocol 2 counterpart.
    QMetaObject::invokeMethod(this, [this, addr] { emit controlRequestFailed(addr, true); },
                              Qt::QueuedConnection);
    return false;
}

void P2Transport::requestIoBoardStatus()
{
    QMetaObject::invokeMethod(this, [this] {
        emit ioBoardStatusFailed(QStringLiteral("no IO board on ANAN"));
    }, Qt::QueuedConnection);
}

void P2Transport::setBandscopeEnabled(bool) {}

void P2Transport::requestBandscopeFrame()
{
    QMetaObject::invokeMethod(this, [this] {
        emit bandscopeFrameFailed(QStringLiteral("wideband view not implemented for ANAN yet"));
    }, Qt::QueuedConnection);
}

// ---- transmit -----------------------------------------------------------

void P2Transport::sendTransmitSpecific()
{
    if (!m_sock)
        return;
    sendDatagram(*m_sock, buildTransmitSpecific(m_txSpecSeq++, m_micBoost, m_cwKeyer, m_txAttDb,
                                                m_psEnabled ? m_psAttDb : -1, m_psAdc, m_micControl),
                 m_p.host, kTransmitSpecificPort,
                 m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::setMox(bool keyed, const TxCoordinator::Operation& operation)
{
    if (!TxCoordinator::Command{operation, keyed}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    if (keyed && !transmitEnabled())
        keyed = false;
    std::unique_lock<std::mutex> txLock(m_txMutex);
    if (keyed == m_mox)
        return;
    m_mox = keyed;
    m_moxAtomic.store(keyed, std::memory_order_relaxed);
    if (keyed && m_tlPressMs >= 0 && m_tlMoxMs < 0)
        m_tlMoxMs = m_tlClock.elapsed();
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: MOX " << (keyed ? "ON" : "off")
                                << " at " << (m_txHz ? m_txHz : m_rxHz[0]) << " Hz, drive "
                                << m_drive;
    if (keyed) {
        m_hwDucPk = m_hwFbPk = 0.0;
        m_hwDucLast.store(0.0, std::memory_order_relaxed);
        m_hwDucN = m_hwFbN = 0;
        m_scopeFbPk = m_scopeFbGain = 0.0;
        m_scopeFromFb.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_ducSnapMutex);
            m_ducSnapFill = 0;
        }
        beginTxStream();
    } else {
        m_txTimer->stop();
        m_txQueue.clear();
        m_cwEnv = 0.0;
        logTxOver();
        if (m_txUnderflows)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: TX underflow packets this over:" << m_txUnderflows;
        if (m_psEnabled)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: PureSignal feedback packets (total):" << m_psPackets;
    }
    txLock.unlock();
    if (m_run && m_configured) {
        // PureSignal: DDC0/DDC1 become the feedback pair only while MOX.
        if (m_psEnabled)
            sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::unkeyOnLinkLoss()
{
    std::unique_lock<std::mutex> txLock(m_txMutex);
    const bool wasKeyed = m_mox || m_cwDown;
    m_cwDown = false;
    if (!m_mox) {
        txLock.unlock();
        if (wasKeyed && m_run && m_configured)
            sendHighPriority(true);
        return;
    }
    m_mox = false;
    m_moxAtomic.store(false, std::memory_order_relaxed);
    qCWarning(lcAnanP2T).nospace() << "ANAN P2 transport: MOX off -- the link to the radio was lost while "
                                      "transmitting; it does not come back on by itself";
    m_txTimer->stop();
    m_txQueue.clear();
    m_cwEnv = 0.0;
    logTxOver();
    txLock.unlock();
    if (m_run && m_configured) {
        if (m_psEnabled)
            sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::logTxOver()
{
    // One line per transmission: how loud the DUC IQ was, relative to full
    // scale (|IQ| = 1, a full-scale TUNE carrier = 0 dBFS). Mean power is
    // what an ammeter / average wattmeter follows; peak is PEP.
    if (m_ovCount == 0)
        return;
    auto db = [](double p) { return 10.0 * std::log10(std::max(p, 1e-20)); };
    const double mean = m_ovSumSq / double(m_ovCount);
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: TX over " << QString::number(m_ovCount / double(kDucSampleRateHz), 'f', 1)
        << " s: IQ peak " << QString::number(db(m_ovPeak), 'f', 1)
        << " dBFS, mean " << QString::number(db(mean), 'f', 1)
        << " dBFS, loudest 100 ms " << QString::number(db(m_ovWinMax), 'f', 1)
        << " dBFS, drive " << m_drive << "/255"
        << (m_toneAmp > 0.0 ? " (tone)" : m_cwMode ? " (host CW)" : "");
    QString tr;
    for (double v : m_ovTrace)
        tr += QString::number(db(v), 'f', 0) + QLatin1Char(' ');
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: TX IQ mean per 250 ms [" << tr << "] dBFS";
    m_ovTrace.clear();
    m_ovTrSum = 0.0;
    m_ovTrN = 0;
    m_ovCount = 0;
}

void P2Transport::setCwMox(bool keyed, const TxCoordinator::Operation& operation)
{
    setMox(keyed, operation);
}

void P2Transport::beginTxStream()
{
    m_txPrimed = false;
    m_txUnderflows = 0;
    m_hostZeroRun = 0;
    m_hostZeroRuns = m_hostZeroRunsLogged = 0;
    m_txPacketsSent = 0;
    m_txSamplesOut = 0;
    m_txRise = 0;
    // Nothing from before this key: IQ the modulator finished after the last
    // unkey (queued behind it) would otherwise open this over.
    m_txQueue.clear();
    m_txScope.reset();
    m_ovPeak = m_ovSumSq = m_ovWinSumSq = m_ovWinMax = 0.0;
    m_ovTrace.clear();
    m_ovTrSum = 0.0;
    m_ovTrN = 0;
    m_ovCount = 0;
    m_ovWinCount = 0;
    m_txUnderflowsLogged = 0;
    // The radio's clock (txDeviceTick) paces from here; the lead goes out at
    // once. The 2 ms host timer only stands in without a mic stream.
    m_txDevSamples = 0;
    m_leadAdj.store(0, std::memory_order_relaxed);
    m_txFallbackCredit = 0;
    m_txFallbackActive = false;
    m_micProcessed = m_micArrived.load(std::memory_order_relaxed);   // no backlog from before the key
    m_txFallbackClock.start();
    m_txFallbackNs = 0;
    txPumpLocked();
    m_txTimer->start();
}

static_assert(kDucSampleRateHz == anan::TxScope::kSampleRateHz,
              "TxScope's frequency axis assumes the DUC rate");

void P2Transport::fillTxPacket(std::complex<float>* out)
{
    constexpr int n = kDucSamplesPerPacket;
    constexpr double fs = kDucSampleRateHz;
    if (m_toneAmp > 0.0) {
        const double step = 2.0 * std::numbers::pi * m_toneHz / fs;
        for (int i = 0; i < n; ++i) {
            out[i] = {static_cast<float>(m_toneAmp * std::cos(m_tonePhase)),
                      static_cast<float>(m_toneAmp * std::sin(m_tonePhase))};
            m_tonePhase += step;
            if (m_tonePhase > 2.0 * std::numbers::pi)
                m_tonePhase -= 2.0 * std::numbers::pi;
        }
        return;
    }
    if (m_cwMode) {
        // Host CW: a carrier at the DUC frequency, 5 ms raised ramps.
        const double rampStep = 1.0 / (0.005 * fs);
        for (int i = 0; i < n; ++i) {
            m_cwEnv += m_cwDown ? rampStep : -rampStep;
            m_cwEnv = std::clamp(m_cwEnv, 0.0, 1.0);
            const double shaped = 0.5 - 0.5 * std::cos(std::numbers::pi * m_cwEnv);
            out[i] = {static_cast<float>(shaped), 0.0f};
        }
        return;
    }
    // ANAN-100D port (operator, 31.10): voice starts when the transmitter is
    // ready -- RF Delay (byte 13, TX applet) after MOX -- and no later. Until
    // then silence goes out, and the queue keeps only the newest prime's
    // worth of modulation, so the first audio after the delay is current
    // (no backlog, no added latency); then a 2 ms rise.
    {
        const std::uint64_t delaySamples = static_cast<std::uint64_t>(
            std::max(0, m_rfDelayMs.load(std::memory_order_relaxed))) * kDucSampleRateHz / 1000;
        if (m_txSamplesOut < delaySamples) {
            while (m_txQueue.size() > static_cast<std::size_t>(m_txPrime.load(std::memory_order_relaxed)))
                m_txQueue.pop_front();
            std::fill(out, out + n, std::complex<float>{});
            m_txSamplesOut += static_cast<std::uint64_t>(n);
            m_txRise = 0;
            return;
        }
    }
    if (!m_txPrimed && m_txQueue.size() >= static_cast<std::size_t>(m_txPrime.load(std::memory_order_relaxed)))
        m_txPrimed = true;
    if (!m_txPrimed || m_txQueue.size() < static_cast<std::size_t>(n)) {
        if (m_txPrimed) {
            // Counted here (time-critical thread); logged by the I/O thread.
            ++m_txUnderflows;
            m_txPrimed = false;   // re-prime before using the queue again
        }
        std::fill(out, out + n, std::complex<float>{});
        return;
    }
    constexpr int kRise = kDucSampleRateHz / 500;   // 2 ms
    for (int i = 0; i < n; ++i) {
        out[i] = m_txQueue.front();
        m_txQueue.pop_front();
        if (m_txRise < kRise) {
            const float w = 0.5f - 0.5f * std::cos(3.14159265f * float(m_txRise) / float(kRise));
            out[i] *= w;
            ++m_txRise;
        }
    }
    m_txSamplesOut += static_cast<std::uint64_t>(n);
}

void P2Transport::onTxPacerTick()
{
    // I/O thread, every 2 ms while MOX. Normally only bookkeeping: the radio's
    // microphone stream drives the DUC (txDeviceTick). Without it for 30 ms
    // (a radio that sends none, or the transmit thread held up) the host's
    // clock advances the DUC instead, so the transmitter never starves.
    quint64 underflows = 0;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        if (!m_mox)
            return;
        const qint64 ns = m_txFallbackClock.nsecsElapsed();
        const qint64 sinceDev = steadyMs() - m_lastDevTickMs.load(std::memory_order_relaxed);
        if (sinceDev > 30) {
            // On the first stand-in tick, the whole time since the last mic
            // packet (bounded by this over); later ticks, the tick itself.
            const qint64 fromNs = m_txFallbackActive
                ? m_txFallbackNs
                : std::max<qint64>(ns - sinceDev * 1000000LL, 0);
            m_txFallbackActive = true;
            const quint64 add = static_cast<quint64>(double(ns - fromNs) * 1e-9 * kDucSampleRateHz);
            m_txDevSamples += add;
            // Time the host has stood in for: mic packets that were only late
            // (not absent) arrive in a burst afterwards and must not count it
            // twice (a fake-radio test drained the queue that way). Capped,
            // so a radio without a mic stream does not bank a long silence.
            m_txFallbackCredit = std::min<quint64>(m_txFallbackCredit + add, kDucSampleRateHz / 20);
            txPumpLocked();
        } else {
            m_txFallbackActive = false;
        }
        m_txFallbackNs = ns;
        if (m_txUnderflows != m_txUnderflowsLogged) {
            underflows = m_txUnderflows;
            m_txUnderflowsLogged = m_txUnderflows;
        }
        if (m_hostZeroRuns != m_hostZeroRunsLogged) {
            qCInfo(lcAnanP2T) << "ANAN P2 transport: the HOST sent a silent run in the DUC stream:"
                              << m_hostZeroLastLen << "samples below -80 dBFS, ending in DUC packet"
                              << m_hostZeroLastSeq << ", queue" << m_hostZeroLastQueue << "samples; runs this over:"
                              << m_hostZeroRuns;
            m_hostZeroRunsLogged = m_hostZeroRuns;
        }
        if (m_txLateTicks != m_txLateLogged) {
            qCInfo(lcAnanP2T) << "ANAN P2 transport: transmit thread behind the radio --" << m_txLateTicks - m_txLateLogged
                              << "mic packet(s) of air time skipped, not sent late";
            m_txLateLogged = m_txLateTicks;
        }
    }
    if (underflows)
        qCInfo(lcAnanP2T) << "ANAN P2 transport: TX underflow" << underflows
                          << "-- the modulator was late; DUC zeros until re-primed";
}

void P2Transport::txDeviceTick(int ducSamples)
{
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_lastDevTickMs.store(steadyMs(), std::memory_order_relaxed);
    const quint64 packets = static_cast<quint64>(std::max(ducSamples, 0)) / 256;
    m_micProcessed += packets;
    if (!m_mox || ducSamples <= 0)
        return;
    quint64 add = static_cast<quint64>(ducSamples);
    const quint64 absorbed = std::min(add, m_txFallbackCredit);   // already stood in for
    m_txFallbackCredit -= absorbed;
    add -= absorbed;
    if (add == 0)
        return;
    // The transmit thread is far behind the radio (more than 16 mic packets,
    // 21 ms, still waiting -- beyond what the radio's 4096-sample DUC FIFO
    // can take as a catch-up burst): this time has passed on the air. Its IQ
    // is not sent late; time and queue move on, and the lead is
    // re-established once the backlog is down. Smaller backlogs are sent as
    // a burst (they fit the FIFO; dropping them would cost an underflow).
    const quint64 arrived = m_micArrived.load(std::memory_order_relaxed);
    if (arrived > m_micProcessed + 16) {
        m_txDevSamples += add;
        for (quint64 k = 0; k < add && !m_txQueue.empty(); ++k)
            m_txQueue.pop_front();
        const quint64 due = m_txDevSamples / kDucSamplesPerPacket + kTxLeadPackets;
        if (m_txPacketsSent + kTxLeadPackets < due)
            m_txPacketsSent = due - kTxLeadPackets;
        ++m_txLateTicks;
        return;
    }
    m_txDevSamples += add;
    txPumpLocked();
}

void P2Transport::txPumpLocked()
{
    if (!m_mox || !m_sessionUp.load(std::memory_order_acquire))
        return;
    const quint64 lead = quint64(kTxLeadPackets + m_leadAdj.load(std::memory_order_relaxed));
    const quint64 due = m_txDevSamples / kDucSamplesPerPacket + lead;
    // Held up for longer than the FIFO lead covers (the radio has played
    // zeros meanwhile): do not burst the missed packets into its FIFO, just
    // re-establish the lead from now.
    if (due > m_txPacketsSent + lead + 8)
        m_txPacketsSent = due - lead;
    std::complex<float> buf[kDucSamplesPerPacket];
    int burst = 0;
    while (m_txPacketsSent < due && burst < 32) {
        fillTxPacket(buf);
        // Live = primed and 25 ms past the RF Delay (the start and the 2 ms
        // rise are silent by design; an underflow is counted on its own).
        const std::uint64_t liveFrom = static_cast<std::uint64_t>(
            std::max(0, m_rfDelayMs.load(std::memory_order_relaxed))) * kDucSampleRateHz / 1000
            + kDucSampleRateHz / 40;
        const bool live = m_txPrimed && m_txSamplesOut > liveFrom && !m_cwMode && m_toneAmp <= 0.0;
        for (const auto& v : buf) {
            const double p = double(v.real()) * v.real() + double(v.imag()) * v.imag();
            if (live && p < 1e-8) {   // |x| < 1e-4 of full scale (-80 dBFS)
                ++m_hostZeroRun;
            } else {
                if (m_hostZeroRun >= 64) {
                    ++m_hostZeroRuns;
                    m_hostZeroLastLen = m_hostZeroRun;
                    m_hostZeroLastSeq = m_txSeq;
                    m_hostZeroLastQueue = m_txQueue.size();
                }
                m_hostZeroRun = 0;
            }
            m_ovPeak = std::max(m_ovPeak, p);
            m_ovSumSq += p;
            m_hwDucPk = std::max(m_hwDucPk, p);
            if (++m_hwDucN == kDucSampleRateHz / 2) {   // 0.5 s (PS HW peak)
                m_hwDucLast.store(m_hwDucPk, std::memory_order_relaxed);
                m_hwDucPk = 0.0;
                m_hwDucN = 0;
            }
            m_ovWinSumSq += p;
            m_envPeak = std::max(m_envPeak, p);
            if (++m_envN == kDucSampleRateHz / 20) {   // 50 ms
                emit txEnvelopePeak(static_cast<float>(10.0 * std::log10(std::max(m_envPeak, 1e-14))));
                m_envPeak = 0.0;
                m_envN = 0;
            }
            m_ovTrSum += p;
            if (++m_ovTrN == kDucSampleRateHz / 4) {   // 250 ms trace
                if (m_ovTrace.size() < 24)
                    m_ovTrace.push_back(m_ovTrSum / m_ovTrN);
                m_ovTrSum = 0.0;
                m_ovTrN = 0;
            }
            if (++m_ovWinCount == kDucSampleRateHz / 10) {   // 100 ms
                m_ovWinMax = std::max(m_ovWinMax, m_ovWinSumSq / m_ovWinCount);
                m_ovWinSumSq = 0.0;
                m_ovWinCount = 0;
            }
        }
        m_ovCount += kDucSamplesPerPacket;
        {
            std::lock_guard<std::mutex> lock(m_ducSnapMutex);
            if (m_ducSnap.size() != std::size_t(2 * kDucSnap))
                m_ducSnap.assign(std::size_t(2 * kDucSnap), 0.0);
            for (const auto& v : buf) {
                m_ducSnap[std::size_t(2 * m_ducSnapPos)] = v.real();
                m_ducSnap[std::size_t(2 * m_ducSnapPos + 1)] = v.imag();
                m_ducSnapPos = (m_ducSnapPos + 1) % kDucSnap;
            }
            m_ducSnapFill = std::min(kDucSnap, m_ducSnapFill + kDucSamplesPerPacket);
        }
        // The TX panorama shows exactly these samples (TxScope applies the
        // same clamp as buildDucIq), whatever produced them.
        if (m_scopeFromFb.load(std::memory_order_relaxed)
            && (!m_psEnabled || steadyMs() - m_scopeFbMs.load(std::memory_order_relaxed) > 200))
            m_scopeFromFb.store(false, std::memory_order_relaxed);   // the feedback stopped: back to the DUC IQ
        if (!m_scopeFromFb.load(std::memory_order_relaxed)
            && m_txScope.push(buf, kDucSamplesPerPacket, m_txScopeBins)) {
            emit txScopeFrameReady(QList<float>(m_txScopeBins.begin(), m_txScopeBins.end()),
                                   static_cast<double>(m_txHz));
        }
        const auto pkt = buildDucIq(m_txSeq++, std::span<const std::complex<float>>(buf, kDucSamplesPerPacket));
        if (sendNative(pkt.data(), pkt.size(), kDucIqPort) < 0)
            m_nativeSendFails.fetch_add(1, std::memory_order_relaxed);
        ++m_txPacketsSent;
        m_ducSentTotal.fetch_add(1, std::memory_order_relaxed);
        ++burst;
    }
}

void P2Transport::setTxFrequencyHz(std::uint32_t hz)
{
    if (hz == m_txHz)
        return;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);   // read by the DUC sender
        m_txHz = hz;
    }
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTxDriveLevel(int level)
{
    if (!transmitEnabled())
        level = 0;
    level = std::clamp(level, 0, 255);
    if (level == m_drive)
        return;
    m_drive = level;
    m_driveAtomic.store(level, std::memory_order_relaxed);
    // The feedback amplitude is proportional to the drive (the drive byte
    // scales the DAC output): a change of D dB is met by D dB more feedback
    // attenuation, in whole dB once it reaches 1 dB.
    bool attChanged = false;
    if (m_psAttFollowDrive && m_psEnabled && level > 0) {
        if (m_psDriveRef <= 0.0) {
            m_psDriveRef = level;
        } else {
            const double predDb = 20.0 * std::log10(double(level) / m_psDriveRef);
            if (std::abs(predDb) >= 1.0) {
                const int step = int(std::lround(predDb));
                m_psDriveRef *= std::pow(10.0, step / 20.0);
                const int newAtt = std::clamp(m_psAttDb + step, 0, 31);
                if (newAtt != m_psAttDb) {
                    m_psAttDb = newAtt;
                    m_psAttNow.store(newAtt, std::memory_order_relaxed);
                    attChanged = true;
                }
            }
        }
    } else if (level <= 0) {
        m_psDriveRef = 0.0;
    }
    if (m_run && m_configured) {
        if (attChanged)
            sendTransmitSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::setCwKeyDown(bool down, const TxCoordinator::Operation& operation)
{
    if (!TxCoordinator::Command{operation, down}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    if (down && !transmitEnabled())
        return;
    std::lock_guard<std::mutex> lock(m_txMutex);
    if (!down && !m_cwMode)
        return;
    if (down && !m_cwMode) {
        m_txQueue.clear();
        m_cwEnv = 0.0;
    }
    m_cwMode = true;
    m_cwDown = down;
}

void P2Transport::clearCwKeying()
{
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_cwMode = false;
    m_cwDown = false;
    m_cwEnv = 0.0;
}

void P2Transport::queueTxIq(std::span<const std::complex<float>> iq,
                            const TxCoordinator::Context& context)
{
    if (!context.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    // Any thread (the transmit thread, AnanTxEngine).
    std::lock_guard<std::mutex> lock(m_txMutex);
    if (!m_mox)
        return;   // unkeyed: a late block of the last over, not the next one's
    if (!m_txContext.sameContext(context)) {
        m_txQueue.clear();
        m_txContext = context;
    }
    for (const auto& s : iq)
        m_txQueue.push_back(s);
    while (m_txQueue.size() > kTxQueueMax)
        m_txQueue.pop_front();
}

void P2Transport::flushTxIq()
{
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_txQueue.clear();
    m_txPrimed = false;
}

void P2Transport::setTxTestTone(double offsetHz, double amplitude,
                                const TxCoordinator::Operation& operation)
{
    if (!TxCoordinator::Command{operation, amplitude > 0.0}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_toneHz = offsetHz;
    m_toneAmp = std::clamp(amplitude, 0.0, 1.0);
    if (m_toneAmp == 0.0)
        m_tonePhase = 0.0;
}

}  // namespace AetherSDR::anan
