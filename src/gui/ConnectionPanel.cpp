#include "ConnectionPanel.h"
#include "core/AppSettings.h"
#include "core/backends/ConnectionSharingPolicy.h"  // in-use share gate (#4448), shared with MainWindow_Session
#include "core/backends/anan/AnanDiscovery.h" // shared nickname + MAC->serial helpers
#include "core/backends/anan/P2Protocol.h"  // buildDiscovery/parseDiscoveryReply, kRadioPort
#include "core/NetworkPathResolver.h"
#include "ComboStyle.h"   // shared themed combo look (painted arrow)
#include "FramelessResizer.h"
#include "CanonIndicators.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>

#include <QAbstractItemView>
#include <QPointer>
#include <QLineEdit>
#include <QFormLayout>
#include <QGuiApplication>
#include <QDialogButtonBox>
#include <QMenu>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSet>
#include <QSpinBox>
#include <QTcpSocket>
#include <QHostInfo>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QDeadlineTimer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include "core/ThemeManager.h"

namespace AetherSDR {

namespace {

constexpr int kSourceModeRole = Qt::UserRole + 10;
constexpr int kSourceInterfaceIdRole = Qt::UserRole + 11;
constexpr int kSourceInterfaceNameRole = Qt::UserRole + 12;
constexpr int kSourceAddressRole = Qt::UserRole + 13;
constexpr int kSourceStaleRole = Qt::UserRole + 14;
constexpr int kMaxRecentManualIps = 3;
constexpr const char* kRecentManualIpsKey = "RecentConnectByIpAddresses";
// Last radio type chosen on the "Connect by IP" page. Defaults to flex so an
// existing install keeps the behaviour it had before the selector existed.
constexpr const char* kManualRadioFamilyKey = "ConnectByIpRadioFamily";

const char* kHintLabelStyle =
    "QLabel { color: {{color.canon.muted}}; font-size: 11px; background: transparent; border: none; }";
const char* kInfoLabelStyle =
    "QLabel { color: {{color.canon.cyan}}; font-size: 11px; background: transparent; border: none; }";
const char* kErrorLabelStyle =
    "QLabel { color: {{color.accent.danger}}; font-size: 11px; background: transparent; border: none; }";

// The style guide's canon vocabulary (RFC #6226), as Network Diagnostics and
// AetherMap use it: transparent over CanonWindow's ground except the surfaces
// that hold content. Groups are raised cards, lists nested, fields on
// control; selection and focus are canon cyan.
constexpr const char* kConnectionStyle = R"(
QWidget {
    color: {{color.canon.inkSoft}};
    background: transparent;
}
QMenu,
QComboBoxPrivateContainer {
    background: {{color.canon.raised}};
}
QGroupBox {
    background: {{color.canon.raised}};
    border: 1px solid {{color.canon.line}};
    border-radius: 7px;
    margin-top: 10px;
    color: {{color.canon.ink}};
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: {{color.canon.muted}};
}
QListWidget {
    color: {{color.canon.inkSoft}};
    background: {{color.canon.nested}};
    border: 1px solid {{color.canon.line}};
    border-radius: 4px;
    padding: 2px;
    outline: none;
}
QListWidget::item:selected {
    color: {{color.canon.ink}};
    background: {{color.canon.control}};
}
QScrollBar:vertical {
    background: transparent;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: {{color.canon.lineHi}};
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}
QPushButton {
    color: {{color.canon.cyan}};
    background: {{color.canon.control}};
    border: 1px solid {{color.canon.lineHi}};
    border-radius: 4px;
    padding: 5px 12px;
    font-weight: 600;
}
QPushButton:hover {
    background: {{color.canon.nested}};
    color: {{color.canon.aqua}};
}
QPushButton:focus {
    border-color: {{color.canon.aqua}};
}
QPushButton:disabled {
    color: {{color.canon.muted}};
    border-color: {{color.canon.line}};
    background: transparent;
}
QCheckBox {
    background: transparent;
    color: {{color.canon.inkSoft}};
}
)";

// Labels and cards here carry {{token}} placeholders, so they go through the
// theme rather than setStyleSheet(), which would resolve nothing.
void applyStyle(QWidget* widget, const QString& style)
{
    AetherSDR::ThemeManager::instance().applyStyleSheet(widget, style);
}

QJsonObject loadRoutedProfiles()
{
    const QByteArray json =
        AppSettings::instance().value("RoutedProfilesJson", "{}").toString().toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QString normalizeManualIp(const QString& ip)
{
    const QString trimmed = ip.trimmed();
    if (trimmed.isEmpty())
        return QString();

    // A numeric address is canonicalised, so "192.168.001.5" and
    // "192.168.1.5" are one entry rather than two.
    const QHostAddress address(trimmed);
    if (!address.isNull())
        return address.toString();

    // Host names are remembered too (QHostAddress parses numeric only); the
    // IC-705's documented default is ic-705.local. The charset is conservative
    // (alphanumeric ends; letters, digits, . - _) so this widens what is stored,
    // not what reaches a resolver.
    static const QRegularExpression hostName(
        QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?$"));
    if (trimmed.size() <= 253 && hostName.match(trimmed).hasMatch())
        return trimmed;

    return QString();
}

void setAutomationError(QString* error, const QString& text)
{
    if (error) {
        *error = text;
    }
}

QStringList sanitizeRecentManualIps(const QStringList& ips)
{
    QStringList sanitized;
    for (const auto& ip : ips) {
        const QString normalized = normalizeManualIp(ip);
        if (normalized.isEmpty() || sanitized.contains(normalized))
            continue;

        sanitized.append(normalized);
        if (sanitized.size() >= kMaxRecentManualIps)
            break;
    }
    return sanitized;
}

QStringList loadRecentManualIpSettings()
{
    QStringList ips;
    const QByteArray json =
        AppSettings::instance().value(kRecentManualIpsKey, "[]").toString().toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isArray()) {
        const QJsonArray array = doc.array();
        for (const auto& item : array)
            ips.append(item.toString());
    }

    if (ips.isEmpty()) {
        const QString legacyLastIp =
            AppSettings::instance().value("LastRoutedRadioIp").toString();
        if (!legacyLastIp.isEmpty())
            ips.append(legacyLastIp);
    }

    return sanitizeRecentManualIps(ips);
}

void saveRecentManualIpSettings(const QStringList& ips)
{
    QJsonArray array;
    for (const auto& ip : sanitizeRecentManualIps(ips))
        array.append(ip);

    auto& settings = AppSettings::instance();
    settings.setValue(kRecentManualIpsKey,
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.save();
}

void saveRoutedProfiles(const QJsonObject& profiles)
{
    auto& settings = AppSettings::instance();
    settings.setValue("RoutedProfilesJson",
                      QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact)));
    settings.save();
}

// Radio family remembered for one manual address. Older profiles predate the
// selector and carry no identity.family — treat those as flex, which is what
// they were when they were written.
QString familyFromProfile(const QJsonObject& profile)
{
    const QString family =
        profile.value("identity").toObject().value("family").toString().trimmed().toLower();
    if (family == QLatin1String(ConnectionPanel::kFamilyAnan))
        return QString::fromLatin1(ConnectionPanel::kFamilyAnan);
    return QString::fromLatin1(ConnectionPanel::kFamilyFlex);
}

RadioBindSettings bindSettingsFromProfile(const QJsonObject& profile)
{
    const QJsonObject bind = profile.value("bind").toObject();
    RadioBindSettings settings;
    settings.mode = bind.value("mode").toString() == "explicit"
        ? RadioBindMode::Explicit
        : RadioBindMode::Auto;
    settings.interfaceId = bind.value("interface_id").toString();
    settings.interfaceName = bind.value("interface_name").toString();
    settings.bindAddress = QHostAddress(bind.value("last_successful_ipv4").toString());
    return settings;
}

QString staleSelectionText(const RadioBindSettings& settings)
{
    QString iface = settings.interfaceName.trimmed();
    if (iface.isEmpty())
        iface = settings.interfaceId.trimmed();
    if (iface.isEmpty())
        iface = QStringLiteral("Saved source");
    const QString addr = settings.bindAddress.isNull()
        ? QStringLiteral("unknown address")
        : settings.bindAddress.toString();
    return QStringLiteral("%1 (unavailable, last %2)").arg(iface, addr);
}

QLabel* makeWrappedLabel(const QString& text, const char* style = nullptr)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    if (style)
        applyStyle(label, QString::fromLatin1(style));
    return label;
}

QString smartLinkUserText(const SmartLinkClient* client)
{
    if (!client)
        return QStringLiteral("Sign in to see radios at remote stations.");

    if (!client->firstName().isEmpty()) {
        const QString call = client->callsign().trimmed();
        if (!call.isEmpty()) {
            return QStringLiteral("%1 %2 (%3)")
                .arg(client->firstName().trimmed(),
                     client->lastName().trimmed(),
                     call);
        }
        return QStringLiteral("%1 %2")
            .arg(client->firstName().trimmed(), client->lastName().trimmed());
    }

    if (!client->callsign().trimmed().isEmpty())
        return QStringLiteral("Signed in as %1").arg(client->callsign().trimmed());

    return QStringLiteral("Signed in to SmartLink");
}

QString normalizedStatus(QString status)
{
    status.replace('_', ' ');
    return status.trimmed();
}

}

