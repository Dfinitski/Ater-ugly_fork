#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QLineEdit>
#include <QMap>
#include <QStringList>
#include <QSignalBlocker>
#include <QString>

namespace AetherSDR::SerialPortCombo {

inline bool isCustom(const QComboBox* combo)
{
    return combo && combo->currentData().toString() == QLatin1String("__custom__");
}

// Port entries provide portName() and description(), like QSerialPortInfo.
// Block both widgets: repopulation must not save settings or look like an
// operator edit. Callers apply the returned editor visibility themselves.
template<typename Ports>
bool populate(QComboBox* combo, QLineEdit* customEdit, const QString& savedPort,
              const Ports& ports, bool preserveEmptyCustom = false)
{
    const QSignalBlocker comboBlocker(combo);
    const QSignalBlocker editBlocker(customEdit);
    combo->clear();
#ifdef Q_OS_WIN
    // ANAN-100D port (operator, 31.10): a normal Windows COM-port choice --
    // the ports present ("COM3 - USB Serial Port"), then COM1..COM32 that are
    // not plugged in now (so a device can be chosen before it is connected),
    // all in number order; no "Custom..." path entry.
    {
        (void)preserveEmptyCustom;
        auto comNumber = [](const QString& name) {
            if (!name.startsWith(QLatin1String("COM"), Qt::CaseInsensitive))
                return -1;
            bool ok = false;
            const int n = name.mid(3).toInt(&ok);
            return ok ? n : -1;
        };
        QMap<int, QString> present;     // number -> label
        QStringList other;              // present ports without a COM number
        for (const auto& info : ports) {
            const QString name = info.portName();
            const QString desc = info.description();
            const QString label = desc.isEmpty() ? name
                                                 : QStringLiteral("%1 - %2").arg(name, desc);
            const int n = comNumber(name);
            if (n >= 0)
                present.insert(n, label);
            else
                other << name;
        }
        QMap<int, QString> all = present;
        for (int n = 1; n <= 32; ++n) {
            if (!all.contains(n))
                all.insert(n, QStringLiteral("COM%1 (not present)").arg(n));
        }
        const int savedNum = comNumber(savedPort);
        if (savedNum > 32 && !all.contains(savedNum))
            all.insert(savedNum, QStringLiteral("COM%1 (not present)").arg(savedNum));
        for (auto it = all.constBegin(); it != all.constEnd(); ++it)
            combo->addItem(it.value(), QStringLiteral("COM%1").arg(it.key()));
        for (const QString& name : other)
            combo->addItem(name, name);
        int match = -1;
        if (savedNum >= 0)
            match = combo->findData(QStringLiteral("COM%1").arg(savedNum));
        else if (!savedPort.isEmpty())
            match = combo->findData(savedPort);
        if (match < 0 && !present.isEmpty())
            match = combo->findData(QStringLiteral("COM%1").arg(present.firstKey()));
        combo->setCurrentIndex(match >= 0 ? match : 0);
        if (customEdit)
            customEdit->clear();
        return false;
    }
#endif
    for (const auto& info : ports) {
        combo->addItem(QString("%1 — %2").arg(info.portName(), info.description()),
                       info.portName());
    }
    combo->addItem("Custom...", QStringLiteral("__custom__"));

    const int match = savedPort.isEmpty() ? -1 : combo->findData(savedPort);
    if (match >= 0 && match < combo->count() - 1) {
        combo->setCurrentIndex(match);
    } else if (!savedPort.isEmpty() || preserveEmptyCustom || ports.isEmpty()) {
        combo->setCurrentIndex(combo->count() - 1);
        if (customEdit) {
            customEdit->setText(savedPort);
        }
    }
    // An empty enumeration selects Custom even without a saved path.
    return isCustom(combo);
}

// Invoke enumeration only after checking the popup guard. Preserve identity,
// including the operator's explicit empty Custom choice, across refresh/show.
template<typename Enumerate>
bool refresh(QComboBox* combo, QLineEdit* customEdit, Enumerate enumerate)
{
    if (!combo) {
        return false;
    }
    if (combo->view()->isVisible()) {
        return isCustom(combo);
    }

    const bool wasCustom = isCustom(combo);
    QString keep = combo->currentData().toString();
    if (keep.isEmpty() || wasCustom) {
        keep = customEdit ? customEdit->text().trimmed() : QString();
    }
    return populate(combo, customEdit, keep, enumerate(), wasCustom && keep.isEmpty());
}

} // namespace AetherSDR::SerialPortCombo
