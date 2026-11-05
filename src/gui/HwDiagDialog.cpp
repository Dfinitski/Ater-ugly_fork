#include "gui/HwDiagDialog.h"

#include "core/backends/anan/P2Transport.h"
#include "core/backends/hl2/Hl2Backend.h"
#include "models/RadioModel.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {
long long msNow() { return QDateTime::currentMSecsSinceEpoch(); }
const QString kRed = QStringLiteral("#ff6060");
const QString kGreen = QStringLiteral("#30e030");
const QString kAmber = QStringLiteral("#e0b020");

// Counter -> (status byte, width in bytes, first layout version).
struct CounterDef {
    int at;
    int width;
    int since;
};
constexpr CounterDef kDefs[] = {
    {16, 2, 3}, {18, 2, 3}, {20, 2, 3}, {24, 2, 3}, {26, 2, 3},   // link
    {8, 2, 6}, {10, 2, 6},                                         // PHY
    {34, 2, 1}, {36, 2, 1}, {40, 2, 1}, {42, 2, 1}, {44, 2, 1},    // buffers
    {51, 1, 2}, {52, 1, 2},                                        // codec
};

QString cnt(unsigned long long v)
{
    return v ? QStringLiteral("<span style='color:%1'><b>%2</b></span>").arg(kRed).arg(v)
             : QStringLiteral("<b>0</b>");
}

QGroupBox* group(const QString& title, QLabel*& label)
{
    auto* g = new QGroupBox(title);
    auto* l = new QVBoxLayout(g);
    label = new QLabel;
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(label);
    return g;
}
}  // namespace

HwDiagDialog::HwDiagDialog(RadioModel* model, QWidget* parent)
    : PersistentDialog(tr("HW Diag"), QStringLiteral("HwDiagGeometry"), parent)
    , m_model(model)
{
    auto* top = new QVBoxLayout(bodyWidget());
    top->setSpacing(6);
    m_head = new QLabel;
    m_head->setTextFormat(Qt::RichText);
    m_head->setWordWrap(true);
    top->addWidget(m_head);
    top->addWidget(group(tr("Ethernet: radio receive (PC → radio)"), m_eth));
    top->addWidget(group(tr("PHY KSZ9031"), m_phy));
    top->addWidget(group(tr("Radio buffers (since the program started the radio)"), m_buf));
    top->addWidget(group(tr("Codec and clocks (since radio power-on)"), m_codec));

    auto* row = new QHBoxLayout;
    m_reset = new QPushButton(tr("Reset counters"));
    m_count = new QPushButton(tr("Count 4000 packets"));
    m_count->setToolTip(tr("Errors over ~4000 I/Q packets at the current power: compare drive 0 with normal power.\n"
                           "Almost no errors without RF means transmitter pickup."));
    row->addWidget(m_reset);
    row->addWidget(m_count);
    row->addStretch(1);
    top->addLayout(row);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMinimumHeight(90);
    top->addWidget(m_log, 1);

    connect(m_reset, &QPushButton::clicked, this, &HwDiagDialog::resetCounters);
    connect(m_count, &QPushButton::clicked, this, [this] {
        auto* be = m_model ? dynamic_cast<hl2::Hl2Backend*>(m_model->backend()) : nullptr;
        auto* t = be ? qobject_cast<anan::P2Transport*>(be->ananTransport()) : nullptr;
        const auto s = t ? t->statusSnapshot() : anan::P2Transport::StatusSnapshot{};
        if (!s.valid || !s.mox || s.b[47] < 3) {
            logLine(tr("Count: needs a transmission (I/Q packets flowing) and status layout 3 or later."));
            return;
        }
        m_counting = true;
        m_countBase = m_total;
        m_countBaseDuc = s.ducPacketsSent;
        m_countDrive = s.drive;
        m_count->setEnabled(false);
        logLine(tr("Count: started, drive %1 of 255 ...").arg(s.drive));
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &HwDiagDialog::tick);
    m_timer->start();
    tick();
}

void HwDiagDialog::logLine(const QString& t)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + t);
}

void HwDiagDialog::resetCounters()
{
    m_zero = m_total;
    m_zeroDuc = m_ducNow;
    m_fcsHist.clear();
    tick();
}