ConnectionPanel::ConnectionPanel(QWidget* parent)
    : CanonWindow(QStringLiteral("Connect to Radio"), parent)
{
    setObjectName(QStringLiteral("connectionPanel"));
    setAccessibleName(tr("Connect to Radio"));
    // MainWindow anchors it above the status bar before showing it, and Return
    // in a field (an IP address, a SmartLink password) must not also press the
    // first button on the page.
    setOwnerPlaced(true);
    setReturnClicksDefault(false);

    theme::setContainer(this, QStringLiteral("canon/connection"));
    applyCanonSheet(bodyWidget(), [] {
        return QString::fromLatin1(kConnectionStyle) + canonIndicatorRules();
    });
    const QString editStyle =
        "QLineEdit { border: 1px solid {{color.canon.lineHi}}; border-radius: 4px; padding: 4px 6px; "
        "background: {{color.canon.control}}; color: {{color.canon.ink}}; }"
        "QLineEdit:focus { border-color: {{color.canon.aqua}}; }";
    // Uses the shared ComboStyle (themed, real painted arrow). The override is
    // for row height: these rows are 30 px vs the 22 px applet combos the
    // template was shaped for, so the text needs a larger inset.
    const QString comboExtraRules =
        "QComboBox { padding: 4px 8px; }"
        // Pin the drop-down to the BORDER box. Without this it inherits the
        // padding above and floats inward, detached from the frame's right edge.
        "QComboBox::drop-down { subcontrol-origin: border;"
        " subcontrol-position: top right; width: 22px; border: none; }";

    const QString modeCardStyle =
        "QCommandLinkButton { text-align: left; border: 1px solid {{color.canon.line}}; border-radius: 8px; "
        "padding: 10px 12px; background: {{color.canon.raised}}; color: {{color.canon.ink}}; }"
        "QCommandLinkButton:hover { border-color: {{color.canon.lineHi}}; background: {{color.canon.nested}}; }"
        "QCommandLinkButton:checked { border-color: {{color.canon.cyan}}; background: {{color.canon.nested}}; }";
    const QString calloutStyle =
        "QFrame#connectionCallout { border: 1px solid {{color.canon.line}}; border-radius: 8px; "
        "background: {{color.canon.nested}}; }"
        "QFrame#connectionCallout QLabel { background: transparent; border: none; }"
        "QFrame#connectionCallout QCheckBox { background: transparent; border: none; }";
    // The indicator is the canon one, from the body sheet.
    const QString lowBandwidthCheckStyle =
        "QCheckBox { color: {{color.canon.inkSoft}}; spacing: 8px; padding: 2px 0; "
        "background: transparent; border: none; }";

    auto* outer = new QVBoxLayout(bodyWidget());
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Canon header in place of a title bar, fixed above the scrolling body.
    auto* header = makeCanonHeader(tr("Connect to a Radio"));
    header->setContentsMargins(14, 12, 14, 4);
    outer->addWidget(header);

    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("connectionBodyContent"));
    auto* root = new QVBoxLayout(content);
    // No right margin: the 12 px band on that edge belongs to the scroll area
    // (see bodyContainer below), so adding one here would inset the body 24 px
    // from the right against 12 px on the left and 12 px on the footer.
    root->setContentsMargins(12, 6, 0, 10);
    root->setSpacing(10);
    m_rootLayout = root;
    m_bodyContent = content;

    auto* bodyScroll = new QScrollArea(this);
    bodyScroll->setObjectName(QStringLiteral("connectionBodyScrollArea"));
    bodyScroll->setAccessibleName(tr("Connection options"));
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setFrameShape(QFrame::NoFrame);
    bodyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    bodyScroll->setWidget(content);
    m_bodyScroll = bodyScroll;

    // Keep the scrollbars clear of FramelessResizer's edge-grab zone. The
    // fixed footer already carries its own inset; the scrolling body needs one
    // too because QScrollArea places its vertical bar at its outer edge.
    auto* bodyContainer = new QWidget(this);
    auto* bodyContainerLayout = new QVBoxLayout(bodyContainer);
    bodyContainerLayout->setContentsMargins(0, 0, 12, 0);
    bodyContainerLayout->setSpacing(0);
    bodyContainerLayout->addWidget(bodyScroll);
    outer->addWidget(bodyContainer, 1);

    auto* introLabel = makeWrappedLabel(
        "Pick the simplest path for your station. Most first-time users should start with "
        "\"On This Network\" and only use the IP path for VPN or routed connections.",
        kHintLabelStyle);
    root->addWidget(introLabel);

    m_modeButtons = new QButtonGroup(this);
    m_modeButtons->setExclusive(true);

    auto configureModeButton = [&](QCommandLinkButton* button,
                                   const QString& title,
                                   const QString& description,
                                   ConnectionMode mode) {
        button->setText(title);
        button->setDescription(description);
        button->setCheckable(true);
        applyStyle(button, modeCardStyle);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        button->setMinimumHeight(100);
        m_modeButtons->addButton(button, static_cast<int>(mode));
    };

    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);

    m_localModeBtn = new QCommandLinkButton(this);
    m_localModeBtn->setObjectName(QStringLiteral("connectionLocalModeButton"));
    configureModeButton(m_localModeBtn,
                        "On This Network",
                        "Recommended for new users when the radio and computer are on the same LAN.",
                        LocalMode);
    modeRow->addWidget(m_localModeBtn);

    m_smartLinkModeBtn = new QCommandLinkButton(this);
    m_smartLinkModeBtn->setObjectName(QStringLiteral("connectionSmartLinkModeButton"));
    configureModeButton(m_smartLinkModeBtn,
                        "Remote with SmartLink",
                        "Use FlexRadio SmartLink when the radio is away from this computer.",
                        SmartLinkMode);
    modeRow->addWidget(m_smartLinkModeBtn);

    m_manualModeBtn = new QCommandLinkButton(this);
    m_manualModeBtn->setObjectName(QStringLiteral("connectionManualModeButton"));
    configureModeButton(m_manualModeBtn,
                        "Connect by IP",
                        "Best for VPN or routed station access when you already know the radio IP.",
                        ManualMode);
    modeRow->addWidget(m_manualModeBtn);

    root->addLayout(modeRow);

    m_modeStack = new QStackedWidget(this);
    root->addWidget(m_modeStack, 1);

    // ── Local page ────────────────────────────────────────────────────────
    auto* localPage = new QWidget(m_modeStack);
    auto* localLayout = new QVBoxLayout(localPage);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->setSpacing(8);
    localLayout->addWidget(makeWrappedLabel(
        "Discovery finds radios automatically on your local network. If nothing appears, "
        "guest Wi-Fi isolation, VPN software, or firewall rules may be blocking discovery.",
        kHintLabelStyle));

    m_localStateStack = new QStackedWidget(localPage);
    localLayout->addWidget(m_localStateStack, 1);

    auto* localListPage = new QWidget(m_localStateStack);
    auto* localListLayout = new QVBoxLayout(localListPage);
    localListLayout->setContentsMargins(0, 0, 0, 0);
    localListLayout->setSpacing(8);
    auto* localGroup = new QGroupBox("Available radios", localListPage);
    auto* localGroupLayout = new QVBoxLayout(localGroup);
    m_radioList = new QListWidget(localGroup);
    m_radioList->setObjectName(QStringLiteral("connectionLocalRadioList"));
    m_radioList->setAccessibleName(tr("Available local radios"));
    m_radioList->setAccessibleDescription(
        tr("Discovered FlexRadio radios on the local network"));
    m_radioList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_radioList->setWordWrap(true);
    m_radioList->setSpacing(2);
    // Bound the list height so it scrolls internally when more radios are
    // discovered than fit — otherwise on a small display (e.g. a 1024x600 Pi
    // panel) the list grew past the dialog and the Connect button and lower
    // radios became unreachable. Keep a modest minimum, cap the maximum, and
    // force the vertical scrollbar so overflow is always reachable (some
    // themes render an as-needed bar invisibly). Mirrors the WAN list below.
    m_radioList->setMinimumHeight(120);
    m_radioList->setMaximumHeight(240);
    m_radioList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_radioList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_radioList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    localGroupLayout->addWidget(m_radioList);

    // Right-click a discovered radio to set a custom nickname without connecting
    // first. Only radios without an on-radio name store (non-Flex: ANAN)
    // are stored client-side; Flex's own "radio name" is set from Radio Setup
    // while connected, so we don't offer it here for Flex to avoid two sources of
    // truth. The nickname is persisted keyed by serial and picked up on the next
    // discovery sweep.
    m_radioList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_radioList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showRadioContextMenu(pos); });
    localListLayout->addWidget(localGroup, 1);

    auto* localActionRow = new QHBoxLayout;
    localActionRow->addStretch();
    m_localConnectBtn = new QPushButton("Connect Selected Radio", localListPage);
    m_localConnectBtn->setObjectName(QStringLiteral("connectionLocalConnectButton"));
    m_localConnectBtn->setAccessibleName(tr("Connect selected local radio"));
    m_localConnectBtn->setEnabled(false);
    localActionRow->addWidget(m_localConnectBtn);
    localListLayout->addLayout(localActionRow);
    m_localStateStack->addWidget(localListPage);

    m_localEmptyState = new QWidget(m_localStateStack);
    auto* emptyLayout = new QVBoxLayout(m_localEmptyState);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(8);
    auto* emptyCallout = new QFrame(m_localEmptyState);
    emptyCallout->setObjectName("connectionCallout");
    applyStyle(emptyCallout, calloutStyle);
    auto* emptyCalloutLayout = new QVBoxLayout(emptyCallout);
    emptyCalloutLayout->setContentsMargins(14, 14, 14, 14);
    emptyCalloutLayout->setSpacing(8);
    auto* emptyTitle = new QLabel("No local radios found yet", emptyCallout);
    AetherSDR::ThemeManager::instance().applyStyleSheet(emptyTitle, "QLabel { color: {{color.text.primary}}; font-size: 15px; font-weight: bold; "
        "background: transparent; border: none; }");
    emptyCalloutLayout->addWidget(emptyTitle);
    emptyCalloutLayout->addWidget(makeWrappedLabel(
        "AetherSDR is still listening for discovery packets. If your station is on a VPN "
        "or another routed network, switch to \"Connect by IP\" instead.",
        kHintLabelStyle));

    auto* retryBtn = new QPushButton("Retry Discovery", emptyCallout);
    auto* useSmartLinkBtn = new QPushButton("Remote with SmartLink", emptyCallout);
    auto* connectByIpBtn = new QPushButton("Connect by IP", emptyCallout);
    auto* diagnosticsBtn = new QPushButton("Open Network Diagnostics", emptyCallout);
    emptyCalloutLayout->addWidget(retryBtn);
    emptyCalloutLayout->addWidget(connectByIpBtn);
    emptyCalloutLayout->addWidget(useSmartLinkBtn);
    emptyCalloutLayout->addWidget(diagnosticsBtn);
    emptyLayout->addWidget(emptyCallout);
    emptyLayout->addStretch();
    m_localStateStack->addWidget(m_localEmptyState);

    m_modeStack->addWidget(localPage);

    // ── SmartLink page ────────────────────────────────────────────────────
    auto* smartLinkPage = new QWidget(m_modeStack);
    auto* smartLinkLayout = new QVBoxLayout(smartLinkPage);
    smartLinkLayout->setContentsMargins(0, 0, 0, 0);
    smartLinkLayout->setSpacing(8);
    smartLinkLayout->addWidget(makeWrappedLabel(
        "Use SmartLink when the radio is at another location. Sign in, choose a remote radio, "
        "then connect over the internet.",
        kHintLabelStyle));

    auto* accountGroup = new QGroupBox("SmartLink account", smartLinkPage);
    auto* accountLayout = new QVBoxLayout(accountGroup);
    accountLayout->setSpacing(6);

    m_loginForm = new QWidget(accountGroup);
    auto* loginLayout = new QFormLayout(m_loginForm);
    loginLayout->setContentsMargins(0, 0, 0, 0);
    loginLayout->setHorizontalSpacing(8);
    loginLayout->setVerticalSpacing(6);
    // Accessibility + password-manager hints.  macOS Passwords, Windows
    // Authenticator, and KDE Wallet read the OS accessibility tree to
    // associate credential fields; the objectName + accessibleName +
    // accessibleDescription tuple is what they look for.  m_loginForm
    // is named so the password manager can scope the credential pair
    // ("SmartLink login form" vs "MQTT login form").
    m_loginForm->setObjectName(QStringLiteral("smartlinkLoginForm"));
    m_loginForm->setAccessibleName(tr("SmartLink account login"));

    m_emailEdit = new QLineEdit(m_loginForm);
    applyStyle(m_emailEdit, editStyle);
    m_emailEdit->setPlaceholderText("flexradio account email");
    m_emailEdit->setObjectName(QStringLiteral("smartlinkEmail"));
    m_emailEdit->setAccessibleName(tr("SmartLink account email"));
    m_emailEdit->setAccessibleDescription(
        tr("FlexRadio account email address used to sign in to SmartLink"));
    QString storedEmail = AppSettings::instance().value("SmartLinkEmail").toString();
    if (!storedEmail.isEmpty())
        m_emailEdit->setText(QString::fromUtf8(QByteArray::fromBase64(storedEmail.toUtf8())));
    m_passwordEdit = new QLineEdit(m_loginForm);
    applyStyle(m_passwordEdit, editStyle);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("password");
    m_passwordEdit->setObjectName(QStringLiteral("smartlinkPassword"));
    m_passwordEdit->setAccessibleName(tr("SmartLink account password"));
    m_passwordEdit->setAccessibleDescription(
        tr("FlexRadio account password used to sign in to SmartLink"));
    loginLayout->addRow("Email:", m_emailEdit);
    loginLayout->addRow("Password:", m_passwordEdit);
    m_loginBtn = new QPushButton("Sign In", m_loginForm);
    loginLayout->addRow(QString(), m_loginBtn);
    accountLayout->addWidget(m_loginForm);

    auto* accountActionBar = new QWidget(accountGroup);
    auto* accountActionRow = new QHBoxLayout(accountActionBar);
    accountActionRow->setContentsMargins(0, 0, 0, 0);
    accountActionRow->setSpacing(10);

    m_logoutBtn = new QPushButton("Sign Out", accountActionBar);
    m_logoutBtn->setVisible(false);
    accountActionRow->addWidget(m_logoutBtn);
    m_wanDisconnectClientsBtn = new QPushButton("Disconnect Remote Clients", accountActionBar);
    m_wanDisconnectClientsBtn->setVisible(false);
    m_wanDisconnectClientsBtn->setEnabled(false);
    accountActionRow->addWidget(m_wanDisconnectClientsBtn);
    accountActionRow->addStretch();
    accountLayout->addWidget(accountActionBar);

    m_slUserLabel = makeWrappedLabel(QString());
    setAccountLine(QStringLiteral("Sign in to see radios at remote stations."), AccountTone::Hint);
    accountLayout->addWidget(m_slUserLabel);
    smartLinkLayout->addWidget(accountGroup);

    auto* remoteGroup = new QGroupBox("Remote radios", smartLinkPage);
    auto* remoteLayout = new QVBoxLayout(remoteGroup);
    remoteLayout->setSpacing(10);
    m_wanList = new QListWidget(remoteGroup);
    m_wanList->setObjectName(QStringLiteral("connectionSmartLinkRadioList"));
    m_wanList->setAccessibleName(tr("Available SmartLink radios"));
    m_wanList->setAccessibleDescription(
        tr("Remote FlexRadio radios available through SmartLink"));
    m_wanList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wanList->setWordWrap(true);
    m_wanList->setSpacing(2);
    m_wanList->setMinimumHeight(120);
    m_wanList->setMaximumHeight(160);
    m_wanList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    remoteLayout->addWidget(m_wanList);
    m_smartLinkEmptyLabel = makeWrappedLabel(
        "Remote radios appear here after SmartLink sign-in.",
        kHintLabelStyle);
    remoteLayout->addWidget(m_smartLinkEmptyLabel);
    smartLinkLayout->addWidget(remoteGroup);

    auto* wanActionBar = new QWidget(smartLinkPage);
    auto* wanActionRow = new QHBoxLayout(wanActionBar);
    wanActionRow->setContentsMargins(0, 0, 0, 0);
    wanActionRow->setSpacing(10);
    wanActionRow->addStretch();
    m_wanConnectBtn = new QPushButton("Connect Remote Radio", wanActionBar);
    m_wanConnectBtn->setObjectName(QStringLiteral("connectionSmartLinkConnectButton"));
    m_wanConnectBtn->setAccessibleName(tr("Connect selected SmartLink radio"));
    m_wanConnectBtn->setEnabled(false);
    m_wanConnectBtn->setMinimumWidth(190);
    wanActionRow->addWidget(m_wanConnectBtn);
    smartLinkLayout->addWidget(wanActionBar);
    smartLinkLayout->addStretch(1);

    m_modeStack->addWidget(smartLinkPage);

    // ── Manual / VPN page ────────────────────────────────────────────────
    auto* manualPage = new QWidget(m_modeStack);
    auto* manualLayout = new QVBoxLayout(manualPage);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(8);
    m_manualHintLabel = makeWrappedLabel(QString(), kHintLabelStyle);
    m_manualHintLabel->setObjectName(QStringLiteral("connectionManualHintLabel"));
    manualLayout->addWidget(m_manualHintLabel);

    auto* manualGroup = new QGroupBox("Radio IP address", manualPage);
    // Baseline floor for the collapsed page. The rows and the result line carry
    // explicit minimums of their own, but a word-wrapped QLabel under-reports
    // its height, so without this the surrounding layout can still hand the
    // group less than it needs and Qt overlaps children. The taller states
    // (Advanced expanded) are covered by the enclosing QScrollArea, which never
    // gives the body less than qSmartMinSize() and scrolls the difference.
    manualGroup->setMinimumHeight(240);
    auto* manualGroupLayout = new QVBoxLayout(manualGroup);
    manualGroupLayout->setContentsMargins(12, 14, 12, 12);
    manualGroupLayout->setSpacing(12);
    // Explicit label+field rows rather than a QFormLayout. A QFormLayout nested
    // inside the group's QVBoxLayout squeezed both rows to a few pixels tall
    // once the wrapped error message appeared below them, so the labels ended up
    // overlapping the fields. Plain rows with a fixed-width label column give
    // the two entries a stable height and keep the labels aligned.
    auto* manualForm = new QVBoxLayout;
    manualForm->setContentsMargins(0, 0, 0, 0);
    manualForm->setSpacing(10);

    constexpr int kManualLabelWidth = 96;
    constexpr int kManualFieldHeight = 30;
    // Every row's label, so the column can be squared up once they all exist.
    //
    // setMinimumWidth() alone did NOT give a column. With a Fixed size policy Qt
    // caps a widget at its own sizeHint, so a label narrower than the minimum
    // sat at 96 while "Icom password:" — which is wider than 96 — sat at its own
    // hint, and the four fields started at three different x positions. The
    // width has to come from the WIDEST label, and that is not known until the
    // last row is added.
    QList<QLabel*> manualRowLabels;
    // Returns the row CONTAINER so a caller can hide the label and the field
    // together. Takes QWidget* rather than QComboBox* because the Icom
    // credential fields are QLineEdits; both calls below it are QWidget methods.
    const auto addManualRow = [&](const QString& labelText, QWidget* field) -> QWidget* {
        auto* rowWidget = new QWidget(manualGroup);
        auto* row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(12);
        auto* label = new QLabel(labelText, rowWidget);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(kManualFieldHeight);
        manualRowLabels.append(label);
        field->setMinimumHeight(kManualFieldHeight);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(label);
        row->addWidget(field, 1);
        manualForm->addWidget(rowWidget);
        return rowWidget;
    };

    // Radio type. A FlexRadio answers a TCP/4992 command-plane probe; an ANAN
    // answers an openHPSDR Protocol 2 discovery datagram on UDP/1024 and will
    // never answer the Flex probe (and vice versa), so the operator tells us
    // which wire protocol to speak.
    m_manualRadioTypeCombo = new QComboBox(manualGroup);
    m_manualRadioTypeCombo->setObjectName(QStringLiteral("connectionManualRadioType"));
    m_manualRadioTypeCombo->setAccessibleName(tr("Radio type"));
    m_manualRadioTypeCombo->setAccessibleDescription(
        tr("Which radio family to look for at the address below"));
    AetherSDR::applyComboStyle(m_manualRadioTypeCombo, comboExtraRules);
    m_manualRadioTypeCombo->addItem(tr("FlexRadio"), QString::fromLatin1(kFamilyFlex));
    m_manualRadioTypeCombo->addItem(tr("ANAN-100D"), QString::fromLatin1(kFamilyAnan));
    addManualRow(QStringLiteral("Radio type:"), m_manualRadioTypeCombo);

    m_manualIpCombo = new QComboBox(manualGroup);
    m_manualIpCombo->setObjectName(QStringLiteral("connectionManualIpCombo"));
    m_manualIpCombo->setAccessibleName(tr("Radio IP address"));
    m_manualIpCombo->setAccessibleDescription(
        tr("IP address or host name for a routed or VPN radio connection"));
    m_manualIpCombo->setEditable(true);
    m_manualIpCombo->setInsertPolicy(QComboBox::NoInsert);
    m_manualIpCombo->setMaxVisibleItems(kMaxRecentManualIps);
    AetherSDR::applyComboStyle(m_manualIpCombo, comboExtraRules);
    m_manualIpEdit = m_manualIpCombo->lineEdit();
    m_manualIpEdit->setObjectName(QStringLiteral("connectionManualIp"));
    m_manualIpEdit->setAccessibleName(tr("Radio IP address"));
    m_manualIpEdit->setAccessibleDescription(
        tr("IP address or host name for a routed or VPN radio connection"));
    m_manualIpEdit->setClearButtonEnabled(true);
    m_manualIpEdit->setPlaceholderText("Example: 10.0.0.25");
    // NO HEIGHT ON THE EDITOR. The combo around it is 30 px with 4 px of
    // vertical padding, which leaves a 22 px content box; a 26 px minimum on
    // the editor overflows that box and Qt resolves it downward, so the
    // address sat visibly below the centre of its own field — and below the
    // Radio type text in the row above it. The row's height is the combo's
    // to set (addManualRow does), and the editor fills what it is given.
    addManualRow(QStringLiteral("Radio IP:"), m_manualIpCombo);

    // ANAN's front-end options (DDC0 rate, ADC select, dither,
    // randomization, the two filter-bank bypasses and the radio-speaker stream)
    // are set on Settings > Radio Setup > "ANAN Front End", which is reachable
    // with no radio connected: they configure the session a connect starts, so
    // they must be settable before the first one. The connect path reads them
    // from AnanSettings (RadioModel's populateFamilyParams()), never from this
    // form; this row only points there.
    //
    // Added straight to manualForm rather than through addManualRow(): that
    // helper gives its field a FIXED row height, which would clip a label that
    // wraps.
    auto* ananHint = makeWrappedLabel(
        QStringLiteral("Sample rate, ADC choice, dither, randomization, filter bypass and the "
                       "radio's own speaker are in Settings > Radio Setup > ANAN Front End. "
                       "They apply to the next connect."),
        kHintLabelStyle);
    ananHint->setObjectName(QStringLiteral("connectionManualAnanSetupHint"));
    ananHint->setParent(manualGroup);
    manualForm->addWidget(ananHint);
    m_manualAnanHintRow = ananHint;

    // One column, set from the widest label. Rows that are hidden for a family
    // still count: the Icom rows appear and disappear as the operator changes
    // radio type, and a column that resized with them would move the Radio type
    // and Radio IP fields sideways every time.
    {
        int labelColumnWidth = kManualLabelWidth;
        for (QLabel* label : manualRowLabels)
            labelColumnWidth = std::max(labelColumnWidth, label->sizeHint().width());
        for (QLabel* label : manualRowLabels)
            label->setFixedWidth(labelColumnWidth);
    }

    manualGroupLayout->addLayout(manualForm);

    auto* manualActionRow = new QHBoxLayout;
    manualActionRow->setSpacing(10);
    auto* manualDiagnosticsBtn = new QPushButton("Network Diagnostics", manualGroup);
    manualActionRow->addWidget(manualDiagnosticsBtn);
    manualActionRow->addStretch();
    m_manualConnectBtn = new QPushButton("Connect by IP", manualGroup);
    m_manualConnectBtn->setObjectName(QStringLiteral("connectionManualConnectButton"));
    m_manualConnectBtn->setAccessibleName(tr("Connect by IP"));
    manualActionRow->addWidget(m_manualConnectBtn);
    manualGroupLayout->addLayout(manualActionRow);

    m_manualResultLabel = makeWrappedLabel(QString(), kHintLabelStyle);
    // Always laid out, even when empty. A word-wrapped QLabel reports a
    // one-line minimum height, so showing a message that wraps to two or three
    // lines used to grow the page past the dialog height and Qt resolved the
    // overflow by overlapping the rows above it — the "Radio type"/"Radio IP"
    // labels ended up on top of their own entry fields. Reserving the space up
    // front keeps the page height constant whether or not a message is showing.
    m_manualResultLabel->setMinimumHeight(36);
    m_manualResultLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_manualResultLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    manualGroupLayout->addWidget(m_manualResultLabel);

    m_manualAdvancedToggle = new QToolButton(manualGroup);
    m_manualAdvancedToggle->setObjectName(QStringLiteral("connectionManualAdvancedToggle"));
    m_manualAdvancedToggle->setText("Advanced: choose the VPN source path");
    m_manualAdvancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_manualAdvancedToggle->setCheckable(true);
    m_manualAdvancedToggle->setArrowType(Qt::RightArrow);
    m_manualAdvancedToggle->setVisible(false);
    manualGroupLayout->addWidget(m_manualAdvancedToggle, 0, Qt::AlignLeft);

    m_manualAdvancedWidget = new QWidget(manualGroup);
    m_manualAdvancedWidget->setObjectName(QStringLiteral("connectionManualAdvancedSection"));
    auto* manualAdvancedLayout = new QVBoxLayout(m_manualAdvancedWidget);
    manualAdvancedLayout->setContentsMargins(8, 4, 8, 4);
    manualAdvancedLayout->setSpacing(6);
    auto* manualAdvancedHint = makeWrappedLabel(
        "Most users can leave this on Auto. Pick a source path only if your VPN creates more "
        "than one active network adapter or a saved path is no longer available.",
        kHintLabelStyle);
    manualAdvancedHint->setObjectName(QStringLiteral("connectionManualAdvancedHint"));
    manualAdvancedLayout->addWidget(manualAdvancedHint);
    auto* sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins(0, 0, 0, 0);
    sourceRow->addWidget(new QLabel("Source path:", m_manualAdvancedWidget));
    m_manualSourceCombo = new QComboBox(m_manualAdvancedWidget);
    m_manualSourceCombo->setObjectName(QStringLiteral("connectionManualSourcePath"));
    m_manualSourceCombo->setAccessibleName(tr("Manual connection source path"));
    m_manualSourceCombo->setAccessibleDescription(
        tr("Network interface or source address to use for the routed radio connection"));
    sourceRow->addWidget(m_manualSourceCombo, 1);
    manualAdvancedLayout->addLayout(sourceRow);
    m_manualSourceWarningLabel = makeWrappedLabel(QString(), kErrorLabelStyle);
    m_manualSourceWarningLabel->setVisible(false);
    manualAdvancedLayout->addWidget(m_manualSourceWarningLabel);
    m_manualAdvancedWidget->setVisible(false);
    manualGroupLayout->addWidget(m_manualAdvancedWidget);
    manualLayout->addWidget(manualGroup);
    manualLayout->addStretch();

    m_modeStack->addWidget(manualPage);

    // ── Contextual options ────────────────────────────────────────────────
    m_linkOptionsWidget = new QFrame(this);
    m_linkOptionsWidget->setObjectName("connectionCallout");
    applyStyle(m_linkOptionsWidget, calloutStyle);
    auto* optionsLayout = new QVBoxLayout(m_linkOptionsWidget);
    optionsLayout->setContentsMargins(12, 10, 12, 10);
    optionsLayout->setSpacing(6);
    auto* optionsTitle = new QLabel("Connection options for slower links", m_linkOptionsWidget);
    AetherSDR::ThemeManager::instance().applyStyleSheet(optionsTitle, "QLabel { color: {{color.text.primary}}; font-weight: bold; background: transparent; border: none; }");
    optionsLayout->addWidget(optionsTitle);
    m_lowBwHintLabel = makeWrappedLabel(QString(), kHintLabelStyle);
    optionsLayout->addWidget(m_lowBwHintLabel);
    m_lowBwCheck = new QCheckBox("Use low bandwidth mode", m_linkOptionsWidget);
    const auto remoteLowBandwidth = AppSettings::instance()
        .value("LowBandwidthRemotePreferred",
               AppSettings::instance().value("LowBandwidthConnect", "False"))
        .toString();
    m_lowBwCheck->setChecked(remoteLowBandwidth == "True");
    m_lowBwCheck->setToolTip("Reduces FFT and waterfall traffic from the radio.");
    applyStyle(m_lowBwCheck, lowBandwidthCheckStyle);
    optionsLayout->addWidget(m_lowBwCheck);
    root->addWidget(m_linkOptionsWidget);

    m_adaptiveThrottleCheck = new QCheckBox("Enable adaptive frame-rate throttle", this);
    const bool adaptiveEnabled = AppSettings::instance()
        .value("AdaptiveThrottleEnabled", "False").toString() == "True";
    m_adaptiveThrottleCheck->setChecked(adaptiveEnabled);
    m_adaptiveThrottleCheck->setToolTip(
        "Automatically reduces FFT/waterfall frame rate when network quality degrades, "
        "reducing the chance of a disconnect on congested links. "
        "Toggling while connected takes effect at the next quality update; "
        "reconnect to lift an already-applied cap immediately.");
    applyStyle(m_adaptiveThrottleCheck, lowBandwidthCheckStyle);
    connect(m_adaptiveThrottleCheck, &QCheckBox::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AdaptiveThrottleEnabled", on ? "True" : "False");
        s.save();
    });
    root->addWidget(m_adaptiveThrottleCheck);

    m_autoConnectCheck = new QCheckBox("Connect to last radio on start up", this);
    m_autoConnectCheck->setChecked(
        AppSettings::instance().value("AutoConnectToLastRadio", "True").toString() == "True");
    applyStyle(m_autoConnectCheck, lowBandwidthCheckStyle);
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoConnectToLastRadio", on ? "True" : "False");
        s.save();
    });
    root->addWidget(m_autoConnectCheck);

    // ── Footer ────────────────────────────────────────────────────────────
    auto* footerRow = new QHBoxLayout;
    footerRow->setSpacing(8);
    m_statusLabel = makeWrappedLabel("Choose how you want to connect.", kHintLabelStyle);
    footerRow->addWidget(m_statusLabel, 1);
    m_disconnectBtn = new QPushButton("Disconnect", this);
    m_disconnectBtn->setObjectName(QStringLiteral("connectionDisconnectButton"));
    m_disconnectBtn->setAccessibleName(tr("Disconnect from radio"));
    m_disconnectBtn->setVisible(false);
    footerRow->addWidget(m_disconnectBtn, 0, Qt::AlignRight);

    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("connectionFooter"));
    footer->setLayout(footerRow);
    footerRow->setContentsMargins(12, 0, 12, 12);
    outer->addWidget(footer);

    // Family first: applySavedSourceSelection() may override it with the
    // per-address choice remembered for whichever recent IP we preselect.
    setManualFamily(AppSettings::instance()
                        .value(kManualRadioFamilyKey, QString::fromLatin1(kFamilyFlex))
                        .toString());
    loadRecentManualIps();
    applySavedSourceSelection(m_manualIpEdit->text().trimmed());

    connect(m_modeButtons, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &ConnectionPanel::onConnectionModeClicked);

    connect(retryBtn, &QPushButton::clicked, this, [this] {
        setStatusText("Refreshing local discovery…");
        emit retryDiscoveryRequested();
    });
    connect(connectByIpBtn, &QPushButton::clicked, this, [this] {
        setCurrentMode(ManualMode);
        m_manualIpEdit->setFocus();
        m_manualIpEdit->selectAll();
    });
    connect(useSmartLinkBtn, &QPushButton::clicked, this, [this] {
        setCurrentMode(SmartLinkMode);
        if (m_loginForm->isVisible())
            m_emailEdit->setFocus();
    });
    connect(diagnosticsBtn, &QPushButton::clicked,
            this, &ConnectionPanel::networkDiagnosticsRequested);
    connect(manualDiagnosticsBtn, &QPushButton::clicked,
            this, &ConnectionPanel::networkDiagnosticsRequested);

    connect(m_radioList, &QListWidget::itemSelectionChanged,
            this, &ConnectionPanel::onListSelectionChanged);
    connect(m_wanList, &QListWidget::itemSelectionChanged,
            this, &ConnectionPanel::onWanSelectionChanged);
    connect(m_localConnectBtn, &QPushButton::clicked,
            this, &ConnectionPanel::onLocalConnectClicked);
    connect(m_wanConnectBtn, &QPushButton::clicked,
            this, &ConnectionPanel::onWanConnectClicked);
    connect(m_wanDisconnectClientsBtn, &QPushButton::clicked,
            this, &ConnectionPanel::onWanDisconnectClientsClicked);
    connect(m_disconnectBtn, &QPushButton::clicked,
            this, &ConnectionPanel::disconnectRequested);
    connect(m_radioList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (!m_connected)
            onLocalConnectClicked();
    });
    connect(m_wanList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (!m_connected)
            onWanConnectClicked();
    });

    connect(m_manualConnectBtn, &QPushButton::clicked,
            this, &ConnectionPanel::onManualConnectClicked);
    connect(m_manualIpEdit, &QLineEdit::returnPressed,
            this, &ConnectionPanel::onManualConnectClicked);
    connect(m_manualIpEdit, &QLineEdit::textChanged,
            this, &ConnectionPanel::onManualIpChanged);
    // Editing the route hands control back to the operator, including while
    // the asynchronous startup credential read is still pending.
    connect(m_manualIpEdit, &QLineEdit::textEdited, this, [this] {
        m_startupProbe = false;
    });
    connect(m_manualRadioTypeCombo, &QComboBox::activated, this, [this] {
        m_startupProbe = false;
    });
    // The address was CHOSEN, not typed — restore its remembered radio type.
    // `activated` fires only for a pick out of the popup, which is exactly the
    // gesture the per-address family restore was written for; the keystroke
    // path deliberately does not carry it (see applySavedSourceSelection).
    connect(m_manualIpCombo, &QComboBox::activated, this, [this](int) {
        m_startupProbe = false;
        applySavedSourceSelection(m_manualIpEdit->text().trimmed(),
                                  /*restoreFamily=*/true);
    });
    connect(m_manualRadioTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                auto& settings = AppSettings::instance();
                settings.setValue(kManualRadioFamilyKey, currentManualFamily());
                settings.save();
                updateManualFamilyHints();
                setManualMessage(QString());
            });
    connect(m_manualAdvancedToggle, &QToolButton::toggled,
            this, &ConnectionPanel::onManualAdvancedToggled);
    connect(m_manualSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                updateManualAdvancedVisibility();
                setManualMessage(QString());
            });
    connect(m_manualSourceCombo, &QComboBox::activated, this, [this](int) {
        m_manualSourcePick = currentManualBindSettings();
    });

    const auto doLogin = [this] {
        const QString email = m_emailEdit->text().trimmed();
        const QString pass = m_passwordEdit->text();
        if (email.isEmpty() || pass.isEmpty())
            return;
        m_loginBtn->setEnabled(false);
        m_loginBtn->setText("Signing In...");
        emit smartLinkLoginRequested(email, pass);
    };
    connect(m_loginBtn, &QPushButton::clicked, this, doLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, doLogin);
    connect(m_logoutBtn, &QPushButton::clicked, this, [this] {
        if (!m_smartLink)
            return;
        m_smartLink->logout();
        m_wanRadios.clear();
        m_wanList->clear();
        setAccountLine(QStringLiteral("Signed out of SmartLink."), AccountTone::Hint);
        updateSmartLinkUi();
        emit smartLinkSignedOut();
    });

    // Settle the body layout so preferredClientHeight() has a real answer the
    // first time fitToScreen() asks for one. The height floor this used to set
    // is gone: the manual page is still the tallest of the three, but overflow
    // now belongs to the scroll area rather than to the top-level minimum.
    refitToContent();
    setMinimumSize(kSafeMinimumWidth, kSafeMinimumHeight);
    resize(kPreferredWidth, kPreferredHeight);

    setConnected(false);
    setCurrentMode(LocalMode);
    updateLocalPageState();
    updateSmartLinkUi();
    updateManualAdvancedVisibility();
    FramelessResizer::install(this);
}

