#include "ThemeEditorDialog.h"
#include "core/ForkDataDir.h"
#include "ScopedChildWidget.h"
#include "Theme.h"
#include "ThemeInspector.h"
#include "TokenEditorWidget.h"
#include "core/AppSettings.h"
#include "core/ThemeManager.h"

#include <QAction>
#include <QMouseEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QSlider>
#include <QHash>
#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QCheckBox>
#include <QColorDialog>
#include <QToolButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QBrush>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QAbstractSlider>
#include <QAbstractButton>
#include <QCursor>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QLineEdit>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QDir>
#include <QVBoxLayout>
#include <QWidget>

namespace AetherSDR {

namespace {

// Where user themes actually live, resolved rather than asserted.  These
// strings used to say "~/.config/AetherSDR/themes/" everywhere, which is only
// true on Linux — storage is GenericConfigLocation, so it's %LOCALAPPDATA% on
// Windows and ~/Library/Preferences on macOS.  A dialog that tells an operator
// to go check a directory their OS doesn't have is worse than one that says
// nothing.
QString userThemesDir()
{
    return QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + AetherSDR::forkDataDir() + QStringLiteral("/themes"));
}

QMessageBox::StandardButton showModalMessage(
    QMessageBox::Icon icon, QWidget* parent, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    ScopedChildWidget<QMessageBox> boxOwner(parent);
    QMessageBox& box = *boxOwner.get();
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton) {
        box.setDefaultButton(defaultButton);
    }
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

// Build a tiny coloured square QIcon for use as a swatch on a list row.
// Re-rendered whenever a token's value changes so the swatch tracks the
// live value.  A 2x2 checkerboard sits under the colour fill so the
// operator can see translucency at a glance — opaque colours render
// identically to a solid swatch (the checkerboard is fully covered).
QIcon swatchIcon(const QColor& c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0.5, 0.5, 15.0, 15.0), 3, 3);
    p.save();
    p.setClipPath(clip);
    p.fillRect(QRect(0, 0, 16, 16), QColor(0xc8, 0xc8, 0xc8));
    p.fillRect(QRect(0, 0, 8, 8),   QColor(0x80, 0x80, 0x80));
    p.fillRect(QRect(8, 8, 8, 8),   QColor(0x80, 0x80, 0x80));
    p.restore();

    p.setBrush(c);
    p.setPen(QPen(QColor(0, 0, 0, 80), 1));
    p.drawRoundedRect(QRectF(0.5, 0.5, 15.0, 15.0), 3, 3);
    return QIcon(pm);
}

// Token hex serialisation matching ThemeManager's storage convention:
// "#rrggbb" for opaque, "#aarrggbb" for translucent.
QString colorToTokenHex(const QColor& c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb)
                            : c.name(QColor::HexArgb);
}

// A scope column that inherits from an ancestor shows this placeholder.
QString inheritedCellText()
{
    return QStringLiteral("inherited");
}

// Marks a scope cell as inherited, so an override whose value happens to be
// the placeholder text is never mistaken for one.
constexpr int kInheritedCellRole = Qt::UserRole + 1;

// The placeholder follows the theme, so it reads on light and dark themes.
QBrush inheritedCellBrush()
{
    return QBrush(ThemeManager::instance().color(QStringLiteral("color.text.secondary")));
}

// Mini left-to-right preview of a gradient for the token list row.
// Stops are sampled in declaration order without sorting — matches the
// strip in GradientEditorDialog.
QIcon gradientSwatchIcon(const ThemeGradient& g)
{
    QPixmap pm(32, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0.5, 0.5, 31.0, 15.0), 3, 3);
    p.save();
    p.setClipPath(clip);
    p.fillRect(QRect(0, 0, 32, 16), QColor(0xc8, 0xc8, 0xc8));
    for (int x = 0; x < 32; x += 8)
        for (int y = 0; y < 16; y += 8)
            if (((x / 8 + y / 8) & 1) == 0)
                p.fillRect(QRect(x, y, 8, 8), QColor(0x80, 0x80, 0x80));

    QLinearGradient lg(QPointF(0.5, 0), QPointF(31.5, 0));
    for (const auto& s : g.stops) lg.setColorAt(std::clamp(s.at, 0.0, 1.0), s.color);
    p.fillRect(QRect(0, 0, 32, 16), QBrush(lg));
    p.restore();

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 80), 1));
    p.drawRoundedRect(QRectF(0.5, 0.5, 31.0, 15.0), 3, 3);
    return QIcon(pm);
}

// Build the short text shown on a gradient row: "N stops, Xdeg" so the
// list stays scannable without having to open every gradient.
} // namespace

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent)
    : PersistentDialog(QStringLiteral("Theme Editor"),
                       QStringLiteral("ThemeEditorDialogGeometry"),
                       parent)
{
    // PersistentDialog owns the outer (FramelessWindowTitleBar + bodyWidget)
    // layout and the geometry persistence; we just need to populate
    // bodyWidget() with the actual editor controls.  Subclasses are
    // expected to be non-modal — MainWindow's showOrRaisePersistent
    // wires that up + the frameless chrome from AppSettings.
    // Size budget — fits CompactColorPicker (~218×220) on the color
    // page; the gradient page is taller (strip + stop list + picker
    // + angle row).
    setMinimumSize(440, 720);
    applyAppTheme(this);

    // Container declaration — places this dialog under the "dialog"
    // umbrella so step 3's editor UI can navigate from root → dialog →
    // dialog.themeEditor when rendering its own scope tree.
    theme::setContainer(this, QStringLiteral("dialog/themeEditor"));

    auto* root = new QVBoxLayout(bodyWidget());
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_themeLabel = new QLabel(bodyWidget());
    m_themeLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-weight: bold; }"));
    root->addWidget(m_themeLabel);

    // Inline editor — swaps controls based on the currently selected
    // token's namespace + value shape.  Sits above the filter + token
    // list so the operator can see what they're editing without their
    // gaze jumping to a separate window.  Every control writes live to
    // ThemeManager, so the rest of the app re-themes as they edit.
    m_tokenEditor = new TokenEditorWidget(bodyWidget());
    root->addWidget(m_tokenEditor);

    auto* divider = new QFrame(bodyWidget());
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    root->addWidget(divider);

    // Inspector + scope picker row.  Layout left-to-right:
    //   [Scope: combo] [Inspect btn] [Editing label] [status] [Reset btn]
    // Scope picker sits leftmost so it visually anchors the row as
    // "where this edit will land".  Combo width is clamped to the
    // longest container-path label + 4 px pad on each side; no
    // stretch so it doesn't grow with the dialog.
    auto* inspectRow = new QHBoxLayout;
    inspectRow->addWidget(new QLabel(QStringLiteral("Scope:"), bodyWidget()));
    m_containerCombo = new QComboBox(bodyWidget());
    m_containerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_containerCombo->setToolTip(QStringLiteral(
        "Container scope to edit.  \"(root)\" is the global namespace.\n"
        "Selecting a nested scope (e.g. applet.tx) routes new overrides\n"
        "into that scope, and only widgets inside that container see them."));
    inspectRow->addWidget(m_containerCombo);

    m_inspectBtn = new QPushButton(QStringLiteral("🎯  Inspect"), bodyWidget());
    m_inspectBtn->setCheckable(true);
    m_inspectBtn->setToolTip(QStringLiteral(
        "Click, then point at any region of the main UI to find the\n"
        "token(s) painting it.  ESC cancels."));
    inspectRow->addWidget(m_inspectBtn);
    // "Editing: <token>" sits left-aligned right after the Inspect
    // button.  Inspector status text takes the leftover stretch so
    // transient messages ("Inspector canceled.") show between the
    // header and the Reset button without nudging either side.
    // Inspector status sits on the inspect row in transient-message
    // territory; Editing label moves down to the filter row.
    m_inspectStatus = new QLabel(bodyWidget());
    m_inspectStatus->setWordWrap(true);
    m_inspectStatus->setStyleSheet(QStringLiteral(
        "QLabel { font-style: italic; }"));
    inspectRow->addWidget(m_inspectStatus, 1);
    // Reset button reparents out of the editor's bottom row and sits
    // right-aligned on the inspect row — keeps the editor's Cancel/OK
    // pair clean, and Reset stays visible without scrolling.
    if (auto* reset = m_tokenEditor->resetButton()) {
        inspectRow->addWidget(reset, 0, Qt::AlignRight);
    }
    root->addLayout(inspectRow);

    // Filter input + "Editing: <token>" header share one row: filter
    // is clamped to ~50% of the body width, header takes the rest so
    // its single-line layout always has the room it needs to render.
    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);
    m_filterEdit = new QLineEdit(bodyWidget());
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter tokens (e.g. accent, slice, meter)…"));
    m_filterEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    filterRow->addWidget(m_filterEdit, 1);
    if (auto* hdr = m_tokenEditor->headerLabel()) {
        filterRow->addWidget(hdr, 1, Qt::AlignLeft | Qt::AlignVCenter);
    }
    // Build 796: the token list and the palette are two tabs.
    m_tabs = new QTabWidget(bodyWidget());
    auto* tokensPage = new QWidget(m_tabs);
    auto* tokensLay = new QVBoxLayout(tokensPage);
    tokensLay->setContentsMargins(0, 6, 0, 0);
    tokensLay->setSpacing(6);
    tokensLay->addLayout(filterRow);
    // Build 798: only the tokens that differ from another theme.
    auto* compareRow = new QHBoxLayout;
    compareRow->setSpacing(6);
    m_changedOnly = new QCheckBox(QStringLiteral("Only changed vs"), tokensPage);
    m_changedOnly->setToolTip(QStringLiteral("Shows only the tokens whose value differs from the chosen theme\n"
                                             "(e.g. a copy against the theme it was made from)."));
    m_compareCombo = new QComboBox(tokensPage);
    m_compareCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_changedCount = new QLabel(tokensPage);
    compareRow->addWidget(m_changedOnly);
    compareRow->addWidget(m_compareCombo);
    compareRow->addWidget(m_changedCount, 1);
    tokensLay->addLayout(compareRow);

    m_tokenList = new QTreeWidget(bodyWidget());
    m_tokenList->setIconSize(QSize(18, 18));
    m_tokenList->setAlternatingRowColors(true);
    m_tokenList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tokenList->setRootIsDecorated(false);   // flat-table look
    m_tokenList->setIndentation(0);
    m_tokenList->setUniformRowHeights(true);
    m_tokenList->setSortingEnabled(false);
    // Columns rebuild dynamically as the scope picker changes — see
    // rebuildColumns().  Initial setup with just the Object + Value
    // pair so the widget has something to render before refreshTokenList.
    rebuildColumns();
    tokensLay->addWidget(m_tokenList, 1);
    m_tabs->addTab(tokensPage, QStringLiteral("Tokens"));
    auto* paletteScroll = new QScrollArea(m_tabs);
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setFrameShape(QFrame::NoFrame);
    m_palettePage = new QWidget;
    paletteScroll->setWidget(m_palettePage);
    m_tabs->addTab(paletteScroll, QStringLiteral("Palette"));
    root->addWidget(m_tabs, 1);

    auto* btnRow = new QHBoxLayout;
    // Theme-management dropdown — left side, since it's a destructive
    // group that shouldn't be confused with the Save / Close primary
    // actions on the right.  Built-in themes can't be renamed or
    // deleted; actions are disabled at click time when the active
    // theme is built-in.
    // No trailing ▾ in the label — QPushButton auto-renders a dropdown
    // arrow once setMenu() is called below, and the two collide visually.
    auto* themeActionsBtn = new QPushButton(QStringLiteral("Theme actions"), bodyWidget());
    auto* themeActionsMenu = new QMenu(themeActionsBtn);
    auto* newThemeAct = themeActionsMenu->addAction(QStringLiteral("New theme from colors…"));   // build 796
    auto* adjustAct = themeActionsMenu->addAction(QStringLiteral("Adjust colors (hue / saturation / lightness)…"));   // build 797
    auto* contrastAct = themeActionsMenu->addAction(QStringLiteral("Check contrast…"));   // build 798
    themeActionsMenu->addSeparator();
    auto* renameAct = themeActionsMenu->addAction(QStringLiteral("Rename theme…"));
    auto* deleteAct = themeActionsMenu->addAction(QStringLiteral("Delete theme…"));
    themeActionsMenu->addSeparator();
    auto* exportAct = themeActionsMenu->addAction(QStringLiteral("Export to file…"));
    auto* importAct = themeActionsMenu->addAction(QStringLiteral("Import from file…"));
    themeActionsBtn->setMenu(themeActionsMenu);
    btnRow->addWidget(themeActionsBtn);
    // Build 798: undo / redo (Ctrl+Z / Ctrl+Y).
    m_undoBtn = new QPushButton(QStringLiteral("Undo"), bodyWidget());
    m_redoBtn = new QPushButton(QStringLiteral("Redo"), bodyWidget());
    m_undoBtn->setToolTip(QStringLiteral("Undo the last theme edit (Ctrl+Z)"));
    m_redoBtn->setToolTip(QStringLiteral("Redo (Ctrl+Y / Ctrl+Shift+Z)"));
    btnRow->addWidget(m_undoBtn);
    btnRow->addWidget(m_redoBtn);

    btnRow->addStretch(1);
    m_saveAsBtn = new QPushButton(QStringLiteral("Save As…"), bodyWidget());
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), bodyWidget());
    btnRow->addWidget(m_saveAsBtn);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(newThemeAct, &QAction::triggered,
            this, &ThemeEditorDialog::onNewThemeClicked);
    connect(adjustAct, &QAction::triggered,
            this, &ThemeEditorDialog::onAdjustColorsClicked);
    connect(contrastAct, &QAction::triggered,
            this, &ThemeEditorDialog::onCheckContrastClicked);
    connect(m_undoBtn, &QPushButton::clicked, this, [this] { doUndo(false); });
    connect(m_redoBtn, &QPushButton::clicked, this, [this] { doUndo(true); });
    auto* undoSc = new QShortcut(QKeySequence::Undo, this);
    auto* redoSc = new QShortcut(QKeySequence::Redo, this);
    connect(undoSc, &QShortcut::activated, this, [this] { doUndo(false); });
    connect(redoSc, &QShortcut::activated, this, [this] { doUndo(true); });
    connect(m_changedOnly, &QCheckBox::toggled, this, [this](bool) { filterTokensTo(m_activeSubset); });
    connect(m_compareCombo, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (name.isEmpty())
            return;
        AppSettings::instance().setValue(QStringLiteral("ThemeEditorCompareWith"), name);
        m_compareTokens = ThemeManager::instance().rootTokensOfTheme(name);
        filterTokensTo(m_activeSubset);
    });
    connect(renameAct, &QAction::triggered,
            this, &ThemeEditorDialog::onRenameThemeClicked);
    connect(deleteAct, &QAction::triggered,
            this, &ThemeEditorDialog::onDeleteThemeClicked);
    connect(exportAct, &QAction::triggered,
            this, &ThemeEditorDialog::onExportThemeClicked);
    connect(importAct, &QAction::triggered,
            this, &ThemeEditorDialog::onImportThemeClicked);

    // Accept .aethertheme / .json drag-and-drop on the dialog body — the
    // PersistentDialog base widget gets the drop events as long as the
    // outermost dialog has setAcceptDrops(true).
    setAcceptDrops(true);

    refreshContainerCombo();
    refreshTokenList();
    rebuildPalettePage();   // build 796
    reloadCompareThemes();  // build 798
    updateUndoButtons();
    updateTitle();
    m_lastRenderedTheme = ThemeManager::instance().activeTheme();
    m_inheritedCellColor = inheritedCellBrush().color();

    // Selection-driven editing — `currentItemChanged` fires for both
    // mouse clicks and keyboard navigation so arrow-key walks through
    // the list also update the inline editor.
    connect(m_tokenList, &QTreeWidget::currentItemChanged,
            this, &ThemeEditorDialog::onTokenRowSelectionChanged);
    // Click on a scope-column cell focuses that scope in the picker AND
    // selects the row's token — the columnar view becomes the primary
    // navigation surface instead of being read-only.
    connect(m_tokenList, &QTreeWidget::itemClicked,
            this, &ThemeEditorDialog::onTokenCellClicked);
    m_tokenList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tokenList, &QTreeWidget::customContextMenuRequested,
            this, &ThemeEditorDialog::onTokenContextMenu);
    connect(m_tokenEditor, &TokenEditorWidget::tokenChanged,
            this, &ThemeEditorDialog::onTokenEditedByEditor);
    // Built-in themes can't be edited in place; the editor stashes
    // its buffered edit and asks us to run Save As, then we restore
    // the snapshot into the new user copy.
    connect(m_tokenEditor, &TokenEditorWidget::requestSaveAsBeforeCommit,
            this, &ThemeEditorDialog::onSaveAsBeforeCommit);
    connect(m_containerCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ThemeEditorDialog::onContainerChanged);

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        // Re-evaluate visibility against both the text filter and the
        // last inspector-picked subset (if any) — filterTokensTo() reads
        // the QLineEdit's current text directly.
        filterTokensTo(m_activeSubset);
    });
    connect(m_saveAsBtn, &QPushButton::clicked,
            this, &ThemeEditorDialog::onSaveAsClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    // Active-theme switches outside the editor (e.g. via View → Theme)
    // should refresh both the title and the swatch values so the editor
    // doesn't lie about what it's editing.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ThemeEditorDialog::onActiveThemeChanged);

    // Inspector wiring.  The inspector is owned by the dialog (parent),
    // so it dies when the dialog does and removes its event filter
    // cleanly.
    m_inspector = new ThemeInspector(this, this);
    connect(m_inspectBtn, &QPushButton::toggled,
            this, &ThemeEditorDialog::onInspectToggled);
    connect(m_inspector, &ThemeInspector::widgetPicked,
            this, &ThemeEditorDialog::onInspectorPicked);
    connect(m_inspector, &ThemeInspector::activeChanged,
            this, &ThemeEditorDialog::onInspectorActiveChanged);
    connect(m_inspector, &ThemeInspector::canceled, this, [this]() {
        updateInspectorStatus(QStringLiteral("Inspector canceled."));
    });
}

