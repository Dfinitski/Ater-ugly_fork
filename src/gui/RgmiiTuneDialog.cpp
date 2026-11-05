#include "gui/RgmiiTuneDialog.h"

#include "core/AppSettings.h"
#include "core/backends/anan/P2Transport.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace AetherSDR {

namespace {
long long msNow() { return QDateTime::currentMSecsSinceEpoch(); }
constexpr long long kSettleMs = 300;      // after the radio reports the value
constexpr long long kApplyTimeoutMs = 3000;
constexpr double kStopRate = 0.005;       // errors per DUC packet ending a direction
constexpr int kIdxIq = 2, kIdxFcs = 3, kIdxRxer = 4, kIdxPhyIdle = 5, kIdxPhyRx = 6;
}  // namespace

void RgmiiTuneDialog::scaleToDelays(int s, int& clk, int& data)
{
    s = std::clamp(s, kSMin, kSMax);
    if (s > 16) {
        clk = 31;
        data = 7 - (s - 16);
    } else if (s < -15) {
        clk = 0;
        data = 7 + (-15 - s);
    } else {
        clk = 15 + s;
        data = 7;
    }
}

RgmiiTuneDialog::RgmiiTuneDialog(anan::P2Transport* transport, QWidget* parent)
    : PersistentDialog(tr("RGMII: receive tuning"), QStringLiteral("RgmiiTuneGeometry"), parent)
    , m_t(transport)
{
    auto* top = new QVBoxLayout(bodyWidget());
    top->setSpacing(6);

    auto* ctl = new QGroupBox(tr("Radio receive delays (KSZ9031)"));
    auto* g = new QGridLayout(ctl);
    m_manual = new QCheckBox(tr("Manual delays"));
    m_manual->setToolTip(tr("Off: the firmware uses its default delays."));
    g->addWidget(m_manual, 0, 0, 1, 3);
    g->addWidget(new QLabel(tr("Clock delay (RX_CLK)")), 1, 0);
    m_clk = new QSpinBox;
    m_clk->setRange(0, 31);
    g->addWidget(m_clk, 1, 1);
    m_clkPs = new QLabel;
    g->addWidget(m_clkPs, 1, 2);
    g->addWidget(new QLabel(tr("Data delay (RXD)")), 2, 0);
    m_data = new QSpinBox;
    m_data->setRange(0, 15);
    g->addWidget(m_data, 2, 1);
    m_dataPs = new QLabel;
    g->addWidget(m_dataPs, 2, 2);
    m_now = new QLabel;
    m_now->setTextFormat(Qt::RichText);
    g->addWidget(m_now, 3, 0, 1, 3);
    top->addWidget(ctl);

    auto* cnt = new QGroupBox(tr("Radio receive errors (since "Reset counters")"));
    auto* cl = new QVBoxLayout(cnt);
    m_counters = new QLabel;
    m_counters->setTextFormat(Qt::RichText);
    cl->addWidget(m_counters);
    m_reset = new QPushButton(tr("Reset counters"));
    cl->addWidget(m_reset);
    m_count = new QPushButton(tr("Count 4000 packets"));
    m_count->setToolTip(tr("Errors per ~4000 I/Q packets at the current power; compare zero power with normal.\n"
                           "Almost no errors without RF means transmitter pickup."));
    cl->addWidget(m_count);
    connect(m_count, &QPushButton::clicked, this, [this] {
        if (!m_t)
            return;
        const auto s = m_t->rgmiiStatus();
        if (!s.mox || !s.linkCounters) {
            logLine(tr("Count: needs a transmission (I/Q packets) and the radio counters."));
            return;
        }
        m_counting = true;
        m_countBase = m_total;
        m_countBaseDuc = s.ducPacketsSent;
        m_countDrive = s.drive;
        m_count->setEnabled(false);
        logLine(tr("Count: started, drive %1 of 255 ...").arg(s.drive));
    });
    top->addWidget(cnt);

    auto* at = new QGroupBox(tr("Auto-tune"));
    auto* al = new QGridLayout(at);
    auto* how = new QLabel(tr("Needs an I/Q packet stream: transmit into a dummy load at low power "
                              "(or at zero drive). 60 ps steps from the middle up, then down, until the errors "
                              "exceed 0.5 % of the packets; the middle of the widest error-free window wins."));
    how->setWordWrap(true);
    al->addWidget(how, 0, 0, 1, 3);
    al->addWidget(new QLabel(tr("Time per step, s")), 1, 0);
    m_dwell = new QSpinBox;
    m_dwell->setRange(2, 60);
    m_dwell->setValue(5);
    al->addWidget(m_dwell, 1, 1);
    m_auto = new QPushButton(tr("Auto-tune"));
    al->addWidget(m_auto, 1, 2);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMinimumHeight(160);
    al->addWidget(m_log, 2, 0, 1, 3);
    top->addWidget(at, 1);

    auto& st = AppSettings::instance();
    {
        const QSignalBlocker b1(m_manual), b2(m_clk), b3(m_data);
        m_manual->setChecked(st.value(QStringLiteral("AnanRgmiiManual"), QStringLiteral("False")).toString()
                             == QStringLiteral("True"));
        m_clk->setValue(std::clamp(st.value(QStringLiteral("AnanRgmiiClk"), 15).toInt(), 0, 31));
        m_data->setValue(std::clamp(st.value(QStringLiteral("AnanRgmiiData"), 7).toInt(), 0, 15));
    }
    auto changed = [this] {
        if (m_phase == Phase::Idle)
            sendNow();
    };
    connect(m_manual, &QCheckBox::toggled, this, changed);
    connect(m_clk, qOverload<int>(&QSpinBox::valueChanged), this, changed);
    connect(m_data, qOverload<int>(&QSpinBox::valueChanged), this, changed);
    connect(m_reset, &QPushButton::clicked, this, &RgmiiTuneDialog::resetCounters);
    connect(m_auto, &QPushButton::clicked, this, [this] {
        if (m_phase == Phase::Idle)
            startAuto();
        else
            stopAuto(tr("stopped by hand"));
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &RgmiiTuneDialog::tick);
    m_timer->start();
    sendNow();
    tick();
}

void RgmiiTuneDialog::sendNow()
{
    const bool manual = m_manual->isChecked();
    const int clk = m_clk->value(), data = m_data->value();
    m_clkPs->setText(tr("%1 ps").arg((clk - 15) * 60));
    m_dataPs->setText(tr("%1 ps").arg((data - 7) * 60));
    m_clk->setEnabled(manual && m_phase == Phase::Idle);
    m_data->setEnabled(manual && m_phase == Phase::Idle);
    auto& st = AppSettings::instance();
    st.setValue(QStringLiteral("AnanRgmiiManual"), manual ? QStringLiteral("True") : QStringLiteral("False"));
    st.setValue(QStringLiteral("AnanRgmiiClk"), clk);
    st.setValue(QStringLiteral("AnanRgmiiData"), data);
    if (m_t)
        QMetaObject::invokeMethod(m_t, "setRgmiiDelays", Qt::QueuedConnection, Q_ARG(bool, manual),
                                  Q_ARG(int, clk), Q_ARG(int, data));
    m_lastResendMs = msNow();
}

void RgmiiTuneDialog::resetCounters()
{
    m_zero = m_total;
    m_zeroDuc = m_ducNow;
    m_fcsHist.clear();
    tick();
}

void RgmiiTuneDialog::logLine(const QString& t)
{
    m_log->appendPlainText(t);
}

void RgmiiTuneDialog::tick()
{
    if (!m_t) {
        m_now->setText(tr("<span style='color:#ff6060'>no link to the radio</span>"));
        return;
    }
    const auto s = m_t->rgmiiStatus();
    const long long now = msNow();
    m_ducNow = s.ducPacketsSent;
    if (s.linkCounters) {
        for (std::size_t k = 0; k < 7; ++k) {
            const unsigned v = s.raw[k];
            if (m_haveRaw)
                m_total[k] += unsigned(std::uint16_t(v - m_lastRaw[k]));
            m_lastRaw[k] = v;
        }
        if (!m_haveRaw) {
            m_zero = m_total;
            m_zeroDuc = m_ducNow;
        }
        m_haveRaw = true;
        m_fcsHist.emplace_back(now, m_total[kIdxFcs]);
        while (!m_fcsHist.empty() && now - m_fcsHist.front().first > 60000)
            m_fcsHist.pop_front();
    }
    // What the radio runs.
    const bool manual = m_manual->isChecked();
    if (!s.layout4) {
        m_now->setText(tr("<span style='color:#e0b020'>The radio does not report the delays (needs status layout 4)</span>"));
    } else {
        const bool match = manual ? (s.manualNow && s.clkNow == m_clk->value() && s.dataNow == m_data->value())
                                  : !s.manualNow;
        m_now->setText(tr("In use: clock <b>%1</b> (%2 ps), data <b>%3</b> (%4 ps), %5 %6")
                           .arg(s.clkNow).arg((s.clkNow - 15) * 60).arg(s.dataNow).arg((s.dataNow - 7) * 60)
                           .arg(s.manualNow ? tr("set by the program") : tr("default"))
                           .arg(match ? QStringLiteral("<span style='color:#30e030'>✓</span>")
                                      : tr("<span style='color:#ff6060'>— not applied yet</span>")));
        // Bytes 30/31: read back from the PHY registers; must equal 28/29.
        if (s.phyClk >= 0) {
            const bool same = s.phyClk == s.clkNow && s.phyData == s.dataNow && s.phyRxCtl == s.dataNow
                              && s.phyRxdEqual;
            QString t = m_now->text() + QStringLiteral("<br>");
            if (!s.phyFound)
                t += tr("<span style='color:#ff6060'>From the PHY: PHY not found (byte 30, bit 7 = 0)</span>");
            else if (!s.phyReadDone)
                t += tr("<span style='color:#e0b020'>From the PHY: not read yet (byte 30, bit 6 = 0)</span>");
            else
                t += tr("Read from the PHY: clock <b>%1</b>, RXD0 <b>%2</b>, RX_CTL <b>%3</b>, RXD0..3 %4 %5")
                         .arg(s.phyClk).arg(s.phyData).arg(s.phyRxCtl)
                         .arg(s.phyRxdEqual ? tr("equal") : tr("<span style='color:#ff6060'>DIFFERENT</span>"))
                         .arg(same ? QStringLiteral("<span style='color:#30e030'>✓ as in use</span>")
                                   : tr("<span style='color:#ff6060'>— differs from bytes 28/29</span>"));
            m_now->setText(t);
        }
        // Not applied: send again (the radio takes it only from a frame with a good FCS).
        if (!match && now - m_lastResendMs >= 1000 && m_phase == Phase::Idle)
            sendNow();
    }
    if (!s.linkCounters) {
        m_counters->setText(tr("The radio does not report link counters (needs status layout 3 or later)"));
    } else {
        auto d = [&](int k) { return m_total[std::size_t(k)] - m_zero[std::size_t(k)]; };
        double perMin = 0.0;
        if (m_fcsHist.size() >= 2) {
            const double span = double(m_fcsHist.back().first - m_fcsHist.front().first) / 60000.0;
            if (span > 0.05)
                perMin = double(m_fcsHist.back().second - m_fcsHist.front().second) / span;
        }
        auto c = [](unsigned long long v) {
            return v ? QStringLiteral("<span style='color:#ff6060'><b>%1</b></span>").arg(v)
                     : QStringLiteral("<b>0</b>");
        };
        const unsigned long long pk = m_ducNow - m_zeroDuc;
        const QString per1000 = pk > 0
            ? QString::number(1000.0 * double(d(kIdxFcs) + d(kIdxRxer)) / double(pk), 'f', 2)
            : QStringLiteral("--");
        m_counters->setText(tr("FCS errors: %1 &nbsp;(%2 per minute)<br>PHY errors (RXER): %3<br>"
                               "I/Q packets dropped: %4, audio: %5<br>Command packets not applied: %6<br>"
                               "I/Q packets sent: %8, errors (FCS+RXER) per 1000 packets: <b>%9</b><br>"
                               "<small>%7</small>")
                                .arg(c(d(kIdxFcs))).arg(perMin, 0, 'f', 1).arg(c(d(kIdxRxer)))
                                .arg(c(d(kIdxIq))).arg(c(d(1))).arg(c(d(0)))
                                .arg(s.mox ? tr("transmitting: I/Q packets flowing, drive %1 of 255").arg(s.drive)
                                           : tr("receiving: no I/Q packets"))
                                .arg(pk).arg(per1000));
        // Firmware v6: the PHY's own counters and state.
        if (s.phyDiag) {
            QString eee;
            if (s.eeeAdv < 0)
                eee = QStringLiteral("--");
            else if (s.eeeAdv & 0x06)
                eee = tr("<span style='color:#ff6060'><b>on at the PC</b> (%1%2)</span>")
                          .arg((s.eeeAdv & 0x04) ? QStringLiteral("1000BASE-T ") : QString())
                          .arg((s.eeeAdv & 0x02) ? QStringLiteral("100BASE-TX") : QString());
            else
                eee = tr("off at the PC");
            auto ok = [](bool v) {
                return v ? QStringLiteral("<span style='color:#30e030'>OK</span>")
                         : QStringLiteral("<span style='color:#ff6060'><b>NOT OK</b></span>");
            };
            const int gs = std::max(0, s.gbStatus);
            QString xtal;
            if (s.phyClkDevHz8 != INT_MIN) {
                const double ppm = s.phyClkDevHz8 * 8.0 / 125.0;
                const QString col = std::abs(ppm) > 50.0 ? QStringLiteral("#ff6060")
                                    : std::abs(ppm) > 25.0 ? QStringLiteral("#e0b020")
                                                           : QStringLiteral("#30e030");
                xtal = tr("<br>PHY crystal (125 MHz against C122): <span style='color:%1'><b>%2 ppm</b></span> (%3 Hz; "
                          "1000BASE-T allows ±50 ppm)")
                           .arg(col).arg(ppm, 0, 'f', 2).arg(s.phyClkDevHz8 * 8);
            }
            m_counters->setText(m_counters->text()
                                + tr("<br><b>PHY:</b> 1000BASE-T idle errors %1, KSZ9031 receive errors %2"
                                     "<br>EEE: %3<br>1000BASE-T: radio is clock %4, local receiver %5, "
                                     "remote %6")
                                      .arg(c(d(kIdxPhyIdle))).arg(c(d(kIdxPhyRx))).arg(eee)
                                      .arg((gs & 0x40) ? tr("master") : tr("slave"))
                                      .arg(ok(gs & 0x20)).arg(ok(gs & 0x10))
                                + xtal);
        }
        if (m_counting) {
            const unsigned long long n = s.ducPacketsSent - m_countBaseDuc;
            if (!s.mox) {
                m_counting = false;
                m_count->setEnabled(true);
                logLine(tr("Count: aborted (transmission stopped after %1 packets).").arg(n));
            } else if (n >= kCountPackets) {
                const auto e = [&](int k) { return m_total[std::size_t(k)] - m_countBase[std::size_t(k)]; };
                logLine(tr("Count: %1 packets at drive %2%3: FCS %4, RXER %5, I/Q dropped %6, audio %7 "
                           "-- %8 errors per 1000 packets%9")
                            .arg(n).arg(m_countDrive)
                            .arg(m_countDrive != s.drive ? tr(" (changed, now %1)").arg(s.drive) : QString())
                            .arg(e(kIdxFcs)).arg(e(kIdxRxer)).arg(e(kIdxIq)).arg(e(1))
                            .arg(1000.0 * double(e(kIdxFcs) + e(kIdxRxer)) / double(n), 0, 'f', 2)
                            .arg(s.phyDiag ? tr("; PHY: idle errors %1, receive errors %2")
                                                 .arg(e(kIdxPhyIdle)).arg(e(kIdxPhyRx))
                                           : QString()));
                m_counting = false;
                m_count->setEnabled(true);
            }
        }
    }
    if (m_phase != Phase::Idle)
        autoTick();
}

void RgmiiTuneDialog::applyS(int s)
{
    int clk = 15, data = 7;
    scaleToDelays(s, clk, data);
    {
        const QSignalBlocker b1(m_manual), b2(m_clk), b3(m_data);
        m_manual->setChecked(true);
        m_clk->setValue(clk);
        m_data->setValue(data);
    }
    sendNow();
    m_phase = Phase::Apply;
    m_phaseMs = m_applyFromMs = msNow();
}

void RgmiiTuneDialog::startAuto()
{
    if (!m_t)
        return;
    const auto s = m_t->rgmiiStatus();
    if (!s.layout4 || !s.linkCounters) {
        logLine(tr("Auto-tune impossible: the radio reports no delays and counters (status layout 4)."));
        return;
    }
    if (!s.mox) {
        logLine(tr("Transmit first (dummy load, low power): without I/Q packets no errors show."));
        return;
    }
    m_prevManual = m_manual->isChecked();
    m_prevClk = m_clk->value();
    m_prevData = m_data->value();
    m_steps.clear();
    m_log->clear();
    logLine(tr("   S  clock data  shift,ps   packets  errors(FCS+RXER)  I/Q dropped"));
    m_dir = +1;
    m_s = 0;
    m_auto->setText(tr("Stop"));
    m_manual->setEnabled(false);
    m_reset->setEnabled(false);
    applyS(0);
}

void RgmiiTuneDialog::stopAuto(const QString& why)
{
    logLine(tr("Auto-tune: %1; back to the previous delays.").arg(why));
    m_phase = Phase::Idle;
    {
        const QSignalBlocker b1(m_manual), b2(m_clk), b3(m_data);
        m_manual->setChecked(m_prevManual);
        m_clk->setValue(m_prevClk);
        m_data->setValue(m_prevData);
    }
    m_auto->setText(tr("Auto-tune"));
    m_manual->setEnabled(true);
    m_reset->setEnabled(true);
    sendNow();
}

void RgmiiTuneDialog::autoTick()
{
    const auto st = m_t->rgmiiStatus();
    const long long now = msNow();
    int clk = 15, data = 7;
    scaleToDelays(m_s, clk, data);
    const bool applied = st.layout4 && st.manualNow && st.clkNow == clk && st.dataNow == data;

    // Where the next step goes; false = this direction is over.
    auto advance = [&](bool bad) {
        if (!bad && m_s + m_dir >= kSMin && m_s + m_dir <= kSMax) {
            applyS(m_s + m_dir);
            m_s += m_dir;
            return;
        }
        if (m_dir > 0) {
            m_dir = -1;
            m_s = -1;
            applyS(-1);
            return;
        }
        // Done: the widest run of error-free steps, its middle.
        std::vector<Step> v = m_steps;
        std::sort(v.begin(), v.end(), [](const Step& a, const Step& b) { return a.s < b.s; });
        int bestLo = 0, bestLen = 0;
        for (std::size_t i = 0; i < v.size();) {
            if (!(v[i].applied && v[i].pkts > 0 && v[i].errs == 0)) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j + 1 < v.size() && v[j + 1].applied && v[j + 1].pkts > 0 && v[j + 1].errs == 0
                   && v[j + 1].s == v[j].s + 1)
                ++j;
            const int len = int(j - i + 1);
            if (len > bestLen) {
                bestLen = len;
                bestLo = v[i].s;
            }
            i = j + 1;
        }
        int best = 0;
        if (bestLen > 0) {
            best = bestLo + (bestLen - 1) / 2;
            logLine(tr("Error-free window: S = %1 … %2 (%3 steps, %4 ps). Chosen S = %5.")
                        .arg(bestLo).arg(bestLo + bestLen - 1).arg(bestLen).arg(bestLen * 60).arg(best));
        } else {
            double bestRate = 1e9;
            for (const auto& x : v)
                if (x.applied && x.pkts > 0) {
                    const double r = double(x.errs) / double(x.pkts);
                    if (r < bestRate) {
                        bestRate = r;
                        best = x.s;
                    }
                }
            logLine(tr("No error-free step; chosen S = %1 with the lowest error rate (%2 %).")
                        .arg(best).arg(100.0 * bestRate, 0, 'f', 3));
        }
        int bc = 15, bd = 7;
        scaleToDelays(best, bc, bd);
        logLine(tr("Result: clock %1 (%2 ps), data %3 (%4 ps), manual delays on and saved.")
                    .arg(bc).arg((bc - 15) * 60).arg(bd).arg((bd - 7) * 60));
        m_phase = Phase::Idle;
        {
            const QSignalBlocker b1(m_manual), b2(m_clk), b3(m_data);
            m_manual->setChecked(true);
            m_clk->setValue(bc);
            m_data->setValue(bd);
        }
        m_auto->setText(tr("Auto-tune"));
        m_manual->setEnabled(true);
        m_reset->setEnabled(true);
        sendNow();
        resetCounters();
    };

    switch (m_phase) {
    case Phase::Apply:
        if (applied) {
            m_phase = Phase::Settle;
            m_phaseMs = now;
        } else if (now - m_applyFromMs > kApplyTimeoutMs) {
            logLine(tr("%1  %2  %3   the radio did not take the value in %4 s -- end of this direction")
                        .arg(m_s, 4).arg(clk, 4).arg(data, 5).arg(kApplyTimeoutMs / 1000));
            Step x;
            x.s = m_s;
            x.applied = false;
            m_steps.push_back(x);
            advance(true);
        } else if (now - m_lastResendMs >= 500) {
            sendNow();   // only a frame with a good FCS counts
        }
        break;
    case Phase::Settle:
        if (now - m_phaseMs >= kSettleMs) {
            m_base = m_total;
            m_baseDuc = st.ducPacketsSent;
            m_phase = Phase::Measure;
            m_phaseMs = now;
        }
        break;
    case Phase::Measure: {
        if (!st.mox) {
            // No packets: wait (and start this step's count again).
            m_base = m_total;
            m_baseDuc = st.ducPacketsSent;
            m_phaseMs = now;
            break;
        }
        if (now - m_phaseMs < 1000LL * m_dwell->value())
            break;
        Step x;
        x.s = m_s;
        x.errs = (m_total[kIdxFcs] - m_base[kIdxFcs]) + (m_total[kIdxRxer] - m_base[kIdxRxer]);
        x.iq = m_total[kIdxIq] - m_base[kIdxIq];
        x.pkts = st.ducPacketsSent - m_baseDuc;
        m_steps.push_back(x);
        logLine(QStringLiteral("%1  %2  %3  %4/%5  %6  %7  %8")
                    .arg(m_s, 4).arg(clk, 4).arg(data, 5)
                    .arg((clk - 15) * 60, 5).arg((data - 7) * 60, -5)
                    .arg(x.pkts, 8).arg(x.errs, 10).arg(x.iq, 12));
        const bool bad = x.pkts > 0 && double(x.errs) / double(x.pkts) > kStopRate;
        advance(bad);
        break;
    }
    case Phase::Idle:
        break;
    }
}

}  // namespace AetherSDR