bool ConnectionPanel::event(QEvent* e)
{
    switch (e->type()) {
    // A fit is only valid for the screen, DPI and font metrics it was made
    // under. #4515 asks for it to be redone when any of those change beneath an
    // open window — dragged to a shorter monitor, scaling changed in Display
    // settings, UI font swapped. Deferred by one turn of the event loop because
    // the children have not re-laid-out by the time these arrive, so a fit
    // computed here would use the outgoing metrics.
    case QEvent::ScreenChangeInternal:
    case QEvent::ApplicationFontChange:
    case QEvent::FontChange:
    case QEvent::StyleChange:
        if (isVisible()) {
            QTimer::singleShot(0, this, [this] {
                if (isVisible())
                    fitAndClampToScreen();
            });
        }
        break;
    default:
        break;
    }
    return CanonWindow::event(e);
}

QScreen* ConnectionPanel::screenFitTarget(QScreen* preferredScreen) const
{
    QScreen* targetScreen = preferredScreen;
    if (!targetScreen && windowHandle()) {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen && parentWidget()) {
        targetScreen = parentWidget()->screen();
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen;
}

int ConnectionPanel::preferredClientHeight(int clientWidth) const
{
    if (!m_bodyContent || !m_bodyScroll) {
        return kPreferredHeight;
    }
    // Everything in `outer` except the scroll area — the title bar when it is
    // shown, the fixed footer always. Taking it as a difference rather than
    // summing the two widgets keeps this correct without duplicating which of
    // them is visible in which frameless mode.
    const int chromeHeight = sizeHint().height() - m_bodyScroll->sizeHint().height();
    int bodyHeight = chromeHeight + m_bodyContent->sizeHint().height();

    // A body wider than the viewport shows a horizontal scrollbar, and that bar
    // takes its height out of the viewport. Left out, the fit comes up exactly
    // one bar short and the dialog opens with a vertical scrollbar anyway
    // (#5811). maximumViewportSize() is the viewport with no scrollbars, which
    // is the state this fit is aiming for; the horizontal chrome around it
    // carries over unchanged to the width the caller is about to apply.
    const int viewportWidth = clientWidth
        - (width() - m_bodyScroll->maximumViewportSize().width());
    const int contentMinWidth = m_bodyContent->minimumSizeHint()
                                    .expandedTo(m_bodyContent->minimumSize())
                                    .width();
    if (contentMinWidth > viewportWidth) {
        bodyHeight += m_bodyScroll->horizontalScrollBar()->sizeHint().height();
    }
    return qMax(kPreferredHeight, bodyHeight);
}

void ConnectionPanel::fitToScreen(QScreen* preferredScreen)
{
    QScreen* targetScreen = screenFitTarget(preferredScreen);
    if (!targetScreen) {
        return;
    }

    // Realise the native window before measuring. QWindow::frameMargins() is
    // only populated once the platform window exists, so on the very first open
    // an un-created panel falls back to the style estimate — which on Windows,
    // the platform #4515 was reported on, is roughly a title bar short.
    if (!windowHandle()) {
        (void)winId();
    }

    const QRect available = targetScreen->availableGeometry();
    const QMargins frameMargins = screenFitFrameMargins();

    const int maximumClientWidth =
        qMax(1, available.width() - frameMargins.left() - frameMargins.right());
    const int maximumClientHeight =
        qMax(1, available.height() - frameMargins.top() - frameMargins.bottom());
    setMinimumSize(qMin(kSafeMinimumWidth, maximumClientWidth),
                   qMin(kSafeMinimumHeight, maximumClientHeight));

    // Shrinking is the invariant: the frame must fit the work area whatever the
    // operator wants. Growing is a courtesy, so it only applies to a height
    // this function chose — otherwise a dialog dragged short on a big screen
    // would spring back every time it was reopened.
    const int targetWidth = qMin(width(), maximumClientWidth);
    int targetHeight = qMin(height(), maximumClientHeight);
    if (height() == m_autoFitHeight) {
        targetHeight = qBound(qMin(kSafeMinimumHeight, maximumClientHeight),
                              preferredClientHeight(targetWidth),
                              maximumClientHeight);
    }
    resize(targetWidth, targetHeight);
    m_autoFitHeight = height();
}

void ConnectionPanel::fitAndClampToScreen(QScreen* preferredScreen)
{
    QScreen* targetScreen = screenFitTarget(preferredScreen);
    if (!targetScreen) {
        return;
    }
    fitToScreen(targetScreen);
    // pos() on a window is its frame top-left, the same space
    // constrainedFrameTopLeft() works in, so this is a no-op when the window
    // already fits and a pull-back-inside when it does not.
    move(constrainedFrameTopLeft(pos(), targetScreen->availableGeometry()));
}

// A CanonWindow is always frameless, so this is whatever the platform reports
// once the window exists (nothing before), not a title-bar estimate.
QMargins ConnectionPanel::screenFitFrameMargins() const
{
    return windowHandle() ? windowHandle()->frameMargins() : QMargins();
}

QSize ConnectionPanel::screenFitFrameSize() const
{
    const QMargins frameMargins = screenFitFrameMargins();
    return QSize(width() + frameMargins.left() + frameMargins.right(),
                 height() + frameMargins.top() + frameMargins.bottom());
}

QPoint ConnectionPanel::constrainedFrameTopLeft(
    const QPoint& preferredFrameTopLeft,
    const QRect& availableGeometry) const
{
    const QSize frameSize = screenFitFrameSize();
    const int maxX =
        availableGeometry.left() + availableGeometry.width() - frameSize.width();
    const int maxY =
        availableGeometry.top() + availableGeometry.height() - frameSize.height();
    return QPoint(
        qMax(availableGeometry.left(), qMin(preferredFrameTopLeft.x(), maxX)),
        qMax(availableGeometry.top(), qMin(preferredFrameTopLeft.y(), maxY)));
}

void ConnectionPanel::setConnected(bool connected)
{
    if (connected) {
        m_startupProbe = false;
    }

    m_connected = connected;
    m_disconnectBtn->setVisible(connected);
    updateActionState();
    // Showing the Disconnect button is the one moment the fixed footer changes
    // height, so it is the one moment a fit made earlier stops being right —
    // and it is exactly when #4515 was reported. Cheap: fitToScreen() only
    // grows from a height it chose itself, and the clamp is a no-op for a
    // window already inside the work area.
    if (isVisible()) {
        fitAndClampToScreen();
    }
}

void ConnectionPanel::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

QList<RadioInfo> ConnectionPanel::automationLocalRadios() const
{
    return m_radios;
}

bool ConnectionPanel::selectRadio(const QString& serial)
{
    for (int index = 0; index < m_radios.size(); ++index) {
        if (m_radios[index].serial == serial) {
            setCurrentMode(LocalMode);
            m_radioList->setCurrentRow(index);
            return true;
        }
    }
    for (int index = 0; index < m_wanRadios.size(); ++index) {
        if (m_wanRadios[index].serial == serial) {
            setCurrentMode(SmartLinkMode);
            m_wanList->setCurrentRow(index);
            return true;
        }
    }
    return false;
}

void ConnectionPanel::selectManualConnection()
{
    setCurrentMode(ManualMode);
}

bool ConnectionPanel::canRenameRadio(const QString& serial) const
{
    for (const RadioInfo& radio : m_radios) {
        if (radio.serial == serial) {
            return canRenameRadio(radio);
        }
    }
    return false;
}

bool ConnectionPanel::canRenameRadio(const RadioInfo& radio) const
{
    return !radio.serial.isEmpty() && !anan::AnanDiscovery::nicknameLivesOnRadio(radio);
}

QString ConnectionPanel::radioDisplayName(const RadioInfo& radio, const QString& fallback) const
{
    return canRenameRadio(radio)
        ? anan::AnanDiscovery::effectiveNickname(radio.family, radio.serial, fallback)
        : fallback;
}

void ConnectionPanel::renameRadio(const QString& serial)
{
    for (const RadioInfo& radio : m_radios) {
        if (radio.serial == serial) {
            renameRadio(radio);
            return;
        }
    }
}

void ConnectionPanel::renameRadio(const RadioInfo& radio)
{
    if (!canRenameRadio(radio)) {
        return;
    }
    // The dialog itself is the owner's to build (MainWindow, as a
    // PersistentDialog).  Keeping it out of this class keeps ConnectionPanel
    // free of the frameless-dialog stack, which the startup/auto-connect
    // tests link without.
    emit radioRenameRequested(radio, anan::AnanDiscovery::effectiveNickname(
                                         radio.family, radio.serial, QString()));
}

void ConnectionPanel::setRadioNickname(const RadioInfo& radio, const QString& nickname)
{
    anan::AnanDiscovery::setNickname(radio.family, radio.serial, nickname.trimmed());
    for (int current = 0; current < m_radios.size(); ++current) {
        if (m_radios[current].serial == radio.serial) {
            m_radios[current].nickname = anan::AnanDiscovery::effectiveNickname(
                radio.family, radio.serial, radio.model);
            if (QListWidgetItem* item = m_radioList->item(current)) {
                item->setText(formatLocalRadioLabel(m_radios[current]));
            }
            break;
        }
    }
    emit radioNicknameChanged();
}

bool ConnectionPanel::automationConnectLocalSerial(const QString& serial, QString* error)
{
    const QString wanted = serial.trimmed();
    if (wanted.isEmpty()) {
        setAutomationError(error, QStringLiteral("local serial selector is empty"));
        return false;
    }
    if (m_connected) {
        setAutomationError(error, QStringLiteral("already connected to a radio"));
        return false;
    }

    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial.compare(wanted, Qt::CaseInsensitive) == 0) {
            setCurrentMode(LocalMode);
            m_radioList->setCurrentRow(i);
            onLocalConnectClicked();
            return true;
        }
    }

    setAutomationError(
        error,
        QStringLiteral("no discovered local radio has serial '%1'").arg(wanted));
    return false;
}

