#pragma once

// Build 267 (operator, 11.11): "Forcedly assign the radio" (Setup > Radio, the
// radio's page). A radio can report the wrong board -- a modified ANAN-100D
// answering Protocol 1 discovery as Hermes. The operator then names the type,
// and the session runs with that type's board profile (DDC plan, ADCs, Alex
// filter scheme, PureSignal, meters, connector options).
//
// Kept per REPORTED board id, as the model choice always was:
//   AnanForceModel_b<id>   "True" / "False"
//   AnanForcedModel_b<id>  model key ("ANAN-7000DLE", ...)
// Off, the type is the board's default and the choice is locked.
//
// Migration: before build 267 the model combo chose among the models sharing
// the reported board (AnanModel_b<id>: 8000DLE, G2-1K, 10E/100B ...). Such a
// choice, when it is not the board's default, becomes a forced assignment of
// the same model, so those radios keep running as before.

#include "core/AppSettings.h"
#include "core/backends/anan/P2Protocol.h"

#include <optional>
#include <vector>

namespace AetherSDR::anan {

// Protocol 1 firmware exists for these boards only (P1DdcPlan).
inline bool boardRunsOverProtocol1(std::uint8_t boardId) noexcept
{
    return boardId == kBoardIdHermes || boardId == kBoardIdHermesII || boardId == kBoardIdAngelia
        || boardId == kBoardIdOrion || boardId == kBoardIdOrion2;
}

// The models a connection over this protocol can be driven as.
inline std::vector<AnanModel> modelsForProtocol(bool protocol1)
{
    std::vector<AnanModel> out;
    for (int i = 0; i < kAnanModelCount; ++i) {
        const auto m = static_cast<AnanModel>(i);
        if (!protocol1 || boardRunsOverProtocol1(modelTraits(m).boardId))
            out.push_back(m);
    }
    return out;
}

inline QString forceModelKey(std::uint8_t reportedBoard)
{
    return QStringLiteral("AnanForceModel_b%1").arg(int(reportedBoard));
}

inline QString forcedModelKey(std::uint8_t reportedBoard)
{
    return QStringLiteral("AnanForcedModel_b%1").arg(int(reportedBoard));
}

inline std::optional<AnanModel> modelFromSetting(const QString& value)
{
    const QByteArray key = value.toLatin1();
    return modelFromKey(key.constData(), std::size_t(key.size()));
}

// The pre-267 same-board choice, once.
inline void migrateModelChoice(std::uint8_t reportedBoard)
{
    if (reportedBoard == 0)
        return;
    auto& st = AppSettings::instance();
    if (st.contains(forceModelKey(reportedBoard)))
        return;
    const QString old = st.value(QStringLiteral("AnanModel_b%1").arg(int(reportedBoard)), QString()).toString();
    const auto m = modelFromSetting(old);
    const AnanModel def = defaultModelFor(reportedBoard);
    const bool keep = m && *m != def && modelTraits(*m).boardId == modelTraits(def).boardId;
    st.setValue(forceModelKey(reportedBoard), keep ? QStringLiteral("True") : QStringLiteral("False"));
    if (keep)
        st.setValue(forcedModelKey(reportedBoard), old);
    st.save();
}

inline bool forceModelOn(std::uint8_t reportedBoard)
{
    migrateModelChoice(reportedBoard);
    return AppSettings::instance().value(forceModelKey(reportedBoard), QStringLiteral("False")).toString()
        == QLatin1String("True");
}

// The forced model for this reported board, or nullopt (off, unset, or a model
// the protocol cannot drive -- then the reported board's default applies).
inline std::optional<AnanModel> forcedModelFor(std::uint8_t reportedBoard, bool protocol1)
{
    if (!forceModelOn(reportedBoard))
        return std::nullopt;
    const auto m = modelFromSetting(
        AppSettings::instance().value(forcedModelKey(reportedBoard), QString()).toString());
    if (!m || (protocol1 && !boardRunsOverProtocol1(modelTraits(*m).boardId)))
        return std::nullopt;
    return m;
}

// The type a reported board is driven as.
inline AnanModel effectiveModelFor(std::uint8_t reportedBoard, bool protocol1)
{
    return forcedModelFor(reportedBoard, protocol1).value_or(defaultModelFor(reportedBoard));
}

}  // namespace AetherSDR::anan
