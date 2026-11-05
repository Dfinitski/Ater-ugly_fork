#pragma once

#include "CanonWindow.h"
#include "core/RadioDiscovery.h"
#include "core/backends/flex/SmartLinkClient.h"
#include "core/IConnectionAutomation.h"

#include <QWidget>
#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QStackedWidget>
#include <QToolButton>

class QVBoxLayout;
class QScreen;
class QScrollArea;
class QSpinBox;

namespace AetherSDR {

// Novice-first dialog for local, SmartLink, and manual/VPN radio connections:
// a CanonWindow dialog (style guide, RFC #6226) that MainWindow places itself,
// above the status bar.
class ConnectionPanel : public CanonWindow, public IConnectionAutomation {
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget* parent = nullptr);

    static constexpr int kSafeMinimumWidth = 640;
    static constexpr int kSafeMinimumHeight = 360;
    static constexpr int kPreferredWidth = 760;
    static constexpr int kPreferredHeight = 660;

    bool selectRadio(const QString& serial);
    void selectManualConnection();
    bool canRenameRadio(const QString& serial) const;
    bool canRenameRadio(const RadioInfo& radio) const;
    QString radioDisplayName(const RadioInfo& radio, const QString& fallback) const;
    // Ask the owner to collect a new nickname (radioRenameRequested); the
    // owner applies it with setRadioNickname().  Empty resets to the default.
    void renameRadio(const QString& serial);
    void renameRadio(const RadioInfo& radio);
    void setRadioNickname(const RadioInfo& radio, const QString& nickname);
    void fitToScreen(QScreen* preferredScreen = nullptr);
    // Fit, then pull the frame back inside the work area without otherwise
    // moving the window. The placement-preserving counterpart to
    // MainWindow::showConnectionDialog(), for the show paths this class owns
    // (the automation bridge) and for the screen/DPI/font
    // changes that can invalidate a fit made earlier (#4515).
    void fitAndClampToScreen(QScreen* preferredScreen = nullptr);
    QMargins screenFitFrameMargins() const;
    QSize screenFitFrameSize() const;
    QPoint constrainedFrameTopLeft(const QPoint& preferredFrameTopLeft,
                                   const QRect& availableGeometry) const;
    void setConnected(bool connected);
    void setStatusText(const QString& text);
    void probeRadio(const QString& ip, bool restoreSavedFamily = false);

    // Radio families the "Connect by IP" page can dial: a FlexRadio answers
    // TCP/4992 and an ANAN openHPSDR Protocol 2 discovery on UDP/1024, so the
    // operator picks the wire protocol and we probe exactly that one. Values
    // match RadioInfo::family.
    static constexpr const char* kFamilyFlex = "flex";
    static constexpr const char* kFamilyAnan = "anan";

    // IConnectionAutomation — engine-facing connect/disconnect/dialog hook.
    QList<RadioInfo> automationLocalRadios() const override;
    bool automationConnectLocalSerial(const QString& serial, QString* error = nullptr) override;
    bool automationConnectByIp(const QString& hostOrIp,
                               const QString& family = QString(),
                               QString* error = nullptr) override;
    bool automationDisconnect(QString* error = nullptr) override;
    bool automationDialogVisible() const override { return isVisible(); }
    void automationSetDialogVisible(bool visible) override
    {
        if (visible) {
            // Same fit-and-place contract MainWindow::showConnectionDialog()
            // applies, minus the re-centring: an agent-driven show must not
            // yank a window the operator positioned, only pull it back inside
            // the work area if it no longer fits there.
            fitAndClampToScreen();
            show();
            raise();
            activateWindow();
        } else {
            hide();
        }
    }
    QObject* asQObject() override { return this; }

protected:
    bool event(QEvent* e) override;

public slots:
    void onRadioDiscovered(const RadioInfo& radio);
    void onRadioUpdated(const RadioInfo& radio);
    void onRadioLost(const QString& serial);