bool ConnectionPanel::automationConnectByIp(const QString& hostOrIp,
                                            const QString& family,
                                            QString* error)
{
    const QString target = hostOrIp.trimmed();
    if (target.isEmpty()) {
        setAutomationError(error, QStringLiteral("connect ip requires an address"));
        return false;
    }
    if (m_connected) {
        setAutomationError(error, QStringLiteral("already connected to a radio"));
        return false;
    }

    const QString wantedFamily = family.trimmed().toLower();
    if (!wantedFamily.isEmpty()
        && wantedFamily != QLatin1String(kFamilyFlex)
        && wantedFamily != QLatin1String(kFamilyAnan)) {
        setAutomationError(
            error,
            QStringLiteral("unknown radio family '%1' (use flex or anan)")
                .arg(family.trimmed()));
        return false;
    }

    setCurrentMode(ManualMode);
    m_manualIpCombo->setCurrentText(target);
    m_manualIpEdit->setText(target);
    // After the address, so the per-address profile restore inside
    // applySavedSourceSelection() cannot overwrite an explicit request.
    if (!wantedFamily.isEmpty())
        setManualFamily(wantedFamily);
    onManualConnectClicked();
    return true;
}

bool ConnectionPanel::automationDisconnect(QString* error)
{
    if (!m_connected) {
        setAutomationError(error, QStringLiteral("not connected to a radio"));
        return false;
    }

    emit disconnectRequested();
    return true;
}