void ThemeEditorDialog::rebuildColumns()
{
    if (!m_tokenList) return;
    // Column layout: Object | <root, seg0, seg0/seg1, …, leaf> | Value
    QStringList scopeLevels;       // canonical paths for each header column
    QStringList headerLabels = { QStringLiteral("Object") };
    if (m_activeContainerPath.isEmpty()) {
        // No scope selected → just Object + Value.  Skip the "root"
        // column since it would duplicate the Value column.
    } else {
        scopeLevels.append(QString());          // root
        headerLabels.append(QStringLiteral("root"));
        const QStringList segs = m_activeContainerPath.split(QLatin1Char('/'),
                                                             Qt::SkipEmptyParts);
        QString running;
        for (const QString& s : segs) {
            running = running.isEmpty() ? s : running + QLatin1Char('/') + s;
            scopeLevels.append(running);
            headerLabels.append(s);
        }
    }
    headerLabels.append(QStringLiteral("Value"));

    m_tokenList->setColumnCount(headerLabels.size());
    m_tokenList->setHeaderLabels(headerLabels);
    // The Object + Value columns size to content; intermediate scope
    // columns get reasonable fixed widths so the table doesn't lurch
    // every time the user picks a different leaf.
    auto* hdr = m_tokenList->header();
    if (hdr) {
        hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        for (int i = 1; i < headerLabels.size() - 1; ++i) {
            hdr->setSectionResizeMode(i, QHeaderView::Stretch);
        }
        hdr->setSectionResizeMode(headerLabels.size() - 1,
                                  QHeaderView::ResizeToContents);
        hdr->setStretchLastSection(false);
    }
    // Stash scope paths on the header column index so populateRow()
    // can use them to look up the override value at each level.
    for (int i = 0; i < scopeLevels.size(); ++i) {
        // header data slot per column: Qt::UserRole carries the path
        m_tokenList->headerItem()->setData(i + 1, Qt::UserRole, scopeLevels.at(i));
    }
}

void ThemeEditorDialog::refreshTokenList()
{
    m_tokenList->clear();
    rebuildColumns();
    auto& tm = ThemeManager::instance();
    for (const QString& key : tm.allTokenKeys()) {
        // Editable token namespaces — gradient + scalar colours under
        // "color.", string + integer leaves under "font." and "sizing.".
        if (!key.startsWith(QStringLiteral("color."))
            && !key.startsWith(QStringLiteral("font."))
            && !key.startsWith(QStringLiteral("sizing."))) {
            continue;
        }
        auto* item = new QTreeWidgetItem;
        item->setData(0, Qt::UserRole, key);
        m_tokenList->addTopLevelItem(item);
        populateRow(item);
    }
    // Re-apply the current filter (text + inspector subset) so the
    // user's view doesn't suddenly fall back to "show everything"
    // after a scope change / theme reload.  filterTokensTo reads the
    // QLineEdit's current text directly.
    filterTokensTo(m_activeSubset);
}

void ThemeEditorDialog::populateRow(QTreeWidgetItem* item)
{
    if (!item) return;
    const QString key = item->data(0, Qt::UserRole).toString();
    auto& tm = ThemeManager::instance();

    // Column 0 — Object: token name + swatch.
    QIcon swatch;
    if (key.startsWith(QStringLiteral("color."))) {
        if (tm.brush(key).gradient()) {
            swatch = gradientSwatchIcon(tm.gradient(key));
        } else {
            swatch = swatchIcon(tm.color(key));
        }
    }
    item->setIcon(0, swatch);
    item->setText(0, key);

    // Intermediate scope-chain columns (if any).  Each shows either
    // the literal override stored at that scope, or "inherited" in
    // italics when the scope inherits from an ancestor.
    const int nCols = m_tokenList->columnCount();
    const int lastCol = nCols - 1;
    const QFont normalFont = m_tokenList->font();
    QFont inheritedFont = normalFont; inheritedFont.setItalic(true);
    for (int col = 1; col < lastCol; ++col) {
        const QString scopePath = m_tokenList->headerItem()->data(col, Qt::UserRole).toString();
        if (tm.isOverriddenAt(scopePath, key)) {
            // Format the override succinctly: hex for colours, plain
            // number / string for sizings and font families.
            QString text;
            if (key.startsWith(QStringLiteral("color."))) {
                if (tm.brush(key).gradient()) {
                    {
                    const ThemeGradient g = tm.gradientAt(scopePath, key);
                    text = g.type == ThemeGradient::Radial
                        ? QStringLiteral("radial, %1 stops").arg(g.stops.size())
                        : QStringLiteral("linear, %1°, %2 stops")
                              .arg(static_cast<int>(std::round(g.angle)))
                              .arg(g.stops.size());
                }
                } else if (const QString img = tm.imageFileAt(scopePath, key); !img.isEmpty()) {
                    text = QStringLiteral("image: %1").arg(img);   // build 764
                } else {
                    text = colorToTokenHex(tm.colorAt(scopePath, key));
                }
            } else if (key.startsWith(QStringLiteral("font.family."))) {
                text = tm.valueAt(scopePath, key);
            } else {
                text = QStringLiteral("%1 px").arg(tm.sizingAt(scopePath, key));
            }
            item->setText(col, text);
            item->setFont(col, normalFont);
            item->setForeground(col, QBrush());
            item->setData(col, kInheritedCellRole, QVariant());
        } else {
            item->setText(col, inheritedCellText());
            item->setFont(col, inheritedFont);
            item->setForeground(col, inheritedCellBrush());
            item->setData(col, kInheritedCellRole, true);
        }
    }

    // Last column — Value: the resolved value walking the chain from
    // the leaf container (or root, when no scope is selected).
    const QString leafPath = m_activeContainerPath;
    if (key.startsWith(QStringLiteral("color."))) {
        if (tm.brush(key).gradient()) {
            {
                const ThemeGradient g = tm.gradientAt(leafPath, key);
                const QString text = g.type == ThemeGradient::Radial
                    ? QStringLiteral("radial, %1 stops").arg(g.stops.size())
                    : QStringLiteral("linear, %1°, %2 stops")
                          .arg(static_cast<int>(std::round(g.angle)))
                          .arg(g.stops.size());
                item->setText(lastCol, text);
            }
        } else if (const QString img = tm.imageFileAt(leafPath, key); !img.isEmpty()) {
            item->setText(lastCol, QStringLiteral("image: %1").arg(img));   // build 764
        } else {
            item->setText(lastCol, colorToTokenHex(tm.colorAt(leafPath, key)));
        }
    } else if (key.startsWith(QStringLiteral("font.family."))) {
        item->setText(lastCol, tm.valueAt(leafPath, key));
    } else {
        item->setText(lastCol, QStringLiteral("%1 px").arg(tm.sizingAt(leafPath, key)));
    }
}