void HwDiagDialog::tick()
{
    auto* be = m_model ? dynamic_cast<hl2::Hl2Backend*>(m_model->backend()) : nullptr;
    auto* t = be ? qobject_cast<anan::P2Transport*>(be->ananTransport()) : nullptr;
    if (!t) {
        m_head->setText(tr("<b>Radio not connected.</b>"));
        for (QLabel* l : {m_eth, m_phy, m_buf, m_codec})
            l->setText(QStringLiteral("--"));
        m_have = false;
        return;
    }
    const auto s = t->statusSnapshot();
    const long long now = msNow();
    const auto& b = s.b;
    const int layout = s.valid ? int(b[47]) : 0;
    auto be16 = [&](int i) { return unsigned((unsigned(b[std::size_t(i)]) << 8) | b[std::size_t(i + 1)]); };

    // The first status: take the baseline.
    if (s.valid && !m_have) {
        for (int k = 0; k < kCounters; ++k)
            m_last[std::size_t(k)] = kDefs[k].width == 2 ? be16(kDefs[k].at) : unsigned(b[std::size_t(kDefs[k].at)]);
        m_zero = m_total;
        m_zeroDuc = s.ducPacketsSent;
        m_have = true;
    }
    m_lastCount = s.count;
    m_ducNow = s.ducPacketsSent;
    if (s.valid) {
        for (int k = 0; k < kCounters; ++k) {
            if (layout < kDefs[k].since)
                continue;
            const unsigned v = kDefs[k].width == 2 ? be16(kDefs[k].at) : unsigned(b[std::size_t(kDefs[k].at)]);
            const unsigned mask = kDefs[k].width == 2 ? 0xffffu : 0xffu;
            unsigned dv = (v - m_last[std::size_t(k)]) & mask;
            // A step "backwards" by more than half the range is the radio
            // clearing the counter (buffer counters at every run = 0, all of
            // them at power-on), not a wrap: count from its new value.
            if (dv > mask / 2)
                dv = v;
            m_total[std::size_t(k)] += dv;
            m_last[std::size_t(k)] = v;
        }
        m_fcsHist.emplace_back(now, m_total[kBadFcs]);
        while (!m_fcsHist.empty() && now - m_fcsHist.front().first > 60000)
            m_fcsHist.pop_front();
    }
    auto d = [&](int k) { return m_total[std::size_t(k)] - m_zero[std::size_t(k)]; };

    // Head: is the radio talking, which layout.
    if (!s.valid) {
        m_head->setText(tr("<span style='color:%1'>No status from the radio yet.</span>").arg(kAmber));
    } else {
        const bool fresh = s.ageMs >= 0 && s.ageMs < 1000;
        m_head->setText(tr("Radio status: layout <b>v%1</b>%2, packets %3, last %4 &nbsp;|&nbsp; %5")
                            .arg(layout)
                            .arg(layout == 0 ? tr(" (firmware without diagnostics)") : QString())
                            .arg(s.count)
                            .arg(fresh ? tr("%1 ms ago").arg(s.ageMs)
                                       : tr("<span style='color:%1'>%2 s ago -- the radio sends no status</span>")
                                             .arg(kAmber).arg(s.ageMs / 1000))
                            .arg(s.mox ? tr("<b>transmitting</b>, drive %1 of 255").arg(s.drive) : tr("receiving")));
    }

    // Ethernet.
    if (layout < 3) {
        m_eth->setText(tr("Not in this firmware (needs layout 3 or later)."));
    } else {
        double perMin = 0.0;
        if (m_fcsHist.size() >= 2) {
            const double span = double(m_fcsHist.back().first - m_fcsHist.front().first) / 60000.0;
            if (span > 0.05)
                perMin = double(m_fcsHist.back().second - m_fcsHist.front().second) / span;
        }
        const unsigned long long pk = m_ducNow - m_zeroDuc;
        m_eth->setText(
            tr("Frames with a bad FCS (no PHY error): %1 &nbsp;(%2 per minute; since power-on %3)<br>"
               "Frames with PHY RXER: %4 &nbsp;(since power-on %5)<br>"
               "Packets dropped: TX I/Q %6, audio %7, commands not applied %8<br>"
               "I/Q packets sent: %9, errors (FCS + RXER) per 1000 packets: <b>%10</b>")
                .arg(cnt(d(kBadFcs))).arg(perMin, 0, 'f', 1).arg(be16(24))
                .arg(cnt(d(kRxer))).arg(be16(26))
                .arg(cnt(d(kIqDropped))).arg(cnt(d(kAudioDropped))).arg(cnt(d(kCmdNotApplied)))
                .arg(pk)
                .arg(pk ? QString::number(1000.0 * double(d(kBadFcs) + d(kRxer)) / double(pk), 'f', 2)
                        : QStringLiteral("--")));
    }

    // PHY.
    if (layout < 6) {
        m_phy->setText(tr("Not in this firmware (needs layout 6 or later)."));
    } else {
        const int eee = b[12], gs = b[13];
        QString eeeText = (eee & 0x06)
            ? tr("<span style='color:%1'><b>on at the PC</b> (%2%3)</span> -- turn it off in the NIC properties")
                  .arg(kRed)
                  .arg((eee & 0x04) ? QStringLiteral("1000BASE-T ") : QString())
                  .arg((eee & 0x02) ? QStringLiteral("100BASE-TX") : QString())
            : tr("off at the PC");
        auto ok = [](bool v) {
            return v ? QStringLiteral("<span style='color:%1'>OK</span>").arg(kGreen)
                     : QStringLiteral("<span style='color:%1'><b>NOT OK</b></span>").arg(kRed);
        };
        QString xtal = tr("--");
        if (layout >= 7) {
            const int dev = int(std::int16_t(be16(53)));
            const double ppm = dev * 8.0 / 125.0;
            const QString col = std::abs(ppm) > 50.0 ? kRed : std::abs(ppm) > 25.0 ? kAmber : kGreen;
            xtal = tr("<span style='color:%1'><b>%2 ppm</b></span> (%3 Hz; 1000BASE-T allows ±50 ppm)")
                       .arg(col).arg(ppm, 0, 'f', 2).arg(dev * 8);
        }
        m_phy->setText(tr("1000BASE-T idle errors: %1 &nbsp;(since power-on %2) -- growth: line / PHY supply<br>"
                          "KSZ9031 receive errors: %3 &nbsp;(since power-on %4)<br>"
                          "EEE: %5<br>"
                          "1000BASE-T: radio is clock %6, local receiver %7, remote %8%9<br>"
                          "PHY crystal (125 MHz against the radio's 122.88 MHz): %10")
                           .arg(cnt(d(kPhyIdle))).arg(be16(8))
                           .arg(cnt(d(kPhyRx))).arg(be16(10))
                           .arg(eeeText)
                           .arg((gs & 0x40) ? tr("master") : tr("slave"))
                           .arg(ok(gs & 0x20)).arg(ok(gs & 0x10))
                           .arg((gs & 0x80) ? tr(", <span style='color:%1'><b>master/slave fault</b></span>")
                                                  .arg(kRed)
                                            : QString())
                           .arg(xtal));
    }

    // Buffers.
    if (layout < 1) {
        m_buf->setText(tr("Not in this firmware (needs layout 1 or later)."));
    } else {
        const int txLvl = int(be16(32)), spkLvl = int(be16(38));
        const bool txRun = b[46] & 0x01, spkRun = b[46] & 0x02;
        if (txRun)
            m_txLevels.emplace_back(now, txLvl);
        if (spkRun)
            m_spkLevels.emplace_back(now, spkLvl);
        for (auto* q : {&m_txLevels, &m_spkLevels})
            while (!q->empty() && now - q->front().first > 5000)
                q->pop_front();
        auto range = [](const std::deque<std::pair<long long, int>>& q) {
            if (q.empty())
                return QStringLiteral("--");
            int lo = q.front().second, hi = lo;
            for (const auto& p : q) {
                lo = std::min(lo, p.second);
                hi = std::max(hi, p.second);
            }
            return QStringLiteral("%1..%2").arg(lo).arg(hi);
        };
        m_buf->setText(tr("Transmitter: %1, level <b>%2</b> of 8192 (last 5 s %3; normal 6000..6200)<br>"
                          "&nbsp;&nbsp;underflows %4, samples dropped (full) %5<br>"
                          "Speaker: %6, level <b>%7</b> of 2048 (last 5 s %8; normal 700..1100)<br>"
                          "&nbsp;&nbsp;fades %9, reads from empty %10, samples dropped %11")
                           .arg(txRun ? tr("running") : tr("stopped")).arg(txLvl).arg(range(m_txLevels))
                           .arg(cnt(d(kTxEmpty))).arg(cnt(d(kTxOverflow)))
                           .arg(spkRun ? tr("playing") : tr("stopped")).arg(spkLvl).arg(range(m_spkLevels))
                           .arg(cnt(d(kSpkFades))).arg(cnt(d(kSpkEmptyReads))).arg(cnt(d(kSpkOverflow))));
    }

    // Codec.
    if (layout < 2) {
        m_codec->setText(tr("Not in this firmware (needs layout 2 or later)."));
    } else {
        m_codec->setText(tr("Codec clock PLL lock losses: %1 &nbsp;(since power-on %2, should be 0)<br>"
                            "TLV320 SPI writes: %3 &nbsp;(since power-on %4; 8 after power-on, "
                            "+2 per mic settings change)")
                             .arg(cnt(d(kPllLoss))).arg(int(b[51]))
                             .arg(d(kCodecWrites)).arg(int(b[52])));
    }

    // "Count 4000 packets".
    if (m_counting) {
        const unsigned long long n = s.ducPacketsSent - m_countBaseDuc;
        auto e = [&](int k) { return m_total[std::size_t(k)] - m_countBase[std::size_t(k)]; };
        if (!s.mox) {
            m_counting = false;
            m_count->setEnabled(true);
            logLine(tr("Count: aborted (transmission stopped after %1 packets).").arg(n));
        } else if (n >= kCountPackets) {
            logLine(tr("Count: %1 packets at drive %2%3: FCS %4, RXER %5, I/Q dropped %6, audio %7 -- "
                       "%8 errors per 1000 packets%9")
                        .arg(n).arg(m_countDrive)
                        .arg(m_countDrive != s.drive ? tr(" (changed, now %1)").arg(s.drive) : QString())
                        .arg(e(kBadFcs)).arg(e(kRxer)).arg(e(kIqDropped)).arg(e(kAudioDropped))
                        .arg(1000.0 * double(e(kBadFcs) + e(kRxer)) / double(n), 0, 'f', 2)
                        .arg(layout >= 6 ? tr("; PHY: idle errors %1, receive errors %2")
                                               .arg(e(kPhyIdle)).arg(e(kPhyRx))
                                         : QString()));
            m_counting = false;
            m_count->setEnabled(true);
        }
    }
}

}  // namespace AetherSDR