void ConnectionPanel::saveLowBandwidthPreference(bool enabled)
{
    auto& settings = AppSettings::instance();
    const QString value = enabled ? QStringLiteral("True") : QStringLiteral("False");
    settings.setValue("LowBandwidthConnect", value);
    settings.setValue("LowBandwidthRemotePreferred", value);
    settings.save();
}

QString ConnectionPanel::formatLocalRadioLabel(const RadioInfo& radio) const
{
    QString title = radio.model.trimmed();
    if (!radio.nickname.trimmed().isEmpty())
        title += QStringLiteral("  %1").arg(radio.nickname.trimmed());
    if (!radio.callsign.trimmed().isEmpty())
        title += QStringLiteral("  %1").arg(radio.callsign.trimmed());
    if (title.trimmed().isEmpty())
        title = QStringLiteral("Radio");
    if (title == radio.model.trimmed() && !radio.address.isNull())
        title += QStringLiteral("  %1").arg(radio.address.toString());

    QString detail;
    if (!radio.guiClientStations.isEmpty()) {
        const QString station = radio.guiClientStations.first().trimmed();
        detail = station.isEmpty()
            ? QStringLiteral("Shared radio on your network via multiFLEX")
            : QStringLiteral("Shared radio on your network via multiFLEX at %1").arg(station);
    } else if (!radio.address.isNull()) {
        detail = QStringLiteral("Ready on your local network • %1").arg(radio.address.toString());
    } else {
        detail = QStringLiteral("Ready on your local network");
    }

    if (!radio.turfRegion.trimmed().isEmpty())
        detail += QStringLiteral(" • %1").arg(radio.turfRegion.trimmed());

    return title + QLatin1Char('\n') + detail;
}

QString ConnectionPanel::formatWanRadioLabel(const WanRadioInfo& radio) const
{
    QString title = radio.model.trimmed();
    if (!radio.nickname.trimmed().isEmpty())
        title += QStringLiteral("  %1").arg(radio.nickname.trimmed());
    if (!radio.callsign.trimmed().isEmpty())
        title += QStringLiteral("  %1").arg(radio.callsign.trimmed());
    if (title.trimmed().isEmpty())
        title = QStringLiteral("SmartLink radio");

    QString detail = QStringLiteral("Remote via SmartLink");
    const QString status = normalizedStatus(radio.status);
    if (!status.isEmpty() && status.compare(QStringLiteral("Available"), Qt::CaseInsensitive) != 0)
        detail += QStringLiteral(" • %1").arg(status);
    else
        detail += QStringLiteral(" • Ready to connect");

    if (!radio.guiClientStations.trimmed().isEmpty())
        detail += QStringLiteral(" • station %1").arg(radio.guiClientStations.trimmed());

    return title + QLatin1Char('\n') + detail;
}

void ConnectionPanel::setManualMessage(const QString& text, bool error)
{
    if (text.trimmed().isEmpty()) {
        // Cleared, not hidden: the label keeps its reserved height so the rows
        // above it do not shift (and overlap) when a message comes and goes.
        m_manualResultLabel->clear();
        return;
    }

    m_manualResultLabel->setText(text);
    applyStyle(m_manualResultLabel, QString::fromLatin1(error ? kErrorLabelStyle : kInfoLabelStyle));
    m_manualResultLabel->setVisible(true);
    // A long message wraps to more lines than the reserved height covers.
    refitToContent();
}

void ConnectionPanel::updateLocalPageState()
{
    const bool hasRadios = !m_radios.isEmpty();
    m_localStateStack->setCurrentIndex(hasRadios ? 0 : 1);

    if (hasRadios && !m_radioList->currentItem())
        m_radioList->setCurrentRow(0);

    updateActionState();
}

void ConnectionPanel::setAccountLine(const QString& text, AccountTone tone)
{
    m_slUserTone = tone;
    m_slUserLabel->setText(text);
    const char* style = tone == AccountTone::Info  ? kInfoLabelStyle
                      : tone == AccountTone::Error ? kErrorLabelStyle
                                                   : kHintLabelStyle;
    applyStyle(m_slUserLabel, QString::fromLatin1(style));
}

void ConnectionPanel::updateSmartLinkUi()
{
    const bool authed = m_smartLink && m_smartLink->isAuthenticated();
    const bool hasWanRadios = authed && !m_wanRadios.isEmpty();

    m_loginForm->setVisible(!authed);
    m_logoutBtn->setVisible(authed);
    m_wanDisconnectClientsBtn->setVisible(authed);
    m_wanList->setVisible(hasWanRadios);
    m_wanConnectBtn->setVisible(authed);

    if (authed) {
        if (m_slUserLabel->text().trimmed().isEmpty() || m_slUserTone == AccountTone::Hint)
            setAccountLine(smartLinkUserText(m_smartLink), AccountTone::Info);
        if (hasWanRadios) {
            m_smartLinkEmptyLabel->setVisible(false);
        } else {
            m_smartLinkEmptyLabel->setText(
                "No SmartLink radios are available right now. If the station is on your current "
                "LAN, it will appear on the local page instead.");
            m_smartLinkEmptyLabel->setVisible(true);
        }
    } else {
        if (m_slUserLabel->text().trimmed().isEmpty())
            setAccountLine(QStringLiteral("Sign in to see radios at remote stations."), AccountTone::Hint);
        m_smartLinkEmptyLabel->setText("Remote radios appear here after SmartLink sign-in.");
        m_smartLinkEmptyLabel->setVisible(true);
    }

    if (hasWanRadios && !m_wanList->currentItem())
        m_wanList->setCurrentRow(0);

    updateActionState();
}

void ConnectionPanel::updateActionState()
{
    m_localConnectBtn->setEnabled(!m_connected && m_radioList->currentItem() != nullptr);

    const bool smartLinkReady = !m_connected
        && m_smartLink
        && m_smartLink->isAuthenticated()
        && m_wanList->currentItem() != nullptr;
    m_wanConnectBtn->setEnabled(smartLinkReady);

    const int wanRow = m_wanList->currentRow();
    const bool remoteClientsAvailable = wanRow >= 0
        && wanRow < m_wanRadios.size()
        && !m_wanRadios[wanRow].guiClientHandles.trimmed().isEmpty();
    const bool smartLinkDisconnectReady = m_smartLink
        && m_smartLink->isAuthenticated()
        && m_smartLink->isConnected()
        && remoteClientsAvailable;
    m_wanDisconnectClientsBtn->setEnabled(smartLinkDisconnectReady);

    const bool manualReady = !m_connected && !m_manualIpEdit->text().trimmed().isEmpty();
    m_manualConnectBtn->setEnabled(manualReady);
}

void ConnectionPanel::updateLowBandwidthVisibility()
{
    const auto mode = static_cast<ConnectionMode>(m_modeStack->currentIndex());
    const bool slowLinksVisible = mode == SmartLinkMode || mode == ManualMode;
    m_linkOptionsWidget->setVisible(slowLinksVisible);

    if (!slowLinksVisible)
        return;

    if (mode == SmartLinkMode) {
        m_lowBwHintLabel->setText(
            "Recommended for SmartLink, hotel Wi-Fi, LTE, or other internet paths where "
            "waterfall and FFT traffic may feel heavy.");
    } else {
        m_lowBwHintLabel->setText(
            "Helpful on VPN or routed links when the radio is reachable by IP but the network "
            "feels slower than a normal local LAN.");
    }
}

void ConnectionPanel::updateManualAdvancedVisibility()
{
    const auto family = NetworkPathResolver::targetFamily(
        m_manualIpEdit ? m_manualIpEdit->text() : QString());
    const auto candidates = NetworkPathResolver::enumerateCandidates(family);
    // One IPv6 adapter normally has a GUA and a ULA; that is not a choice of
    // path, so IPv6 counts adapters. IPv4 keeps counting addresses.
    qsizetype paths = candidates.size();
    if (family == QAbstractSocket::IPv6Protocol) {
        QSet<QString> interfaces;
        for (const auto& candidate : candidates)
            interfaces.insert(candidate.interfaceId);
        paths = interfaces.size();
    }
    const RadioBindSettings settings = currentManualBindSettings();
    const bool hasExplicitSelection = settings.mode == RadioBindMode::Explicit;
    const bool showToggle = paths > 1
        || hasExplicitSelection
        || m_manualSourceWarningLabel->isVisible();

    m_manualAdvancedToggle->setVisible(showToggle);
    if (!showToggle) {
        if (m_manualAdvancedToggle->isChecked()) {
            const QSignalBlocker blocker(m_manualAdvancedToggle);
            m_manualAdvancedToggle->setChecked(false);
        }
        m_manualAdvancedToggle->setArrowType(Qt::RightArrow);
        m_manualAdvancedWidget->setVisible(false);
    } else if (hasExplicitSelection || m_manualSourceWarningLabel->isVisible()) {
        const QSignalBlocker blocker(m_manualAdvancedToggle);
        m_manualAdvancedToggle->setChecked(true);
        m_manualAdvancedToggle->setArrowType(Qt::DownArrow);
        m_manualAdvancedWidget->setVisible(true);
    } else {
        m_manualAdvancedWidget->setVisible(m_manualAdvancedToggle->isChecked());
        m_manualAdvancedToggle->setArrowType(
            m_manualAdvancedToggle->isChecked() ? Qt::DownArrow : Qt::RightArrow);
    }

    // Expanding the Advanced section is the biggest single height change this
    // page has, and it happens on its own whenever a saved source path goes
    // stale — the exact VPN case this feature exists for. The extra height goes
    // into the scroll range now, not into the top-level minimum; this only
    // re-activates the body layout so the new hint is published immediately
    // rather than on the next spontaneous relayout.
    refitToContent();
}