void ThemeEditorDialog::updateRow(QTreeWidgetItem* item)
{
    populateRow(item);
}

void ThemeEditorDialog::updateTitle()
{
    const QString name = ThemeManager::instance().activeTheme();
    m_themeLabel->setText(QStringLiteral("Profile: %1").arg(
        name.isEmpty() ? QStringLiteral("(no active theme)") : name));
    setWindowTitle(QStringLiteral("Theme Editor — %1").arg(name));
}

void ThemeEditorDialog::onTokenRowSelectionChanged()
{
    // Triggered by either mouse click or keyboard navigation on the
    // token list — load the selected token into the inline editor so
    // its controls reflect the value and the operator can edit in place.
    auto* item = m_tokenList->currentItem();
    if (!item) {
        m_tokenEditor->setToken(QString());
        return;
    }
    m_tokenEditor->setToken(item->data(0, Qt::UserRole).toString());
}

void ThemeEditorDialog::onTokenEditedByEditor(const QString& key)
{
    // The inline editor just wrote a new value for `key` to ThemeManager —
    // refresh the matching list row so its swatch + descriptor reflect
    // the edit.  ThemeManager's themeChanged signal already drove a
    // repaint everywhere else; this just keeps the editor's own list
    // consistent.
    for (int i = 0; i < m_tokenList->topLevelItemCount(); ++i) {
        auto* item = m_tokenList->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == key) {
            updateRow(item);
            break;
        }
    }
}

void ThemeEditorDialog::onRenameThemeClicked()
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    const QString current = tm.activeTheme();
    if (tm.isBuiltInTheme(current)) {
        showModalMessage(QMessageBox::Information, this, QStringLiteral("Cannot rename"),
            QStringLiteral("\"%1\" is a built-in theme and can't be renamed. "
                           "Use Save As… to create an editable copy under a "
                           "different name first.").arg(current));
        return;
    }
    bool ok = false;
    const QString newName = QInputDialog::getText(this,
        QStringLiteral("Rename theme"),
        QStringLiteral("New name for \"%1\":").arg(current),
        QLineEdit::Normal, current, &ok).trimmed();
    if (!self || !ok || newName.isEmpty() || newName == current) {
        return;
    }

    // Check the NAME before attempting the rename, so a refusal can say what
    // was actually wrong.  ThemeManager enforces this too — that's the safety
    // barrier; this is the diagnosis.  Without it the operator who typed
    // "HF/Contest" is told the file may be unwriteable and goes looking at
    // directory permissions.
    QString why;
    if (!ThemeManager::isValidThemeName(newName, &why)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Rename failed"), why);
        return;
    }

    if (!tm.renameTheme(current, newName)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Rename failed"),
            QStringLiteral("Could not rename \"%1\" to \"%2\".  A theme with "
                           "that name may already exist, or the theme file "
                           "may be unwriteable.").arg(current, newName));
    }
}

void ThemeEditorDialog::onExportThemeClicked()
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    const QString current = tm.activeTheme();
    if (current.isEmpty()) return;

    // Suggest a filename matching the theme name + the `.aethertheme`
    // extension so shared files are recognisable on sight.  The OS file
    // picker remembers the last directory across invocations.
    const QString suggested = QDir::homePath()
                              + QLatin1Char('/')
                              + current + QStringLiteral(".aethertheme");
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export theme"), suggested,
        QStringLiteral("AetherSDR themes (*.aethertheme *.json)"));
    if (!self || path.isEmpty()) {
        return;
    }

    QString err;
    if (!tm.exportThemeToFile(current, path, &err)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Export failed"),
            QStringLiteral("Could not write \"%1\":\n\n%2").arg(path, err));
        return;
    }
}

void ThemeEditorDialog::onImportThemeClicked()
{
    const QPointer<ThemeEditorDialog> self(this);
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Import theme"), QDir::homePath(),
        QStringLiteral("AetherSDR themes (*.aethertheme *.json)"));
    if (!self || path.isEmpty()) {
        return;
    }
    importThemeFromPath(path);
}

void ThemeEditorDialog::importThemeFromPath(const QString& filePath)
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    QString err;
    const QString name = tm.importThemeFromFile(filePath, &err);
    if (!self) {
        return;
    }
    if (name.isEmpty()) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Import failed"),
            QStringLiteral("Could not import \"%1\":\n\n%2").arg(filePath, err));
        return;
    }
    showModalMessage(QMessageBox::Information, this, QStringLiteral("Theme imported"),
        QStringLiteral("Installed \"%1\" and made it the active theme.").arg(name));
}

void ThemeEditorDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        PersistentDialog::dragEnterEvent(event);
        return;
    }
    // Only accept drops where every URL is a theme-shaped file — refusing
    // the drop is the cleanest signal that an arbitrary file isn't valid.
    const auto urls = event->mimeData()->urls();
    for (const QUrl& u : urls) {
        if (!ThemeManager::isImportableThemeFile(u)) {
            PersistentDialog::dragEnterEvent(event);
            return;
        }
    }
    event->acceptProposedAction();
}

void ThemeEditorDialog::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        PersistentDialog::dropEvent(event);
        return;
    }
    const auto urls = event->mimeData()->urls();
    // Re-check here: a drop is not guaranteed to follow an accepted dragEnter.
    for (const QUrl& u : urls) {
        if (ThemeManager::isImportableThemeFile(u)) importThemeFromPath(u.toLocalFile());
    }
    event->acceptProposedAction();
}

void ThemeEditorDialog::onDeleteThemeClicked()
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    const QString current = tm.activeTheme();
    if (tm.isBuiltInTheme(current)) {
        showModalMessage(QMessageBox::Information, this, QStringLiteral("Cannot delete"),
            QStringLiteral("\"%1\" is a built-in theme and can't be deleted.").arg(current));
        return;
    }
    const auto reply = showModalMessage(QMessageBox::Question, this,
        QStringLiteral("Delete theme"),
        QStringLiteral("Permanently delete \"%1\"?\n\n"
                       "The theme file at %2 will be removed. "
                       "The active theme will switch to Default Dark.")
            .arg(current,
                 userThemesDir() + QDir::separator()
                     + current + QStringLiteral(".json")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (!self || reply != QMessageBox::Yes) {
        return;
    }

    if (!tm.deleteTheme(current)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Delete failed"),
            QStringLiteral("Could not delete \"%1\". The theme file may be "
                           "locked by another process.").arg(current));
        return;
    }

    // The per-theme recent-colors bucket in AppSettings outlives the
    // theme file by default; clean it up so deleted themes don't
    // accumulate orphan entries under ThemeEditor.RecentColors/<name>.
    auto& s = AppSettings::instance();
    const QString recentsKey = TokenEditorWidget::recentColorsKeyFor(current);
    if (s.contains(recentsKey)) {
        s.remove(recentsKey);
        s.save();
    }
}


void ThemeEditorDialog::onSaveAsClicked()
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    QString suggestion = tm.activeTheme();
    if (!suggestion.startsWith(QStringLiteral("My ")))
        suggestion = QStringLiteral("My %1").arg(suggestion);

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        QStringLiteral("Save Theme As"),
        QStringLiteral("Theme name:"),
        QLineEdit::Normal, suggestion, &ok).trimmed();
    if (!self || !ok || name.isEmpty()) {
        return;
    }

    // Name first, so a rejected name is reported as a rejected name.
    QString why;
    if (!ThemeManager::isValidThemeName(name, &why)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Save failed"), why);
        return;
    }

    // Disallow overwriting a built-in (bundled :/themes/*.json) — that
    // would silently shadow it from the user dir which is confusing.
    // User-dir themes can be overwritten freely.
    const QStringList existing = tm.availableThemes();
    if (existing.contains(name)) {
        const auto reply = showModalMessage(QMessageBox::Question, this,
            QStringLiteral("Theme exists"),
            QStringLiteral("A theme named \"%1\" already exists. Overwrite it?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (!self || reply != QMessageBox::Yes) {
            return;
        }
    }

    if (!tm.saveCurrentThemeAs(name)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Save failed"),
            QStringLiteral("Could not write the theme file. Check that "
                           "%1 is writable.").arg(userThemesDir()));
        return;
    }
    // saveCurrentThemeAs() makes the new theme active; onActiveThemeChanged
    // will fire from themeChanged and refresh the title.
}

void ThemeEditorDialog::onSaveAsBeforeCommit()
{
    const QPointer<ThemeEditorDialog> self(this);
    auto& tm = ThemeManager::instance();
    const QString current = tm.activeTheme();
    if (!tm.isBuiltInTheme(current)) {
        // Race / stale signal — nothing to fork, just complete the
        // commit directly.
        m_tokenEditor->completeDeferredCommit();
        return;
    }

    QString suggestion = QStringLiteral("My %1").arg(current);
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        QStringLiteral("Save Theme As"),
        QStringLiteral("\"%1\" is a built-in theme and can't be modified "
                       "directly.\nSave your changes as a new theme:")
            .arg(current),
        QLineEdit::Normal, suggestion, &ok).trimmed();
    if (!self || !ok || name.isEmpty()) {
        return;
    }

    // Name first.  This path matters more than the plain Save As: the operator
    // has a pending token edit stashed in DeferredEdit, and every early return
    // below drops it.  Being told the real reason is what lets them retype the
    // name and keep the edit instead of concluding the editor is broken.
    QString why;
    if (!ThemeManager::isValidThemeName(name, &why)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Save failed"), why);
        return;
    }

    // Disallow accidentally writing to another built-in or overwriting
    // an existing user theme without confirmation.
    if (tm.isBuiltInTheme(name)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Reserved name"),
            QStringLiteral("\"%1\" is a built-in theme name and can't be used.")
                .arg(name));
        return;
    }
    const QStringList existing = tm.availableThemes();
    if (existing.contains(name)) {
        const auto reply = showModalMessage(QMessageBox::Question, this,
            QStringLiteral("Theme exists"),
            QStringLiteral("A theme named \"%1\" already exists. Overwrite it?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (!self || reply != QMessageBox::Yes) {
            return;
        }
    }

    if (!tm.saveCurrentThemeAs(name)) {
        showModalMessage(QMessageBox::Warning, this, QStringLiteral("Save failed"),
            QStringLiteral("Could not write the theme file. Check that "
                           "%1 is writable.").arg(userThemesDir()));
        return;
    }
    if (!self) {
        return;
    }
    // saveCurrentThemeAs() has switched the active theme and refreshed
    // the list (clearing the editor's selection in the process).  The
    // editor stashed its buffer in DeferredEdit before emitting; tell
    // it to write that into the new user copy now.
    m_tokenEditor->completeDeferredCommit();

    // Re-select the row that matches the just-committed token so the
    // list highlight matches what the editor is showing.
    const QString committed = m_tokenEditor->currentToken();
    if (!committed.isEmpty()) {
        for (int i = 0; i < m_tokenList->topLevelItemCount(); ++i) {
            auto* it = m_tokenList->topLevelItem(i);
            if (it->data(0, Qt::UserRole).toString() == committed) {
                m_tokenList->setCurrentItem(it);
                break;
            }
        }
    }
}