    // SmartLink
    void setSmartLinkClient(SmartLinkClient* client);

signals:
    void connectRequested(const RadioInfo& radio);
    void wanConnectRequested(const WanRadioInfo& radio);
    void wanDisconnectClientsRequested(const WanRadioInfo& radio);
    void disconnectRequested();
    void routedRadioFound(const RadioInfo& radio);
    void retryDiscoveryRequested();
    void radioNicknameChanged();
    void radioRenameRequested(const RadioInfo& radio, const QString& currentNickname);
    // The operator signed out of SmartLink and the WAN list was cleared.
    void smartLinkSignedOut();
    void networkDiagnosticsRequested();
    void smartLinkLoginRequested(const QString& email, const QString& password);
    // A startup auto-connect gave up before it could reach the radio. Carries
    // the operator-facing reason, which would otherwise be stranded on the
    // manual page (see reportStartupProbeFailure).
    void startupConnectUnavailable(const QString& reason);

private slots:
    void onConnectionModeClicked(int id);
    void onListSelectionChanged();
    void onWanSelectionChanged();
    void onLocalConnectClicked();
    void onWanConnectClicked();
    void onWanDisconnectClientsClicked();
    void onManualIpChanged(const QString& ip);
    void onManualConnectClicked();
    void onManualAdvancedToggled(bool checked);

private:
    enum ConnectionMode {
        LocalMode = 0,
        SmartLinkMode = 1,
        ManualMode = 2
    };

    void setCurrentMode(ConnectionMode mode);
    void updateLocalPageState();
    void updateSmartLinkUi();
    // The SmartLink account line: a sign-in hint, the signed-in account, or a
    // sign-in error. Its tone is kept here, not read back from the label's
    // stylesheet, which holds the theme-resolved colors.
    enum class AccountTone { Hint, Info, Error };
    void setAccountLine(const QString& text, AccountTone tone);
    friend struct ConnectionPanelSmartLinkTestAccess;
    // Right-click menu on a discovered radio row: set/clear a client-side
    // nickname (non-Flex families only). pos is in m_radioList viewport coords.
    void showRadioContextMenu(const QPoint& pos);
    void updateActionState();
    void updateLowBandwidthVisibility();
    void updateManualAdvancedVisibility();
    void refreshManualSourceOptions(const RadioBindSettings* selected = nullptr);
    // `restoreFamily` lets the per-address profile move the Radio type selector.
    // False on the keystroke path: the editable combo's inline completer can
    // complete one typed character to a saved address, and the selector must
    // not jump mid-typing. True when the operator picks an address (`activated`)
    // and at startup.
    void applySavedSourceSelection(const QString& ip, bool restoreFamily = true);
    RadioBindSettings currentManualBindSettings(bool* staleSelection = nullptr) const;
    void loadRecentManualIps();
    void rememberManualIp(const QString& ip);
    // Radio-type selector on the manual page (persisted globally, and per-IP in
    // the routed profile so picking a recent address restores its family).
    QString currentManualFamily() const;
    void setManualFamily(const QString& family);
    void updateManualFamilyHints();
    // Directed (unicast) openHPSDR Protocol 2 discovery against one host.
    //
    // Three outcomes, not two: "nothing answered" and "we never got to ask" need
    // different messages, and collapsing them into a bool sent the operator to
    // power-cycle a radio that was never contacted. Only NoAnswer leaves the
    // error message to the caller — the other two have already reported.
    enum class AnanProbeResult {
        Answered,      // an ANAN replied; connect or refusal already reported
        NoAnswer,      // nothing replied within the deadline; caller owns the message
        NotAttempted,  // never got to ask — bind, resolve or send failed; reported here
    };
    AnanProbeResult probeAnan(const QString& ip, const RadioBindSettings& bindSettings);
    void probeFlexRadio(const QString& ip, const RadioBindSettings& bindSettings);
    // For an explicit path and a host name with only IPv6 addresses, moves
    // `bind` to its interface's IPv6 address. False (with the message shown)
    // when that interface has none.
    bool resolveNamedSourceFamily(const QString& host, RadioBindSettings* bind);
    // Reports early startup credential/configuration or directed-probe failures.
    // Session authentication failures and broadcast discovery are separate paths.
    // Interactive/automation attempts are silent once the startup probe ends.
    void reportStartupProbeFailure(const QString& reason);
    void finishManualProbe(const RadioInfo& info, bool routedOnly = false);
    friend struct ConnectionPanelStartupTestAccess;
    void resetManualConnectButton();
    // Re-activate the body layout after a page change. The overlap this used to
    // guard against — the Advanced section expanding, or the result line
    // wrapping, while the dialog could not grow — is now structurally
    // impossible: the body lives in a QScrollArea, which never hands its widget
    // less than qSmartMinSize(). Cheap and idempotent; safe to call often.
    void refitToContent();
    // Height the body would like if the screen allows it. The panel's own
    // sizeHint() cannot answer this — QScrollArea caps its hint by design, so
    // it under-reports the body it is scrolling. Taken at the client width the
    // caller is about to apply, because that width decides whether a
    // horizontal scrollbar takes its height out of the viewport.
    int preferredClientHeight(int clientWidth) const;
    QScreen* screenFitTarget(QScreen* preferredScreen) const;
    void saveManualProfile(const QString& targetIp,
                           const RadioBindSettings& settings,
                           const QHostAddress& lastSuccessfulLocalIp);
    void saveLowBandwidthPreference(bool enabled);
    void setManualMessage(const QString& text, bool error = false);
    QString formatLocalRadioLabel(const RadioInfo& radio) const;
    QString formatWanRadioLabel(const WanRadioInfo& radio) const;