void ConnectionPanel::setCurrentMode(ConnectionMode mode)
{
    if (m_modeButtons->checkedId() != static_cast<int>(mode)) {
        const QSignalBlocker blocker(m_modeButtons);
        if (auto* button = m_modeButtons->button(static_cast<int>(mode)))
            button->setChecked(true);
    }

    m_modeStack->setCurrentIndex(static_cast<int>(mode));
    updateLowBandwidthVisibility();
    updateActionState();
}

void ConnectionPanel::onConnectionModeClicked(int id)
{
    setCurrentMode(static_cast<ConnectionMode>(id));
}

void ConnectionPanel::showRadioContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_radioList->itemAt(pos);
    if (!item)
        return;
    const int row = m_radioList->row(item);
    if (row < 0 || row >= m_radios.size())
        return;
    const RadioInfo radio = m_radios[row];

    // Flex stores its name on the radio itself (set from Radio Setup while
    // connected); offering a client-side override here would create two sources
    // of truth. So the client-side nickname is only for families without an
    // on-radio store (ANAN).
    if (anan::AnanDiscovery::nicknameLivesOnRadio(radio))
        return;

    QMenu menu(this);
    QAction* setNick = menu.addAction(tr("Set Nickname…"));
    const bool hasCustom =
        anan::AnanDiscovery::hasCustomNickname(radio.family, radio.serial);
    QAction* clearNick = hasCustom ? menu.addAction(tr("Clear Nickname")) : nullptr;

    QAction* chosen = menu.exec(m_radioList->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == setNick) {
        renameRadio(radio.serial);
        return;
    } else if (clearNick && chosen == clearNick) {
        anan::AnanDiscovery::setNickname(radio.family, radio.serial, QString());
    }

    // Reflect the change immediately: re-label this row from the saved setting
    // rather than waiting for the next discovery sweep.
    // Discovery may remove or reorder rows while either nested loop runs.
    // Resolve the radio again instead of retaining a QListWidgetItem pointer.
    for (int i = 0; i < m_radios.size(); ++i) {
        RadioInfo& updated = m_radios[i];
        if (updated.family != radio.family || updated.serial != radio.serial) {
            continue;
        }
        updated.nickname = anan::AnanDiscovery::effectiveNickname(
            updated.family, updated.serial, updated.model);
        if (QListWidgetItem* currentItem = m_radioList->item(i)) {
            currentItem->setText(formatLocalRadioLabel(updated));
        }
        break;
    }
    emit radioNicknameChanged();
}

void ConnectionPanel::onRadioDiscovered(const RadioInfo& radio)
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial == radio.serial) {
            m_radios[i] = radio;
            if (auto* item = m_radioList->item(i))
                item->setText(formatLocalRadioLabel(radio));
            updateLocalPageState();
            return;
        }
    }

    m_radios.append(radio);
    m_radioList->addItem(formatLocalRadioLabel(radio));
    if (m_radioList->count() == 1)
        m_radioList->setCurrentRow(0);
    updateLocalPageState();
}

void ConnectionPanel::onRadioUpdated(const RadioInfo& radio)
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial == radio.serial) {
            m_radios[i] = radio;
            if (auto* item = m_radioList->item(i))
                item->setText(formatLocalRadioLabel(radio));
            return;
        }
    }
}

void ConnectionPanel::onRadioLost(const QString& serial)
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial == serial) {
            delete m_radioList->takeItem(i);
            m_radios.removeAt(i);
            break;
        }
    }

    updateLocalPageState();
}

void ConnectionPanel::onListSelectionChanged()
{
    updateActionState();
}

void ConnectionPanel::onWanSelectionChanged()
{
    updateActionState();
}

void ConnectionPanel::onLocalConnectClicked()
{
    const int row = m_radioList->currentRow();
    if (m_connected || row < 0 || row >= m_radios.size())
        return;

    const RadioInfo& info = m_radios[row];
    // F5 (#4448): an in-use radio of a single-client family can't be shared,
    // and connecting would wedge both clients. Fail closed. The rule lives in
    // ConnectionSharingPolicy.h — one home, shared with the startup
    // auto-connect gate in MainWindow_Session.
    if (info.inUse && !AetherSDR::familySupportsSharedInUseConnect(info.family)) {
        setStatusText(QStringLiteral(
            "%1 is already in use by another client and can't be shared.")
            .arg(info.model));
        return;
    }

    // Low bandwidth mode is a slow-link concession, and for a Flex on the LAN it
    // is meaningless — so a local Flex connect clears it. Not for other
    // families, where the same preference may cap the panadapter span. (#4470)
    if (info.family.compare(QLatin1String("flex"), Qt::CaseInsensitive) == 0) {
        auto& settings = AppSettings::instance();
        settings.setValue("LowBandwidthConnect", "False");
        settings.save();
    }

    emit connectRequested(info);
}

void ConnectionPanel::onWanConnectClicked()
{
    const int row = m_wanList->currentRow();
    if (m_connected || row < 0 || row >= m_wanRadios.size())
        return;

    saveLowBandwidthPreference(m_lowBwCheck->isChecked());
    emit wanConnectRequested(m_wanRadios[row]);
}

void ConnectionPanel::onWanDisconnectClientsClicked()
{
    const int row = m_wanList->currentRow();
    if (row < 0 || row >= m_wanRadios.size())
        return;

    emit wanDisconnectClientsRequested(m_wanRadios[row]);
}

void ConnectionPanel::setSmartLinkClient(SmartLinkClient* client)
{
    m_smartLink = client;
    if (!client)
        return;

    connect(client, &SmartLinkClient::authenticated, this, [this] {
        m_passwordEdit->clear();
        m_loginBtn->setEnabled(true);
        m_loginBtn->setText("Sign In");
        setAccountLine(smartLinkUserText(m_smartLink), AccountTone::Info);
        updateSmartLinkUi();
    });

    connect(client, &SmartLinkClient::serverConnected, this, [this] {
        QTimer::singleShot(500, this, [this] {
            if (m_smartLink && m_smartLink->isAuthenticated()) {
                setAccountLine(smartLinkUserText(m_smartLink), AccountTone::Info);
                updateSmartLinkUi();
            }
        });
    });

    connect(client, &SmartLinkClient::serverDisconnected, this, [this] {
        if (!m_smartLink || !m_smartLink->isAuthenticated()) {
            if (m_slUserLabel->text().trimmed().isEmpty())
                setAccountLine(QStringLiteral("Sign in to see radios at remote stations."), AccountTone::Hint);
        }
        updateSmartLinkUi();
    });

    connect(client, &SmartLinkClient::authFailed, this, [this](const QString& err) {
        m_passwordEdit->clear();
        m_loginBtn->setText("Sign In");
        m_loginBtn->setEnabled(true);
        setAccountLine(QStringLiteral("SmartLink sign-in failed: ") + err, AccountTone::Error);
        updateSmartLinkUi();
    });

    connect(client, &SmartLinkClient::radioListReceived, this,
            [this](const QList<WanRadioInfo>& radios) {
        m_wanRadios.clear();
        m_wanList->clear();

        for (const auto& radio : radios) {
            bool isLanRadio = false;
            for (const auto& lan : m_radios) {
                if (lan.serial == radio.serial) {
                    isLanRadio = true;
                    break;
                }
            }
            if (isLanRadio)
                continue;

            m_wanRadios.append(radio);
            m_wanList->addItem(formatWanRadioLabel(radio));
        }

        if (m_wanList->count() > 0 && !m_wanList->currentItem())
            m_wanList->setCurrentRow(0);

        updateSmartLinkUi();
    });

    client->tryAutoLogin();
    updateSmartLinkUi();
}

void ConnectionPanel::refreshManualSourceOptions(const RadioBindSettings* selected)
{
    const QSignalBlocker blocker(m_manualSourceCombo);
    m_manualSourceCombo->clear();

    m_manualSourceCombo->addItem("Auto");
    m_manualSourceCombo->setItemData(0, static_cast<int>(RadioBindMode::Auto), kSourceModeRole);
    m_manualSourceCombo->setItemData(0, false, kSourceStaleRole);

    int selectedIndex = 0;
    const auto candidates = NetworkPathResolver::enumerateCandidates(
        NetworkPathResolver::targetFamily(m_manualIpEdit ? m_manualIpEdit->text() : QString()));
    for (const auto& candidate : candidates) {
        const int idx = m_manualSourceCombo->count();
        m_manualSourceCombo->addItem(candidate.label());
        m_manualSourceCombo->setItemData(idx, static_cast<int>(RadioBindMode::Explicit), kSourceModeRole);
        m_manualSourceCombo->setItemData(idx, candidate.interfaceId, kSourceInterfaceIdRole);
        m_manualSourceCombo->setItemData(idx, candidate.interfaceName, kSourceInterfaceNameRole);
        m_manualSourceCombo->setItemData(idx, candidate.address.toString(), kSourceAddressRole);
        m_manualSourceCombo->setItemData(idx, false, kSourceStaleRole);
    }
    if (selected && selected->mode == RadioBindMode::Explicit) {
        const qsizetype match = NetworkPathResolver::matchSelection(
            candidates, selected->interfaceId, selected->interfaceName, selected->bindAddress,
            QHostAddress(m_manualIpEdit ? m_manualIpEdit->text().trimmed() : QString()));
        if (match >= 0)
            selectedIndex = 1 + static_cast<int>(match);   // row 0 is Auto
    }

    if (selected
        && selected->mode == RadioBindMode::Explicit
        && selectedIndex == 0) {
        selectedIndex = m_manualSourceCombo->count();
        m_manualSourceCombo->addItem(staleSelectionText(*selected));
        m_manualSourceCombo->setItemData(
            selectedIndex, static_cast<int>(RadioBindMode::Explicit), kSourceModeRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->interfaceId, kSourceInterfaceIdRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->interfaceName, kSourceInterfaceNameRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->bindAddress.toString(), kSourceAddressRole);
        m_manualSourceCombo->setItemData(selectedIndex, true, kSourceStaleRole);
    }

    m_manualSourceCombo->setCurrentIndex(selectedIndex);
    updateManualAdvancedVisibility();
}

void ConnectionPanel::applySavedSourceSelection(const QString& ip, bool restoreFamily)
{
    const QString trimmedIp = ip.trimmed();
    m_manualProfileIp = trimmedIp;
    m_manualSourceWarningLabel->clear();
    m_manualSourceWarningLabel->setVisible(false);

    if (trimmedIp.isEmpty()) {
        refreshManualSourceOptions();
        updateManualAdvancedVisibility();
        return;
    }

    const QJsonObject profiles = loadRoutedProfiles();
    const QJsonObject profile = profiles.value(trimmedIp).toObject();
    if (profile.isEmpty()) {
        refreshManualSourceOptions();
        updateManualAdvancedVisibility();
        return;
    }

    // Restore the radio type this address was last reached with, so picking a
    // recent ANAN address out of the dropdown does not silently probe it as a
    // Flex (and time out on TCP/4992).
    //
    // ONLY when the address was CHOSEN, never while it is being typed — see the
    // parameter's note in the header. The bind settings below are restored on
    // both paths: they are not a control the operator is looking at, and an
    // address's VPN source path is a property of the address either way.
    if (restoreFamily)
        setManualFamily(familyFromProfile(profile));

    RadioBindSettings settings = bindSettingsFromProfile(profile);
    if (settings.mode == RadioBindMode::Explicit) {
        const auto resolved = NetworkPathResolver::resolveExplicitSelection(
            settings.interfaceId, settings.interfaceName, settings.bindAddress,
            NetworkPathResolver::targetFamily(trimmedIp), QHostAddress(trimmedIp));
        if (resolved.isValid()) {
            settings.interfaceId = resolved.interfaceId;
            settings.interfaceName = resolved.interfaceName;
            settings.bindAddress = resolved.address;
        } else {
            m_manualSourceWarningLabel->setText(
                QStringLiteral("Saved VPN source path for %1 is unavailable. Pick a live path "
                               "below before connecting.")
                    .arg(trimmedIp));
            m_manualSourceWarningLabel->setVisible(true);
        }
    }

    refreshManualSourceOptions(&settings);
    updateManualAdvancedVisibility();
    if (restoreFamily)
        m_manualSourcePick = settings;   // a chosen address brings its own path
}