void ThemeEditorDialog::onInspectToggled(bool on)
{
    if (!m_inspector) return;
    if (on) {
        m_activeSubset.clear();
        filterTokensTo({});                       // clear any prior subset
        m_filterEdit->clear();
        updateInspectorStatus(QStringLiteral(
            "Move the cursor over the main UI and click a region.  "
            "ESC cancels."));
        m_inspector->start();
    } else {
        m_inspector->stop();
    }
}

void ThemeEditorDialog::onInspectorActiveChanged(bool active)
{
    // Keep the toggle button's checked state in lock-step with the
    // inspector — covers self-deactivation paths (post-pick, ESC).
    if (m_inspectBtn && m_inspectBtn->isChecked() != active) {
        QSignalBlocker sb(m_inspectBtn);
        m_inspectBtn->setChecked(active);
    }
}

void ThemeEditorDialog::onInspectorPicked(QWidget* target, QPoint localPos)
{
    if (!target) return;
    auto& tm = ThemeManager::instance();

    // Auto-select the picked widget's container scope so any subsequent
    // edit lands at the right level of the inheritance chain.  Walks
    // `target`'s Qt parent chain to the nearest declared `themeContainer`
    // (the same lookup widget-aware getters use for token resolution).
    const QString pickedScope = tm.containerPathFor(target);
    if (m_containerCombo) {
        const int idx = m_containerCombo->findData(pickedScope);
        if (idx >= 0) m_containerCombo->setCurrentIndex(idx);
    }

    // Walk up the parent chain until we find a widget with a declared
    // token list — that's how a click on a deep child (e.g. the QLabel
    // inside a QPushButton inside a themed panel) still surfaces the
    // panel-level tokens.  Use tokensAtPoint() instead of plain
    // tokensForWidget() so custom-paint widgets that declared sub-regions
    // (panadapter trace vs waterfall vs slice triangles) get the narrowed
    // hit-list rather than every token they paint with.  The point is
    // mapped from the original target's coordinate space into each
    // ancestor's space as we walk.
    QStringList tokens;
    QWidget* w = target;
    QString hitName = target->metaObject()->className();
    QPoint scanPos = localPos;

    // ANAN fork build 763 (operator, 26.11: "users change a button picked
    // with Inspect and a quite different button changes"). The walk below
    // went on up to the first ancestor with tokens whenever the picked
    // widget had none of its own -- and for a button whose colours are
    // written in its own style sheet (most of the program's buttons) or a
    // widget that paints itself, that ancestor's tokens are the colours of
    // OTHER widgets: editing them changed those, never the one picked. Now:
    // a click inside a control (button, combo, slider, spin box, edit) is
    // the control; a control -- or any widget -- that has its own style
    // sheet or paints itself and declares no tokens says so instead of
    // borrowing its parent's; only a widget without a style of its own (a
    // label in a panel, a button the panel's QPushButton rule styles) still
    // takes the tokens of the panel styling it.
    {
        const auto isControl = [](const QWidget* x) {
            return qobject_cast<const QAbstractButton*>(x)
                || qobject_cast<const QComboBox*>(x)
                || qobject_cast<const QAbstractSlider*>(x)
                || qobject_cast<const QAbstractSpinBox*>(x)
                || qobject_cast<const QLineEdit*>(x);
        };
        for (QWidget* x = target; x; x = x->parentWidget()) {
            if (isControl(x)) {
                if (x != target) {
                    scanPos = x->mapFromGlobal(target->mapToGlobal(localPos));
                    w = x;
                    hitName = x->metaObject()->className();
                }
                break;
            }
            if (x->isWindow())
                break;
        }
        const bool ownStyle = tm.isTrackedWidget(w) || !w->styleSheet().isEmpty();
        const bool selfPainted = QString::fromLatin1(w->metaObject()->className())
                                     .startsWith(QLatin1String("AetherSDR::"));
        if (tm.tokensAtPoint(w, scanPos).isEmpty() && (ownStyle || selfPainted)) {
            updateInspectorStatus(QStringLiteral(
                "%1: its colours are set in the program's code, not by theme tokens "
                "-- the theme editor cannot change this element.").arg(hitName));
            m_activeSubset.clear();
            filterTokensTo({});
            return;
        }
    }
    while (w && tokens.isEmpty()) {
        tokens = tm.tokensAtPoint(w, scanPos);
        if (!tokens.isEmpty()) break;
        QWidget* parent = w->parentWidget();
        if (!parent) break;
        scanPos = parent->mapFromGlobal(w->mapToGlobal(scanPos));
        w = parent;
    }

    if (tokens.isEmpty()) {
        updateInspectorStatus(QStringLiteral(
            "%1: no theme tokens registered for this region yet.  Use the "
            "filter or browse the full token list below.").arg(hitName));
        m_activeSubset.clear();
        filterTokensTo({});
        return;
    }

    m_activeSubset = tokens;
    filterTokensTo(tokens);

    const QString matchedClass = w ? w->metaObject()->className() : hitName;
    const QString scopeSuffix = pickedScope.isEmpty()
        ? QString()
        : QStringLiteral("  ·  scope: %1").arg(pickedScope);
    if (w && w != target) {
        updateInspectorStatus(QStringLiteral(
            "%1 (via %2): %3 token%4.%5").arg(matchedClass).arg(hitName)
            .arg(tokens.size()).arg(tokens.size() == 1 ? "" : "s")
            .arg(scopeSuffix));
    } else {
        updateInspectorStatus(QStringLiteral(
            "%1: %2 token%3.%4").arg(matchedClass)
            .arg(tokens.size()).arg(tokens.size() == 1 ? "" : "s")
            .arg(scopeSuffix));
    }

    // Convenience: if exactly one token matched, select it so the
    // inline editor surfaces it immediately — the "click ugly thing →
    // fix it" flow now happens in the editor above instead of a modal.
    if (tokens.size() == 1) {
        for (int i = 0; i < m_tokenList->topLevelItemCount(); ++i) {
            auto* item = m_tokenList->topLevelItem(i);
            if (item->isHidden()) continue;
            if (item->data(0, Qt::UserRole).toString() == tokens.first()) {
                m_tokenList->setCurrentItem(item);
                break;
            }
        }
    }
}

void ThemeEditorDialog::updateInspectorStatus(const QString& text)
{
    if (m_inspectStatus) m_inspectStatus->setText(text);
}

void ThemeEditorDialog::filterTokensTo(const QStringList& subset)
{
    // Empty subset → unhide everything that the text filter doesn't
    // already exclude (i.e. fall back to the QLineEdit's current
    // textChanged behaviour).
    const QString needle = m_filterEdit
                               ? m_filterEdit->text().trimmed().toLower()
                               : QString();
    const QSet<QString> wanted(subset.begin(), subset.end());
    const bool changedOnly = m_changedOnly && m_changedOnly->isChecked();   // build 798
    int changed = 0;
    for (int i = 0; i < m_tokenList->topLevelItemCount(); ++i) {
        auto* item = m_tokenList->topLevelItem(i);
        const QString key = item->data(0, Qt::UserRole).toString();
        bool hidden = false;
        if (!subset.isEmpty() && !wanted.contains(key)) hidden = true;
        if (!hidden && !needle.isEmpty() && !key.contains(needle)) hidden = true;
        if (!hidden && changedOnly && !tokenDiffers(key)) hidden = true;
        if (changedOnly && tokenDiffers(key)) ++changed;
        item->setHidden(hidden);
    }
    if (m_changedCount)
        m_changedCount->setText(changedOnly ? QStringLiteral("%1 changed").arg(changed) : QString());
}

void ThemeEditorDialog::refreshContainerCombo()
{
    if (!m_containerCombo) return;
    QSignalBlocker block(m_containerCombo);
    m_containerCombo->clear();
    // Friendlier label for the empty (root) path so the dropdown is
    // self-explanatory; userData carries the real path string.
    m_containerCombo->addItem(QStringLiteral("(root)"), QString());
    for (const QString& path : ThemeManager::instance().containerPaths()) {
        if (path.isEmpty()) continue;  // already added as "(root)"
        m_containerCombo->addItem(path, path);
    }
    // Clamp the combo to the longest label + 4 px pad on each side
    // (plus arrow + frame allowance).  Sized once per refresh; combo
    // stays exactly that wide regardless of which item is selected.
    QFontMetrics fm(m_containerCombo->font());
    int maxLabelW = 0;
    for (int i = 0; i < m_containerCombo->count(); ++i) {
        maxLabelW = std::max(maxLabelW,
                             fm.horizontalAdvance(m_containerCombo->itemText(i)));
    }
    // 4 px L + 4 px R text padding (overrides Theme.h's 6/6 default),
    // ~20 px dropdown arrow, ~2 px frame borders.
    m_containerCombo->setStyleSheet(QStringLiteral(
        "QComboBox { padding: 2px 4px; }"));
    m_containerCombo->setFixedWidth(maxLabelW + 4 + 4 + 20 + 2);
    // Restore previous selection by path string, falling back to root.
    int idx = m_containerCombo->findData(m_activeContainerPath);
    if (idx < 0) idx = 0;
    m_containerCombo->setCurrentIndex(idx);
}

void ThemeEditorDialog::onContainerChanged(int)
{
    if (!m_containerCombo) return;
    m_activeContainerPath = m_containerCombo->currentData().toString();
    if (m_tokenEditor) m_tokenEditor->setActiveContainerPath(m_activeContainerPath);
    // Defer the heavy rebuild to the next event-loop tick.  When the
    // inspector triggers a scope change via setCurrentIndex, the slot
    // runs inside the synchronous signal cascade from
    // ThemeInspector::eventFilter → widgetPicked → onInspectorPicked
    // → setCurrentIndex; tearing down QTreeWidgetItems (and their
    // QPixmap-backed icon QVariants) inside that stack has hit a
    // destructor SEGV in the wild.  Deferring breaks the chain — the
    // inspector callback completes, and the rebuild runs from a
    // clean stack on the next iteration.
    QTimer::singleShot(0, this, &ThemeEditorDialog::refreshTokenList);
}

void ThemeEditorDialog::onTokenCellClicked(QTreeWidgetItem* item, int column)
{
    if (!item || !m_tokenList) return;
    if (column == 0) return;                                  // Object column: already wired via currentItemChanged
    if (column == m_tokenList->columnCount() - 1) return;     // Value column: just informational
    // Scope-chain column — switch the active scope to that column's
    // path so the editor's commit lands there.  The user's perspective
    // becomes "I want to override this token at this exact scope".
    const QString scopePath =
        m_tokenList->headerItem()->data(column, Qt::UserRole).toString();
    // Sync the picker, which fires onContainerChanged → updates the
    // editor + columns + repopulates the rows.
    const int idx = m_containerCombo
                        ? m_containerCombo->findData(scopePath)
                        : -1;
    if (idx >= 0) m_containerCombo->setCurrentIndex(idx);
    // After the picker change re-selects the previous current row,
    // explicitly re-select this row so the editor reflects the clicked
    // token (the refresh may have lost the highlight).
    m_tokenList->setCurrentItem(item);
}