    QVBoxLayout* m_rootLayout{nullptr};
    QScrollArea* m_bodyScroll{nullptr};
    QWidget*     m_bodyContent{nullptr};
    // Last height fitToScreen() chose, so it can tell its own sizing from a
    // height the operator dragged to. It grows the dialog back toward the
    // body's preferred height only from the former — a size the operator
    // picked is theirs to keep, even if that means the body scrolls.
    int          m_autoFitHeight{kPreferredHeight};

    QButtonGroup* m_modeButtons{nullptr};
    QStackedWidget* m_modeStack{nullptr};
    QCommandLinkButton* m_localModeBtn{nullptr};
    QCommandLinkButton* m_smartLinkModeBtn{nullptr};
    QCommandLinkButton* m_manualModeBtn{nullptr};

    QLabel*      m_statusLabel;
    QPushButton* m_disconnectBtn{nullptr};

    QListWidget* m_radioList{nullptr};
    QStackedWidget* m_localStateStack{nullptr};
    QWidget* m_localEmptyState{nullptr};
    QPushButton* m_localConnectBtn{nullptr};

    QList<RadioInfo> m_radios;   // LAN radios only
    bool m_connected{false};

    // SmartLink UI
    SmartLinkClient* m_smartLink{nullptr};
    QWidget*     m_loginForm{nullptr};
    QLineEdit*   m_emailEdit{nullptr};
    QLineEdit*   m_passwordEdit{nullptr};
    QPushButton* m_loginBtn{nullptr};
    QPushButton* m_logoutBtn{nullptr};
    QLabel*      m_slUserLabel{nullptr};
    AccountTone  m_slUserTone{AccountTone::Hint};
    QListWidget* m_wanList{nullptr};
    QLabel*      m_smartLinkEmptyLabel{nullptr};
    QPushButton* m_wanDisconnectClientsBtn{nullptr};
    QPushButton* m_wanConnectBtn{nullptr};
    QList<WanRadioInfo> m_wanRadios;

    // Manual (VPN / routed) connection
    QComboBox*   m_manualRadioTypeCombo{nullptr};
    QLabel*      m_manualHintLabel{nullptr};
    QComboBox*   m_manualIpCombo{nullptr};
    // Points at Radio Setup's "ANAN Front End" page, where this family's
    // front-end options are set. Shown only for family "anan", like the Icom
    // rows above, and held as a row container for the same reason -- it hides
    // as a unit.
    QWidget*     m_manualAnanHintRow{nullptr};
    QLineEdit*   m_manualIpEdit{nullptr};
    QLabel*      m_manualResultLabel{nullptr};
    QToolButton* m_manualAdvancedToggle{nullptr};
    QWidget*     m_manualAdvancedWidget{nullptr};
    QComboBox*   m_manualSourceCombo{nullptr};
    QLabel*      m_manualSourceWarningLabel{nullptr};
    QPushButton* m_manualConnectBtn{nullptr};
    QString      m_manualProfileIp;
    // The source path the operator last chose themselves. Editing the address
    // can flip the list's family on the way; the pick survives so the exact
    // address comes back when the family does.
    RadioBindSettings m_manualSourcePick;
    bool         m_manualConnectPending{false};
    // Set for the duration of a startup probe (probeRadio's restoreSavedFamily
    // call), which is the one probe with no operator watching the manual page.
    // Ends on failure, dispatch to the session layer, a proven connection,
    // or an operator route edit/manual connect; an async credential re-entry
    // alone is still part of the same probe.
    bool         m_startupProbe{false};
    QCheckBox*   m_autoConnectCheck{nullptr};

    QWidget*     m_linkOptionsWidget{nullptr};
    QLabel*      m_lowBwHintLabel{nullptr};
    QCheckBox*   m_lowBwCheck{nullptr};
    QCheckBox*   m_adaptiveThrottleCheck{nullptr};
};

} // namespace AetherSDR