RadioBindSettings ConnectionPanel::currentManualBindSettings(bool* staleSelection) const
{
    RadioBindSettings settings;
    const int index = m_manualSourceCombo->currentIndex();
    settings.mode = static_cast<RadioBindMode>(
        m_manualSourceCombo->itemData(index, kSourceModeRole).toInt());
    settings.interfaceId = m_manualSourceCombo->itemData(index, kSourceInterfaceIdRole).toString();
    settings.interfaceName = m_manualSourceCombo->itemData(index, kSourceInterfaceNameRole).toString();
    settings.bindAddress = QHostAddress(m_manualSourceCombo->itemData(index, kSourceAddressRole).toString());
    if (staleSelection)
        *staleSelection = m_manualSourceCombo->itemData(index, kSourceStaleRole).toBool();
    return settings;
}

QString ConnectionPanel::currentManualFamily() const
{
    const QString family = m_manualRadioTypeCombo
        ? m_manualRadioTypeCombo->currentData().toString()
        : QString();
    return family.isEmpty() ? QString::fromLatin1(kFamilyFlex) : family;
}

void ConnectionPanel::setManualFamily(const QString& family)
{
    if (!m_manualRadioTypeCombo)
        return;

    const QString lowered = family.trimmed().toLower();
    const QString wanted =
        lowered == QLatin1String(kFamilyAnan) ? QString::fromLatin1(kFamilyAnan)
                                              : QString::fromLatin1(kFamilyFlex);
    const int index = m_manualRadioTypeCombo->findData(wanted);
    if (index < 0 || index == m_manualRadioTypeCombo->currentIndex()) {
        updateManualFamilyHints();
        return;
    }

    m_manualRadioTypeCombo->setCurrentIndex(index);   // persists via currentIndexChanged
    // Render the hint here too, rather than relying on currentIndexChanged.
    //
    // The constructor calls this BEFORE that signal is connected, so
    // setCurrentIndex() fires into nothing and the hint paragraph — constructed
    // empty — stayed blank with the Flex placeholder still showing. Idempotent,
    // so the signal doing it again once connected is harmless. (PR #4528 review.)
    updateManualFamilyHints();
}

void ConnectionPanel::updateManualFamilyHints()
{
    const QString family = currentManualFamily();
    const bool anan = family == QLatin1String(kFamilyAnan);

    if (m_manualAnanHintRow)
        m_manualAnanHintRow->setVisible(anan);

    if (m_manualHintLabel) {
        m_manualHintLabel->setText(
            anan
                ? QStringLiteral(
                      "Use this path when discovery broadcasts cannot reach the radio — a VPN, a "
                      "routed subnet, or a switch that drops broadcasts. AetherSDR sends an "
                      "openHPSDR Protocol 2 discovery request straight to the address you enter.")
                : QStringLiteral(
                      "Use this path for VPN or other routed networks where discovery broadcasts "
                      "cannot reach the radio. Enter the radio IP address and AetherSDR will take "
                      "care of the probe."));
    }
    if (m_manualIpEdit) {
        m_manualIpEdit->setPlaceholderText(
            anan ? QStringLiteral("Example: 172.16.10.14")
                 : QStringLiteral("Example: 10.0.0.25"));
    }
}

void ConnectionPanel::loadRecentManualIps()
{
    const QStringList ips = loadRecentManualIpSettings();
    const QSignalBlocker comboBlocker(m_manualIpCombo);
    const QSignalBlocker editBlocker(m_manualIpEdit);

    m_manualIpCombo->clear();
    for (const auto& ip : ips)
        m_manualIpCombo->addItem(ip);

    if (!ips.isEmpty())
        m_manualIpCombo->setCurrentText(ips.first());
    else
        m_manualIpEdit->clear();
}

void ConnectionPanel::rememberManualIp(const QString& ip)
{
    const QString normalized = normalizeManualIp(ip);
    if (normalized.isEmpty())
        return;

    QStringList ips = loadRecentManualIpSettings();
    ips.removeAll(normalized);
    ips.prepend(normalized);
    const QStringList sanitized = sanitizeRecentManualIps(ips);
    saveRecentManualIpSettings(sanitized);

    const QSignalBlocker comboBlocker(m_manualIpCombo);
    const QSignalBlocker editBlocker(m_manualIpEdit);
    m_manualIpCombo->clear();
    for (const auto& recentIp : sanitized)
        m_manualIpCombo->addItem(recentIp);
    m_manualIpCombo->setCurrentText(normalized);
}

void ConnectionPanel::saveManualProfile(const QString& targetIp,
                                        const RadioBindSettings& settings,
                                        const QHostAddress& lastSuccessfulLocalIp)
{
    if (targetIp.trimmed().isEmpty())
        return;

    QJsonObject profiles = loadRoutedProfiles();
    QJsonObject profile;
    profile["schema_version"] = 1;

    QJsonObject identity;
    identity["target_address"] = targetIp;
    identity["family"] = currentManualFamily();
    profile["identity"] = identity;

    QJsonObject bind;
    bind["mode"] = settings.mode == RadioBindMode::Explicit ? "explicit" : "auto";
    bind["interface_id"] = settings.interfaceId;
    bind["interface_name"] = settings.interfaceName;
    bind["last_successful_ipv4"] = lastSuccessfulLocalIp.toString();
    profile["bind"] = bind;

    profiles[targetIp] = profile;
    saveRoutedProfiles(profiles);
}

void ConnectionPanel::onManualIpChanged(const QString& ip)
{
    const QString trimmed = ip.trimmed();
    m_manualConnectPending = false;
    if (trimmed != m_manualProfileIp) {
        // Typing an address must not discard a source path the operator has
        // already selected. The saved profile is useful for startup and for
        // choosing a recent address, but it must not silently turn an
        // explicit interface back into Auto during an interactive edit.
        const RadioBindSettings selected = m_manualSourcePick.mode == RadioBindMode::Explicit
            ? m_manualSourcePick : currentManualBindSettings();
        applySavedSourceSelection(trimmed, /*restoreFamily=*/false);
        if (selected.mode == RadioBindMode::Explicit)
            refreshManualSourceOptions(&selected);
    }
    setManualMessage(QString());
    updateActionState();
}

void ConnectionPanel::onManualConnectClicked()
{
    const QString ip = m_manualIpEdit->text().trimmed();
    if (m_connected || ip.isEmpty())
        return;

    m_manualConnectPending = true;
    m_startupProbe = false;
    setManualMessage(QStringLiteral("Checking %1…").arg(ip));
    probeRadio(ip);
}

void ConnectionPanel::onManualAdvancedToggled(bool checked)
{
    m_manualAdvancedToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    m_manualAdvancedWidget->setVisible(checked);
}

void ConnectionPanel::reportStartupProbeFailure(const QString& reason)
{
    // A startup probe is the one probe with nobody reading the manual page.
    // MainWindow has covered the window with "Looking for your radio…", and it
    // suppressed the no-saved-radio dialog popup precisely BECAUSE a radio is
    // saved — so setManualMessage() alone writes the reason onto a page behind a
    // dialog that will never open. The operator is left with a spinner and no
    // route back to the connection UI. Hand the reason up instead.
    if (!m_startupProbe) {
        return;
    }
    m_startupProbe = false;
    // Land the operator on the page that explains the failure: the full
    // guidance setManualMessage() wrote lives on the Connect by IP page, and
    // the footer line MainWindow sets carries only the short reason.
    setCurrentMode(ManualMode);
    emit startupConnectUnavailable(reason);
}

void ConnectionPanel::probeRadio(const QString& ip, bool restoreSavedFamily)
{
    const QString trimmedIp = ip.trimmed();
    if (trimmedIp.isEmpty())
        return;

    // Latched, not assigned. Cleared on failure, probe dispatch, a proven connection, or
    // an operator route edit/manual connect.
    if (restoreSavedFamily) {
        m_startupProbe = true;
    }

    // Interactive and automation probes keep the family currently selected by
    // the operator. Startup is the exception: it has no current operator
    // choice, so restoreSavedFamily asks the retained route which wire protocol
    // belongs to the saved address.
    if (m_manualIpEdit->text().trimmed() != trimmedIp) {
        m_manualIpEdit->setText(trimmedIp);
        if (restoreSavedFamily)
            applySavedSourceSelection(trimmedIp, /*restoreFamily=*/true);
    } else if (restoreSavedFamily) {
        // Interactive connects keep the currently selected source path. Only
        // startup and an explicit recent-address selection restore profiles.
        applySavedSourceSelection(trimmedIp, /*restoreFamily=*/true);
    }

    bool staleSelection = false;
    const RadioBindSettings bindSettings = currentManualBindSettings(&staleSelection);
    if (bindSettings.mode == RadioBindMode::Explicit && staleSelection) {
        m_manualSourceWarningLabel->setText(
            QStringLiteral("The selected VPN source path is unavailable. Choose a live path "
                           "before connecting."));
        m_manualSourceWarningLabel->setVisible(true);
        updateManualAdvancedVisibility();
        setManualMessage("Choose a live source path before trying again.", true);
        m_manualConnectPending = false;
        reportStartupProbeFailure(
            QStringLiteral("The saved source path for this radio is unavailable."));
        return;
    }

    const QString busyText = m_manualConnectPending
        ? QStringLiteral("Connecting...")
        : QStringLiteral("Checking...");
    m_manualConnectBtn->setEnabled(false);
    m_manualConnectBtn->setText(busyText);
    m_manualSourceWarningLabel->setVisible(false);
    updateManualAdvancedVisibility();

    // Probe exactly the family the operator selected: the two wire protocols
    // are disjoint, so there is nothing to gain from trying both.
    if (currentManualFamily() == QLatin1String(kFamilyAnan)) {
        const AnanProbeResult probe = probeAnan(trimmedIp, bindSettings);
        if (probe == AnanProbeResult::NoAnswer) {
            resetManualConnectButton();
            setManualMessage(
                QStringLiteral("No ANAN answered at %1. Check the address, and that the "
                               "radio is powered, idle, and reachable on UDP port 1024.")
                    .arg(trimmedIp),
                true);
            reportStartupProbeFailure(
                QStringLiteral("No ANAN answered at %1.").arg(trimmedIp));
        } else if (probe == AnanProbeResult::NotAttempted) {
            resetManualConnectButton();
        }
        return;
    }

    RadioBindSettings flexBind = bindSettings;
    if (!resolveNamedSourceFamily(trimmedIp, &flexBind))
        return;
    probeFlexRadio(trimmedIp, flexBind);
}

bool ConnectionPanel::resolveNamedSourceFamily(const QString& host, RadioBindSettings* bind)
{
    // The source list follows the typed text, so a host name lists IPv4
    // paths. A name with only IPv6 addresses reaches the radio over IPv6:
    // use the chosen interface's IPv6 address instead of failing on family.
    if (bind->mode != RadioBindMode::Explicit || !QHostAddress(host).isNull())
        return true;
    const QHostInfo info = QHostInfo::fromName(host);
    QHostAddress v6;
    for (const QHostAddress& address : info.addresses()) {
        bool isV4 = false;
        address.toIPv4Address(&isV4);
        if (isV4)
            return true;   // reachable over IPv4: the listed path applies
        if (v6.isNull() && address.protocol() == QAbstractSocket::IPv6Protocol)
            v6 = address;
    }
    if (v6.isNull())
        return true;   // unresolved: the probe reports it
    const NetworkPathCandidate path = NetworkPathResolver::resolveExplicitSelection(
        bind->interfaceId, bind->interfaceName, bind->bindAddress,
        QAbstractSocket::IPv6Protocol, v6);
    if (!path.isValid()) {
        resetManualConnectButton();
        setManualMessage(
            QStringLiteral("“%1” has only IPv6 addresses, and the selected source path has "
                           "no IPv6 address. Choose Auto or another path.").arg(host),
            true);
        reportStartupProbeFailure(
            QStringLiteral("The source path for “%1” has no IPv6 address.").arg(host));
        return false;
    }
    bind->interfaceId = path.interfaceId;
    bind->interfaceName = path.interfaceName;
    bind->bindAddress = path.address;
    return true;
}

void ConnectionPanel::finishManualProbe(const RadioInfo& info, bool routedOnly)
{
    // Discovery has handed off to the session layer. A later session failure
    // or interactive probe must not inherit this startup probe's ownership.
    m_startupProbe = false;
    if (routedOnly) {
        emit routedRadioFound(info);
    } else {
        emit connectRequested(info);
    }
}

void ConnectionPanel::refitToContent()
{
    if (!m_rootLayout)
        return;

    // The body owns overflow now. Refresh its geometry so expanding Advanced or
    // wrapping a result message extends the scroll range instead of increasing
    // the top-level minimum past the screen's available height.
    m_rootLayout->invalidate();
    m_rootLayout->activate();
}

void ConnectionPanel::resetManualConnectButton()
{
    m_manualConnectPending = false;
    m_manualConnectBtn->setText(QStringLiteral("Connect by IP"));
    m_manualConnectBtn->setEnabled(true);
    updateActionState();
}