void ThemeEditorDialog::onTokenContextMenu(const QPoint& pos)
{
    if (!m_tokenList) return;
    QTreeWidgetItem* item = m_tokenList->itemAt(pos);
    if (!item) return;
    const int column = m_tokenList->columnAt(pos.x());
    const QString token = item->data(0, Qt::UserRole).toString();
    auto& tm = ThemeManager::instance();
    // "Clear override" -- only on a scope-chain column (not Object, not
    // Value, not root) where an actual override exists. Reset-to-factory
    // belongs to the dedicated Reset button on the inspect row.
    QString scopePath;
    bool canClear = false;
    if (column > 0 && column < m_tokenList->columnCount() - 1) {
        scopePath = m_tokenList->headerItem()->data(column, Qt::UserRole).toString();
        canClear = !scopePath.isEmpty() && tm.isOverriddenAt(scopePath, token);
    }
    // Build 797: colour actions on any column of a plain colour token.
    const QString path = m_activeContainerPath;
    const bool isColour = token.startsWith(QStringLiteral("color."))
                          && tm.gradientAt(path, token).stops.isEmpty();
    const QColor cur = isColour ? tm.colorAt(path, token) : QColor();

    const QString activeTheme = tm.activeTheme();
    const QPointer<ThemeEditorDialog> self(this);
    const QPointer<QTreeWidget> tokenList(m_tokenList);
    ScopedChildWidget<QMenu> menuOwner(this);
    QMenu& menu = *menuOwner.get();
    QAction* clearAct = nullptr;
    if (canClear)
        clearAct = menu.addAction(QStringLiteral("Clear override at %1").arg(scopePath));
    QAction* replaceAct = nullptr;
    QAction* sameAct = nullptr;
    QAction* similarAct = nullptr;
    QHash<QAction*, QString> linkActs;
    if (cur.isValid()) {
        if (clearAct)
            menu.addSeparator();
        const QString hex = colorToTokenHex(cur);
        replaceAct = menu.addAction(swatchIcon(cur),
                                    QStringLiteral("Replace %1 everywhere…").arg(hex));
        sameAct = menu.addAction(QStringLiteral("Show tokens with %1").arg(hex));
        similarAct = menu.addAction(QStringLiteral("Show tokens with similar colors"));
        // The nearest palette entries (exact first).
        QMenu* link = menu.addMenu(QStringLiteral("Link to palette color"));
        QVector<QPair<double, QString>> nearest;   // ("near" is a Windows macro)
        for (const QString& key : tm.primitiveColorKeys()) {
            const QColor p = tm.primitiveColor(key);
            const double d = std::hypot(std::hypot(double(p.red() - cur.red()), double(p.green() - cur.green())),
                                        double(p.blue() - cur.blue()));
            nearest.append({d, key});
        }
        std::sort(nearest.begin(), nearest.end());
        for (int i = 0; i < nearest.size() && i < 8; ++i) {
            const QColor p = tm.primitiveColor(nearest[i].second);
            QAction* a = link->addAction(swatchIcon(p), QStringLiteral("%1   %2%3")
                .arg(nearest[i].second, p.name(QColor::HexRgb),
                     nearest[i].first < 0.5 ? QStringLiteral("  (same)") : QString()));
            linkActs.insert(a, nearest[i].second);
        }
        link->setEnabled(!nearest.isEmpty());
    }
    QAction* showAllAct = nullptr;
    if (!m_activeSubset.isEmpty()) {
        menu.addSeparator();
        showAllAct = menu.addAction(QStringLiteral("Show all tokens"));
    }
    if (menu.isEmpty())
        return;
    QAction* picked = menu.exec(m_tokenList->viewport()->mapToGlobal(pos));
    if (!self || !menuOwner || !tokenList
        || self->m_tokenList != tokenList.data() || !picked
        || tm.activeTheme() != activeTheme) {
        return;
    }

    auto showSubset = [self](const QStringList& subset, const QString& status) {
        self->m_activeSubset = subset;
        self->filterTokensTo(subset);
        self->updateInspectorStatus(status);
    };
    if (picked == clearAct) {
        tm.removeOverride(scopePath, token);
        if (!self || !tokenList || self->m_tokenList != tokenList.data()) {
            return;
        }
        // Find the current row after the nested loop; the original item may
        // have been replaced. Keep selection and scroll position intact.
        QTreeWidgetItemIterator rows(tokenList.data());
        while (*rows) {
            if ((*rows)->data(0, Qt::UserRole).toString() == token) {
                self->populateRow(*rows);
                break;
            }
            ++rows;
        }
        return;
    }
    if (picked == sameAct || picked == similarAct) {
        const bool similar = picked == similarAct;
        QStringList subset;
        for (const QString& key : tm.allTokenKeys()) {
            if (!key.startsWith(QStringLiteral("color.")) || !tm.gradientAt(path, key).stops.isEmpty())
                continue;
            const QColor c = tm.colorAt(path, key);
            if (!c.isValid())
                continue;
            const double d = std::hypot(std::hypot(double(c.red() - cur.red()), double(c.green() - cur.green())),
                                        double(c.blue() - cur.blue()));
            if (similar ? (d <= 28.0 && std::abs(c.alpha() - cur.alpha()) <= 40) : c.rgba() == cur.rgba())
                subset << key;
        }
        showSubset(subset, similar ? QStringLiteral("%1 tokens with colors close to %2 (right-click > Show all tokens).")
                                         .arg(subset.size()).arg(colorToTokenHex(cur))
                                   : QStringLiteral("%1 tokens with %2 (right-click > Show all tokens).")
                                         .arg(subset.size()).arg(colorToTokenHex(cur)));
        return;
    }
    if (picked == showAllAct) {
        showSubset({}, QString());
        return;
    }
    if (picked == replaceAct) {
        QColorDialog::ColorDialogOptions opts;
        if (cur.alpha() < 255)
            opts |= QColorDialog::ShowAlphaChannel;
        const QColor to = QColorDialog::getColor(cur, this,
            QStringLiteral("Replace %1 everywhere").arg(colorToTokenHex(cur)), opts);
        if (!self || !to.isValid() || !self->ensureEditableTheme())
            return;
        const int n = ThemeManager::instance().replaceColorEverywhere(cur, to);
        if (!self)
            return;
        self->refreshTokenList();
        self->filterTokensTo(self->m_activeSubset);
        self->rebuildPalettePage();
        self->updateInspectorStatus(QStringLiteral("%1 → %2: %3 places changed.")
                                        .arg(colorToTokenHex(cur), colorToTokenHex(to)).arg(n));
        return;
    }
    if (linkActs.contains(picked)) {
        const QString key = linkActs.value(picked);
        if (!self->ensureEditableTheme())
            return;
        ThemeManager::instance().linkTokenToPrimitive(self->m_activeContainerPath, token, key);
        if (!self)
            return;
        self->refreshTokenList();
        self->filterTokensTo(self->m_activeSubset);
        self->rebuildPalettePage();
        self->updateInspectorStatus(QStringLiteral("%1 now follows {%2}.").arg(token, key));
    }
}

void ThemeEditorDialog::onActiveThemeChanged()
{
    // themeChanged fires for BOTH "user switched active theme" (View →
    // Theme menu, Save As) AND "user edited one token in-place"
    // (setColor / setSizing).  We must only rebuild the row list for
    // the first case; for the second, the caller in onTokenRowClicked
    // is still holding a pointer to the row it clicked and will run
    // updateRow() on it — destroying the list mid-edit causes a SEGV
    // in QPixmapIconEngine when the now-dangling row tries to repaint.
    //
    // Distinguish the two by comparing active-theme names.
    const QString current = ThemeManager::instance().activeTheme();
    updateTitle();
    if (current != m_lastRenderedTheme) {
        m_lastRenderedTheme = current;
        m_inheritedCellColor = inheritedCellBrush().color();
        refreshContainerCombo();
        refreshTokenList();
        rebuildPalettePage();   // build 796
        reloadCompareThemes();  // build 798
        updateUndoButtons();
        return;
    }
    updateUndoButtons();   // build 798
    // Build 798: an edit may move a token into / out of "changed".
    if (m_changedOnly && m_changedOnly->isChecked()) {
        const QPointer<ThemeEditorDialog> self(this);
        QTimer::singleShot(0, this, [self] {
            if (self)
                self->filterTokensTo(self->m_activeSubset);
        });
    }
    // An in-place edit keeps every row, but the "inherited" cells carry
    // color.text.secondary, which may be the token just edited. Repaint them
    // without rebuilding the list, and only when that colour moved.
    const QBrush inherited = inheritedCellBrush();
    if (!m_tokenList || inherited.color() == m_inheritedCellColor) {
        return;
    }
    m_inheritedCellColor = inherited.color();
    const int lastCol = m_tokenList->columnCount() - 1;
    for (QTreeWidgetItemIterator rows(m_tokenList); *rows; ++rows) {
        for (int col = 1; col < lastCol; ++col) {
            if ((*rows)->data(col, kInheritedCellRole).toBool()) {
                (*rows)->setForeground(col, inherited);
            }
        }
    }
}

// ── ANAN fork build 796: the Palette tab and the new-theme wizard ───────────

namespace {

QIcon wideSwatch(const QColor& c, int w = 40, int h = 18)
{
    QPixmap pm(w, h);
    pm.fill(c.isValid() ? c : QColor(Qt::transparent));
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 140));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    p.end();
    return QIcon(pm);
}

// A swatch button: clicking it asks for a colour and calls `set` with it.
QToolButton* colorButton(QWidget* parent, const QColor& c, const QString& title,
                         std::function<void(const QColor&)> set)
{
    auto* b = new QToolButton(parent);
    b->setIcon(wideSwatch(c));
    b->setIconSize(QSize(40, 18));
    b->setToolTip(c.name(QColor::HexRgb));
    QObject::connect(b, &QToolButton::clicked, parent, [b, c, title, set] {
        const QPointer<QToolButton> guard(b);
        const QColor picked = QColorDialog::getColor(c, b->window(), title);
        if (guard && picked.isValid())
            set(picked);
    });
    return b;
}

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    ThemeManager::instance().applyStyleSheet(
        l, QStringLiteral("QLabel { color: {{color.text.secondary}}; }"));
    return l;
}

// Build 803 (operator, 26.11: "Secondary label, ON and OFF do nothing"):
// the wizard's preview is a live sample, painted (no style sheet, so the
// current theme's literal remap cannot touch its colours): ground, a panel
// with text, a toggle button (click: ON / OFF) and a push button, both with
// hover and pressed states, an accent trace, and the 12 + 3 code-colour ramp.
class ThemePreviewWidget : public QWidget
{
public:
    explicit ThemePreviewWidget(QWidget* parent) : QWidget(parent)
    {
        setMouseTracking(true);
        setFixedSize(360, 124);
        setToolTip(QStringLiteral("Preview of the new theme -- the buttons can be clicked."));
    }
    void setColors(const ThemeManager::NewThemeColors& c)
    {
        ThemeManager::buildLiteralRamps(c, &m_n, &m_a);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_n.size() != 12 || m_a.size() != 3)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), m_n[2]);
        p.setPen(m_n[5]);
        p.setBrush(m_n[4]);
        p.drawRoundedRect(QRectF(10.5, 10.5, 230, 66), 4, 4);
        QFont f = font();
        p.setFont(f);
        p.setPen(m_n[9]);
        p.drawText(QRect(20, 15, 210, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Panel text  14.074.000"));
        p.setPen(m_n[8]);
        p.drawText(QRect(20, 32, 210, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Secondary label"));
        // Toggle button: ON = accent, OFF = neutral; hover lighter, pressed darker.
        {
            const bool hov = m_hover == 1, prs = m_press == 1;
            QColor bg = m_on ? m_a[0] : m_n[3];
            QColor bd = m_on ? m_a[1] : m_n[5];
            if (hov) { bg = m_on ? m_a[1] : m_n[5]; bd = m_on ? m_a[2] : m_n[7]; }
            if (prs) bg = bg.darker(130);
            p.setPen(bd);
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(kToggle).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p.setPen(m_on ? QColor(255, 255, 255) : m_n[9]);
            p.drawText(kToggle, Qt::AlignCenter, m_on ? QStringLiteral("ON") : QStringLiteral("OFF"));
        }
        // Push button.
        {
            const bool hov = m_hover == 2, prs = m_press == 2;
            QColor bg = hov ? m_n[5] : m_n[3];
            if (prs) bg = m_a[0];
            p.setPen(hov ? m_a[1] : m_n[5]);
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(kPush).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p.setPen(m_n[9]);
            p.drawText(kPush, Qt::AlignCenter, QStringLiteral("Button"));
        }
        // Accent trace.
        QPen tr(m_a[1]);
        tr.setWidthF(1.5);
        p.setPen(tr);
        const QPointF pts[] = {{250, 66}, {268, 40}, {286, 54}, {304, 24}, {322, 46}, {350, 30}};
        p.drawPolyline(pts, 6);
        // The ramp.
        const int sw = 20;
        p.setPen(QColor(0, 0, 0, 120));
        for (int i = 0; i < 12; ++i) {
            p.setBrush(m_n[i]);
            p.drawRect(10 + i * sw, 90, sw, 22);
        }
        for (int i = 0; i < 3; ++i) {
            p.setBrush(m_a[i]);
            p.drawRect(10 + 12 * sw + 12 + i * sw, 90, sw, 22);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        const int h = hitTest(e->position().toPoint());
        if (h != m_hover) {
            m_hover = h;
            setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent*) override
    {
        m_hover = 0;
        m_press = 0;
        update();
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_press = hitTest(e->position().toPoint());
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        if (m_press == 1 && hitTest(e->position().toPoint()) == 1)
            m_on = !m_on;
        m_press = 0;
        update();
    }

private:
    static constexpr QRect kToggle{20, 52, 64, 18};
    static constexpr QRect kPush{94, 52, 64, 18};
    int hitTest(const QPoint& pt) const
    {
        if (kToggle.contains(pt)) return 1;
        if (kPush.contains(pt)) return 2;
        return 0;
    }
    QVector<QColor> m_n, m_a;
    bool m_on = true;
    int m_hover = 0;   // 0 none, 1 toggle, 2 push
    int m_press = 0;
};

} // namespace

bool ThemeEditorDialog::ensureEditableTheme()
{
    auto& tm = ThemeManager::instance();
    if (!tm.isBuiltInTheme(tm.activeTheme()))
        return true;
    const QPointer<ThemeEditorDialog> self(this);
    showModalMessage(QMessageBox::Information, this, QStringLiteral("Built-in theme"),
        QStringLiteral("\"%1\" is a built-in theme and can't be changed. "
                       "Save a copy first, then edit the copy.").arg(tm.activeTheme()));
    if (!self)
        return false;
    onSaveAsClicked();
    return self && !tm.isBuiltInTheme(tm.activeTheme());
}

void ThemeEditorDialog::rebuildPalettePage()
{
    if (!m_palettePage)
        return;
    if (auto* old = m_palettePage->layout()) {
        while (QLayoutItem* it = old->takeAt(0)) {
            if (QWidget* w = it->widget()) {
                w->hide();
                w->deleteLater();   // may be the button being clicked
            }
            delete it;
        }
        delete old;
    }
    auto& tm = ThemeManager::instance();
    auto* page = m_palettePage;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(4, 8, 4, 4);
    lay->setSpacing(6);
    const QPointer<ThemeEditorDialog> self(this);
    // After a palette edit: the swatches of aliasing tokens, and this page.
    auto refreshLater = [self] {
        QTimer::singleShot(0, self.data(), [self] {
            if (!self)
                return;
            self->refreshTokenList();
            self->rebuildPalettePage();
        });
    };

    // ── Primitives ──
    auto* hdr1 = new QLabel(QStringLiteral("<b>Palette</b>"), page);
    lay->addWidget(hdr1);
    lay->addWidget(hintLabel(QStringLiteral(
        "The base colours tokens refer to as {color.…}. Changing one repaints every "
        "token that uses it."), page));
    auto* grid1Host = new QWidget(page);
    auto* grid1 = new QGridLayout(grid1Host);
    grid1->setContentsMargins(0, 0, 0, 0);
    grid1->setHorizontalSpacing(8);
    grid1->setVerticalSpacing(3);
    grid1->setColumnStretch(1, 1);
    int row = 0;
    const QStringList keys = tm.primitiveColorKeys();
    for (const QString& key : keys) {
        const QColor c = tm.primitiveColor(key);
        grid1->addWidget(colorButton(grid1Host, c, key, [self, key, refreshLater](const QColor& col) {
            if (!self || !self->ensureEditableTheme())
                return;
            ThemeManager::instance().setPrimitiveColor(key, col);
            refreshLater();
        }), row, 0);
        grid1->addWidget(new QLabel(key, grid1Host), row, 1);
        grid1->addWidget(new QLabel(QStringLiteral("%1   ×%2").arg(c.name(QColor::HexRgb))
                                        .arg(tm.primitiveUseCount(key)), grid1Host), row, 2);
        ++row;
    }
    if (keys.isEmpty())
        grid1->addWidget(new QLabel(QStringLiteral("This theme has no palette."), grid1Host), 0, 0, 1, 3);
    lay->addWidget(grid1Host);

    // ── Literal remap ──
    lay->addSpacing(8);
    lay->addWidget(new QLabel(QStringLiteral("<b>Colours written in the code</b>"), page));
    lay->addWidget(hintLabel(QStringLiteral(
        "Much of the program is painted with colours written in its code for Default Dark. "
        "A theme maps them onto its own: each Default Dark grey-blue (left) becomes the colour "
        "on the right, shades in between are blended by lightness; the cyan-blue accent family "
        "maps onto the three accent colours. Status, band and special colours stay as written."),
        page));
    const QVector<QColor> neutral = tm.literalNeutral();
    const QVector<QColor> accent = tm.literalAccent();
    auto* btnRow = new QHBoxLayout;
    auto* gen = new QPushButton(neutral.isEmpty() ? QStringLiteral("Turn on (from theme colours)")
                                                  : QStringLiteral("Regenerate from theme colours"), page);
    gen->setToolTip(QStringLiteral("Builds the mapping from color.background.0, color.background.1,\n"
                                   "color.text.primary and color.accent of this theme."));
    btnRow->addWidget(gen);
    QPushButton* off = nullptr;
    if (!neutral.isEmpty()) {
        off = new QPushButton(QStringLiteral("Turn off"), page);
        off->setToolTip(QStringLiteral("Code colours as written (as in Default Dark)."));
        btnRow->addWidget(off);
    }
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    connect(gen, &QPushButton::clicked, this, [self, refreshLater] {
        if (!self || !self->ensureEditableTheme())
            return;
        auto& t = ThemeManager::instance();
        ThemeManager::NewThemeColors c;
        c.background = t.color(QStringLiteral("color.background.0"));
        c.panel = t.color(QStringLiteral("color.background.1"));
        c.text = t.color(QStringLiteral("color.text.primary"));
        c.accent = t.color(QStringLiteral("color.accent"));
        QVector<QColor> n, a;
        ThemeManager::buildLiteralRamps(c, &n, &a);
        t.setLiteralRemap(n, a);
        refreshLater();
    });
    if (off) {
        connect(off, &QPushButton::clicked, this, [self, refreshLater] {
            if (!self || !self->ensureEditableTheme())
                return;
            ThemeManager::instance().setLiteralRemap({}, {});
            refreshLater();
        });
    }
    if (!neutral.isEmpty()) {
        auto* grid2Host = new QWidget(page);
        auto* grid2 = new QGridLayout(grid2Host);
        grid2->setContentsMargins(0, 0, 0, 0);
        grid2->setHorizontalSpacing(8);
        grid2->setVerticalSpacing(3);
        grid2->setColumnStretch(3, 1);
        const QStringList src = ThemeManager::literalNeutralSources();
        auto srcSwatch = [grid2Host](const QColor& c) {
            auto* l = new QLabel(grid2Host);
            l->setPixmap(wideSwatch(c).pixmap(40, 18));
            l->setToolTip(c.name(QColor::HexRgb));
            return l;
        };
        int r = 0;
        for (int i = 0; i < 12 && i < neutral.size(); ++i, ++r) {
            grid2->addWidget(srcSwatch(QColor(src[i])), r, 0);
            grid2->addWidget(new QLabel(QStringLiteral("→"), grid2Host), r, 1);
            grid2->addWidget(colorButton(grid2Host, neutral[i], src[i], [self, i, refreshLater](const QColor& col) {
                if (!self || !self->ensureEditableTheme())
                    return;
                auto& t = ThemeManager::instance();
                QVector<QColor> n = t.literalNeutral();
                const QVector<QColor> a = t.literalAccent();
                if (n.size() != 12)
                    return;
                n[i] = col;
                t.setLiteralRemap(n, a);
                refreshLater();
            }), r, 2);
            static const char* kRole[12] = {"black", "", "ground", "", "panels", "borders", "", "",
                                            "secondary text", "text", "", "white"};
            grid2->addWidget(new QLabel(QStringLiteral("%1   %2").arg(src[i], QLatin1String(kRole[i])),
                                        grid2Host), r, 3);
        }
        static const char* kAccent[3] = {"accent dark (e.g. #0070c0)", "accent (e.g. #00b4d8)",
                                         "accent light (e.g. #80e0f8)"};
        for (int i = 0; i < 3 && i < accent.size(); ++i, ++r) {
            grid2->addWidget(new QLabel(QStringLiteral("A%1").arg(i + 1), grid2Host), r, 0);
            grid2->addWidget(new QLabel(QStringLiteral("→"), grid2Host), r, 1);
            grid2->addWidget(colorButton(grid2Host, accent[i], QLatin1String(kAccent[i]),
                                         [self, i, refreshLater](const QColor& col) {
                if (!self || !self->ensureEditableTheme())
                    return;
                auto& t = ThemeManager::instance();
                const QVector<QColor> n = t.literalNeutral();
                QVector<QColor> a = t.literalAccent();
                if (a.size() != 3)
                    return;
                a[i] = col;
                t.setLiteralRemap(n, a);
                refreshLater();
            }), r, 2);
            grid2->addWidget(new QLabel(QLatin1String(kAccent[i]), grid2Host), r, 3);
        }
        lay->addWidget(grid2Host);
    }
    lay->addStretch(1);
}

void ThemeEditorDialog::onNewThemeClicked()
{
    auto& tm = ThemeManager::instance();
    // Build 802: a PersistentDialog, so it has the program's own (gradient)
    // title bar in frameless mode, like the editor itself.
    ScopedChildWidget<PersistentDialog> dlgOwner(QStringLiteral("New Theme from Colors"),
                                                 QStringLiteral("ThemeWizardDialogGeometry"), this);
    PersistentDialog& dlg = *dlgOwner.get();
    applyAppTheme(&dlg);
    auto* root = new QVBoxLayout(dlg.bodyWidget());
    root->addWidget(hintLabel(QStringLiteral(
        "Pick four colours; the whole theme is built from Default Dark with them: every "
        "grey-blue and cyan-blue colour (tokens, palette and the colours written in the code) "
        "is placed by its lightness on your scale. Status and band colours stay. For a light "
        "theme pick a light background and dark text. Fine-tune afterwards in Tokens and Palette."),
        &dlg));

    auto colors = std::make_shared<ThemeManager::NewThemeColors>();
    colors->background = tm.color(QStringLiteral("color.background.0"));
    colors->panel = QColor();
    colors->text = tm.color(QStringLiteral("color.text.primary"));
    colors->accent = tm.color(QStringLiteral("color.accent"));

    auto* form = new QFormLayout;
    auto* nameEdit = new QLineEdit(QStringLiteral("My Theme"), &dlg);
    form->addRow(QStringLiteral("Name:"), nameEdit);
    auto* preset = new QComboBox(&dlg);
    preset->addItems({QStringLiteral("Current theme"), QStringLiteral("Navy night"),
                      QStringLiteral("Teal ocean"), QStringLiteral("Graphite"),
                      QStringLiteral("Warm paper (light)"), QStringLiteral("Cool slate (light)")});
    form->addRow(QStringLiteral("Start from:"), preset);
    struct Row { QToolButton* btn; QLabel* hex; };
    auto mkRow = [&dlg](const QString& title) {
        auto* w = new QWidget(&dlg);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto* b = new QToolButton(w);
        b->setIconSize(QSize(40, 18));
        b->setToolTip(title);
        auto* l = new QLabel(w);
        h->addWidget(b);
        h->addWidget(l, 1);
        return std::make_pair(w, Row{b, l});
    };
    auto bg = mkRow(QStringLiteral("Background"));
    auto pn = mkRow(QStringLiteral("Panels"));
    auto tx = mkRow(QStringLiteral("Text"));
    auto ac = mkRow(QStringLiteral("Accent"));
    auto* autoPanel = new QCheckBox(QStringLiteral("Auto"), pn.first);
    autoPanel->setChecked(true);
    pn.first->layout()->addWidget(autoPanel);
    form->addRow(QStringLiteral("Background:"), bg.first);
    form->addRow(QStringLiteral("Panels:"), pn.first);
    form->addRow(QStringLiteral("Text:"), tx.first);
    form->addRow(QStringLiteral("Accent:"), ac.first);
    root->addLayout(form);
    auto* previewCaption = new QLabel(QStringLiteral("<b>Preview</b> — the buttons can be clicked"), &dlg);   // build 803
    root->addWidget(previewCaption);
    auto* preview = new ThemePreviewWidget(&dlg);
    root->addWidget(preview);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    auto* create = buttons->addButton(QStringLiteral("Create"), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto refresh = [colors, autoPanel, bg, pn, tx, ac, preview]() {
        ThemeManager::NewThemeColors c = *colors;
        if (autoPanel->isChecked())
            c.panel = QColor();
        QVector<QColor> n;
        ThemeManager::buildLiteralRamps(c, &n, nullptr);
        const QColor shownPanel = c.panel.isValid() ? c.panel : n.value(4);
        const std::pair<Row, QColor> rows[] = {{bg.second, c.background}, {pn.second, shownPanel},
                                               {tx.second, c.text}, {ac.second, c.accent}};
        for (const auto& [r, col] : rows) {
            r.btn->setIcon(wideSwatch(col));
            r.hex->setText(col.name(QColor::HexRgb));
        }
        pn.second.btn->setEnabled(!autoPanel->isChecked());
        preview->setColors(c);
    };
    QDialog* const dp = &dlg;
    auto hook = [dp, colors, refresh](QToolButton* b, QColor ThemeManager::NewThemeColors::*field,
                                      const QString& title) {
        QObject::connect(b, &QToolButton::clicked, dp, [dp, colors, refresh, field, title] {
            const QColor start = (*colors).*field;
            const QColor c = QColorDialog::getColor(start.isValid() ? start : QColor(0x40, 0x50, 0x60),
                                                    dp, title);
            if (c.isValid()) {
                (*colors).*field = c;
                refresh();
            }
        });
    };
    hook(bg.second.btn, &ThemeManager::NewThemeColors::background, QStringLiteral("Background"));
    hook(pn.second.btn, &ThemeManager::NewThemeColors::panel, QStringLiteral("Panels"));
    hook(tx.second.btn, &ThemeManager::NewThemeColors::text, QStringLiteral("Text"));
    hook(ac.second.btn, &ThemeManager::NewThemeColors::accent, QStringLiteral("Accent"));
    connect(autoPanel, &QCheckBox::toggled, &dlg, [colors, refresh](bool on) {
        if (!on && !colors->panel.isValid()) {
            QVector<QColor> n;
            ThemeManager::buildLiteralRamps(*colors, &n, nullptr);
            colors->panel = n.value(4);
        }
        refresh();
    });
    connect(preset, qOverload<int>(&QComboBox::currentIndexChanged), &dlg,
            [colors, autoPanel, refresh](int i) {
        struct P { const char *b, *p, *t, *a; };
        static const P kP[] = {
            {nullptr, nullptr, nullptr, nullptr},
            {"#1a1b26", "#24283b", "#c0caf5", "#7aa2f7"},   // navy night
            {"#002b36", "#073642", "#eee8d5", "#268bd2"},   // teal ocean
            {"#1c1c1e", "#2a2a2d", "#e2e2e6", "#4ea1ff"},   // graphite
            {"#d8cfbb", "", "#3a2e1f", "#1f5f99"},          // warm paper
            {"#c3ccd6", "", "#1e2a38", "#1565c0"},          // cool slate
        };
        auto& t = ThemeManager::instance();
        if (i <= 0) {
            colors->background = t.color(QStringLiteral("color.background.0"));
            colors->panel = QColor();
            colors->text = t.color(QStringLiteral("color.text.primary"));
            colors->accent = t.color(QStringLiteral("color.accent"));
        } else if (i < int(sizeof(kP) / sizeof(kP[0]))) {
            colors->background = QColor(QLatin1String(kP[i].b));
            colors->panel = QColor(QLatin1String(kP[i].p));
            colors->text = QColor(QLatin1String(kP[i].t));
            colors->accent = QColor(QLatin1String(kP[i].a));
        }
        {
            const QSignalBlocker block(autoPanel);
            autoPanel->setChecked(!colors->panel.isValid());
        }
        refresh();
    });
    connect(create, &QPushButton::clicked, &dlg, &QDialog::accept);
    refresh();

    const QPointer<ThemeEditorDialog> self(this);
    for (;;) {
        if (dlg.exec() != QDialog::Accepted || !self || !dlgOwner)
            return;
        const QString name = nameEdit->text().trimmed();
        QString why;
        if (!ThemeManager::isValidThemeName(name, &why)) {
            showModalMessage(QMessageBox::Warning, &dlg, QStringLiteral("Theme name"), why);
            continue;
        }
        if (tm.isBuiltInTheme(name)) {
            showModalMessage(QMessageBox::Warning, &dlg, QStringLiteral("Reserved name"),
                QStringLiteral("\"%1\" is a built-in theme name.").arg(name));
            continue;
        }
        if (tm.availableThemes().contains(name)) {
            const auto reply = showModalMessage(QMessageBox::Question, &dlg,
                QStringLiteral("Theme exists"),
                QStringLiteral("A theme named \"%1\" already exists. Overwrite it?").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (!self || !dlgOwner)
                return;
            if (reply != QMessageBox::Yes)
                continue;
        }
        ThemeManager::NewThemeColors c = *colors;
        if (autoPanel->isChecked())
            c.panel = QColor();
        if (!tm.createThemeFromColors(name, c, &why)) {
            showModalMessage(QMessageBox::Warning, &dlg, QStringLiteral("New theme"), why);
            continue;
        }
        return;   // the new theme is active; onActiveThemeChanged refreshes the editor
    }
}

QStringList ThemeEditorDialog::visibleTokenKeys() const
{
    QStringList out;
    if (!m_tokenList)
        return out;
    for (int i = 0; i < m_tokenList->topLevelItemCount(); ++i) {
        auto* item = m_tokenList->topLevelItem(i);
        if (!item->isHidden())
            out << item->data(0, Qt::UserRole).toString();
    }
    return out;
}

void ThemeEditorDialog::onAdjustColorsClicked()
{
    if (!ensureEditableTheme())
        return;
    auto& tm = ThemeManager::instance();
    const QStringList visible = visibleTokenKeys();
    const bool filtered = m_tokenList && visible.size() < m_tokenList->topLevelItemCount();

    ScopedChildWidget<PersistentDialog> dlgOwner(QStringLiteral("Adjust Colors"),
                                                 QStringLiteral("ThemeAdjustDialogGeometry"), this);   // build 802
    PersistentDialog& dlg = *dlgOwner.get();
    applyAppTheme(&dlg);
    dlg.setMinimumWidth(420);
    auto* root = new QVBoxLayout(dlg.bodyWidget());
    root->addWidget(hintLabel(QStringLiteral(
        "Shifts the colors of the theme; the program shows the result as you move the "
        "sliders. OK keeps it, Cancel puts every color back."), &dlg));
    auto* grid = new QGridLayout;
    QDialog* const dp = &dlg;
    auto mkSlider = [dp, grid](int row, const QString& name, int lo, int hi, const QString& unit) {
        auto* l = new QLabel(name, dp);
        auto* sl = new QSlider(Qt::Horizontal, dp);
        sl->setRange(lo, hi);
        sl->setValue(0);
        auto* v = new QLabel(QStringLiteral("0%1").arg(unit), dp);
        v->setMinimumWidth(48);
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QObject::connect(sl, &QSlider::valueChanged, v, [v, unit](int x) {
            v->setText(QStringLiteral("%1%2").arg(x > 0 ? QStringLiteral("+%1").arg(x) : QString::number(x), unit));
        });
        grid->addWidget(l, row, 0);
        grid->addWidget(sl, row, 1);
        grid->addWidget(v, row, 2);
        return sl;
    };
    QSlider* hue = mkSlider(0, QStringLiteral("Hue"), -180, 180, QStringLiteral("°"));
    QSlider* sat = mkSlider(1, QStringLiteral("Saturation"), -100, 100, QStringLiteral("%"));
    QSlider* lig = mkSlider(2, QStringLiteral("Lightness"), -100, 100, QStringLiteral("%"));
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    auto* form = new QFormLayout;
    auto* target = new QComboBox(&dlg);
    target->addItem(QStringLiteral("Whole theme (tokens, palette, code colors)"));
    if (filtered)
        target->addItem(QStringLiteral("Only the %1 tokens shown in the list").arg(visible.size()));
    form->addRow(QStringLiteral("Apply to:"), target);
    auto* family = new QCheckBox(QStringLiteral("Keep status, band and special colors (red, yellow, green …)"), &dlg);
    family->setChecked(true);
    form->addRow(QString(), family);
    root->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto* resetBtn = buttons->addButton(QStringLiteral("Reset"), QDialogButtonBox::ResetRole);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(resetBtn, &QPushButton::clicked, &dlg, [hue, sat, lig] {
        hue->setValue(0);
        sat->setValue(0);
        lig->setValue(0);
    });

    // Live preview, coalesced: a slider drag fires many valueChanged.
    auto* debounce = new QTimer(&dlg);
    debounce->setSingleShot(true);
    debounce->setInterval(120);
    const auto applyPreview = [hue, sat, lig, target, family, visible] {
        const QStringList only = target->currentIndex() == 1 ? visible : QStringList();
        ThemeManager::instance().previewColorAdjust(hue->value(), sat->value(), lig->value(),
                                                    only, family->isChecked());
    };
    QObject::connect(debounce, &QTimer::timeout, &dlg, applyPreview);
    auto kick = [debounce] { debounce->start(); };
    QObject::connect(hue, &QSlider::valueChanged, &dlg, kick);
    QObject::connect(sat, &QSlider::valueChanged, &dlg, kick);
    QObject::connect(lig, &QSlider::valueChanged, &dlg, kick);
    QObject::connect(target, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, kick);
    QObject::connect(family, &QCheckBox::toggled, &dlg, kick);

    tm.beginColorAdjust();
    const QPointer<ThemeEditorDialog> self(this);
    const int rc = dlg.exec();
    if (dlgOwner && debounce->isActive()) {
        debounce->stop();
        if (rc == QDialog::Accepted)
            applyPreview();   // the last slider move not previewed yet
    }
    ThemeManager::instance().endColorAdjust(rc == QDialog::Accepted && dlgOwner);
    if (!self)
        return;
    refreshTokenList();
    filterTokensTo(m_activeSubset);
    rebuildPalettePage();
}

// ── ANAN fork build 798: changed-vs filter, undo / redo, contrast check ────

void ThemeEditorDialog::reloadCompareThemes()
{
    if (!m_compareCombo)
        return;
    auto& tm = ThemeManager::instance();
    QString want = m_compareCombo->currentText();
    if (want.isEmpty())
        want = AppSettings::instance().value(QStringLiteral("ThemeEditorCompareWith"),
                                             QStringLiteral("Default Dark")).toString();
    const QStringList themes = tm.availableThemes();
    {
        const QSignalBlocker block(m_compareCombo);
        m_compareCombo->clear();
        m_compareCombo->addItems(themes);
        int idx = m_compareCombo->findText(want);
        if (idx < 0)
            idx = m_compareCombo->findText(QStringLiteral("Default Dark"));
        m_compareCombo->setCurrentIndex(std::max(0, idx));
    }
    m_compareTokens = tm.rootTokensOfTheme(m_compareCombo->currentText());
    filterTokensTo(m_activeSubset);
}

bool ThemeEditorDialog::tokenDiffers(const QString& key) const
{
    const auto it = m_compareTokens.constFind(key);
    if (it == m_compareTokens.constEnd())
        return false;   // not in that theme's file: both run on the built-in default
    auto& tm = ThemeManager::instance();
    const QString path = m_activeContainerPath;
    const QVariant& base = it.value();
    if (base.userType() == qMetaTypeId<ThemeGradient>()) {
        const ThemeGradient a = base.value<ThemeGradient>();
        const ThemeGradient b = tm.gradientAt(path, key);
        if (a.stops.size() != b.stops.size() || a.type != b.type || !qFuzzyCompare(a.angle + 1, b.angle + 1))
            return true;
        for (int i = 0; i < a.stops.size(); ++i) {
            if (a.stops[i].color.rgba() != b.stops[i].color.rgba()
                || std::abs(a.stops[i].at - b.stops[i].at) > 1e-4)
                return true;
        }
        return false;
    }
    if (base.userType() == qMetaTypeId<ThemeFont>()) {
        const ThemeFont a = base.value<ThemeFont>();
        const ThemeFont b = tm.fontTokenAt(path, key);
        return a.family != b.family || a.size != b.size || a.color.rgba() != b.color.rgba();
    }
    if (base.userType() == QMetaType::QString) {
        const QString v = base.toString();
        if (v.startsWith(QLatin1Char('#'))) {
            const QColor c(v);
            return c.isValid() && c.rgba() != tm.colorAt(path, key).rgba();
        }
        return v != tm.valueAt(path, key);
    }
    if (base.canConvert<double>())
        return int(std::lround(base.toDouble())) != tm.sizingAt(path, key);
    return false;
}

void ThemeEditorDialog::updateUndoButtons()
{
    auto& tm = ThemeManager::instance();
    if (m_undoBtn)
        m_undoBtn->setEnabled(tm.canUndo());
    if (m_redoBtn)
        m_redoBtn->setEnabled(tm.canRedo());
}

void ThemeEditorDialog::doUndo(bool redo)
{
    auto& tm = ThemeManager::instance();
    if (!(redo ? tm.redo() : tm.undo()))
        return;
    refreshTokenList();
    filterTokensTo(m_activeSubset);
    rebuildPalettePage();
    updateUndoButtons();
    updateInspectorStatus(redo ? QStringLiteral("Redone.") : QStringLiteral("Undone."));
}

namespace {

// WCAG relative luminance and contrast ratio.
double relLuminance(const QColor& c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double la = relLuminance(a), lb = relLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// A translucent colour as it shows over `under`.
QColor over(const QColor& c, const QColor& under)
{
    const double a = c.alphaF();
    return QColor::fromRgbF(float(c.redF() * a + under.redF() * (1 - a)),
                            float(c.greenF() * a + under.greenF() * (1 - a)),
                            float(c.blueF() * a + under.blueF() * (1 - a)));
}

} // namespace

void ThemeEditorDialog::onCheckContrastClicked()
{
    auto& tm = ThemeManager::instance();
    const QString path = m_activeContainerPath;
    const QColor ground = tm.colorAt(path, QStringLiteral("color.background.0"));
    // The flat colour of a token: a gradient by the average of its stops,
    // a translucent one over the ground.
    auto flat = [&tm, &path, ground](const QString& key) -> QColor {
        const ThemeGradient g = tm.gradientAt(path, key);
        QColor c;
        if (!g.stops.isEmpty()) {
            double r = 0, gg = 0, b = 0, a = 0;
            for (const auto& st : g.stops) {
                r += st.color.redF(); gg += st.color.greenF(); b += st.color.blueF(); a += st.color.alphaF();
            }
            const double n = g.stops.size();
            c = QColor::fromRgbF(float(r / n), float(gg / n), float(b / n), float(a / n));
        } else {
            c = tm.colorAt(path, key);
        }
        if (c.isValid() && c.alpha() < 255 && ground.isValid())
            c = over(c, ground);
        return c;
    };
    const QStringList all = tm.allTokenKeys();
    const QSet<QString> keys(all.begin(), all.end());
    struct Pair { QString text, bg; QColor tc, bc; double ratio; double need; };
    QVector<Pair> pairs;
    auto add = [&pairs, &flat](const QString& t, const QString& b, double need) {
        const QColor tc = flat(t), bc = flat(b);
        if (!tc.isValid() || !bc.isValid() || tc.alpha() == 0 || bc.alpha() == 0)
            return;
        pairs.append({t, b, tc, bc, contrastRatio(tc, bc), need});
    };
    // 1) <prefix>.text against <prefix>.background (one state level up as a
    //    fallback: ".hover.text" against ".background").
    for (const QString& k : all) {
        if (!k.startsWith(QStringLiteral("color.")) || !k.endsWith(QStringLiteral(".text"))
            || k.contains(QStringLiteral(".disabled")))
            continue;
        QString prefix = k.left(k.size() - 4);   // keeps the trailing '.'
        QString bg = prefix + QStringLiteral("background");
        if (!keys.contains(bg)) {
            const QString up = prefix.left(prefix.size() - 1);
            const qsizetype dot = up.lastIndexOf(QLatin1Char('.'));
            if (dot > 6)
                bg = up.left(dot + 1) + QStringLiteral("background");
        }
        if (keys.contains(bg))
            add(k, bg, 4.5);
    }
    // 2) The main text colours on the grounds.
    const QStringList texts = {QStringLiteral("color.text.primary"), QStringLiteral("color.text.secondary"),
                               QStringLiteral("color.text.label")};
    const QStringList grounds = {QStringLiteral("color.background.0"), QStringLiteral("color.background.1"),
                                 QStringLiteral("color.background.2")};
    for (const QString& t : texts) {
        if (!keys.contains(t))
            continue;
        for (const QString& g : grounds) {
            if (keys.contains(g))
                add(t, g, t.endsWith(QStringLiteral("primary")) ? 4.5 : 3.0);
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.ratio < b.ratio; });
    // 3) The colours written in the code (literal remap).
    struct LitPair { QString name; QColor tc, bc; double ratio; double need; };
    QVector<LitPair> lits;
    const QVector<QColor> n = tm.literalNeutral();
    if (n.size() == 12) {
        lits.append({QStringLiteral("code text (#c8d8e8) on code ground (#0f0f1a)"), n[9], n[2], contrastRatio(n[9], n[2]), 4.5});
        lits.append({QStringLiteral("code text (#c8d8e8) on code panels (#1a2a3a)"), n[9], n[4], contrastRatio(n[9], n[4]), 4.5});
        lits.append({QStringLiteral("code labels (#8ea8c0) on code panels (#1a2a3a)"), n[8], n[4], contrastRatio(n[8], n[4]), 3.0});
        lits.append({QStringLiteral("code white (#ffffff) on code borders (#304050)"), n[11], n[5], contrastRatio(n[11], n[5]), 3.0});
    }

    ScopedChildWidget<PersistentDialog> dlgOwner(QStringLiteral("Contrast Check — %1").arg(tm.activeTheme()),
                                                 QStringLiteral("ThemeContrastDialogGeometry"), this);   // build 802
    PersistentDialog& dlg = *dlgOwner.get();
    applyAppTheme(&dlg);
    dlg.resize(720, 520);
    auto* root = new QVBoxLayout(dlg.bodyWidget());
    root->addWidget(hintLabel(QStringLiteral(
        "Text / background pairs of this theme with their contrast ratio (WCAG). Main text needs "
        "4.5 : 1, secondary text and labels 3 : 1. Double-click a row to show its two tokens in "
        "the list. Disabled states are not checked."), &dlg));
    auto* onlyBad = new QCheckBox(QStringLiteral("Only problems"), &dlg);
    onlyBad->setChecked(true);
    root->addWidget(onlyBad);
    auto* tree = new QTreeWidget(&dlg);
    tree->setColumnCount(4);
    tree->setHeaderLabels({QStringLiteral("Text"), QStringLiteral("Background"), QStringLiteral("Ratio"),
                           QString()});
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    root->addWidget(tree, 1);
    auto* summary = new QLabel(&dlg);
    root->addWidget(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto verdict = [](double ratio, double need) {
        if (ratio >= need) return QStringLiteral("✓ ok");
        if (ratio >= 3.0) return QStringLiteral("! low");
        return QStringLiteral("✗ poor");
    };
    int bad = 0;
    for (const Pair& p : pairs) {
        auto* it = new QTreeWidgetItem(tree);
        it->setIcon(0, swatchIcon(p.tc));
        it->setText(0, p.text);
        it->setIcon(1, swatchIcon(p.bc));
        it->setText(1, p.bg);
        it->setText(2, QStringLiteral("%1 : 1").arg(p.ratio, 0, 'f', 2));
        it->setText(3, verdict(p.ratio, p.need));
        it->setData(0, Qt::UserRole, QStringList{p.text, p.bg});
        it->setData(3, Qt::UserRole, p.ratio < p.need);
        if (p.ratio < p.need)
            ++bad;
    }
    for (const LitPair& p : lits) {
        auto* it = new QTreeWidgetItem(tree);
        it->setIcon(0, swatchIcon(p.tc));
        it->setText(0, p.name);
        it->setIcon(1, swatchIcon(p.bc));
        it->setText(1, QStringLiteral("Palette tab > Colours written in the code"));
        it->setText(2, QStringLiteral("%1 : 1").arg(p.ratio, 0, 'f', 2));
        it->setText(3, verdict(p.ratio, p.need));
        it->setData(3, Qt::UserRole, p.ratio < p.need);
        if (p.ratio < p.need)
            ++bad;
    }
    summary->setText(QStringLiteral("%1 pairs checked, %2 below the target.")
                         .arg(pairs.size() + lits.size()).arg(bad));
    tree->resizeColumnToContents(0);
    tree->resizeColumnToContents(1);
    auto applyFilter = [tree, onlyBad] {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* it = tree->topLevelItem(i);
            it->setHidden(onlyBad->isChecked() && !it->data(3, Qt::UserRole).toBool());
        }
    };
    QObject::connect(onlyBad, &QCheckBox::toggled, &dlg, applyFilter);
    applyFilter();

    QStringList chosen;
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dlg, [&chosen, &dlg](QTreeWidgetItem* it, int) {
        const QStringList pair = it->data(0, Qt::UserRole).toStringList();
        if (pair.isEmpty())
            return;
        chosen = pair;
        dlg.accept();
    });
    const QPointer<ThemeEditorDialog> self(this);
    dlg.exec();
    if (!self || chosen.isEmpty())
        return;
    if (m_tabs)
        m_tabs->setCurrentIndex(0);
    m_activeSubset = chosen;
    filterTokensTo(chosen);
    updateInspectorStatus(QStringLiteral("%1 on %2 (right-click > Show all tokens).").arg(chosen.value(0), chosen.value(1)));
}

} // namespace AetherSDR