// Directed (unicast) openHPSDR Protocol 2 discovery against one host: a
// routed/VPN path never sees a broadcast sweep (P2Protocol::buildDiscovery /
// parseDiscoveryReply, kRadioPort, isSupportedAnan()).
ConnectionPanel::AnanProbeResult ConnectionPanel::probeAnan(
    const QString& ip, const RadioBindSettings& bindSettings)
{
    QUdpSocket hpsdr;
    const bool explicitBind = bindSettings.mode == RadioBindMode::Explicit
                           && !bindSettings.bindAddress.isNull();
    const bool bound = explicitBind
        ? hpsdr.bind(bindSettings.bindAddress, 0)
        : hpsdr.bind(QHostAddress(QHostAddress::AnyIPv4), 0);
    if (!bound) {
        if (explicitBind) {
            m_manualSourceWarningLabel->setText(
                QStringLiteral("Failed to bind %1: %2")
                    .arg(bindSettings.bindAddress.toString(), hpsdr.errorString()));
            m_manualSourceWarningLabel->setVisible(true);
            updateManualAdvancedVisibility();
            setManualMessage(
                QStringLiteral("AetherSDR could not use that VPN source path. "
                               "Try Auto or choose another path."),
                true);
        } else {
            setManualMessage(
                QStringLiteral("Could not open a UDP socket to probe for an "
                               "ANAN: %1").arg(hpsdr.errorString()),
                true);
        }
        reportStartupProbeFailure(explicitBind
            ? QStringLiteral("The saved source path for this radio is unavailable.")
            : QStringLiteral("Could not open a UDP socket to reach the ANAN."));
        return AnanProbeResult::NotAttempted;
    }

    // Resolve a name before probing: QHostAddress(ip) is null for non-literals
    // and writeDatagram() to a null address sends nothing. fromName() is
    // synchronous, fine on a path that already blocks. IPv4 only: Protocol 2
    // discovery is IPv4-only, so an AAAA-only name is reported as such.
    QHostAddress dest(ip);
    if (dest.isNull()) {
        const QHostInfo resolved = QHostInfo::fromName(ip);
        for (const QHostAddress& a : resolved.addresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                dest = a;
                break;
            }
        }
        if (dest.isNull()) {
            setManualMessage(
                resolved.error() != QHostInfo::NoError
                    ? QStringLiteral("Could not resolve “%1”: %2")
                          .arg(ip, resolved.errorString())
                    : QStringLiteral("“%1” has no IPv4 address, and an ANAN "
                                     "is reachable over IPv4 only.").arg(ip),
                true);
            reportStartupProbeFailure(
                QStringLiteral("Could not resolve an IPv4 address for the ANAN at %1.").arg(ip));
            return AnanProbeResult::NotAttempted;
        }
    }

    const auto request = anan::buildDiscovery();
    if (hpsdr.writeDatagram(reinterpret_cast<const char*>(request.data()),
                            qint64(request.size()),
                            dest,
                            anan::kRadioPort) < 0) {
        setManualMessage(
            QStringLiteral("Could not send a discovery request to %1: %2")
                .arg(dest.toString(), hpsdr.errorString()),
            true);
        reportStartupProbeFailure(
            QStringLiteral("Could not send a discovery request to %1.").arg(dest.toString()));
        return AnanProbeResult::NotAttempted;
    }

    QDeadlineTimer deadline(600);
    while (!deadline.hasExpired()) {
        if (!hpsdr.waitForReadyRead(static_cast<int>(deadline.remainingTime())))
            break;
        while (hpsdr.hasPendingDatagrams()) {
            const QByteArray d = hpsdr.receiveDatagram().data();
            const auto reply = anan::parseDiscoveryReply(
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(d.constData()), std::size_t(d.size())));
            // ANAN-100D port: ANGELIA (100D), ORION (200D) and SATURN (G2) --
            // the same predicate AnanDiscovery applies to broadcast replies.
            if (!reply || !reply->isSupportedAnan())
                continue;

            RadioInfo info;
            info.family   = QString::fromLatin1(kFamilyAnan);
            info.address  = dest;
            info.port     = anan::kRadioPort;
            info.model    = QString::fromLatin1(
                anan::boardProfileFor(reply->boardId)
                    .value_or(anan::defaultBoardProfile()).modelName);
            info.name     = info.model;
            info.serial   = anan::AnanDiscovery::stableSerial(reply->mac, reply->boardId);
            info.nickname = anan::AnanDiscovery::effectiveNickname(info.family, info.serial,
                                                                    info.model);
            info.version  = QString::number(reply->firmwareVer);
            // A bare integer is a gateware bitstream number, not a software
            // version (discrepancy #1, see P2Protocol.h's DiscoveryReply) --
            // same label the broadcast path sets.
            info.versionLabel = QStringLiteral("Gateware");
            info.inUse    = reply->streaming;
            info.status   = reply->streaming ? QStringLiteral("In_Use")
                                             : QStringLiteral("Available");
            info.isRouted           = true;
            info.bindSettings       = bindSettings;
            info.sessionBindAddress = bindSettings.mode == RadioBindMode::Explicit
                ? bindSettings.bindAddress
                : QHostAddress();

            resetManualConnectButton();

            if (reply->streaming) {
                // openHPSDR Protocol 2 is single-client, same as Protocol 1.
                // Fail closed rather than wedging both clients.
                setManualMessage(
                    QStringLiteral("The ANAN at %1 is already in use by another client "
                                   "and can't be shared.").arg(ip),
                    true);
                reportStartupProbeFailure(
                    QStringLiteral("The ANAN at %1 is in use by another client.").arg(ip));
                return AnanProbeResult::Answered;
            }

            saveManualProfile(ip, bindSettings, info.sessionBindAddress);
            rememberManualIp(ip);
            setManualMessage(
                QStringLiteral("Found an ANAN at %1 — connecting.").arg(ip), false);
            finishManualProbe(info);
            return AnanProbeResult::Answered;
        }
    }

    return AnanProbeResult::NoAnswer;
}

void ConnectionPanel::probeFlexRadio(const QString& trimmedIp, const RadioBindSettings& bindSettings)
{
    auto* sock = new QTcpSocket(this);
    if (bindSettings.mode == RadioBindMode::Explicit
        && !sock->bind(bindSettings.bindAddress, 0)) {
        m_manualSourceWarningLabel->setText(
            QStringLiteral("Failed to bind %1: %2")
                .arg(bindSettings.bindAddress.toString(), sock->errorString()));
        m_manualSourceWarningLabel->setVisible(true);
        updateManualAdvancedVisibility();
        setManualMessage("AetherSDR could not use that VPN source path. Try Auto or choose another path.", true);
        sock->deleteLater();
        m_manualConnectPending = false;
        m_manualConnectBtn->setText("Connect by IP");
        updateActionState();
        reportStartupProbeFailure(
            QStringLiteral("The saved source path for this radio is unavailable."));
        return;
    }

    sock->connectToHost(trimmedIp, 4992);

    QTimer::singleShot(3000, sock, [this, sock, trimmedIp] {
        if (sock->state() != QAbstractSocket::ConnectedState) {
            sock->abort();
            sock->deleteLater();
            m_manualConnectPending = false;
            m_manualConnectBtn->setText("Connect by IP");
            updateActionState();
            setManualMessage(
                QStringLiteral("No radio responded at %1. If this is a VPN path, confirm the IP "
                               "address and try Advanced only if your VPN exposes multiple adapters.")
                    .arg(trimmedIp),
                true);
            reportStartupProbeFailure(
                QStringLiteral("No radio responded at %1.").arg(trimmedIp));
        }
    });

    connect(sock, &QTcpSocket::connected, this, [this, sock, trimmedIp, bindSettings] {
        // Shared state across readyRead calls and the peek timer.
        auto buffer      = std::make_shared<QByteArray>();
        auto version     = std::make_shared<QString>();
        auto statusLines = std::make_shared<QStringList>();
        auto localSrc    = std::make_shared<QHostAddress>();
        auto seenHandle  = std::make_shared<bool>(false);

        // Fired 400 ms after H is received; by then the radio has sent
        // its full radio + client status burst in response to our subs.
        auto* peekTimer = new QTimer(sock);
        peekTimer->setSingleShot(true);
        peekTimer->setInterval(400);

        // Build RadioInfo from collected status and emit the connect signal.
        auto finishProbe = [this, sock, trimmedIp, bindSettings, version, statusLines, localSrc] {
            QHostAddress targetAddress(trimmedIp);
            if (targetAddress.isNull()) {
                targetAddress = sock->peerAddress();
            }

            sock->disconnectFromHost();
            sock->deleteLater();

            RadioInfo info;
            info.address  = targetAddress;
            info.port     = 4992;
            info.version  = *version;
            info.status   = QStringLiteral("Available");
            info.model    = QStringLiteral("FLEX");
            info.name     = QStringLiteral("FLEX");
            info.serial   = trimmedIp;
            info.isRouted = true;
            info.bindSettings       = bindSettings;
            info.sessionBindAddress = *localSrc;

            // Parse S-type status lines collected during the peek window.
            for (const QString& line : *statusLines) {
                // S<handle>|<object> [key=val ...]
                const int pipeIdx = line.indexOf('|');
                if (pipeIdx < 0)
                    continue;
                const QString body = line.mid(pipeIdx + 1);

                if (body.startsWith(QStringLiteral("radio "))) {
                    const QStringList parts = body.mid(6).split(' ', Qt::SkipEmptyParts);
                    for (const QString& part : parts) {
                        const int eq = part.indexOf('=');
                        if (eq < 0)
                            continue;
                        const QString key = part.left(eq);
                        const QString val = part.mid(eq + 1);
                        if (key == QStringLiteral("mf_enable")) {
                            info.multiFlexEnabled = (val != QStringLiteral("0"));
                        } else if (key == QStringLiteral("model") && !val.isEmpty()) {
                            info.model = val;
                            info.name  = val;
                        } else if (key == QStringLiteral("nickname") && !val.isEmpty()) {
                            info.nickname = val;
                        } else if (key == QStringLiteral("callsign") && !val.isEmpty()) {
                            info.callsign = val;
                        }
                    }
                } else if (body.startsWith(QStringLiteral("client 0x"))
                           && body.contains(QStringLiteral("connected"))) {
                    // "client 0x<handle> connected program=... station=..."
                    const QStringList tokens = body.split(' ', Qt::SkipEmptyParts);
                    if (tokens.size() >= 3 && tokens[2] == QStringLiteral("connected")) {
                        bool ok = false;
                        const quint32 handle = tokens[1].toUInt(&ok, 16);
                        if (!ok || handle == 0)
                            continue;
                        QString program;
                        QString station;
                        for (int i = 3; i < tokens.size(); ++i) {
                            const int eq = tokens[i].indexOf('=');
                            if (eq < 0)
                                continue;
                            const QString key = tokens[i].left(eq);
                            const QString val = tokens[i].mid(eq + 1);
                            if (key == QStringLiteral("program"))
                                program = val;
                            else if (key == QStringLiteral("station"))
                                station = val;
                        }
                        info.guiClientHandles.append(QString::number(handle, 16).toUpper());
                        info.guiClientPrograms.append(program);
                        info.guiClientStations.append(station);
                    }
                }
            }

            saveManualProfile(trimmedIp, bindSettings, *localSrc);
            rememberManualIp(trimmedIp);

            m_manualConnectBtn->setText(QStringLiteral("Connect by IP"));
            updateActionState();

            if (m_manualConnectPending) {
                saveLowBandwidthPreference(m_lowBwCheck->isChecked());
                setManualMessage(
                    QStringLiteral("Found a radio at %1. Connecting now…").arg(trimmedIp));
                m_manualConnectPending = false;
                finishManualProbe(info);
            } else {
                setManualMessage(
                    QStringLiteral("Found a radio at %1 and saved the path for later.")
                        .arg(trimmedIp));
                finishManualProbe(info, /*routedOnly=*/true);
            }
        };

        connect(peekTimer, &QTimer::timeout, this, [finishProbe] { finishProbe(); });

        connect(sock, &QTcpSocket::readyRead, this,
                [sock, buffer, version, statusLines, localSrc, seenHandle, peekTimer] {
            buffer->append(sock->readAll());

            while (buffer->contains('\n')) {
                const int idx = buffer->indexOf('\n');
                const QString line = QString::fromUtf8(buffer->left(idx)).trimmed();
                buffer->remove(0, idx + 1);

                if (line.startsWith('V')) {
                    *version = line.mid(1);
                } else if (line.startsWith('H') && !*seenHandle) {
                    *seenHandle = true;
                    *localSrc   = sock->localAddress();
                    // Ask the radio for its current state before we commit
                    // to a real connection. This lets us populate mf_enable
                    // and connected client info even when there's no discovery
                    // broadcast (direct IP / VPN / routed path).
                    sock->write("C1|sub radio all\n");
                    sock->write("C2|sub client all\n");
                    sock->flush();
                    peekTimer->start();
                } else if (line.startsWith('S') && *seenHandle) {
                    statusLines->append(line);
                }
            }
        });
    });

    connect(sock, &QTcpSocket::errorOccurred, this,
            [this, sock, trimmedIp](QAbstractSocket::SocketError) {
        const QString reason =
            QStringLiteral("Could not reach %1: %2").arg(trimmedIp, sock->errorString());
        setManualMessage(reason, true);
        sock->deleteLater();
        m_manualConnectPending = false;
        m_manualConnectBtn->setText("Connect by IP");
        updateActionState();
        reportStartupProbeFailure(reason);
    });
}

} // namespace AetherSDR
