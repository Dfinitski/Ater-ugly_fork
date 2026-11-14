# AetherSDR → ANAN-100D (openHPSDR Protocol 2): журнал переделки

База: AetherSDR v26.10.4 (тег `v26.10.4`, коммит `9efd0fb`).
Цель: одно целевое устройство — Apache Labs ANAN-100D (плата ANGELIA,
board type 3 в P2 discovery) по openHPSDR Protocol 2.
Эталон поведения GUI — FlexRadio, пример клиентской DSP/TX-цепочки — Hermes-Lite 2.

Журнал фиксирует: что удалено, что временно отключено, к чему вернуться,
чтобы не потерять функционал.

---

## 0. Ключевые находки до начала работ

| Что | Состояние в v26.10.4 |
|---|---|
| `backends/anan/` | **Уже есть** P2-бэкенд ANAN: `P2Client`, `P2Protocol`, `AnanDiscovery`, `AnanRxDsp` (WDSP), droop-калибровка, страница «ANAN Front End». Документ: `docs/architecture/anan-p2-backend-design.md`. |
| Ограничение 1 | Discovery принимает только board type 10 (SATURN / ANAN-G2): `AnanDiscovery.cpp:332` (`isSaturn()`). ANAN-100D = type 3 (ANGELIA). |
| Ограничение 2 | **Только приём**: `canTransmit = false` (`AnanBackend.cpp:510`), `setKeying()` — заглушка. DUC, PTT, TX I/Q, микрофон по P2 не реализованы. |
| Ограничение 3 | Alex-фильтры, тактовая частота, число АЦП заточены под Saturn. |
| `backends/hl2/` | openHPSDR **Protocol 1** (Metis), с TX. Источник примера клиентской TX-цепочки (WDSP TX, ALC, PTT). |

Вывод: задача — **расширить `AnanBackend` на ANGELIA и добавить TX**, а не писать бэкенд с нуля.

---

## 1. Общие изменения сборки

| Изменение | Где | Как вернуть |
|---|---|---|
| Тесты отключены опцией `AETHER_BUILD_TESTS` (по умолчанию OFF) | `CMakeLists.txt`, вокруг `include(tests/tests.cmake)` | `tests/tests.cmake` (10,7 тыс. строк) ещё регистрирует цели удалённых Icom/RTL/Kiwi. Перед включением: вычистить эти цели и `tests/*icom*`, `tests/*rtl*`, `tests/*kiwi*`; проверить общие тесты (`backend_family_switch_test`, `backend_receive_contract_test`, `connection_panel_*` и т. п.) — в них есть ветки по удалённым семействам. |

---

## 2. Icom (CI-V / RS-BA1) — удалён

**Удалено полностью:** `src/core/backends/icom/` (29 файлов, ~19 тыс. строк) и его исходники в `CMakeLists.txt`.

**Изменено:**

| Файл | Что сделано |
|---|---|
| `src/models/RadioModel.cpp` | Убраны include, ветка `icom` в фабрике `makeBackend()` и параметры подключения `icom.*` в `populateFamilyParams()`. |
| `src/gui/ConnectionPanel.cpp/.h` | Убраны поля Icom на странице «Connect by IP» (пользователь, пароль, порты NAT, выбор модели CI-V), флажок «Wake Icom on connect», хранение пароля в keychain, ветка Icom в `probeRadio()`, параметр `icomBasePort` у `saveManualProfile()`, `kFamilyIcom`. |
| `src/gui/ExperimentalRadioSupport.h` | Убрана запись Icom; `experimentalRadioIdentityPending()` всегда `false` (ожидание идентификации было нужно только Icom). |
| `src/gui/MainWindow_Session.cpp` | Ключ идентичности эксперимент-уведомления больше не включает модель (было только для Icom). |
| `src/core/AutomationServer.cpp` | `connect ip <host> [flex\|hl2\|anan]` — вместо `icom` теперь принимается `anan`. |
| `src/core/LogManager.cpp` | Убраны категории логов `aether.icom.*`. |

**Вторая волна (сделано): «спящие» ветки Icom убраны**

| Где | Что сделано |
|---|---|
| `src/core/TciServer.*`, `src/core/IcomTciUnkeySettle.h` (удалён) | Окно «settle» при отпускании PTT через CI-V убрано; каждая ветка заменена её веткой для не-Icom радио. Поля телеметрии `unkeySettling` всегда `false` (формат ответа `tci routes` сохранён). |
| `src/models/RadioModel.*` | Убраны `wakeIcomRadio()` и обработка `power.wakeNeeded`. Общая машинка «radio wake» (`m_radioWakeActive`, сигналы `radioWakeProgress/Failed`) осталась, но её больше никто не запускает. |
| `src/core/AutomationServer.*` | Убраны команды `civ ...` и `controls ...`; `radiocert persist` больше не собирает CI-V диагностику. |
| `src/gui/Ax25HfPacketDecodeDialog.*`, `src/core/tnc/Ax25AudioCapture.*` | Убран Icom-захват TX-аудио после ресемплинга (`TxIcomPostResample`). |

**Отключено (код на месте) — вернуться при необходимости:**

| Где | Что | Для ANAN |
|---|---|---|
| `src/gui/MemoryDialog.cpp` | `usesNativeMemorySchema = false`: табличная схема памятей, импортированных из радио | Не нужно (у ANAN нет памятей на стороне радио). |
| `src/gui/SpectrumOverlayMenu.cpp` | Ряд «Scope: Center / Scroll-F / Fixed» скрыт (`setVisible(false)`) | Не нужно. |

**Оставлено:**

| Где | Почему |
|---|---|
| `RadioModel::setPcAudioEnabled/notePcAudioEnabled` | Общая кнопка «PC Audio» в заголовке окна; пересылает в Icom-расширение, для остальных радио — пустая операция. **Для ANAN:** можно связать с выводом звука на динамик радио (`anan.speakerAudioEnabled`). |
| `SettingsCredentialPolicy.h` (`Icom/Password`) | Защита: не даёт старому паролю Icom попасть в экспорт настроек. |
| HID: `IcomRC28Parser`, меню «Icom RC-28 Remote Encoder» | **Не Icom-радио**, а USB-энкодер RC-28 — работает с любым радио. |
| Генератор TX-аудио: `pttConfirmMs`, `kIcomPttConfirmTimeoutMs` в AX.25 | Привязаны к возможности `hasRadioPttReadback`, а не к семейству Icom. |

---

## 3. RTL-SDR — удалён

**Удалено полностью:**
`src/core/backends/rtl/` (23 файла), `src/core/RtlSdrDiscovery.*`, `src/core/RtlSliceSettings.*`,
`src/core/RtlDeviceSettings.*`, `src/gui/RtlReceiverSettingsWidget.*`, `cmake/AetherRequireRtl.cmake`;
в `CMakeLists.txt` — опции `ENABLE_RTL`/`REQUIRE_RTL`, поиск librtlsdr/fftw3f для RTL,
копирование `rtlsdr.dll`/`libusb-1.0.dll`, udev-правило.

**Изменено:**

| Файл | Что сделано |
|---|---|
| `src/models/RadioModel.cpp` | Ветка `rtl` в фабрике. |
| `src/gui/ConnectionPanel.*` | Пункт «RTL-SDR (USB)», `kFamilyRtl`. |
| `src/gui/RadioSetupDialog.*` | Страница «RTL Receiver» и её индекс. |
| `src/gui/MainWindow.h`, `MainWindow_Session.cpp` | USB-обнаружение `m_rtlDiscovery`. |
| `src/core/backends/LocalRadioDiscoverySource.cpp`, `ModelRadioConnectionTarget.cpp` | Источник обнаружения `rtl`/`usb` (aetherd). |

**Вторая волна (сделано):** убраны `rtlMigrationSource()`/`storeRtlRfGainPreservingLegacy()` (`RadioStateMemory`), анонимная область настроек для RTL (`RadioModel::settingsScope`), принудительное отключение RTL при смене радио (`RadioModel.cpp`), ключ настроек по серийному номеру USB (`RadioSettingsIdentity.h`), флаг `ClientSettingsDomain::RtlSlices` (бит 8 оставлен свободным).

**Оставлено:**

| Где | Комментарий |
|---|---|
| `CMakeLists.txt`, блок NR4/libspecbleach: `RTL_FFTW3F_TARGET` | Переменная больше не задаётся → срабатывает обычный `find_library(fftw3f)`. На Windows NR4 использует свой блок `FFTW3F_DLL`, не затронут. Можно упростить. |

**Что ценного было в RTL и может пригодиться ANAN:**
- `RtlReceiverRegistry`/`RtlReceivePipeline` — несколько независимых приёмников (слайсов) из одного I/Q-потока на стороне клиента. У ANAN-100D 4+ DDC в железе, поэтому многослайсовость лучше делать аппаратно (по DDC на слайс), но подход к реестру приёмников можно подсмотреть в git-истории (`git show v26.10.4:src/core/backends/rtl/RtlReceiverRegistry.cpp`).
- Страница калибровки ppm — у ANAN есть свой путь калибровки (`hostFrequencyCalibration`, см. HL2).

---

## 4. KiwiSDR — этап K1 выполнен (функция удалена), этап K2 запланирован

Kiwi был вплетён в ядро (~2,7 тыс. упоминаний: спектр, звуковой тракт, синхронизация
приёма). Поэтому удаление разбито на два этапа.

### K1 (сделано): Kiwi больше нельзя создать, подключить или увидеть

**Удалено полностью:**
`src/core/KiwiSdrClient.*`, `KiwiSdrManager.*`, `KiwiSdrCredentialStore.*`,
`KiwiSdrRedirectPolicy.*`, `KiwiPublicDirectory.*`, `KiwiSdrTxMutePolicy.h`,
`src/gui/KiwiSdrApplet.*`, `KiwiPublicReceiverPicker.*`, `KiwiRebindTracker.h`,
`src/gui/MainWindow_KiwiSdr.cpp` (2434 строки, ~40 методов главного окна).

**Изменено:**

| Файл | Что сделано |
|---|---|
| `src/gui/MainWindow_RxPan.cpp` (**новый**) | Три общих метода, жившие в `MainWindow_KiwiSdr.cpp`: `flexRxPanSourceSlice()`, `syncFlexRxPanToAudioEngine()`, `applyPanBandwidthLimitsToWidget()` — без Kiwi-веток. |
| `MainWindow.h/.cpp` | Убраны менеджер Kiwi, его создание/уничтожение, `wireKiwiSdr()`, все Kiwi-методы и поля (виртуальные антенны, band-recall rebind, TX-приглушение, источники отображения панорам), восстановление mute при выходе. |
| `MainWindow_Wiring.cpp` | Убраны: Kiwi-водопад (обновление вида/жесты), выбор источника отображения панорамы, Kiwi-линия SQL, Auto-SQL для Kiwi, ручки WF Ceiling/Floor/Rate в Kiwi-режиме, подписки слайса на Kiwi-трекинг, band-recall rebind, ветки `kiwiSdrPanDisplaysKiwi()` в центровке/зуме/клонировании настроек панорам. |
| `MainWindow_ReceiveSync.cpp` | Подбор пары «Flex-слайс + Kiwi-профиль» заменён заглушкой «пары нет». |
| `MainWindow_Session.cpp`, `_Shortcuts.cpp`, `_DigitalModes.cpp` | Убраны вызовы Kiwi (band-recall, rebind, TX-приглушение при RADE, обработчики автоматизации). |
| `MainWindow_Menus.cpp` | Убран пункт «Tools → Configure KiwiSDR...»; меню «Settings → Receive Sync» **скрыто**. |
| `VfoWidget`, `RxApplet`, `SpectrumOverlayMenu` | Убраны «виртуальные антенны» Kiwi в выборе RX-антенны и сигнал `kiwiRxAntennaSelected`. |
| `RadioSetupDialog` | Убрана группа «KiwiSDR RX Antennas» (профили, пароли, импорт/экспорт CSV, каталог публичных приёмников); параметр конструктора `KiwiSdrManager*`. |
| `AppletPanel` | Убран апплет «KiwiSDR» (KSDR). |
| `AutomationServer` | Убраны команды `get kiwi` и `slice rxsource`. |

**Оставлено намеренно — полезно для любого радио:**
- **Kiwi DX споты** (`BandPlanManager::loadKiwiDxSpots`, настройка «Kiwi DX» в Spot Settings / DX Cluster, клик по споту): это встроенная статическая база маяков, утилитарных станций и сигналов времени из сообщества KiwiSDR, а не подключение к приёмнику.

### K2 (запланировано после того, как ANAN-100D заработает на приём)

Остался «спящий» код: он компилируется, но никогда не активируется — его некому включить.

| Где | Что | Риск при удалении |
|---|---|---|
| `src/gui/SpectrumWidget.*` (~750 упоминаний) | Второй поток водопада (`kiwiStream`), Kiwi FFT-трасса, Kiwi SQL-линия, кнопка источника отображения | Высокий: пронизывает рендер (CPU и GPU). Нужна проверка на живом радио. |
| `src/core/AudioEngine.*` (~700) | Микшер внешних RX-источников, отдельная DSP-цепочка (NR2/NR4/MNR/RN2/DFNR/NNR/BNR) для внешнего аудио, джиттер-буфер | Высокий: основной звуковой тракт. **Можно переиспользовать** для второго приёмника ANAN, если понадобится отдельная обработка. |
| `src/core/ReceivePresentationSync.*`, `MainWindow_ReceiveSync.cpp` | Выравнивание задержки звук/картинка Flex↔Kiwi (кросс-корреляция) | Средний. |
| `src/models/SliceModel.*` | «Внешний приёмник вместо радио» (`externalReceive*`: AGC, SQL, громкость) | Средний. |
| `SMeterWidget`, `VfoWidget`, `RxApplet`, `MainWindow_Controllers` | Показания S-метра без калибровки, шкалы SQL/AGC Kiwi | Низкий. |
| `src/core/KiwiSdrProtocol.*`, `src/gui/KiwiSdrTraceMath.h` | Чистые функции и типы (без сети), нужны «спящему» коду выше | Удаляются последними. |
| `SpectrumOverlayMenu` | Режим ручек водопада в dBm для Kiwi | Низкий. |
| `PanRecenterPolicy.h`, `PanSpanControlGate.h` и др. | Параметр `kiwiDisplayActive` (всегда `false`) | Низкий. |
| `LogManager` | Категории `aether.kiwisdr*` | Низкий. |

---

## 6. ANAN-100D (ANGELIA): приём — этап R1

Было: бэкенд `anan` умел только ANAN-G2 (плата SATURN, board type 10) и
отбрасывал все остальные ответы discovery.

### Что сделано

| Где | Изменение |
|---|---|
| `P2Protocol.h/.cpp` | `BoardProfile` и `boardProfileFor(boardId)`: ANGELIA (3) → «ANAN-100D», ORION (4) → «ANAN-200D», SATURN (10) → «ANAN-G2». В профиле: первый приёмный DDC (`rxDdcBase` = 2 для 100D/200D, 0 для G2), схема Alex, маска Alex enable (байт 59 General), есть ли mute усилителя динамика и банк фильтров ADC1. |
| `P2Protocol` | `buildGeneral(mask)`, `buildDdcSpecific(..., hwDdcBase)`, фазовые слова High Priority по `9 + 4·(base+n)`. Новая схема `LegacyHighPassLowPass`: Alex0 = ANT1 + HPF (1,5/6,5/9,5/13/20 МГц, 6m-preamp, bypass < 1,8 МГц) + TX LPF по частоте приёма; Alex1 = ANT1 + LPF. Ветка Saturn байт-в-байт прежняя. |
| `P2Client` | После Discovery ждёт ответ радио (до 300 мс); по board id выбирает профиль и только тогда шлёт General, DDC-Specific и High Priority run=1. Нет ответа — подсказка `anan.boardId` или профиль ANAN-100D. IQ ждёт с порта `1035 + rxDdcBase` (у 100D это 1037). Если радио объявляет меньше DDC, чем нужно, число приёмников урезается. Пакет остановки — в раскладке платы. Новый сигнал `boardConfigured(boardId, fromReply)`. |
| `AnanDiscovery`, `ConnectionPanel::probeAnan` | Принимают ANGELIA/ORION/SATURN (`isSupportedAnan()`), имя модели из профиля. Сообщения «ANAN-G2» → «ANAN». В ручном подключении пункт «ANAN (100D / G2)». |
| `AnanBackend` | `capabilities().model` по board id из Discovery (по умолчанию «ANAN-100D»); параметр подключения `anan.boardId` (необязательный). |
| `ExperimentalRadioSupport.h` | Название в уведомлении: «ANAN-100D». |
| `tests/anan_angelia_protocol_test.cpp` | Новый тест без Qt (регистрируется в `tests.cmake`, собирается при `AETHER_BUILD_TESTS=ON`). |

### Найдено при испытаниях на радио (28.10.2026)

| Симптом | Причина | Исправление |
|---|---|---|
| Хаотичные пропадания звука, 1–2 разрыва номеров пакетов за 5 с (~14 пакетов I/Q и ~1,4% пакетов микрофона) | Фоновые поиски HL2 (P1) и ANAN (P2) каждые 5 с слали широковещательный Discovery на порт 1024 и во время сеанса; ANAN-100D, отвечая, теряет пачку пакетов. Буфер сокета 4 МБ ничего не менял — потеря выше по цепочке. | `Hl2Discovery/AnanDiscovery::setSweepPaused()`, пауза на время подключения (`MainWindow_Session`). После: 48000 отсчётов/с, 0 разрывов. Буфер сокета — значение ОС. |
| Светлые полосы по краям панорамы, тёмный водопад | Таблица поправки спада у краёв (droop) рассчитана под FIR прошивки G2 (до +54 дБ у краёв) | Для плат ≠ SATURN заводская таблица не применяется, только собственная калибровка радио |
| Подключение ~12 с | Построение канала WDSP (FFTW) при первом подключении, не сеть | Не исправлялось (так же у G2) |
| — | Диагностика `ANAN diag:` в журнале каждые 5 с (частота выборки, разрывы, mic/status, частота) | Оставлена |
| — | Автоуровень панорамы (Floor) включён по умолчанию | `SpectrumWidget`, `MainWindow_Wiring` |

Замечание: ANAN-100D не прислал ни одного пакета High Priority Status (порт 1025) — 0 за сеанс.
Для TX (PTT, ALC, мощность, перегрузка АЦП) нужно выяснить, при каких условиях прошивка их шлёт.

### Сознательно не сделано (вернуться)

| Что | Почему / когда |
|---|---|
| DDC0/DDC1 на ANGELIA (PureSignal feedback, diversity) | Сейчас не включаются. Нужны для PureSignal (этап TX) и Diversity. |
| Alex-аттенюаторы 10/20 дБ, выбор ANT2/ANT3, RX1-In/RX2-In/XVTR, Ext1/Ext2 | Всегда ANT1, аттенюаторы Alex выключены. Только step-аттенюатор АЦП (0–31 дБ). |
| Подмена DDC0 для Brick3/«p2_angelia_ddc0_map» в Thetis | Не нужна для обычного приёма. |
| Приём на RX2 (DDC3) | Протокол готов (`activeDdcs` из 2 элементов), но бэкенд пока использует один приёмник (`maxSlices = 1`, как у G2). |
| Настройки «ANAN-G2» в `AnanSettings`, `AnanDroopDefaults` (поправка АЧХ CIC под гейтвер G2) | Для 100D данные дроопа другие: при «завале» краёв панорамы запустить встроенный droop sweep. |
| TX (DUC, PTT, I/Q на передачу, микрофон, PA) | Этап T1, после проверки приёма. |

---

## 7. Переход на Hl2Backend поверх Protocol 2 (этап R2)

Решение (28.10.2026): бэкенд `AnanBackend` автора доведён только до минимального приёма
(1 приёмник, режимы/фильтры не объявлены, нет squelch/notch/APF, нет настройки внутри
панорамы, нет TX). `Hl2Backend` реализует всё это поверх транспорта `MetisClient`
(Protocol 1). Вместо наращивания `AnanBackend` транспорт заменён один к одному.

| Где | Изменение |
|---|---|
| `hl2/MetisClient.h` | Методы транспорта (`start/stop`, `setRxFrequencyHz`, `setSampleRate`, `setReceiverCount`, `setLnaGainDb`, `setBandFilter`, …, `setMox`, `queueTxIq`, `discover`) сделаны `virtual`. Поведение HL2 не меняется. |
| `anan/P2Transport.h/.cpp` (новый) | Наследник `MetisClient` на Protocol 2: Discovery → профиль платы → General / DDC-Specific / High Priority; приёмник i = DDC (база + i); I/Q выравнивается по приёмникам и отдаётся сигналом `iqBlocksReady`, как EP6; разрывы — по номерам пакетов DDC; Alex HPF/LPF всегда по частоте RX1 (как Thetis); `setLnaGainDb` → аттенюатор `clamp(20 − gain, 0, 31)`; watchdog тишины 2 с; диагностика `ANAN diag` раз в 5 с. |
| `hl2/Hl2Backend.h/.cpp` | `TransportKind {Metis, AnanP2}`; для ANAN: `P2Transport`, семейство `anan`, производитель Apache Labs, модель по board id, диапазон 10 кГц – 55 МГц, передача запрещена (`canTransmit = false`) до этапа T1. |
| `models/RadioModel.cpp` | Семейство `anan` → `Hl2Backend(AnanP2)`. Запасной путь: переменная окружения `AETHER_ANAN_LEGACY=1` включает старый `AnanBackend`. |

### Что пока заглушено в P2Transport (вернуться)

| Что | Состояние |
|---|---|
| Передача: DUC I/Q (P2 — 192 кГц, у HL2 48 кГц → нужен апсемплинг ×4), PTT, мощность | `setMox` игнорируется с предупреждением, I/Q передачи отбрасывается |
| Пакеты состояния High Priority (PTT, перегрузка АЦП, мощность, напряжение) | ANAN-100D их не шлёт; возможно, идут на порт ПК 1025 — проверить |
| Звук на динамик радио (порт 1028) | заглушка (у HL2 это локальный кодек) |
| HL2-специфика: регистры I2C, плата IO, CL1, bandscope EP4 (автоусиление по нему) | отказ/заглушка |
| Широкополосный вид (wideband, порт 1027) | не реализован |
| Калибровка S-метра в dBm | использует опорные значения HL2 (`Hl2DbReference`) — нужна поправка для ANAN |
| Страницы настроек HL2 (плата фильтров, кодек, CL1…) | видны, но к ANAN не относятся |
| Второй АЦП (RX2 на ADC1), выбор антенн, аттенюаторы Alex | все приёмники на ADC0, ANT1 |
| Частоты выборки 768/1536 кГц | как у HL2: 48–384 кГц |

---

## 8. ANAN-G2 удалён (28.10.2026)

Удалены: `AnanBackend.*`, `P2Client.*`, `AnanRxDsp.*`, `AnanPanAnalyzer.*`, `AnanDroop*` (поправка
спада для гейтвера G2), `AnanSettings.*`, `AnanSliceAudio.h`, `AnanSpeakerPacing.h`; страница
«ANAN Front End» в Radio Setup; параметры `anan.*` в `populateFamilyParams`; запасной путь
`AETHER_ANAN_LEGACY`; профиль SATURN (board 10) в `P2Protocol` — теперь находятся только
ANGELIA (3) и ORION (4). Тесты G2 удалены из `tests.cmake` (11 шт.).

Вернуться при включении тестов (`AETHER_BUILD_TESTS=ON`): ссылки на удалённые классы остались в
смешанных тестах `backend_receive_contract_test`, `client_display_pan_settings_test`,
`control_receive_test`, `droop_calibration_seam_test`, `hl2_gain_restore_test`,
`hl2_hardware_ptt_state_test`, `noise_floor_auto_adjust_gate_test`, `radio_model_client_state_test`,
`radio_setup_control_routing_test`, `spectrum_sequence_gap_test`, `squelch_level_scale_test`,
`wdsp_process_tally_test`.

Что ушло вместе с G2 и может понадобиться ANAN-100D позже: звук на динамик радио (порт 1028,
пейсинг `AnanSpeakerPacing`), калибровка спада у краёв панорамы (общий механизм `DroopCalibration`
остался), частоты выборки 768/1536 кГц.

Flex пока не трогали (решение пользователя): RadioModel построена вокруг модели Flex.

---

## 9. Передача ANAN-100D (этап T1, 28.10.2026)

| Где | Что |
|---|---|
| `P2Protocol` | `buildGeneral(mask, paEnable)` — байт 58 бит 0 = PA; `buildTransmitSpecific()` (порт 1026: 1 DAC, без FPGA-ключа, mic PTT разрешён); `buildDucIq()` (порт 1029: seq + 240 × 24-бит I/Q); в High Priority — PTT0 (байт 4 бит 1), фазовое слово DUC0 (329–332), drive (345); Alex: при передаче Alex0 = ANT1 + HPF приёма + **LPF по частоте передачи** + реле T/R (бит 27), Alex1 = ANT1 + LPF передачи + реле (как deskHPSDR). |
| `P2Transport` | `setMox` → PTT0 и поток DUC I&Q: таймер 1 мс, опережение 6 пакетов (1440 отсчётов) в FIFO DUC, 192 кГц; очередь от Hl2TxDsp с «подкачкой» 6144 отсчётов, при опустошении — нули и повторная подкачка; TUNE-тон и CW-несущая (огибающая 5 мс) генерируются в транспорте, как в MetisClient; `setTxFrequencyHz`, `setTxDriveLevel` (0–255); прямая/отражённая мощность из пакета состояния (байты 14–15, 22–23). |
| `Hl2Backend` | Для ANAN: TXA выдаёт сразу 192 кГц (`outputSampleRateHz`), передача разрешена тем же правилом, что у HL2, номинал 100 Вт, мощность/КСВ по формуле Thetis для ANAN-100D (мост 0,095, опора 3,3 В, смещение 6). |

**Антенны (T1a).** В панели ANT вместо флексового списка — две строки кнопок: RX ANT и TX ANT (ANT1/ANT2/ANT3), ключи AppSettings `AnanRxAnt` / `AnanTxAnt` (0..2). Реле Alex: биты 24/25/26. RX ANT — то, что подаётся на ADC1 (ADC2 — отдельный вход RX2, выбирать нечего); выбирает приёмник, который слушает ADC1 и владеет им (при общем АЦП — RX1). При PTT в Alex0 подставляется TX-антенна, в Alex1 всегда TX-антенна. Фильтры HPF/LPF приёма теперь следуют первому приёмнику на ADC1, а не всегда RX1.

DUC в P2 работает на фиксированных 192 кГц (в P1 было 48 кГц); Thetis и deskHPSDR передают на P2 тоже 192 кГц.

RX1 = DDC2, RX2 = DDC3 — передача их не трогает (DDC0/DDC1 остаются под PureSignal).

Вернуться: PureSignal (DDC0/DDC1 на частоте передачи, обратная связь), выбор TX-антенны ANT1/2/3, FPGA CW-ключ и самоконтроль, микрофон радио (порт 1026 → хост), калибровка мощности по ваттметру, проверка запрета передачи вне диапазона.

---

## 10. FDV (FreeDV) и правая панель (28.10.2026)

**Баг правой панели.** «Показать дополнительные кнопки» → все апплеты съезжали вправо и обрезались. Причина: у стопки апплетов (`AppletPanel`, `m_stack`) ограничение по умолчанию записывает минимальную ширину раскладки в `minimumSize()` контейнера, а `QScrollArea` с `widgetResizable` растягивает виджет до max(viewport, minimumSize()) — переопределения `FlexContainer` при этом не работали. Как только стопка становится выше окна (открыли ящик кнопок) — появляется вертикальная полоса, viewport теряет 12 px, контейнер остаётся прежней ширины. Отсюда «иногда правильно» — зависит от высоты. Исправление: `m_stack->setSizeConstraint(QLayout::SetNoConstraint)`. Плюс диагностика в лог: если какой-то апплет сам не может ужаться до viewport, он называется (`AppletPanel: … needs N px`).

**FDV вместо RADE.** Псевдорежим «RADE» в списках режимов (RX-апплет, VFO, кнопки быстрых режимов; сохранённое «RADE» мигрирует) теперь «FDV». Кнопка на правой панели — «FDV» (id апплета остаётся `RADE`, чтобы не сбить настройки). Апплет FDV: режим 1600 / 700D / 700E / RADE и позывной (до 8 символов, A–Z 0–9), ключи `FdvMode`, `FdvCallsign`; ниже — синхронизация, SNR, смещение (RADE), принятый позывной; ошибка загрузки libfdv.

**Движок — как в MicronSDR.** `third_party/libfdv` — `libfdv.dll` + `libcodec2.dll` (MicronSDR_64/win64, C API, грузятся в MSVC-сборку), исходники обёртки и `build_win.sh`. CMake копирует DLL к exe. `src/core/fdv/FdvEngine` — порт `FdvUnit` (QLibrary, 24↔48 кГц полуполосным FIR), `FdvState` — общее состояние GUI↔бэкенд.
* RX: `Hl2Backend::mixReceiverAudio` — звук приёмника слайса FDV (USB) → libfdv → декодированная речь (усиление 2, как в MicronSDR).
* TX: `submitTxAudio` — микрофон → libfdv → модемный звук (USB) → Hl2TxDsp. Нажатие PTT — `fdv_tx_start`.
* Конец передачи: `MainWindow` ставит PTT-off hook (как у Flex RADE): PTT держится, пока libfdv не выдаст всё (RADE — EOO с позывным), максимум 1,5 с. Аппаратный PTT минует hook — тогда `fdv_tx_stop` при отпускании, EOO может не уйти.
* Активация на радио без DAX (`activateRADE` → `activateLocalFdv`): слайс в USB и TX, бейдж «FDV 1600 ●» на VFO, уход из USB выключает FDV.

Вернуться: обработка микрофона (компрессор/EQ) при FDV лучше отключать автоматически; свободный текст 1600/700x (`fdv_set_text`, `fdv_read_text`) не выведен; проверка на эфире.

---

## 11. Водопад из сырых бинов FFT (29.10.2026)

`Hl2Spectrum` держит кадр до усреднения (`rawBins()`), `Hl2RxDsp::spectrumReady` отдаёт оба, `Hl2Backend` прикладывает сырой кадр (в dBm) к `spectrumFrameReady` как `SpectrumCoverage` с флагом `viewportBounds`; `RadioModel` подставляет границы по геометрии панорамы, и строка водопада идёт по «coherent»-пути (без DC-ремонта и сглаживания рендера). Итог: FFT AVG и прочие настройки панорамы (уровни, диапазон dB, сглаживание трассы) на водопад не влияют. Влияют только настройки самого водопада: цветовое усиление, уровень чёрного / авто-чёрный, палитра, бланкер импульсов (если включён), скорость. Остаётся одна связь: строк в секунду не больше, чем кадров панорамы (FPS панорамы) — кадры считаются только по её таймеру.

S-Meter над стопкой апплетов ограничен шириной viewport (раньше торчал на 12 px при полосе прокрутки).

---

## 12. Кодек радио: динамик, микрофон, PTT микрофона (29.10.2026)

Было: звук на кодек не шёл (`submitSpeakerAudio` — заглушка), пакеты микрофона (порт 1026) отбрасывались, PTT микрофона только «наблюдался» (логика HL2: радио само ключится) — ANAN само не ключится, а статус после отпускания PTT больше не приходил, поэтому интерфейс зависал в TX с заглушенным приёмом.

* **Динамик / наушники радио:** `P2Transport` шлёт 48 кГц стерео на порт 1028 (пакеты по 64 кадра, темп по часам, очередь ≤300 мс, подкачка 60 мс). Источник — уже существующий отвод `Hl2Backend::forwardSpeakerAudioToCodec` (тот же микс, что и на ПК). Громкость отдельная, общая: `AnanCodecVolume` 0–100 (Настройки → Radio Audio).
* **Микрофон радио:** пакеты порта 1026 → `codecMicReady` → 48→24 кГц → прямо в модулятор (`Hl2TxDsp::processAudioBlock`, источник Microphone, FDV при необходимости), темп — часы радио. Контекст TX — отдельный «непрерывный микрофон»-производитель (`MainWindow::m_codecMicTxProducer` → `RadioModel::setCodecMicContext`). Микрофон ПК при `AnanMicSource = 1` игнорируется (DAX/TCI/генерируемый звук проходят). Звуковая карта ПК не нужна. `AnanMicBoost` — +20 дБ (Transmitter Specific, байт 50 бит 1).
* **Темп динамика:** один пакет 1028 на каждый пакет микрофона радио (оба по 64 кадра 48 кГц) — часы радио; таймер только если радио не шлёт микрофон.
* **PTT микрофона:** `Hl2Backend::ananHardwarePtt` → `RadioModel::hardwarePttChanged` → `MainWindow` ключит MOX через `TxController` (как кнопка MOX: FDV, микрофон, блокировки работают одинаково) и отпускает только свой ключ. Отпускание: бит PTT в статусе или прекращение статуса на 250 мс.

**Громкость кодека в заголовке окна (29.10, вечер).** Кнопка «PC Audio» рядом с главным регулятором теперь переключает цель регулятора и значка динамика: «PC Audio» ↔ «Codec». У каждой цели свой уровень и своё выключение звука; кодек — `CodecVolume` / `CodecMuted`, цель — `VolumeTarget`, всё запоминается. Включение/выключение звука ПК этой кнопкой больше не делается (для ANAN он обязателен). Ползунок громкости со страницы Radio Audio убран, там остался микрофон.
**Mic/CW (бывший Phone/CW).** Апплет и его кнопка переименованы в «Mic/CW» (id `P/CW` прежний). Список источника: `PC` (микрофон компьютера) / `MIC` (разъём микрофона радио) → `AnanMicSource`; кнопка «+ACC» на радио без Flex-входов стала «+20dB» → `AnanMicBoost`. Регулятор рядом — усиление микрофона (действует на оба источника). Страница настроек «Radio Audio» убрана (функция `buildAnanAudioTab` осталась, не используется).
**Настройки «не сохраняются» (29.10).** В базе настроек (`%LOCALAPPDATA%\AetherSDR\AetherSDR.db`, таблица `radio_settings`) четыре разных ANAN: 00:04:A3:00:53:4D, :B0:57, :EA:A7, :FB:FE — радио после каждого включения отдаёт другой MAC. Серийник = MAC, а от него зависят все настройки радио (ClientDisplay, OperatingState, BandStack) и автоподключение. Теперь серийник ANAN стабильный: `AnanDiscovery::stableSerial` = префикс производителя + номер платы (00:04:A3:00:00:03). Автоподключение принимает и старый серийник с тем же префиксом. Старые записи не переносились — настройки дисплея нужно выставить один раз заново.
**Mic/CW:** кнопка «20dB» (ширина 52); список профилей микрофона Flex сверху скрыт; индикатор уровня при MIC показывает микрофон радио (`Hl2Backend::ananCodecMicLevel` → `RadioModel::codecMicLevelChanged`), раньше всегда показывал микрофон ПК — поэтому прироста от 20 dB не было видно. Бит 1 байта 50 — «Mic Boost», как в Thetis `CmdTx`.

**Кодек: ~1 с звука, потом тишина (29.10, 19:30).** Поток в радио непрерывный (750 пак/с, пик ~2200), значит радио перестаёт играть само. Версия: при темпе 1:1 по пакетам микрофона буфер кодека в радио стоит на пустом краю и после первого сбоя глохнет. Теперь при начале темпа по микрофону уходит 24 пакета вперёд (32 мс), дальше 1:1 — запас держится. Если не поможет — следующий подозреваемый: байт 1400 / порядок пакетов.
**Как это делает Thetis (ChannelMaster: obbuffs.c, network.c `WriteUDPFrame` id 0):** пакет 1028 — seq + 64 кадра L/R 16 бит BE, тот же формат; отправляется СРАЗУ, как только накопилось 64 кадра звука приёма, без какого-либо темпа и без привязки к микрофону. Звук приёма рождается из потока IQ радио, так что средний темп — радио, а пачки поглощает FIFO кодека в FPGA. Сделано так же: `submitSpeakerAudio` шлёт все полные пакеты сразу; темп по микрофону и таймер убраны. Байт 1400 бит 1 у Thetis = `!audioamp_enable` (усилитель на J16 Orion MkII/Saturn), у нас 0.
**Mic/CW:** список PC/MIC оставался неактивным: на ANAN флаг «входы выбираемы» = true, а ветка срабатывала только при false. Теперь условие — `!selectable || hostModulation`. PROC: ширина 56 (надпись обрезалась). PROC на HL2/ANAN — компрессор речи в программе (ClientComp в AudioEngine), поэтому к микрофону радио (MIC) он пока не применяется.

**Перетаскивание окна за верхнюю панель:** рука и перенос окна — только в свободной середине (и на левых надписях); от кнопки PC Audio / Codec вправо (регуляторы, промежутки, переключатели док-панели) окно не таскается (`TitleBar::isSystemMoveAreaAt`, `mousePressEvent`, снятые `markDragHandle`).

**Уровень кодека (замер на радио 29.10):** поток идёт как надо — 750 пакетов/с с обеих сторон, очередь ~60 мс, без пустых пакетов, но пик ~300 из 32767 (−40 дБFS): микс приёма на шуме ~−35 дБFS. Громкость Codec стала законом в дБ с подъёмом: 1..100 → −10..+30 дБ (50 = +10 дБ; по просьбе: прежние 80 стали 100), 0 — тишина; мягкое ограничение выше 0,7 FS.
Диагностика кодека в строке «ANAN diag»: пакеты микрофона, пакеты на динамик (сколько тишины), блоков звука пришло, пик, очередь.

Проверено на имитаторе: PTT on/off (+250 мс), 1028 ≈750 пак/с без пропусков, микрофон принимается, mic boost в байте 50.

**PROC и MON (29.10, ночь).** PROC «не регулировался» с микрофоном радио: компрессор ClientComp стоит в AudioEngine только на пути микрофона ПК, а звук кодека идёт мимо него прямо в модулятор. Теперь у `Hl2Backend` свой `ClientComp` для микрофона радио (24 кГц, моно) с теми же пресетами NOR/DX/DX+ (−18/−24/−30 дБ порог, 2,5/4/6:1, подъём +4/+8/+12 дБ); включается и переключается из `requestTransmitControl` (ProcessorEnabled/Level). MON был `Unsupported` → теперь на ANAN хранит вкл/уровень; во время передачи обработанный микрофон (после PROC, до FDV) идёт в динамик ПК (`publishLegacyAudio`) и в кодек радио (−20 дБ, т.к. громкость Codec содержит подъём +10..+30 дБ под шум эфира). Уровень MON 1..100 → −40..0 дБ.

**Верхняя панель (29.10):** регулятор громкости наушников (значок, ползунок, число) скрыт — не нужен; виджеты остались в коде, но не показываются (и в компактном режиме тоже).

**Шкала панорамы при передаче (29.10):** на ANAN во время передачи все панорамы показывают фиксированно −60…+10 дБм (`SpectrumWidget::setTxFixedDbmRange`, включается из `MainWindow_Session` по `RadioModel::isAnanRadio()`); вместо «заморозки» шкалы Flex. Диапазон, пришедший во время передачи, применяется после неё; иначе возвращается прежний.

**MON на кодек тихо (29.10):** снято ослабление −20 дБ для кодека — MON идёт в кодек тем же уровнем, что на ПК, поверх закона громкости Codec (мягкое ограничение защищает от клиппинга).

**Калибровка ваттметра (29.10):** показывало 54 Вт при 48 Вт реальных (формула Thetis: мост 0,095, 3,3 В, смещение 6). Добавлен множитель `AnanFwdPowerCal` (по умолчанию 48/54 = 0,889) в `Hl2Backend::couplerWatts`; КСВ не меняется. Читается один раз при старте.

**Кодек: обрывы при клике по заголовку и шелест (29.10).** Причина: звук приёма для кодека шёл через поток GUI (Hl2RxDsp на I/O-потоке → очередь → `mixReceiverAudio` в GUI → очередь → `P2Transport`). Клик по заголовку окна (Windows держит поток GUI ~0,5 с в DragDetect/цикле перемещения) или тяжёлая перерисовка опустошали FIFO кодека в радио. Теперь звук для кодека микшируется и отправляется прямо на I/O-потоке (`Hl2Backend::ananIoCodecAudio`, DirectConnection на `Hl2RxDsp::audioReady`); поток GUI только публикует параметры микса (`ananPushCodecMix`: громкость Codec, уровни/баланс/mute приёмников) по таймеру ANAN и при каждом переключении mute. При FDV и во время передачи (MON) кодек по-прежнему кормится из потока GUI. Приоритеты на Windows: I/O-поток — TimeCritical, процесс — HIGH_PRIORITY_CLASS (как в Thetis). Путь микрофона радио в модулятор пока ещё идёт через поток GUI.

**Нет мощности в SSB (29.10).** Лог: на 40 м «MOX ON … drive 28»; в базе `driveByBand` 40m = 11 %. Байт drive — амплитуда, мощность ~ квадрату: 11 % линейно = 28/255 ≈ 1 Вт. Теперь на ANAN ползунок мощности — в ваттах, как в Thetis: drive = 162 · √(процент/100); 162 — drive для 100 Вт по замеру (drive 112 → 48 Вт по ваттметру). Настройка `AnanDriveFor100W` (по умолчанию 162). Индикатор ALC — пиковый уровень звука после ALC перед модулятором; «красный» у 0 дБFS = полная модуляция, это нормально.

---

## 13. Удалены симулятор и Hermes-Lite 2 (этап H1); подключение без окна (29.10.2026)

### Симулятор (демо-радио) — удалён полностью

**Удалено:** `src/core/backends/sim/*` (SimBackend, SimSignalSource, NoiseMixer, SpectrumPatternGenerator, DemoRadioConstants), `src/gui/DemoApplet.*` (апплет «Demo Noise»), `resources/demo_voice.wav`, тесты `sim_backend_test`, `sim_signal_source_test`, `spectrum_pattern_test`, `noise_mixer_test`, `demo_backend_swap_test` (и их регистрация в `tests.cmake`).

| Файл | Что сделано |
|---|---|
| `RadioModel.*` | Убраны семейство `"sim"` в фабрике и ветка SimBackend в `setupBackend()`; устаревшие комментарии. |
| `flex/RadioConnection.*` | Убрана «синтетическая» демо-сессия: `startSyntheticDemoConnect`, `emitSyntheticStatus`, `injectFaultStatus`, сигналы `demo*Changed`, `m_syntheticDemo`, демо-ответы в `writeCommand()`. |
| `flex/PanadapterStream.cpp` | Убрана демо-ветка в `start()`. |
| `AudioEngine.*` | Убраны `setMainSourceLegacyNr2()` / `m_mainSourceLegacyNr2` (геометрия NR2 для демо). |
| `MainWindow*.cpp/.h` | Убраны апплет Demo Noise, «добавить демо-радио», подача звука симулятора, `backendFeedsEngineDirectly()`. |
| `AppletPanel.*`, `ConnectionPanel.*` | Убраны апплет `DEMO`, флажок «Show the AetherSDR demo simulator», демо-строка в списке радио. |
| `AutomationServer.*` | Убрана команда `sim <fault>`. |
| `LocalRadioDiscoverySource.cpp`, `RadioDiscoverySource.h`, `aetherd/main.cpp`, `ModelRadioConnectionTarget.cpp`, `RadioCatalogue.cpp` | Убраны опция `--discover-sim` / `LocalDiscoveryOptions::simulator`, транспорт `"sim"`. |

### Hermes-Lite 2 — этап H1: HL2 нельзя найти, выбрать или подключить

**Удалено:** `Hl2Discovery.*` (поиск HL2, Protocol 1), `Hl2ControlRequest.*` (RQST/ACK HL2), реализация Protocol 1 в `MetisClient.cpp` (~2400 строк), проба HL2 по IP в `ConnectionPanel`, пункт «Hermes-Lite 2» в выборе типа радио, семейство `"hl2"` в фабрике `RadioModel::makeBackend()`, в `connect ip` автоматизации, в `RfGainRestore.h`, `ExperimentalRadioSupport.h`.

| Файл | Что сделано |
|---|---|
| `hl2/MetisClient.h/.cpp` | Теперь только **абстрактный интерфейс транспорта** Hl2Backend (типы `Params`/`Discovered`/`LinkCounters`, чисто виртуальные команды, сигналы, шлюз передачи). Реализация — `anan::P2Transport`. Имя класса историческое. |
| `hl2/Hl2Backend.*` | Нет `TransportKind` и `isAnan()`: всегда P2Transport. Все ветки `!isAnan()` убраны (оставлены ветки ANAN), удалены `stepAutoGain()` (закон АРУ HL2) и HL2-хвост `onRadioPttChanged()`; семейство в возможностях всегда `"anan"`, производитель Apache Labs, 100 Вт. Широкополосный bandscope HL2 (EP4) больше не объявляется — пункт меню «Wideband Bandscope…» неактивен. Сообщения об ошибках подключения — «ANAN: …». |
| `anan/AnanDiscovery.*` | Сюда перенесены помощники псевдонимов (`hasCustomNickname`, `nicknameLivesOnRadio`). |
| `RadioSetupDialog.cpp` | Страница «HL2 Hardware» заменена страницей **«ANAN Hardware»**: только Dither и Random АЦП (LTC2208, оба АЦП; по умолчанию выкл.). Кодек HL2, плата фильтров N2ADR, CL1, ATU убраны со страницы. |
| `MainWindow*` | Убраны `m_hl2Discovery` и его подключения; вкладки радио в заголовке теперь обновляются и от поиска ANAN. |

### H2 (запланировано): «спящий» код HL2 внутри Hl2Backend и вокруг

Компилируется, но для ANAN не работает или работает вхолостую:

| Где | Что |
|---|---|
| `Hl2Backend.*` | IO-плата (`applyIoBoardFrequency`, статус), выбор фильтров N2ADR (`applyBandFilter`, `m_ocFilterByte`), CL1/ATU, мост EP4-bandscope и закон автоусиления HL2 (`Hl2AutoGainPolicy`, `Hl2BandscopeHeadroom`), `widebandConverterViewRecord()`, телеметрия HL2 через порт 1025 (`Hl2TelemetryService`/`Hl2TelemetryPoller`, регистрируется только для семейства `"hl2"`), строки логов «HL2 …». Переименование `Hl2Backend` → ANAN-имя. |
| `hl2/MetisProtocol.*` | Кодеки Protocol 1 (C&C, EP6, discovery). Типы `SampleRate`, `Hl2Telemetry`, `Ep4Stats`, `DiscoveryReply` ещё используются интерфейсом — вынести и удалить остальное. |
| `hl2/Hl2HardwareOptions.*` | Поля HL2 (кодек, плата фильтров, CL1, ATU); для ANAN нужны dither/random и уровень кодека. |
| `Hl2EmergencyStop.*` | Аварийная остановка при сигнале ОС — работала только для Protocol 1; **стоит переделать под Protocol 2** (run=0 в High Priority), ANAN после «убийства» программы продолжает слать поток. |
| `RadioSetupDialog.cpp` | Страница «Calibration» ещё учитывает «CL1 locked» (для ANAN всегда нет). |
| `BandscopeDialog.*`, `RadioCertification.*`, `RadioHealthDialog.cpp` | Части, рассчитанные на HL2. |
| Внутреннее пространство расширений `"hl2"` (`hw.get/hw.set`, `freqcal.*`) | Оставлено как есть (только имя); переименовать вместе с Hl2Backend. |
| `tests/` | Десятки `hl2_*`/`metis_*` тестов ссылаются на удалённые классы (тесты выключены, `AETHER_BUILD_TESTS=OFF`). |

### Подключение без окна (по просьбе: «окно подключения выглядит ужасно, пусть само находит трансивер»)

* **Окна подключения больше нет.** `MainWindow::showConnectionDialog()` ничего не делает; при старте, ошибках и потере связи окно не открывается; модальное окно «Radio Disconnected» для ANAN не показывается. `ConnectionPanel` остаётся скрытым списком найденных радио.
* **Поиск раз в секунду.** `AnanDiscovery::start(1000)`; пока радио подключено, поиск стоит (как и раньше — он стоил ANAN пачку IQ-пакетов). Новый сигнал `AnanDiscovery::radioSeen` — на каждый ответ, даже без изменений: радио, оставшееся в списке после потери связи, иначе не переподключилось бы.
* **Автоподключение** (`MainWindow::maybeAutoConnectAnan`): первое найденное ANAN подключается без условий «последнее радио» / AutoConnectToLastRadio. **Занятое** ANAN перехватывается только когда занятая сессия, скорее всего, наша: в первые 20 с после запуска для радио, использованного здесь последним (программа была закрыта/остановлена без run=0), в течение 60 с после обрыва нашей сессии, или по просьбе (щелчок по статусу / File → Connect). Иначе статус «… is in use by another program — click to take it over» — две программы не перетягивают радио друг у друга. После трёх неудач — пауза 10 с, потом снова. После обрыва связи — 3 с ожидания (транспорт сам восстанавливает поток, если IQ вернулся). `RadioModel` для ANAN больше не повторяет подключение вслепую по старому адресу (`reconnectsByDiscovery()`). File → Disconnect доступен и во время поиска — останавливает автоподключение.
* По итогам независимой проверки: окно «Setting Up Your Radio» (долгая первая настройка WDSP) теперь не модальное; уведомление «Experimental radio support» — не чаще раза за запуск; поздняя ошибка подключения не перезаписывает «подключено»; «Rescan radios» опрашивает и ANAN; CL1 и ATU в сохранённых настройках оборудования для ANAN принудительно выключены (иначе старое значение со страницы HL2 обнуляло бы калибровку частоты без способа это исправить); регистрация метатипов транспорта восстановлена в конструкторе `MetisClient`.
* **Статус в заголовке** (`TitleBar::setLinkStatus`, `MainWindow::setLinkState`), слева от свободной середины: «Searching for radio…» (жёлтый), «Connecting to ANAN-100D 172.16.10.14…» (жёлтый), «ANAN-100D 172.16.10.14» (зелёный), «Connection lost — searching…» / «Connection failed: …» / «… is in use by another program» (красный), «Disconnected — click to connect» (серый). Щелчок по статусу, File → «Connect to Radio» и щелчок по вкладке радио — подключить сейчас. File → Disconnect — отключиться и не подключаться, пока не попросят.
* Пункт «Connect manually…» во всплывающем списке радио убран.

**Файлы, которые больше не используются** (на машине пользователя остались, в сборку не входят, можно удалить вручную): `src/core/backends/sim/` (вся папка), `src/gui/DemoApplet.h/.cpp`, `src/core/backends/hl2/Hl2Discovery.h/.cpp`, `src/core/backends/hl2/Hl2ControlRequest.h/.cpp`, `resources/demo_voice.wav`, `tests/sim_backend_test.cpp`, `tests/sim_signal_source_test.cpp`, `tests/spectrum_pattern_test.cpp`, `tests/noise_mixer_test.cpp`, `tests/demo_backend_swap_test.cpp`.

## 14. Панорама передачи: постоянный зум ±12 кГц и уровень от полной шкалы (30.10.2026)

Просьба: «Панорама передачи ширина фиксированная ±12 кГц. Уровень привязан только к полной шкале данных, уходящих на трансивер. На панораме передачи всегда данные, уходящие в трансивер, даже в режиме FDV». Затем уточнение: «регулировка мощности не должна влиять на показания — регулятор только ставит байт 0..255; панораму заново рисовать не надо, она уже была — верни старое и сделай постоянный зум».

* Первый вариант (отдельно нарисованная панорама поверх спектра) **убран**; `SpectrumWidget` возвращён к прежнему виду.
* **Зум.** При нажатии PTT панорама, на которой стоит передатчик, переходит на TX-частоту ±12 кГц (`SpectrumWidget::enterTxZoom`), при отпускании возвращается (`exitTxZoom`). Только отображение: радио ничего не командуется (DDC ANAN умеет только 48/96/192/384 кГц), водопад (во время передачи он стоит) не пересчитывается разрушающе. Геометрию, пришедшую от модели во время передачи, панорама запоминает и применяет при отпускании. Перетаскивание/колесо/щипок по панораме во время передачи игнорируются — иначе они посчитались бы по «зумированной» оси и ушли бы в радио. Обрезка краёв панорамы на время зума выключена.
* **Данные и уровень.** Во время передачи след панорамы — не приёмник (тот видит выходной сигнал, и его уровень зависел от мощности), а спектр DUC IQ ровно в том виде, в каком он уходит на порт 1029: `anan::TxScope` в `P2Transport::onTxPacerTick()`, тот же клип [-1, 1], что и в `buildDucIq()`. TUNE, CW, голос, FDV — одинаково. Ползунок мощности пишет только байт drive и на панораму не влияет. 0 dB = полная шкала отсчётов на проводе (в Protocol 2 это 24 бита; от разрядности dBFS не зависит); шкала по-прежнему −60…+10.
* **Боковая полоса.** На проводе IQ в «HPSDR-ориентации» — зеркало ВЧ-спектра (Hl2TxDsp сопрягает для провода, приём сопрягает обратно). Первая сборка показывала его как есть — USB выглядел как LSB; `TxScope` теперь отражает обратно (30.10, по замечанию оператора).
* **Сброс FFT после передачи** (30.10, «сигналы передачи остаются на панораме»): `Hl2Backend::dropAllPanHistory()` сбрасывает у всех приёмников усреднение и недособранное окно БПФ (независимо от настройки AVG) на нажатии, на отпускании и ещё через 150 и 350 мс — приёмник слышит свою передачу ещё 178–285 мс после отпускания.
* **Анализ.** БПФ 4096 на 192 кГц (46,9 Гц), Блэкман–Харрис, нормировка: тон |x| = 1 = 0,00 dB (проверено: половина амплитуды −6,02). 513 бинов на ±12 кГц, ~23 кадра/с. Голос на каждом бине ниже 0 — его мощность распределена по многим бинам.
* **Путь.** `MetisClient::txScopeFrameReady` → `Hl2Backend::ananTxScopeFrame` → `RadioModel::txScopeFrameReady` → `MainWindow` → `SpectrumWidget::setTxScopeFrame()` (принимается только под зумом передачи, кадры приёмника в это время не рисуются).

## 15. MTR, Mic, CW вместо MQTT (30.10.2026)

* **MTR (Radio Vitals) был пуст на ANAN.** Возможности бэкенда (с HL2) объявляли температуру PA и не объявляли напряжение, а транспорт P2 ни того ни другого не передавал. Теперь: напряжение питания из пакета статуса P2 (байты 49–50, АЦП 12 бит, 3,3 В, делитель (4,7 к + 0,82 к)/0,82 к как в Thetis; подстройка `AnanSupplyVoltsScale` в настройках) → метр `RAD:+13.8A` → шкала Supply Voltage. Температуры PA у ANAN-100D в протоколе нет — шкала скрыта. Первое значение пишется в лог («ANAN: supply ADC raw … -> … V») для сверки с вольтметром.
* Кнопка и заголовок **Mic/CW → Mic** (ключ `P/CW` прежний, раскладка не сбивается).
* **MQTT выключен**: в CMakeLists `ENABLE_MQTT` принудительно OFF (FORCE — перекрывает кэш сборки), весь код MQTT за `HAVE_MQTT` и не собирается (библиотека mosquitto тоже). На его месте пустой апплет **CW** (`src/gui/CwApplet.*`, ключ `CW`); в сохранённой раскладке кнопок `MQTT` заменяется на `CW` на том же месте.

## 16. Убраны +TNF, PHN, MTR, CLK, HLTH (30.10.2026)

* **+TNF** (левое меню панорамы) — по словам оператора, «полностью повисает управление трансивером». Кнопка, её сигнал `addTnfClicked` и обработчик в `MainWindow_Wiring` удалены, индексы кнопок меню пересчитаны. Чтобы тот же путь не открывался из контекстного меню панорамы, ANAN теперь объявляет `maxNotchFilters = 0` — ручные режекторы WDSP на ANAN недоступны вовсе (код в Hl2Backend не вызывается).
* **PHN** (Phone: VOX/фильтры TX/DEXP/AM carrier — для ANAN не работал), **MTR** (Radio Vitals), **CLK** (AetherClock), **HLTH** (Antenna Health) — апплеты, кнопки, записи в каталоге и порядке кнопок, все вызовы из MainWindow удалены; исходники `PhoneApplet`, `MeterApplet`, `AetherClockApplet`, `HealthApplet` исключены из сборки (закомментированы в CMakeLists, файлы остались — на них ссылаются выключенные тесты). Движок AetherClock остаётся только для команды автоматизации `get clock` и не запускается.
* Напряжение питания (раздел 15) по-прежнему публикуется метром `RAD:+13.8A`, но показывать его теперь негде.

## 17. Правая панель: апплеты обрезались справа — снова (30.10.2026)

Скриншот оператора: RX Controls и TX Controls обрезаны справа на ~14 px (частота, «2.8K», ползунки, стрелки), заголовки целы. Причина: панель жёстко 260 px; когда появляется вертикальная полоса прокрутки (12 px), видимая область — 248 px. Стопка (`SetNoConstraint`, раздел 10) держит контейнер по ширине видимой области, но вертикальная раскладка всё равно отдаёт каждому апплету не меньше ЕГО минимальной ширины — апплет с минимумом больше 248 рисуется шире видимой области и обрезается. Прошлое исправление закрыло случай «контейнер не сжимается», а не «апплету нужно больше места».

Исправление: `AppletPanel::fitWidthToApplets()` — ширина панели = max(260, минимум самого широкого открытого апплета + полоса прокрутки); пересчитывается при изменении видимой области и при изменении минимумов апплетов (LayoutRequest контейнера). Когда широкий апплет закрыт, панель возвращается к 260. В лог: «AppletPanel: width N for <класс> needing M px, scroll bar 12 px» — видно, какой апплет требует ширину.

## 18. Сброс FFT один раз, бит тюнера, LNA, удалён SWR sweep (30.10.2026)

* **Сброс FFT после передачи — один раз** (оператор видел два): осталось только обнуление на самом фронте PTT (`dropAllPanHistory()` в `applyKeying`), повторные через 150/350 мс убраны.
* **Бит тюнера.** High Priority байт 1400 бит 2 («tuner start», так его называет p2app) ставится в 1 на время TUNE (`Hl2Backend::setTune` → `P2Transport::setTunerStart`) и снимается при отпускании.
* **LNA.** В меню ANT (левое меню панорамы, ряды ANAN) — маленькая кнопка «LNA». Настройка `AnanLna`, передаётся таймером ANAN, как антенны → Alex0 бит 3 («6M Preamp», приложение D спецификации — единственный бит предусилителя в Protocol 2 для ANAN-100/100D/200D), только в состоянии приёма. По схеме Alex этот предусилитель стоит в 6-метровом тракте, поэтому на КВ-диапазонах эффекта может не быть.
* **SWR sweep удалён целиком**: кнопки Start/Clear Sweep, Save CSV, Limit range/From/To в меню ANT; пункт Tools → «Start SWR Scan…»; остановка по Esc; запреты «sweep is running» в настройке и автоматизации; график на панораме (`SpectrumWidget::drawSwrSweep` и др.); `MainWindow_SwrSweep.cpp` и `SwrSweepLicenseDialog.cpp` исключены из сборки (файлы на диске остались, не используются).

## 19. Управление панорамой: клик, правое перетаскивание, зум внутри частоты дискретизации (30.10.2026)

* **ЛКМ клик** (панорама или водопад, вне полосы фильтра и его краёв) — активный слайс встаёт на частоту клика (с шагом STEP), панорама не двигается (`edgePanTuneRequested` с текущим центром — без «догоняния»). Двойной клик — то же. ЛКМ по полосе фильтра / краям — по-прежнему перетаскивание VFO / краёв. Перетаскивание панорамы левой кнопкой убрано.
* **ПКМ перетаскивание** — панорама едет за курсором, слайс остаётся на своей частоте (порог 4 px). **ПКМ клик** без движения — контекстное меню, как раньше (повторно вызывается обработчик нажатия с флагом `m_rmbMenuReplay`).
* **Линейка частот** — перетаскивание любой кнопкой — зум (так и было), но теперь это **зум отображения внутри текущей частоты дискретизации**, а не переключение 48/96/192/384 кГц:
  * `Hl2Backend::setPanBandwidth` → `Receiver::zoomViewHz` (0 = вся полоса DDC; минимум max(1,5 кГц, rate/64)); пан сообщает ширину = зум (`panViewHz`).
  * Бины спектра и водопада обрезаются по центру до доли view/rate; чтобы при зуме хватало разрешения, размер БПФ растёт (`Hl2RxDsp::setSpectrumFftSize`: ~1024 бина в видимой части, до 16384 точек и окна ≤ 0,1 с). Шумовая дорожка при зуме ниже (узкие бины); для AUTO ATT это учтено поправкой 10·lg(N/1024).
  * Частоту дискретизации меняют только кнопки [−] rate [+] — новый глагол расширения `anan.rate` (`SpectrumWidget::receiverRateRequested` → MainWindow); зум при этом сбрасывается в 0.
* **Надписи справа сверху:** значение частоты дискретизации (из `AnanUiState::sampleRateHz`, не меняется при зуме), левее — «Zoom ×N.N» — увеличение относительно всей полосы с точностью до десятой (×1.0 — вся полоса, ×2.0 — вдвое крупнее; по просьбе оператора вместо процентов).

## 20. Панорама перерисовывалась при перетаскивании и зуме (30.10.2026)

Причины (бэкенд): (1) `setPanCenter` на каждом шаге перетаскивания (~30/с) сбрасывал усреднение FFT и вместе с ним собираемое окно — след начинался заново, а при большом БПФ зума кадр вообще не успевал собраться; (2) `setPanBandwidth` (зум) на каждом шаге тоже сбрасывал усреднение и при переходе через степень двойки пересоздавал БПФ; (3) число точек кадра менялось с каждым шагом зума (обрезка), и панорама с водопадом начинали след заново.

Исправление: усреднение сдвигается вместе с NCO (`Hl2Spectrum::shiftBins`, `Hl2RxDsp::shiftSpectrum`; сброс — только при скачке больше половины полосы); при зуме ничего не сбрасывается, размер БПФ меняется через 400 мс после окончания жеста (`m_zoomFftTimer`); кадр панорамы и строка водопада всегда 1024 точки (пиковое прореживание или линейная интерполяция видимой части).

## 21. Щелчок в динамике при передаче с тангенты (30–31.10.2026)

Оператор: «при переходе на передачу с тангенты микрофона сильнейший щелчок в динамике трансивера, с MOX — нет; щелчок в разное время».

* Первая версия (30.10) считала причиной выброс контакта PTT на линии микрофона и глушила микрофон 120 мс после нажатия — **не помогло и убрано**: микрофон радио шлёт поток всегда, а щелчок приходит в разное время, что указывает на буфер, а не на сигнал.
* **Причина** (по строке диагностики «speaker N pkts»): на динамик кодека отправлялось ~3750 пакетов за 5 с, а в интервалах с передачей — ~3690: за одну передачу пропадало ~60 пакетов (≈80 мс). При передаче звук приёмника глушится, и микшер на потоке ввода-вывода переставал слать звук на кодек, а MON начинал слать свой из GUI-потока, когда доходили отсчёты микрофона; при отпускании — наоборот, с паузой удержания 70 мс. Между двумя «поставщиками» образовывалась дыра, FIFO кодека в ПЛИС опустошался — щелчок, в момент, зависящий от того, как легли пакеты.
* **Исправление:** у потока на динамик один поставщик — микшер на потоке ввода-вывода работает всегда (заглушённые приёмники отдают тишину в такт радио), а MON подмешивается в него через `m_monFifo` (24 кГц, до 200 мс, очищается на фронтах PTT). Прямой путь MON на кодек остался только для случая, когда микшер выключен (FDV).

## 22. Виджеты: вынос и возврат перетаскиванием, высота окна по содержимому (30.10.2026)

* **Вынос:** шапку виджета тянут из правой панели и отпускают вне её — виджет становится отдельным окном, шапкой под курсором (`ContainerWidget::onTitleBarDragStart`: `QDrag::exec` вернул IgnoreAction → `floatAtRequested` → `ContainerManager::floatContainerAt`). Отпускание над самой панелью — по-прежнему перестановка, не вынос.
* **Возврат:** отдельное окно тянут за его шапку на правую панель — пока курсор над стопкой виджетов, видна голубая линия места вставки; при отпускании виджет встаёт туда (`ContainerTitleBar` двигает окно вручную, а не системным циклом, чтобы получить отпускание → `floatingDragReleased` → `AppletPanel::installDockDropHandlers`: dock + перестановка в `m_appletOrder`). Простой клик по шапке без перемещения окно не встраивает.
* **Высота отдельного окна** фиксирована по содержимому и подстраивается при его изменении (`FloatingContainerWindow::fitHeightToContent`); растягивать мышью можно только по ширине.
* Кнопку открепления в шапке пока оставили — удалить после проверки перетаскивания.

## 23. Вынос виджета — окно сразу (31.10.2026)

Пока шапку тянут внутри панели, это перестановка (как раньше). Как только курсор выходит за панель, перетаскивание отменяется (`QDrag::cancel`, таймер следит за курсором во время перетаскивания) и виджет сразу становится отдельным окном под курсором, которое продолжает двигаться за мышью, пока кнопка нажата (`ContainerTitleBar::beginFloatingDragFromCursor`). Отпустить над панелью — встроится обратно в место под курсором.


## 24. CW-виджет = страница Keyer из MicronSDR; плашка EXPERIMENTAL; стоп/старт по вкладке радио (31.10.2026)

**CW-виджет** (`src/gui/CwApplet.*`) повторяет MicronSDR Options → Keyer (`Options.ui`, `page_6`; `micronsdr.cpp sendKeyer`), с теми же пределами и умолчаниями:

| Группа | Настройка | Пределы / умолч. | AppSettings | P2 Transmit Specific (порт 1026, раскладка Thetis `CmdTx`) |
|---|---|---|---|---|
| Keyer | Mode: Straight / Iambic A / Iambic B | B | `AnanKeyerMode` | байт 5: бит 3 iambic, бит 5 mode B |
| | Speed | 10..63 WPM, 20 | `AnanKeyerSpeed` | байт 9 |
| | Weight | 33..66, 50 | `AnanKeyerWeight` | байт 10 |
| | Paddle swap | нет | `AnanPaddleSwap` | байт 5 бит 2 |
| | Keyer auto letter spacing | нет | `AnanKeyerAutoSpace` | байт 5 бит 6 (strict spacing) |
| Transmit | Hang time | 10..1000 мс (шаг 10), 250 | `AnanHangTime` | байты 11–12 |
| | CW demo (CW без RF) | нет | `AnanCwDemo` | на хосте: PA выкл (General 58), реле T/R выкл, drive 0 |
| Side tone | Level | 0..99 %, 50 (→ 0..255) | `AnanSideToneLevelPct` | байт 6; бит 4 = уровень > 0 |
| | Frequency | 400..1000 Гц, 700 | `AnanSideToneFreq` | байты 7–8 |

Постоянно: байт 5 бит 1 (keyer on) — только когда TX-приёмник в CW/CWU/CWL; бит 7 break-in = 1 (у MicronSDR устройство всегда в break-in с hang time); байт 13 RF delay = 20 мс; байт 17 фронт CW = 7 мс (умолчание Thetis). `Hl2Backend::ananPushCwKeyer` (таймер 300 мс) → `P2Transport::setCwKeyer` → `buildTransmitSpecific(seq, micBoost, cw)`.

Хост следует за брейк-ином радио: при включённом кейере точка/тире в статусе (байт 4 биты 1–2) считаются PTT радио и держат его ещё hang time после последнего элемента (`m_paddleHangTimer`), так что панорама/реле/антенна переходят в TX вместе с радио. Скорость кейера синхронизирована с CW-скоростью программы в обе стороны (как в MicronSDR, где она же — скорость CW-сообщений). Тест протокола дополнен байтами кейера.

Не проверено на радио: что ПЛИС ANAN-100D формирует CW сама при `cw_enable` (данные DUC при этом не используются); шкала уровня самоконтроля 0..255 (Thetis шлёт 0..100).

**Плашка EXPERIMENTAL** в шапке и окно «Experimental radio support» отключены (`updateExperimentalRadioSupport` сразу гасит значок).

**Стоп/старт радио:** клик по вкладке радио в шапке — если оно подключено (или идёт подключение), отключить (`disconnectFromRadioByUser`, автоподключение не срабатывает до следующего клика); если остановлено — подключить (`connectRadioNow`).


## 25. Без External Key; вынесенное окно следует за мышью; без «руки» на шапках (31.10.2026)

* Группа External Key из CW-виджета убрана по просьбе оператора (последовательный порт для PTT/CW — по-прежнему Radio Setup → Serial).
* **Вынос виджета:** окно, созданное из вытащенного виджета, переставало следовать за мышью — у нового окна на Windows нет захвата мыши и фокуса (кнопку нажали над панелью, захват держал отменённый QDrag), поэтому `grabMouse()` не давал ни движений, ни отпускания. Теперь окно ведёт таймер 10 мс: позиция курсора (`QCursor::pos`) и состояние кнопки (`GetAsyncKeyState`) опрашиваются напрямую, без событий мыши (`ContainerTitleBar::liveDragTick`); при отпускании — `floatingDragReleased` (встраивание в панель, если над ней).
* Курсор над шапками виджетов — обычная стрелка (убраны OpenHand/ClosedHand).


## 26. Звенящий щелчок и всплеск на TX-панораме в начале передачи с тангенты (31.10.2026)

Оператор: щелчок при переходе на передачу с тангенты остался, «звенит», на панораме передачи виден всплеск — мусор в начале передачи.

Разбор: всплеск виден на TX-панораме, то есть он в данных, которые хост отправляет в DUC. Микрофон радио идёт с постоянной составляющей (смещение, и ступенька от контакта PTT, которую ФВЧ кодека гасит сотни мс). Передача начиналась сразу с этим смещением → ступенька на входе; компрессор (состояние от прошлой передачи) и полосовой фильтр TX превращали её в звенящий импульс; MON нёс его в динамик.

Исправление (`Hl2Backend`, обработчик `codecMicReady`):
* Фильтр постоянной составляющей на микрофоне радио — работает всегда, и на приёме, так что к нажатию ступенька уже затухла (в разделе 27 заменён на ФВЧ 2-го порядка 120 Гц).
* Каждая передача открывается тихо: 30 мс нулей, затем плавное нарастание 20 мс (приподнятый косинус); компрессор сбрасывается в начале передачи.
* `P2Transport`: очередь IQ очищается при нажатии, а IQ, пришедший без MOX (хвост прошлой передачи), отбрасывается.

Не проверено: если остаток прошлой передачи хранит FIFO DUC в ПЛИС, на TX-панораме его не видно — тогда понадобится досылать нули при отпускании.


## 27. Мало мощности в SSB голосом (31.10.2026)

Оператор: TUNE — полная мощность; голосом ALC до красной зоны, а мощность мала, ток почти не растёт (2,5 А; FT-710 на «АААА» — 13 А).

Что проверено в облаке (сборка WDSP 2.10 из `third_party/wdsp` под Linux, стенд `scratchpad/txlev`):
* Модулятор TXA ровно в нашей конфигурации (24 кГц → 192 кГц, блоки 512/4096, USB 300..2700): тон 1 кГц с пиком 0,85 на входе → |IQ| пик −0,2 dBFS, двухтональный 700+1900 Гц → пик −1,4 dBFS. **Модулятор полную шкалу даёт.**
* Синтетический гласный «А» → наш пиковый ALC → TXA: пик −2,4 dBFS, среднее −8,4 dBFS (это и есть средняя мощность голоса без процессора, ~15 % от PEP). Постоянка 0,01 + фон 50 Гц на микрофоне съедают ещё ~3 дБ (ALC меряет пики вместе с ними); ФВЧ 120 Гц возвращает их.

Из журналов оператора: полоса передачи **300..1600 Гц** (`HL2 TXA modulator ... passband 300..1600`) — срез TX-фильтра в Mic-апплете стоит на 1600 Гц; и предупреждения «microphone peaked at −21…−35 dBFS … mic gain 98 of 100» на коротких нажатиях.

Сделано:
* ФВЧ 2-го порядка (Баттерворт, 120 Гц) на микрофоне радио вместо ФВЧ 1-го порядка 60 Гц — постоянка, фон и ступенька PTT больше не управляют ALC.
* **Источник TEST** в выборе микрофона (PC / MIC / TEST, `AnanMicSource` = 2): вместо микрофона — двухтональный сигнал 700 + 1900 Гц с пиком в полную шкалу через весь голосовой тракт (ALC, TXA, DUC), как 2-TONE в Thetis. Ожидаемо: пик ≈ −1,4 dBFS, среднее ≈ −4,4 dBFS → около 100 Вт PEP / 50 Вт среднего.
* Строки журнала на каждую передачу: `ANAN P2 transport: TX over N s: IQ peak / mean / loudest 100 ms dBFS, drive N/255` (что ушло в DUC) и `ANAN TX over (radio mic): mic peak / rms / below 150 Hz dBFS, mic gain xN` (что дал микрофон до усиления).


## 28. Путь сигнала микрофона радио; PROC не работал; запись TX (31.10.2026)

По журналу оператора (10:11–10:19, «АААА» с тангентой и MOX):
* Микрофон радио (до усиления): пик −36…−49 dBFS, RMS −55…−75 dBFS — на ~40 дБ ниже полной шкалы. Аппаратное +20 dB (20dB в Mic-апплете) было выключено. Ползунок MIC на 98 (×83, +38 дБ) — у края.
* На выходе (DUC IQ): пик −4…−8 dBFS, громкие 100 мс −11…−15 dBFS, среднее −15…−42 dBFS при TUNE = 0 dBFS → 3–8 Вт среднего. Пики упираются в ALC (0,85), а среднее голоса на 11–16 дБ ниже — нужна компрессия.
* **PROC фактически не работал:** компрессор микрофона радио стоял ДО усиления ползунка MIC, т. е. на сигнале −40 dBFS, а его пороги −18/−24/−30 dBFS — он ни разу не сжимал, только добавлял make-up. Исправлено: усиление MIC применяется в бэкенде перед PROC, модулятору блок уходит как EngineGenerated (без второго усиления), ALC остаётся защитой. MON теперь слышит то, что уходит в эфир (громче прежнего).
* **Блок TX DSP (EQ/COMP/GATE/… приложения) к микрофону радио не подключён** — он стоит в AudioEngine только на пути микрофона ПК. Путь микрофона радио: кодек 48 кГц → ½ → 24 кГц → ФВЧ 120 Гц → MIC-усиление → PROC → ALC → WDSP TXA (300..2800) → DUC. Подключение к полосе приложения — отдельная задача.
* MOX и тангента в программе ключат одинаково (`TxController::Activity::Mox`); разница только в сигнале микрофона в момент нажатия. Для разбора — **запись каждой передачи** в `logs/txcap-mic.wav` (последняя; 24 кГц float stereo: L — сырой микрофон, R — что ушло модулятору; с 1,5 с до нажатия) и строка `ANAN TX capture: … radio PTT at N ms, host key at M ms`.


## 29. Щелчок — в динамике, а не в микрофоне; PROC = компрессор WDSP (31.10.2026)

**Щелчок.** Запись `txcap-mic.wav` (10:36, нажатия тангенты во время MOX) показала: на микрофоне при нажатии — лишь −46…−50 dBFS, никакой постоянки; оператор прав. Зато строка диагностики динамика: `peak 28553` (из 32767) только в 5-секундном окне с передачами с тангенты, во всех остальных ~600. Причина: тангента радио сама переключает его реле приём/передача в момент нажатия, раньше, чем хост об этом узнаёт; приёмник получает переходный процесс переключения (ослабление АЦП на передаче = 0) и демодулирует его с усилением приёма, а заглушение приёма от хоста ещё в пути (статус → ключ → mute, каждый шаг через очередь), а очередь микшера его уже держит. С MOX хост сначала глушит приём, потом ключит — переходный процесс не слышен.
Исправление: `P2Transport::setRadioPttPressHook` — в момент, когда статус радио сообщил нажатие PTT, на потоке ввода-вывода очередь звука приёмников в микшере сбрасывается, а приёмники в динамике радио глушатся на 250 мс (MON звучит); дальше держит обычное заглушение на передаче. Звук на динамики ПК этим не затронут.

**Мощность.** Голос оператора из той же записи прогнан через настоящую цепочку (ФВЧ, ALC, WDSP TXA 300..2800) на стенде: без сжатия громкие 100 мс −14 dBFS (3–5 Вт), с компрессором WDSP TXA (COMP Thetis) 10/15/20 дБ — −7 / −4,5 / −2,5 dBFS (≈20 / 35 / 55 Вт в среднем на громких местах). PROC для микрофона радио теперь включает компрессор TXA: NOR 10 дБ, DX 15 дБ, DX+ 20 дБ (`Hl2TxDsp::setCompressor`, `WdspChannel::setTxCompressor`, `SetTXACompressorRun/Gain`); ClientComp для микрофона радио выключен (не сжимать дважды). Условие — пики микрофона после MIC должны доходить до ALC: сырой микрофон −35…−40 dBFS, нужно ~+38 дБ (20dB + MIC ≈ 74, или MIC ≈ 98 без 20dB).


## 30. Щелчок с тангенты — доказательства вместо версии (31.10.2026)

Оператор справедливо возразил: «приёмник не может выдать такой уровень при переключении — его сигнал уменьшается, а не возрастает». Версия раздела 29 (переходный процесс реле в приёмнике) не доказана: `peak 28553` говорит только, что почти полная шкала ушла на динамик радио в окне с передачами с тангенты, но не откуда.
Добавлено для доказательства:
* `logs/txcap-spk.wav` — то, что уходит на динамик радио, от −1,5 до +2 с вокруг нажатия тангенты: левый канал — приёмники, правый — MON, оба с усилением кодека (1,0 = полная шкала), ДО глушения 250 мс; строка `ANAN speaker capture: … radio PTT at N ms; receivers' loudest X dBFS at M ms`.
* Строка `ANAN PTT timeline`: через сколько мс после PTT радио пришёл MOX хоста и пик входа приёмника RX1 (IQ, dBFS) каждые 5 мс от −50 до +400 мс.
Сброс очереди микшера при нажатии убран (иначе он стёр бы улику); глушение приёмников на динамике 250 мс оставлено.


## 31. Что не даёт голосу дойти до полной шкалы (31.10.2026)

Лог 10:55–10:58: PROC был выключен (`compressor off`); с 20dB и MIC ×9 передача 22 с: IQ пик −3,2 dBFS, громкие 100 мс −8,7, среднее −15,8 dBFS.
Запись голоса оператора (txcap-mic.wav, 10:58, с 20dB) через настоящую цепочку на стенде, MIC +19 дБ:

| вариант | пик | громкие 100 мс | среднее |
|---|---|---|---|
| как было: ALC HL2 (мгновенная атака, отпускание 500 мс) | −2,2 | −9,6 | −17,9 |
| ALC HL2 с отпусканием 50 / 10 мс | −0,2 / −1,4 | −10,1 / −9,7 | −17,0 / −16,7 |
| без ALC HL2 (только ALC WDSP, как в Thetis) | −0,1 | **−6,8** | −15,0 |
| + компрессор TXA 10 дБ (PROC NOR) | 0,0 | **−2,8** | −8,5 |
| + компрессор TXA 15 дБ (PROC DX) | 0,0 | **−2,4** | −7,5 |

Вывод: «ограничитель» — это ALC HL2 перед модулятором: после каждого пика он опускает усиление на весь слог (отпускание 500 мс). Он выключен для ANAN (`tc.alcEnabled = false`); остаётся ALC WDSP TXA (атака 1 мс, спад 10 мс, после TX-фильтра) и жёсткий клип на полной шкале. Пики голоса и раньше доходили до полной шкалы (PEP); среднюю мощность голоса поднимает только сжатие — PROC (компрессор TXA).


## 32. Кнопка 20dB сама выключалась; базовое сжатие без PROC (31.10.2026)

* **20dB выключалась при нажатии других кнопок.** В режиме микрофона радио кнопка +ACC Flex — это «20dB». `PhoneCwApplet::syncPhoneFromModel` при каждом обновлении состояния делал `m_accBtn->setChecked(model->micAcc())` (у Flex-поля всегда false) без блокировки сигналов → `toggled(false)` → обработчик записывал `AnanMicBoost = False`. То же снятие отметки в `setSelectableMicInputs`. Теперь в режиме микрофона радио кнопку ставит только `applyRadioMicChoice()` из `AnanMicBoost`.
* **Без PROC — тоже сжатие.** Компрессор TXA на микрофоне радио работает всегда: 6 дБ при выключенном PROC (роль «ALC» обычного трансивера), PROC NOR / DX / DX+ = 12 / 16 / 20 дБ. Стенд, запись оператора, громкие 100 мс / среднее: без сжатия −6,8 / −15,0; 6 дБ −3,4 / −10,8; 12 дБ −2,0 / −7,6. Левелер TXA (15–20 дБ) проверен — даёт лишь +1–2 дБ среднего, не включён.


## 33. Щелчок: данные записи; спад уровня через 0,5 с — трассировка (31.10.2026)

**Данные (лог 11:13–11:29, anan66):**
* `ANAN PTT timeline`: MOX хоста — через 1–6 мс после PTT радио. Вход приёмника RX1 до нажатия — уровень эфира (от −95 до −18 dBFS, 7,2 МГц), через 5–10 мс после нажатия — −95 dBFS (вход отключён), через ~150 мс — утечка собственной передачи (−60…−16). **Скачка на входе приёмника нет — вход пропадает**, как и говорил оператор.
* `txcap-spk.wav` (приёмники): шум −47 dBFS, в момент нажатия — всплеск до −8 dBFS (у колена ограничителя кодека) ~140 мс, затем плавное заглушение (−10 дБ / 10 мс), тишина передачи. MON молчит.
* Значит, щелчок — это реакция цепочки приёма на **резкое пропадание** сильных сигналов эфира (обрыв несущих вне полосы даёт широкополосную ступеньку, попадающую в полосу, где АРУ держит большое усиление по слабому шуму), совпадающее с переключением на передачу. С MOX хост глушит приём до переключения.
* Глушение 250 мс от момента статуса PTT ловит не всё: часть всплеска уже ушла в динамик к моменту, когда статус дошёл до хоста.
**Проверка (anan69):** после PTT радио хост ключится через 60 мс (приёмники в динамике уже заглушены) — если переключение вызывает наш MOX, всплеск окажется внутри глушения. Если щелчок останется — радио переключается само по своей тангенте, и следующий шаг — бит «Orion mic PTT disabled» (байт 50, бит 2).

**Спад уровня через 0,5 с при ровном «аааа».** HL2-ALC выключен (раздел 31), компрессор TXA мгновенный, ALC WDSP — 10 мс. Шкала ALC теперь показывает уровень микрофона после MIC (до компрессора), т. е. её спад — спад самого сигнала микрофона. Для разбора добавлены трассы каждые 250 мс: `ANAN TX trace per 250 ms: raw mic rms […] -> modulator rms […] peak […]` и `ANAN P2 transport: TX IQ mean per 250 ms […]`; длинная передача (≥ 3 с) пишется в `txcap-mic.wav`, короткие — в `txcap-mic-short.wav`, чтобы не затирать.


## 34. Щелчок найден; «ALC у потолка» (31.10.2026, anan70)

**Лог 11:41 (anan69, MOX хоста через 62–69 мс после PTT радио):**
* Вход приёмника RX1 остаётся прежним до +75 мс и пропадает (−95) только через 5–10 мс после MOX хоста → **радио само по тангенте не переключается**, вход отключает наш MOX.
* `txcap-spk.wav` (приёмники): у нажатия — затухающее колебание ~100 Гц (знак меняется каждые 5 мс, ~50 мс) с размахом до −9 dBFS: ступенька постоянной составляющей в звуке приёма, звенящая на нижнем краю фильтра приёма (100 Гц). Она начинается **за ~25 мс до** того, как статус радио сообщает PTT (антидребезг в радио), — поэтому глушение от статуса опаздывало. Второе такое же колебание — от отключения входа нашим MOX (+90 мс) — уже внутри глушения и не слышно. С MOX хоста контакт не замыкается — первого колебания нет вообще.
* **Исправление:** звук на динамик радио задерживается на 80 мс перед отправкой (`m_ioSpkHold`); в момент статуса PTT непосланные 80 мс обнуляются, дальше глушение 250 мс. Задержка звука приёма в динамике радио +80 мс.

**«ALC должна упираться в потолок при ровном аааа».** Трасса: сырой микрофон −28 → −32 dBFS за 2 с (сам голос садится), вход модулятора и IQ повторяют его (IQ −8 → −13). Задержки или спада в нашей цепочке нет. Стенд на этой записи (IQ, среднее по 250 мс): сжатие 6 дБ −8…−17; 12 дБ + левелер TXA (+10 дБ) −5…−8, ровно.
* Микрофон радио: левелер TXA всегда включён (+10 дБ), компрессор TXA 10 дБ без PROC; PROC NOR / DX / DX+ = 14 / 17 / 20 дБ.
* Шкала **TX:ALC** теперь показывает огибающую того, что уходит в DUC (`txEnvelopePeak`, пик |IQ| за 50 мс) — после всей цепочки; раньше это был уровень микрофона до компрессора, который падал вместе с голосом.


## 35. Итог по щелчку; индикатор мощности без сглаживания; зум без сброса FFT (31.10.2026)

* Оператор: щелчков больше нет; причина была и в самом трансивере (он должен был сам глушить звук на начале передачи). Задержка 80 мс и глушение по статусу PTT остаются.
* **Индикатор мощности — как шлёт трансивер** (там уже пиковый детектор): убраны удержание пика в `Hl2Backend` (`fwdPeakHoldStep`, теперь мгновенное значение), сглаживание FWDPWR в `MeterModel` (атака 0,5 / спад 0,15), баллистика стрелки S-метра на передаче (`SMeterWidget::updateNeedleTarget`), сглаживание и PEP-метка шкалы мощности TX-апплета (`forwardPowerRequiresSmoothing = false`).
* **Зум:** при смене размера БПФ (через 400 мс после жеста) новый анализатор начинал усреднение с нуля — след и водопад «обнулялись». Теперь `Hl2Spectrum::seedFrom` переносит в новый анализатор старое усреднение (бины по частоте, мощность × старый/новый размер) и непосланные отсчёты IQ — данные просто продолжают перезаписываться новыми.


## 36. Аттенюатор приёмника на передаче 31 дБ; S-метр после передачи (31.10.2026)

Оператор: после передачи стрелка S-метра дёргается до упора, иногда всплеск доходит до динамиков; в Thetis дуплекс работает без выбросов.
* В пакете Transmitter Specific байты 57..59 (ступенчатый аттенюатор ADC2/ADC1/ADC0 **на время передачи**) были 0 — приёмник слышал собственный передатчик почти без ослабления (в трассе вход RX1 во время передачи −16…−50 dBFS). Thetis ставит там 31 дБ (`SetTxAttenData`, по умолчанию 31). Теперь 31 дБ (`P2Transport::setTxAttenuation`, настройка `AnanTxAttenuationDb`, по умолчанию 31).
* S-метр: пока передача и 300 мс после неё уровень приёма не публикуется, сглаживатель начинает заново — в конвейере ещё уровень собственного передатчика.


## 37. Полоса прокрутки правой панели; Mic-виджет без CW-страницы (31.10.2026)

* Правая панель: вертикальная полоса прокрутки видна всегда (`ScrollBarAlwaysOn`), ширина 12 → 18 px; `kPanelWidth` 260 → 266, чтобы ширина виджетов осталась прежней.
* Mic-виджет (PhoneCwApplet) больше не превращается в CW-страницу при CW: `setMode` всегда показывает страницу микрофона. CW-страница (Delay, Speed, Pitch, Sidetone, L/R, Breakin, Iambic, Hold Delay, APF) — не Flex-управление, а собственный кейер программы (CWX, клавиатура, ключ на COM-порту), её самоконтроль на ПК и приёмные Pitch/APF; она остаётся в коде, но не показывается. Кейер радио — в CW-виджете.


## 38. Самоконтроль CW программы выключен (31.10.2026)

Оператор: «мониторинг ключа через программу не нужен, звук в режиме передачи заблокировать, настройки не нужны». Локальный генератор самоконтроля (`AudioEngine::cwSidetone`, через него звучат CWX, ключ на COM-порту, клавиатура) всегда выключен (`MainWindow_Session.cpp`: кнопка Sidetone и синхронизация с TransmitModel ставят `setEnabled(false)`). CW-страница Mic-виджета не показывается (раздел 37). Самоконтроль трансивера (кейер ПЛИС) — в CW-виджете, не затронут.


## 39. Края спектра (спад фильтра DDC) отрезаны (31.10.2026)

Оператор: обрезать по 10 % с каждого края исходного БПФ, чтобы не было провалов по краям, и чтобы края нигде больше не учитывались.
* `anan::kUsableSpanFraction = 0.8` (AnanUiState.h). Самый широкий вид панорамы — 80 % частоты дискретизации (`Hl2Backend::panViewHz`, предел зума в `setPanBandwidth`); обработчик спектра (`spectrumReady`) первым делом вырезает эту середину из бинов БПФ, так что края не попадают ни в панораму, ни в водопад, ни в AUTO ATT. Полоса панорамы, сообщаемая приложению, — эти 80 %.
* SpectrumWidget: предел зума перетаскиванием по шкале — 80 % частоты, «Zoom ×1.0» = весь полезный участок. Надпись частоты дискретизации не меняется.


## 40. 6 м без автоматического LNA; без кнопок открепления (31.10.2026)

* `legacyAlexHpfFor`: на 6 м (≥ 50 МГц) фильтром приёма выбирался бит 3 «6M Preamp» — LNA включался сам и кнопкой LNA не выключался. Теперь на 6 м — ФВЧ 20 МГц (верхняя секция, пропускает 6 м; фильтр не обходится — оператор), а бит 3 ставит только кнопка LNA (`HighPriorityOptions::alexPreamp`), как на остальных диапазонах. Тест протокола обновлён.
* Кнопка открепления (⧉/↙) убрана из шапок виджетов в панели и в отдельных окнах — вынос и возврат перетаскиванием шапки. Оставлена только на «холсте» (canvas), где возврата перетаскиванием нет.

---

## 41. CW: режим при запуске, тишина вместо IQ, переход на приём (31.10.2026)

- **RX Control показывал USB при восстановленном CWL.** Статический список режимов RxApplet не содержит CWL; при пересборке списка из `modeList` слайса выбирался прежний текст комбо (USB), а не режим слайса. Теперь после пересборки выбирается `m_slice->mode()` (`RxApplet.cpp`, оба места).
- **Голос и «горб» мимо фильтра в CW.** В CW несущую делает FPGA-ключ радио на частоте DUC (= несущая слайса = центр фильтра RX), а всё, что хост шлёт как TX IQ, тоже уходит в эфир: тангента давала голос, а CW-полоса модулятора 300..900 Гц (плюс компрессор/левелер, поднимавшие шум микрофона до −10 dBFS IQ) рисовала сигнал на ~600 Гц от центра. Теперь при TX-приёмнике в CW хост шлёт нули (`m_ananCwTxSilent`, ставится в `ananPushCwKeyer`, который зовётся и сразу при смене режима в `setSliceMode`).
- **Выбросы после передачи.** После отпускания приёмник ещё 178..285 мс слышит свой передатчик. Окно `kAnanUnkeyGuardMs = 350` мс: S-метр молчит (и пока радио само держит PTT), кадры панорамы/водопада не публикуются, по окончании окна история FFT сбрасывается ещё раз. АТТ-AUTO не получает кадры во время передачи.

## 42. Тест перехода на приём (31.10.2026)

Оператор: выброс после окна 350 мс остаётся, «как будто перезапускается весь DSP». Добавлен тест (только запись, поведение не менялось):

- `ANAN unkey test` — строки по 25 мс от −300 мс до +3 с вокруг отпускания для RX1: S-метр (dBm, до защитного окна), максимум и пол панорамы (dBm, средние 80 %), усиление AGC WDSP (dB), пик на входе DSP (нули во время заглушки) и на выходе DSP; `events` — все команды бэкенда в окне 3 с (mute/unmute, сброс истории панорамы, PTT радио, ADC att, частоты, фильтр, AGC, режим, зум/полоса, пересборка DSP, band filter).
- `ANAN unkey IQ` — пик IQ RX1 по 20 мс от −100 до +2000 мс и файл `logs/rxcap-unkey-iq.wav` (float I/Q на частоте выборки, отпускание на 500 мс, длина 3,5 с).
- Стенд WDSP (scratchpad/rxagc): AGC «med», нули 3 с, затем шум −98 dBFS — выход плавно поднимается за ~50 мс, без выброса: нули на время передачи сами выброса не делают. Сильный сигнал −31 dBFS, затем шум — выброс до +12 dBFS на выходе.

## 43. Итог теста перехода на приём; сброс уровней AGC и S-метра (31.10.2026)

Лог 18:17 (CW, 40 м, три передачи):
- IQ от радио после отпускания чистый: −97 dBFS с первых 20 мс, выбросов нет. Радио и протокол ни при чём; DSP не перезапускается (в событиях только mute/unmute, band filter и сброс истории панорамы).
- Во время передачи RX1 видит свой передатчик на −30 dBFS, и примерно 100 мс он доходит до DSP: IQ идёт впереди статуса PTT на ~30 мс, плюс 60–70 мс до заглушки хоста.
- Этого хватает: S-метр после окна 350 мс начинает с −73…−95 dBm и ~1 с спадает до −122 (среднее WDSP, ~0,1 с на e). AGC прижат и восстанавливается до 0,5 с (выход DSP −92 → −46 dBFS).
- Панорама после окна чистая (−111…−115 dBm).

Исправление: патч WDSP 23 `RXAResetLevelHistory()` (`wcpAGC.c`) — AGC и метры RXA начинают как на свежем канале, настройки не трогаются; вызывается в `Hl2RxDsp::setAudioMuted(false)` через `WdspChannel::resetLevelHistory()`. Стенд (scratchpad/rxagc/t2): без сброса S_AV после передачи −108 → −122, со сбросом растёт снизу к уровню шума; звук одинаковый. Тест (раздел 42) оставлен.

## 44. DUP — дуплекс на панораме (31.10.2026)

Кнопка **DUP** в Mic-виджете рядом с MON (ключ `AnanDuplex`). Включена: панорамы не получают состояние передачи (`MainWindow_Session`: `setTransmitting(tx && !duplex)`) — нет зума ±12 кГц на спектр передачи, фиксированной шкалы, TX-маски водопада и пропуска строк после передачи; в `Hl2Backend` нет сброса истории БПФ на фронтах и окна 350 мс после отпускания. Приёмник (RX1 через аттенюатор 31 дБ) виден всё время, свой сигнал — таким, каким его слышит приёмник. Звук приёма на передаче по-прежнему заглушён, S-метр на передаче не показывает (DSP приёма получает нули). Выключена — всё как раньше.

## 45. Тест «тишина, через пару секунд выброс» (31.10.2026)

Лог 18:32 (anan81): в 3 с после отпускания приёмник чистый — IQ −97 dBFS, S-метр −121 dBm с +325 мс, выход DSP −45 dBFS с +100 мс, панорама −114 dBm; звук приёма до микшера в окнах по 5 с с пиком ≤ 0,009. То есть выброс дальше по цепочке или позже 3 с. Тест расширен: бэкенд — до 6 с; PC-звук после клиентской цепочки (AudioEngine: пик и заполнение буфера по 100 мс, какие NR/EQ/gate/comp включены); панорама (SpectrumWidget: принятые кадры и нарисованный след — максимум и медиана, верх/диапазон шкалы, зум/TX, по 100 мс, 6 с).

## 46. БПФ панорамы больше не прерывается на передаче (31.10.2026)

Лог 18:47: с DUP выброса нет; без DUP данные и след чистые, но оператор видит всплеск. Причина (оператор): прерванное БПФ обнуляется и накапливается заново — первые кадры после перезапуска выглядят как всплеск. Убраны все перезапуски:
- `Hl2Backend`: история БПФ не сбрасывается на фронтах ключа, окно 350 мс на панораме снято (S-метр своё окно сохраняет).
- `SpectrumWidget`: под зумом передачи кадры приёмника продолжают усредняться (`m_txZoomRxBins/Smoothed`) и возвращаются при выходе из зума; сглаживание при отпускании на ANAN не сбрасывается.
Свой сигнал после передачи спадает на панораме по усреднению (~4 с при текущем FFT AVG), как в DUP. Строки теста звука ПК — через qInfo (категория aether.audio пишет только предупреждения).

## 47. Приёмник не прерывается на передаче (полный дуплекс) (31.10.2026)

Оператор: приёмник независим от передачи, весь — до звука — работает без перерыва; у передачи свой путь и своё БПФ для экрана.
- Проверено: БПФ передачи отдельное (`anan::TxScope` в `P2Transport`, спектр уходящего DUC IQ); БПФ приёмника — `Hl2Spectrum` каждого приёмника, всегда на настоящем IQ. Общим был только буфер следа в `SpectrumWidget` — с разд. 46 под зумом передачи приёмник усредняется отдельно.
- `Hl2RxDsp`: канал WDSP всегда получает настоящий IQ (раньше на передаче — нули), NB не замораживается, S-метр считается всегда; заглушка — только на выходе звука, плюс 200 мс после (в конвейере ещё звук передачи). Сброс уровней (патч 23) больше не вызывается.
- AGC: удержание на время передачи (патч WDSP 24) на уровне до передачи — минимум за ~400 мс до нажатия (радио ключится само раньше, чем узнаёт хост). Стенд: без удержания звук после передачи −86…−55 dBFS больше 0,8 с (восстановление AGC ~2 с — «тишина, потом выброс»), с удержанием сразу −45 dBFS. Пока AGC удерживается, выход не звучит даже при MON.
- S-метр: на передаче не показывается; после — снова, как только вернётся в пределы 6 дБ от уровня до передачи (не позже 2 с), вместо окна 350 мс.

## 48. Выброс в звуке ПК в начале CW-передачи; AGC восстанавливается, а не держится (31.10.2026)

Лог 19:14 (6 передач, с DUP и без): звук ПК с пиком 0,86 в окнах с каждой CW-передачей ключом (с тангентой — 0,06/0,008). Ключ радио даёт RF сразу; хост ключится на ~65 мс позже статуса PTT, а статус на ~30 мс позже IQ — звук приёма на ПК ничем не глушился до хоста (у кодека радио есть задержка 80 мс и бланк по статусу, у ПК не было). После отпускания по записи txcap-spk звук чистый. Строки теста не выведены: передачи шли чаще 6 с.
- `Hl2RxDsp::pressBlank()` — по фронту статуса PTT на потоке I/O (хук `setRadioPttPressHook`) выход звука приёмников сразу молчит; дальше — заглушка хоста (если хост не ключится — 500 мс).
- AGC больше не замораживается на передаче (замороженное усиление гнало свой сигнал до +49 dBFS внутри цепочки), а работает и после передачи восстанавливается на уровень до неё (патч 24, `RXASetAGCHold(ch, 0, volts)`). Стенд: сразу −44 dBFS после передачи (без восстановления −80…−55 dBFS больше 1 с). Пока идёт передача, выход приёмника не звучит, плюс 200 мс после.

## 49. Стрелка S-метра после передачи (31.10.2026)

Оператор: звук после передачи чистый (anan86: заглушка выхода по статусу PTT на потоке I/O, AGC работает на передаче и восстанавливается после, приёмник не прерывается). Остался скачок стрелки — иногда небольшой, иногда зашкал с медленным спадом.
Причина: среднее S-метра WDSP (0,1 с) слышит свой передатчик и после передачи спадает с −30 dBFS до шума больше 1,6 с; плюс показания, ушедшие до того, как хост узнал о передаче.
- `Hl2RxDsp`: S-метр считается всегда, публикуется только вне передачи (по статусу PTT, заглушке хоста, удержанию и 200 мс после); при начале передачи запоминается показание до неё (минимум ~0,4 с; метр за CW-фильтром с задержкой ~85 мс, так что это ещё эфир), и когда передача ушла из цепочки — AGC и среднее S-метра возвращаются на уровни до передачи (патчи WDSP 24, 25). Стенд: S_AV сразу −127 вместо спада больше 1,6 с.
- `Hl2Backend`: окно «в пределах 6 дБ / 2 с» снято — DSP сам не публикует передачу.

## 50. Накопление БПФ после передачи; плавная стрелка на передаче (31.10.2026)

Оператор: звук и стрелка S-метра в порядке (anan87). Остались (1) свой сигнал на панораме после передачи медленно перетекает в приёмник при длинном FFT AVG; (2) стрелка мощности на передаче скачет между редкими значениями.
- `Hl2Spectrum`: каждые ~100 мс IQ сохраняется копия усреднения (3 штуки); в начале передачи (`markOverStart`, по статусу PTT / ключу хоста) берётся копия старше 150 мс — эфир до передачи; когда передача ушла из цепочки (там же, где S-метр и AGC), `restorePreOver` возвращает её. След продолжается от эфира, без перезапуска с одного кадра и без хвоста своей передачи. Нет копии (усреднение выключено, перестройка) — обычный сброс. Сдвиг панорамы и сброс усреднения стирают копии.
- `SMeterWidget`: на передаче значения радио без обработки, но стрелка между ними движется: вверх τ = 30 мс, вниз τ = 200 мс (приём: 45/180 мс, как было).

## 51. Задержка начала SSB; быстрее возврат БПФ; MON (31.10.2026)

Лог 19:45 (голос, тангента): по таймлайну PTT RF появляется через ~280 мс после статуса PTT радио (хост ключится через 63 мс, дальше ~215 мс конвейера). В протоколе P2 задержка есть только одна — байт 13 Transmitter Specific «RF Delay», и она только для CW-ключа ПЛИС (у нас 20 мс, `P2Protocol.h` `rfDelayMs`); для голоса параметра нет — задержка целиком наша. Сокращено:
- хост ключится сразу по статусу PTT (было +60 мс; приёмники глушит `pressBlank` на потоке I/O);
- в начале передачи микрофон радио: тишина 5 мс + подъём 10 мс (было 30 + 20);
- блок TXA 128 отсчётов (5,3 мс) вместо 512 (21 мс), заполнение очереди DUC перед стартом 2048 (10,7 мс) вместо 6144 (32 мс);
- запуск канала TXA: подъём 5 мс без задержки (было 10 мс задержки + 25 мс подъёма).
Ожидаемо ~280 → ~100 мс от тангенты до RF; проверить по «ANAN PTT timeline».

БПФ: среднее панорамы возвращается через 60 мс IQ после отпускания (раньше — вместе с AGC через 200 мс); частичное окно БПФ сбрасывается. `SpectrumWidget::ananOverEnded()`: 120 мс след держится (до передачи), затем первый кадр берётся целиком; под зумом передачи след приёмника хранится с момента до передачи.

MON: уровень — ползунок рядом с MON в Mic (1..100 → −40..0 дБ), идёт и на ПК, и в кодек радио (`ananMonitorTx`). Запись txcap-spk 19:46 (MON выключен): в поток на динамик радио во время передачи не уходит ничего (приёмники и MON — тишина).

## 52. RF Delay в TX-виджете; голос начинается ровно после него (31.10.2026)

Оператор: RF Delay (байт 13 Transmitter Specific) — общая установка передатчика, по умолчанию 25 мс (готовность T/R); задержка начала голоса привязана к ней и не больше.
- TX-виджет: строка «RF Delay» 1..255 мс (AppSettings `AnanRfDelayMs`, 25). `ananPushCwKeyer` кладёт её в опции, `buildTransmitSpecific` шлёт байт 13 во всех режимах (раньше только с включённым ключом, 20 мс). Тест протокола дополнен.
- `P2Transport::fillTxPacket`: первые RF Delay после MOX — тишина, очередь держит только последние 2048 отсчётов модуляции (свежие, без накопления), затем подъём 2 мс. Голос в эфире — через RF Delay после ключа плюс ~10 мс запаса очереди; хвоста задержки сверх этого нет.
- Микрофон радио в начале передачи: без тишины, подъём 5 мс (задержку держит транспорт).

## 53. Видимые чекбоксы во всех виджетах (31.10.2026)

Оператор (скриншот CW-виджета): квадратики чекбоксов не видны. Причина: свой лист стилей виджета `QCheckBox { color… }` переводит отрисовку на style sheet, у индикатора которого нет рамки на тёмном фоне. `main.cpp`: один стиль на всё приложение — `QCheckBox::indicator` 12×12, рамка #8a96a3, наведение и отмеченный — #00b4d8, отключённый — #4a5560. Виджеты, где индикатор стилизован своим листом, остаются как были.

## 54. TX-виджет почищен; RF delay в панели ANT; MON/DUP в TX (31.10.2026)

- RF delay — из TX-виджета в панель ANT (оверлей панорамы), справа от LNA: поле со стрелками вверх/вниз (QSpinBox), шаг 1 мс, 1..255, по умолчанию 25 (`AnanRfDelayMs`).
- TX-виджет: строка профиля TX + Success/Byp/Mem и кнопки ATU/MEM убраны (собраны в скрытом контейнере — остальной код к ним обращается). Строка кнопок: TUNE, MOX, MON, DUP. MON — `TransmitModel::setSbMonitor`, состояние из `micStateChanged`; DUP — `AnanDuplex`.
- Mic-виджет: кнопки MON/DUP скрыты; строка уровня — «Mon lvl:» (подпись 48, значение 46 px, как TX Low/TX High).

## 55. RF delay по сетке ANT; голос в динамике радио; отправка пакетов (31.10.2026)

- Панель ANT: строка LNA разбита на те же три колонки, что ANT1..3 — LNA под ANT1, подпись «RF delay» под ANT2 (вправо), поле со стрелками под ANT3.
- Голос в динамике трансивера при выключенном MON: поток, который программа шлёт на динамик (порт 1028), во время передачи — нули. Доказательства: «ANAN diag … speaker … peak 0» во всех окнах 20-секундной передачи 19:45:49–19:46:08, запись txcap-spk 19:46 (приёмники и MON — тишина весь TX). Значит, звук возникает в самом трансивере (аналоговый путь микрофон → усилитель кодека/наводка), MON и его уровень на него не влияют. Проверка оператором: заглушить Codec в заголовке окна и передать — если голос слышен, он не из программы; сравнить с Thetis и с выключенным 20dB.
- Пакеты на трансивер: у UDP нет своей буферизации (writeDatagram сразу в стек). Задерживать могла наша отправка по таймеру 1 мс на системном тике Windows 15,6 мс — теперь на время сеанса `timeBeginPeriod(1)` (`P2Transport::start/stop`), датаграммы помечены DSCP 46 (EF). Настройки драйвера сетевой карты (Interrupt Moderation, Energy-Efficient Ethernet, Green Ethernet, Flow Control — выключить) задаются в Диспетчере устройств, программой не меняются.

## 56. Монитор (ПЛИС); строка состояния; PA Level Calibration (31.10.2026)

- Монитор: оператор нашёл в ПЛИС глушение звука на передаче (осталось от поиска щелчка, причина которого была в программе) и убрал его — MON работает.
- Строка состояния: убраны слева точки band stack, +PAN, TNF/CWX/ASR/DVK/FDX и разделитель до названия радио; справа — температура PA и напряжение питания с разделителем. Виджеты созданы в скрытых контейнерах (их обновляет остальной код).
- Настройки → ANAN Hardware → группа «PA Level Calibration»: ползунки 0..100 % для 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6 м (`AnanPaCal_<band>`, по умолчанию 64 % = 162/255, прежние «100 Вт»). Это предел привода на бенде: `applyDrive` = 255 × cal% × √(ползунок%/100) — RF Power и Tune Power при 100 % дают ровно этот уровень. Изменение ползунка или смена бенда применяются сразу (таймер ANAN 300 мс сравнивает «бенд:значение» и вызывает `applyDrive` с последним уровнем). `AnanDriveFor100W` больше не используется. 6 м — ключ бенда «8m» (всё выше 32 МГц).

## 57. Точки band stack возвращены; поле RF delay; Power Meter Calibration (31.10.2026)

- Строка состояния: точки band stack возвращены («от и до исключительно» — первый и последний элементы остаются); скрыты +PAN, TNF/CWX/ASR/DVK/FDX и разделитель.
- Панель ANT: «LNA:» убрана, кнопка LNA в колонке подписей; поле RF delay 58 px («255 ms» + стрелки), текст по центру.
- ANAN Hardware: группы калибровки построены одной функцией (3 в ряд, ползунок 110 px). Новая «Power Meter Calibration»: −50..+50 %, 0 посередине (`AnanPwrCal_<band>`); `ananFwdDisplayWatts` умножает показываемую прямую мощность на (1 + %/100) на текущем бенде, читается при каждом показании. Общий множитель `AnanFwdPowerCal` (48/54 с 29.10) убран из `couplerWatts`: отражённая мощность и КСВ (`couplerSwr` → TX:SWR) считаются из чистых данных.

## 58. Разделитель внизу; стрелка-курсор у меню панорамы; чистка настроек — начало (31.10.2026)

- Строка состояния: разделитель « · » между точками band stack и названием радио.
- Кнопки меню слева на панораме (←, +RX, Band, ANT, Display, Memory …) и все их панели: курсор — обычная стрелка (`SpectrumOverlayMenu` и каждая панель `setCursor(Qt::ArrowCursor)`; раньше наследовали курсор панорамы).
- Чистка настроек (по одной, с проверкой; спорное — вопрос оператору):
  - Radio Setup → Network → Advanced: убраны строки FlexRadio — Enforce Private IP Connections, Network MTU, VITA-49 RX buffer (созданы в скрытом контейнере: на них ссылается `updateRadioCapabilityVisibility`). Группа «Agent Automation (MCP)» оставлена до решения оператора — это функция программы (мост автоматизации для ИИ-ассистента), не Flex.
  - File: меню убрано целиком (оператор). Строится, но не добавляется в строку меню (на его действия ссылается код); Ctrl+Q оставлен окну. Автопроверка обновлений AetherSDR и пункты Help → Check for Updates — решить при разборе Help.
  - Agent Automation (MCP) оставлена: мост автоматизации пригодится для проверки интерфейса ИИ-ассистентом (нужен доступ к оболочке на ПК оператора или локальный Claude Code с tools/aether_mcp.py).

## 59. Калибровка частоты под 122.88 МГц; заголовки разделов не кликаются; чистка Audio/Transmit/Phone & CW/Receive; имена антенн (31.10.2026)

- Calibration: «Effective clock» считался от 76.8 МГц (HL2). Добавлены `Hl2FreqCal::kAnanClockHz = 122.88 МГц` и `effectiveClockHz(ppb, nominal)`; страница, лог и мост автоматизации показывают 122 880 000 Гц × (1+ppb/1e9). Сама коррекция — чистое отношение (`scaleForPpb`), применяется к целым Гц команды NCO до `P2Protocol::phaseWord()` (деление на 122.88 МГц), поэтому от частоты генератора не зависит. Проверка: 14.074 МГц, +1000 ppb → команда 14 073 986 Гц, сдвиг DSP −0.074 Гц, итог −0.006 Гц (шаг фазового слова 0.0286 Гц). Верхний предел своего эталона: 38.4 → 61.44 МГц.
- Дерево настроек: заголовки разделов (RADIO, RECEIVE & TRANSMIT …) — только надписи: фильтр `NavigationHeaderClickBlocker` гасит нажатия/двойные щелчки по ним (раньше щелчок перескакивал на соседнюю страницу, двойной сворачивал группу); `setItemsExpandable(false)`.
- Audio: «Radio Audio Outputs» (Flex line out/наушники) и «Connection» (сглаживание потерь VITA-49, запрет сна) убраны в скрытый контейнер; запрет сна сохраняет своё значение (по умолчанию включён). Recording: строка «Record Mode» (Radio/Client Side) скрыта — запись всегда на ПК.
- Страницы Transmit, Phone & CW, Receive не регистрируются (Flex-тайминги/интерлоки/профили, дубли апплетов, Flex-смещение частоты и 10 МГц). Цифра на передачу у нас есть (DAX/TCI/FreeDV через `submitTxAudio`); группа «Digital» содержала только RTTY Mark (Flex) и переключатель окна RTTY-декодера. Вместе со страницей ушли переключатели декодера CW RX/TX и RTTY Decode — вопрос оператору, куда их перенести.
- Antennas: страница переписана — три порта ANT1–ANT3, имя до 8 символов, хранится в `AnanAntName1..3` (AppSettings; прежнее хранилище Flex привязано к серийному номеру, которого нет до подключения). Имена показываются на кнопках ANT1–3 панели ANT панорамы (RX ANT и TX ANT; длинные — с многоточием, полное имя с портом в подсказке; кнопки равной ширины). Обновление — сигнал `RadioModel::antennaAliasesChanged` → `SpectrumOverlayMenu::syncAnanAntennas`.

## 60. Transmit/Receive переделаны: профили TX, матрицы User Out, декодеры; антенна в строке состояния (31.10.2026)

- **User Out (открытые коллекторы OC1..OC7), по Thetis Penny.cs.** Две матрицы — приём и передача, строки 160 80 60 40 30 20 17 15 12 10 6 м (без вещательных и УКВ), столбцы OC1..OC7. У передачи ещё «действие» на каждый выход: MOX / TUNE / MOX+TUNE (Thetis TXPinActions без двухтонового; по умолчанию MOX+TUNE). Приём — строка диапазона RX1 (приёмник на ADC1), передача — строка диапазона частоты передачи; вне любительских диапазонов (IARU, широкие края) все выходы выключены, как «GEN» в Thetis; при остановке радио — 0. Протокол: High Priority байт 1401 = биты << 1 (спец. «[1] = Open Collector 1», Thetis network.c). Ключи: `AnanOcRx_<band>`, `AnanOcTx_<band>` (маски), `AnanOcTxAction1..7`. Hl2Backend (таймер ANAN) → `P2Transport::setOpenCollectorMatrix` → `openCollectorBits()` (P2Protocol, чистая функция) на каждой отправке High Priority; смена выходов пишется в журнал («ANAN: User Out …»). Тест в `anan_angelia_protocol_test` (строки диапазонов, RX/TX/TUNE, байт 1401) — проходит.
- **Transmit:** «TX Profiles» — список, Save (в выбранный), Save As…, Delete; выбор профиля применяет его сразу. В профиле: RF power, TUNE power, источник микрофона (PC/MIC/TEST), усиление микрофона, +20 dB, PROC вкл/уровень, TX-фильтр low/high, уровень монитора, TX-эквалайзер (вкл + 8 полос). Хранение: `AnanTxProfiles` (JSON), `AnanTxProfile`. Ниже — матрица User Out передачи.
- **Receive:** «Decoders» — CW Decode RX/TX и RTTY Decode (перенесены с убранной Phone & CW; применяются при закрытии окна), ниже матрица User Out приёма. Flex-содержимое Transmit/Receive удалено из кода.
- **Строка состояния:** после имени радио через разделитель — антенна: сверху номер (ANT1..3), снизу имя (если задано). На приёме RX ANT, на передаче TX ANT красным; подсказка с полным текстом. Опрос 250 мс.

## 61. Матрица TX без действий; USB Cables, TX Band Settings, Inhibit during TUNE убраны; выбор COM-порта по-виндовому (31.10.2026)

- User Out — Transmit: строка «Action» убрана, матрица выглядит как у приёма и действует на любую передачу (MOX, TUNE, CW). Бэкенд шлёт всем выходам действие MOX+TUNE; поддержка действий в протоколе (`OcTxAction`) оставлена.
- Radio Setup: страница USB Cables не регистрируется. Меню Settings: USB Cables…, TX Band Settings…, Inhibit during TUNE скрыты (объекты остаются — на них ссылаются `applyCapabilitiesToUi` и сигнал диалога). Для нашего радио Inhibit during TUNE и так ничего не делал (`m_txBandSettings` пуст).
- Выбор последовательного порта (Serial & Controllers → Port Configuration, Peripherals → Serial): на Windows `SerialPortCombo::populate` даёт список COM1..COM32 по номерам — присутствующие с описанием («COM3 - USB Serial Port»), остальные «COMn (not present)», чтобы порт можно было выбрать до подключения устройства; сохранённый COM с номером > 32 добавляется; пункта «Custom…» с путём /dev/… больше нет; сохранённый линуксовый путь заменяется первым присутствующим портом. Проверено на макете (Qt, offscreen).
- Справка оператору: Settings Browser — просмотр/правка базы настроек AetherSDR.db (ключи приложения, станция, документы радио), экспорт для диагностики; AetherRX — настройки клиентского шумоподавления (NR2/NR4/DFNR/RN2/BNR/NNR) в звуковом тракте программы — к нашему приёму применяется.

## 62. Динамик трансивера (кодек) играет звук после AetherRX (31.10.2026)

- Было: кодек получал «сырой» микс приёмников из Hl2Backend (поток ввода-вывода), а AetherRX (NR, gate, EQ, компрессор, tube, exciter, boost/trim) обрабатывал только звук ПК.
- Стало: `anan::AnanCodecFeed` (новый, header-only). AudioEngine после всей цепочки AetherRX и до громкости ПК (громкость ПК — аттенюация устройства) кладёт звук основного источника в FIFO (48 кГц, линейный ресемплер). Кодек-микшер на потоке ввода-вывода (часы радио) берёт оттуда, добавляет MON, громкость Codec и мягкое ограничение — как раньше.
- Два тактовых генератора (звуковая карта ПК и радио): FIFO держит 50 мс, выше 90 мс выбрасывает кадр, ниже 25 мс повторяет кадр; при опустошении (застой потока GUI) — тишина и повторное заполнение, радио при этом получает нули вовремя. Проверено моделью: дрейф ±100..300 ppm, без пропусков и скачков.
- PTT с тангенты: хук нажатия обнуляет содержимое FIFO и следующие 250 мс поступающего звука (помеха контакта приходит раньше статуса). Задержка 80 мс на этом пути не нужна и снята.
- Запасной путь: если AudioEngine не даёт звука > 300 мс (звук ПК выключен), кодек играет прежний сырой микс. FDV — по-прежнему прямой путь.
- Задержка звука в кодеке теперь ≈ задержка звука ПК до выхода цепочки + 50 мс.

## 63. Tools и About; Wideband Monitor по протоколу (01.11.2026)

- Tools: AetherRX перенесён из Settings сразу под AetherTX. Скрыты Pre-tune ATU Bands, Clear ATU Memories, Radio Health (объекты остаются — на них ссылается `updateToolsMenuState`).
- Help: меню не добавляется в строку меню (всё в нём вело на aethersdr.com: руководство, новости, пожертвования, issues, обновления). Автопроверка обновлений AetherSDR и значок UPDATE отключены (`wireUpdateChecker`, `restoreKnownUpdate`). В строке меню вместо Help — пункт «About».
- About: переписан — «Software-defined radio client for the Apache Labs ANAN-100D (openHPSDR Protocol 2)»; версия; блок: Based on AetherSDR (AetherSDR Contributors), Founder — Jeremy Fielder, KK7GWY (основатель и ведущий AetherSDR, GOVERNANCE.md), This version — modified by N7DDC for the ANAN-100D, DSP — WDSP © Warren Pratt NR0V (GPL-2.0+), FreeDV — codec2 © David Rowe VK5DGR (LGPL-2.1), RADE/Opus (BSD), Built with; подвал — © AetherSDR Contributors, modifications © N7DDC, GPL v3, без гарантий, ссылки на LICENSE/THIRD_PARTY_LICENSES. Убраны: Contributor Logbook, ссылки на GitHub/hamqsl, строки про FlexRadio/Icom/D-STAR, renderer.
- **Wideband Monitor** (Tools → Wideband Monitor…, вместо HL2 «Wideband Bandscope»):
  - Протокол: General байты 23..28 (`WidebandOptions`: маска АЦП, 512 отсчётов × 16 бит, период кадра 20..255 мс, 32 пакета на кадр); порты 1027 (ADC1) / 1028 (ADC2); `parseWidebandPacket`. Включается только пока окно открыто (`P2Transport::setWideband`), иначе байты 23..28 нулевые, как раньше.
  - Сборка кадра: 32 пакета подряд от seq 0 (или кратного 32) = 16384 непрерывных отсчёта 122,88 МГц; пропуск — кадр отбрасывается. Thetis каждый пакет по 512 отсчётов отдаёт потоковому анализатору как непрерывный сигнал, хотя между кадрами разрыв, — отсюда его плохая картинка.
  - Окно: БПФ 16384 с окном Блэкмана–Харриса, без постоянной составляющей, шаг 7,5 кГц, 0–61,44 МГц, dBFS на бин (полная синусоида = 0 dBFS; проверено: 14,2 МГц, −6 dBFS → −6,4 с учётом гребешка), усреднение мощности 1..50 кадров, спектр + водопад, пик по пикселю, любительские диапазоны подсвечены, красная метка — активный приёмник. Колесо — масштаб, перетаскивание — сдвиг, двойной щелчок — настроить активный приёмник. Настройки: АЦП, период 20/50/100/200 мс, усреднение, верх/низ шкалы (`AnanWb*`).

## 64. PureSignal: виджет PS, окно Amp View, 2 Tone (01.11.2026)

- About: «Modified by N7DDC (dfainitski@gmail.com) for the Apache Labs ANAN-100D».
- **Протокол (по deskHPSDR new_protocol.c / Thetis):**
  - DDC Specific во время MOX с PS (`PsDdcOptions`): DDC0 = АЦП обратной связи (ADC0), DDC1 = ЦАП передатчика (индекс АЦП = число АЦП, 2), оба 192 кГц / 24 бит, байт 1363 = 0x02 (DDC1 синхронизирован с DDC0), включён DDC0; приёмники DDC2/DDC3 работают дальше (полный дуплекс).
  - High Priority: на передаче DDC0 и DDC1 = фазовое слово TX; Alex0 бит 18 (PS) на передаче, Alex1 бит 18 всегда при включённом PS; вход обратной связи: внутренний ответвитель / EXT1 (биты 9+11) / RX Bypass (бит 11); байт 1443 (АЦП0) на передаче = аттенюатор PS.
  - Transmitter Specific байт 59 (ADC0 на передаче) = аттенюатор PS (`buildTransmitSpecific(..., psFeedbackAttDb)`).
  - Пакеты DDC0 (порт 1035) на передаче: 238 отсчётов, чередуются DDC0 (обратная связь) / DDC1 (ЦАП) → `decodePsPairs` → пары I/Q в `pscc(ch, 1024, tx, rx)` прямо на потоке ввода-вывода (`P2Transport::setPsSink`).
  - Тесты протокола (anan_angelia_protocol_test): DDC Specific, разбор пар, слова DDC0/1, биты Alex, байт 59 — проходят.
- **WDSP** (calcc/iqc уже в TXA): `core/dsp/WdspPureSignal.h` — объявления pscc, SetPSControl/Mox/HWPeak/MoxDelay/TXDelay/LoopDelay/FeedbackRate, GetPSInfo/MaxTX/HWPeak/Disp, PSSave/RestoreCorr, TXA PostGen.
- **`anan::AnanPureSignal`** (GUI-поток, дочерний объект Hl2Backend; `Hl2Backend::pureSignal()`): настройки `AnanPs*`; параметры WDSP (обратная связь 192 кГц, HW peak 0.2899 — значение Thetis для P2, MOX delay 0,2 с, Amp delay 150 нс, Loop delay 0); SetPSMox на каждом фронте ключа (не в CW — несущую делает ключ ПЛИС); командный автомат Thetis (Auto = reset+automode, Single = reset+mancal, Reset, Off с «прокачкой» нулей через pscc, Load = PSRestoreCorr + turnon); автоаттенюатор Thetis (уровень 129..181 норма, цель 152,3; при выходе за пределы — сброс, att += 20·lg(fb/152,3), перезапуск); 2 Tone — пост-генератор TXA (режим 1, 700/1900 Гц, 0,49 каждый, в LSB тоны ниже несущей) через `Hl2TxDsp::setTwoTone`.
- **Проверка на модели** (WDSP под Linux, TXA 24→192 кГц, пост-генератор 2 тона, модель PA AM/AM 1,6r/(1+0,45r²), AM/PM 0,35r²/(1+0,6r²), обратная связь = PA·0,2, эталон = выход TXA·0,2899, pscc по 1024): ИМД3 −20 дБн без PS → −60…−95 дБн с PS за 8 с; уровень обратной связи 169 (норма), коррекция включена. Подтверждён порядок tx/rx и знак фазы для графика.
- **Виджет PS** (кнопка «PS» в ящике кнопок, категория Transmit): PS вкл/выкл, Auto/Single, Calibrate, Reset; **2 Tone** — только USB/LSB (иначе неактивна), ключит MOX через путь TX-виджета (`TxApplet::setMoxFromTool`), сама отпускается, если передача кончилась, режим ушёл из SSB или MOX не включился за 3 с; Amp View…; индикаторы FEED (идут пары), FB n (цвета Thetis: красный <90 < жёлтый ≤128 < зелёный ≤181 < синий), CORR (коррекция применяется), состояние автомата calcc, число калибровок; вход обратной связи, аттенюатор 0..31 дБ (ползунок следует автоаттенюатору), Auto attenuate, HW peak, MOX delay, Amp delay.
- **Окно Amp View** (`PsAmpViewDialog`): графики последней калибровки (GetPSDisp) — AM/AM усилителя (точки), AM/PM усилителя (фаза относительно самых громких 5 %), усиление и фаза коррекции (линии), идеальная линия; шкала фазы автоматическая; «Hold curves»; тесты: 2 Tone, Calibrate, Auto, Reset, частоты тонов и уровень (дБ на тон); сохранение/загрузка файла коррекции (Документы/AetherSDR/PureSignal); статус: состояние, уровень обратной связи, калибровки, аттенюатор, GetPSMaxTX и HW peak, число пар, все info[0..15].
- Проверить на радио: идут ли пакеты DDC0 (в журнале при отпускании «PureSignal feedback packets»), уровень FB при 2 Tone, работа автоаттенюатора; для голоса пик TX ограничен нашим ALC (0,85) — если калибровка на голосе не завершается, сравнить GetPSMaxTX с HW peak в Amp View.

## 65. PureSignal: измерение IMD в Amp View, журнал калибровок (01.11.2026)

- Оператор: после Calibrate побочки уходят вниз и возвращаются вверх; где посмотреть результат.
- Amp View: под кривыми — спектр сигнала с ответвителя (то, что видит PS, DDC0) вокруг двух тонов в дБ относительно одного тона, отметки IMD3..IMD9; ниже — история IMD3 (красная) и IMD5 (оранжевая) за 30 с (4 замера в секунду). Справа группа «IMD»: IMD3/5/7/9 в дБн (худшая сторона каждой пары), частоты тонов, шум рядом. Замер: последние 16384 пары обратной связи (85 мс, `AnanPureSignal::feedbackSnapshot`, кольцо на потоке I/O), БПФ 16384 Блэкман–Харрис, шаг 11,7 Гц, энергия линии ±4 бина; только пока идёт передача с PS.
- Журнал: на каждую калибровку строка «PureSignal: calibration N, feedback, correcting, att, state, info0..3, info6, info12»; срабатывание автоаттенюатора — отдельная строка (сброс коррекции, новый аттенюатор).
- Модель (WDSP, статичный PA): одиночная калибровка держится (ИМД3 −86 дБн 30 с подряд, state STAYON) — сам WDSP коррекцию не теряет. Подозреваемые на радио: автоаттенюатор (уровень FB после калибровки вне 129..181 → сброс коррекции и перекалибровка), дрейф PA (прогрев) при Single, — покажет журнал.

## 66. PureSignal: HW peak по измерению, фиксируемые кнопки, PS в профиле TX (01.11.2026)

- Журнал оператора (10:40): калибровки каждые ~0,3 с, ошибок calcc нет (info0..3 = 0), уровень FB 130..155, автоаттенюатор не срабатывал (17 дБ) — а IMD3 около −20 дБн. Значит, поправка считается, но не та.
- Модель WDSP: ключевой параметр — HW peak (амплитуда DDC1 при полной шкале DUC). iqc ищет поправку по |выхода TXA|, calcc строит её по |DDC1|/HW peak; при несовпадении: реальный 0,40 при заданном 0,2899 → IMD3 −27 дБн (вместо −80…−99 при совпадении); 0,33 → −34; ошибка 5 % → −42; реальный меньше заданного → калибровка не идёт вовсе. Значение 0,2899 взято из Thetis/deskHPSDR, для нашего пути (наша упаковка 24 бит в DUC) не проверено.
- **Измерение HW peak** (P2Transport): пик |DDC1| / пик |IQ, отправленного в DUC| за 0,5 с (только при отправленном пике ≥ −6 дБFS) — точный масштаб независимо от того, включена ли поправка; передаётся в AnanPureSignal вместе с парами.
- **Auto HW peak** (по умолчанию включено): если измерение дважды подряд отличается от настройки больше чем на 1,5 %, HW peak ставится по нему и калибровка перезапускается (строка в журнале). В строке каждой калибровки теперь HW peak и measured. В Amp View: «HW peak X (measured Y)» — красным при расхождении, кнопка «Use measured HW peak». В виджете: «Auto HW peak» и «measured …».
- **Кнопки фиксируются:** в виджете вместо списка Auto/Single — две кнопки-переключателя Auto и Single; Calibrate горит, пока одиночная калибровка не применена (STAYON + поправка); у всех кнопок вид нажатия; в Amp View то же (Auto, Single, Calibrate, 2 Tone красным).
- **PS в профиле TX:** вкл/выкл, Auto/Single, вход, авто-аттенюатор, аттенюатор, HW peak, Auto HW peak, MOX/Amp delay, частоты и уровень 2 Tone — сохраняются в профиль и применяются из него (`AnanPureSignal::applySettings`; без радио — в AppSettings); виджет обновляется (`settingsChanged`). Все настройки PS и раньше запоминались в AppSettings (`AnanPs*`).

## 67. 2 Tone и монитор (01.11.2026)

- Оператор: в MON слышен тихий искажённый сигнал, а не чистый двухтон; предложение формировать 2 тона прямо на передатчике, как тон TUNE.
- Разбор: MON на ANAN всегда играл обработанный микрофон (раздел про MON), а не то, что уходит в эфир; во время 2 Tone передатчик получает тоны пост-генератора TXA, а микрофон просто шумел в MON. Журнал over: IQ в DUC среднее −3,3 дБFS ровно (−4 по 250 мс) — это мощность чистого двухтона 0,49+0,49; 4 пакета недогрузки за 75 с.
- Почему 2 тона идут через TXA, а не как TUNE: поправка PureSignal (iqc) стоит в цепочке TXA после пост-генератора; тон TUNE формируется в транспорте после TXA и поправку не получает — двухтон оттуда PS исправить не смог бы. Так же сделано в Thetis (gen1 TXA).
- Сделано: во время 2 Tone MON играет сами тоны (те же частоты и уровень, 24 кГц) вместо микрофона (`Hl2Backend::ananMonitorTx`, `AnanPureSignal::twoToneOn`).

## 68. MON = то, что уходит на передатчик; CFIR как в Thetis; панорама TX с PS (01.11.2026)

- **MON**: больше не отвод микрофона. Демодулируется ровно тот IQ, что идёт в DUC
  (`AnanTxMonitor.h`: SSB/DIGI/DSB/CW — Re(iq), AM — огибающая минус несущая, FM —
  дискриминатор ±5 кГц; ФНЧ 127 отводов 8 кГц, 192 → 24 кГц), в обработчике `iqReady`
  (поток I/O) → `ananMonitorTx` (PC + кодек). Микрофон, 2 TONE, TUNE, PROC, коррекция PS —
  всё слышно как есть, без переключений по моде/источнику. Синтез тонов anan110 удалён.
  FIFO MON кодека: старт с 40 мс запаса, пере-заполнение при опустошении (нет треска).
- **CFIR** (`SetTXACFIRRun`, Hl2TxDsp::setCicCompensation): Thetis cmaster.cs включает его
  на любом Protocol 2 («always ON with new protocol firmware»). Его усиление в полосе
  1/droop^5 = 0.914 стоит между коррекцией PS и DUC и входит в HW peak 0.2899. Без CFIR
  измеренное DDC1/DUC = 0.324 — те самые 12 %, которые anan110 «подгонял» авто-HW-peak.
  Теперь: HW peak = 0.2899 (Thetis), измерение показывается как DDC1/DUC × 0.914 ≈ 0.296,
  Auto HW peak по умолчанию выключен; настройки и профили без `cfgVer` 2 сброшены на 0.2899.
- **Панорама TX**: DUC IQ при работе PS содержит предыскажение (продукты, гасящие продукты
  усилителя) — это и были «интермоды двухтона». Двухтон верный (постгенератор TXA, как
  Thetis setup.cs chkTestIMD). Пока PS подаёт обратную связь, панорама TX показывает
  DDC0 (сигнал с ответвителя, реальный РЧ-выход), приведённый к уровню DUC.
- **Auto-attenuate**: восстановление как Thetis timer2code — сохраняется, шёл ли Auto или
  незаконченная Single-калибровка; законченная Single-коррекция больше не перекалибруется.
- Single (Calibrate) = снимок: коррекция фиксируется (STAYON) и дальше не следит за
  прогревом PA; Auto калибрует непрерывно.
- Симулятор (pssim/sim4): CFIR + HW peak = отношение×0.914 → IMD3 −62..−82 дБн; с
  отношением без 0.914 калибровка не сходится вовсе (как и ожидалось).

## 69. MON при подключении; классический PureSignal Thetis (01.11.2026)

- **MON**: в логе 12:27 нет ни одной строки «ANAN MON on» — бэкенд всю сессию считал MON
  выключенным. `RadioModel::dispatchTransmitControl` выбрасывает команды, пока радио не
  подключено: нажатие MON до подключения меняло только кнопку. Теперь при подключении
  текущие MON и уровень отправляются заново. В лог: состояние MON при нажатии ключа
  («ANAN MON at key-down: on/off, level, to codec + PC») и что сыграно за over (отсчёты,
  пик) — чтобы было видно, работает ли он.
- **Лог прошивки с исправленной синхронизацией**: HW peak измеренный 0.2956 стабильно
  (Thetis 0.2899), калибровки Auto каждые 0,35 с без ошибок, обратная связь плавно
  151 → 128 за 25 с (прогрев PA, Auto следит), IMD3 −31 стабильно. 1–4 пакета недогрузки
  TX за over: микрофон радио идёт через поток GUI, его задержки голодят TXA (отдельно).
- **Алгоритм**: Thetis 2.10.3.x работает на классическом calcc/iqc (1164 строки), у нас был
  переписанный calcc WDSP 2.10 (2540 строк). Симулятор с PA с памятью (sim5: огибающая
  с памятью, короткая память, шум обратной связи) разницы однозначно не показал (оба
  −42…−58 дБн, разброс большой), поэтому решает проверка на радио.
- `third_party/wdsp/classic/`: calcc.c/h, iqc.c/h из Thetis без изменений, TXA.c/h —
  upstream с классическими интерфейсами calcc/iqc. CMake-опция `AETHER_WDSP_PS_CLASSIC`
  (по умолчанию ON): upstream копируется в каталог сборки, classic поверх. Хост:
  `GetPSDisp` классический (сплайны 16×4, кривая коррекции считается как в xiqc). В логе:
  «PureSignal engine: Thetis classic calcc/iqc» или «WDSP 2.10 calcc/iqc».

## 70. Задержка DDC1 → DDC0 измеряется; Reset как в Thetis (01.11.2026)

- Оператор: MON работает. Calibrate и Reset дают одинаковые −30 (Single), кривая коррекции
  (зелёная) лежит произвольно и не меняется, а должна лечь между «двумя синими линиями».
- Две синие линии = AM/AM открыт в петлю: при росте и спаде огибающей выход разный. Так
  бывает, когда отсчёты опорного DDC1 и обратной связи DDC0 сдвинуты во времени: безынерционная
  коррекция проходит по середине петли и ничего не исправляет. Модель (классический calcc):
  сдвиг 4 отсчёта (21 мкс) → −37 дБн, 8 → −34 (без сдвига −59…−79); тот же сдвиг, скомпенсированный
  `SetPSTXDelay(+8/192000)`, → −79.
- `AnanPureSignal::measureDelay`: раз в секунду при подаче обратной связи — взаимная корреляция
  огибающих |DDC1| и |DDC0| (±79 отсчётов, меньше половины периода биений 1200 Гц), уточнение
  параболой. Знак: + = обратная связь отстаёт, Amp delay = это значение. Проверено на синтетике
  (−12, 0, 3, 8, 40 отсчётов — точно). В лог при изменении > 100 нс; в Amp View строка
  «TX ref -> feedback delay» и кнопка «Use measured delay» (ставит Amp delay и калибрует заново).
- Amp delay: диапазон ±1 000 000 нс (WDSP до ±1024 отсчётов; отрицательный задерживает обратную
  связь).
- Reset: как Thetis — сбрасывает коррекцию; Auto учится заново, Single остаётся без коррекции до
  Calibrate (раньше Reset в Single сразу запускал новую калибровку — выглядел как Calibrate).

## 71. Главная ошибка PS: обратная связь была в зеркальной (сопряжённой) системе (01.11.2026)

- Оператор: задержка 10,8 мкс применена, значения зелёные, график изменился, ИМД3 всё те же −30
  (и так же после Reset).
- Причина: у проводов P2 противоположная «рукость». DDC кладёт сигналы выше своего NCO на
  отрицательные частоты (`Hl2RxDsp`: «the HPSDR wire puts signals above the NCO at negative
  frequency», поэтому спектр там сопрягается), а DUC берёт выход TXA без сопряжения
  (`Hl2TxDsp`: сопряжение перевернуло бы боковую). Значит пары DDC0/DDC1 — комплексно
  сопряжённая копия того сигнала, который исправляет iqc. calcc выучивал AM/PM с обратным
  знаком: амплитуда исправлялась, фаза удваивалась, итог — как без PS.
- Модель (классический calcc, PA с AM/PM): PS выкл −21,0; пары сопряжены −21,2 (лучшее −23);
  в одной системе −79,2. Ровно симптом «Calibrate = Reset».
- Исправление: `P2Transport` сопрягает обе последовательности (DDC1 и DDC0) сразу после
  `decodePsPairs` — calcc, снимки Amp View и панорама TX с обратной связью теперь в системе TXA/DUC.
  Модуль задержки и HW peak от сопряжения не зависят.

## 72. После сопряжения калибровка отклоняется; A/B-переключатель и диагностика (01.11.2026)

- Оператор (anan114): зелёной линии нет, Calibrate остаётся нажатой.
- Лог 13:48: каждая попытка отклонена scheck классического calcc, info[6] = 0x10 («corrected drive
  < 0»: сплайн усиления cm уходит ниже нуля), коррекция не включается — поэтому кривой нет, а
  Calibrate горит (Single не завершилась). В anan113 (13:38, без сопряжения, та же задержка 10,85 мкс)
  калибровки принимались.
- Противоречие: cm строится только из модулей |tx|, |rx| (ym = hw·|tx| / (rx_scale·|rx|)), а модули
  от сопряжения не зависят; задержка WDSP (delay.c) тоже действует на I и Q одинаково. На модели
  сопряжённые и несопряжённые пары не дают отказов. Причина отказов пока не доказана.
- Сделано: переключатель в Amp View «Feedback in TX handedness» (вкл = сопряжение, выкл = как
  в anan113, сброс коррекции при смене) для A/B на радио; расшифровка info[6] в Amp View («Last
  calibration check») и в логе; в лог после каждой калибровки — подогнанное усиление ym по пяти
  полосам выхода x (min..max, число отсчётов) — видно выбросы на малых амплитудах.
- Calibrate: повторное нажатие, пока Single не завершилась, отменяет её (как Thetis
  btnPSCalibrate); действующая коррекция сохраняется.

## 73. Тест прошивки: DDC0 и DDC1 оба на ЦАП (01.11.2026)

- Лог 14:31: с сопряжением (галочка) 5 калибровок приняты, без — отклонены «corrected drive < 0».
  Разница только в полосе малых амплитуд x 0..0,2: ym 0,14..10 против 0,01..35 — провалы
  огибающей двухтона при остаточном сдвиге DDC0/DDC1; приём калибровки на грани.
- Разработчик ПЛИС разрешил DDC0 брать отсчёты ЦАП (ADC = 2), как DDC1, и попросил тест.
- `PsDdcOptions.feedbackAdc` до numAdcs (2); `P2Transport::setPsLoopbackTest` (DDC0 = ADC 2 в
  DDC Specific во время MOX с PS); `AnanPureSignal::setLoopbackTest`; галочка в Amp View
  «Loopback test: DDC0 = DAC» (не сохраняется). Синхронизация DDC1→DDC0, 192 кГц, NCO обоих =
  слово DUC — как в обычном PS.
- В тесте раз в секунду в лог: для целых сдвигов −4..4 остаток |DDC0 − a·DDC1|² / |DDC0|² (a —
  лучший комплексный множитель), лучший сдвиг (+ = DDC0 позже DDC1) и сдвиг по огибающей.
  Совпадение отсчёт в отсчёт = минимум на 0 около −100 дБ.

## 74. −28 дБн при 2 Вт: ИМД3 по трём точкам тракта (01.11.2026)

- Лог 14:59: калибровки принимаются и при Amp delay −63 нс, и при 11531 нс; ИМД3 −28 в обоих
  случаях — компенсация задержки на результат не влияет (на модели давала +40 дБ).
- Оператор: при ~2 Вт (аттенюатор сам стал 4 дБ) без калибровки −28, с калибровкой −35. При 2 Вт
  усилитель на 100 Вт даёт ИМД3 заведомо ниже −45, значит −28 рождаются не в усилителе. Отвод
  обратной связи — резистор 4,7 кОм с выходного трансформатора на вход приёмника; при 2 Вт на входе
  ~0,1 В, ограничение диодами маловероятно.
- Разработчик ПЛИС: ADC = 2 не может быть до интерполятора (DDC принимает действительный поток
  122,88 МГц). Принято; гипотеза раздела 73 снята. Тест DDC0 = DDC1 закрывает путь от мультиплексора
  до пакета, но не путь АЦП0 → мультиплексор и ЦАП → отвод ADC 2; задержка плывёт в течение over
  (10,1 → 11,8 мкс), на 10 м 8,75 мкс против 10,85 на 40 м.
- `AnanTwoTone.h`: ИМД двухтона по записи (Hann, Гёрцель, знак боковой из данных). Раз в секунду
  при 2 Tone в лог «PureSignal 2-tone chain: host DUC IQ … | DAC data (DDC1) … | coupler (DDC0) …»
  (ИМД3/ИМД5, уровень тона, баланс) — где появляются продукты: до радио, в ПЛИС/ЦАП или после.
  `P2Transport::ducSnapshot` (кольцо 16384 отсчётов DUC). В Amp View строка «2-tone IMD3 along the
  chain». Проверено на синтетике (−40 дБн задано → −40,0 измерено, USB и LSB).

## 75. Результат измерения по трём точкам: искажения между ЦАП и АЦП0, усиление растёт с амплитудой (01.11.2026)

- Лог 20:02–20:04, 40 м, drive 18…36, 2 Tone: host DUC IQ −130 дБн; DAC data (DDC1) −79 дБн
  (это пол 24-битного DDC); coupler (DDC0) −25…−27 дБн, ИМД5 −36…−40. При коррекции host и DAC
  data показывают −26 (предыскажение, так и должно быть), coupler −32. Цифровой тракт чист,
  источник — между ЦАП и АЦП0.
- Подогнанное усиление ym (= опора/обратная связь, нормировано к 1 на полной шкале) монотонно
  падает с амплитудой: 1,35 при x 0,2–0,4; 1,19 при 0,4–0,6; 1,10 при 0,6–0,8; 1,04 при 0,8–1,0.
  ym > 1 внизу = выход меньше линейного на малых амплитудах = усиление усилителя РАСТЁТ с амплитудой
  примерно на 2,6 дБ от 0,3 до 1,0. Это не компрессия (там ym < 1 внизу), а расширение —
  характеристика недосмещённого класса AB / класса C (crossover). Согласуется с: ИМД3 почти не зависит
  от мощности (−25 при 2 Вт, −28 при 46), в полосе 0–0,2 ym до 35 (выход почти ноль), отказы
  калибровки «corrected drive < 0» (обратная кривая к расширению имеет крутой участок у нуля),
  рост выхода на 5 дБ за 10 с при неизменном drive (прогрев при малом смещении).
- Проверить аппаратно: ток покоя PA при нажатом ключе без модуляции; выдаёт ли прошивка сигнал
  включения смещения PA при бите PA enable (General байт 58 бит 0) — на Angelia смещение включается
  только во время TX транзисторным ключом от ПЛИС.

## 76. Другой трансивер (проверен с Thetis): задержка ~0, ИМД −25 → −32; проверка «рукости» (01.11.2026)

- Оператор: прежний трансивер неисправен (Thetis на нём тоже не работал). Другой, проверенный с Thetis
  (Calibrate/Reset дают большую разницу), у нас тоже не работает. Ток покоя PA 1 А на оба транзистора —
  недосмещения нет; выводы раздела 75 о PA относились к неисправному экземпляру.
- Лог 20:02 — этот трансивер: задержка DDC1→DDC0 измерена 0…0,6 мкс (11 мкс были неисправностью
  прежнего); host −130, DAC data −79, coupler без коррекции −25…−27, с коррекцией −32; калибровки
  принимаются. ym растёт к низу (1,35 при x 0,2–0,4).
- Добавлено: `TwoToneImd.side` — на какой стороне частот лежат тоны; в лог раз в секунду при 2 Tone
  «PureSignal handedness check: OK/MISMATCH -- host IQ tones at ±f, PS pairs (as fed to calcc) at ±f/±f»:
  iqc работает в системе host IQ, calcc обязан видеть пары в той же. Это прямая проверка сопряжения
  раздела 71 на исправном радио.

## 77. PureSignal работает: −45 дБн; сопряжение пар (раздел 71) было ошибкой (01.11.2026)

- Оператор на исправном трансивере: с галочкой (сопряжение) −32, без галочки −45 дБн — как в Thetis.
- Лог 20:19–20:20, «handedness check»: с сопряжением MISMATCH (host IQ тоны на +f, пары на −f),
  без — OK (+f/+f). Цепочка без сопряжения, коррекция включена: host −25,4 (предыскажение), DAC data
  −25,4, coupler ИМД3 −45,3 / ИМД5 −64,2 дБн.
- Вывод: пары DDC0/DDC1 PS уже в системе host TXA IQ. Рассуждение раздела 71 (DDC приёмников кладёт
  сигнал выше NCO на −f, значит PS-пары сопряжены) неверно для PS-DDC, настроенных фазовым словом DUC;
  на неисправном радио A/B ничего не показывал, отсюда ошибочный вывод.
- Исправлено: P2Transport больше не сопрягает пары; в AnanPureSignal флаг стал диагностическим
  «conjugate» (по умолчанию выкл., ключ AnanPsConjugate, старый AnanPsTxaDomain не читается); галочка в
  Amp View «Conjugate feedback pairs (diagnostic)». Панорама TX с обратной связью теперь не зеркальна.
  Проверка «handedness check» в логе остаётся.

## 78. Двухтон в боковой фильтра; тестовые органы управления заменены константами (01.11.2026)

- Двухтон шёл в противоположной фильтру боковой: генератор WDSP (gen.c, Q = −sin) кладёт
  положительную частоту на отрицательную полосу, а TXA этого хоста держит USB на +f. Теперь
  USB → частоты генератора −700/−1900, LSB → +700/+1900 (`Hl2TxDsp::applyTwoTone`).
- Константы (`forceConstants` в AnanPureSignal, применяются при загрузке и из профилей TX): HW peak
  0.2899, MOX delay 0,2 с, Amp delay 150 нс (Thetis P2), автоматический HW peak выкл., пары без
  сопряжения, тоны 700/1900 Гц по 0,49.
- Виджет PS: убран раздел Settings (HW peak, Auto HW peak, MOX delay, Amp delay). Amp View: убраны
  «Use measured HW peak», «Use measured delay», «Conjugate feedback pairs», «Loopback test», поля
  тонов и уровня (вместо них подпись). Остались: PS, Auto/Single, Calibrate, Reset, 2 Tone, вход
  обратной связи, аттенюатор, Auto attenuate, Hold curves, файлы коррекции; измерения (задержка,
  HW peak, ИМД по тракту, проверка calcc) — только для чтения. Логи цепочки ИМД и «handedness» раз
  в 5 с, задержка — при изменении > 1 мкс.
- График Amp View: синие точки (AM/AM усилителя) ниже диагонали — компрессия; зелёная (коррекция)
  выше — обратная функция, зеркальна синей относительно диагонали; их композиция — диагональ. Так и
  задумано (как AmpView Thetis); то же для фазы (оранжевая и фиолетовая зеркальны относительно 0°).

## 79. Полная автоматика задержки и HW peak; движок WDSP 2.10; знак двухтона возвращён (01.11.2026)

- Оператор: на большой мощности −38; задержка красная (3206 нс); двухтон на панораме всё ещё по
  другую сторону фильтра.
- Двухтон: возвращён знак как у полосы TXA (USB +, LSB −, как Thetis). В anan121 знак перевёрнут
  ошибочно: жалоба на сторону относилась к панораме TX с обратной связью anan114–119, которая была
  сопряжена (зеркальна). Лог 20:35 (LSB, anan121): host и DDC1 оба на −f, т. е. двухтон перевёрнут
  относительно речи той же моды (полоса LSB TXA и генератор WDSP в одной системе знаков).
- Автоматика (`forceConstants`): HW peak стартует с 0.2899 и следует измерению (DDC1/DUC × CFIR,
  > 1,5 % два раза → установить и перекалибровать); Amp delay стартует со 150 нс и следует измеренной
  задержке DDC1→DDC0 (сглаживание 0,3, расхождение > 300 нс три измерения подряд → установить, без
  сброса — следующая калибровка с ней). Значения не берутся из профилей TX и каждую сессию начинаются
  с умолчаний.
- Движок PS: CMake-строка `AETHER_WDSP_PS_ENGINE` = `wdsp210` (по умолчанию) | `classic`; прежний
  bool `AETHER_WDSP_PS_CLASSIC` из кэша больше не читается, так что смена умолчания доходит до
  существующего каталога сборки. Проверено конфигурированием обоих вариантов.

## 80. Amp View: компоновка; оценка −40 дБн (01.11.2026)

- По скриншоту оператора: правая панель фиксированной ширины 380 (текст статуса/ИМД больше не
  растягивает окно — устраняет «дёргание»), статус выровнен вверх-влево с переносом, info[0..15]
  убраны, статус сокращён до 6 строк (режим/состояние; коррекция, калибровки, проверка; уровень
  обратной связи и аттенюатор; HW peak + измеренный; Amp delay + измеренный; ИМД3 по тракту).
  В блоке ИМД — ИМД3 отдельно для нижнего и верхнего продукта (асимметрия = эффекты памяти).
- Скриншот (большая мощность, WDSP 2.10, Single): ИМД3 −40,1, ИМД5 −63,6, ИМД7 −66,6, ИМД9 −63,5,
  шум −84 дБн; HW peak 0.2957 (изм. 0.2957), задержка изм. 2428 / уст. 2640 нс; AM/PM ≈ 4° по
  диапазону, точки AM/PM в две тонкие ветви (небольшой гистерезис).

## 81. Тест разноса тонов (01.11.2026)

- Оператор: нижний и верхний ИМД3 одинаковы (асимметрии нет — типичного признака эффектов памяти
  нет). Тест разноса: в Amp View выбор пары тонов «700/1900 Гц (1200)» или «1200/1500 Гц (300)»,
  −6,2 дБFS каждый; при смене очищается история ИМД. Поиск второго тона в измерении — от 200 Гц.
  Пара при запуске снова 700/1900 (forceConstants).

## 82. ИМД3 зависит от разноса тонов: −40 (1200 Гц) / −52 (300 Гц); подбор задержки по ИМД (01.11.2026)

- Остаток в 4 раза меньше при разносе в 4 раза меньше (12 дБ) — пропорционален разносу. Так ведут
  себя и ошибка времени между опорой и обратной связью, и память усилителя первого порядка;
  нижний и верхний ИМД3 одинаковы (асимметрии, типичной для памяти, нет).
- Задержка по корреляции огибающих на сжимающем усилителе не точна, поэтому подбор по результату:
  `AnanPureSignal::startDelaySweep` (кнопка «Optimize delay» в Amp View, только 2 Tone + Auto):
  Amp delay = измеренная + {−8, −5, −3, −1,5, 0, +1,5, +3, +5, +8} мкс, каждое 3 с установка +
  3 с усреднение ИМД3 выхода (по мощности), лучшее смещение (если лучше нуля > 1 дБ) становится
  поправкой (AnanPsDelayTrimNs, сохраняется), автоследование добавляет её. В лог таблица
  «offset : IMD3». Статус показывает ход и поправку. Если ни одно смещение не помогает — предел
  усилителя (память).

## 83. Автоподстройка задержки только по широкому двухтону (01.11.2026)

- Логи 21:12 и 21:24: «Optimize delay» не запускался (строк «delay sweep» нет). На 300 Гц (21:13)
  оценка задержки по огибающей гуляла от −1 до −8 мкс (биения 3,3 мс — пик корреляции широкий), и
  автоследование увело Amp delay в −6,6 мкс; ИМД3 при этом −52. На 1200 Гц: измерено 2,0–2,4 мкс,
  ИМД3 −40,5…−41.
- Автоследование теперь только при двухтоне с разносом ≥ 1000 Гц (или по речи) и в пределах ±20 мкс.

## 84. Подбор задержки: ИМД3 не зависит от неё в пределах ±8 мкс; Wideband Monitor — диагностика (01.11.2026)

- Лог 21:30: «Optimize delay» прошёл 8 из 9 шагов (оператор остановил передачу на последнем, таблица
  при прерывании не писалась). По строкам цепочки ИМД (раз в 5 с) выход: −8 мкс −39,8; −5 −40,4;
  −3 −40,4; −1,5 −40,2; 0 −40,4; +1,5 −40,3; +3 −40,3; +5 −40,3 дБн. ИМД3 не зависит от задержки
  даже при сдвиге на 1,5 отсчёта — остаток −40 на разносе 1200 Гц не связан с выравниванием
  опоры и обратной связи; вместе с разделом 82 (−52 на 300 Гц, нижний = верхний) — это память
  усилителя, предел безынерционной коррекции.
- Подбор: шаг 2,5 + 2,5 с (~45 с); при прерывании в лог частичная таблица.
- Wideband Monitor: в логе 21:30 ни одного обращения к потоку (нет «ANAN: wideband on»). Пункт
  Tools → Wideband Monitor... теперь включается и по connectionStateChanged (не только по
  capabilitiesChanged); в лог: открытие окна, подключение к транспорту, первые пакеты по АЦП, первый
  кадр.

## 85. Задержка — один раз, на малой мощности, без коррекции; уровень ОС (01.11.2026)

Совет разработчика ПЛИС: (1) задержку мерить один раз в линейной области на малой мощности и
фиксировать, всё амплитудозависимое — в таблицы AM/AM, AM/PM; (2) держать уровень ОС постоянным
аттенюатором на передаче с запасом от перегрузки АЦП.

- Подтверждение п.1 в логе 21:30: первая оценка при выключенной коррекции 1943 нс, после включения
  коррекции автоследование ушло в 2020 → 2360 нс. При коррекции DDC1 — уже предыскажённый сигнал,
  корреляция огибающих «предыскажение ↔ выход» смещается и плывёт с мощностью.
- Сделано: автоследование задержки убрано. Кнопка «Measure delay» (вместо «Optimize delay» —
  подбор по ИМД3 ответа не дал, раздел 84, и это как раз переоценка на большой мощности):
  сброс коррекции (SetPSControl 1,0,0,0), 0,7 с, ждём info[14] = 0, 5 отсчётов задержки
  (корреляция > 0,95), медиана; отказ при разбросе > 300 нс или сжатии > 1 дБ на пике (усиление
  ОС/опора при 0,85–1 от пика огибающей против 0,25–0,5). Результат 10 нс-кратный в
  AnanPsDelayMeasuredNs (+ дата, сжатие), загружается каждый сеанс; без измерения — 150 нс
  (Thetis) с жёлтой подсказкой. После измерения — возврат Auto / новая Single. Двухтон 300 Гц
  отклоняется (биение слишком медленное). Старые ключи AnanPsAmpDelayNs (значения автоследования)
  и AnanPsDelayTrimNs больше не читаются; из файла не удаляются.
- На время измерения отключены автослежение HW peak и автоаттенюатор.
- П.2 уже был (Thetis timer2code: окно 128..181, цель 152, только после новой калибровки); в логе
  21:30 ОС 177 → 157 при att 12 дБ — в окне, перегрузок ADC0 при передаче нет. Медленный спад
  ОС на ~1 дБ за 25 с при постоянном драйве — прогрев оконечника (тепловая память); Auto
  перекалибровывает каждые ~1,5 с.

## 86. «Not in the linear region» — это расширение усиления класса AB, а не перегруз (01.11.2026)

- Лог 21:58: три попытки на разных мощностях (att 11 → 9 → 7 дБ) — показатель всегда +2,0 дБ,
  со знаком «плюс»: усиление на пике БОЛЬШЕ, чем на середине огибающей. Это расширение, а не
  сжатие; кривая calcc ym подтверждает (1,3–2,0 при 0–0,2 от выхода, 1,0 на пике — у малых
  амплитуд усиление ниже, ступенька класса AB). От мощности не зависит, поэтому снижением мощности
  не уходит; критерий |сжатие| ≤ 1 дБ был неверен по смыслу.
- Теперь отказ только при СЖАТИИ на пике > 1,5 дБ (насыщение); расширение пишется в результат.
  Разброс до 400 нс (было 300; во второй попытке 283).
- Задержки трёх попыток: ~1430, ~1040, ~770 нс — снижаются вместе с мощностью/ступенью
  аттенюатора; рабочая коррекция при этом была на 150 нс и давала −45…−48 дБн.

## 87. Wideband Monitor работает с ADC1, с ADC2 пакетов нет (01.11.2026)

- Лог 22:02: окно открылось, подключилось, «wideband on mask 1» → пакеты с порта 1027 (ADC1),
  первый кадр 16384 отсчёта — работает. «mask 2» (ADC2) в 22:02:58 — за 3 с ни одного пакета
  с порта 1028 (строка «arriving from ADC 2» не появилась).
- Приём сверен с Thetis network.c: порт 1027 + adc_id (1027 = ADC0, 1028 = ADC1), 1028 байт,
  сброс кадра на seq 0 — у нас то же. Маска в байте 23 General, как в Thetis (wb_enable).
  Значит, вероятно, прошивка не отдаёт широкополосный поток второго АЦП (или шлёт его иначе).
- Диагностика: в строке «ANAN diag» (раз в 5 с) счётчики пакетов широкополосного размера по
  портам 1027/1028 (до проверки маски), кадров по АЦП и пакетов того же размера с других портов.

## 88. Задержка по диапазонам, один двухтон, выбор АЦП обратной связи (01.11.2026)

Решение оператора: память задержки на каждый диапазон, по умолчанию 600 нс; переключатель пар
тонов убрать (только 700/1900); выбор АЦП для отбора мощности (ADC1 / ADC2). По широкополосному
потоку разработчик ПЛИС нашёл ошибку (порт 1028 не менялся при ADC2) и меняет прошивку — мы
следуем протоколу, без изменений у нас.

- Задержка: AnanPureSignal::setTxFrequency (из Hl2Backend::setTxFrequency, с XIT) → диапазон
  160…6 м (вне диапазонов — по МГц). Ключи AnanPsDelayNs_<band>, AnanPsDelayAt_<band>,
  AnanPsDelayCompDb_<band>; без значения — 600 нс («default» жёлтым). «Measure delay» пишет в
  ячейку текущего диапазона; смена диапазона во время измерения его прерывает. Одиночный ключ
  anan128 (AnanPsDelayMeasuredNs) не используется — у него нет диапазона.
- Пары тонов: комбобокс удалён; forceConstants и так держит 700/1900/0,49.
- АЦП обратной связи: Settings.feedbackAdc (AnanPsFeedbackAdc), «ADC:» в PS-апплете.
  P2Transport::setPsFeedbackAdc: DDC0 при PS-передаче на ADC index 0/1 (DDC Specific байт 17);
  аттенюатор PS — на выбранный АЦП: High Priority (ADC0 / ADC1 attenuation) и Transmit Specific
  байт 59 (ADC0) или 58 (ADC1); остальные — AnanTxAttenuationDb. Тест протокола дополнен.

## 89. Аттенюатор обратной связи — своё значение для каждого АЦП (01.11.2026)

- Разработчик: значение аттенюатора ADC1 к ADC2 применять нельзя (уровни на входах разные).
  Один ползунок «Attenuator» управляет аттенюатором выбранного АЦП; значения запоминаются
  отдельно: AnanPsAttDb (ADC1, прежний ключ) и AnanPsAttDbAdc2. Settings.attDbByAdc[2], attDb —
  активное. Смена АЦП: текущее значение сохраняется в ячейку старого АЦП, загружается значение
  нового, автоаттенюатор возвращается в Monitor, коррекция перезапускается (другой тракт ОС),
  апплет перечитывает ползунок (settingsChanged). Автоаттенюатор пишет в ячейку активного АЦП.

## 90. Скачки искажений: автоаттенюатор по устаревшему уровню и перегруз АЦП (01.11.2026)

Лог 22:34. Рост искажений и «перекалибровка» совпадают со срабатываниями автоаттенюатора;
каждое — сброс коррекции (Thetis timer2code), отсюда провалы до −24 (коррекция выключена).
- Устаревший уровень: калибровки 113 и 114 — обе «feedback 29», почти одинаковые кривые; 114
  через 190 мс после смены 31 → 17 дБ, т. е. по отсчётам ещё старого аттенюатора. Автомат шагнул
  17 → 3 дБ, АЦП в перегрузе (feedback 347), 8 с без новой калибровки, IMD3 −13,8 дБн.
  То же «качание» в 22:35:30: 115/184 при одном аттенюаторе (8 ↔ 12 дБ).
- Исправление: (1) уровень после смены аттенюатора принимается только со второй калибровки
  и не раньше 400 мс; (2) перегруз АЦП обратной связи (статус байт 5, бит 0/1, защёлка
  P2Transport::takeOverloadLatch) — сразу +6 дБ (или больше по уровню), не дожидаясь калибровки.
- Input (Alex0 при PS-передаче): Internal — бит PS (18), вход ADC1 на ответвитель;
  EXT1 — + EXT1 (9) + Bypass (11); RX Bypass — + Bypass (11). Только перед ADC1; при ADC2
  (голый вход RX2) список неактивен.

## 91. «Прыгает жёлтая линия» на средней мощности (01.11.2026)

Лог 22:47: автоаттенюатор после раздела 90 спокоен (одно срабатывание в начале, ОС 151 → 138
плавно), IMD3 выхода по 5-секундным строкам ровно −39…−38,3. 4 TX underflow за передачу 67 с.
- Шкала фазы в Amp View: span считался по максимуму |фаза| ВСЕХ точек AM/PM, включая самые
  малые амплитуды (в «мёртвой зоне» класса AB ym внизу до 15–21, выход почти ноль — фаза там
  шум до ±180°). Span скакал между калибровками (каждые ~0,3 с), вся жёлтая кривая
  перемасштабировалась — визуальный «прыжок». Теперь точки со входом < 0,15 в масштаб не
  входят (рисуются как раньше), шкала растёт сразу, сжимается после 5 калибровок ниже 60 %.
- Диагностика реального скачка (искажения «в такт»): каждая TX underflow — строка с моментом
  (DUC нули до повторного заполнения — провал на выходе, калибровка по нему); раз в секунду
  скачок IMD3 выхода > 3 дБ — строка «IMD3 jump» с номером калибровки; к строке «fitted gain»
  добавлены фаза усилителя по 5 полосам и диапазон фазы коррекции.

## 92. Щелчки = TX underflow; защита калибровки от провалов (01.11.2026)

Лог 22:55: 6 underflow за передачу 44 с (22:58:31.6, 37.4, 48.4, 22:59:02.4, 06.4, 10.4; в очереди
32…208 из 240 отсчётов пакета). Скачки IMD3: 22:59:02.0 выход −38,8 → −26,6 при «correcting 0»
и DAC −129 (коррекция в этот момент не применялась), 22:59:04.0 DAC −49,9 (снимок наполовину
без предыскажения); перед этим 22:59:00.8–02.2 — секундный простой в журнале калибровок и
перезапуск RX-аудио (AudioEngine «no audio data … restarting RX»).
- Защита: в AnanPureSignal::feed блок 1024 пары, где в опоре (DDC1) ≥ 64 подряд отсчётов
  < 1e-3 HW peak (точные нули DUC — провал на передатчике, не пауза речи), и три следующих
  (~21 мс: подъём после повторного заполнения, установление усилителя) в pscc не передаются.
  Коррекция остаётся прежней, калибровка по провалу не строится. Строка в логе на каждый провал.
- Причина щелчков: запас очереди DUC 2048 отсчётов (10,7 мс, два блока TXA) — мало для
  разброса доставки модуляции; поднят до 4096 (21,3 мс, +10,7 мс задержки).

## 93. Передача и динамик в своих потоках, синхронно с радио (01.11–02.11.2026)

Задача оператора: «все отправки-приёмы пакетов синхронизированы с устройством; передача и
спикер — каждая в отдельном потоке с максимальным приоритетом; ориентир — Thetis».

Что было: микрофон радио шёл I/O-поток → GUI-поток (лямбда Hl2Backend: ФВЧ, усиление,
нарастание) → I/O-поток (TXA); пакеты DUC отправлял таймер 1 мс по часам ПК. Любая заминка
GUI (перерисовка, перетаскивание окна, перезапуск звука) опустошала очередь DUC — щелчки в
эфире (раздел 92). Динамик радио играл звук AudioEngine (после AetherRX), который тоже идёт
через GUI-поток; коррекция дрейфа (выброс/повтор одного кадра при выходе глубины из 25..90 мс)
реагировала на неровную доставку — шелест и щелчки.

Сделано:
- Поток «anan-tx» (TimeCritical, на Windows ещё MMCSS «Pro Audio»): AnanTxEngine + Hl2TxDsp.
  Пакет микрофона радио (64 кадра 48 кГц, порт 1026) — из I/O-потока прямо в движок:
  ФВЧ 120 Гц / тест-двухтон, индикатор, нарастание 5 мс, MIC gain, захват txcap-mic.wav,
  статистика передачи → TXA (processAudioBlock, тот же поток) → iqReady (Direct) → очередь
  DUC → P2Transport::txDeviceTick(256): пакет микрофона = 256 отсчётов DUC времени радио,
  уходят пакеты DUC, положенные к этому моменту (+ запас 6 пакетов). Модулятор и DUC идут в
  ногу по построению, уровень очереди не плывёт. Запас очереди: 1536 при микрофоне радио
  (блок TXA 1024 + 2 пакета), 4096 при звуке с ПК (его часы).
- Без потока микрофона > 30 мс — подменяет таймер 2 мс по часам ПК; «кредит» не даёт
  посчитать время дважды, когда опоздавшие пакеты микрофона приходят пачкой.
- Отправка DUC и динамика — sendto() по дескриптору сокета сессии (AnanNativeUdp: тот же
  локальный порт, без объектов Qt; shared_mutex на время закрытия сокета). ws2_32 в CMake.
- Динамик: поток «anan-spk» (TimeCritical), пакет 64 кадра уходит сразу, как готов (как
  Thetis); звук — смесь приёмников в I/O-потоке (по IQ радио, его часы). AetherRX на динамике
  радио — по ключу AnanCodecAetherRx (по умолчанию выкл., т.к. через GUI); у AnanCodecFeed
  убрана покадровая коррекция, при нехватке — затухание, обрезка только при > 250 мс.
- MON на кодек — из потока передачи прямо в FIFO микшера (ananMonToCodec), на ПК — как было.
- FreeDV-передача: блок через GUI (libfdv однопоточный), обратно в TXA — как раньше.
- Hl2Backend: настройка TXA при подключении — на потоке передачи (BlockingQueued из I/O);
  dspChains собирает RX на I/O, TX на своём потоке; деструктор: I/O → TX → удаление.
- ClientComp для микрофона радио не перенесён: он был всегда выключен (PROC = компрессор TXA).

Проверка на стенде (поддельное радио на 127.0.0.2, часы радио на 0,5 % быстрее ПК):
DUC/ожидание по часам радио 1,0001 / 1,0003 / 0,9999, ни одного underflow, ни одного нулевого
пакета; с остановкой потока радио на 60 мс — один провал (неизбежен) и восстановление; после
отпускания DUC не идёт; чистое завершение потоков. Сборка под MSVC и проверка на радио — у
оператора.

Независимая проверка кода (до отправки) — исправлено:
- запас очереди выбирается по источнику: 1536, когда движок сам модулирует микрофон радио;
  4096 (держится 1 с), когда TXA кормит звук с ПК (DAX/TCI/WSPR/микрофон ПК) или FreeDV;
- флаг FreeDV-передачи обновляется сразу при нажатии/отпускании, не раз в 50 мс;
- потерянные пакеты микрофона (разрыв номера ≤ 16) заменяются тишиной — время радио не
  теряется (стенд: 12 потерь, отношение DUC 1,0000); строка diag «mic lost»;
- переход на подмену часами ПК берёт всё время с последнего пакета микрофона;
- поток передачи отстал больше чем на 16 пакетов (21 мс, больше FIFO DUC) — это время не
  досылается пачкой, лог «transmit thread behind the radio»; меньше — досылается;
- m_psEnabled / m_txHz пишутся под m_txMutex; интервалы по монотонным часам (steady_clock);
  ошибки sendto() считаются (diag «native send fails»).

Не сделано (не критично, по словам оператора): замирание приёма при перетаскивании окна.
Звук на ПК и панорама идут через GUI-поток (mixReceiverAudio → RadioModel → AudioEngine);
обход GUI для звука ПК затрагивает общие для всех радио PcmFrame/сессии, FreeDV и MON —
отдельная работа. Динамик радио от GUI больше не зависит.

## 94. Автоаттенюатор шагами 1 дБ без сброса; состояние буферов радио (02.11.2026)

Лог 17:41: underflow ноль. Всплеск и «перекалибровка» в 17:42:45 — шаг автоаттенюатора:
ОС за минуту сползла 160 → 128 (прогрев), timer2code Thetis сбросил коррекцию и прыгнул
12 → 10 дБ — ~1 с без коррекции (−21 дБн). Три «TX dropout in the reference» посреди
передачи без underflow у нас — опустошение FIFO DUC в ПЛИС (пакеты опоздали за запас 7,5 мс).

- Автоаттенюатор (просьба оператора): штатное слежение шагом 1 дБ БЕЗ сброса, держит ОС в
  140..165 (152 ±0,7 дБ), не чаще раза в 2 калибровки и 400 мс. Обоснование: calcc
  нормирует ОС в каждой калибровке (rx_scale = 1 / экстраполированный rx при полном
  драйве, calcc.c calc()), абсолютный уровень в коррекцию не входит; шаг задевает одну
  собирающуюся выборку. Сброс с прыжком (Thetis) — только при ОС > 200, < 100 или перегрузе
  АЦП. Amp View: «(tracked 140..165)».
- Прошивка Odyssey (новая): FIFO TX 8192, старт с 6144 (32 мс), счётчики в статусе высокого
  приоритета, байты 32–47, версия в байте 47 (= 1; 0 — старая прошивка, ничего не меняется).
  P2Transport::handleBufferStatus: разности 16-битных счётчиков; сразу в лог «the radio's
  TX buffer ran EMPTY while transmitting» (не чаще раза в секунду); раз в 5 с строка
  «ANAN diag radio buffers»: уровень TX мин..макс из 8192, опустошения, переполнение;
  динамик: уровень из 2048, угасания, пустые чтения, переполнение. С новой прошивкой наш
  запас 6 пакетов остаётся: ПЛИС сама копит до 6144 перед стартом, дальше уровень стоит.

## 95. Аттенюатор держит середину; счётчики буферов в Amp View; пауза обучения при сбое (02.11.2026)

- Автоаттенюатор (оператор: «держать середину окна как цель, как шумовую полку»):
  сглаженный уровень ОС (EMA 0,3 по калибровкам) удерживается на 152 в пределах ±0,6 дБ
  шагами 1 дБ без сброса (шаг попадает внутрь полосы — без качания); после шага EMA
  пересчитывается на ожидаемый уровень. Грубый сброс с прыжком — только > 200, < 100, перегруз.
- Amp View: строка «Radio buffers: TX empty N, overflow N | speaker empty N, overflow N»
  (итоги сессии; красным, если не ноль) — только при прошивке со статусом буферов. Счётчики
  передатчика копятся только во время MOX (конец передачи — не ошибка).
- Сбой (нули в опоре DDC1 или сообщение радио об опустошении/переполнении буфера TX):
  ~0,21 с обратной связи (40 блоков) не идёт в calcc, коррекция остаётся прежней;
  слежение аттенюатора и грубый шаг по уровню (кроме перегруза) ждут 1 с. В логе
  «the radio reported a TX buffer error -- learning paused».

## 96. Скачок 19:03:15: потеря пакетов на приёме; петля по уровню FIFO радио (02.11.2026)

Лог 19:00 (новая прошивка, статус буферов):
- 19:03:15: за одно окно потеряно 25 пакетов DDC и 22 пакета микрофона (~30 мс) —
  I/O-поток опоздал, а приёмный буфер сокета Windows по умолчанию 64 КБ (~30 мс этого
  потока). Отсюда: пропуск времени микрофона (> 16 — как перезапуск) → наш underflow →
  «radio's TX buffer ran EMPTY» → нули в эфире → скачок PureSignal; и затухание динамика
  (fades 1). Ещё одно затухание динамика в 19:02:19 — при потере 2 пакетов DDC.
  Исправление: SO_RCVBUF 8 МБ, SO_SNDBUF 1 МБ (в лог — что дала ОС).
- Уровень FIFO TX радио медленно сползал целыми пакетами: 6273 → 5072 за ~30 с при нуле
  наших отказов sendto и без потерь микрофона — пакеты DUC не доходили до радио. Петля:
  каждые 0,25 с передачи средний уровень держится на 6144 ±1,5 пакета изменением запаса
  на 1 пакет (−4…+24), в лог «radio TX buffer at N of 8192, lead A -> B packets». Только с
  прошивкой со статусом.
- 12 из 13 «TX dropout in the reference» пришли при НЕпустом FIFO радио. В строку лога
  добавлено: длина нулевого участка и уровень ответвителя (DDC0) на нём относительно блока
  — «silent too: a gap on the air» или «the reference stream only». IMD3 в окне 0,5 с после
  такого участка не измеряется (снимок мерил бы сам провал).
- Динамик в остальное время в норме: уровень 450..1090 из 2048, без пустых чтений.

## 97. «Беспрерывная перекалибровка» после нового замера задержки; замер по модели (02.11.2026)

Лог 19:15 (40 м, att 11, обратная связь ~150, буферы без ошибок): после замера 1210 → 1580 нс
коррекция каждые 3–10 с пропадает на ≤1 с (IMD3 на выходе −43 → −24 дБк, DAC −128 = без
предыскажения), затем возвращается. Строки RESET и биты info6 в логе нет — и не могло быть:
строка «calibration N» пишется только для **успешного** calcc (binfo[5] растёт только при
scOK), а провалы и собственные сбросы calcc (WDSP 2.10: три провала подряд bs_count ≥ 3,
или «deadlock» верхнего бакета, info[6] |= 2) были невидимы. Промежуток без успешной
калибровки перед каждым провалом ~1,4 с — это сброс calcc и новый сбор.

Замер задержки — наша ошибка: корреляция огибающих. Пик широкий (период биений двух тонов
160 отсчётов), парабола по трём точкам на плоской вершине — разброс в сотни нс (показания
1492…1788), и три замера дали 650 → 1210 → 1580 нс. А calcc нужна не задержка огибающей, а
выравнивание, при котором ОДНА кривая (усиление и фаза по амплитуде) объясняет выход.

Сделано:
- Журнал здоровья calcc: «calcc calibration FAILED: <причины>» (info0 — экстраполяция уровня,
  info1/2/3 — подгонка AM/AM, AM/PM cos/sin: нет подгонки / плохое качество / сплайн / ошибка
  > 50× шума / экстремумы / sin²+cos², info6 — scheck / deadlock) и «correction DROPPED by
  calcc (состояние до → после, причина)». Наши собственные сбросы (AA, замер, HW peak) не
  считаются. Счётчики за передачу и последняя причина — в Amp View.
- Замер по модели без памяти: для пробной задержки d опорный сигнал интерполируется
  (кубически), в 16 бакетах амплитуды подгоняется комплексное усиление, линейное по |x|;
  остаток |rx − G·x|²/|rx|² минимален при нужном calcc выравнивании. Скан ±2,5 мкс шагом 100 нс
  вокруг оценки огибающей, уточнение параболой. Симуляция (ПА с AM/AM и AM/PM, шумовой пол остатка −57 дБ):
  600/1210/1580 нс находятся с точностью ±3 нс; рядом с 1210 нс ошибка в 370 нс стоит 3 дБ остатка.
  Огибающая остаётся запасным вариантом, если в скане нет минимума.
- Уже измеренная задержка меняется, только если остаток модели улучшается ≥ 0,5 дБ.
- Предыдущая задержка бенда хранится (`AnanPsDelayPrevNs_<band>`); кнопка «Previous delay
  (N нс)» в Amp View возвращает её (повторное нажатие — обратно) и переучивает коррекцию.
- В лог замера: огибающая / модель / компрессия / остаток при новой и при текущей задержке.

## 98. «Палка» у нуля в спектре Amp View — наш артефакт (02.11.2026)

Слева от тонов (тоны +700/+1900 Гц) в спектре обратной связи стояла линия около −50 дБк
ровно на 0 Гц — на несущей. Это была не несущая, а наша обработка: перед БПФ из снимка
вычиталось простое среднее. Тоны не укладываются в снимок (16384 отсчёта, 85 мс) целым
числом периодов, их простое среднее — утечка тонов около −50 дБк, и вычитание этой константы
само рисовало линию на 0 Гц (симуляция: чистые тоны → −46…−48 дБк в бине 0; без вычитания
−118 дБк, окно Ханна само не пускает тоны в бин 0).

Сделано: среднее не вычитается; то, что видно на 0 Гц теперь, — настоящая несущая. В тексте
Amp View — уровень несущей (бин 0) и самая сильная линия в ±150 Гц. В лог раз в 5 с — строка
«PureSignal carrier» для хоста, DAC (DDC1) и ответвителя (DDC0): среднее, взвешенное тем же окном,
и самая сильная линия без него — откуда несущая, если она есть.

## 99. Щелчок 19:43: провалы в эфире при полном FIFO радио; кто шлёт тишину (02.11.2026)

Лог 19:39 (40 м, 2 Tone, PS, задержка 1580 нс; anan140 уже работал): за передачу 4 провала
(кроме нормального старта) — 19:43:23.9 (149 отсчётов), 19:43:51.9 (174), 19:44:32.8 (172),
19:44:35.9 (107), т. е. 0,55–0,9 мс тишины в DDC1 (данные DAC) И в ответвителе (−64…−69 дБ
относительно блока) — настоящая дыра в эфире, это и есть щелчок. При этом:
- FIFO TX радио 5450…6300 из 8192, «ran empty 0, overflow 0» — радио не голодало;
- у нас ни одного underflow, ни потерь микрофона, ни отказов sendto;
- длина 107–174 меньше пакета DUC (240): мы шлём либо целый пакет нулей (underflow, он
  логируется), либо ничего — неполных пакетов тишины хост не делает.
Отдельно: уровень FIFO дважды ступенькой упал на ~240 (6084 → 5862 → 5615 между 19:43:38 и
19:43:48) — до радио не дошли 2 пакета DUC; по времени с провалами не совпадает.

Новая проверка в P2Transport (поток передачи): участки ≥ 64 отсчётов ниже −80 dBFS в том, что
хост реально отправляет (после старта и RF Delay, не CW/тон) — строка «the HOST sent a silent
run in the DUC stream: N samples …, DUC packet S, queue Q». Проверено на имитаторе радио: блок
нулей от модулятора ловится (1024 отсчёта), на чистом потоке ложных срабатываний нет.
Если при следующем щелчке этой строки не будет, а «TX dropout in the reference» будет —
тишину вставляет радио при непустом FIFO (вопрос к ПЛИС: номер пакета, уровень FIFO).

## 100. Счётчики канала связи от тестовой прошивки (раскладка статуса v3) (02.11.2026)

Тестовая прошивка: в пакете состояния (порт 1025) байт 47 = 0x03 и новые 16-битные счётчики с
момента включения радио: 16–17 не применённые пакеты команд, 18–19 отброшенные пакеты звука,
20–21 отброшенные пакеты I/Q, 24–25 кадры с неверной FCS без ошибки PHY, 26–27 кадры с RXER от
PHY (22–23 — по-прежнему обратная мощность).

Сделано: раскладка принимается с любой версией ≥ 1 (буферы 32–46 как раньше), при ≥ 3 читаются и
счётчики канала. При первом статусе в лог — их значения с момента включения; дальше считаются
приращения за сеанс (с переходом через 0xFFFF). Каждое приращение — сразу строкой «radio link:
+N …» с отметкой, шла ли передача и уровень FIFO TX (для сопоставления со щелчками и ступеньками
FIFO); итоги за 5 с — в строке «ANAN diag radio buffers». В Amp View под счётчиками буферов —
строка «Radio link: commands not applied / audio dropped / I/Q dropped | bad FCS / PHY RXER»
(ненулевые красным). Проверено на имитаторе радио со статусом v3.

## 101. Потерянные пакеты DUC: почему защита не сработала и что сделано (02.11.2026)

Лог 20:33 (тестовая прошивка, раскладка v3): за 45 с передачи радио 11 раз отбросило пакет I/Q,
каждый раз вместе с кадром с неверной FCS (PHY ошибки не сообщал) — порча кадров на Ethernet;
пакеты команд и звука не пострадали (они короче: I/Q — 1444 байта). С ними совпадают
неудачные калибровки calcc (scheck) и 5 сбросов коррекции, и щелчки.

Почему наша защита не сработала:
1. Она ловила только нули в опорном сигнале (DDC1) — так выглядел провал, когда FIFO
   радио пустел, — и ошибки буфера от прошивки. Но при отброшенном пакете FIFO полон: нулей
   нет, поток DAC просто ПЕРЕСКАКИВАЕТ 240 отсчётов (1,25 мс) — ступенька сигнала. calcc
   учился на парах со ступенькой → scheck → сброс коррекции.
2. Петля по уровню FIFO считала 250 пакетов статуса как 0,25 с (думали, статус 1 кГц), а
   радио шлёт 20 в секунду — петля срабатывала раз в 12,5 с, а пакеты терялись раз в 2–5 с:
   FIFO TX сполз 6050 → 3925 и через ~2 мин опустел бы.
3. И даже сработав, она брала лишний пакет из очереди модулятора — а модулятор делает ровно
   столько, сколько играет радио: 2–3 таких шага — и underflow.

Сделано:
- PureSignal: детектор ступеньки в опорном сигнале — вторая разность DDC1 выше 5 % HW peak
  (в симуляции двух тонов и голоса с предыскажением норма < 0,4 %, пропуск 240 отсчётов даёт
  3–70 %) → пауза обучения 0,2 с и аттенюатора 1 с, коррекция сохраняется; в лог «STEP in the
  TX reference». Плюс счётчик «I/Q dropped» радио во время передачи даёт ту же паузу.
- Восстановление FIFO: на каждый отброшенный пакет (и когда средний уровень за 0,5 с ниже
  6144 − 360) — ВСТАВКА одного пакета тишины, запас +1 пакет, очередь модулятора не трогается:
  в паузе сигнала (обе стороны ниже −60 dBFS — неслышно), или сразу, если FIFO ниже 2400
  (щелчок, но иначе FIFO опустеет). В лог «refilled the radio's TX buffer …».
- Петля по уровню — по времени (0,5 с), уменьшение запаса — как раньше.
Проверено на имитаторе радио (статус v3, отброшенные пакеты, низкий уровень FIFO): вставки
идут, DUC по-прежнему по часам радио, без underflow.

Сами щелчки от потерянного пакета с хоста не убрать — пакет испорчен на линии. Счётчик «кадры с
неверной FCS без ошибки PHY» указывает на приём RGMII в ПЛИС (временные соотношения RX).

## 102. Лог 20:48: обучение защищено; щелчки на линии; вынужденная дозаливка по одному (02.11.2026)

Передачи 20:49:04–20:50:05 (2 Tone): радио выбросило 24 пакета I/Q (по одному примерно в
секунду; 22 — с неверной FCS, 2 — с RXER от PHY; кроме того 1 пакет команд и 1 пакет звука).
Каждый раз сработала пауза обучения (по счётчику радио, 10 раз ещё и по ступеньке в DDC1 —
остальные ступеньки пришлись на уже идущую паузу). Ни одной неудачной калибровки calcc, ни
одного сброса коррекции — защита PureSignal работает. Щелчок в эфире от каждого выброшенного
пакета остаётся: эти 240 отсчётов до DAC не дошли.

Наша ошибка: в 20:49:39 уровень FIFO упал ниже 2400, и вынужденная дозаливка выдала все
16 накопленных пакетов тишины подряд — 20 мс тишины в эфире. Уровень FIFO приходит со
статусом раз в 50 мс, и до следующего отчёта он оставался «низким». Теперь — не больше одного
вынужденного пакета на один свежий отчёт статуса. При двух тонах пауз нет, поэтому FIFO держится
около 2400, и каждый выброшенный пакет даёт ещё один вынужденный (второй короткий щелчок);
в голосе дозаливка идёт в паузах, неслышно.

Для ПЛИС: и неверная FCS без ошибки PHY (приём RGMII), и RXER от PHY (линия/кабель) —
надёжно лечится только там. Вариант защиты в протоколе: хост шлёт каждый пакет DUC дважды, ПЛИС
берёт первый целый по номеру последовательности и отбрасывает повтор (поток DUC ~1,2 МБ/с).

## 103. Окно «RGMII: подстройка приёма» (тестовая прошивка, раскладка v4) (02.11.2026)

Прошивка: General (порт 1024) байт 40 = бит 7 «ручные» + задержка RX_CLK 0..31 (15 = 0 пс,
шаг 60 пс), байт 41 = задержка RXD0..3/RX_CTL 0..15 (7 = 0 пс); принимается только из кадра с
правильной FCS, ПЛИС сразу переписывает регистры KSZ9031. Статус (порт 1025): байт 28 = RX_CLK в
работе + бит 7 «задано программой», 29 = RXD, байт 47 = 0x04.

Сделано:
- P2Protocol: `RgmiiDelays`, `buildGeneral(..., rgmii)` пишет байты 40/41 (тест протокола
  дополнен). P2Transport: `setRgmiiDelays(manual, clk, data)` — сразу General и дальше в каждом
  General; повтор того же значения шлёт General ещё раз; статус v4 — байты 28/29 (в лог при
  изменении), сырые счётчики канала и число отправленных пакетов DUC для окна.
- Окно «RGMII: подстройка приёма…» (кнопка в Amp View): «Ручные задержки», «Задержка такта
  (RX_CLK)» 0…31, «Задержка данных (RXD)» 0…15 со сдвигом в пс; «в работе» из байтов 28/29 —
  зелёная галочка или «ещё не применено» (тогда General повторяется раз в секунду); ошибки FCS
  (и в минуту за последние 60 с), RXER, отброшено I/Q / звука, не применено команд — от
  «Сбросить счётчики». Значения сохраняются (AnanRgmiiManual/Clk/Data) и отправляются при
  каждом подключении (Hl2Backend).
- Автоподбор по шкале S (S = 0: 15/7; −15…+16 — только такт; дальше такт 0/31 и данные):
  шаг за шагом от 0 вверх, затем от −1 вниз; на каждом шаге — дождаться, пока радио сообщит
  значение (3 с, General повторяется раз в 0,5 с; не приняло — конец направления), 0,3 с
  установления, N секунд (по умолчанию 5) счёта FCS + RXER против отправленных пакетов DUC;
  направление кончается, когда ошибок больше 0,5 % пакетов. Без передачи шаг ждёт. Итог —
  середина самого широкого окна без ошибок (если такого нет — шаг с наименьшей долей ошибок),
  включаются ручные задержки; «Стоп» возвращает прежние значения. Таблица шагов — в окне.
- Проверено на имитаторе радио (General → байты 28/29, ошибки FCS вне окна S −4…+9): найдено
  окно −4…+9, выбрано S = 2 (такт 17), радио приняло.

Для прошивки: если задержки окажутся такими, что радио не принимает ни одного целого кадра,
вернуть его программой нельзя (команды принимаются только с правильной FCS). Автоподбор
поэтому идёт малыми шагами и останавливается на 0,5 % ошибок, но в ПЛИС стоит предусмотреть
возврат к умолчаниям, если N секунд нет ни одного целого кадра.

## 104. RGMII: чтение задержек из PHY и замер «на 4000 пакетов» (02.11.2026)

Новая прошивка: статус байты 30–31 — задержки, прочитанные из регистров KSZ9031; байт 30 бит 7
= «PHY найден». Должны совпадать с байтами 28–29 (в работе).

Сделано:
- P2Transport читает 30/31 (при раскладке ≥ 4), в лог при изменении: «RGMII delays read from the
  PHY: … -- as in use» или «-- DIFFERENT from bytes 28/29», «(PHY NOT found)».
- Окно RGMII: строка «Прочитано из PHY: такт X, данные Y» с ✓ или «не совпадает с байтами 28/29»;
  при бите 7 = 0 — «PHY не найден».
- Ошибки на 1000 пакетов (FCS + RXER на отправленные пакеты I/Q) с момента «Сбросить счётчики»,
  рядом уровень drive.
- Кнопка «Замер на 4000 пакетов»: ошибки FCS, RXER, отброшенные I/Q и звук за следующие ~4000
  пакетов I/Q при текущем drive; итог строкой в окне. Главное сравнение: drive 0 против обычной
  мощности — если без ВЧ ошибок почти нет, причина в наводке от передатчика на линию / PHY.
Проверено на имитаторе радио (байты 30/31 = 28/29): строка PHY с ✓, автоподбор как раньше.

## 105. Диагностика PHY из прошивки v6 (03.11.2026)

Прошивка v6: при настройке PHY объявление EEE выключено (MMD7 рег. 60 = 0), раз в секунду
читаются диагностические регистры. Статус, раскладка 0x06: 8–9 накопленный счётчик ошибок
простоя 1000BASE-T (IEEE рег. 10), 10–11 счётчик ошибок приёма KSZ9031 (0x15), 12 — что
объявляет по EEE сетевая карта ПК (бит 2 — 1000BASE-T, бит 1 — 100BASE-TX), 13 — статус
1000BASE-T (бит 6 радио ведущее по тактам, 5 локальный приёмник в норме, 4 удалённый).

Сделано:
- P2Transport (раскладка ≥ 6): счётчики 8–9 и 10–11 как [5]/[6] сырых счётчиков окна; в лог —
  при первом статусе значения, дальше каждое приращение «PHY: +N 1000BASE-T idle errors, +M
  KSZ9031 receive errors (transmitting/receiving)» и итог за 5 с в «diag radio buffers»; при
  изменении — «the PC's network card advertises EEE: … -- EEE is ON at the PC» / «none» и
  «PHY 1000BASE-T status: radio is clock MASTER/slave, local receiver OK/NOT OK, remote …».
- Окно RGMII: строка «PHY: ошибок простоя 1000BASE-T N, ошибок приёма KSZ9031 N» (от «Сбросить
  счётчики»), «EEE: включено на ПК (…)» красным или «выключено на ПК», «1000BASE-T: радио
  ведущее/ведомое по тактам, локальный / удалённый приёмник в норме / НЕ в норме». «Замер на
  4000 пакетов» добавляет приращения обоих счётчиков PHY.
Как читать: растут ошибки простоя / приёма PHY вместе с FCS — ошибки на линии или в приёмнике
PHY (кабель, наводка от передатчика); PHY чист, а FCS растут — между PHY и ПЛИС (RGMII).
Проверено на имитаторе радио (раскладка 6).

## 106. Прошивка v7: частота кварца PHY, накопленный счётчик ошибок KSZ9031 (03.11.2026)

Прошивка v7 (раскладка 0x07; по исходникам Odyssey.v / CC_encoder.v): байты 53–54 — частота
PHY_CLK125 (5 × кварц 25 МГц KSZ9031) против C122 (122,88 МГц, от опоры 10 МГц, если она есть),
ворота 1 с: (счёт − 125 000 000) / 8, со знаком, т. е. Гц / 8; 1 ppm = 125 Гц = 15,6 ед.;
1000BASE-T допускает ±50 ppm. Байты 10–11 (ошибки приёма KSZ9031, рег. 0x15) теперь
накапливаются (PHY сбрасывает регистр при чтении, раньше было почти всегда 0) — программа и так
считает приращения, менять не пришлось. Байты 30/31 (с v5): 30 — бит 7 PHY найден, бит 6 чтение
выполнено, бит 5 RXD0..3 одинаковы, биты 4..0 RX_CLK; 31 — биты 7..4 RX_CTL, 3..0 RXD0.

Сделано:
- P2Transport: раскладка ≥ 7 — байты 53–54 (int16): при первом статусе в лог «PHY crystal …
  Hz = … ppm», в строке «diag radio buffers» — размах за 5 с в ppm. Разбор 30/31 полный: RX_CTL,
  «чтение выполнено», «RXD0..3 одинаковы»; совпадение с 28/29 — по такту, RXD0, RX_CTL и
  равенству RXD0..3.
- Окно RGMII: «Кварц PHY (125 МГц против C122): X ppm (Y Гц; допуск ±50 ppm)», цвет: зелёный
  до 25 ppm, жёлтый до 50, красный больше; строка «Прочитано из PHY: такт, RXD0, RX_CTL,
  RXD0..3 одинаковы / РАЗНЫЕ».
Проверено на имитаторе радио (раскладка 7, −800 Гц = −6,40 ppm).

## 107. Лог 18:47 (v7): строка «diag radio buffers» и что в ней видно (03.11.2026)

Оператор не нашёл строку «ANAN diag radio buffers». В логе 18:47:44 она есть (69 раз, раз в 5 с),
но радио шлёт пакеты статуса только на передаче: 0 пакетов до первого MOX (18:48:12), после MOX off
(18:52:28) счётчик стоит на 5040. В это время строка выходила с нулями («TX idle», 0 ошибок) —
выглядело как чистая линия. Теперь в такой период: «no status packets from the radio in the last
5 s (none while receiving)».

Что видно на передаче (две передачи, drive 75 и drive 15):
- EEE на ПК: нет (байт 12 = 0); 1000BASE-T: радио ведомое, оба приёмника в норме (0x38).
- Кварц PHY: +11,5 → +13,5 ppm за 4 мин (прогрев), в допуске ±50.
- Ошибки простоя 1000BASE-T (собственная оценка PHY): 50–330 за 15 с на обеих мощностях;
  KSZ9031 receive errors 0; FCS 2–17 и RXER 0–4 за 15 с, отброшено I/Q столько же.
  PHY сам видит ошибки на линии → ошибки на линии / в приёмнике PHY, не RGMII; от мощности
  заметно не зависят. На приёме сравнить нельзя — статуса нет.
Для прошивки: слать статус и на приёме (хотя бы раз в 100–200 мс), чтобы сравнить счётчики
PHY без передачи.

## 108. Потеря связи на передаче: RF Power 100 % и передача после восстановления (03.11.2026)

Оператор: «если связь потерялась и возобновилась, RF Power становится 100 %». Лог 20:36:16:
передача с 20:40:23 (drive 62); 20:40:52 «no IQ for 2000 ms» → linkDown → RadioModel disconnected;
20:40:57 IQ пошёл снова → linkUp → connected; MOX off только в 20:41:06 — уже с drive 150 из 255.

Две наши ошибки:
1. Мощность. Связь, пропавшая и вернувшаяся внутри сеанса, идёт без connectRadio() (нет
   засева из памяти бенда), но RadioModel::onDisconnected() сбрасывает TransmitModel к 100 %,
   а при connected() RadioModel отправляет это значение в бэкенд как волю оператора
   (setTxPower(100) — и оно же записалось в память бенда). Теперь Hl2Backend перед
   connected() публикует текущие RF Power и Tune Power оператора — отправка RadioModel
   становится эхом того же значения, которое setTxPower() не принимает как изменение.
2. Передача после восстановления. Хост снял ключ в момент потери (20:40:52.556), но
   координатор передачи отклоняет снятие ключа после сброса соединения, и в P2Transport MOX
   остался включённым: как только связь вернулась, радио снова передавало (14 с, drive 150).
   Теперь P2Transport сам снимает MOX (и ключ CW) при «no IQ for 2000 ms» и шлёт High
   Priority; после восстановления передачи нет, пока оператор не нажмёт снова. В лог:
   «MOX off -- the link to the radio was lost while transmitting; it does not come back on by
   itself». Проверено на имитаторе радио (пауза 2,6 с на передаче: MOX снят, после
   восстановления пакетов DUC нет).
Память бенда 40 м могла сохранить 100 % — выставить RF Power заново один раз.

## 109. Кнопка WB на панораме; память задержки PureSignal отдельно для АЦП2 (03.11.2026)

- Wideband Monitor перенесён из Tools на левую колонку кнопок панорамы, под +RX: кнопка «WB»
  (SpectrumOverlayMenu: kBtnWideband = 1, остальные индексы сдвинуты; сигнал widebandClicked →
  MainWindow открывает тот же WidebandMonitorDialog). Пункт Tools → Wideband Monitor... убран.
- Measure delay: у АЦП2 (RX2 in, внешний ответвитель — свои кабели и своя задержка) теперь своя
  память по бендам: AnanPsDelayNsAdc2_<бенд>, AnanPsDelayPrevNsAdc2_, AnanPsDelayAtAdc2_,
  AnanPsDelayCompDbAdc2_; у АЦП1 ключи прежние. Смена АЦП обратной связи (и профиль с другим АЦП)
  загружает задержку этого АЦП для бенда; идущий замер прерывается. «Previous delay» — тоже своя.
  В Amp View: «Amp delay (40 m, ADC2) …»; в лог: «band …, feedback ADC2, amp delay …».
  Для АЦП2 первый раз действует умолчание 600 нс — нужен замер на каждом бенде.

## 110. About с именем форка, окно Tools → HW Diag, чистка Amp View; статус v8 (03.11.2026)

- About: под словесным знаком крупно «Ater-ugly_fork · build N» (src/core/ForkBuild.h:
  AETHER_FORK_NAME, AETHER_FORK_BUILD = номер поставки anan, сейчас 153; повышать с каждой
  поставкой).
- Прошивка v8 (описание раскладки v8 в проекте: Odyssey_Status_Bytes_v8.md): управление
  задержками RGMII (General 40–41) и их чтение (статус 28–31) удалены, задержки фиксированы в
  прошивке. Программа больше ничего не шлёт в байтах 40–41 General (нули; тест протокола
  проверяет); окно «RGMII: подстройка приёма», его кнопка в Amp View, сохранение
  AnanRgmii* и разбор 28–31 удалены (RgmiiTuneDialog.* больше не собираются — файлы можно
  удалить).
- Tools → «HW Diag...» (HwDiagDialog): всё, что радио сообщает о себе в пакете состояния —
  Ethernet (кадры с неверной FCS и в минуту, RXER, отброшенные I/Q / звук / команды, ошибки на
  1000 пакетов I/Q), PHY (ошибки простоя 1000BASE-T, ошибки приёма KSZ9031, EEE ПК, статус
  1000BASE-T с ошибкой ведущий/ведомый, кварц PHY в ppm), буферы (уровни ТХ и динамика с
  размахом за 5 с, опустошения, переполнения, затухания, чтения из пустого), кодек (потери PLL,
  записи в TLV320). Счётчики — от «Сбросить счётчики» и с включения радио; поля показываются по
  версии раскладки; сброс счётчиков радио (run = 0) распознаётся. «Замер на 4000 пакетов» —
  здесь же. Данные — снимок последнего пакета состояния (P2Transport::statusSnapshot()).
- Amp View: убраны «Previous delay», кнопка RGMII, строки «Radio buffers» и «Radio link» (они в
  HW Diag). Оставлено для PureSignal: состояние, коррекция, проверки calcc, уровень обратной
  связи и аттенюатор, HW peak, задержка (бенд, АЦП), IMD3, несущая, здоровье calcc; новая
  строка «Learning paused: reference dropouts / steps / radio buffer-packet errors» — сколько
  раз обучение пропускало испорченную обратную связь.

## 111. Чистка: защиты от провалов радио убраны; задержка PureSignal — автоматически на каждой передаче (03.11.2026)

Оператор: провалы передачи в радио исправлены; другое железо расширения протокола не имеет
(там нули) — программа должна надёжно работать на идеальных пакетах и заполнении буфера, без
опоры на диагностику радио.

Лог 21:36 (линия чистая, 0 отброшенных пакетов): за минуту передачи 23 отказа calcc «scheck»
и 12 сбросов коррекции. scheck (WDSP 2.10) сравнивает новую кривую с прошлой в 40 точках
(допуск 0,10); ломается низ AM/AM: ym 1,24..2,36 на 0..0,2 при ровном верхе — пары у нуля
огибающей при неверной задержке. Стояла 1580 нс, подгонка той же передачи — 1140 нс (±4 нс);
правило «≥ 0,5 дБ» её не применило. И главное: задержка DDC1 → DDC0 меняется от передачи к
передаче (01.11 подряд 1427, ~1100, ~770 нс; 02.11 1580 / 2040; 03.11 1140), каждый замер при
этом устойчив до нескольких нс — похоже на разную фазу децимации DDC0/DDC1 при каждом старте
(вопрос к ПЛИС: синхронный сброс). Задержка «на бенд» поэтому неверна на следующей передаче.

Сделано:
- PureSignal: задержка ищется автоматически раз в секунду на каждой передаче той же подгонкой
  без памяти (fitDelay), без выключения коррекции (выход — функция без памяти от данных DAC,
  предыскажённых или нет). Первая хорошая подгонка передачи (остаток < −25 дБ) применяется
  сразу, если отличается > 75 нс, и обучение начинается заново; дальше — при отличии > 150 нс
  две подгонки подряд. Последнее значение хранится по бенду и АЦП как старт следующей передачи.
  В лог: «PureSignal auto delay: 40 m, ADC1: A -> B ns (fit residual …)». Ручной замер (Measure
  delay, корреляция огибающих, правило 0,5 дБ, проверка компрессии) убран вместе с кнопкой.
- PureSignal: убраны паузы обучения по нулям в DDC1, по ступеньке (вторая разность), по ошибке
  буфера / отброшенным пакетам радио, задержка аттенюатора после «сбоя», пропуск IMD после
  сбоя; каждый блок идёт в calcc.
- P2Transport: убраны досылка пакетов тишины, вынужденная дозаливка, петля по уровню FIFO
  радио и сигнал ошибки буфера для PureSignal. Остались: передача по часам радио (пакеты
  микрофона), снятие MOX при потере связи, логи и окно HW Diag (только показывают).
- Amp View: строка «Amp delay (бенд, АЦП) N ns, auto; last fit M ns (residual R dB)».
- Build 154.

## 112. Авто-задержка только по данным без коррекции (03.11.2026, сборка 155)

Лог 22:08 (сборка 154, одна передача 100 с, 40 м, ADC1). Первые ~25 с всё было чисто, потом пошли сбои scheck (18 за передачу) и 3 сброса коррекции.

Авто-задержка за одну передачу «уплыла»:

| Время | Задержка, нс | Невязка подгонки: новая / при старой задержке, дБ |
|---|---|---|
| 22:08:24 | 1580 → 910 | −47.4 / −46.1 |
| 22:08:44 | 910 → 1080 | −46.5 / −46.5 |
| 22:09:25 | 1080 → 1260 | −45.6 / −45.5 |
| 22:09:28 | 1260 → 1770 | −43.1 / −42.8 |
| 22:09:39 | 1770 → 2060 | −42.4 / −42.4 |

- Невязка при новой и при старой задержке отличается на 0..0.3 дБ: подгонка при включённой коррекции не несёт информации о задержке.
- Это замкнутая петля. calcc учится на задержке d, его предыскажение строится под d, и следующая подгонка по предыскажённым данным идёт за ним.
- Каждое применение перезапускало обучение.
- При 910/1080 нс нижний край кривой AM/AM был ym 0.8..2.3; одиночные сбои scheck были, сбросов не было.
- При 1770/2060 нс он стал ym 1.3..3.1, и calcc сбросил коррекцию 3 раза.
- Несущая в норме: на ответвителе DC −94..−109 дБн, линия −72 дБн на 60.9 Гц. Ошибок связи нет.

Исправление (AnanPureSignal::autoDelayStep):
- Задержка берётся только из обратной связи БЕЗ коррекции: PS выключен или info[14] == 0. Это промежуток после нажатия или сброса, до первой кривой calcc.
- Весь снимок (85 мс) должен лежать внутри этого окна: m_uncorrSinceMs ведётся в tick(), запас 250 мс.
- Не чаще одного раза за передачу; решает первая такая подгонка (применяется, если отличие больше 75 нс).
- Подгонки при включённой коррекции только показываются в Amp View («correction on: display only»).
- Новая строка лога: «PureSignal delay fit without correction: …». Она даёт данные о том, меняется ли истинная задержка между передачами; это вопрос синхронного старта DDC0/DDC1 в ПЛИС.
- Если коррекция осталась с прошлой передачи (info[14] == 1 с самого начала), задержка в этой передаче не меряется и остаётся сохранённой.

## 113. Back to the manual delay measurement; S-meter options in Setup > Receive; HW Diag in English (03.11.2026, build 156)

Operator: return to the earlier algorithm. The delay is measured by hand at low power, stored per band, and then used unchanged at any power. It worked well.

- The automatic delay of builds 154/155 is removed: autoDelayStep() and the correction-off window tracking are gone.
- startDelayMeasure(), measTick() and measFinish() are restored from build 153 without "Previous delay". The rules are the same:
  - 5 readings with the correction off;
  - spread ≤ 400 ns;
  - compression ≤ 1.5 dB at the peak;
  - a measured delay is replaced only when the fit improves by ≥ 0.5 dB.
- Keys stay as they were: AnanPsDelayNs_/At_/CompDb_ per band, with "Adc2" for ADC2.
- The **Measure delay** button is back in the Amp View.
- While a measurement runs, the attenuator tracking and the HW peak are held.
- The fit is still computed every second and shown; it does not change the delay.
- **Note:** builds 154/155 overwrote the stored delay of the bands transmitted on (40 m: 2060 ns). Measure those bands again.

S-meter, Setup > Receive, group "S-Meter" in one row:
- Correction −50..+50 dB, step 0.5.
- [ATT]: add the step attenuator of the ADC the receiver listens to (the reading refers to the antenna).
- [LNA] and its gain 0..99 dB: subtract the gain of an external LNA.
- Applied at once and saved: AnanSMeterCorrDb / UseAtt / UseLna / LnaGainDb in AnanUiState.
- Applied in the Hl2Backend meterUpdate to the S-meter only; the spectrum is unchanged.

HW Diag (and the unused RgmiiTune window) translated to English. No Cyrillic is left in src/.

## 114. Attenuator: Thetis window plus drive feed-forward; manual delay measurement always accepted (04.11.2026, build 157)

Log 17:28 (build 156, 40 m, ADC1). Over 113 s; drive raised smoothly from 67 to 138 (+6.3 dB) around 17:30:52..17:31:04.

- Six tracking steps happened, 11 → 17 dB, one every 1.2..3 s. The feedback stayed at 164..183, i.e. inside the Thetis window 128..181 (almost all of it).
- Every step was followed by an scheck failure (+0.3, +0.25, +2.5, +2.5, +0.6 s). Twice there were three in a row, and calcc dropped the correction (17:30:53.7, 17:31:05.0).
- Cause (ours): the ±0.6 dB hold around 152 from anan143 steps without need. Each step leaves a collection with samples at two levels behind it. The step also lags the drive change by a calibration.

Delay:
- Measure delay at 17:30:07 gave five readings of 1572..1600 ns (the same as 02.11).
- The stored 2060 ns (written by the auto delay of build 155) fitted only 0.4 dB worse and was kept by the "≥ 0.5 dB gain" rule.
- With 2060 ns the low end of AM/AM is ym 0.74..4.0.

Fix:
1. **Drive feed-forward.** P2Transport::driveLevel() gives the commanded drive. As soon as the drive changes by ≥ 1 dB against the reference, ATT moves by the same whole number of dB, with the correction kept. The feedback amplitude is proportional to the drive, so the level calcc sees does not change. The reference is kept across overs.
2. **Feedback window.** The smoothed level is left alone inside 128..181 (Thetis NeedToRecalibrate). Outside it, one step straight to 152, the correction kept.
   - The smoothing takes only settled calibrations (≥ 2 after a change, ≥ 400 ms).
   - The coarse reset-and-jump (> 200 / < 100) and the ADC overload (+6 dB) are unchanged.
3. **Delay measurement:** the 0.5 dB rule is removed. A steady measurement (spread ≤ 400 ns, compression ≤ 1.5 dB) is always accepted.

**Needed:** measure the delay on 40 m again (and on the other bands transmitted on with builds 154/155).

## 115. S-meter: LNA gain only while the LNA is on (04.11.2026, build 158)

Operator: the [LNA] box in Setup > Receive > S-Meter means "account for the LNA when it is on". The LNA itself is switched by the LNA button of the panadapter ANT panel ("AnanLna", the Alex preamp bit), and while it is off nothing may be corrected.

- AnanUiState::sMeterOffsetDb(attDb, lnaOn) subtracts the gain only when [LNA] && lnaOn.
- lnaOn is m_ananLnaPushed == 1, the state actually pushed to the radio by the 300 ms ANAN timer.

## 116. Failures with nothing changing; scheck diagnostics; "S-meter Calibration" (04.11.2026, build 159)

Log 17:52 (build 158, one over of 74 s, 40 m, ADC1). The radio was clean: no buffer, link or PHY errors.

- Before the measurement (delay 2060 ns) there were 5 scheck failures and 1 drop in 12 s.
- Measure delay gave **1030 ns** (998..1052, spread 54 ns, residual −44.4 → −46.7 dB). It was accepted (the 0.5 dB rule is gone).
- With the drive (54), ATT (9 dB) and feedback (156..158) all constant, scheck still failed at 17:54:13, :15, :19, :20, :21, with a drop at :16.4. **No attenuator steps were involved.**
- The drive feed-forward then worked as intended: 9 → 16 dB on the ramp and back, one step per ~1 dB.
- The fitted curves of the successful calibrations are almost identical. The low bin is ym 1.38..2.28 and varies by ±0.005 between calibrations. So the failures are single outlier calibrations: scheck compares each solution with the previous one at 40 points, x = 0..1, |Δ| ≤ 0.10. It also stores the failed one as "previous", so one outlier gives two failures in a row.
- Compare log 17:28 (another program run, delay 2060 ns): 80 s at constant drive without a single failure.

Delay between program runs:
- 17:30 (run 17:28): 1572..1600 ns.
- 17:54 (run 17:52): 998..1052 ns, the same band.
- Each measurement is steady to ±30 ns, so the true DDC1 → DDC0 lag changes when the radio is (re)started (history: 770..2060 ns).
- This is the open FPGA question: do DDC0 and DDC1 start their decimation together? One 192 kHz sample is 5208 ns; the offsets are sub-sample, i.e. the CIC decimation phase. A delay stored per band can only hold if the phase is the same at every start.

Build 159:
- WDSP patch 26 (diagnostics only): GetPSScheckDiag() gives the worst point of each failed scheck.
- The host logs every failure: "PureSignal scheck detail (#N): worst magnitude/cos/sin at x … (point k of 40): prev -> new (|d| …), feedback, att, drive". This shows whether the outliers sit at the low end (x ≈ 0, noise) or across the curve.
- Setup > Receive: the group is renamed "S-meter Calibration".

## 117. Every scheck failure is at x = 0.026 of the magnitude curve; attenuator set in the same instant as the drive (04.11.2026, build 160)

Log 18:06 (build 159, over of 121 s, 40 m, ADC1, delay 1030 ns): 29 scheck failures, 6 drops.

- **All 29** are the magnitude curve m at point 1 of 40 (x = 0.026). No other point and no cos/sin curve fails.
- The values there are 1.3..3.2 and jump by 0.11..0.45 between calibrations. The limit is 0.10.
  - At constant drive 67 (18:06:53..59): failures #1..7 with |Δ| 0.24..0.39, i.e. noise in the lowest interval.
  - On drive ramps m(0.026) creeps steadily, 1.97 → 2.33 → 2.58 → 2.83 → 3.22, about 0.3 per calibration. That is a real change of the normalised curve with the drive, but scheck counts it as a failure, and three in a row drop the correction.
  - #8 (18:07:09.3): 1.83 → 7.39 right after "drive 78 → att 11 → 12" from the 50 ms PureSignal tick. The collection held samples at two levels. **This one is ours.**
- m ≈ 2 at x = 0.026 means a predistortion of +6 dB at the very bottom: the PA gain near zero amplitude is half of that at the peak (crossover). It varies by ±1.5 dB between 0.35 s calibrations. This is either the PA (bias / crossover) or the noise of the feedback in the lowest interval (fit residual −46 dB ≈ 0.005 of the peak, against x = 0.026 → about 14 dB SNR).
- WDSP 2.10 scheck compares solution with solution at 40 points with an absolute 0.10, including x = 0.026. Thetis' classic calcc does not have this comparison. The engine can be switched by building with `-DAETHER_WDSP_PS_ENGINE=classic` (both variants compile).

Build 160:
- The drive feed-forward moved into P2Transport::setTxDriveLevel(). When the drive changes by ≥ 1 dB, m_psAttDb changes by the same whole dB, and the Transmit Specific and High Priority packets go out back to back with the new drive.
- AnanPureSignal enables it with setPsAttFollowsDrive(enabled && auto attenuate) and adopts psAttNow() in its tick ("auto-attenuate (drive): … set with the drive").
- The window (128..181), coarse and overload rules are unchanged.

## 118. scheck switched off as a test (04.11.2026, build 161)

Operator: the PA has a low bias on purpose, and floating gates (the bias rises with power by itself), so nothing can be done in hardware. "Just switch that check off and see."

- WDSP patch 27: SetPSScheckOff(channel, off). With it set, scheck still compares the new solution with the previous one and records the worst deviation (patch 26), but never rejects it.
- The other checks are unchanged: fit quality, sin²+cos², top bucket.
- An accepted solution still enters the averaged curves with α = 0.3 (curve_ema_update), so a single outlier is blended in at 30 %, not taken as it is.
- AnanPureSignal: kScheckOff = true. It is set in applyWdspParams() and at every key-down (a re-created calcc starts with the check on).
- Log: "PureSignal engine: WDSP 2.10 calcc/iqc, scheck OFF (logged only)". What would have been rejected: "PureSignal scheck (off) would have failed (#N): worst … at x …".
- Things to watch: IMD3 ("IMD3 jump" lines, Amp View), "correction DROPPED" (should be gone), and the other calcc failures.

## 119. The delay drifts with the PA heating; a time law for a smooth correction (04.11.2026, build 162)

Log 18:34 (build 161, one over of 90 s at drive 108, after 8 min of rest). Measure delay three times:

| Time into the over | Delay, ns | Fit residual, dB |
|---|---|---|
| 2..6 s | 3010 → 3190 (rising within the 5 readings) | −40.8 → −40.3 |
| 45..49 s | 3740..3870 | −38.7 |
| 75..79 s | 4010..4056 | −38.3 |

- The delay drifts within one transmission (+1 µs in 75 s), and the residual worsens with it. The per-run differences seen earlier (1030 / 1580 / 1980 ns at drive 54..67) are the same thing in different heat states.
- Probable mechanism: the floating gates get their bias from the signal through an RC network. The bias lags the envelope, which is memory, and the memoryless model shows it as an apparent delay that changes with heating.
- The two calibration errors: at 18:34:49, sin²+cos² failed and the correction was dropped; at 18:35:24 and 18:35:51, "AM/AM: fit quality bad". A few seconds after a measurement the delay no longer matched, and m(0.026) went to 79, 71, −1.98.
- Least squares over the three measurements: d(t) = 4235 − 1205·exp(−t / 42 s), drive 108, starting cold.

Build 162 (AnanPureSignal::heatTick(), every tick):
- Heat state h: towards u = (drive / 108)² with τ = 42 s while transmitting, back to 0 with τ = 90 s while receiving.
  - The cooling constant is not measured yet; we only know the over after 8 min of rest started at the cold value.
  - The scaling of u with the square of the drive is an assumption (dissipation).
- Delay applied = measured delay of the band + 1205 ns · (h − h at the measurement). SetPSTXDelay is updated in 10 ns steps. SetDelayValue only re-indexes the delay line, so the learning is not restarted.
- Measure delay stores h with the delay (AnanPsDelayHeat_ per band and ADC). Older values without it count as h = 0.
- Log every 10 s while transmitting: "PureSignal heat delay: heat …, amp delay X + Y = Z ns, drive …".
- Amp View: "Amp delay Z ns … (measured X + heat drift Y ns, heat h)".
- Model check against the measurements: 3086 / 3190 / 3822 / 4033 ns at 2 / 6 / 45 / 75 s, against the measured 3010..3190 / 3800 / 4040.
- **Needed:** measure the delay again (the stored 4050 ns was taken hot without h).
- Not modelled: the delay also depends on the drive itself (cold about 1000 ns at drive 54, 3030 at 108). Measure at the working drive.

## 120. The correction dropped for 17 s: with the check off a negative correction got in; scheck as in Thetis (04.11.2026, build 163)

Log 18:47 (build 162, over of 100 s at drive 108).

- **Start:** the stored 4050 ns (measured hot by build 161, without a heat value, so taken as h = 0) plus the model gave 4307 ns at h = 0.21, against a true value of about 3300. At 18:47:31 "AM/PM cos: fit quality bad" and the correction was dropped. This was expected: the value had to be re-measured.
- **Measure delay at 18:47:37:** 3420 ns at h = 0.39. The model predicts 3030 + 0.39·1205 = 3500 for that state, so it agrees. After that the heat drift followed smoothly: 3578 → 3702 → 3800 → 3876 → 3936 → 3984 → 4021 ns.
- **18:47:42..47:** with scheck off, the magnitude correction at x = 0.026..0.05 went **negative** (−1.4..−2.4) and was blended into the curves. "AM/AM: fit quality bad" four times in a row, and at 18:47:48 the correction was dropped. The next good calibration came only at 18:48:05, so there was no correction for 17 s. From 18:48:05 to the end of the over (51 s) there were no failures.

Thetis' classic scheck (third_party/wdsp/classic/calcc.c):
- compares the previous solution only for intervals i = 4..15 of 16, i.e. x ≥ 0.25;
- rejects a negative corrected drive (0x10) and an out-of-range one.

The WDSP 2.10 scheck compares at all 40 points from x = 0 (that was all our failures, at x = 0.026), and with it off nothing protected the bottom.

Build 163 (WDSP patch 28, SetPSScheckMode):
- Mode 2 = as Thetis: compare from x = 0.25 up, and reject a negative magnitude correction.
- Host: kScheckMode = 2, set in applyWdspParams() and at every key-down. Start-up line: "scheck as Thetis (x >= 0.25, magnitude >= 0)". A failure is logged as "PureSignal scheck FAILED (#N): worst …", where "magnitude < 0" marks the negative check.

## 121. Arrow cursor on the slice flag; Thetis Auto AGC studied (04.11.2026, build 164)

- Operator: the window over the panadapter (the slice flag with the audio / DSP / mode tabs) should show the plain arrow, as the applet panel does.
  - VfoWidget sets Qt::ArrowCursor on itself, so it no longer inherits the panadapter crosshair.
  - All its controls get the arrow instead of PointingHandCursor (applyInteractiveCursors and the explicit setCursor calls).
  - The slice-drag overrides of SpectrumWidget are unchanged.
- Log 18:55 (build 163): one glitch in 84 s. At 6 s, m(0.026) = −22, rejected three times by the negative check, and calcc reset; it re-learnt in 1.4 s. The rest was clean, IMD3 −38.5..−40.5 dBc at drive 108. Operator: leave it as it is.
- Thetis Auto AGC (ramdor/Thetis console.cs, tmrAutoAGC_Tick, every 500 ms, not on TX, only with a valid noise-floor estimate): `setAGCThresholdPoint(NF + AutoAGCOffset)`.
  - The offset is −60..+60 dB, default **+20**.
  - It ends in WDSP `SetRXAAGCThresh(ch, thresh_dBFS_per_bin, fftSize, rate)`: `max_gain = out_target / (var_gain · 10^((thresh + 10·log10(BW_filter · size / rate)) / 20))`. So the AGC ceiling follows the floor and the filter width, and the noise heard in the pauses stays the same.
  - Moving AGC-T by hand (or the knee on the panadapter) switches Auto off.

## 122. AGC follows the noise floor (Auto AGC as in Thetis, AGC-T as an offset) (04.11.2026, build 165)

Operator: option 2 — Auto makes the AGC follow the floor, with the offset set by a slider; without Auto, AGC-T sets the gain by hand. The controls go into the audio tab of the slice flag (speaker icon).

- AnanUiState: agcAuto[rx] and agcNfOffsetDb[rx] (−20..+60 dB, default **+40**, see below). Keys AnanRx1/2AgcAuto, AnanRx1/2AgcNfOffsetDb.
- Hl2Backend::ananAutoAttStep now estimates the floor always, not only with AUTO ATT (the same estimator: 20th percentile of the inner 90 %, EMA 0.8/0.2, display dBm on 1024-point bins, receive only). Then ananAutoAgcStep(rx) runs every 500 ms:
  - `top = 20·log10(out_target / var_gain) − (thresh + 10·log10(BW · 1024 / rate))`. This is WDSP SetRXAAGCThresh, the formula Thetis uses.
  - thresh = floor − 10 (dBm → dBFS) − 6.02 (Hl2Spectrum's tone gain) + offset.
  - out_target = (1 − e⁻⁴)·0.9999 (wcpAGC, n_tau 4); var_gain = 10^(35/200) (slope 35).
  - BW is the slice filter width. The result is clamped to 0..120 dB.
  - It is applied when it changes by ≥ 0.5 dB and logged when it changes by ≥ 3 dB: "ANAN Auto AGC RXn: floor … dBm + … dB, filter … Hz -> AGC gain … dB (manual AGC-T law …)".
  - With AGC Off, or Auto off, the manual law 0.6·AGC-T + 40 returns.
- agcCeilingFor(r) replaces m_dbRef.agcCeilingDb(r.agcThresholdDb) at all 8 Config / setAgc sites, so the auto ceiling survives rebuilds (zoom, rate, mode).
- VfoWidget, audio tab: the existing Auto button next to AGC-T (Flex Auto AGC-T, disabled on this radio) now switches the floor AGC for the slice's receiver.
  - A new row "NF + [slider] N dB" (only for this radio) sets the offset.
  - With Auto on, AGC-T is greyed (manual gain only without Auto).
  - The row appears once the backend has filled the pan → receiver map (polled every 1 s).
- Example (filter 2700 Hz, 192 kHz, offset +20): floor −120 dBm → 100.8 dB of AGC gain; −110 → 90.8; −100 → 80.8. A noisier antenna gets less gain, so the noise heard stays the same.
- Default offset **+40**, not Thetis' +20. Our dBm scale differs: at the AUTO ATT target of −120 dBm, +20 gives 101 dB against the 79 dB of the default manual AGC-T 65 (22 dB more hiss). +40 gives about 81 dB, within 2 dB of today's loudness.

## 123. The NF+ row did not appear (04.11.2026, build 166)

- Operator (build 165): "where are these sliders?". The row was shown only when VfoWidget could find the slice's receiver in AnanUiState::panRx and the radio reported connected. If either was missing, it stayed hidden.
- Now ananAgcRx() returns −1 only on a radio with its own Auto AGC-T (Flex). Otherwise it is the receiver from panRx, and before that map exists, slice 1 → RX2, anything else → RX1. The row is always there on this radio.
- The availability registry no longer disables the Auto button on ANAN (it judged by the Flex Auto AGC-T conditions), and "engaged" follows AnanUiState::agcAuto.
- Log: "ANAN Auto AGC flag: slice N, pan …, RXn, NF+ row shown".

## 124. Auto AGC / NF+ also in RX Control, synchronised with the slice flag (04.11.2026, build 167)

- Operator: the flag's audio-tab controls are duplicated in the RX Control applet; add the missing / new ones there too and keep them in sync like the rest.
- RxApplet:
  - The "A" button next to AGC-T (Flex Auto AGC-T, disabled on this radio) now switches the noise-floor AGC of the slice's receiver.
  - A new row "NF + [slider] N dB" below the AGC row (−20..+60 dB, drag value shown in dB).
  - The availability registry treats it as on ANAN, and "engaged" follows AnanUiState::agcAuto.
  - With Auto on, AGC-T is greyed (it is the manual gain).
- Synchronisation: both surfaces write AnanUiState::setAgcAuto() and re-read it every 250 ms (VfoWidget's poll went from 1 s to 250 ms). A slider being dragged is not overwritten. When Auto changes, AGC-T availability is recomputed first (syncAgcSliderFromSlice / updateAgcSliderFromSlice) and then greyed if Auto is on.

## 125. The NF+ slider was there but invisible (04.11.2026, build 168)

- Screenshot of the operator's window (build 167): both the slice flag and RX Control show the row "NF + … 40 dB". The slider itself was disabled (Auto off), and a disabled slider is all but invisible in this theme.
- Now the NF+ slider is always enabled. The offset is kept and used once Auto is on. Only the value label is greyed while Auto is off.

## 126. NF+ caption drawn as the SQL button (04.11.2026, build 169)

- Operator: badly laid out. The "NF +" caption should be a button that cannot be pressed, looking exactly like SQL, and the spacing between the sliders should be the same in the flag and in the applet.
- The caption is now a QPushButton with the SQL button's style when off: kDspToggle + kDisabledBtn in the flag (60×20), kButtonBase() + kDisabledBtn in RX Control (52×20).
  - It takes no clicks (WA_TransparentForMouseEvents) and no focus.
  - Its 20 px height makes the row as high as the SQL / AGC rows, so the spacing between the sliders is the same.
- The value label in the flag is 63 px wide (AGC-T value + Auto), so the NF+ slider ends where the AGC-T slider ends.

## 127. Every Protocol 2 ANAN: G2, 7000DLE and the rest (05.11.2026, build 170)

- Report: users of the ANAN-G2 Ultra and the 7000DLE could not connect. Cause: discovery (`isSupportedAnan()`) listed only board 3 (ANGELIA) and 4 (ORION). The G2 (SATURN, board 10) and the 7000DLE (ORION II, board 5) replies were dropped, so the radios never appeared.
- Boards now driven (Thetis `HPSDRHW`, deskHPSDR `NEW_DEVICE_*`). Receivers per board still come from Discovery byte 20 (DDC count) minus the receiver base, capped at 4.

  | Board | Models | DDCs | ADCs | Alex | PS |
  |---|---|---|---|---|---|
  | 1 HERMES | ANAN-10, ANAN-100 | RX from DDC0 | 1 | classic HPF + LPF | no |
  | 2 HERMES II | ANAN-10E, ANAN-100B | RX from DDC0 | 1 | classic | no |
  | 3 ANGELIA | ANAN-100D | RX from DDC2, PS DDC0/1 | 2 | classic | yes |
  | 4 ORION | ANAN-200D | as 100D | 2 | classic | yes |
  | 5 ORION II | 7000DLE, 8000DLE, Orion MkII, Anvelina Pro3 | as 100D | 2 | band-pass ADC0 + ADC1 | yes |
  | 10/11 SATURN | ANAN-G2, G2-1K | as 100D | 2 | band-pass ADC0 + ADC1 | yes |
  | 20 HermesC10 | ANAN-G2E | RX from DDC0 | 1 | band-pass ADC0 | no |

- New Alex scheme `Orion2BandPass` (deskHPSDR `new_protocol.c`, NEW_DEVICE_ORION2/SATURN/G2E):
  - ADC0 band-pass in Alex0 and ADC1 band-pass in Alex1, edges 1.5 / 2.1 / 5.5 / 11 / 22 / 35 MHz;
  - the TX LPF follows the TX frequency only (the receive path does not go through it);
  - T/R relay and the PS bit in both words;
  - Alex1 bit 8 grounds the ADC1 input on TX, except while PS takes its feedback on ADC1;
  - PS feedback "EXT1" does not exist on these radios and falls back to the RX bypass input;
  - General byte 59 = 0x03 (Alex0 and Alex1).
- PureSignal runs as on the 100D (DDC0/DDC1, receivers on DDC2+). It is not offered on the single-ADC boards (Hermes, G2E), whose receivers use DDC0/DDC1. The transport refuses it there and logs it.
- The Odyssey buffer/link counters in status bytes 8-53 are read only on boards 3/4. On the other boards those bytes are the stock firmware's user ADCs.
- Model (`anan::ModelTraits`, P2Protocol). One board id covers several radios and the radio does not say which, so the operator chooses. The setting is `AnanModel_b<board>`; the default is the first in the table. The model decides:
  - Power meter (Thetis `computeAlexFwdPower` / `computeRefPower`). Forward and reflected now have their own constants (V = (raw − offset)/4095·Vref, W = V²/bridge). **ANAN-100D: the reflected power now uses Thetis' own reflected constants (bridge 0.095, 0.5 on 6 m, offset 3) instead of the forward ones; SWR on 6 m changes.**
    - 200D: 0.108 / 5 V / 4, reflected 0.108 (6 m 0.5) / 2.
    - 7000DLE, G2, G2-1K, G2E, Anvelina: 0.12 / 5 V / 32, reflected 0.15 (6 m 0.7) / 28.
    - 8000DLE, Orion MkII: 0.08 / 5 V / 18, reflected 0.08 / 16.
  - Volts: 7000DLE/8000DLE/G2/G2-1K/Anvelina/G2E show the PA rail from User ADC0 (status bytes 57-58, ×5 V/4095×23/1.1; 3.3 V on the G2E). The others keep the supply from bytes 49-50.
  - PA drain current, User ADC1 (bytes 55-56): A = (mV − voff)/sens.
    - 7000DLE/Anvelina: 340/88.
    - G2/G2-1K: 0/66.23.
    - 8000DLE, G2E: 360/120.
    - Published as meter RAD:PACURRENT; the status bar shows "Id … A" while transmitting.
  - Temperature: no ANAN reports one over Protocol 2 (neither Thetis nor deskHPSDR shows one), so the status bar shows volts and current.
  - S-meter: Thetis' per-model meter offset relative to the 100D (7000/8000/MkII/Anvelina +3.86 dB, G2 −5.46 dB). The S-meter Calibration correction comes on top.
- Setup > Radio: the "ANAN Hardware" page is renamed to the connected model. It gains:
  - **Model**: the models of this board. A note says when PureSignal is not available.
  - **Microphone & Connectors**:
    - Thetis "Orion" mic options (200D, 7000DLE, 8000DLE, MkII, Anvelina, G2, G2-1K): Mic PTT, PTT on tip / mic on ring, Mic bias. These are Transmitter Specific byte 50 bits 2-4.
    - G2/G2-1K: balanced XLR input (bit 5).
    - Two-ADC band-pass boards: "RX2 input grounded on transmit".
    - Keys: `AnanMicPttOff`, `AnanMicTipPtt`, `AnanMicBias`, `AnanMicXlr`, `AnanAdc2GndOnTx`.
  - The PA Level and Power Meter Calibration groups stay as they were, with the same keys for every model.
- The picker shows the chosen model name for the board (`AnanDiscovery::modelName`).
- Not verified on the radios: nobody here has a G2 or a 7000DLE. The bits are deskHPSDR's and Thetis', which both drive these radios.

## 128. AUTO ATT did not latch; the AGC-T slider shows what Auto AGC does (05.11.2026, build 170, no number change)

- Operator: the attenuator's AUTO button does not stay on. He also thinks Auto AGC does nothing, and that NF+ is only a trim of the current AGC gain rather than a target.
- AUTO ATT, the cause. The panel's widgets and the backend name the pans differently:
  - the widgets know a pan by the model's neutral id (`0xe1000000`);
  - `AnanUiState` (receiver per pan, ATT ownership) is filled by the backend under its own id (`hl2-0`).
  - So `rxForPan()` always returned −1. The click stored nothing (`setAutoWanted` needs rx 0/1), and the 250 ms poll put the button back off.
  - No log of any session has the line "ANAN AUTO ATT RX… on": the loop never ran.
  - The same miss is why the slice flag and RX Control had to fall back to the slice number (section 123).
- Fix: `AnanUiState::panIdToBackend`, installed by RadioModel (`backendPanIdFor`); `rxForPan` / `ownsAtt` translate first.
- Auto AGC, from the log of 18:40 (build 170). The loop runs: every change of NF+ gives a new AGC gain. But the measured floor stayed at −120.4…−121.0 dBm the whole time, so Auto had nothing to follow.
  - Auto also gives the same gain as the manual AGC-T at NF+ ≈ 29 dB (92.5 against 92.8 dB). Switching Auto on or off at that setting changes nothing you can hear.
- What Auto does (as Thetis): it sets the AGC threshold to floor + NF+ every 500 ms.
  - NF+ is the set point: the noise in the pauses stays NF+ dB below the AGC output level, whatever the floor.
  - Auto moves the gain when the floor in dBFS moves: band, antenna, ATT / LNA, band noise, filter width.
  - With the floor fixed, NF+ only moves the gain, as the AGC-T slider would.
- Now visible: with Auto on, the AGC-T slider (slice flag and RX Control) shows where Auto has put the gain, in AGC-T units. The backend computes it as the inverse of `Hl2DbReference::agcCeilingDb`, `AnanUiState::agcAutoT`.
  - The slider's tooltip shows floor, NF+ and the resulting AGC-T.
  - The operator's own AGC-T comes back with Auto off.

## 129. S-meter: Thetis' peak detector, 50 ms publishing, rise trace (05.11.2026, build 171)

- Operator: the S-meter takes a long time to come up, as if it were tied to the panadapter's FFT settings. It should average by the usual rules and respond fast. Asked what exactly he sees: a slow **rise**.
- The chain, read through:
  - WDSP RXA S-meter `RXA_S_AV`: a power EMA with τ = 100 ms;
  - one reading per 48 kHz-equivalent block, about 47/s;
  - `SMeterSmoother`: attack 0.5, decay 0.15 per reading, published every 100 ms;
  - needle: 45 ms up, 180 ms down.
  - Nothing in it depends on the FFT (the spectrum is computed separately and does not gate the meter).
  - The arithmetic for a carrier step from −116 to −60 dBm: the published value is within 2 dB after about 0.15 s.
  - A slow rise must therefore come either from what is measured (speech on a 100 ms power average comes up syllable by syllable) or from somewhere not yet seen. Hence the trace below.
- Thetis, the reference:
  - the default receive meter is "Signal" = `RXA_S_PK` (`current_meter_rx_mode = SIGNAL_STRENGTH`), the block's highest |IQ|², up on the first block of a signal;
  - "Sig Avg" = `RXA_S_AV`;
  - its display smooths with rise 0.8 / decay 0.2 every `meter_delay` = 50 ms, which per our 21 ms readings is our 0.5 attack. Our decay is a little faster.
- Changes:
  - Detector selectable in Setup > Receive > S-meter Calibration: **Peak** (default, as Thetis) or **Average**. Key `AnanSMeterDetector`; `Hl2RxDsp::setSMeterPeak`, atomic, read per reading on the DSP thread.
    - A steady carrier reads the same on both.
    - Band noise reads higher on Peak, about 10 dB, as in Thetis' "Signal".
  - `SMeterSmoother::kPublishIntervalMs` 100 → 50 ms (Thetis `meter_delay`).
  - Rise trace in the log: "ANAN S-meter rise RXn". It fires when a reading is 15 dB over the minimum of the last 0.6 s, at most once per 10 s. It records 1.2 s of readings as ms:WDSP/published and the number of readings. This shows whether the meter itself is slow, or something after it.

## 130. AUTO ATT from the drawn panadapter floor; 10 MHz reference in HW Diag (05.11.2026, build 172)

- Rise trace, build 171: with a generator at −73 dBm in CW 500 Hz, the published S-meter reading reaches its value within 60–150 ms, at 48 readings/s. The operator: "very fast now".
- AUTO ATT ran up to 31 dB (log 21:15: reconnect with ATT 31 dB).
  - The display is not corrected for the attenuator, so the floor cannot go below the receiver's own noise (about −113 dBm on this trace).
  - The fixed target of −120 dBm was therefore unreachable.
  - The floor also depends on bin width: rate, FFT size and zoom.
- Operator: the target should refer to what the panadapter shows, start at the radio's own floor, and go at most 30 dB above it.
- Done:
  - **Floor estimate.** `ananShownFloorStep` takes the floor of the DRAWN trace: the 1024-point output after zoom resampling and AVG, 20th percentile of the middle 90 %, smoothed 0.8/0.2. The AGC keeps its floor normalised to 1024-point bins (`m_ananFloor`).
  - **Own floor.**
    - Measured when AUTO goes on for the first time, or when the trace's scale changes (rate / FFT size / zoom / AVG) and has held for 2 s.
    - ATT goes to 31 dB for 800 ms + 4 × AVG time. The floor read then is `AnanUiState::ownFloorDbm`, and ATT returns to its previous value.
    - Logged as "ANAN AUTO ATT RXn: own floor on the panadapter …".
    - Kept across AUTO off/on while the scale is the same: it is the receiver's noise, not the antenna's.
  - **Target.**
    - Slider "Floor" from own to own + 30 dB (`kAnanAutoAttTargetRangeDb`), in dBm as on the trace. Until the measurement the range is −140..−80.
    - A saved target below own (the old −120) becomes own + 10 dB.
    - Regulation waits while the measurement runs.
- **10 MHz reference** (firmware layout 0x09), status bytes 28-29, signed: the 10 MHz input's periods over one second of 122.88 MHz, minus 10 000 000; 1 = 1 Hz = 0.1 ppm.
  - 0 / ±1 → locked;
  - a steady other value → not locked, the oscillator's error against the reference;
  - −32768 → no 10 MHz at the input.
  - HW Diag, "Codec and clocks": state, value, ppm and the last 5 s range.
  - The transport logs each state change ("ANAN P2 transport: 10 MHz reference: …").

## 131. Crash report and minidump (06.11.2026, build 173)

- First users. One switched the radio off with its power button while the program was connected. When he switched it on again, the program closed without a word.
- What the log could have said: nothing at the end. The log is written by an asynchronous writer, so its last queue dies with the process. There was no crash handler; Windows offers only its own error report.
- What the path is (read from the code, not reproduced):
  - Power off → no I/Q → after 2 s the transport's watchdog gives linkDown, but the session stays "running". The keepalive keeps sending High Priority with run = 1, and MOX ends at the transport.
  - RadioModel goes to disconnected and discovery resumes.
  - After `kLinkLostGraceMs`, the radio's discovery reply (the radio booted again) makes `maybeAutoConnectAnan` start a full `connectToRadio`. That runs `Hl2Backend::connectRadio` → `P2Transport::start()` (which calls `stop()` first) while the old session's receivers and DSP chains still stand.
  - Link drops where the radio stayed on (03.11) healed by themselves (I/Q resumed, no new connect), so this reconnect after a real power cycle has not been seen in a log.
- New `src/core/CrashDump.h` (header-only, Windows), installed in `main()` once the log directory is known. It catches:
  - an unhandled SEH exception (access violation …);
  - `abort()` (qFatal, a failed assert, `std::terminate`);
  - the CRT's invalid-parameter and pure-virtual-call traps.
- It writes next to the logs:
  - `aethersdr-crash-<date>-<time>.txt`: the exception code, the address, module + offset (readable without symbols), read/write address of an access violation, thread, fork build, the session's log file;
  - `aethersdr-crash-<date>-<time>.dmp`: a minidump (thread info, indirectly referenced memory, unloaded modules), opened in Visual Studio / WinDbg with that build's `AetherSDR.pdb`.
- Then the log writer gets up to 2 s to put its queue on disk. It runs in its own thread, because the writer may be the thread that crashed. Windows' own report still follows (`EXCEPTION_CONTINUE_SEARCH`). dbghelp is loaded at crash time; no build file change.
- The user has an ANAN-7000DLE with stock firmware. The operator has never seen this on the 100D, so it may be specific to that radio. What differs at power-on:
  - A radio that boots knows nothing of the session. Our session after the watchdog drop sends only High Priority (run = 1) every 100 ms. It never sends the General / DDC Specific / Transmitter Specific packets, so the stream cannot come back by itself.
  - When the radio answers discovery again, the program starts a fresh connect on top of the old session after 3 s. That path had never been exercised with a radio that had really restarted.
- Fixes (still build 173):
  - `P2Transport::onKeepaliveTick`: while the session runs and the I/Q is missing longer than `kSilenceTimeoutMs`, the whole configuration goes out once a second: General, Transmitter Specific, DDC Specific (High Priority is already every tick). The expected sequence numbers are cleared, because a restarted radio counts from 0. Log: "no I/Q -- sending the whole configuration …" and "I/Q back". To a radio that is still running this is the same configuration again.
  - `MainWindow::maybeAutoConnectAnan`: after a link loss, a radio that answers discovery again first gets `kRebootRecoverMs` = 5 s for the old session to recover before a fresh connect (`m_ananSeenSinceLostMs`, cleared at every loss). Log: "answers again after the link loss -- waiting …".
  - The fresh-connect path itself is not fixed: the crash place is still unknown. CrashDump reports it if it happens.
- Next step: the 7000DLE user tries build 173; on a crash he sends the logs folder (.txt, .dmp, log).

## 132. Power and Tune sliders: the PA's power law (06.11.2026, build 174)

- Users:
  - one of the power sliders (it is Tune) "is not tied to the per-band level calibration and gives too much power";
  - half the slider gives much more than half the power. Wanted: a linear characteristic in watts, as a formula; the operator checks it with a wattmeter.
- What the code does: RF Power and Tune both go through `applyDrive()`, which includes the per-band PA Level Calibration (`AnanPaCal_<band>`). Every caller was checked: `setTxPower`, `setTune`, `setTunePower`, the band-memory paths, the 250 ms re-apply after a calibration change. Nothing bypasses it.
- Why Tune reads high:
  - its carrier is full scale (amplitude 1.0), while speech averages about 4 dB below its peaks;
  - the law was drive = full × √(percent), Thetis' law, which gives watts ∝ percent only if the PA's output goes with drive². A PA that is already compressing at the 100 % point (100 W) gives more than half the power at 50 %. On a constant carrier that shows at once.
- Change: drive = full × (percent/100)^(1/γ), where γ is the PA's real law (output ∝ drive^γ). Key `AnanPaPowerLaw`, 0.8..2.5, default 2.0 = the old law.
  - Setup > Radio > *model page* > group **PA Power Law**: γ, plus two wattmeter readings (slider 100 % and 50 %) and **Apply**.
  - Derivation: with the law in force g, drive(50 %) = full·0.5^(1/g) and W50/W100 = 0.5^(γ/g), so **γ = g · log2(W100 / W50)**. One measurement is enough; a second one with the new law should give 50 %.
  - The 250 ms re-apply now also follows a change of γ.
- Open: γ is global. If the compression differs much by band, the next step is per band.

## 133. Crash on reconnect: PureSignal inside a TXA channel being rebuilt (06.11.2026, build 175)

- The operator's own crash on build 174 (100D, Radio tab stop → start, auto-connect): `aethersdr-crash-20261106-180804`, exception 0xC0000005, **writing address 0x24** in ntdll + 0x1A12D, GUI thread. Log: "HL2 TXA modulator: channel 1 open", then the crash.
- Minidump (read without PDB: the exe's exports, WDSP symbols and the call sites):
  - GUI thread: the AnanPureSignal status timer → `AnanPureSignal::tick()` → WDSP `GetPSInfo(1, …)` → `EnterCriticalSection(&txa[1].calcc->cs_update)`;
  - the critical section had been deleted (DebugInfo = NULL, hence the write to 0x24).
- Cause:
  - `Hl2TxDsp::buildModulator()` (TX thread) runs on every connect. It destroys the TXA channel (`m_channel.reset()` → `destroy_txa` → `destroy_calcc` → `DeleteCriticalSection`) and creates it again **with the same id 1**.
  - Meanwhile AnanPureSignal keeps calling the PS functions from the GUI thread (status/tick/heat timers, the PS dialog) and feeding `pscc` from the transport thread.
  - Nothing synchronised the two. `Hl2Backend` calls `setTxChannel(id)` every tick, but the id did not change, so PureSignal never knew the channel was new: its WDSP parameters (scheck mode, feedback rate, HW peak, delays) stayed on the old channel only.
  - The race window is short (destroy … create), so it shows only sometimes, on reconnect. It is probably also what the 7000DLE user had.
- Fix:
  - New `src/core/dsp/WdspTxaGuard.h`: a process-wide `std::shared_mutex` `txaRebuildMutex()` and a counter `txaGeneration()`.
  - `Hl2TxDsp::buildModulator()` and `~Hl2TxDsp()` hold the mutex **exclusively** around destroying/creating the channel. The generation goes up on every exit, a refused channel included.
  - Every AnanPureSignal path into WDSP takes `wdsp::TxaShared` (a shared **try**-lock, nested on one thread, outermost only):
    - `status`, `display`, `save`/`restoreCorrection`, `applyWdspParams`, `control`, `feed`, `tick` return their default when a rebuild is running (one poll or one feed block dropped, never waited for);
    - `setEnabled`, `setKeyed`, `updateFeedGate`, `heatTick` still change their state and skip only the WDSP call.
  - `setTxChannel(channel, generation)`: a new generation with the same id counts as a new channel, so the WDSP parameters, the PS control and the feed gate are applied again. Log: "PureSignal: TXA channel 1 generation N".
  - `Hl2Backend` reads the TX DSP's channel id under the shared lock too (it dereferences the channel pointer that the TX thread resets); a skipped tick is picked up by the next one.
- The TX thread waits only for the PS calls already running (microseconds); the GUI thread never takes the mutex exclusively, so there is no deadlock path.
- Compile check: errcheck on AnanPureSignal.cpp, Hl2TxDsp.cpp and Hl2Backend.cpp against build 170, no new errors.

## 134. Radio tab stop → start never came back: the previous session's I/Q clock (06.11.2026, build 176)

- Build 175 on the operator's 100D: no crash any more, but stop → start on the Radio tab never connected again. Every retry, about every 3 s, ended with "no IQ stream from the radio within 2000 ms of start" (log 20261106-182950).
- Each retry got the Discovery reply ("ANAN-100D (board 3, from Discovery)"), so the configuration was sent. But **0.1 s** after start the log already showed "no I/Q -- sending the whole configuration once a second".
- Cause: my own build 173 change. `onKeepaliveTick` decides "the radio restarted" from `m_sinceIq.elapsed() > kSilenceTimeoutMs`, and `start()` did not reset `m_sinceIq`. The clock of the previous session was still running, so a brand-new session looked silent at once. General, TX Specific and DDC Specific then went out again every second over a session the radio had only just started, and the radio never streamed.
- The same stop → start worked on build 172 (log 20261106-172958, 17:38:08: I/Q at once). It was not seen on 173/174 because the reconnect crashed first (section 133).
- Fix: `P2Transport::start()` invalidates `m_sinceIq`. The re-send now runs only in a session that had I/Q and lost it. A radio that never answers is handled by the connect timeout, as before.
- Open, for the firmware: the operator's 100D evidently does not take a repeated General / DDC Specific during a running session without stopping the stream. If so, the re-send would also prevent recovery after a short network drop to a radio that did **not** restart. To check: pull the cable for 3 s while connected; the log should show "I/Q back". If it does not, the re-send must first send run = 0 and then run = 1 (a start edge), or be dropped.

## 135. Not even a fresh start connects: what the radio does answer (06.11.2026, build 177)

- Build 176, a fresh program start (log 20261106-183618): every attempt ends with "no IQ stream within 2000 ms".
  - The Discovery reply comes every time ("from Discovery"), and the re-send from section 134 no longer fires.
  - The program side is the same as the start that worked at 18:29:51 in the same build family, where I/Q came within milliseconds of the configuration. More time does not help, because a running radio answers at once.
  - What changed is the radio. It had just taken about 25 sessions in a row from build 175, each with General / TX Specific / DDC Specific re-sent once a second.
- So the radio (Odyssey firmware) is most likely stuck in some state of its own, and power-cycling it is the test.
- Diagnostics added (P2Transport):
  - the Discovery reply is logged: board, firmware, protocol, DDC count, and status **idle / STREAMING** (byte 4 = 0x03: the radio says it already serves a session);
  - at the connect timeout, what the radio sent in those 2 s: all packets, mic, status, whether a Discovery reply came. Mic or status without I/Q means the session runs but the DDC is not enabled; nothing at all means the radio did not take the session.

## 136. A session is started and stopped as a whole, as in Thetis (06.11.2026, build 178)

- Build 177 after the radio was power-cycled (log 20261106-184136):
  - the first start works;
  - stop → start 4 s later fails: "no I/Q within 2000 ms; from the radio: 1466 packets, mic 1465, status 0, Discovery reply yes". The Discovery reply says **idle**.
  - So the radio takes run = 1 (the mic stream runs) but the DDC sends nothing. Retries over the next 20 s do not help; only power-cycling the radio does.
- What the logs show over time:
  - stop → start that worked: 14 s (06.11 17:37:54 → 17:38:08), 13 min and 29 min apart;
  - stop → start that failed: 6 s (18:30:00 → 18:30:06) and 4 s (18:41:48 → 18:41:52) apart.
  - So the gap between run = 0 and the next start matters, and after a failed start the radio stays stuck. That points at the firmware; the question is with the operator.
- Thetis (ChannelMaster network.c):
  - `SendStart()`: run = 1, then General (1024), DDC Specific / CmdRx (1025), Transmitter Specific / CmdTx (1026), High Priority (1027);
  - `SendStop()`: High Priority with run = 0, then the sockets close.
  - It does not watch the radio's state. The General every 500 ms runs only with the watchdog bit set.
- Changes:
  - The build-173 recovery is gone completely: the configuration re-send in `P2Transport::onKeepaliveTick` and the `kRebootRecoverMs` wait in `MainWindow` (MainWindow*.cpp/.h are back to build 170).
  - `configureSession` sends the four in Thetis' order: General, DDC Specific, Transmitter Specific, High Priority run = 1 (it was General, TX, DDC, HP).
  - `stop()` sends High Priority run = 0 as before. After it, the session state goes back to the program's power-on state: PureSignal off. The next session therefore starts with the same packets as the first one, and PureSignal switches on again in it through the same push as at the first start (`AnanPureSignal::setTxChannel` on the new TXA generation).
    - `start()` uses `stopSession(false)`, so a PureSignal push already queued for the new session is not undone.
  - Log: every packet that starts or stops a session ("ANAN P2 packet General / DDC Specific / Transmitter Specific / High Priority run=1 / run=0"), in hex up to the last non-zero byte, so two sessions can be compared byte by byte.

## 137. Packet log removed; the PA power law is a constant 0.91 (06.11.2026, build 179)

- With build 178, stop → start works for the operator (with the Thetis start/stop order). The start/stop packet hex log (section 136) is removed at his request. Thetis order and the PureSignal reset at stop() stay.
- PA power law: the operator measured γ = 0.91 with a wattmeter (build 174's two-point method). It is now the constant `gamma = 0.91` in `Hl2Backend::applyDrive`. The "PA Power Law" group in Setup > Radio is gone (RadioSetupDialog.cpp is back to build 171), and so is the key `AnanPaPowerLaw`. The 250 ms re-apply key is band:cal again.
- While idle, the program still broadcasts the ANAN Discovery once a second (AnanDiscovery, paused while connected). That is left on purpose: it shows whether the radio is reachable.

## 138. The noise floor in dBm / 500 Hz at the ADC; AUTO ATT holds a margin above the receiver's own noise (06.11.2026, build 180)

- Operator: users do not know what floor to set. The drawn trace's floor depends on the measuring bandwidth, and nothing gives a proper base. Wanted: the panadapter always shows the real floor on its ruler, independent of the DDC rate, the FFT and the zoom, and roughly equal to what the meter measures in a 500 Hz filter. The level is what reaches the ADC (not corrected for the attenuator; the S-meter applies its options if the user wants them). The AUTO target is a difference above the receiver's own noise.
- Why the drawn trace cannot be the base: Hl2Spectrum uses a Hanning window and is tone-normalised, so each bin holds the noise of 1.5·Fs/N Hz (563 Hz at 384 k / 1024, 18 Hz at 48 k / 4096: 15 dB apart on the same band). A zoomed-out view also keeps the peak of the bins under each pixel.
- `Hl2Backend::ananNoiseLineStep(rx, raw, rate)`, on every frame (not during TX):
  - takes the raw frame: all bins of the DDC, before AVG and before the display resampling, the middle 80 %;
  - in linear power, a noise-only bin is exponentially distributed, with median = ln 2 · mean. So mean per bin = median / ln 2. Signals have to fill about a third of the span before they move it;
  - refers it to 500 Hz: + offsetDb (the panadapter's ruler) − 10·log10(1.5·Fs/N) + 10·log10(500), then an EMA in dB (0.8 / 0.2);
  - stores it in AnanUiState::shownFloorDbm (with a timestamp).
- `SpectrumWidget::drawAnanNoiseFloor`: a dashed line at that level, labelled "Noise −NNN dBm/500 Hz (own +MM)", plus a short mark at the receiver's own noise by the dB scale. It is drawn only while the value is fresh (≤ 3 s), so it disappears after a disconnect and during TX.
- AUTO ATT (the loop moved from ananAutoAttStep, which now only feeds the Auto AGC floor):
  - own noise = the same measure at ATT 31 dB: 300 ms settle (frames still at the old ATT), then 800 ms. Measured when AUTO goes on and when the DDC rate changes. Zoom, FFT size and AVG no longer trigger a re-measure.
  - target = own + margin. Margin per receiver: keys `AnanAutoAttMarginDb` / `AnanAutoAttMarginDbRx2`, 0..30, default +10 (they replace `AnanAutoAttTargetDbm*`).
  - The receiver's own noise then costs 10·log10(1 + 10^(−margin/10)) dB of sensitivity: +3 → 1.8, +6 → 1.0, +10 → 0.4, +20 → 0.04 dB.
  - The step law is unchanged: 0.6 × error, dead band ±1.5 dB, at most every 400 ms.
- ATT panel: the row "Noise:" is a margin slider 0..+30 dB ("+10 dB"), plus "now +N" (current noise − own). Its tooltip carries the sensitivity table.
- Check in the log every 10 s per receiver: "ANAN noise floor RXn: X dBm / 500 Hz at the ADC (N-point FFT at F Hz); S-meter Y dBm without options, filter W Hz, ATT A dB". In a 500 Hz CW filter with no signal and S-meter Average, X and Y should agree within about 1 dB. If they do not, the difference is the pan-vs-meter scale (the panadapter's tone reads +6.02 dB, Hl2Spectrum::kToneGainDb) and is to be found, not fitted.

## 139. The trace is the power in 500 Hz, on the meter's calibrated scale (06.11.2026, build 181)

- Build 180, operator (log 20261106-194038):
  - the noise line does not sit on the drawn floor; the drawn floor moves with the zoom and the panadapter width;
  - the calibrated meter reads −120 dBm in 500 Hz, the line −116.
- What the log shows about the line itself:
  - it is independent of rate and FFT: −115.0…−117.1 at 384 / 192 / 96 / 48 kHz, −116.7…−116.8 at 1024 / 4096 / 8192 points;
  - own noise −116.7…−117.4 at every rate.
  - Against the S-meter without options:
    - 500 Hz filter: meter −116.5 / −118.0 / −118.3, line −116.7…−116.8, so about 0.8 dB apart;
    - 2800 Hz filter: meter −105…−110, line + 10·log10(2800/500) = −109.3.
  - So the estimate agrees with the meter. The 3–4 dB to "−120 calibrated" is the operator's S-meter calibration correction, which the line did not carry.
- Changes (`Hl2Backend`):
  - The panadapter trace (and the waterfall, the same resampling) is the power in 500 Hz: each bin + `ananRbwTo500HzDb(rate, N)` = 10·log10(500 / (1.5·rate/N)).
  - A pixel holding several bins takes their **mean** power instead of the peak. The peak of noise bins sits above the noise, which is why the floor rose when zoomed out.
  - Plus `ananDisplayCalDb()`: the S-meter calibration correction (Setup > Receive) and the model's front-end offset against the 100D. Not the ATT / LNA options: the panadapter shows what reaches the ADC, on the meter's calibrated scale.
  - The noise line and the own noise are published with the same calibration (AnanUiState). Internally, `m_ananShownFloor` and `m_ananOwnNoise` stay without it, so AUTO ATT works on differences that a calibration change does not move.
  - The 10 s check line now compares the calibrated line with the calibrated S-meter (without ATT/LNA options).
- The price of a noise-true display: a steady carrier reads its power when a pixel spans about 500 Hz. On narrower bins it reads higher, by 10·log10(500 / pixel or bin bandwidth). At full span with many bins per pixel, a weak narrow carrier is averaged with the noise around it, like a wider RBW on an analyser.
- Open:
  - the amsq squelch line on the panadapter (capabilities' SquelchLevelScale) still uses the per-bin scale, so it is offset by the same normalisation;
  - the Auto AGC floor (`ananAutoAttStep`, per-bin at 1024) is unchanged.

## 140. One scale for the floor: own noise captured in Setup > Receive; Auto AGC and the squelch line on the noise line (06.11.2026, build 182)

- Operator: the program cannot measure the receiver's own noise on the air. On 40 m the antenna noise is far above it even at ATT 31 dB. Wanted:
  - an option in Setup > Receive to capture it, with a reminder that the antenna must be disconnected;
  - a checkbox for the noise line;
  - the squelch line and the Auto AGC on the same scale.
- Own noise (`Hl2Backend::ananOwnNoiseCaptureStep`):
  - The automatic ATT 31 measurement at AUTO on / rate change is gone.
  - Setup > Receive, new group **Receiver Noise Floor**:
    - "Show the noise floor line on the panadapter" (key `AnanShowNoiseLine`, default on);
    - a warning: disconnect the antenna (or terminate it in 50 Ohm), every receiver goes to ATT 31 dB for about a second;
    - **Capture**, with a status line showing what is stored, per ADC, with the date.
  - Capture:
    - every receiver that owns its ADC's attenuator goes to ATT 31 (`ananRxAtt` returns 31 while the capture runs);
    - 300 ms settle, then 800 ms averaging of the noise line;
    - stored per ADC, without the meter's calibration: `AnanOwnNoiseDbmAdc1/2` and `AnanOwnNoiseDateAdc1/2`;
    - then the attenuators go back.
    - No receive data within 3 s gives "is the radio connected?".
  - AUTO ATT waits while there is no stored own noise. The ATT panel's "now" tooltip says where to capture it.
- Auto AGC (`ananAutoAgcStep`):
  - follows the noise line: floor per 1024-point Hanning bin = noise line − `ananRbwTo500HzDb(rate, 1024)` (WDSP's per-bin threshold), called from ananNoiseLineStep;
  - the old estimate (20th percentile of the averaged bins, `ananAutoAttStep`, `m_ananFloor`) is removed. With AVG the 20th percentile sat about 0.5–1 dB under the mean, so the AGC gain moves by about that much;
  - log, AnanUiState::agcAutoFloorDbm and the AGC-T tooltips (RxApplet, VfoWidget) show the calibrated noise line in dBm / 500 Hz.
- amsq squelch line: its scale maps onto per-bin levels. The backend now publishes `AnanUiState::panShiftDb[rx]` (the bin's referral to 500 Hz plus the calibration, as added to the trace). `SpectrumWidget::drawSquelchLine` adds it on ANAN pans, so the line stays where it was relative to the trace and the noise.

## 141. HW Diag on receive: the DUC zero stream while the window is open (06.11.2026, build 183)

- Operator: HW Diag (and with it the 10 MHz reference line) seems to work only while PureSignal is active. Wanted: working while its window is open, off when it is closed.
- The logs:
  - 05.11 20:07–20:10: "status packets 0" on receive; from MOX ON at 20:08:03 they rise at 20 / s; after MOX off at 20:09:36 they stop again (1862 and no more).
  - 06.11 19:40: 0 all session (no transmission), so no "10 MHz reference" line either.
  - So the radio's status packets (port 1025) arrive only while we transmit, although the firmware note says every 50 ms on receive.
  - The one thing a transmission adds on the PC side is the DUC I/Q stream (port 1029). Thetis sends it all the time (ChannelMaster, zeros on receive); this program only while keyed. PureSignal is not the condition: on 05.11 it was on from 20:07:25 and the status still started only at MOX.
- Change:
  - `P2Transport::renewDiagStream(bool)`: while renewed, a 5 ms precise timer sends zero DUC I/Q at the DUC rate (800 packets / s, the shared `m_txSeq`) when not MOX and a session runs. MOX and PTT stay off, so nothing reaches the air. A late timer does not burst more than 16 packets.
  - `HwDiagDialog::tick` renews it every 250 ms while the window is visible; `hideEvent` ends it. With no renewal for 1.5 s it stops by itself.
  - Log: "HW Diag open -- DUC zero stream on receive …" / "… closed -- DUC zero stream off".
- To check: HW Diag open on receive → "Radio status: layout v9, packets N" grows, and the 10 MHz line appears. If it does not, the firmware itself gates the status packets on PTT, and the change is to be reverted.
- Side effect to watch: the first MOX with the window open finds the radio's TX FIFO already running on zeros; the over's lead may overflow it at the start (counted in "TX samples dropped because the FIFO was full").

## 142. Build 183's DUC zero stream reverted; the ATT row is "NF+" again (06.11.2026, build 184)

- Operator on build 183: HW Diag still works only while transmitting. So the DUC stream is not what starts the status packets; the firmware sends them only under PTT. Reverted: P2Transport.cpp/.h back to build 179, HwDiagDialog.cpp/.h back to build 172. The status gating is a firmware matter.
- ATT panel (operator: "everything is clear and works well now"):
  - the margin slider's caption is "NF+" again;
  - the "now +N" label is removed, so the slider has its former width, like the other sliders in the panel;
  - the value still reads "+N dB" (margin above the receiver's own noise).

## 143. One TX voice chain for both microphones: PC / CDC switched at its input (06.11.2026, build 185)

- Operator: "why two processing branches for two microphones? The inputs are to be switched at the input of the whole chain, one chain with the same settings." The trigger: the TX equalizer was not heard in the radio's MON.
- What it was:
  - The MON (codec and PC) is the transmitted IQ demodulated (AnanTxEngine::onIq), so it always played what went on the air.
  - The computer microphone ran through AudioEngine's TX voice chain (TxVoiceProcessor: RN2, gate, EQ, de-ess, compressor, tube, ..., mic gain, limiter).
  - The radio microphone went from the I/O thread straight into AnanTxEngine::micChain → modulator, past all of that. So the EQ was never on the air with the radio mic.
- Now (`AnanMicSource`: 0 PC, 1 CDC, 2 TEST):
  - **CDC**:
    - `Hl2Backend` takes each codec mic packet on the I/O thread (DirectConnection on `codecMicReady`), applies the 120 Hz Butterworth high-pass (input conditioning against bias, hum and the PTT step, now at 48 kHz), and emits `ananCodecMicPcm` (48 kHz mono float).
    - RadioModel relays it on the same thread (no GUI hop) to `AudioEngine::feedExternalMic` (queued, AudioEngine's thread). It builds the canonical stereo float block and runs `runTxVoiceTail`: the former tail of `onTxAudioReady`, moved out unchanged. Same chain, same settings, same taps (monitor, recorder, scope, PC mic meter), same `txTransportPcmReady` → `submitTxAudio` → modulator.
    - `AudioEngine::setExternalMicInput(bool)` (from `ananCodecMicInputChanged` / RadioModel `codecMicInputChanged`, false on every non-ANAN backend):
      - re-prepares TxVoiceProcessor at 48 kHz (or back at the device rate; the device-format path keeps 48 kHz while CDC is the input);
      - while it is set, the computer's capture is read and dropped.
  - **PC**: as before.
  - **TEST**: the two-tone stays AnanTxEngine's own (fromRadio = TEST only). It does not go through the chain, and with it the PC mic is dropped as before.
  - AnanTxEngine still gets every mic packet: they clock the DUC (txDeviceTick).
- The same settings, also for the TXA compressor and leveler (`pushTxCompressor`): on for PC and CDC (before only for the radio mic), off for TEST.
- Mic meter: PC and CDC both show AudioEngine's PC-mic meter (post chain); only TEST shows the backend's codec meter.
- UI: the Phone applet's source list is "PC / CDC / TEST" (was "PC / MIC / TEST"); Setup's combo reads "Radio codec microphone (CDC)".
- Costs:
  - CDC now has the PC path's DUC prime (kTxPrimeComputer 4096 instead of 1536 samples, +13 ms) plus the TxVoiceProcessor's 48 → 24 kHz conversion;
  - the radio-mic TX capture (txcap-mic.wav) and the per-over radio-mic trace come only from AnanTxEngine's micChain, i.e. with TEST now.

## 144. No TEST mic source; ADC overload warning across the panadapter (06.11.2026, build 186)

- TEST (two-tone as a microphone source) was in the Phone applet list since the radio-mic work (AnanMicSource 2). The operator: remove it, the PureSignal widget has its own 2-Tone.
  - The list is "PC / CDC".
  - An old stored 2 reads as PC: `Hl2Backend::ananApplyCodecSettings` takes 1 as CDC and anything else as PC; testTone is always false there.
- ADC overload. What there was:
  - the status byte 5 bits (0 ADC0, 1 ADC1) fed PureSignal's auto-attenuator (takeOverloadLatch), the log ("ADC0 overload ON/off") and the small front-end lamp in the overlay menu (frontEndOverload, ADC0 only).
  - Nothing on the panadapter.
- New:
  - P2Transport has a second latch for the display (`takeDisplayOverload`), so the PS latch is not stolen.
  - The backend's 300 ms ADC timer writes `AnanUiState::adcOverloadAtMs[adc]` and each receiver's ADC (`rxAdc`).
  - `SpectrumWidget::drawAnanAdcOverload`: a large bold half-transparent red "ADCn OVERLOAD" (alpha 120, a fifth of the panadapter height) in the middle of the pan of a receiver whose ADC clipped within the last 1 s, blinking 400 / 400 ms. Drawn in both paint paths, next to the noise line.
  - Setup > Receive > Receiver Noise Floor: "Test the ADC overload warning on the panadapter" shows it without an overload (not saved).
- Limitation: the overload flag comes only with the status packets, which this firmware sends only under PTT (sections 141, 142). On receive the warning cannot appear until the firmware sends status packets on receive too.

## 145. ADC0 overload log line debounced, 200 ms (06.11.2026, build 187)

- The operator has fixed the firmware: status packets now come on receive too (the cause of sections 141, 142 and 144). HW Diag, the 10 MHz line, the overload warning and the PA / supply meters work on receive with no program change.
- The log's "ADC0 overload ON / off" was written on every edge. On receive, on a strong band, the comparator chatters up to the packet rate.
- `P2Transport::handleStatus`: ON at the first report; off only after `kOvlLogDebounceMs` = 200 ms without one (m_ovlLogOn / m_ovlLogLastMs, reset at start). Only the log is debounced. PureSignal's latch, the display latch (held 1 s on the panadapter) and m_adc0Overload / the front-end window are unchanged.

## 146. Noise line and overload warning: the cached overlay is redrawn when they change (06.11.2026, build 188)

- Operator on build 187:
  - the overload test checkbox did nothing until the settings window was closed, and then the warning came and went as he retuned by clicking the panadapter;
  - the noise-line checkbox behaved oddly too.
- Cause: SpectrumWidget draws these lines in its CACHED overlay layer (the same pass as the auto-SQL floor and the squelch line). That layer is redrawn only after `markOverlayDirty()`. Nothing marked it dirty for the noise line, the overload warning or their blink, so they showed only when another change (a retune) redrew the overlay.
- Fix: `m_ananOverlayPoll`, a 100 ms timer in SpectrumWidget. For an ANAN pan it builds a signature of what the two would draw:
  - whether the line is shown, its level (0.5 dB steps) and the own-noise mark;
  - whether the warning is active, the blink phase (400 ms) and the ADC.
  - It calls `markOverlayDirty()` when the signature changes, so the checkboxes act at once and the warning blinks.

## 147. HW Diag: a group of its own for the 10 MHz reference (06.11.2026, build 189)

- Operator: a separate group at the bottom for the 10 MHz reference, with everything that came with the last diagnostics change, and all parameters shown also with "no signal".
- HwDiagDialog: the "Codec and clocks" group is now "Codec" (PLL lock losses and SPI writes only). The new group **10 MHz Reference** (layout v9, bytes 28-29) sits below it:
  - State (no signal / locked / NOT locked), how long it has been in that state, and the number of state changes since the window opened;
  - Reading: the signed value (Hz at 10 MHz, and ppm), or the raw −32768 code with "no signal";
  - Last 5 s: min..max of the readings with a signal, and how many of the readings had one;
  - 122.88 MHz clock: follows the reference / v·12.288 Hz (v·0.1 ppm) from it / on the radio's own oscillator;
  - PHY crystal against the 122.88 MHz clock (bytes 53-54): ppm and Hz at 125 MHz, marked absolute when the reference is locked, otherwise against the radio's own oscillator. The same figure is still in the PHY group.

## 148. 10 MHz group without the PHY crystal line; release notes for build 190 (06.11.2026, build 190)

- Operator: the PHY crystal line stays only in the PHY group, not duplicated in the 10 MHz Reference group. Removed there.
- `docs/release-notes-build-190.md`: the GitHub release text for builds 171–190 (noise floor in dBm / 500 Hz, AUTO ATT margin NF+ and the own-noise capture procedure, overload warning, S-meter detector, 10 MHz group, one TX chain PC / CDC, power sliders, the reconnect fixes, the crash report).

## 149. CAT on a serial port, chosen per OS (07.11.2026, build 191)

- A user reported that CAT does not work. Operator: the problem is in the ports, and they are named and set up differently on Windows and Linux.
- What was there: CatPort = a TCP server (rigctld / TS-2000 / Flex) plus, on Linux/macOS only, a PTY (`$XDG_RUNTIME_DIR/aethersdr/cat-A`). On Windows there was NO serial path at all: a logger that wants a COM port (N1MM, Log4OM, HRD, WSJT-X with a Kenwood rig) could not reach the program. Only TCP worked (Hamlib NET rigctl, port 4532).
- Now each CAT port row has **Serial** and **Baud** (8N1, no flow control; 4800..115200, default 9600) and a state dot:
  - Windows: Off, or a COM port. The list is the same as for the PTT/CW serial setup (SerialPortCombo): present ports with their description, then COM1..COM32 not present. For a logger on the same PC, use a virtual null-modem pair (com0com / VSPE): COM11 here, COM12 in the logger, radio Kenwood TS-2000.
  - Linux: "PTY only" (the PTY stays as before), or a real tty device by its /dev path from QSerialPortInfo. A saved device that is not plugged in is kept as "(not present)". The user must be in `dialout`.
  - The list is re-read every time the popup opens.
- `CatPort`: `setSerialPort(name, baud)`, `openSerial` / `closeSerial` (QSerialPort, HAVE_SERIALPORT; both builds have Qt SerialPort).
  - If the port cannot be opened (not present, busy) or is lost (ResourceError: USB unplugged), it is retried every 3 s. A changed error is logged once.
  - The dot is green when the port is open, red with the error in the tooltip.
  - TS-2000 / Flex on serial go through `SmartCatSession`, the same as a TCP client, so AI push and PTT release on close are the same. `SmartCatSession` now takes any QIODevice (`ownsDevice` false for the serial port; disconnect is wired only for sockets). Rigctld framing on serial goes through its own `RigctlProtocol`.
  - `start(0)` means no TCP server. `isRunning()` is an `m_running` flag. A port runs with a TCP port ≥ 1024, a serial port, or both. A failed TCP bind no longer stops the serial side.
- MainWindow::applyCatPortCount: keys `CatPort_N_Serial`, `CatPort_N_SerialBaud`.
- Checked: syntax against the real Qt SerialPort headers (github.com/qt/qtserialport 6.8), with and without HAVE_SERIALPORT. errcheck diff empty. Not run on Windows or Linux yet.

## 150. Thetis CAT dialect (07.11.2026, build 192)

- Operator asked for it after the question "which CAT is emulated". Before this build there were Rigctld, TS-2000 (ID019) and Flex (TS-2000 + SmartSDR ZZ). On an ANAN, Flex answers `ID919;`, an ID no logger knows.
- New dialect **Thetis** (`CatDialect::Thetis`, key "Thetis"): the TS-2000 base with `ID019;` plus Thetis' ZZ set, taken from its source (Console/CAT/CATCommands.cs, field widths from CATStructs.xml). It works on TCP, PTY and serial.
  - `SmartCatProtocol::setThetisDialect`; `processThetisZZ` handles the ZZ commands, and the Kenwood commands are shared with the other dialects.
  - Where Flex and Thetis use the same ZZ name for different things, the Thetis meaning is used: ZZRX is the RX1 attenuator (in Flex it is PTT off), ZZFT is the TX frequency (in Flex it is split), and ZZMD 11 is DRM (in Flex it is NFM).
- Implemented:
  - VFOs and mode: ZZFA, ZZFB, ZZFT; ZZMD and ZZME with Thetis mode codes 00..11, and a set is refused if the slice has no such mode; ZZIF (36 characters, with the step code and mode as in Thetis); ZZBS (read only, band from the frequency); ZZST; ZZSA and ZZSB.
  - TX: ZZSP and ZZSW (split); ZZTX; ZZTU (tune; a tune this client started stops when the client goes away); ZZPC; ZZTO (tune power); ZZKS.
  - Receive: ZZAG, ZZMA, ZZMB; ZZGT (0 FIXD, 2 SLOW, 3 MED, 4 FAST; LONG and CUSTOM answer "?;"); ZZSM ((dBm+140)·2 from MeterModel::sLevelForSlice, arg 0 = VFO A, 1 = VFO B); ZZFL and ZZFH (sign + 4 digits); ZZNA, ZZNR, ZZNT.
  - RIT and XIT: ZZRT, ZZXS; ZZRF and ZZXF (sign + 4 digits, ±9999); ZZRC, ZZXC; ZZRU, ZZRD, ZZXU, ZZXD (no value moves 10 Hz; a 5-character value sets the offset).
  - Identity and status: ZZAI (one digit); ZZPS (always 1; ZZPS0 answers "?;"); ZZID (no answer); ZZFM = 1; ZZVN = AETHERSDR_VERSION padded to 12 characters; ZZZM = the model name without "-" or spaces (ANAN100D).
- Not implemented, so they answer "?;":
  - ZZRX, ZZRY, ZZPA: the ATT and preamp belong to the panadapter and cannot be reached from CAT yet;
  - ZZBS with a value, ZZFI (filter presets), ZZRM;
  - the console, mixer, VAC and display commands.
- In the applet, the Dialect list has gained "Thetis" and a tooltip that explains the four dialects. `catDialectFromKey` / `catDialectKey` in CatPort.h are now the only place that maps keys to dialects (MainWindow, MainWindow_Session, applet).
- Checked: errcheck diff is empty, with and without HAVE_SERIALPORT. Not run with a logger yet.

## 151. PA power law back in the settings, as the slider mid-scale (07.11.2026, build 193)

- Operator: bring back the setting for the middle of the RF Power scale; Tune uses the same value. Another transceiver has a quite different characteristic.
- Since build 179 the law was the constant γ = 0.91 in `applyDrive`. It is now set as the **mid-scale point**: the drive, in % of the calibrated full drive, that the sliders give at 50 %.
  - drive(p) = full·cal·(p/100)^e, with 0.5^e = mid/100, so e = −log2(mid/100).
  - Key `AnanPaMidDrivePct`, 35..80 %, default 46.7 % = 0.5^(1/0.91). The default behaves exactly like build 179. 70.7 % is the ideal square-law PA (the old √ law).
  - `Hl2Backend::ananPaMidDrivePercent()` (static) and the constants kAnanPaMidDrive{Default,Min,Max} live in Hl2Backend.h. The setup page uses the same ones.
  - The 250 ms re-apply key is band:cal:mid again, so a change takes effect at once, also during TX.
- Setup > Radio > ANAN page, group **PA Power Law** between PA Level Calibration and Power Meter Calibration:
  - "Drive at slider 50 %" with a Default button;
  - two wattmeter readings (slider 100 % and 50 %) and **Apply**.
  - Derivation: the PA's law is W ∝ drive^g, so with mid m in force W50/W100 = m^g. Then g = ln(W50/W100)/ln(m), and half the power at 50 % needs m′ = 0.5^(1/g).
- Checked: errcheck diff empty. Not run.

## 152. Protocol 1, step 1: receive (07.11.2026, build 194)

- Operator: support the old protocol for the same ANAN boards with Protocol 1 firmware. Branch as close to the transport as possible, no second processing chain; parse the protocol as Thetis does. HL2 is left for later. Receive first; a user will test it.
- Design: one transport, one radio state. `P2Transport` keeps every member (frequencies, ATT, ADC per receiver, antennas, Alex, OC, dither/random, mic options, CW keyer). Only the wire differs:
  - Protocol 2: General / DDC Specific / High Priority / TX Specific.
  - Protocol 1: the 17 C&C registers of the EP2 rotation are built from the same members.
  - The receivers go into the same alignment queues (`m_pending` → `deliverAlignedBlocks` → `iqBlocksReady`), so everything above the transport stays as it is.
- Framing reused from hl2/MetisProtocol (the same protocol as HL2): `discoveryRequest`, `parseDiscoveryReply`, `metisCommand`, `ep2Packet`, `ep2WriteTxAudio`, `ep6SamplesMulti`.
- New anan/P1Protocol.{h,cpp}, after Thetis:
  - `p1Cc(index, P1CcState)` = WriteMainLoop cases 0..16;
  - `parseP1Status` = the EP6 C&C reading in MetisReadThreadMainLoop: overload addr 0 / 4, exciter / fwd / rev / user ADC / volts addr 1..3, PTT / dot / dash;
  - `p1MicSamples`;
  - Alex bits in Protocol 1 order (SetAlexHPFBits / SetAlexLPFBits);
  - P1 board type → P2 id: 1 Hermes, 4 Angelia → 3, 5 Orion → 4, 10 Orion MkII → 5;
  - the DDC plan of UpdateDDCs: ANGELIA/ORION/ORION MkII nddc 5, RX1 = DDC0 (DDC1 its partner on RX1), RX2 = DDC2, DDC3/4 on TX; HERMES nddc 4, RX1 = DDC0, RX2 = DDC1; HERMES II nddc 2 (chosen by the model setting ANAN-10E/100B, Protocol 1 reports HERMES for both).
- Session:
  - `start()` sends the P2 Discovery and the P1 one (EF FE 02).
  - A P2 reply configures at once. A P1 reply waits for the board wait (300 ms) in case a P2 one follows, then the P1 session starts.
  - Start as Thetis: two EP2 packets with config + TX / RX1 frequency, then EF FE 04 01. Repeated on every keepalive (100 ms, up to 20 times) until EP6 arrives.
  - Stop: EF FE 04 00 twice.
  - The P2 senders (sendGeneral/DdcSpecific/HighPriority/TransmitSpecific) do nothing on a P1 session. The setters still update the members, and the rotation sends them within ~9 EP2 packets.
- EP2 runs on the radio's clock: rounds × 48000 per EP6 packet accumulate, and one EP2 goes out per 126 × rate. Each EP2 carries 126 samples of speaker audio from the same `m_spkQueue`, L/R swapped as in Thetis. The P2 speaker thread stands aside (its wait predicate checks `m_p1Atomic`).
- Overload goes to the same latches (panadapter warning, PureSignal). The coupler / volts raw values go to the same members.
- Limits:
  - Protocol 1 rates stop at 384 ksps. `Hl2Backend::iqRates()` returns only the first 4 when `m_ananP1` is set: at connect from the discovery sweep (`isProtocol1Host`), at linkUp from `isProtocol1()`. A wider span is brought down to 384 at linkUp.
  - RX1/RX2 only.
  - MOX is refused (log line); PureSignal off; EP4 wideband and the mic: next steps.
- Discovery (AnanDiscovery) also sends EF FE 02. A P1 radio shows as "<model> (Protocol 1)" with the same serial as under P2 (the same settings).
- Checked: errcheck diff empty. A unit test of the encode/decode (EP6 nddc 5: 30 rounds, DDC2 value, mic decimation, status addresses 1 and 4; C&C 0/2/11/12 bytes match the Thetis layout). Not run on a radio.

## 153. Protocol 1: the wide bandscope (EP4) (07.11.2026, build 195)

- Neither Thetis (networkproto1.c drops EP != 6) nor deskHPSDR (old_protocol.c "case 4: not implemented") has it. The operator attached "Metis – How it works" V1.33 (VK6APH), which specifies:
  - start command bit 1 = wide bandscope;
  - EP4 packets `EF FE 01 04 seq` + 512 × 16-bit raw ADC samples;
  - start at a sequence number whose low 5 bits are 0, accumulate 16384 words (32 packets).
- Implemented on the same path as Protocol 2 (`widebandFrame(adc, samples)` → Wideband Monitor, 122.88 Msps, 16384 samples):
  - `setWideband` over P1 sends `EF FE 04 03` / `01` at once. The start command already carries bit 1 if the monitor was on.
  - `handleP1Ep4` assembles the 32-packet buffers, dropping one on a gap, and emits at most one frame per `updateMs`: the radio sends buffers back to back.
  - ADC1 only; asking for ADC2 logs a warning.
- Byte order: the document does not give it (the headers are big-endian; HL2's compatible gateware is little-endian). It is decided once per session from the first packet: the reading with the lower RMS wins (read the wrong way round, the ADC noise lands near full scale). Logged: both RMS values and the choice. To confirm on the radio.
- Checked: errcheck diff empty. Not run.

## 154. Protocol 1 wide bandscope as cuSDR does it (07.11.2026, build 196)

- Operator: cuSDR has a working bandscope. The folder link was not visible to this session, so the cuSDR sources came from GitHub (xdiegool/cuSDR64, fallenangel3k/cusdr3; DL3HVH's code).
- cuSDR:
  - DataIO::readData EP4, KISS Konsole's "three ifs": start when `(31 & seq[7]) == 0`, append 32 packets, enqueue.
  - WideBandDataProcessor decodes `s = (qint8)buf[i+1] << 8 | (quint8)buf[i]`: 16-bit signed, **little-endian**.
  - 16384 samples (BIGWIDEBANDSIZE) for Hermes firmware > 11 / Mercury > 32, else 4096.
  - Started with `EF FE 04 03` (0x01 without the bandscope).
- Ours already matched that (start condition, 32 packets, start 0x03). Changed: the byte-order guess from build 195 is gone; the order is fixed little-endian, as cuSDR. Old Hermes firmware with 4096-sample buffers is not supported (the Wideband Monitor takes 16384).
- Checked: errcheck diff empty.

## 155. Protocol 1 transmit (07.11.2026, build 197)

- Operator: transmit over Protocol 1, PureSignal aside; resample to connect to our processing.
- The whole TX path stays at the Protocol 2 DUC rate, 192 ksps: Hl2TxDsp, AnanTxEngine, queueTxIq, fillTxPacket (tone, host CW, RF Delay, priming, the 2 ms rise), statistics, TX panorama. Only the last step differs:
  - per EP2 packet (sent on the radio's clock, one per 126 codec samples), `sendP1Ep2` takes 504 samples with `fillTxPacket(buf, 504)`;
  - accounts them with `accountTxSamples` (taken out of txPumpLocked, shared by both protocols);
  - decimates them 4:1 to 126 (`P1TxDecimator`: 96-tap Blackman windowed sinc, fc 20 kHz; measured flat to 10 kHz, −1.4 dB at 18 kHz, −38.7 dB at 24 kHz, −75 dB at 28 kHz);
  - writes them with `hl2::ep2WriteTxIq`.
- `txPumpLocked` (the P2 DUC sender) does nothing on a P1 session. txDeviceTick keeps its bookkeeping.
- The radio mic: `p1MicSamples` (one word per rate/48k rounds) is gathered into 64-sample packets → `codecMicReady` + `m_micArrived`, exactly as the P2 port-1026 stream. AnanTxEngine then modulates on the radio's clock as before. Upsampling is not needed: the codec is 48 kHz on both protocols.
- C&C while MOX:
  - C0 bit 0 in every frame;
  - drive (C0 0x12 C1; 0 in CW demo);
  - PA bit;
  - TX antenna;
  - Alex LPF on the TX frequency (legacy boards);
  - OC TX row;
  - the 6 m preamp off;
  - ADC0 TX attenuation (0x1C C3 = m_txAttDb); ADC1 31 dB (Thetis).
- Radio PTT and the keyer paddles come from EP6 C0 (every frame), into the same updateRadioPtt / hang logic as P2. The MOX refusal of build 194 is gone.
- Not yet: PureSignal over P1 (the DDC3/4 pair), the FPGA CW keyer's IQ-LSB keying (Thetis cw_enable: dot/dash/cwx bits in the I/Q words).
- Checked: errcheck diff empty, decimator response measured. Not run on a radio.

## 156. Release archive build 197; Tools → Open Logs (07.11.2026, build 198)

- Release archive `AetherSDR-ANAN-build197-win64-portable.zip`: the build 190 archive with the new exe and `README-build197.md` (= docs/release-notes-build-197.md, English, covering builds 191-197). 87 files, SHA-256 fa9aeaaf49143e73d77d24140a7cc5905bce4e6e191f0ebe0fbe72a782a56af1. Saved to Desktop\AetherSDR-Release.
- Operator: there is no Help menu (removed in this port), so Support & Diagnostics' "Open Log Folder" cannot be reached. **Tools → Open Logs** is added at the very bottom, after a separator: it opens the folder of `LogManager::logFilePath()` (`%LOCALAPPDATA%\AetherSDR\logs`) in Explorer.

## 157. Display panel trimmed: Log Avg, fixed FPS, floor always on (07.11.2026, build 199)

- "Wt Avg" renamed **Log Avg**: it averages the frames in dB (Hl2Spectrum log average). The noise
  reads about 2.5 dB lower and grassier (dB std 5.6 vs 4.3 dB per frame). Off = power average, the
  true level, matching the noise floor line and the S-meter. Tooltip says so.
- Removed rows: **FFT FPS** (fixed 25: kFixedFftFps in MainWindow_Wiring, requested for a locally
  shaped pan; a saved FPS is no longer restored), **FFT Smooth** and **De-comb** (both off:
  SpectrumWidget::loadFftTraceShaping no longer reads the saved object), **NF Range**.
- **FFT Floor:** no Auto button; tracking always on (m_noiseFloorEnable = true at load, the Display
  reset sets it on). The slider spans the full row and is the floor line's height above the
  bottom: widget position (% from top) = 100 - slider. Minimum = floor at the bottom. Default 25
  (= position 75, as before).
- Members of the removed rows stay null; all syncs check them. tests/noise_floor_range_test and
  spectrum_overlay_fft_fps_bounds_test look up the removed sliders (tests are off in this port).
- Files: gui/SpectrumOverlayMenu.cpp, gui/SpectrumWidget.cpp, gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 158. Display panel: NB Blank removed, Black Level always SW (07.11.2026, build 200)

- **NB Blank** did nothing on an ANAN. The blanker (WaterfallImpulseBlanker, #277) replaces a whole
  waterfall row when the row's mean dB rises more than 5..95 dB (slider; default 15) above the
  mean of the last 32 rows. ANAN rows are averaged FFT frames; an impulse is spread over the
  frame and the averaging and lifts the row mean by far less, so the test never fired. A useful
  waterfall blanker would have to work per bin before averaging (Hl2Spectrum), or the RX noise
  blanker does the job. Removed: the row is gone and the blanker is forced off at load
  (m_wfBlankerEnabled = false). Members stay null; syncExtraDisplaySettings checks them.
- **Black Level:** the Off/SW/HW button is removed; the mode is always SW (client-side noise-floor
  estimate): SpectrumWidget forces m_wfAutoBlack = true and m_wfAutoBlackRadioSide = false at load;
  syncDisplaySettings applies mode 1. The slider spans the full row and is always the offset from
  the noise floor (50 = at the floor). The Kiwi-mode sync checks the null button.
- makeRowWithBtn has no user left and is removed.
- Files: gui/SpectrumOverlayMenu.cpp, gui/SpectrumWidget.cpp, ForkBuild.h.

## 159. NB Blank made to work on the ANAN (07.11.2026, build 201)

- Operator: "why not make NB Blank work? Feed it clean rows; the band is jumpy right now."
- The rows were already clean: Hl2Backend gives the waterfall the frame BEFORE FFT AVG, and
  RadioModel samples one frame per row interval (no integration). So build 200's reason was wrong.
  What was wrong was the test: on an absolute dB row it took the row MEAN and a margin of 1 dB per
  slider step (5..95 dB, default 15 dB) above the last 32 rows. Our error, two parts:
  - scale: one frame's noise floor spreads ~0.2 dB frame to frame (median of 1024 bins, measured
    by simulation: 0.197 dB), so a 15 dB margin is ~75 sigma; a broadband impulse lifts the floor
    by a few dB, never 15;
  - level: the mean of dB bins moves with strong signals coming and going; the median is the
    noise floor and they do not move it.
- Now (WaterfallImpulseBlanker.h): an absolute row's level is its median (rowMedian()); margin
  0.2 dB per step (kDbPerThresholdUnit 20: 1..19 dB, default 3 dB = 15 sigma). The Flex tile test
  is unchanged.
- New rule: after kMaxRejectedRun = 8 rejected rows in a row the level is taken as real (retune,
  gain change, band opening): the ring restarts from that row and it is drawn. Without it a lasting
  step froze the waterfall while the capped ring crept up (1 dB per row / 32).
- NB Blank row back in the Display panel (On/Off + slider), label in dB; saved enable restored.
  Menu sync rounds the slider value (1.15f - 1 truncated to 14).
- Untested on the air. tests/waterfall_impulse_blanker_test checks the old 5/15/95 dB margins
  (tests are off in this port).
- Files: gui/WaterfallImpulseBlanker.h, gui/SpectrumWidget.{h,cpp}, gui/SpectrumOverlayMenu.cpp,
  ForkBuild.h.

## 160. NB Blank: lower the impulse row instead of substituting it (07.11.2026, build 202)

- Operator, build 201 on a jumpy band: "it works now, but many black stripes; not sure it is
  better".
- Cause (reasoned, not measured): a rejected row was replaced by the last CLEAN row. On a band
  whose floor keeps jumping, the last clean row is an older, quieter one, so each substitution
  draws a row darker than its neighbours (with SW auto-black the noise sits near black), and every
  signal has a gap there. The fill path (no cached row: the floor colour) is black outright.
- Now, for an absolute dB row only: the row keeps its own content and every value is lowered by
  the floor's rise (row median - baseline) -- buildLevels(shift) in updateWaterfallRow, the levels
  loop wrapped in a lambda. The floor lands on the baseline, signals continue through the impulse.
  The last-good cache is not updated by such a row. The Flex tile path still substitutes (#277).
- Not addressed: the SW auto-black estimate still takes the impulse rows (5 % per row).
- Files: gui/SpectrumWidget.cpp, gui/SpectrumOverlayMenu.cpp (tooltip), ForkBuild.h.

## 161. NB Blank: soft knee, auto-black corrected (07.11.2026, build 203)

- Operator, build 202: "much better, but some striping is visible".
- Cause: a hard step at the margin. A row whose floor rose 2.9 dB (margin 3) was drawn as is, one
  at 3.1 dB was pulled down by the whole rise: on a jumpy band neighbouring rows differ by up to the
  margin. Second, the SW auto-black took every raised row's floor (5 % per row) before the blanker,
  so the black level rode up behind each burst.
- Now: every absolute row (ring with >= 8 rows) is lowered by floorShiftDb(rise, margin):
  0 at or below the baseline, rise^2 / margin below the margin, the whole rise above it. What is
  left standing is at most margin / 4 (0.75 dB at 3 dB), continuous, none above the margin. The
  impulse decision (ring cap, run rule, no caching as last good) is unchanged.
- Auto-black: after a shift, m_autoBlackThresh -= 0.05 * shift -- the same as if the EMA had seen
  the lowered row (its noise estimate is linear in a whole-row offset).
- Files: gui/WaterfallImpulseBlanker.h (floorShiftDb), gui/SpectrumWidget.cpp, ForkBuild.h.

## 162. Panadapter filter-edge drag stops at the carrier (07.11.2026, build 204)

- Operator: on the panadapter, dragging the inner filter edge must not cross the carrier, as in RX
  Controls; LSB may take any negative value, USB any positive.
- RX Controls gets it from the receive filter ranges the backend declares (Hl2Backend
  capabilities: USB/DIGU low 0..11990, LSB/DIGL high -11990..0; ModeFilters::acceptsFilterEdges
  refuses anything outside). The panadapter drag (SpectrumWidget::mouseMoveEvent, m_draggingFilter)
  sent any value through filterChangeRequested -> SliceModel::setFilterWidth, unchecked.
- Now the dragged edge is clamped: USB/DIGU low >= 0, LSB/DIGL high <= 0, and in every mode the two
  edges keep >= 10 Hz apart (the declared minimum width). The edge stops at the limit instead of
  being refused, so it follows the mouse up to it.
- Files: gui/SpectrumWidget.cpp, ForkBuild.h.

## 163. Panadapter: symmetric edge drag in AM / SAM / FM (07.11.2026, build 205)

- Operator: in AM, FM, SAM dragging one edge should move the other symmetrically, and check that
  an edge cannot be dragged past the carrier to the other side.
- Before: nothing of the kind -- the drag moved one edge only, to any value (it could cross the
  carrier and the other edge). The declared AM/SAM ranges (low -12000..-10, high 10..12000) were
  only checked in RX Controls.
- Now in AM, SAM, FM, NFM: the dragged edge stays on its own side of the carrier, at least 10 Hz
  from it (the declared AM/SAM limit), and the other edge is its mirror: low = -edge,
  high = +edge. An asymmetric passband becomes symmetric on the first drag.
- Files: gui/SpectrumWidget.cpp, ForkBuild.h.

## 164. Filter drag overloaded the I/O thread: edits coalesced (07.11.2026, build 206)

- Operator: dragging a filter edge (panadapter or RX Controls) loads the program hard: audio
  breaks up, the panadapter jumps and freezes, finally the audio stops and comes back only after a
  restart.
- Cause: every edge the drag produced went through: SliceModel -> Hl2Backend::setSliceFilter ->
  queued Hl2RxDsp::setFilter on the hl2-io thread -> WdspChannel::setFilter, which re-designs the
  RXA bandpass AND the NBP filter (8192 taps, minimum phase: ~14 ms per edit by the header's own
  measurement) under the global FFTW lock. A drag gives tens of edits a second; hl2-io is the
  thread that processes IQ and paces EP2, so it fell behind. The return value of setFilter (refused
  while a callback is in flight) was also ignored: the edge in m_config and the one in the channel
  could disagree.
- The permanent silence is not explained by the code read; if it happens again with this build,
  the log is needed.
- Now Hl2RxDsp::setFilter keeps the newest edges and applies at most one edit per 60 ms
  (kFilterApplyMinMs); processIqBlock() applies a held edit when due and retries a refused one. The
  drag's last position is always applied, within 60 ms.
- Panadapter AM/SAM/FM symmetric drag: minimum 100 Hz from the carrier (WdspChannel refuses an AM
  passband inside +-100 Hz), was 10.
- Files: core/backends/hl2/Hl2RxDsp.{h,cpp}, gui/SpectrumWidget.cpp, ForkBuild.h.

## 165. RX Controls passband widget: mouse wheel on an edge (07.11.2026, build 207)

- Operator: in the filter-edge widget, with the mouse over an edge, the wheel should move it:
  up +50 Hz, down -50 Hz.
- FilterPassbandWidget::wheelEvent: the same zones as a press (left of the lo line = low edge,
  right of the hi line = high edge); the centre zone and a drag in progress pass the wheel on.
  50 Hz per notch, notches accumulated from angleDelta (touchpads). The radio's width range
  (constrainPassbandWidth) keeps the other edge as it is, and the edge nearest the carrier stops at
  it: USB/DIGU low >= 0, LSB/DIGL high <= 0, AM/SAM/FM 100 Hz either side. The result goes out
  through filterChanged like a drag (RxApplet checks acceptsFilterEdges).
- Only RxApplet uses this widget.
- Files: gui/FilterPassbandWidget.{h,cpp}, ForkBuild.h.

## 166. Speaker silent for good: firmware FIFO-full bug (07.11.2026, firmware sources only)

- Log 20:30-20:33 (build 207): speaker FIFO filled to 2000 of 2048 with overflows (20:31:21),
  then stood at 1940 (20:31:36), then "idle" with every sample overflowing (240000 per 5 s) until
  the program was restarted. Mic packets kept coming: the codec ran, the playout had stopped.
- Fixed in Odyssey_3_2.0_ANP/Sources (audio_I2S.v, Odyssey.v; backup in backup_2.0/before_spkfull).
  Not yet built. Details in the firmware note below.
- The program side (pacing the speaker packets, a separate DSP thread) is still to be done.

### Speaker FIFO stuck silent when full (fixed in the sources 2026-11-07, not yet built)
Seen on the air (AetherSDR log 2026-11-07 20:31): after a burst of speaker packets from the PC the speaker FIFO filled up, the audio faded out and never came back (status: speaker not playing, FIFO full, every sample overflowing, 240000 per 5 s) until the program stopped the radio (run = 0).
Cause: Rx_Audio_fifo is 2048 words with an 11-bit rdusedw, which reads **0 when the FIFO is full**. audio_I2S took that 0 as "below 260", faded out and stopped reading (holdoff); with no reads the FIFO stayed full, rdusedw stayed 0 and "> 767" never came true again.
Fix: the FIFO's rdfull output (was unconnected) goes to audio_I2S as `full`: low water is `!full && rdusedw < 260` (also fade_evt), high water `full || rdusedw > 767`. Status byte 38-39 (speaker level) is `{4'd0, rdfull, rdusedw}`, so a full FIFO reads 2048 instead of 0. Backup of the previous files: Sources/backup_2.0/before_spkfull.
The bursts themselves come from the PC (speaker packets leave as soon as WDSP produces them; a stall on the program's I/O thread delays and then bunches them); the program side is to be paced separately.

## 167. Filter design off the DSP thread, as in Thetis (07.11.2026, build 208)

- Operator: the pacing proposal is out (the speaker FIFO level is outside the official protocol;
  most radios do not report it), the firmware fix helps only our radios -- look at how Thetis does
  it.
- Thetis (ChannelMaster cmbuffs.c, network.c; wdsp bandpass.c, nbp.c, fircore): the network thread
  only copies IQ into a ring (Inbound) and releases a semaphore; a cm_main thread per stream runs
  WDSP. Filter edits are made on the GUI thread: SetRXABandpassFreqs designs the new impulse and
  the fircore masks THERE, into the idle half of fircore's double buffer, and only the swap takes
  csDSP / the fircore update lock (one block at most). RXANBPSetFreqs the same (setImpulse_fircore
  with flip). The DSP thread never waits for a design and never drops a block for it.
- Ours: Hl2RxDsp::setFilter ran on hl2-io (WDSP, spectrum, speaker audio, EP2 pacing) and
  WdspChannel::setFilter wrapped the call in the control-operation handshake, during which
  processIq() returns Busy -- the block is dropped. So every edit cost the design time (~14 ms) on
  that thread plus dropped audio, and build 206's rate limit only spaced the stalls out.
- Our vendored WDSP has the same setters (upstream/bandpass.c, nbp.c, firmin.c calc_fircore), so
  the Thetis arrangement applies as is:
  - WdspChannel::setFilterLive(): same validation as setFilter() (WBFM excluded: its band-pass is
    ours, inside processIq, unlocked); g_setupMutex (FFTW planner: minimum phase plans) around
    SetRXABandpassFreqs + RXANBPSetFreqs; no control-operation handshake. Excludes control
    operations by a flag pair (m_liveSetter / m_controlOperation, seq_cst, claim-then-look): it
    backs off if one is in progress; beginControlOperation() waits for a running design to end.
  - Hl2RxDsp: setFilter() hands the newest edges to a worker thread (filterWorkerLoop, started on
    the first edit); edges arriving during a design replace the waiting ones. The worker holds
    m_channelLifeMutex while it uses m_channel; installRebuiltChannel() takes it to swap the
    channel, the destructor stops the worker first. Refused edits retried every 20 ms, dropped
    with a warning after 1 s. The filter LENGTH (CW, RXASetNC) stays a control operation on hl2-io.
  - Build 206's rate limit removed.
- Not done (yet): splitting network receive from WDSP (Thetis' Inbound/cm_main). With the design
  gone from hl2-io, check first whether it is still needed.
- Files: core/dsp/WdspChannel.{h,cpp}, core/backends/hl2/Hl2RxDsp.{h,cpp}, ForkBuild.h.

## 168. Pan drag without retuning the DDC (07.11.2026, build 209)

- Operator (build 208 good: filter edits clean, also in CW): while dragging the panadapter the bins
  go jelly-like and lag, some fall out of the filter, the audio distorts. Redrawing the pan must not
  redo the FFT -- only the bins that are replaced; in zoom there is margin on the sides.
- Cause: Hl2Backend::setPanCenter retuned the DDC on every drag step (~30 a second): NCO command
  to the radio, WDSP shift and spectrum axis (shiftBins) at once. The IQ arrives on the new NCO a
  few ms after the command, so for those ms the shift did not match the IQ (audio off-tune, the
  distortion) and the average mixed frames of two axes (the jelly); the frame in flight was also
  labelled with the new centre.
- Now:
  - The view has its own centre: Receiver::viewOffsetHz (valid while ncoHz == viewForNcoHz, so any
    other NCO move -- slice tune out of the window, band memory -- re-centres it). panCenterHz()
    = NCO + offset; emitPanState reports it; the spectrum lambda crops the FFT at the offset (the
    waterfall coverage too).
  - setPanCenter moves only the view while it fits the captured band (during a drag up to 96 % of
    the rate, i.e. into the DDC filter's roll-off edges). In zoom that covers most drags with no
    retune at all. When the view would leave it, moveNcoForView() retunes with a lead (the view
    lands near the trailing side): at full span (view 80 %) one retune per ~55 kHz of drag at
    384 ksps instead of 30 a second.
  - 400 ms after the last centre command settlePanViews() brings a view left in the roll-off back
    into the clean 80 % by the smallest retune.
  - Each retune: shift + Hl2RxDsp::ncoMoved(delta): the average moves by delta (shiftBins), the
    partial window is dropped and the spectrum is not fed for 12 ms (old-NCO IQ in flight); the
    audio dips for 20 ms (3 ms fade, silence, 5 ms fade back) over the time shift and IQ disagree.
    A jump of more than half the span still drops the average.
- Untested on the radio. Assumed: bin index increases with frequency in the conjugated spectrum
  (Hl2RxDsp's handedness note), as the pan axis does.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, core/backends/hl2/Hl2RxDsp.{h,cpp}, ForkBuild.h.

## 169. Waterfall from the whole captured band (07.11.2026, build 210)

- Operator: in zoom the panadapter has margin when dragged, the waterfall has none -- let it work
  from all available bins and draw what the screen needs.
- Was: the waterfall coverage was the viewport crop (resample(raw) over the view,
  viewportBounds = true; RadioModel gave it the pan's bounds), so nothing outside the view existed.
- Now the spectrum lambda sends the whole captured band as the coverage: 96 % of the rate (the
  drag reach of build 209), true RF bounds from the NCO (bin k at (k - n/2) * rate / n),
  viewportBounds false, at most 8192 bins (mean power per output bin; fewer FFT bins go as they
  are). The widget treats it as it treats a Flex tile wider than the pan: draws the viewport from
  it at full resolution and keeps the whole row as the supplemental row, which it uses when a pan
  asks for frequencies outside the original viewport.
- Note: the supplemental row is stored at the widget's width (destWidth pixels over the whole
  band), so in deep zoom the part revealed by a drag is coarser until new rows replace it.
- The NB blanker's median and the SW auto-black now see the whole band (both noise-floor
  estimates; the roll-off edges are a small minority of the bins).
- Files: core/backends/hl2/Hl2Backend.cpp, ForkBuild.h.

## 170. New-release link in the status bar (07.11.2026, build 211)

- Operator: on start, check once for a newer release on our GitHub; if there is one, show its name
  at the bottom right; a click opens the release page in the browser.
- MainWindow::wireUpdateChecker (the AetherSDR checker stays off): 8 s after start, one GET of
  api.github.com/repos/Dfinitski/Ater-ugly_fork/releases?per_page=20 (Accept
  application/vnd.github+json, User-Agent "Ater-ugly_fork/<build>", 15 s timeout). Of the
  non-draft releases tagged "build-N" whose page is under
  github.com/Dfinitski/Ater-ugly_fork/releases/, the highest N above AETHER_FORK_BUILD wins.
  Prereleases count too (the list endpoint, not /latest).
- Shown as a permanent status-bar widget (right side): "New release: <release name>" (name trimmed
  to 80 characters, HTML-escaped), link colour #4fc3f7, tooltip; a click opens the URL with
  QDesktopServices, only if it is https://github.com/... Nothing shown on errors (logged at info).
- The releases on GitHub now: build-169, 170, 190. Build 197 was packed but not published.
- Files: gui/MainWindow_Menus.cpp, ForkBuild.h.

## 171. Release build 211 prepared (07.11.2026)

- README.md on GitHub (fork's own README): Protocol 1 stated in the header, "What the fork adds"
  and "Supported transceivers"; a "Protocol 1" section (models, what works, what does not, untested);
  "Build 190" replaced by a "Build 211" summary; Tools -> Open Logs for the logs; FFT Smooth /
  De-comb no longer listed among inherited features.
- docs/release-notes-build-211.md: the GitHub release text for builds 191-211 (197 was never
  published).
- Source pushed to main (src/, CMakeLists.txt, docs): commit e48a43e.
- Archive AetherSDR-ANAN-build211-win64-portable.zip: the build 197 archive with the build 211 exe
  (Desktop\AetherSDR-Release\AetherSDR.exe, 21:15, contains the release-check strings) and
  README-build211.md; top folder AetherSDR-ANAN-build211; 87 entries, 154619558 bytes, SHA-256
  1f3b97ae6d308d30d8398256b95c3ed45e4a4dc466f314a81408f11ac0760f4a. Saved to Desktop\AetherSDR-Release.
- The release itself (tag build-211 on main) is created by the operator in the GitHub web UI; the
  tag push starts the AppImage workflow, which attaches the Linux AppImage.

## 172. Protocol 1 radios that answer only a broadcast discovery (08.11.2026, build 212)

- Two Protocol 1 users (build 211 confirmed later): the radio is listed, connecting fails with
  "no IQ stream". Logs: every attempt "no Discovery reply within 300 ms; assuming ANAN-100D" and
  "no I/Q within 2000 ms; from the radio: 0 packets" -- nothing at all reached the transport
  socket, not even the Protocol 1 Discovery reply (it would log "Protocol 1 Discovery reply"),
  while the discovery sweep (broadcast, its own socket) did find the radio (no "no ANAN answered"
  in the clean log). Routing checked (ipconfig / route print of one user): radio 192.168.1.168,
  PC Ethernet 192.168.1.169/24 on-link, no VPN routes.
- Cause (inferred): the transport starts with a UNICAST discovery (P2 and P1 request) from its own
  socket and waits 300 ms; with no reply it assumed a Protocol 2 ANAN-100D and sent P2 start --
  which a Protocol 1 radio ignores. Protocol 1 firmware that answers discovery only when it is
  broadcast never replies to the unicast request. Thetis never sends a unicast discovery: it
  broadcasts, then sends the start command to the radio, which streams to the sender.
- Fix: the discovery sweep records, per Protocol 1 host, its board (P2 id) and gateware
  (noteProtocol1Host(ip, Protocol1HostInfo), protocol1HostInfo(ip)); P2Transport::onBoardWaitTimeout,
  with no reply on its socket, starts Protocol 1 with that board's profile when the sweep found a
  Protocol 1 radio at the address (log "the discovery sweep found a Protocol 1 ... starting
  Protocol 1"). Only without such a record does it still assume a Protocol 2 ANAN-100D.
- Also seen: users ran an old build believing it was 211 (no Tools -> Open Logs); a Windows
  Firewall block rule (Network Diagnostic -> Windows Firewall -> Fix) on one PC.
- Files: core/backends/anan/P1Protocol.{h,cpp}, P2Transport.cpp, AnanDiscovery.cpp, ForkBuild.h.

## 173. PureSignal over Protocol 1, untested (08.11.2026, build 213)

- Operator: release 212 is too thin; add PureSignal for Protocol 1 blind, users will report.
- Thetis P1 (networkproto1.c, console.cs UpdateDDCs, checked in Thetis-master): twist(stream0,
  stream1) -- stream0 the feedback, stream1 the TX DAC reference (SetPSRxIdx 0, SetPSTxIdx 1):
  nddc 5 (Angelia / Orion / Orion MkII) DDC3 / DDC4; nddc 4 (Hermes) DDC2 / DDC3; nddc 2
  (Hermes II) DDC0 / DDC1, the receivers' DDCs, while MOX + PS (psrx 0, pstx 1). PS frequencies =
  TX frequency. C&C: C0 0x14 C2 bit 6 and C0 0x24 C2 bit 6 = puresignal_run; C0 0x1C C3 = ADC0
  TX step attenuator (the PS feedback attenuator); C0 0x00 C3 bit 7 RX bypass out, bits 5-6 the
  RX-only input (SetAntBits: 0x20 Ext2, 0x40 Ext1, 0x60 XVTR). The ADC map (C0 0x1C C1/C2) is
  not touched by PS in Thetis P1: DDC3 / DDC4 stay ADC0 (the FPGA feeds the DAC side itself).
- Thetis switches the whole P1 rate to ps_rate (192 k) while MOX + PS. Here the session rate
  stays (the receivers do not reconfigure on every over); both PS streams are resampled to
  calcc's 192 ksps by P1PsResampler (x4 / x2 / x1 / /2 for 48 / 96 / 192 / 384, Blackman
  windowed sinc, cut-off 0.45 x the lower rate, unity gain), one instance per stream: identical
  filter and delay, so they cancel in calcc's comparison. Bench test of the converter: unity
  gain and exact frequency at 1 / 15 / -10 kHz from every rate; -1.2 dB at 20 kHz from 48 k
  (the edge, the same on both streams).
- Code:
  - configureSession: m_profile.psSupported = true for every P1 board (no longer forced off,
    m_psEnabled no longer cleared); Hl2Backend publishes ananPureSignal = psSupported || P1.
  - p1State(): pureSignal = m_psEnabled; txAdc0AttDb = PS attenuator while PS is on; Hermes II
    DDC0 / DDC1 on the TX frequency while MOX + PS; feedback source on TX: bypass 0x80, EXT1
    0x40 | 0x80 (ORION MkII: bypass only), as the Protocol 2 Alex bits.
  - The Protocol 2 PS block (peaks, hwPeak, m_psSink, TX scope from feedback) is
    deliverPsPairs(); P2 calls it after decodePsPairs, P1 from handleP1Ep6 with the resampled
    pairs. Log on the first PS block of an over: "PureSignal feedback DDCn, TX reference DDCm,
    R -> 192 ksps".
- What to ask users for: the log of a two-tone with PS on (the PS line above, calcc state,
  feedback level / attenuator), and whether the PS "Feedback" level moves at all. If the
  feedback is silent or the correction diverges, suspect the stream order (swap DDC3/DDC4) or
  the feedback input bits first.
- Files: core/backends/anan/P1Protocol.{h,cpp}, P2Transport.{h,cpp}, hl2/Hl2Backend.cpp,
  ForkBuild.h.

## 174. Own-noise Capture reminder (08.11.2026, build 214)

- Operator: users do not find Setup -> Receive -> Receiver Noise Floor -> Capture, so AUTO ATT
  never starts. Check at start; if not captured, a short window: what it is, why, how.
- MainWindow::maybeShowOwnNoiseHint() (MainWindow_Session.cpp), 3 s after an ANAN connects,
  once per run: if AnanOwnNoiseDbmAdc1 (AnanUiState::ownNoiseKey(0)) is not stored and
  AnanOwnNoiseHintOff is not set, a non-modal box: not captured -> AUTO ATT needs it -> antenna
  off / 50 Ohm, Capture, antenna back. Buttons Open Setup (opens the Receive page) and Later;
  "Don't remind me" sets AnanOwnNoiseHintOff.
- CAT (same day): the user's log had no CAT lines (aether.cat is warning-level by default); the
  only serial line was Serial & Controllers failing on COM5 with Win32 error 5 (held by another
  program) at 21:28:31, before the CAT ports start (on connect). Operator sets up CAT locally to
  look at it.
- Files: gui/MainWindow.{h,cpp}, gui/MainWindow_Session.cpp, ForkBuild.h.

## 175. The operator's look as the default, and a one-time overwrite (08.11.2026, build 215)

- Operator: new users do not know how to set up the panadapter; this version must put its own
  settings in once (over earlier settings) and use them as defaults on a first start: Receive
  Detector Average, FFT AVG 27, FFT Floor 10, everything from the Display panel as his.
- His values, read from his settings database (AetherSDR.db, app_settings and radio_settings
  ClientDisplay of his radio 00:04:A3:00:00:03): fill alpha 0.47, fill #cefff1, line #9dff8c,
  line width 1.5, WF gain 63, auto-black offset 54, floor position 90 (slider 10), background
  opacity 74, fill #0c0b14; per radio: FFT AVG 27 with Log Avg, waterfall rate 65; Receive:
  detector Average, noise line off. Heat map, grid, 2D, 3D gain/span, dss depth 6 were the
  defaults already.
- gui/ForkDisplayDefaults.h holds them. Used by SpectrumWidget::loadSettings() and its member
  defaults (FFT AVG 27 / Log Avg / rate 65 for a radio with nothing stored), the Display
  panel's Reset to Defaults, and:
  - MainWindow constructor, once (ForkDisplayDefaultsV214): removes the Display keys above for
    pan 0 and the _1.._7 suffixes so the new defaults apply; AnanUiState detector Average and
    noise line off.
  - MainWindow_Wiring pan restore, once per "radioId#pan" (ForkDisplayDefaultsV214Radios):
    FFT AVG / Log Avg / waterfall rate set to the defaults and saved, instead of the stored ones.
- Files: gui/ForkDisplayDefaults.h (new), gui/SpectrumWidget.{h,cpp}, gui/MainWindow.cpp,
  gui/MainWindow_Session.cpp, gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 176. Tools -> Log Settings (08.11.2026, build 216)

- Operator testing CAT (COM3, TS-2000): no CAT lines in the log; Runtime Monitor -> Logs has no
  category switches (it only filters the view). The switches are in SupportDialog ("Diagnostic
  Logging"), reachable only from Help -> Support & Diagnostics, and the Help menu went in build
  198. My instruction (Runtime Monitor) was wrong.
- Tools -> Log Settings... opens SupportDialog again (next to Open Logs). "TCI / CAT / rigctld"
  there turns on aether.cat info + debug: "CatPort: serial COMn open" and one "SmartCAT <cmd> ->
  <answer>" line per logger command.
- Files: gui/MainWindow_Menus.cpp, ForkBuild.h.

## 177. Floating applets open at their own height, scroll areas included (08.11.2026, build 217)

- Operator: the floating CAT applet shrank to a minimum height and cannot be made taller; applets
  must always open at their own size. Saved ContainerGeometry_CAT: 399 x 127 px.
- Cause: FloatingContainerWindow::fitHeightToContent() (build ~30.10: fixed height = content,
  no vertical mouse resize) takes the layout's sizeHint. The CAT port table is in a QScrollArea,
  whose sizeHint is not its content's, and rows built later inside the scroll area raise no
  LayoutRequest up to the window -- so the window stayed at the docked-page size and the
  table was cut, with no way to enlarge it.
- Fix: fitHeightToContent adds, for each visible widgetResizable QScrollArea in the container,
  what its content needs beyond the scroll area's own hint; the height is capped at 90 % of the
  screen. restoreAndEnsureVisible refits once more on the next event-loop pass, and
  CatControlApplet::buildTableRows posts a LayoutRequest to its window. Also covers the KAT500,
  KPA500 and KXPA100 applets (scroll areas too).
- Files: gui/containers/FloatingContainerWindow.cpp, gui/CatControlApplet.cpp, ForkBuild.h.

## 178. CAT serial: DTR/RTS on, and what the logger sends is logged (08.11.2026, build 218)

- Operator's CAT test (log 20261108-150241, aether.cat on): "CatPort: serial COM3 open, 9600
  baud 8N1, dialect 1" at 15:02:43, then not one SmartCAT line in 7 minutes -- no byte arrived
  from the logger. The Clients column stays empty for a serial port by design (it counted
  only TCP clients).
- Not an answer problem then: the logger sends nothing to COM3. Either it is not on the other
  end of the pair, or it waits for the handshake lines: QSerialPort leaves DTR / RTS as they are,
  and over com0com / VSPE they are the logger's DSR / CTS.
- Build 218 (core/CatPort.{h,cpp}): after open DTR and RTS on, as a real radio's port; the open
  line (warning level, in every log) names the port's description / manufacturer (shows a
  com0com / VSPE port); the first bytes from the logger are logged; 15 s without a byte gives
  "nothing received from the logger -- is the logger on the OTHER end of the virtual pair";
  a talking serial logger counts as one client in the Clients column.
- Files: core/CatPort.{h,cpp}, ForkBuild.h.

## 179. CAT works; floating applets also open at their own width (08.11.2026, build 219)

- Operator: CAT now works (the logger had reported that the other end did not accept the
  connection -- its handshake lines; DTR/RTS of build 218, or handshake off in the logger).
- The CAT window opened at the full height but not the full width; applets are small, so they
  must open at their whole own size.
- fitHeightToContent() also takes the width: the layout's hint plus what each scroll area's
  content needs beyond the scroll area's hint, capped at 90 % of the screen; that is the
  minimum width, and a narrower window (saved geometry) is widened to it. Wider by mouse stays
  possible; the height stays fixed.
- Files: gui/containers/FloatingContainerWindow.cpp, ForkBuild.h.

## 180. Applets open at their width but stay resizable; CAT on Linux (08.11.2026, build 220)

- Operator: the width must be free to make wider or narrower; only the opening is at the
  content's size.
  - fitHeightToContent(): no minimum width any more. The content width is applied only while
    the window opens (m_fitWidth, set by restoreAndEnsureVisible, cleared 300 ms later after
    the late layout passes); the height stays fixed to the content.
- Operator: check that CAT works the same on Linux.
  - Found: a "PTY only" row (no TCP port, no serial device) never started on Linux --
    MainWindow::applyCatPortCount ran a row only with a TCP port or a serial device (build 191
    condition, made for Windows), and CatPort::start(0) refused without a serial name. Off
    Windows an enabled row now runs on its PTY alone.
  - The PTY now logs the logger's first bytes (warning level) and a talking logger counts as one
    client, as the serial port of build 218. A real tty (/dev/ttyUSB0) goes through the same
    QSerialPort code as Windows (DTR/RTS on). No "nothing received" warning on the PTY: every
    running row has one, with or without a logger.
  - Still different: over the PTY auto-information (AI1) is not pushed (SmartCatProtocol
    without a session); Hamlib polls, so it does not need it.
- Files: gui/containers/FloatingContainerWindow.{h,cpp}, core/CatPort.{h,cpp}, gui/MainWindow.cpp,
  ForkBuild.h.

## 181. Release build 220 prepared (08.11.2026)

- docs/release-notes-build-220.md: short, as the operator asked -- PureSignal over Protocol 1,
  CAT (DTR/RTS, Clients, Linux PTY-only rows), Tools -> Log Settings, the own-noise reminder;
  one line each for the one-time display defaults, applet window size and the Protocol 1
  broadcast-discovery fix.
- README.md: PureSignal column for Hermes / Hermes II "Protocol 1 only", the Protocol 1 section
  lists PureSignal, "Build 211" summary replaced by "Build 220".
- Source of builds 212-220 pushed to main; archive: the build 211 archive with the build 220 exe
  (Desktop\AetherSDR-Release\AetherSDR.exe built 15:26) and README-build220.md; 87 entries,
  153009474 bytes, SHA-256 14ffd874a83151ffa398be246484e9278078b7e9d1da2568175e68fe8a77a87e; saved to
  Desktop\AetherSDR-Release. Commit 14d2bff on main.

## 182. Reliable routing to the radio; Connect by IP for Protocol 1 (09.11.2026, build 221)

- User log 20261109-221337 (.168, fresh install, ANAN-100D Protocol 1, gateware 59): the sweep
  finds the radio, the transport's unicast Discovery gets no reply, the Protocol 1 fallback
  starts, 0 packets in 2 s -- every 3 s. Thetis works on the same PC: no kill-switch VPN. The
  difference: Thetis binds its socket to the adapter the radio was found on; we sent from the
  wildcard socket (routing table). The user says the radio has a static IP.
- Operator: make the routing reliable for any network (radio on a Wi-Fi bridge, PC on a switch,
  ...), plus manual settings where the automatics cannot tell.
- What exists upstream: Connection panel -> Connect by IP, radio type ANAN, Advanced: the
  network adapter (all adapters, RadioBindSettings). Found: its ANAN probe sent only the
  Protocol 2 request, so a Protocol 1 radio could not be connected by IP.
- Build 221:
  - core/backends/anan/AnanRoute.h (new, header-only): localRouteFor(datagram) -- the local
    address a reply came in on (destination address, else the receiving interface, else the
    adapter whose subnet holds the radio), adapter name and prefix; sameSubnet().
  - P1Protocol: noteHostRoute / hostRoute (radio -> local address), thread-safe.
  - AnanDiscovery records it and sets RadioInfo::sessionBindAddress (-> Params::localAddress);
    logs once per radio "answered on adapter X (a.b.c.d/n)", a WARNING when the radio is not in
    that adapter's subnet. P2Transport::discover records it too.
  - P2Transport::start: the session socket binds to the wildcard first (as before), and after a
    start with nothing back to the adapter the radio was found on, alternating; the bind that
    gets I/Q is kept for the radio. An adapter chosen in Connect by IP -> Advanced is tried
    first. Logged: "session socket on <address> (adapter ...) to <radio>".
  - A start with nothing back appends the reason to the connection error: the radio not in the
    adapter's subnet (put both in one subnet), or "answers the broadcast, nothing came back:
    VPN, firewall rule, another program", naming the adapter of the next attempt.
  - ConnectionPanel::probeAnan sends the Protocol 1 request too and maps a Protocol 1 reply
    (noteProtocol1Host, route); the type is "ANAN (openHPSDR)", the hint mentions the adapter.
- Not possible from the program: reaching a radio whose static address is outside every
  subnet of the PC through a router that does not route to it -- the message says so.
- Files: core/backends/anan/AnanRoute.h (new), P1Protocol.{h,cpp}, AnanDiscovery.{h,cpp},
  P2Transport.{h,cpp}, gui/ConnectionPanel.cpp, ForkBuild.h.

## 183. TCI I/Q for the ANAN: CW Skimmer / SDC get the receiver's I/Q (09.11.2026, build 222)

- A user: TCI control works, the I/Q does not come (a CW skimmer).
- Cause: TCI I/Q (iq_start) is built on the Flex DAX IQ plane -- TciServer::ensureIqStream
  refuses unless RadioCapabilities::hasDaxStreams, and the ANAN backend (Hl2Backend) has none.
  The skimmer was answered "iq_stop" every time.
- Build 222:
  - Hl2Backend: an I/Q tap per receiver on the I/O thread (next to processIqBlock): the DDC
    samples mixed down by the view offset (TCI's dds: is the pan centre = NCO + offset), then
    converted to the TCI rate (24/48/96/192 k) by P1PsResampler, now any power-of-two ratio
    either way (configure(in, out); the PS 2:1 case unchanged), in frames of 1024 I/Q pairs,
    emitted as tciIqReady(channel, float32 LE I/Q, rate). Started / stopped with
    invokeExtension("hl2", "tci.iq", {slice, channel, rate, on}); logged "ANAN: TCI I/Q on ...".
  - TciServer: nativeIq() = no DAX plane and the backend declares "hl2"; ensureIqStream then
    maps trx -> channel trx+1, asks the backend for the tap and connects tciIqReady to
    onIqDataReady (the same frame builder as the Flex path); iq_samplerate re-arms the tap at the
    new rate; releaseIqChannel stops it. effectiveIqSampleRate is the requested rate as is.
  - Bench test of the converter: 384->48, 1536->48, 192->96, 48->192, 96->48, 48->24 k: exact
    sample counts, unity gain, exact frequency in band, out-of-band rejected.
- Untested with a skimmer. In the log: "TCI: IQ started for client ..." and "ANAN: TCI I/Q on for
  receiver N channel N at R Hz, DDC D Hz".
- Files: core/backends/anan/P1Protocol.{h,cpp}, core/backends/hl2/Hl2Backend.{h,cpp},
  core/TciServer.{h,cpp}, ForkBuild.h.

## 184. Transmit only inside the amateur bands of the band plan (09.11.2026, build 223)

- Operator: is transmit restricted? It must be allowed on the amateur bands only, taken from the
  band plan. Found: no restriction at all for the ANAN -- applyKeying gated only on the
  automation bridge (m_txAllowed).
- MainWindow::pushTxBandsToBackend(): the active band plan's segments (Display -> band plan,
  BandPlanManager) merged into bands -- segments closer than 25 kHz join, so the small
  unassigned gaps inside a band (IARU R1 29.51-29.52 MHz ...) do not block it -- sent with
  invokeExtension("hl2", "tx.bands", {bands, plan}) on connect and on every plan change.
  Logged by the backend: "ANAN: transmit allowed in N amateur band(s) of the band plan ...".
- Hl2Backend: txInAmateurBand(hz) -- the whole signal must be inside one band: the RF span by
  the TX mode (txSpanHz: USB/DIGU f+lo..f+hi, LSB/DIGL f-hi..f-lo, AM/SAM/DSB f+-hi, FM f+-(hi
  + 5 kHz deviation), CW the carrier) from effectiveTxPassband. Until the plan arrives, a
  built-in table of every IARU region's amateur allocations (2200 m .. 6 m).
  - applyKeying: a key-down outside the bands is refused (like the automation gate), logged,
    and the operator gets a configurationWarning "Transmit refused: X MHz is outside the
    amateur bands of the band plan ..." (at most every 3 s).
  - setTxFrequency: while keyed, a move outside the bands is not followed -- the carrier stays
    in the band.
- Not covered: the radio's own CW keyer (keys in the FPGA), and no operator override (MARS,
  out-of-band tests) yet.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, gui/MainWindow.{h,cpp}, ForkBuild.h.

## 185. The radio's CW keyer off outside the amateur bands (09.11.2026, build 224)

- Operator: block the radio's internal keyer too -- clear the CW enable bit in the protocol
  outside the amateur bands; receive is not affected.
- The bits: Protocol 2 Transmitter Specific byte 5 bit 1 (cwModeControlByte: the whole byte is
  0 when the keyer is off, Thetis CmdTx), Protocol 1 C0 0x1E C1 bit 0 (p1Cc bank 13).
- P2Transport::setCwBandInhibit(bool): effectiveCwKeyer() = the keyer options with enabled
  false while inhibited; used for the Transmitter Specific packet (sent at once on a change),
  the Protocol 1 C&C (p1State) and the host's paddle-follow (radio PTT from the paddles).
  Logged "the radio's CW keyer disabled / enabled again".
- Hl2Backend::updateCwBandInhibit(): the TX carrier (dial + XIT, as written to the TX register)
  checked against the bands (hzInAmateurBand), on every setTxFrequency and on a band-plan
  update.
- Fixed in passing (build 223): a TX frequency held during an over (outside the bands) left the
  register on the old in-band frequency after the unkey, and the key-down check looked at that
  register -- the next over could go out on the old frequency while the dial was elsewhere. Now
  the check uses the frequency asked for (m_txWantedHz) and the held frequency is written at the
  unkey.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, core/backends/anan/P2Transport.{h,cpp},
  ForkBuild.h.

## 186. Finer panadapter frequency resolution (09.11.2026, build 225)

- A user: the panadapter's frequency resolution is not enough (Thetis looks the same).
- What limited it:
  - the trace was always 1024 points across the view (kOutBins), so at 192 kHz unzoomed one
    point was ~190 Hz, and the widget stretched 1024 points over 1500-2500 pixels;
  - the FFT aimed at ~1024 bins in the view, capped at 16384 points and a 0.1 s window -- the
    cap was there because frames did not overlap: a window of N samples gave rate / N frames
    per second;
  - when a frame had more points than pixel columns, the shader sampled one per pixel and
    skipped the rest (a narrow carrier could vanish or flicker).
- Build 225:
  - Hl2Spectrum::setSlidingWindow (on in Hl2RxDsp): after a frame the window keeps its newest
    fftSize - 1 samples and slides, so the next due frame (25 fps cap) needs one new sample --
    a long window costs time smear, not frame rate. Off by default (the fixtures).
  - applyZoomFftSize: ~4096 bins in the view, up to 65536 points and a 0.35 s window (48 k: 4096
    points, 11.7 Hz bins; 192 k: 16384, 11.7 Hz; zoomed to 10 kHz at 48 k: 16384, 2.9 Hz).
  - kOutBins 4096.
  - SpectrumWidget::buildFftDisplayTrace: more points than pixel columns -> one per column, the
    mean POWER of the column's points (the noise stays on the noise line, a carrier stays one
    column wide). Applies to every backend.
- Cost: FFT up to 65536 at 25 fps and up to 65536 dB->power conversions per frame on the GUI
  thread for the pixel/waterfall reduction; to watch on slow PCs.
- Files: core/backends/hl2/Hl2Spectrum.{h,cpp}, Hl2RxDsp.cpp, Hl2Backend.cpp,
  gui/SpectrumWidget.cpp, ForkBuild.h.

## 187. Full zoom stops at its limit (09.11.2026, build 226)

- Operator: at the maximum zoom the panadapter keeps trying to zoom further and jerks; it
  must stop at the limit.
- Cause: the backend reported a zoom floor of 1.5 kHz (panBandwidthLimitsChanged) while
  setPanBandwidth clamps to max(1.5 kHz, rate / 64) -- 3 kHz at 192 k, 6 kHz at 384 k. The
  widget zoomed below, the backend pulled it back, every drag step. And at a limit the widget
  still reprojected the spectrum (clearing it when that failed), reset the noise-floor baseline
  and sent a request for the same range on every mouse move.
- Build 226: Hl2Backend::ananMinViewHz() is the one floor, used by setPanBandwidth and reported
  as the pan's lower limit (at connect, in pushInitialState and after a rate change, "anan.rate").
  SpectrumWidget's bandwidth drag returns at once when the clamped bandwidth and centre are
  the ones already shown.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, gui/SpectrumWidget.cpp, ForkBuild.h.

## 188. Band memory for the ANAN (09.11.2026, build 227)

- Users: the band buttons do not remember the frequency. Operator: check that a band change by
  the buttons, or a jump to a frequency outside the band, keeps the last frequency, the
  panadapter width, the mode and the TX passband etc.
- Found: nothing is kept. MainWindow::selectBand for a radio without a band stack (the ANAN)
  tuned to the button's fixed frequency and mode (BAND_GRID); BandSettings::saveBandState /
  loadBandState were never called and its file persistence is a stub ("pending redesign").
- Build 227 (gui/MainWindow_Wiring.cpp, MainWindow.cpp):
  - ananBandMemTick, every 0.5 s while connected to a radio without the Flex command plane:
    the active slice's state (frequency, mode, RX filter, AGC mode / threshold, its pan's
    width, the TX passband) is kept for the band it is on (BandSettings::bandForFrequency,
    "GEN" outside the bands). When the slice leaves the band -- button, typed frequency, spot
    -- the band's last state is stored in AppSettings "AnanBandMemory" (JSON per band). Also
    stored on a band button press and when the window closes.
  - selectBand: a visited band comes back as it was left -- mode, frequency, pan centred on it,
    RX filter, AGC, pan width, and the TX passband in USB/LSB. A band never visited still gets
    the button's frequency and mode. Logged "band X restored: ...".
- The LNA / drive per band were already kept by the backend (rememberCurrentBandState).
- Files: gui/MainWindow.{h,cpp}, gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 189. Band memory as Thetis keeps it (09.11.2026, build 228)

- Compared with Thetis 2.10 (console.cs: SetBand / BandStackEntry {mode, filter, frequency,
  CTUN, zoom, centre}; the *_by_band arrays: rx1/rx2 AGC mode and threshold, rx1/rx2 preamp,
  rx1/rx2/tx step attenuator, power, tune power; Alex antennas per band in Setup). Ours (227)
  lacked: the view's position (CTUN centre), antennas per band, RX2's attenuator, a memory of
  its own for RX2. The multi-entry band stack (BandStack2) the operator does not want.
- Build 228 (gui/MainWindow_Wiring.cpp):
  - one memory per receiver: key "40m" for RX1, "RX2/40m" for RX2; every slice sampled every
    0.5 s (ananBandMemTick), ananBandMemFlush on a band button and at close;
  - stored in addition: the pan centre ("center"); for RX1 the Alex RX / TX antennas
    (AnanRxAnt / AnanTxAnt) and the LNA (AnanLna); for RX2 its pan RF gain (= -attenuator);
  - ananBandMemApplyHardware: on ENTERING a band by any means (button, typed frequency, spot)
    the band's antennas / LNA (RX1) or attenuator (RX2) are applied, as Thetis does on a band
    change; a band never visited keeps the current ones; the first sample after connect only
    learns;
  - a band button restores the view centre when the frequency is inside the restored view
    (45 % of the width), else centres on the frequency.
- RX1 attenuator, drive and tune power per band were kept by the backend already.
- Files: gui/MainWindow.{h,cpp}, gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 190. FFT Smooth back on the Display panel (09.11.2026, build 229)

- Operator: bring back the slider that coarsens the panadapter bins (removed in build 199);
  default 0; saved per pan like the panel's other settings.
- SpectrumOverlayMenu: the "FFT Smooth" row (0 .. kFftSmoothingMaxHalfWidthPx, the half-width
  in pixels, power averaging; 0 = Off) after FFT AVG. The wiring (fftSmoothingChanged ->
  SpectrumWidget::setFftSmoothing, the menu sync, Reset to Defaults -> 0, pan cloning) had
  stayed in place.
- SpectrumWidget::loadFftTraceShaping reads smoothPx again from the per-pan
  DisplayFftTraceShaping object, only from version 2 (written from now on); a value saved by a
  build before 199 is ignored, so everyone starts at 0. De-comb stays off.
- Files: gui/SpectrumOverlayMenu.cpp, gui/SpectrumWidget.cpp, ForkBuild.h.

## 191. The fine panadapter from the first frame (09.11.2026, build 230)

- Operator: at start the old coarse panadapter shows for a few seconds, then the fine one.
- Cause: not the saved settings. The FFT size of build 225 was applied only by
  applyZoomFftSize -- after a zoom gesture (400 ms timer) or a rate button. Every receiver chain
  was BUILT with Hl2RxDsp::Config's default fftSize 1024: at connect, when a receiver is
  added, and on every DDC rate rebuild. So the trace stayed at 1024 points (47 Hz at 48 k,
  187 Hz at 192 k) until the zoom restore at start fired the timer -- or, after a rate
  change, until the next zoom.
- Build 230: Hl2Backend::spectrumFftSizeFor(zoomViewHz, rateHz) (static, the build 225 rule)
  sets Config.fftSize at all four construction sites (connect template and per receiver,
  receiver add, rate rebuild) and is what applyZoomFftSize applies.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, ForkBuild.h.

## 192. The power meter moves smoothly (09.11.2026, build 231)

- Users: the power meter jumps; operator: fix it, the receive level meter moves smoothly.
- Cause: P2Transport::publishTelemetry ran on the 100 ms keepalive and took the LAST coupler
  reading (m_fwdRaw) -- one sample of hundreds a second (P2 high-priority status, P1 EP6
  status); in SSB a random point of the speech envelope, 10 times a second. Hl2Backend
  emitted it as is and MeterModel shows FWDPWR unsmoothed (operator, 31.10).
- Build 231:
  - P2Transport: m_fwdPeakWin / m_revPeakWin, the highest readings since the last publish
    (both protocols); published as forward / reverse (and the peak) and reset. While MOX a
    40 ms timer (m_meterTimer, started / stopped with the keepalive) publishes -- 25 a second;
    the keepalive publishes only when not transmitting.
  - Hl2Backend::publishTelemetry: the gauge value m_fwdMeterW rises at once and falls with a
    0.2 s time constant (dt = the window length) -- S-meter-like movement, not the slow hold
    removed on 31.10. Reset at key-down and on disconnect.
- SWR comes from the same window peaks.
- Files: core/backends/anan/P2Transport.{h,cpp}, core/backends/hl2/Hl2Backend.{h,cpp},
  ForkBuild.h.

## 193. Power meter: 250 ms peak detector, gliding gauge (09.11.2026, build 232)

- Operator (on learning that FWDPWR had no smoothing since 31.10): a 250 ms peak detector,
  and the gauge moving smoothly between the readings.
- Hl2Backend::publishTelemetry (keyed): the 40 ms window peaks of build 231 go into
  m_fwdPeakHist (time, W); the target is the highest of the last 250 ms; the gauge
  m_fwdMeterW approaches it with an 80 ms time constant in both directions (replaces the
  instant-rise / 0.2 s release of build 231). Cleared at key-down, unkeyed and on disconnect.
- Files: core/backends/hl2/Hl2Backend.{h,cpp}, ForkBuild.h.

## 194. ANF button on the VFO DSP tab (09.11.2026, build 233)

- Operator: an ANF button in the VFO's DSP row, before NB.
- The button already existed (Flex radio-side ANF) but was hidden on ANAN: no radio-side
  DSP, and the backend refused Feature::Anf. Now WDSP's own ANF (anf.c, LMS predictor) runs
  on this host, the same way as the APF:
  - WdspChannel::setAnf(enabled, gain): SetRXAANFVals(64, 16, gain, 0.1) + SetRXAANFRun;
    held in Config (anfEnabled, anfGain) and re-applied in open(). Taps/delay/leakage are
    RXA.c's defaults.
  - Hl2RxDsp::setAnf(on, level): level 0..100 -> gain 1e-5 * 100^(level/100) (50 = RXA.c's
    1e-4), runs only in LSB/USB/DSB/AM/SAM (the modes where the button is offered),
    re-applied on mode change and on channel install.
  - Hl2Backend: Receiver anfOn/anfLevel, requestSliceDsp(Feature::Anf) -> setSliceAnf,
    pushAnf on receiver open / reconnect, slice echo (d.anf, d.anfLevel), receiver memory
    keys anfOn/anfLevel.
  - RadioCapabilities::hasHostAutoNotch (Hl2Backend sets it); VfoWidget shows ANF on it,
    and relayoutDspGrid orders NR, ANF, NB, ...
- The DSP level slider follows ANF like on Flex (LvlAnf). Untested on air.
- Files: core/dsp/WdspChannel.{h,cpp}, core/backends/hl2/Hl2RxDsp.{h,cpp},
  core/backends/hl2/Hl2Backend.{h,cpp}, core/backends/RadioCapabilities.h,
  gui/VfoWidget.cpp, ForkBuild.h.

## 195. Wheel tunes the digit under the cursor; User Out follows the tuned band (09.11.2026, build 234)

- Operator: over the VFO frequency the wheel should tune the digit under the cursor, not by
  the slice step. And the User Out matrices must switch whenever the receiver is tuned into
  a band, by any method.
- VfoWidget::wheelEvent / freqDigitPlaceAt: over the expanded frequency label the step is
  10^place of the digit under the cursor (right-aligned text measured with the label's
  font; over a separator -> the digit after it; left of the number -> its leading digit).
  100 kHz and up go out as directEntryCommitted ("vfo-digit-wheel", the pan re-centres),
  smaller as stepTuneRequested. Collapsed widget: unchanged (slice step).
- User Out cause: P2Transport chose the RX row from m_rxHz, the DDC NCO = the panadapter
  centre, not the slice. A click-tune inside a wide pan (or a pan centred past a band
  edge) left the outputs on the band of the pan centre, or off.
  - P2Transport::setRxTunedHz(rx, hz) / ocRxHz(): the RX row (P2 byte 1401 and the P1
    C&C open-collector bits) uses the tuned frequency, the NCO only while none is known; a
    band change sends the High Priority packet.
  - Hl2Backend::pushAnanTunedFrequencies(): each receiver's sliceFreqHz, on change, from
    applyBandFilter() (every tune, pan, add/close receiver, key/unkey) and from the 300 ms
    ANAN timer as a net. TX row unchanged (transmit frequency).
- Untested on a radio.
- Files: gui/VfoWidget.{h,cpp}, core/backends/anan/P2Transport.{h,cpp},
  core/backends/hl2/Hl2Backend.{h,cpp}, ForkBuild.h.

## 196. Digit wheel: the panadapter moves, the filter stays (09.11.2026, build 235)

- Operator: with the per-digit wheel of build 234 the filter moved on the pan and the flag
  with it, so the digit ran away from the cursor. The pan should move, the filter stay.
- VfoWidget: a digit step emits digitTuneRequested(mhz) (all places; build 234's
  directEntryCommitted / stepTuneRequested split is gone).
- MainWindow_Wiring: the pan centre is moved by the same delta first
  (applyTuneCenteringWrite), then the slice is set (pushSliceFrequencyToOverlays +
  setFrequency) without the reveal/follow step. Centre lock or a confirmed-control backend:
  the ordinary IncrementalTune.
- Files: gui/VfoWidget.{h,cpp}, gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 197. Release build 235 prepared (09.11.2026)

- docs/release-notes-build-235.md: builds 221-235 by topic (connection, transmit band gate and
  CW keyer, power meter, panadapter, band memory, digit wheel, User Out, ANF, TCI I/Q), with
  what is untested on a radio. README.md: "Build 220" summary replaced by "Build 235".
- Source of builds 221-235 pushed to main: commit 49c7335.
- Archive AetherSDR-ANAN-build235-win64-portable.zip: the build 220 archive with the build 235
  exe (build\Desktop_Qt_6_12_0_MSVC2022_64bit_Release\AetherSDR.exe, 20:42, contains
  digitTuneRequested) and README-build235.md; top folder AetherSDR-ANAN-build235; 87 entries,
  154867207 bytes, SHA-256 206d7ef4da8f1f9a094088cdc3310c3e89ffa808d67b89f13753c55b912e1d4e.
  Saved to Desktop\AetherSDR-Release. Desktop\AetherSDR-Release\AetherSDR.exe is still the
  20:30 (build 234) exe.

## 198. DRM, stage 1: the decoder evaluated, the DRM applet (09.11.2026, build 236)

- Operator: a DRM receiver, slowly; a new DRM widget with its settings, large (pictures)
  when popped out; the codec question is his to settle later.
- Decoder: Dream (github.com/Drm-tools/dream, GPL v2 "or any later version" -> fits GPL v3,
  active again since 12.2025). Its receiver core (115 files, about 63k lines) builds without
  Qt (-DNO_QT) against FFTW3 and zlib; Journaline's three C files must be compiled as C.
  Two small additions to CDRMReceiver (kept for the integration): SetExternalInput /
  SetExternalOutput -- with no sound card driver compiled in, LoadSettings binds the input to
  an RSCI source and the receiver never reads the host's samples.
- Tested in the container with KiwiSDR's real recordings (DRM.BBC.Journaline.au,
  DRM.KTWR.slideshow.au: 12 kHz I/Q, resampled to 48 kHz, input CS_IQ_POS_ZERO):
  - BBC WS (mode C, xHE-AAC + Journaline): sync, SNR ~20 dB, FAC 97 %, SDC 94 %, labels
    "BBC WS" / "BBC WS JL".
  - KTWR (mode B, AAC + MOT slideshow): SNR ~27 dB, FAC 99 %, SDC 98 %, "KTWR  Japanese" /
    "KTWR Slides".
  - Audio: FAAD2 2.11 (libfaad_drm, loaded at run time) rejects every AAC frame (errors 26/32);
    FDK-AAC (built from source for the test only) decodes AAC 551/562 frames and the xHE-AAC
    of the BBC recording. The codec stays open (FDK-AAC licence vs GPL; FFmpeg's xHE-AAC).
  - Dream's own transmitter -> receiver loopback gets FAC but no SDC/MSC: a transmitter-side
    issue, not used.
- Build 236 (UI only, no decoder yet):
  - core/DrmStatus.h: what a decoder reports (stage lights IO/Time/Freq/FAC/SDC/MSC, SNR,
    robustness, bandwidth, QAM, interleaver, up to 4 services with label/codec/bitrate/
    language, text message, time, slideshow image, Journaline text).
  - gui/DrmApplet: docked -- DRM On/Off, slice, the six lights, SNR and mode line, station
    label, service buttons 1..4 (audio / data), service details, text message, a hint line;
    popped out (dockModeChanged, like CAT) -- also the slideshow picture, Journaline, channel
    details and the receiver settings (frequency / time interpolation, MSC iterations, flip
    spectrum; AppSettings DrmFreqInt / DrmTimeInt / DrmMlcIter / DrmFlipSpectrum). DRM On is
    never restored at start. Without a decoder the hint says so.
  - AppletPanel: applet "DRM" ("DRM Receiver"), category Audio & DSP, in the default order
    after IQ and in the button bar list.
- Next: Dream's core into third_party/dream (CMake library, MSVC), fed from the I/Q tap at
  48 kHz, audio into the slice's audio, the DrmStatus filled from CParameter; then the codec.
- Files: core/DrmStatus.h (new), gui/DrmApplet.{h,cpp} (new), gui/AppletPanel.{h,cpp},
  CMakeLists.txt, ForkBuild.h.

## 199. DRM, stage 2: Dream inside, fed from the slice's DRM filter (10.11.2026, build 237)

- Operator: keep FDK-AAC as a DLL next to the binary (drop it later if need be); do not feed
  raw 48 kHz I/Q -- the station is selected by a filter on the panadapter, tuned by hand.
- Signal path: the slice is put in DRM mode. WDSP runs no demodulator for DRM (RXA.c: no
  amd/fmd), its band-pass is the DRM filter (+-5 kHz default, Hl2Backend line ~257, on the
  panadapter) and the panel keeps I and Q (create_panel: copy 0), so the channel output
  (L = I, R = Q, 24 kHz -- AudioEngine's native RX rate) is the filtered zero-IF I/Q of the
  station. Hl2RxDsp::applyDrm() pushes it to the decoder and puts the decoded audio in its
  place (after ~0.8 s buffered; re-buffers after a gap), before the slice mute, so every
  consumer (speaker mix, radio codec, DAX/TCI) gets the DRM audio.
- third_party/dream: Dream's receiver core (commit 7b38ce5, 99 sources + headers), CMake
  target aether_dream, built without Qt. Interface aether/DrmEngine.h (Qt-free: Dream's
  headers change meaning with QT_CORE_LIB). DrmEngine: own thread, pushIq / pullAudio at
  24 or 48 kHz (24 kHz: Dream upsamples x2, "sigupratio"), slow input gain (2 s, -21 dBFS
  RMS in 16 bits), status snapshot every 200 ms, slideshow pictures, Journaline pages,
  service selection, settings. First data service decoded automatically.
- FDK-AAC: aether/fdk_dyn.cpp defines the 11 FDK functions Dream calls and forwards them
  to fdk-aac.dll / libfdk-aac-2.dll (libfdk-aac.so.2, libfdk-aac.2.dylib) loaded on first
  use; absent -> no capabilities, no audio, the rest works. Headers for types only in
  third_party/fdk-aac (NOTICE = FDK licence). CMake copies third_party/fdk-aac/bin/fdk-aac.dll
  next to the exe when present.
- Dream fixes (marked AetherSDR, listed in README.aether.md): external input/output;
  aacinfo() zeroed / DecGetVersion() null title; portable Pacer; without Qt, MOT objects
  were WRITTEN AS FILES INTO THE WORKING DIRECTORY and never handed out -- now queued
  (bounded); EPG file output off.
- Tests (container, Linux, the CMake target, KiwiSDR recordings):
  - without FDK: KTWR sync, labels, language, country, Japanese text messages, UTC time;
    audio silent, as designed.
  - with FDK (libfdk-aac.so built from github.com/mstorsjo/fdk-aac, test only): all six
    lights green; BBC WS (mode D -- not C as written in 198 -- 64-QAM, xHE-AAC) audio +
    Journaline pages; KTWR (mode B, 16-QAM, HE-AAC) audio + 4 slideshow pictures
    (18.PNG, 19-21.JPG); the same at 24 kHz in and out.
- Backend: extension verb "drm" (slice, action start/stop/service/settings/journaline),
  Receiver::drm (follows the receiver into a rebuilt chain), signals drmStatusReady
  (QVariantMap, 4/s) and drmSlideReady (picture bytes).
- GUI: MainWindow_Drm.cpp. DRM On: the slice chosen in the applet goes to DRM mode
  (previous mode remembered), decoder starts; Off restores the mode. Leaving DRM mode, closing
  the slice or disconnecting turns it off. Status -> DrmApplet; pictures decoded with QImage;
  Journaline: double-click a "[n]" line, "Menu" back to the main page. Hints: no FDK library,
  no signal, "centre the DRM filter on the station".
- Not tested: the MSVC build of Dream (no Windows compiler here), the whole chain on air.
- Files: third_party/dream/** (new), third_party/fdk-aac/** (new), src/gui/MainWindow_Drm.cpp
  (new), CMakeLists.txt, core/backends/hl2/Hl2Backend.{h,cpp}, Hl2RxDsp.{h,cpp},
  gui/MainWindow.{h,cpp}, MainWindow_Session.cpp, gui/DrmApplet.{h,cpp}, ForkBuild.h.

## 200. FDK-AAC: DLL in the Windows release only; built into the Linux AppImage (10.11.2026, build 238)

- Operator: Windows -- fdk-aac.dll goes next to the exe in the release archive only (not in
  git); Linux must be complete out of the box.
- .github/workflows/appimage.yml (repo, ater-ugly_fork): new step "Build FDK-AAC": upstream
  mstorsjo/fdk-aac tag v3.0.0 (commit a68a51a checked), shared, target fdk-aac only (the
  decoder library; fdk-xhe-aac is a separate xHE-AAC ENCODER, not needed), libfdk-aac.so*
  -> AppDir/usr/lib, NOTICE -> usr/lib/libfdk-aac.NOTICE. continue-on-error like FreeDV.
  The same commands were run in the container: SONAME libfdk-aac.so.2.
- fdk_dyn.cpp (Linux): dlopen() of a bare name never looks next to the program, so the
  executable's ../lib/ and own directory (/proc/self/exe) are tried first, then the system
  path. Tested with the AppImage layout (usr/bin/test + usr/lib/libfdk-aac.so.2, no
  LD_LIBRARY_PATH): found; without it: not found, DRM runs silent as designed.
- MainWindow_Drm.cpp: the "no AAC decoder" hint per OS (Windows: fdk-aac.dll next to
  AetherSDR.exe; Linux: libfdk-aac.so.2 / package libfdk-aac2; macOS: brew install fdk-aac).
- Files: third_party/dream/aether/fdk_dyn.cpp, src/gui/MainWindow_Drm.cpp, ForkBuild.h;
  .github/workflows/appimage.yml in the repo (goes out with the next release push).

## 201. MSVC: Dream's sound factory without the Windows MME driver (10.11.2026, build 239)

- Operator's first Windows build: soundfactory.cpp(33): C1083 '../windows/Sound.h' -- under
  _WIN32 Dream's sound factory includes its MME driver from src/windows, which is not
  vendored (and not wanted: the host feeds the receiver).
- third_party/dream/src/sound/soundfactory.cpp: the three _WIN32 blocks (include, input and
  output driver) are now `defined(_WIN32) && !defined(AETHER_DREAM)`; CMake defines
  AETHER_DREAM for aether_dream. README.aether.md lists the change.
- Checked the other reachable includes: every remaining unresolved one is behind USE_*,
  HAVE_SPEEX or !NO_QT. clang -Werror=vla-extension/gnu-designator/zero-length-array over
  all sources: clean.
- Files: third_party/dream/src/sound/soundfactory.cpp, third_party/dream/CMakeLists.txt,
  third_party/dream/README.aether.md, ForkBuild.h.

## 202. Radio outside the adapter's subnet: commands as broadcasts from that adapter (10.11.2026, build 240)

- User log 10.11 (P1 ANAN-100D, gateware 59, static 192.168.1.168; PC adapter "Ethernet" at
  169.254.56.125/16 -- link-local, no DHCP): every start ends "0 packets" from the radio.
  The discovery broadcast from that adapter IS answered (radio -> PC works), but unicast
  to 192.168.1.168 cannot leave that adapter (bound socket: "Unable to send a message")
  and the routing table sends it somewhere else (wildcard socket: sent, nothing back).
  After switching the radio to P2 firmware it connected -- that firmware evidently came up
  with an address the PC could reach; the P1 path itself was not the cause.
- P2Transport: m_txHost = where commands go. When the radio is not in the subnet of the
  adapter it was found on, the session binds to that adapter FIRST (the wildcard is the
  second try, alternating as in build 221) and sends everything -- Discovery, P1 start /
  EP2 frames / stop, P2 General / DDC / High Priority / TX / audio, the native send path --
  to 255.255.255.255 from it, exactly as the discovery that the radio answers. Replies
  are still accepted only from the radio's address. Logged as a warning with the advice to
  give the adapter an address in the radio's subnet.
- Assumes the radio's gateware takes commands addressed to the broadcast address (it
  takes the discovery so addressed). Not verifiable here: needs the user's radio.
- Risk: on a LAN with several radios a broadcast reaches all of them; only used when the
  radio is outside the adapter's subnet (in practice a direct cable).
- Files: core/backends/anan/P2Transport.{h,cpp}, ForkBuild.h.

## 203. Build 241: hang report and PDB for the connect hang (10.11)

**Symptom (operator, 10.11):** the radio is switched off during a session, the
program searches, the radio is switched on, the program finds it and starts the
connect, then hangs (Windows "not responding"; under Qt Creator "terminated
abnormally"). A launch while the radio was still streaming to the dead process
hung the same way. Logs 20261110-183542 (18:45:15), -185057/-185138/-185257 and
-185812 (18:58:45) all end at `HL2 hardware options` right after
`Auto-connect ... (busy: taking over our previous session)`; every connect to a
radio that was NOT busy (18:36:23, 18:58:23) went through. No crash report: a
hang raises no exception, and the asynchronous log has nothing after the line.

**Not fixed yet** -- the cause is not identified. What follows
`HL2 hardware options` in `Hl2Backend::connectRadio` is the unicast discovery
run on the I/O thread with `BlockingQueuedConnection`; if that thread never
returns to its event loop (for a radio busy with our old session it still owns
the old P2 session socket after a link loss), the GUI waits forever. That is a
hypothesis, not a finding.

**Instrumentation in this build:**
- `src/main.cpp`: a watcher thread reads the main-loop heartbeat (250 ms timer);
  armed by the first beat; when the loop has not run for 8 s it calls
  `CrashDump::reportHang()` once.
- `src/core/CrashDump.h`: `writeReport()` split out of `write()`;
  `reportHang()` writes `aethersdr-crash-<time>.txt/.dmp` with its own once-flag
  (a later real crash still reports); the .txt names the main thread id. The
  minidump holds every thread's stack.
- `CMakeLists.txt`: MSVC Release builds with `/Z7` and links with
  `/DEBUG:FULL /OPT:REF /OPT:ICF`, so `AetherSDR.pdb` lies next to the exe and
  the dump can be symbolized (llvm-pdbutil / llvm-symbolizer here).

**Next:** reproduce (radio off -> on during a session), send the
`aethersdr-crash-*.txt/.dmp` and `AetherSDR.pdb` from the build folder.

## 204. Build 242: discovery probe could never return on a streaming radio (10.11)

**Symptom:** the radio left running in receive (streaming I/Q to a session that
no longer exists: power cycle during a session, or the program killed) made
every connect hang at `HL2 hardware options` (sections 202-203: "busy: taking
over our previous session"); Windows' "not responding" box, no report.

**Cause (code):** `P2Transport::discover()`, the unicast probe that
`Hl2Backend::connectRadio()` runs on the I/O thread while the GUI thread waits
(`BlockingQueuedConnection`):
- the inner `while (s.hasPendingDatagrams())` never checked the deadline, so
  the call returned only when the socket ran dry;
- every datagram, before parsing, went through `localRouteFor()` =
  `QNetworkInterface::allInterfaces()` (milliseconds on Windows).
A streaming radio that sends to the probe's socket feeds it thousands of
datagrams a second, faster than that loop drains them: the probe never returns,
the I/O thread never gets back to its event loop, the GUI waits forever.
(That the radio redirects its stream to the probe's port is inferred from the
behaviour, not captured on the wire.)

**Fix:** the deadline bounds the inner loop too; the route lookup runs only
for a parsed discovery reply from a new host.

## 205. Build 243: the pan's [+] rate button works once (10.11)

**Symptom:** the [+] at the top right of the panadapter (next DDC rate) steps
once, then stays disabled.

**Cause:** `SpectrumWidget::refreshRateLabel()` enables [+] while the current
rate is below the pan's upper bandwidth limit. Build 226 made two emitters of
`panBandwidthLimitsChanged` send the CURRENT rate (`m_sampleRateHz`) as that
limit: the `anan.rate` verb (the [+]/[-] buttons themselves) and
`addReceiver()`. After the first step the limit equalled the new rate, so [+]
went grey. Only the connect-time emitter sent the widest rate.

**Fix:** `Hl2Backend::widestRateForReceiversHz()` (the connect-time logic: the
widest rate the running receivers can share on the link) is the upper limit in
all three places. The zoom is unaffected: `panViewHz()` and the widget both cap
the view at the current rate's usable span.

## 206. Build 244: ADC1 band-pass bank on two-ADC boards over Protocol 1 (10.11)

**Request:** a user's ANAN-100D (reports board 3, ANGELIA) runs firmware
modified to drive a band-pass bank ahead of the second ADC "as on the
7000DLE". RX1's filters follow; RX2's do not. With Thetis set by hand to
7000DLE, RX2's filters worked.

**Protocol 1:** C0 0x24 C1 bits 0-6 = the ADC1 bank (same bit order as C0 0x12
C3: 0x01 13 MHz, 0x02 20 MHz, 0x04 9.5 MHz, 0x08 6.5 MHz, 0x10 1.5 MHz,
0x20 bypass, 0x40 6 m), bit 7 = ground RX2 on TX; C2 bit 0 transverter, bit 6
PureSignal. Thetis (networkproto1.c case 16) sends 0x24 in the rotation of
every board; its C1 comes from setAlex2HPF(), called per frequency only for
ORION MkII/7000/8000/G2/Anvelina/Red Pitaya, but also from the Alex2 bypass
setters for any model. We sent 0x24 every rotation with C1 = 0 on the 100D.

**Change (P2Transport, Protocol 1 C&C state):** on the legacy (100D/200D)
scheme, a board with two ADCs gets `alex2Hpf` from the frequency of the
receiver on ADC1, through `alexRxFilterFor()` -- the table the ORION MkII
branch already uses, i.e. what Thetis sends as 7000DLE. RX1 is unchanged
(the user confirms it switches correctly on the 100D scheme). Bit 7 stays 0.
Single-ADC boards keep 0. Stock ANGELIA/ORION: one more value in a register
they already receive every rotation; the stock gateware's handling of 0x24
was not read (inferred safe from Thetis' behaviour). Protocol 2 unchanged.

## 207. Build 245: "Forced RXx Filter control like 7000" (ANAN-100D, Protocol 1); build 244 reverted (10.11)

**Correction of 206:** the user runs Thetis as "7000DLE", so his modified
ANAN-100D has 7000-style band-pass banks ahead of BOTH ADCs, not a standard
RX1 path plus a second bank. Build 244 (ADC1 bank bits on every two-ADC legacy
board) is reverted: stock 100D/200D send C0 0x24 C1 = 0 again.

**Option:** ANAN Hardware page, right of the Model combo: checkbox
"Forced RXx Filter control like 7000" (`ananHwForce7000Filters`, AppSettings
`AnanForce7000Filters`, default off). Shown only when the session runs
Protocol 1 and the model's board is ANGELIA (ANAN-100D)
(`ananForce7000FiltersAvail`). While on, the "RX2 input grounded on transmit"
option is shown too.

**Effect (P2Transport, Protocol 1 C&C state only):** with the option on and
`m_profile.boardId == kBoardIdAngelia`, the ORION MkII filter branch is used:
C0 0x12 C3 = band-pass bank for the first receiver on ADC0
(`alexRxFilterFor`), C0 0x24 C1 = bank for the first receiver on ADC1 (else
the ADC0 one), LPF on the TX frequency, C0 0x24 bit 7 = the RX2-ground-on-TX
setting -- per ADC, whichever receiver uses it. PureSignal input bits, PA and
everything else stay the 100D's. Protocol 2 and other boards unaffected.
Plumbing: `Hl2Backend::ananHwFields/applyAnanHwFields/pushAnanModelOptions`
-> `P2Transport::setForce7000Filters` (queued; the rotation reads it on the
next EP2 packet).

## 208. Build 246: Windows display scaling not applied (10.11)

**Request:** at 120 % Windows scaling the program becomes too big; it must
always run at the monitor's native resolution (the layout is already sized
for normal monitors).

**Change (src/main.cpp, Windows, before QApplication):**
`QT_ENABLE_HIGHDPI_SCALING=0` and `QT_FONT_DPI=96`, unless already set in the
environment (testing override).
- With platform scaling off, Qt 6 (`QHighDpiScaling::logicalDpi`) takes the
  logical DPI from `QPlatformScreen::overrideDpi()`, which returns
  `QT_FONT_DPI` when set -- without it point-sized fonts would still grow by
  Windows' 120/144 dpi while px sizes stay native.
- `QT_SCALE_FACTOR` (View > UI Scale, `UiScalePercent`) is independent of
  `QT_ENABLE_HIGHDPI_SCALING` (`m_globalScalingActive` depends only on it), so
  the program's own scale still works.
- The process stays per-monitor DPI aware: no bitmap stretching, one logical
  pixel = one screen pixel on every monitor.
- After QApplication, one log line per screen: logical size, device pixel
  ratio, logical and physical DPI (expected on Windows: ratio = UI Scale,
  logical DPI 96).

Sources: doc.qt.io/qt-6.12/highdpi.html; qhighdpiscaling.cpp and
qplatformscreen.cpp (overrideDpi) in qtbase.

## 209. Build 247: fork release check every 15 minutes (10.11)

**Request:** the program checks for a new release at start only; check every
15 minutes.

**Change (MainWindow_Menus.cpp, wireUpdateChecker):** the build-211 check of
this fork's GitHub releases (tag `build-N`, N > AETHER_FORK_BUILD) is a lambda
run 8 s after start (as before) and then by a 15-minute QTimer
(`forkReleaseCheckTimer`). A later check that finds a newer release updates
the existing status-bar link (`forkNewReleaseLink`) instead of adding a second
one. 4 requests/hour against GitHub's 60/hour unauthenticated limit; failures
stay quiet (logged only), as before.

## 210. Build 248: steady flag on digit tuning; flag antennas follow the ANT panel (10.11)

**1. Flag jumps on the per-digit wheel.** The digit-wheel path (build 235)
moves the pan centre through `applyTuneCenteringWrite` -> RadioModel -> the
pan model -> `SpectrumWidget::setFrequencyRange()`. A centre change below 25 %
of half the span is a "small nudge", which SpectrumWidget ANIMATES
(`m_panCenterAnim`), while the slice frequency moves in one step
(`pushSliceFrequencyToOverlays` + `setFrequency`). The flag and filter
therefore jumped by the step and slid back during the animation.
Fix (MainWindow_Wiring.cpp, digitTuneRequested): `sw->setFrequencyRangeImmediate(newCenter, bw)`
first; the model's echo of the same centre is then a no-op.

**2. Flag antenna buttons vs the ANT panel.** The flag shows the slice's
rxAntenna/txAntenna; Hl2Backend never published them, so they stayed at the
"ANT1" default and a pick there went nowhere (requestSliceRxAntenna
unsupported, Field::TxAntenna unsupported). The ANT panel keeps the radio's
antennas in AppSettings `AnanRxAnt` / `AnanTxAnt` (0..2), pushed by the 300 ms
tick.
- `emitSliceState`: rxAntennaList/txAntennaList ANT1-3, rxAntenna/txAntenna
  from the settings; a receiver on ADC2 shows "RX2" (ANT1-3 feed ADC1 only).
- `requestSliceRxAntenna` / `requestSliceControl(TxAntenna)`: write the
  setting (`setAnanAntennaSetting`); on ADC2 the RX pick is refused by
  re-echoing the state.
- The tick republishes every slice when either setting changed (panel or
  flag).
- SpectrumOverlayMenu: `syncAnanAntennas()` on the slice's rx/txAntennaChanged
  and in `syncAntPanel()` (panel opening), so the panel follows a pick on the
  flag.

## 211. Build 249: a click outside an overlay panel only closes it (10.11)

**Request:** a Band / ANT / Display / Memory panel opened from the pan's
overlay menu closes on the first click outside it, and that click activates
nothing else. Not the WB window (it has its own closing).

**Change (SpectrumOverlayMenu):** an application event filter
(`qApp->installEventFilter(this)`), handled in
`closeSubPanelsOnOutsidePress()`: on a mouse press (or double click) delivered
to a QWindow -- Qt delivers to the window before the widget under it -- while
any of band, XVTR, ANT, DAX, Display or Memory panel is visible, in the same
top-level window as the pan, and outside every open panel:
`hideAllSubPanels()` and the event is consumed. Presses inside an open panel,
and in other top-level windows (combo/menu popups opened from a panel,
dialogs, the WB window) pass through untouched. A click on another overlay
button therefore only closes the open panel; the next click opens the other.

## 212. Build 250: VFO flag owns the mouse; status/title bar cleanup (10.11)

**1. Mouse acts on the filter through the VFO flag.** VfoWidget accepted
press, release and wheel, but not move or double click: QWidget's default
handlers ignore them and Qt passes them to the parent, the SpectrumWidget,
which hovered / dragged / double-click-tuned the filter under the flag. Moves
without a button also skip a widget without mouse tracking and go to the
parent. Fix: `VfoWidget::mouseMoveEvent` and `mouseDoubleClickEvent` accept,
and the flag has mouse tracking.

**2. Title bar:** the green link-status line ("ANAN-100D  <IP>") is not shown
while connected (`setLinkStatus` with empty text hides the button; the
tooltip keeps "Connected to ..."). Searching / connecting / lost / failed /
disconnected still show and stay clickable.

**3. Status bar, left:** the radio type / firmware stack is hidden (kept,
reparented to the status-bar container, for the code that fills it), with
its separator.

**4. Status bar, right:** "Network: quality" hidden (kept for its updaters;
Network Diagnostics stays in the menus), with the separator before it.

**5. Antenna indicator:** two lines, "RX: ANTn" over "TX: ANTn" (the ANT
panel's AnanRxAnt / AnanTxAnt); the TX line red while transmitting; the
operator's antenna names in the tooltip.

## 213. Build 251: painted PC Audio pill, one-line radio tab (10.11)

**1. PC Audio / Codec pill (TitleBar).** It was a QPushButton with a
stylesheet `border-radius: 12px` at 78 x 24: Qt's stylesheet engine draws
rounded borders without antialiasing, hence the jagged, "stretched" outline.
Now `PillButton` (TitleBar.cpp) paints an antialiased capsule on half-pixel
edges (radius = height / 2), a 1 px border, centred bold 9 px text, slightly
lighter on hover, darker pressed, an accent ring on keyboard focus. Size a
third smaller: 52 x 16. `applyPcAudioStyle()` sets the same theme colours
the stylesheets used (codec: accent.dim / accent.bright; PC: background.success
/ accent.success).

**2. Radio tab with the animated link dot (RadioTabBar).** The second line
(model / state / detail) is gone: the dot's colour and pulse show the state,
the tooltip still carries the full description, and the title bar shows the
state in words when not connected (build 250). The name is centred on the
dot's line; tab height 36 -> 24, side padding 14 -> 10.

## 214. Build 252: quick-memory buttons 20 % lower (10.11)

The left quick-memory panel (BandStackPanel): the remembered-frequency
buttons are 72 x 26 (were 72 x 32). The header buttons (x, +, gear) are
unchanged.

## 215. Build 253: PC Audio pill +15 %; station name box (10.11)

**PC Audio / Codec pill:** 60 x 18, text 10 px (was 52 x 16, 9 px). Six style
variants were shown to the operator as a mock-up (capsule, segmented switch,
dark capsule with LED dot, glass capsule, slide switch, flat tab with
underline); the choice is pending.

**Station name box (status bar centre, the radio's nickname):** font 21 ->
19 px and side padding 12 -> 11 px (the box ~10 % smaller), corners rounded
4 px, arrow cursor (was the pointing hand). Double click opens Radio Setup >
Radio and focuses the Nickname field (`radioNicknameEdit`, text selected)
instead of the connection dialog. Tooltip: "Station name -- the radio's
nickname. Double-click to change it (Radio Setup > Radio > Nickname)."

## 216. Build 254: PC Audio button in style F; full-width bar on the radio tab (10.11)

**PC Audio / Codec (operator's choice F of the mock-up):** `PillButton` now
paints a flat button, 4 px corners, background color.background.1 (lighter on
hover, darker pressed), and a 2 px bar along the whole bottom edge, clipped to
the rounded outline: color.accent.success for PC Audio, color.border.accent
(blue) for Codec. Text 10 px bold, centred above the bar. Still 60 x 18.

**Radio tab (RadioTabBar):** the active tab's accent bar used to be a 2 px
line from radius+4 to width-radius-4, so it stopped short of both edges. Now a
2 px fill across the full width, clipped to the tab's rounded shape -- the
same bar as the PC Audio button.

## 217. Build 255: UTC clock in the status bar (10.11)

Status bar, left, after the antenna indicator and a separator: a two-line
stack (`ananUtcClock`), "UTC Time:" over HH:mm:ss -- the PC clock converted to
UTC (`QDateTime::currentDateTimeUtc()`). A single-shot timer re-arms just
after each whole second, so the seconds change on time; the value label has a
fixed minimum width for "00:00:00" (12 px font) so the bar does not shift;
tooltip with the full UTC date and time.

## 218. Build 256: station box fixed height and grows with its text; PC Audio drawn like the radio tab (10.11)

**Station name box.** It had size policy (Minimum, Preferred), so it
stretched to the status bar's full height. Now (Fixed, Fixed), height 35 px
(17 % below what it stretched to), added with Qt::AlignVCenter (also when it
returns from the permanent-widget slot during a status message,
MainWindow_Wiring). The width follows the text: one font size (19 px), no
shrinking of multi-word names, `setFixedWidth(sizeHint + 2)` on every change,
elided only beyond 420 px. stationFittedFontPx / stationFittedText kept,
[[maybe_unused]].

**PC Audio / Codec.** The blue frame that appeared after a click and then
went away was the focus ring (`hasFocus()`), drawn after every click. Now the
button is drawn like the radio tab: the tab's active background
(hover background on hover), 1 px border and 2 px full-width bottom bar both in
the target's colour -- color.accent.success for PC Audio, color.border.accent
for Codec; a focus ring only for keyboard focus (Tab), focus policy TabFocus.
Frame 66 x 20 (+10 %), text 10 px unchanged, corners 6 px.

## 219. Build 257: PC Audio text drawn like the radio tab's (10.11)

The operator still saw no antialiasing on the PC Audio button next to the
radio tab. Shape and border are drawn the same way on both; the difference
was the text: the button used its own font forced to bold 10 px, which
Windows hints to the pixel grid, while the tab uses the theme's UI family at
a fractional size, weight 600. PillButton now does the same: font.family.ui,
10.5 px as fractional points, DemiBold, PreferAntialias, PreferNoHinting,
drawn with QFontMetricsF into a QRectF.

Question answered (no change): there is no filter-steepness setting in the
menus. The RX bandpass length is fixed in Hl2RxDsp (rxFilterTapsFor /
rxMinimumPhaseFor): SSB/AM/digital 8192 taps, minimum phase; CW 4096 taps
linear phase (8192 with a notch or a passband under 100 Hz); DSP rate 48 kHz,
WDSP window type 0.

## 220. Build 258: SSB transmit filter 16384 taps, minimum phase (10.11)

**Complaint:** poor opposite-sideband suppression on SSB transmit.

**Cause:** the TXA bandpass (bp0, and bp1/bp2 behind the compressor) was left
at create_txa()'s `max(2048, dsp_size)` = 2048 taps, Blackman-Harris 7-term
(wintype 1), at the TX DSP rate of 192 kHz. Its skirt is ~+-0.66 kHz
(7 bins x 192000 / 2048) around the low edge, which is next to the opposite
sideband. Computed for a 100..2800 Hz passband (BH7 windowed complex
bandpass, as fir_bandpass builds it), response relative to 1 kHz at the
opposite-sideband frequencies -100 / -200 / -300 / -500 Hz:
2048 taps: -23 / -38 / -59 / -142 dB; 16384 taps: -202 / -214 / -206 / -204 dB.

**Change:**
- `Hl2TxDsp::buildModulator`: Config::filterTaps = kTxFilterTaps (16384,
  a multiple of the 1024 DSP block) and minimumPhase = true.
- `WdspChannel::open` (transmit): `TXASetNC(filterTaps)` and
  `TXASetMP(minimumPhase)` before SetTXABandpassFreqs (channel not running
  yet, so TXASetNC's stop/restart is a no-op). TXASetNC also sets the CFIR and
  FM / FM-emphasis lengths; TXASetMP the FM / FM-emphasis phase.
- `aether_wdsp.h`: declarations for TXASetNC / TXASetMP (TXA.c, PORT).
Minimum phase: the 8x longer filter would add 16384/2/192000 = 43 ms as
linear phase (signal and MON); the magnitude, i.e. the suppression, is the
same. Not verified on the air / on a spectrum analyser yet.

## 221. Build 259: status messages no longer move the bar (10.11)

**Symptom:** when a status message appeared at the bottom left, everything to
the right "flew away".

**Cause:** QStatusBar shows a temporary message by hiding every normal
widget -- the whole status-bar container. The old workaround
(wireStatusBarMessages) moved the station box into the permanent-widget slot
for the duration and hid the container, so the station box and everything
right of it jumped, then jumped back.

**Change:**
- MainWindow.cpp: the left stretch before the station box is now
  `m_statusMessageLabel` (stretch 1, size policy Ignored, compact 12 px style,
  left aligned). Its text never changes the layout, so the station box (still
  between two equal stretches) and everything right of it stay put; a long
  message is cut at the station box, full text in the tooltip.
- MainWindow_Wiring.cpp `wireStatusBarMessages()`: on messageChanged(text),
  the text goes to the label; the caller's timeout is read from QStatusBar's
  own running timer (the direct child QTimer it creates in showMessage) and
  re-armed on `m_statusMessageTimer` (no running timer = until replaced or
  cleared); then `statusBar()->clearMessage()` (guarded by
  m_statusMessageClearing), so the temporary-message area never takes over.
  A caller's own clearMessage() clears the label.
- Side effect: QStatusBar::currentMessage() is empty right after each
  message (the automation mirror sees the text first, then "").

## 222. Build 260: PC Audio border like the radio tab's (10.11)

The operator's screenshots of the PC Audio button and the radio tab: the
corner pixels of BOTH carry intermediate colours, so both are antialiased.
PC Audio looked stepped because its 1 px border was full-brightness green on
a 6 px corner (high contrast on the dark bar), while the tab draws a dim
border on 8 px corners and keeps the bright colour only for its bottom bar.
PillButton now does the same: corner radius 8, the border at 55 % alpha of
the target colour (green PC Audio / blue Codec), the 2 px bottom bar at full
colour.

## 223. Build 261: torn-off applets keep their width, a visible pin, and a × (10.11)

**Width.** `ContainerManager::floatContainer` sized the new window from the
saved geometry or the container's size hint, which for some applets is far
narrower than their panel width. It now records the width the container had
in the panel (only when it is really on screen; a never-shown widget reports
640 x 480) and, after `restoreAndEnsureVisible`, widens the window to at least
that (the floating window has zero margins).

**Pin.** It is "always on top" (Qt::WindowStaysOnTopHint): the floating
window already stays above the main window (it is parented to it), the pin
keeps it above other programs as well. Pressing it seemed to do nothing
because the glyph is a colour emoji and the checked state only changed the
text colour. Checked now has a lit amber background and border; hover a light
background; tooltip "Keep this window above other programs too (always on
top)".

**Close.** The × was hidden on floating containers. It now stays
(`setFloatingState`: visible when close is allowed). `onCloseRequested` on a
floating container docks it back to its slot first, then hides it -- the same
state as closing it in the panel, so showing it again brings it back in the
panel and no empty floating window is left.

## 224. Build 262: the torn-off width floor survives the opening fits; pin = thin frame (10.11)

**TX Control still shrank (build 261 did not hold).** The resize after
`restoreAndEnsureVisible` was undone by FloatingContainerWindow itself: since
build 219 it sizes the width to the content's size hint on opening
(`m_fitWidth`, in fitHeightToContent, run at once, after one event-loop turn,
after 300 ms and on every LayoutRequest meanwhile). Now the floor goes into
those passes: `FloatingContainerWindow::setOpeningWidthFloor()` (set by
ContainerManager before restoreAndEnsureVisible), and fitHeightToContent
uses `max(content width, floor)`. The floor is the container's width in the
panel or, if wider, its panel column's (c->parentWidget()).

**Pin:** engaged = a 1 px amber frame (border-radius 3), no fill, no hover
fill.

## 225. Build 263: two-colour pin icon (10.11)

Operator: the idle pin in the colour of the × next to it, the engaged pin red
as the emoji is; no frame. A colour emoji cannot be recoloured by a
stylesheet, so the pin button now has no text but a QIcon (`pinIcon()` in
ContainerTitleBar.cpp): the 📌 emoji drawn into a 32 px pixmap; for the Off
state its silhouette is filled with #c8d8e8 (CompositionMode_SourceIn), for
the On state the emoji as is (red). Icon size 13 px in the 16 px button; hover
a faint background; the build-262 checked frame is gone.

## 226. Build 264 — refused transmit builds no TX state

**Report (10.11).** Keying in a receive-only mode or outside the amateur
bands still showed the zoomed TX panadapter, although nothing went on air.

**Cause.** ANAN has no PTT readback, so `RadioModel` publishes the TX edge
(`publishCommandedBackendTransmitEdge` → `radioTransmittingChanged` →
`SpectrumWidget::setTransmitting` → TX zoom) as soon as it commands the key.
The band gate lives in `Hl2Backend::applyKeying()` (build 223) and refused
the key only after that edge; the receive-only-mode refusal sat in
`setTransmit` after `TransmitModel` had already gone optimistic.

**Fix.**
- `IRadioBackend::transmitRefusalMessage()` (default empty); `Hl2Backend`
  implements it with the same test as `applyKeying()` (TX frequency =
  dial + XIT, band plan).
- `RadioModel::localPttInterlockMessage()` (the `TransmitModel` PTT
  preflight for MOX, TUNE, TCI, WSPR, CAT, DAX) now returns the backend's
  refusal and the receive-only-mode message, ahead of the DAX bypass.
- `forwardNonFlexCwKeying()` asks the same before Break-In keys.

A refused key now shows only the message at the bottom: no TX zoom, no TX
indicator, no Quindar or tune latch. `applyKeying()` keeps its own gate.

## 227. Build 265 — waterfall brightness

**Request (10.11).** The waterfall has no brightness control; Black Level
only shifts the scale.

**Existing controls.**
- Black Level: the black point, offset from the noise floor, 0.5 dB per step
  (50 = at the floor).
- WtrFall Gain: the width of the black-to-full range,
  120 - 0.91 x Gain dB.

Neither changes how bright the levels in between are.

**Change.** A new "WtrFall Bright:" slider under Gain (0..100, default 50)
sets the palette's gamma in `SpectrumWidget::waterfallLevelToRgb`:
`t' = t^(3^((50 - b)/50))`.
- 0 gives gamma 3 (darker); 50 is linear; 100 gives gamma 1/3 (brighter).
- Black (t = 0) and full scale (t = 1) do not move.
- Stored per pan as `DisplayWfBrightness`.
- The value is folded into `waterfallPaletteToken()`, and the setter calls
  `recolorWaterfallViewport()`, so the visible history is recoloured at once
  (the history holds levels, not colours).
- Included in Display "clone to other pans" and in "reset to defaults".

## 228. Release build 265 prepared (10.11.2026)

- docs/release-notes-build-265.md: builds 236-265 by topic (DRM, connection, transmit,
  ANAN-100D P1 filter option, panadapter/waterfall, window and status bar), with what is
  untested on a radio. README.md: "Build 235" summary replaced by "Build 265".
- The repo journal lacked sections 198-202 (builds 236-240, DRM and broadcast transport);
  restored from the project copy before 203.
- Archive AetherSDR-ANAN-build265-win64-portable.zip: the build 235 archive with the build 265
  exe (build\Desktop_Qt_6_12_0_MSVC2022_64bit_Release\AetherSDR.exe = Desktop\AetherSDR-Release\
  AetherSDR.exe, 10.11 23:07 local, contains displayWfBrightnessSlider), fdk-aac.dll (new: DRM
  audio, loaded at run time, not an import), licenses/fdk-aac/NOTICE, README-build265.md; top
  folder AetherSDR-ANAN-build265; 90 entries, 154137564 bytes, SHA-256
  b4ef02eb6ba02dfd8067d989228143e82c81363c31e41cb457a974d285eb34d3. Saved to
  Desktop\AetherSDR-Release.
- The release (tag build-265 on main) is created by the operator in the GitHub web UI; the tag
  push starts the AppImage workflow (which now also builds FDK-AAC, build 238).

## 229. Build 266: TCI streams as Thetis serves them; I/Q no longer mirrored (10.11)

**Request:** TCI clients ask for raw I/Q (skimmers) as well as audio; do every
stream mode the Thetis server has, chosen by the client, with as few settings as
possible.

**Thetis (ramdor/Thetis, Console/TCIServer.cs, cmaster.cs, ChannelMaster/tci.c,
pipe.c), read for this:**
- Stream types are IQ 0, RX audio 1, TX audio 2, TX_CHRONO 3, LINEOUT 4 (the last
  is declared but never sent). Every frame has the 64-byte header.
- **I/Q.** The receiver's raw DDC input at its hardware rate (down-sampled to
  384 kHz at most), always float32. Q is negated by default ("Swap IQ", on).
  `iq_samplerate:` from a client is only echoed; the published rate is the
  highest receiver rate. "Always stream IQ" exists for clients that never send
  iq_start.
- **RX audio.** `audio_samplerate` is 8/12/24/48 kHz. `audio_stream_sample_type`
  is int16/int24/int32/float32, answered by name. `audio_stream_channels` is
  1/2. `audio_stream_samples` is 100..2048 per channel per packet; when unset,
  the default follows the rate: 256/512/1024/2048. Packets have exactly that
  many samples per channel, and the header length counts all values.
- **TX_CHRONO.** The request carries the client's rate, type and channels. Its
  length is samples x channels once the client has negotiated any
  audio_stream_* / tx_stream_audio_buffering ("modern"), otherwise samples.
  `tx_stream_audio_buffering` (50..500 ms) sets how far ahead audio is
  requested.
- **TX audio in.** Every sample type is decoded. A header channels field of
  1 or 2 is trusted; any other value falls back to legacy JTDX layout
  detection.
- `line_out_start/stop` switch Thetis' VAC; that is not a stream.

**Ours before:** IQ at 24..192 kHz chosen by the client.
- int16/float32 only, and the sample type was answered as a number.
- audio_stream_samples and tx_stream_audio_buffering were acknowledged and
  ignored.
- TX_CHRONO was fixed at 48 kHz / float32 / stereo / length 2048 every
  21.3 ms, whatever the client negotiated. A 24 kHz client was asked at twice
  its rate; MSHV set to int24/int32 got no RX audio it could read.

**Found while reading: the ANAN I/Q tap (build 222) sent the HPSDR wire as
is.**
- The wire puts a signal above the NCO at negative frequency (Hl2RxDsp: the
  demodulator takes the wire, the spectrum the conjugate; hl2_shift_test).
  Every skimmer therefore saw the band mirrored about the dds.
- With a dragged pan view, the view-offset mixer moved the centre the wrong
  way.
- This is the same reason Thetis negates Q by default.

**Changes:**
- **Hl2Backend::tciIqTap:** conjugate the wire, then mix the view centre
  (NCO + viewOffsetHz = the dds TCI reports) to 0 Hz, then resample.
- **TciServer:**
  - `iq_samplerate` also accepts 384000 for the ANAN tap; DAX rate lists do
    not apply to it.
  - `audio_stream_sample_type` takes int16/int24/int32/float32 (by name or
    number) and answers by name.
  - `audio_stream_samples` takes 100..2048.
  - `tx_stream_audio_buffering` takes 50..500.
  - Bare GET forms for audio_samplerate, audio_stream_*,
    tx_stream_audio_buffering.
  - A rate change re-announces the default packet size while none was set.
  - All of these are carried to the worker (TciStreamConfig.streamSamples /
    txBufferingMs / modern).
- **TciIoWorker:**
  - RX audio is encoded as int24 and int32 too. Packets are exactly
    audio_stream_samples frames when the client set it; without it they go
    out as converted, as before.
  - TX_CHRONO uses the client's rate, type and channels, with length = samples
    (x channels when modern). The period equals what that length lasts at the
    rate, and tx_stream_audio_buffering sets how many requests go out at the
    start (1 when unset).
  - TX audio: int24/int32 decoded; a header with channels = 2 is
    stereo (WSJT-X duplicate-stereo detection still first).
- Unchanged for a client that negotiates nothing: 48 kHz float32 stereo,
  variable RX packets, chrono length 2048 every 21.3 ms.

**Not done, with reasons:**
- "Always stream IQ": a setting for clients that never send iq_start. The
  operator prefers no settings, and every current skimmer sends it.
- The LINEOUT stream: Thetis does not send it either.
- line_out_start/stop are still acknowledged only.
- iq_samplerate is per server, not per receiver, as in TCI itself.

**Untested on a radio:** a skimmer on the conjugated I/Q (signals should now
sit at their true frequencies); MSHV with int24/int32 and 24 kHz.

Files: core/TciServer.{h,cpp}, core/TciIoWorker.{h,cpp},
core/backends/hl2/Hl2Backend.{h,cpp}, ForkBuild.h.

## 230. Build 267: "Forcedly assign the radio" replaces the 7000-style filter option (11.11)

**Report.** Build 245's "Forced RXx Filter control like 7000" did not help the
user. His radio (a modified ANAN-100D) reports itself to Protocol 1 discovery
as HERMES, so the checkbox, offered for ANGELIA only, never showed.

The operator also noted that the Radio model combo could be changed without any
option enabled.

**Change.**
- **Setup > Radio (the radio's page), Model.** The checkbox **"Forcedly assign
  the radio"** sits right of the Radio model combo.
  - **Off:** the combo is locked to the type the radio reports.
  - **On:** a warning appears first (English, Yes/Cancel, shown only when
    turning it on): the wrong type can seriously limit the radio's functions
    and may damage it. After Yes, the combo offers every type the connection's
    protocol drives: all types over Protocol 2; over Protocol 1, only types
    whose board has Protocol 1 firmware (Hermes, Hermes II, Angelia, Orion,
    Orion II).
  - A hint under the row names the reported type.
- **Settings, new header core/backends/anan/AnanForcedModel.h.** Two keys per
  REPORTED board id, as the model choice always was:
  `AnanForceModel_b<id>` (True/False) and `AnanForcedModel_b<id>` (model key).
- **Migration.** A pre-267 same-board choice (`AnanModel_b<id>`) that is not the
  board's default becomes a forced assignment of that model, without the
  warning. This covers 8000DLE / Orion MkII / Anvelina / G2-1K / 10E / 100B,
  which keep running as before.
- **P2Transport.** `setForcedBoardId(reported, forced)` fills a per-reported-board
  map of atomics. `configureSession()` records the reported board
  (`reportedBoardId()`) and runs the session with the forced board's
  BoardProfile: DDC plan (P1 `p1DdcPlanFor`), ADCs, Alex scheme (ORION MkII
  band-pass per ADC), PureSignal, status layout. Over Protocol 1, a board
  without P1 firmware is refused and logged.
  - The P1 HERMES -> 10E/100B special case (it read `AnanModel_b1`) is now the
    forced HERMES II profile.
  - Build 245's `m_force7000Filters` and its condition are removed: forcedly
    assign a 7000DLE instead.
- **Hl2Backend.**
  - The model is the forced one, if any; otherwise the reported board's default.
  - `pushAnanForcedBoard()` gives the transport every board's assignment at
    connect, before discovery and the first packet, and again on every change.
  - On link-up, `m_ananBoardId` follows the transport's reported board (the
    unicast probe can go unanswered).
  - `hw.get` returns `ananForceModel` and `ananReportedModel`; `hw.set` takes
    `ananForceModel` and `ananModel` (the latter only while forced).
- `AnanDiscovery::modelName()` shows the forced type in the radio list.
- The hardware profile applies from the next connection (the note says so);
  meters and connector options follow at once.

**For the user:** connect, Setup > Radio > the radio's page, turn on "Forcedly
assign the radio", choose ANAN-7000DLE (filters per ADC as in Thetis
"7000DLE"), then reconnect.

Files: anan/AnanForcedModel.h (new), anan/AnanDiscovery.{h,cpp},
anan/P2Transport.{h,cpp}, hl2/Hl2Backend.{h,cpp}, gui/RadioSetupDialog.cpp,
ForkBuild.h.

## 231. Build 268: every volume defaults to 1; the radio waits for the operator at program start (11.11)

**Request (operator).** Two things:
- Users are deafened at start. Every volume control (each receiver and the
  overall ones) should default to 1.
- At program start, run discovery and show the antenna picture as usual, but
  do not start the radio until the user presses the button.

**Volumes.** These defaults apply only where nothing is saved; saved levels
are untouched.

| Control | Was | Now | Where |
|---|---|---|---|
| PC master (`MasterVolume`) | 100 | 1 | MainWindow, TitleBar, RadioModel, TciProtocol, DeviceDiagnostics |
| Radio codec (`CodecVolume`) | 50 | 1 | MainWindow, TitleBar, Hl2Backend |
| Headphone slider | 50 | 1 | TitleBar |
| Receiver level without a saved value (Hl2Backend receiver memory `audioGain`) | 100 % | 1 % | `Receiver::audioGain` 1.0 -> 0.01 |
| `SliceModel::m_audioGain` | 50 | 1 | |
| Band stack entry `audioGain` | 50 | 1 | |

The automation fixture (`RadioModel::automationApplySliceFixture`) keeps 50.

**Start.**
- `MainWindow::m_waitForUserStart` (true at start). While it is set,
  `maybeAutoConnectAnan` does not connect a found radio. Instead it shows the
  new `LinkState::Ready` in the title bar ("<radio> — click to start") and keeps
  the panadapter's antenna animation with "<model> found — click its tab or the
  status above to start".
- The radio's tab is listed as usual. If the radio leaves discovery, the text
  goes back to "Looking for your radio…".
- The saved routed-IP auto-connect also waits.
- `connectRadioNow()` (the radio tab, the status text) and any completed
  connection (the connection dialog) clear the flag. After that, reconnects
  following a dropped link stay automatic, as before.

Files: gui/MainWindow.{h,cpp}, gui/MainWindow_Session.cpp, gui/TitleBar.cpp,
models/RadioModel.cpp, models/SliceModel.h, core/DeviceDiagnostics.cpp,
core/TciProtocol.cpp, core/BandStackSettings.{h,cpp},
hl2/Hl2Backend.{h,cpp}, ForkBuild.h.

## 232. Build 269: quick-memory buttons lower, and they restore the whole panadapter view (11.11)

**Request (operator).** The quick-memory buttons on the left:
- 10 % lower, with the gaps grown so each button stays in its place;
- also remember the sample rate, the filter position on the panadapter, the
  zoom and the pan centre, so a press restores the look entirely.

**Panel (BandStackPanel).**
- Buttons go from 72x26 to 72x23.
- The layout spacing goes from 3 to 6. The pitch stays 29 px, so each button
  keeps its slot.
- A 1 px top margin keeps the button centres where they were.
- The tooltip also shows the stored rate, pan centre and span.

**Memory.** BandStackEntry gets `sampleRateHz`, `panCenterMhz` and
`panBandwidthMhz` (JSON; absent in older entries = 0, view left alone).
- **Add** (the + button and auto-save dwell). `MainWindow::captureBandStackView`
  stores:
  - `AnanUiState::sampleRateHz`;
  - the slice's pan `effectivePanCenterMhz` / `effectivePanBandwidthMhz`, i.e.
    the visible span = the zoom.
- **Recall.** If the stored rate differs, `hl2` extension `anan.rate` goes
  first (it resets the zoom). The rest follows 250 ms later in
  `recallBandStackEntry`: mode, tune, filter, antennas, AGC, volume, NB/NR,
  WNB as before. Then `restoreBandStackView` sends `requestPanCenter(pan,
  center, span, Range)`, and once more 300 ms later, after the tune's own
  pan-follow. The slice frequency plus the pan centre put the filter where it
  was on the pan.
- The recall lambda body moved into the member `recallBandStackEntry`
  (MainWindow.cpp, end of file).

Files: gui/BandStackPanel.cpp, gui/MainWindow.{h,cpp},
core/BandStackSettings.{h,cpp}, ForkBuild.h.

## 233. Build 270: 3D ridges in the level's colour; no popup after a right click on the scale (11.11)

**Requests (operator).**
- A right click on the frequency scale shows a useless popup; remove it.
- Is the CPU load the whole system's or only the program's? Show the
  program's own.
- In the 3D panadapter the peaks are drawn with white lines; show the natural
  colour of the level instead.

**3D (resources/shaders/dss_mesh.frag).** The ridge outline ribbon (edge < 0)
was a "neutral ridge highlight": `mix(bgFill, vec3(0.92), 0.45 + 0.55*fade)`,
near-white at the front. It is in upstream v26.10.4 as forked, not a regression
of ours. It now takes the palette LUT colour at its own level (`vLut`), hazed
with depth like the curtain, and keeps the slice shadow. Its opacity goes from
0.12 + 0.5·fade to 0.25 + 0.6·fade, so the coloured ridge still reads over the
curtain. The shader is compiled by the build (qt_add_shaders).

**Right click on the scale (SpectrumWidget).**
- A right press on the frequency scale starts the span drag; nothing consumes
  the QContextMenuEvent Qt sends after the release.
- SpectrumWidget had no `contextMenuEvent`, so the event travelled up to a
  parent's context menu.
- `SpectrumWidget::contextMenuEvent` now accepts it. Every menu of the widget
  opens from its mouse handlers.
- Unverified: whether that parent popup is what the operator saw.

**CPU.** No change. The status bar "CPU:" is already the program's own:
GetProcessTimes (kernel + user) of AetherSDR's process, over the wall time,
divided by the logical CPU count. That is the same as the process's line in
Task Manager.

Files: resources/shaders/dss_mesh.frag, gui/SpectrumWidget.{h,cpp},
ForkBuild.h.

## 234. Build 271: 3D ridges continuous on the slanted sides ("lids") (11.11)

**Report (operator, with a screenshot).** In the 3D panadapter, farther from
the centre and with more slant, gaps appear: a narrow peak reads as a dotted
line across the rows. Asked: fill a gap with the previous value until a new one
arrives.

**Cause.** Each row is a vertical curtain at its own depth, and a frequency
lands at x = 0.5 + (f - 0.5)·w(depth). Toward the sides, successive rows of one
peak step sideways by (f - 0.5)·(1 - backWidthFrac)/96 of the plot width, about
2 px at the edge. A peak is about one texel wide (768 columns), so the steps do
not touch.

**Fix.** Each row now also draws a "lid": its own ridge, the same sample,
carried back to the next row's depth.
- dss_mesh.vert: tag 6 = lid far edge. It is decoded as an overlay-layer top
  vertex with `geometryV += 1/visibleRows` (not on the rear boundary row).
- SpectrumWidget mesh build: the lid quads come first for each row, on the
  overlay layer (the one interior rows draw; base shows only at the two
  boundary rows), then the curtains. The painter's order keeps each curtain
  front over its lid and every lid over the older row behind it.
- `dssFillVerticesPerRow` is now (kMeshCols - 1)·18 (was ·12): the fill VBO
  grows by half, about 8 MiB more GPU memory.
- The gap between two rows now shows the row's level until the next row.
  Outlines are unchanged.

Shader not validated here (no GLSL compiler); the build compiles it.

Files: resources/shaders/dss_mesh.vert, gui/SpectrumWidget.cpp, ForkBuild.h.

## 235. Build 272: depth walls instead of lids, so a carrier's bar is solid (11.11)

**Report.** After build 271 the surface looks better, but the carriers' bars
still show gaps; they should be solid lines.

**Cause.** Build 271's lid only joins the row's ridge (top line) to the next
row's depth. It is a thin band near the peak top. Between the two vertical
curtains of a carrier, below that band, the background still shows.

**Fix.** The lid quads are replaced by a depth wall per column (same vertex
count). Each wall joins the column's curtain line (top and floor) at this row's
depth to the same line one row deeper. Tags: 6 = far top, 7 = far floor;
dss_mesh.vert maps them to the overlay top/floor vertex with
`geometryV += 1/visibleRows`. Each wall is a parallelogram as wide as the
sideways perspective step, so a carrier's bar is filled from the floor to its
peak between rows. Order, layer and memory are as in build 271.

Files: resources/shaders/dss_mesh.vert, gui/SpectrumWidget.cpp, ForkBuild.h.

## 236. Build 273: 3D closed step surface; the waterfall black point no longer moves with zoom (11.11)

**Reports (operator, screenshot).**
- 3D: still no real change after build 272. The picture should be held until
  the next level arrives, seamlessly.
- The waterfall's black level changes with zoom. The floor should be taken
  over the whole width of the pan, not only the part on screen.

**3D.** Each row is now a closed step:
- the curtain at its depth;
- a depth wall per column (build 272);
- the lid from build 271 again, between the columns.

Walls and lids carry the row's own values back to the next row's depth, so the
row is held until the next one takes over, with no seam in depth or across
columns. Per row and column there are 24 fill vertices (overlay layer: wall,
lid, curtain; base: curtain). The fill VBO is about 35 MiB. Shader unchanged
since build 272 (tags 6/7).

**Black point (SpectrumWidget::updateWaterfallRow, SW auto black).**
- The ANAN waterfall row is already the whole captured band (Hl2Backend,
  96 % of the rate).
- Each of its ≤ 8192 bins is the mean of n·0.96/outN FFT bins, and the FFT
  size n grows as the view narrows (spectrumFftSizeFor). One bin at zoom-out,
  ~8 at zoom-in.
- The estimator is a trimmed mean in dB (bins at or below the row's mean). It
  depends on the spread of the values: single noise bins scatter ~5.6 dB and
  their lower half sits several dB below the floor of averaged bins. So the
  black point moved with each zoom step.
- For absolute-dB rows (`m_panBinsAbsolute`, the ANAN), the row is first
  averaged in power into 256 equal groups across the whole band. Each group
  then holds ≥ ~15 FFT bins at any zoom, a spread of ≤ ~1 dB. The same trimmed
  mean is then taken over the groups.
- Flex tile rows keep the original estimator.

Files: gui/SpectrumWidget.cpp, ForkBuild.h.

## 237. Build 274: 3D hold until the level returns; 3D floor depth 0 (11.11)

**Report (operator, screenshot).** Every steady carrier is still drawn as
separate sticks in 3D. It is a display defect, not the signal. Also: remove
the height above the scale where the 3D noise shelf begins.

**Analysis.**
- A CPU re-enactment of the mesh (curtains, plus build 272's per-column walls,
  in painter's order) gives a solid band where the plain curtains break up.
  That holds when a carrier is in every row.
- The built shader (.qsb in the operator's build tree) does contain the walls.
- In the screenshot's pixels, sticks are 3 px apart and some are 2 px wide:
  stick plus one-row wall.
- So the carrier is missing from rows in between. One-row walls bridge one
  row; the rest stay dark. Why those rows lack the carrier is not yet found;
  candidates are the impulse blanker and the row cadence.

**Fix (dss_mesh.vert).** The far edge of a wall or lid now goes back to the
next older row (up to 8 rows) whose value at this column is within 6 dB of
this row's. A row that missed the level is drawn with the last one that had
it, until the level returns. If no row within 8 has it (a passing noise
spike), the hold stays one row, so short peaks do not become streaks. The
cost is up to 8 extra height-texture samples per far vertex.

**3D floor depth.** This is the "Display3DFloorDepth" setting: how far below
the noise-floor anchor the baseline sits. It was 6 dB, so the front noise
stood 6 dB above the scale.
- Default is now 0: SpectrumWidget members, the Display reset in
  MainWindow_Wiring.
- A one-time migration (`Fork3DFloorDepthZeroed`) sets stored values to 0.
- The dBm-strip drag can still raise it.

Files: resources/shaders/dss_mesh.vert, gui/SpectrumWidget.{h,cpp},
gui/MainWindow_Wiring.cpp, ForkBuild.h.

## 238. Build 275: 3D dips repaired in the 2D data before rendering (11.11)

**Request (operator).** Under the perspective slant the path lengthens and
there is not enough data to draw it all, so dips appear. Repair the dips by
averaging between samples while they are still plain 2D amplitude data.

**Change: `DssRenderer::repairRecentDips()`**, run on every pushed row (live
ring, the rows the 3D mesh samples).
- Rows checked: the `kRepairRows` (3) rows just behind the head.
- A column is a dip when it sits more than 3 dB below both its newer and its
  older neighbours, each taken as the highest of up to 3 rows on that side.
  It is lifted to the lower of the two. This is a closing in time; it fills a
  gap of up to 3 rows. Compared against the original values, so one repair
  does not feed another.
- Only where that level is a signal (≥ 10 dB above the row's median): the
  noise floor stays as it is.
- Rows of another frequency frame are never mixed (the check stops at the
  first frame change).
- SpectrumWidget's incremental GPU upload now re-sends `kRepairRows` extra
  rows behind the head with each push.
- Build 274's shader hold stays as a second line.

Files: gui/DssRenderer.{h,cpp}, gui/SpectrumWidget.cpp, ForkBuild.h.

## 239. Сборка 276 — 3D: настоящая причина «палок» и шумовая полка

**Палки вместо сплошной несущей.** Причина не в данных, а в порядке отрисовки. Сначала рисовались поверхности всех 96 рядов, затем поверх всех — контуры гребней всех рядов. На наклонных краях перспективы соседние ряды стоят в 1–2 пикселях друг от друга. Тёмные шумовые контуры дальних рядов ложились поверх полосы несущей ближнего ряда и закрашивали её полосами. Оставались видны только яркие контуры самой несущей в каждом ряду, то есть «палки». Поэтому стенки, удержание и ремонт провалов (сборки 271–275) ничего не меняли: их закрашивали сверху. Теперь ряды рисуются по одному от дальнего к ближнему: поверхность ряда, затем его контур. Ближний ряд закрывает то, что за ним.

**Шумовая полка.** Два источника:
1. Опорный уровень пола брался оценщиком «среднее нижней половины одиночных FFT-бинов». 3D-ряд же рисует каждый столбец как максимум из нескольких бинов, поэтому нарисованный шум стоял на несколько дБ выше пола. Чем больше бинов в столбце, тем выше. Теперь пол привязан к медиане самого нарисованного ряда (`DssRenderer::latestRowMedianDbm`), и шум ложится на базовую линию.
2. Нелинейная высота `m_dssZCurve = 0.70` намеренно приподнимала слабые уровни. Теперь она 1.0, высота линейная.

Файлы: SpectrumWidget.cpp/.h, DssRenderer.cpp/.h, ForkBuild.h (276).

## 240. Сборка 277 — число АЦП и фильтры по типу радио «как ведётся»

Жалоба: радио, которое определяется как Hermes, принудительно назначено как 7000DLE. Фильтры при смене диапазона RX1 применялись к обоим приёмникам, на панораме горела надпись WIDE.

**Причина WIDE.** Индикатор считался по правилу HL2: один входной тракт, все приёмники за одним байтом фильтров. АЦП он не учитывал вовсе. На платах с отдельным банком фильтров на каждый АЦП (7000DLE, 8000DLE, G2) два приёмника на разных АЦП показывались как WIDE, хотя каждый банк шёл за своим приёмником. Теперь правило то же, что применяет транспорт, и считается по профилю радио «как ведётся» (`Hl2Backend::ananProfile()`: профиль назначенного типа, иначе заявленной платы):
- полосовые банки (Orion2BandPass): по банку на каждый АЦП, у которого он есть; банк берёт первый приёмник на этом АЦП;
- классический Alex (100/100D/200D): ФВЧ и ФНЧ передатчика только перед АЦП1; у АЦП2 фильтров нет, поэтому приёмник там никогда не WIDE;
- SaturnBandPass: один выбор на оба АЦП.
WIDE ставится только на панораму того приёмника, которого банк не обслуживает, а не на все панорамы сразу.

**Один АЦП.** Платы Hermes, HermesII и G2E позволяли выбрать АЦП2. Транспорт для DDC ограничивал выбор, но правила фильтров, аттенюаторов, антенн и WIDE читали сырую настройку. Если у RX1 стоял «АЦП2», фильтры АЦП1 уходили к RX2. Теперь:
- бэкенд прижимает выбор к АЦП1 при `numAdcs < 2` и публикует `AnanUiState::numAdcs`;
- транспорт везде читает `effectiveAdc(rx)`;
- строка ADC1/ADC2 в панели ANT скрыта, если АЦП один;
- антенная подсказка берёт фактический АЦП.

По умолчанию RX2 на 2-АЦП платах остаётся на АЦП1, как раньше. Независимые фильтры у RX2 будут только после выбора ADC2 в панели ANT.

Файлы: Hl2Backend.cpp/.h, P2Transport.cpp/.h, AnanUiState.h, SpectrumOverlayMenu.cpp, ForkBuild.h (277).

## 241. Сборка 278 — WIDE в стиле кнопок ширины панорамы

Надпись WIDE теперь рисуется так же, как кнопки ширины панорамы (−, kHz, +):
- плашка 20 px высотой на их линии (y = 4), слева от группы;
- те же прозрачности: фон rgba(15,15,26,90), рамка rgba(144,160,176,110), скругление 2, текст rgba(200,216,232,150), 12 px bold;
- одинаково в GPU-оверлее и в CPU-пути (`SpectrumWidget::drawWideChip`).

Остальные индикаторы (preamp, RF gain, WNB, прогноз) идут левее WIDE, как и раньше.

О правиле из сборки 277: при двух приёмниках на одном АЦП и разных диапазонах WIDE получает только тот, за кем банк фильтров не идёт (второй). Первый приёмник на АЦП слышит через свой фильтр.

## 242. Сборка 279 — WIDE = банк фильтров в байпасе

Оператор: «WIDE означает байпас фильтров, чтобы работали оба приёмника».

До этой сборки банк фильтров АЦП шёл за первым приёмником на этом АЦП. Второй приёмник на другом диапазоне слушал через чужой фильтр. У классического Alex он мог оказаться даже за ФНЧ ниже своего диапазона.

Теперь `P2Transport::bankFor(adc)`:
- если приёмники одного АЦП хотят один фильтр, ставится он;
- если разные, банк этого АЦП переводится в Bypass;
- у классического Alex (100/100D/200D) ФНЧ передатчика в приёмном тракте ставится по самой высокой частоте среди приёмников на АЦП1, тогда он пропускает всех.

Правило работает и в P2 (High Priority), и в P1 (C&C: alexHpf / alex2Hpf / alexLpf). Банки по схемам те же, что в сборке 277: Orion2BandPass — АЦП1 и АЦП2 при втором банке; классика — только АЦП1; Saturn — общий.

WIDE (`Hl2Backend::publishWideState`) теперь горит на всех панорамах приёмников за банком в байпасе.

Файлы: P2Transport.cpp/.h, Hl2Backend.cpp, ForkBuild.h (279).

## 243. Сборка 280 — фильтры решаются только по АЦП

Оператор: любой приёмник может работать с любым АЦП, а фильтры физически привязаны к АЦП. Значит, логика смотрит только на то, какой АЦП в работе и на каком диапазоне, номер приёмника значения не имеет.

Проверка обоих протоколов: `P2Transport::bankFor(adc)` (сборка 279) уже решал банк по `effectiveAdc(i)`, в любом порядке приёмников. На плате с одним АЦП все приёмники попадают в банк 0. Правила:
- **один АЦП** (Hermes, 10E/100B, G2E): первый (единственный) банк; разные диапазоны — байпас и WIDE у обоих;
- **два АЦП, один банк** (100D/200D, классический Alex): АЦП1 по тем же правилам; у АЦП2 фильтров нет — он не настраивается и не бывает WIDE;
- **два АЦП, два банка** (7000DLE/8000DLE/G2): каждый АЦП отдельно, своя схема полосовых банков (`alexRxFilterFor`, свои края и биты; ФНЧ только по TX).

Исправлено в этой сборке:
1. Банк выбирался по частоте NCO (центру панорамы), а WIDE — по частоте слайса. Теперь оба берут настроенную частоту слайса (`ocRxHz`).
2. Смена настроенной частоты пересылала High Priority только при смене диапазона OC. Теперь пересылает и при изменении банков.
3. Смена АЦП у приёмника пересылала только DDC Specific. Теперь пересылает и High Priority (банки).
4. P1, классический Alex в байпасе: ФНЧ приёмного тракта самый широкий, как в P2 (deskHPSDR).

Файлы: P2Transport.cpp/.h, Hl2Backend.cpp, ForkBuild.h (280).

## 244. Сборка 281 — текст кнопок быстрой памяти крупнее

Кнопки быстрой памяти на панели слева: шрифт 11 → 12 px (bold). Размер кнопок 72×23, зазоры и цвета не менялись.

Файлы: BandStackPanel.cpp, ForkBuild.h (281).

## 245. Сборка 282 — 3D-водопад заново

Оператор: «по сути это не панорама, а водопад; вернёмся на исходную и сделаем заново: те же данные и настройки водопада, те же уровни, цвет и контраст, без искажения перспективы, ровно вперёд, уровень — в высоту, наклон как сейчас».

**Откат.** Все 3D-правки сборок 270–276 сняты обратным применением их патчей:
- крышки, стенки, удержание (271–274);
- ремонт провалов (275);
- поштучная отрисовка и привязка пола к медиане (276);
- цвет гребня (270, frag).

`dss_mesh.vert/.frag` и `DssRenderer.cpp/.h` снова совпадают с исходником v26.10.4 (ea16431), поверх них только правки ниже. Остались не 3D-части тех сборок: контекстное меню (270), автоуровень чёрного водопада по всей полосе (273), глубина пола 0 (274).

**Новый 3D-водопад.** Данные — те же строки, что уже шли в 3D: строки нативного тайла водопада, а при их отсутствии — FFT. Окно уровней берётся у 2D-водопада:
- `WaterfallLevelMap::window()` — новая функция, `level()` теперь на ней: точка чёрного и ширина в единицах тайла, с автоуровнем, смещением и ручным Black Level, шириной по Gain;
- для FFT-строк окно `dbmToWaterfallLevel`.

`SpectrumWidget::dssWaterfallWindow()` отдаёт это окно:
- `dssFloorDbm()` — точка чёрного (базовая линия);
- `dssSpanDb()` — ширина (полная высота);
- кривая высоты `dssHeightCurve()` = 1, высота линейна по уровню.

Цвет: LUT шейдера = `waterfallLevelToRgb` (схема и яркость водопада) по уровню 0..1, диапазон цвета = всё окно. Столбец окрашен одним цветом от гребня до низа (было: низ ×0.6). Белые контуры гребней не рисуются.

Геометрия:
- `kBackWidthFrac = 1.0` — без сужения, ряды уходят ровно назад;
- `kHaze = 0` — цвета не тускнеют с глубиной;
- наклон (`kDepthSpanFrac` 0.58) и высота (`kFrontMaxRidgeFrac` 0.46) прежние;
- `kMeshCols` = 5/3 × kCols + 1 (как было).

KiwiSDR остаётся на своём прежнем 3D-отображении. Тест `tests/dss_renderer_test.cpp` рассчитан на перспективу 0.60 и может потребовать правки.

Файлы: dss_mesh.vert/.frag, DssRenderer.cpp/.h, SpectrumWidget.cpp/.h, WaterfallLevelMap.h, ForkBuild.h (282).

## 246. Сборка 283 — разрешение 3D-водопада по горизонтали

Оператор: «горизонтальное разрешение в разы хуже, вместо пиков размазанные полосы».

**Причина.** `resampledRawRow` (бины → столбцы 3D) брал максимум по [floor(c·step), ceil((c+1)·step)) — на один соседний бин больше с каждой стороны:
- при увеличении (вид приближен, бинов меньше, чем столбцов) каждый бин растягивался на 2–3 бина плоской «полкой»;
- при уменьшении добавлялся лишний сосед.

Плюс столбцов было всего 768, а у 2D-водопада около 1440 px.

**Исправлено:**
- бинов меньше, чем столбцов — линейная интерполяция между центрами бинов, как у водопада;
- бинов больше — максимум ровно тех бинов, что попадают в столбец (пик не теряется и не расширяется);
- `kCols` 768 → 1280, `kMeshCols` = 5/4 · kCols + 1 = 1601.

Файлы: DssRenderer.cpp/.h, ForkBuild.h (283).

## 247. Сборка 284 — 3D-водопад с наклоном вправо

Оператор: «наклони все линии вправо на наш прежний угол перспективы».

Косоугольная проекция: каждый ряд с глубиной сдвигается вправо, к заднему ряду на `kDepthSkew = 0.20` ширины. Это наклон левого края прежней перспективы 0.60: 0.5 × (1 − 0.60). Столбцы частот остаются параллельными и все наклонены вправо, ширина рядов не меняется.

Реализация:
- `dss_mesh.vert`: `plotX += depthSkew · geometryV`, в основной поверхности и в `ridgeNdcAt`;
- `depthSkew` занимает первое из двух полей выравнивания std140 перед `bgFill`, раскладка UBO та же; `dss_mesh.frag` объявляет те же поля;
- CPU-путь (`DssRenderer::rebuild`) и `projectPerspective` / `projectSurface` (маркеры) сдвигаются так же.

Задний левый угол открывается. Его закрывают расширенные ряды из данных тайла за краями вида: `kMaxRowSpanFactor = 1 + 2·skew = 1.4`, `wedgeFreeDepth` пересчитан под сдвиг. При полностью отдалённой панораме данных за краем нет, и там остаётся растушёванный клин.

`kMeshCols = kCols · 1.4 + 1 = 1793`, чтобы на экране не было реже текселей.

Файлы: dss_mesh.vert/.frag, DssRenderer.cpp/.h, SpectrumWidget.cpp, ForkBuild.h (284).

## 248. Сборка 285 — плоские бруски и коробки между рядами

Оператор: после наклона «появились зазоры и страшное мельтешение; уже нарисованные палки при перемещении мельтешат амплитудой, а должны оставаться такими же по всей траектории».

**Причина.**
- Пик в один бин был треугольником с острой вершиной шириной в столбец сетки. Ряд при плавной прокрутке едет вбок (наклон) на доли пикселя, и вершина то попадает в пиксель, то нет — амплитуда мельтешит.
- Соседние ряды при наклоне стоят в 3–4 px друг от друга вбок, поэтому узкие пики соседних рядов не касаются — зазоры.

**Сделано** (`dss_mesh.vert` и сетка в `SpectrumWidget::initialize`):
- **Плоские бруски.** Обе верхние вершины столбца сетки берут значение в центре столбца (у правой вершины тег +100). Пик — брусок шириной около 1.4 px с плоской вершиной, его покрытие при субпиксельном сдвиге стабильно.
- **Коробка до следующего ряда.** Каждый столбец ряда продолжен назад:
  - крышка: верх столбца, наклонно до уровня следующего ряда на его глубине (теги 4/6);
  - правая стенка — сторона, которую показывает косоугольный вид (теги 4/5/6/7).
  Они рисуются перед фронтами ряда в слое overlay, у заднего ряда не рисуются. Значения не интерполируются: ближний край — точный уровень ряда, дальний — точный уровень следующего.
- `dssFillVerticesPerRow` = (kMeshCols − 1) · 6 · 4.
- `kCols` 1280 → 1024 (столбец около 1.4 px на типичном графике), `kMeshCols` = 1024 · 1.4 + 1 = 1434.

Файлы: dss_mesh.vert, DssRenderer.h, SpectrumWidget.cpp, ForkBuild.h (285).

## 249. Сборка 286 — бруски 3D-водопада привязаны к данным, а не к экрану

Оператор (стабильная несущая): «при перемещении или зуме панорамы уже отрисованные элементы искажаются, волнообразные изменения цвета в такт движению; отрисованное должно оставаться одинаковым».

**Причина.** Столбцы сетки были привязаны к экрану. Каждый кадр каждый столбец брал ближайший к себе тексель своего ряда, пересчитывая позицию через кадр ряда (`rowFrames`). При сдвиге или зуме менялось, какой тексель достаётся какому столбцу, — отсюда волны.

**Сделано.** Каждый брусок привязан к данным: брусок = один тексель своего ряда. Он стоит на частоте этого текселя, пересчитанной через кадр ряда (`texelViewUnit` — обратная к отображению `effectiveDbmAt`). При сдвиге брусок едет, при зуме растягивается, но его значение и границы не меняются.

Сетка: `kMeshCols = kCols/2 + kCols + 1`:
- первые kCols/2 брусков — дополнительный тайл ряда, по 2 текселя, их максимум; показываются только вне собственного кадра ряда (то, что ряд не захватил сам);
- затем kCols брусков — собственные тексели ряда.
- Нет покрытия — нет бруска.

Крышки и стенки доходят до следующего ряда на той же частоте (`effectiveDbmAt` по частоте). Ленточные контуры (KiwiSDR) остались с прежней привязкой к экрану.

Файлы: dss_mesh.vert, DssRenderer.h, ForkBuild.h (286).

## 250. Сборка 287 — ряды 3D-водопада больше не переписываются при сдвиге и зуме

Оператор: после сборки 286 «увы, идут искажения».

**Настоящая причина** — не в отрисовке. При каждом шаге сдвига или зума панорамы `reprojectWaterfallStreams` вызывал `DssRenderer::reprojectFrequencyFrame`. Он переписывал каждый видимый 3D-ряд: перечитывал из сохранённой истории, заново интерполировал значения на новую сетку частот (`reprojectRow`) и ставил ряду новый кадр. Данные уже нарисованных рядов менялись на каждом шаге движения.

**Исправлено.** Для 3D-водопада на GPU (не KiwiSDR, сетка готова) ряды не трогаются. Каждый хранит свой кадр, а шейдер (сборка 286) ставит каждый тексель на его частоту. CPU-путь без сетки и KiwiSDR пересчитывают ряды по-прежнему. Строка под `#ifdef AETHER_GPU_SPECTRUM`.

Заметка: errcheck в этой среде собирается без `AETHER_GPU_SPECTRUM` (нет заголовков QRhi), поэтому GPU-ветки им не проверяются. Их проверяет только сборка у оператора.

Файлы: SpectrumWidget.cpp, ForkBuild.h (287).

## 251. Сборка 288 — захваченные ряды хранятся точно по бинам

Оператор: после 287 «искажений меньше, но они всё равно есть и большие».

**Причина.** Ряд тайла — это вырезка захваченной полосы по краям вида: при каждом шаге сдвига или зума меняются её первый бин и число бинов. Вырезка пересчитывалась на kCols текселей по своей ширине (с 283 — интерполяцией, если бинов меньше текселей). Устойчивая несущая в каждом таком ряду попадала между текселями на разную долю, и её записанный пик менялся от ряда к ряду. В дБ интерполяция на четверть бина к шуму съедает большую часть пика.

**Исправлено** (`exactCapturedRow`, для рядов с `preserveInput`):
- тексель t = бины [t·g, (t+1)·g), их максимум, без интерполяции; g = 1, пока бинов не больше текселей, иначе наименьшая группа, которая помещается;
- тексели за данными без покрытия;
- кадр ряда переопределён: тексель 0 — на центре первой группы, тексель kCols−1 — на центре kCols-й группы. По этому кадру `dss_mesh.vert` ставит тексели.

Шейдер: дополнительный тайл прячется там, где собственный тексель ряда на этой частоте покрыт, а не по всей ширине кадра (кадр теперь шире данных).

Файлы: DssRenderer.cpp, dss_mesh.vert, ForkBuild.h (288).

## 252. Сборка 289 — плавный переход цвета по несущей в 3D-водопаде

Оператор (скриншот): на 2D-водопаде по несущей видна целая радуга, около 8 цветов, а в 3D только 2 — «вот и плохое горизонтальное разрешение».

**Причина.** 2D-водопад интерполирует уровень между бинами линейно для каждого пикселя. В 3D каждый тексель был плоским бруском (сборка 285), и несущая шириной 3 бина при зуме выходила 2–3 блоками.

**Сделано** (`dss_mesh.vert`). Если тексели ряда на экране отстоят друг от друга на 2 px и больше, брусок становится отрезком от центра текселя t до центра текселя t+1:
- каждый конец — на точном уровне и цвете своего текселя, между ними линейный переход, как у водопада;
- отрезок рисуется, только если покрыты оба текселя;
- крышки и стенки берут следующий ряд на частоте своей вершины;
- геометрия по-прежнему привязана к данным, а не к экрану.

Более узкие тексели (отдалённая панорама) остаются плоскими брусками, они устойчивы к субпиксельному сдвигу. Дополнительный тайл — всегда бруски.

Файлы: dss_mesh.vert, ForkBuild.h (289).

## 253. Сборка 290 — перспектива с точкой схода справа

Оператор: «сделаем перспективу переменной для каждой частоты, как это и должно быть».

Ряды снова сужаются с глубиной: `kBackWidthFrac` 1.00 → 0.60, как до сборки 282. Наклон вправо (`kDepthSkew` 0.20) сохранён. Вместе они дают одну точку схода для всех линий частот — справа, на продолжении правого края графика:
- правый край уходит назад вертикально;
- центр наклонён на 0.20 ширины к заднему ряду;
- левый край — на 0.40.

Высота гребней уменьшается с глубиной вместе с шириной (так шейдер делал всегда).

Зазоры при сильном наклоне слева закрывают крышки и правые стенки (сборка 285). Видимая сторона всегда правая, потому что влево линии не наклоняются нигде. Положение брусков по данным (286–289) не меняется.

Файлы: DssRenderer.h, ForkBuild.h (290).

## 254. Сборка 291 — точка схода по центру

Оператор: «всё правильно, но центр должен быть с нулевой перспективой, а не правая часть».

- `kDepthSkew` 0.20 → 0: точка схода снова по центру. Центр уходит прямо назад, левая половина наклонена вправо, правая влево. Сужение 0.60 сохранено.
- Боковые стенки (теги 8/9/10/11, были 4–7 только справа) шейдер ставит на ту сторону бруска, которую показывает перспектива: справа от линии схода f = 0.5 + skew/(1 − backWidth) — левую, слева — правую. В линейном режиме (289) значение стенки берётся с её конца отрезка.
- `wedgeFreeDepth` не делит на ноль при skew = 0.

Файлы: dss_mesh.vert, DssRenderer.h, SpectrumWidget.cpp, ForkBuild.h (291).

## 255. Сборка 292 — у 3D-водопада свои ползунки, отвязаны от водопада

Оператор: «привяжи ползунки, выделенные для 3D, к параметрам и отвяжи параметры от водопада».

Окно 3D (`dssWaterfallWindow`) больше не берёт Black Level / Gain / яркость водопада:
- **3D Floor** (0..24 дБ, по умолчанию 0): базовая линия = собственный шум рядов минус Floor. Шум — медиана покрытых текселей нового ряда (`DssRenderer::latestRowMedianDbm`), сглаженная (α 0.05, скачок больше 18 дБ принимается сразу), в единицах самих рядов.
- **3D Gain** (0..100): ширина окна — высота и верхний цвет; закон Gain водопада: 80 дБ при 0 … 10 дБ при 100.
- **3D Height** (бывший «3D Span»: бруски стоят по частотам, нахлёст больше не нужен): высота гребня 0.10..0.80 высоты графика, 51 = прежние 0.46. Один раз ставится 51 (`Fork3DHeightMigrated`). В шейдер идёт через `frontMaxRidgeFrac`, в маркеры — через `projectSurface(..., ridgeFrac)`.

Цвет: только цветовая схема, линейно по уровню (`dss3dLevelToRgb`), без яркости водопада. Схема общая с водопадом. KiwiSDR — по-прежнему своё 3D.

Файлы: DssRenderer.cpp/.h, SpectrumWidget.cpp/.h, SpectrumOverlayMenu.cpp, ForkBuild.h (292).

## 256. Сборка 293 — 3D-ряды в полном разрешении на всю видимую перспективой ширину

Оператор: «то, что формируется за пределами экрана и видно после перспективы, рисуется с гораздо худшим разрешением; а FFT считается одинаково по всей ширине принимаемого спектра».

**Причина.** 3D-ряд вырезался из полного тайла (вся принятая полоса, до 8192 бинов) строго по краям вида. Всё за краями шло из дополнительного ряда — того же тайла, ужатого до kCols текселей и ещё вдвое (бруски по 2 текселя). При зуме это в разы грубее, а дальние ряды из-за сужения показывают до 1/0.60 ширины вида.

**Сделано:**
- вырезка для 3D — 1/kBackWidthFrac (≈1.67) ширины вида по центру, обрезанная по тайлу; каждый бин сохраняется точно (`exactCapturedRow`, до kCols, иначе максимум по группам);
- `kCols` 1024 → 2048; `kMeshCols` = kCols + 1 — только собственные бруски ряда, бруски дополнительного тайла убраны (`supplementalBars` = 0);
- на полностью отдалённой панораме ряд = весь тайл: 8192 бина группами по 4 в 2048 текселей.

Вершин: 2048 · 24 · 96 ≈ 4.7 млн (было 1536 брусков).

Файлы: SpectrumWidget.cpp, DssRenderer.h, dss_mesh.vert, ForkBuild.h (293).

## 257. Сборка 294 — меню панорамы без зазоров

Оператор (скриншоты): кнопки меню панорамы (←, +RX, WB, Band, ANT, Display, Memory, DAX) не менялись — цвет, прозрачность и размер те же, что в коде с версии 26.10.4. Мешает только зазор между ними, сквозь который просвечивает панорама.

`SpectrumOverlayMenu::updateLayout`: зазор 2 → 0 px. Кнопки стоят вплотную, рамки соприкасаются.

Файлы: SpectrumOverlayMenu.cpp, ForkBuild.h (294).

## 258. Сборка 295 — AUTO ATT по образцу MicronSDR, без захвата собственного шума

Оператор: «автоматика аттенюатора и АРУ, основанная на захвате собственной шумовой полки, провалилась — пользователи не поняли, что это и зачем. База — показания S-метра, откалиброванного пользователем или нет; работать должно в обоих случаях. Посмотри, как сделано в MicronSDR, и сделай так же».

**MicronSDR** (`micronsdr.cpp` autoAttMeasure / autoAttStep, `PanoramOpt` Auto ATT):
- полка (20-й перцентиль видимого спектра, на АЦП) держится на абсолютной цели, по умолчанию −120 дБ, гистерезис 3 дБ;
- окна ≥200 мс кадров с текущим ATT; шаг ≤6 дБ, до цели ±0.5 дБ;
- шаг на увеличение ослабления, который опустил полку меньше чем на 30 % шага, означает собственный шум. Шаг откатывается, и дальнейшее ослабление блокируется, пока полка не поднимется на 3 дБ или не окажется ниже цели.
- Auto RF (АРУ) работает от той же полки, без захвата.

**У нас** (`Hl2Backend::ananAutoAttStep`) — то же самое:
- полка: кадровая (`ananNoiseLineStep`, медиана/ln2 по сырому кадру), дБм / 500 Гц на АЦП с калибровкой S-метра (`ananDisplayCalDb`). Калибровка только сдвигает шкалу цели;
- после каждого шага `kAnanAttSettleMs` (300 мс) кадров пропускается; на передаче и 500 мс после неё не работает;
- цель на приёмник — `AnanAutoAttNfTargetDbm[Rx2]`, по умолчанию −120, диапазон −150..−60.

ANT-панель: ползунок «NF+» (запас над своим шумом) стал «NF» — абсолютная цель в дБм, новая подсказка. Setup → Receive: кнопка Capture и предупреждение удалены (остались линия шума и тест перегрузки). Напоминание о захвате при подключении отключено. На панораме больше нет отметки «own».

Auto AGC не менялся: он уже работал от полки плюс смещение (закон Thetis), без собственного шума.

Файлы: Hl2Backend.cpp/.h, AnanUiState.h, SpectrumOverlayMenu.cpp, RadioSetupDialog.cpp, MainWindow.cpp, ForkBuild.h (295).

## 259. Сборка 296 — фильтр приёмника появляется во время перетаскивания панорамы

Оператор: «при перетягивании панорамы, если фильтр приёмника был вне экрана, он не отрисовывается, пока не отпустишь кнопку».

**Причина.** Во время перетаскивания слой, привязанный к частоте (слайсы, фильтры, споты, бэндплан), — это текстура, нарисованная в начале жеста и только сдвигаемая преобразованием предпросмотра (`markOverlayDirty` в это время ничего не перерисовывает). То, что было за экраном в начале жеста, в текстуре отсутствует.

**Исправлено** (`renderGpuFrame`). Если видимая сейчас часть фильтра какого-либо слайса выходит за кадр, для которого нарисована текстура, слой частот перерисовывается в текущем кадре (`m_centerMhz` — уже цель жеста), и база преобразования переносится на него — до расчёта преобразования, в том же кадре. Перерисовывается только этот слой и только пока фильтр пересекает край.

Файлы: SpectrumWidget.cpp, ForkBuild.h (296).

## 260. Сборка 297 — шумовая полка: 20-й процентиль видимого спектра (как в MicronSDR)

Оператор: «медиана не может считаться шумовой полкой — в спектре полно мощных сигналов; посмотри в MicronSDR, как правильно».

**MicronSDR** (`Panarama.cpp`, после сглаживания кадра): 20-й процентиль видимого сглаженного спектра, не меньше 1024 бинов.

**У нас было:** медиана мощности по всей принятой полосе (средние 80 % DDC) / ln 2. Она ломается, когда сигналы занимают половину полосы, а во всей полосе их больше, чем на экране.

**Стало** (`ananNoiseLineStep`, сырой кадр):
- бины на экране (`first` / `keep` из того же кадра), при меньше 1024 — расширение до 1024 вокруг центра, в пределах средних 80 % DDC;
- 20-й процентиль мощности, делённый на −ln 0.8 = 0.223 (6.5 дБ). Для бина чистого шума (экспоненциальное распределение) это точно его среднее; проверено на 10⁶ отсчётах — отношение 1.003. Так линия по-прежнему совпадает с S-метром на тихом участке.

От этой полки работают линия на панораме, AUTO ATT и Auto AGC.

Файлы: Hl2Backend.cpp/.h, ForkBuild.h (297).

## 261. Сборка 298 — полка из нарисованного спектра, без поправки (точно как MicronSDR)

Оператор: «линия рисуется выше шума, и её значение совпадает с S-метром, а у того фильтр 2.7 кГц — это неправильно».

Лог 19:21 (сборка 297): линия −99 дБм / 500 Гц, S-метр в 2800 Гц −96…−98. Разница 2–3 дБ, а для 500 Гц против 2.8 кГц должна быть около 7.5 дБ.

**Ошибка сборки 297:** поправка процентиля к средней мощности шума (+6.5 дБ) ставила линию выше шума, каким он нарисован и каким его читает S-метр.

**MicronSDR:** 20-й процентиль сглаженного (нарисованного) спектра, без поправок.

**Стало:**
- в `ananNoiseLineStep` передаются бины после усреднения FFT (`bins`, как на панораме), а не сырой кадр;
- 20-й процентиль видимых бинов (не меньше 1024) — как есть.

Привязка к 500 Гц и калибровка S-метра прежние. AUTO ATT и Auto AGC работают от этой полки, она ниже прежней на несколько дБ — цель NF можно подправить.

Файлы: Hl2Backend.cpp, ForkBuild.h (298).

## 262. Сборка 299 — жёлтая линия Auto AGC, перетаскивается мышью; «Signal Noise Floor»

Оператор: переименовать галку линии шума так, чтобы было ясно, что это шум входного сигнала, а не собственный шум АЦП (выбран вариант 4); добавить такую же жёлтую линию для смещения АРУ со своей галкой; линию можно двигать мышью, значение синхронизируется с ползунком NF+ в АРУ.

**Setup → Receive:**
- группа «Receiver Noise Floor» → **«Signal Noise Floor»**;
- галка «Show the noise floor line on the panadapter» → **«Show the signal noise floor at the ADC input»** (с подсказкой);
- новая галка **«Show the Auto AGC level (noise floor + NF+)»** — `AnanUiState::showAgcLine`, ключ `AnanShowAgcLine`, по умолчанию включена.

**Панорама** (`SpectrumWidget`, только 2D, пока Auto AGC включён у приёмника этой панорамы):
- жёлтый пунктир на уровне полки + NF+ с подписью «AGC N dBm/500 Hz (NF+M)» (`ananAgcLineY`, рисуется в `drawAnanNoiseFloor`);
- захват ±4 px левее шкалы дБм, курсор SizeVer; при перетаскивании NF+ = round(уровень под мышью − полка), в пределах −20..+60;
- во время перетаскивания меняется `AnanUiState::agcNfOffsetDb` (бэкенд читает его раз в 500 мс, ползунки NF+ в апплете RX и VFO следят за ним по своим таймерам);
- при отпускании значение сохраняется (`setAgcAuto`);
- состояние линии добавлено в опрос перерисовки слоя (`m_ananOverlaySig`).

Файлы: SpectrumWidget.cpp/.h, RadioSetupDialog.cpp, AnanUiState.h, ForkBuild.h (299).

## 263. Сборка 300 — над жёлтой линией АРУ не рисуется крест

Оператор: «указатель-стрелка ставится, но поверх него всё равно рисуется крест».

**Причина.** `renderGpuFrame` каждый кадр сам проверяет курсор под мышью и, если `spectrumDefaultsToCrosshairAt()`, ставит крест. Поэтому двойная стрелка над линией (её ставит `mouseMoveEvent`) сразу заменялась.

**Исправлено.** `spectrumDefaultsToCrosshairAt()` возвращает false в ±4 px от линии Auto AGC (`ananAgcLineY`), и кадровая проверка курсор там не трогает.

Файлы: SpectrumWidget.cpp, ForkBuild.h (300).

## 264. Сборка 301 — жёлтая линия: гейт АРУ отдельно от цели автоматики

Оператор: «шум в динамиках заметно падает только когда жёлтая линия на +17 дБ выше шума» — затем: «это же AGC Gate, он и должен быть так высоко… это смесь гейта и цели регулировки, их нужно разделить».

**Почему гейт выше шума — это не ошибка.** Стенд на WDSP (`scratchpad/agcknee/k.cpp`: комплексный белый шум известной мощности, 2.8 кГц, Slow, slope 35, закон Auto AGC): пока порог ниже ~+10 дБ над средней мощностью шума, АРУ сжимает шум до полной громкости; шум начинает падать с +12…+14 дБ и дальше дБ в дБ. Детектор wcpAGC следит за пиками огибающей (атака 2 мс, спад 500 мс), а пики шума в 2.8 кГц на таком интервале на ~8–10 дБ выше среднего. Плюс зелёная линия — 20-й процентиль (ниже средней мощности) и ENBW Ханнинга 1.5 бина (+1.8 дБ). Итого ~17 дБ. Код не трогали.

**Разделено.**
- Гейт — абсолютный уровень, который сейчас держит WDSP. `Hl2Backend::ananPublishAgcGate(rx)` (каждый шаг шумовой линии) обращает закон `ananAutoAgcStep` для фактического усиления `agcCeilingFor(r)` (Auto или ручной AGC-T, после ограничения 0..120 дБ): `gate = K − gain`, где K = out_target − var_gain − 10·log10(BW·1024/rate) + offsetDb + kToneGainDb + RBW→500 Гц + калибровка S-метра. Публикуется в `AnanUiState::agcGateDbm / agcGateAtMs / agcGatePlusGainDb (K) / agcGainAtT0Db`. В Auto совпадает с «полка + NF+», пока усиление не упёрлось в границу.
- Цель — NF+, ставится только ползунком (пока).

**Жёлтая линия** = гейт, в ручном и в авто режиме, пока АРУ включена. Подпись «AGC Gate −NN dBm/500 Hz» и «(Auto)» в авто режиме. В Auto её нельзя тащить (нет стрелки, крест как обычно). С выключенным Auto её можно тащить: это ползунок AGC-T — положение переводится в усиление (K − dBm), усиление в AGC-T (обратный `Hl2DbReference::agcCeilingDb`, 0.6 дБ/ед.), новый сигнал `SpectrumWidget::ananAgcThresholdRequested(int)` → `MainWindow_Wiring` ставит `setAgcThreshold` на слайс этой панорамы (активный, если он на ней). Пока тащат, линия рисуется по отправленному AGC-T. Галочка в Setup: «Show the AGC gate».

Файлы: Hl2Backend.cpp/.h, AnanUiState.h, SpectrumWidget.cpp/.h, MainWindow_Wiring.cpp, RadioSetupDialog.cpp, ForkBuild.h (301).

## 265. Сборка 302 — линии включены по умолчанию; до подключения радио интерфейс openHPSDR, а не Flex

Оператор: «сделай, чтобы линии были активированы по умолчанию. Также при старте программы, пока радио ещё не подключено, на экране видны несвойственные нашему радио кнопки и элементы — для Флекс. Исправь, чтобы сразу показывались кнопки и элементы для openHPSDR».

**Линии.** Умолчание и так было «включено», но сохранённое ранее «выключено» оставалось. `AnanUiState`: один раз (ключ `AnanLinesOnMigrated302`) обе линии — шумовая полка и гейт АРУ — включаются; галочки в Setup > Receive выключают их снова насовсем.

**Причина элементов Flex.** Все ворота видимости устроены как `!connected || caps.x`: пока радио не подключено, показывается всё, что умеет хоть какое-то радио, — то есть Flex (Waveforms, MultiFlex, DAX, GPS, PROC, FlexControl, сжатие аудио и т.д.). `RadioModel` при этом отвечал «есть» на все has…() и брал расширенный DSP из таблицы моделей Flex.

**Исправлено.** Форк работает только с openHPSDR, поэтому до подключения интерфейс читает то, что объявил бы ещё не подключённый openHPSDR-бэкенд:
- `Hl2Backend::capabilities()` разделён на `buildCapabilities(CapsInputs)` (зависимости записи: dB-референс, модель платы, потолок приёмников, IQ-скорости, диапазон настройки, разрешение TX) и статический `presumedCapabilities()` — те же входы, что ставит конструктор (100D, все скорости, без подключения), без создания бэкенда.
- `RadioModel::uiCapabilities()` — записи подключённого бэкенда, иначе `presumedCapabilities()`. Через неё идут hasRadioSideDsp / LMS / APF / автоблэк / CW-кейер / CW-макросы / голосовой кейер / DAX / WPM / расширенный DSP, вместо «есть» и таблицы Flex.
- `ControlAvailabilityRegistry::applyOne`: без подключения контрол таков, каким он был бы на openHPSDR (`available(true, uiCapabilities())`), а не «доступен».
- `MainWindow::applyCapabilitiesToUi(live, …)`: без подключения ворота читают `uiCapabilities()` как подключённые; `live` оставлен для фактов сессии (производитель, состояние мик-метра, сброс последних показаний напряжения).
- `RadioSetupDialog::updateRadioCapabilityVisibility`, FM-репитер в `RxApplet` / `VfoWidget` — то же.

Файлы: Hl2Backend.cpp/.h, RadioModel.cpp/.h, ControlAvailabilityRegistry.cpp, MainWindow.cpp, RadioSetupDialog.cpp, RxApplet.cpp, VfoWidget.cpp, AnanUiState.h, ForkBuild.h (302).

## 266. Релиз build 302 подготовлен (11.11.2026)

- docs/release-notes-build-302.md: сборки 266–302 по темам (старт, тип радио, фильтры по АЦП, AUTO ATT и АРУ, TCI, панорама и водопад), с тем, что не проверено на радио. README.md: раздел «Build 265» заменён на «Build 302», build 265 добавлен в «Earlier notes».
- Архив AetherSDR-ANAN-build302-win64-portable.zip: архив build 265 с exe сборки 302 (build\Desktop_Qt_6_12_0_MSVC2022_64bit_Release\AetherSDR.exe = Desktop\AetherSDR-Release\AetherSDR.exe, 11.11 20:36 местного, содержит «AnanLinesOnMigrated302», «AGC Gate %1 dBm/500 Hz»), README-build302.md вместо README-build265.md; верхняя папка AetherSDR-ANAN-build302; 90 записей, 154450301 байт, SHA-256 5b8446762c36dbacb2ce4b2d5df2f1a664893e23e3186a649adc75587afdc361. Сохранён в Desktop\AetherSDR-Release.
- Релиз (тег build-302 на main) создаёт оператор в веб-интерфейсе GitHub; пуш тега запускает сборку AppImage.

## 267. Сборка 303 — DRM: АРУ выключена, I/Q в правильной ориентации

Оператор: «пытаюсь принимать DRM-станцию, пришлось поставить галочку Flip Spectrum, SDC остаётся красным и звука нет».

Лог 21:04: в режиме DRM на приёмнике работала АРУ (Auto AGC: «filter 10000 Hz -> AGC gain 75.1 dB», режим med).

**Причина 1 — АРУ.** DRM — это OFDM: данные несёт амплитуда QAM-символов, а огибающая похожа на шум. АРУ WDSP с атакой 2 мс отслеживала эту огибающую и модулировала амплитуду символов. FAC (4-QAM) это переживает, а SDC и MSC (16/64-QAM) — нет. У DrmEngine есть своё медленное входное усиление (2 с), поэтому АРУ здесь не нужна. `WdspChannel::effectiveAgcMode()`: в режиме DRM АРУ выключена, работает только фиксированное усиление — в `setAgc`, при `open` и при переходе в DRM и из него (`setMode`). Режим и порог АРУ оператора не трогаются и возвращаются при выходе из DRM.

**Причина 2 — Flip.** Цепочка работает на «проводе» HPSDR, где сигнал выше NCO лежит на отрицательной частоте (демодуляторы под это настроены, спектр берёт сопряжённое — как TCI I/Q с 266). Выход DRM — это I/Q как есть, поэтому Dream получал зеркальный спектр. `Hl2RxDsp::applyDrm` теперь сопрягает I/Q (Q = −Q) перед Dream: нормальная ориентация — Flip Spectrum выключен.

Файлы: WdspChannel.cpp/.h, Hl2RxDsp.cpp, ForkBuild.h (303).

## 268. Сборка 304 — место под название DRM-станции зарезервировано

Оператор: «иногда показывается название станции TDF-DRM, и окно расширяется вниз туда-сюда. Нужно сразу дать место для названия, чтобы окно не дёргалось».

**Причина.** `DrmApplet::refresh` прятал строку названия (`m_label->setVisible(!station.isEmpty())`), пока метка сервиса не принята. При неустойчивом SDC метка то появлялась, то пропадала, и апплет менял высоту.

**Исправлено.** Строка названия есть всегда: одна строка высотой 22 px (шрифт 14 px bold), без переноса (метки DRM — не длиннее 16 символов), пустая, пока метки нет.

Файлы: DrmApplet.cpp, ForkBuild.h (304).

## 269. Сборка 305 — окно DRM не меняет размер при смене состояния декодера

Оператор: «SNR около 6–7, приёма нет; когда загорается FAC зелёным, окно перерисовывается и дёргается, это раздражает».

**Причина.** В `DrmApplet::refresh` (каждые 250 мс) строки появлялись и исчезали и меняли ширину по тексту:
- строка сервиса, текстовое сообщение и подсказка — `setVisible(!text.isEmpty())`;
- строка режима («Mode B · 10 kHz · 64-QAM · …») и SNR тянули ширину по тексту;
- индикаторы получали style sheet каждый раз заново.

С FAC декодер выдаёт режим и подсказку, и раскладка менялась.

**Исправлено.** У каждой строки постоянная высота, текст на размер не влияет (`QSizePolicy::Ignored` по горизонтали):
- SNR и режим — 16 px;
- сервис — одна строка, 16 px;
- текстовое сообщение — 3 строки, 46 px (сообщения DRM до 128 символов);
- подсказка — 2 строки, 30 px;
- индикаторы — 16 px, style sheet ставится только при смене состояния.

**SNR 6–7 дБ** — это уровень сигнала станции на входе декодера, а не ошибка окна. Для SDC и MSC (16/64-QAM) нужно примерно 10–15 дБ и больше; при 6–7 дБ устойчиво декодируется только FAC.

Файлы: DrmApplet.cpp, ForkBuild.h (305).

## 270. Сборка 306 — звук приёмника моно: баланс лево/право больше не зависит от USB/LSB

Оператор: «при переключении USB–LSB я явственно услышал разный баланс лево-право в динамиках компьютера».

**Причина.** Патч-панель RXA в WDSP создаётся бинауральной (`create_panel(..., copy 0)`): левый канал = I, правый = Q комплексного выхода демодулятора. Для SSB Q — это сдвинутая на 90° пара звука, и её знак меняется с боковой полосой, поэтому звуковой образ смещался между колонками. Thetis и pihpsdr по умолчанию держат Binaural выключенным (I в оба канала). Мы панель не настраивали.

**Стенд** (`scratchpad/agcknee/p.cpp`, WDSP, тон в полосе SSB): при binaural корреляция L/R ≈ 0.0 (LSB −0.03, USB −0.02); при mono 1.00 в обеих полосах.

**Исправлено.** `applyRxPanel()` вызывается при `open` и в `setMode`: `SetRXAPanelBinaural(0)`, то есть моно, во всех режимах, кроме тех, где оба канала несут сигнал: DRM (отфильтрованный I/Q для Dream), WFM-стерео и SPEC. `SetRXAPanelBinaural` объявлена в `aether_wdsp.h` (в WDSP она уже есть, `upstream/patchpanel.c`).

Файлы: WdspChannel.cpp, third_party/wdsp/include/aether_wdsp.h, ForkBuild.h (306).

## 271. Сборка 307 — кнопка BIN (бинауральный звук) в RX-апплете

Оператор: «а может сделать нам его включение, это полезно же».

**Сделано.** Кнопка **BIN** в строке панорамы звука (L — ползунок — R — BIN) RX-апплета, по приёмнику, запоминается (`AnanRx1Binaural` / `AnanRx2Binaural`).
- Выключена (по умолчанию) — моно, как в сборке 306.
- Включена — бинаурально, как BIN в Thetis: левый канал = I, правый = Q выхода демодулятора (пара со сдвигом 90°). Звук «объёмнее», образ смещается с боковой полосой.

**Цепочка:**
- `AnanUiState::binaural[rx]` / `setBinaural`;
- `Hl2Backend` в шаге шумовой линии отправляет состояние в DSP приёмника, если оно изменилось или DSP новый (`binauralDsp` сбрасывается при создании DSP);
- `Hl2RxDsp::setBinaural` (хранит состояние и повторяет его после пересборки канала);
- `WdspChannel::setBinaural` → `applyRxPanel(mode, binaural)`: `SetRXAPanelBinaural(1)` при BIN или в режимах DRM/WFM/SPEC, иначе 0.

Файлы: WdspChannel.cpp/.h, Hl2RxDsp.cpp/.h, Hl2Backend.cpp/.h, AnanUiState.h, RxApplet.cpp/.h, ForkBuild.h (307).

## 272. Сборка 308 — разбор потребления процессора и памяти

Оператор: «посмотри неспешно и вдумчиво, что в нашей проге может потреблять процессорное время… может 3D-панорама считается, когда она не нужна… заодно что жрёт память — 600 МБ».

Логи 11.11 (19:47–20:27): потерь пакетов нет («0 gaps», «I/Q dropped 0»), буфер динамика радио ровный (1000–1160 из 2048, 0 пустых чтений). Значит, потерь в сети и в радио нет — искать на хосте.

**Найдено и исправлено:**

1. **3D считалась и в 2D.** Каждая строка водопада также передискретизировалась, сглаживалась и сортировалась (медиана) в строки 3D (2048 колонок с 293) при любом режиме панорамы. Теперь `appendDssWaterfallRow` работает только в 3D; при входе в 3D строки очищаются (`m_dss.clear()`, сброс 3D-уровня шума), поверхность набирается из живых строк (96 строк, несколько секунд).

2. **Память: вершины 3D-меша на каждую панораму.** `initDssMeshPipeline` создавал при старте два статических буфера вершин: 96 рядов × 2048 колонок — ~57 МБ заливки + ~28 МБ лент, на каждую панораму, даже если 3D ни разу не включали. Теперь их делает `ensureDssMeshGeometry()` на первом 3D-кадре; отрисовка проверяет наличие буферов.

3. **Память: история 3D.** При входе в 3D сразу выделялась прокрутка 3D на все 20 минут водопада (24000 строк × 2048 × qfloat16 = 98 МБ на панораму, вдвое больше с дополнительной строкой). Ограничено `DssRenderer::kMaxHistoryRows = 3600` (~3 мин, 14.7 МБ); прокрутка водопада дальше показывает самые старые сохранённые 3D-строки.

4. **I/O-поток (там же DSP приёмников): окно FFT.** Скользящее окно `Hl2Spectrum` было вектором, у которого на каждом IQ-пакете (~1600 раз/с при 384 кГц) стирался начальный участок — сдвиг всего окна (512 КБ при 64 k точек). Теперь это кольцо (`accPush/accDropOldest/accAt`). Стенд (`scratchpad/cpubench/t.cpp`, 3 с IQ при 384 кГц, 30 кадров/с, сравнение со старым кодом — результат совпадает): 64 k точек 257 → 178 мс, с усреднением 278 → 221 мс; 8 k — 44 → 39 мс.

5. **Там же: логарифмы.** `computeFrame`: log10 во float, сырой кадр без усреднения не логарифмируется второй раз, fftshift без деления по модулю.

6. **GUI-поток (через него же идёт звук приёмника): дБ → мощность.** Средние по мощности (строка водопада — до 63 k бинов на кадр при 64 k FFT, 4096-точечная панорама, точки трассы, группы чёрного уровня) — `std::exp` во float вместо `std::pow` в double (стенд: 0.73 → 0.17 мс на 63 k бинов).

7. **Перерисовка всего слоя оверлея из-за линий.** Опрос каждые 100 мс перерисовывал весь кешированный оверлей (шкалы, слайсы, подписи) и заново загружал его текстуру при каждом изменении полки или гейта на 0.5 дБ, то есть почти непрерывно. Теперь линии сравниваются так, как нарисованы (строка пикселя и целые дБ в подписи), а изменение одних линий перерисовывает не чаще 2 раз в секунду. Перегрузка АЦП и её мигание — сразу.

**Проверено и в порядке:**
- 3D GPU-путь (загрузка текстуры, меш) работает только в 3D;
- таймер анимации подключения останавливается;
- AUTO ATT отправляет ослабление только при изменении;
- запись I/Q для диагностики отпускания (unkey) — кольцо без выделений;
- опросы RX-апплета и VFO (250 мс) меняют виджеты только при изменении.

**Не изменено, к сведению:**
- Звук приёмника проходит через GUI-поток (WDSP на I/O-потоке → `Hl2Backend::mixReceiverAudio` → RadioModel → AudioEngine), около 375 блоков/с. Любая задержка GUI-потока (перерисовка, тяжёлая операция) может дать щелчок в звуке ПК. Перенос микширования с GUI-потока — отдельная работа.
- История 2D-водопада (апстрим) — 20 минут × ширина × 1 байт, две копии (видимая и дополнительная): до ~80 МБ на панораму после 20 минут работы.
- 3D-меш сам по себе тяжёл для встроенной видеокарты: ~4.7 млн вершин на кадр.

**Новое для проверки:** GPU-код теперь проверяется компиляцией с заголовками QRhi из `scratchpad/qt6w/include/QtGui/6.8.3`:
`EXTRA="… -DAETHER_GPU_SPECTRUM -I$Q/include/QtGui/6.8.3 -I$Q/include/QtGui/6.8.3/QtGui -I$Q/include/QtCore/6.8.3 -I$Q/include/QtCore/6.8.3/QtCore"`.

Файлы: SpectrumWidget.cpp/.h, DssRenderer.cpp/.h, Hl2Backend.cpp, Hl2Spectrum.cpp/.h, ForkBuild.h (308).

## 273. Сборка 309 — история водопада 5 минут

Оператор: «можно уменьшить память водопада до 5 минут».

`SpectrumWidget::kWaterfallHistoryMs` уменьшен с 20 до 5 минут. Прокрутка водопада назад — до 5 минут. Память истории (видимые и дополнительные строки, 1 байт на пиксель) — вчетверо меньше: до ~20 МБ на панораму вместо ~80 МБ.

Метки времени на водопаде с интервалом 10 и 15 минут в меню остаются, но в 5-минутной истории их линии видны не будут.

Файлы: SpectrumWidget.h, ForkBuild.h (309).

## 274. Сборка 310 — FreeDV на боковой полосе диапазона (LSB ниже 10 МГц)

Пользователи: «модуляция FreeDV требует установки USB или LSB в зависимости от диапазона».

**Подтверждение.** freedv.org, Getting Started: «On bands below 10 MHz, LSB is used for FreeDV. On 10MHz and above, USB is used.» — то же соглашение, что для аналоговой SSB, чтобы с DV-станцией можно было работать на той же полосе, что и голосом; приложение FreeDV подсвечивает красным неожиданную полосу. В обсуждении digitalvoice (2023, Mooneer Salem) — то же: LSB на 40 м и ниже, USB выше, 60 м — особый случай (USB по регламенту).

**Ошибка.** `MainWindow::activateLocalFdv` (FDV через libfdv в Hl2Backend) всегда ставил USB, а при любом другом режиме выключал FDV.

**Исправлено.**
- `freeDvSidebandFor(МГц)`: LSB ниже 10 МГц, кроме 5.25–5.45 МГц (60 м любой страны) — там USB; USB от 10 МГц.
- При включении FDV ставится эта полоса.
- Перестройка на другой диапазон переключает полосу (`m_localFdvFreqConn`).
- FDV выключается только при режиме, отличном от USB/LSB.

Модем работает со звуком после демодулятора, который правильная полоса отдаёт неперевёрнутым; передача идёт на полосе слайса. Путь RADE для Flex уже выбирал полосу по таблице диапазонов.

Файлы: MainWindow_DigitalModes.cpp, MainWindow.h, ForkBuild.h (310).

## 275. Сборка 311 — без подсказок на кнопках быстрой памяти

Оператор: «при наведении курсора на кнопку быстрой памяти открывается чёрное пустое окно, я думаю это подсказка. Убери их, они не нужны».

`BandStackPanel`: кнопкам памяти больше не задаётся подсказка (частота, режим, антенна, скорость, панорама). Подсказки кнопок очистки, добавления и настроек панели остались.

Файлы: BandStackPanel.cpp, ForkBuild.h (311).

## 276. Сборка 312 — подсказки кнопок быстрой памяти возвращены и читаются

Оператор: «если это полезные подсказки, давай их починим. Почему это было чёрное окно, чёрным по чёрному?»

**Возвращено.** Подсказки с частотой, режимом, антенной, скоростью и панорамой, которые сборка 311 убрала.

**Причина чёрного окна.** Подсказка берёт стиль из style sheet своего виджета. У кнопок памяти он задавал только `QPushButton { … }` без правила `QToolTip`, поэтому подсказка получала палитру с тёмным текстом на тёмном фоне (во всём приложении нет общего правила `QToolTip`).

**Исправлено.** В style sheet кнопки добавлено `QToolTip { color: #e8f4fb; background-color: #102131; border: 1px solid #355166; padding: 4px 6px; font-size: 12px; font-weight: normal; }` — те же цвета, что у подсказок PropDashboardDialog.

Файлы: BandStackPanel.cpp, ForkBuild.h (312).

## 277. Сборка 313 — подсказки панели быстрой памяти как во всей программе

Оператор: «у нас все подсказки — чёрный цвет на жёлтом фоне. Приведи эти подсказки к единому стандарту, и подсказки на кнопках внизу панели тоже — они тоже поломаны».

**Настоящая причина** (сборка 312 её не нашла). `BandStackPanel` ставил себе style sheet без селектора: `background: {{color.background.0}};`. Такое правило действует на панель и на всё внутри неё, включая подсказки её кнопок (памяти, Clear All, +, настройки). Подсказка получала тёмный фон при своём чёрном тексте.

**Исправлено.**
- Фон задаётся только самой панели: `QWidget#bandStackPanel { … }` и `WA_StyledBackground`.
- Viewport прокрутки и содержимое не заливаются фоном и показывают фон панели.
- Собственный стиль подсказки из 312 убран: подсказки — стандартные, как во всей программе.

Тот же шаблон (style sheet без селектора с тёмным фоном) есть ещё в `EditorFramelessTitleBar` и `MemoryBrowsePanel` — там подсказки могут быть такими же; не трогал.

Файлы: BandStackPanel.cpp, ForkBuild.h (313).

## 278. Сборка 314 — единый вид подсказок: чёрный фон, голубая рамка, белый текст

Оператор: «верни как положено — чёрный фон, голубая кайма и белый текст, как принято для всех элементов окна, и исправь, если найдёшь другие подсказки с проблемой».

**Как Qt оформляет подсказки** (исходники qtbase: `qstylesheetstyle.cpp` parentObject / nodeNames, `qtooltip.cpp` placeTip). Подсказка (`QTipLabel`) соответствует только правилам `QToolTip` и правилам без селектора. Правила ищутся в style sheet виджета, которому принадлежит подсказка, в его предках (через `_q_stylesheet_parent`) и в приложении. Правила `QLabel {…}` / `QWidget {…}` к подсказке не относятся. Своего правила `QToolTip` в программе не было нигде, поэтому любой фон без селектора у предка (панель быстрой памяти, сборка 313) давал подсказке тёмный фон при её чёрном тексте.

**Исправлено.** Одно правило на всё приложение в `main.cpp` (рядом с правилом для чекбоксов):
`QToolTip { color: #ffffff; background-color: #000000; border: 1px solid #00b4d8; padding: 3px 5px; }`.
Так выглядят все подсказки. Где фон без селектора у предка ещё действует (`MemoryBrowsePanel`, `EditorFramelessTitleBar`), подсказка берёт его тёмный фон (у `MemoryBrowsePanel` — и серую рамку), но текст белый и читается. Эти панели не трогал: их правило без селектора оформляет и все их дочерние элементы.

Сужение фона панели быстрой памяти из 313 оставлено.

Файлы: main.cpp, ForkBuild.h (314).

## 279. Сборка 315 — в режиме DUP автоматика приёмников тоже стоит на время передачи

Оператор: «режим передачи блокирует автоматику приёмников, и это правильно, но автоматика не блокируется в режиме DUP. Нужно, чтобы тоже блокировалась».

**Причина.** В DUP панорамам о передаче не сообщается (`MainWindow_Session`: `panTx = tx && !ananDuplex`), чтобы они продолжали показывать приёмники. Но этим же `setTransmitting` останавливает автоматику панорамы, и в DUP она работала на спектрах собственного передатчика:
- детектор шумовой полки (`m_noiseFloorEnable`);
- автошумоподавитель (auto squelch);
- автоуровень чёрного водопада (Auto Black) и бланкер;
- восстановление запаса шкалы (headroom) и синхронизация уровня при зуме 3D.

Автоматика бэкенда (AUTO ATT, Auto AGC, шумовая линия) и раньше стояла по `m_keyed`, в том числе в DUP.

**Исправлено.** `SpectrumWidget::setDuplexTxHold(bool)`: в DUP на время передачи и 500 мс после отпускания (хвост собственного сигнала в приёмном тракте, как у AUTO ATT) стоят перечисленные выше функции, а отображение идёт как в DUP. `MainWindow_Session` вызывает его для каждой панорамы вместе с `setTransmitting`.

Файлы: SpectrumWidget.cpp/.h, MainWindow_Session.cpp, ForkBuild.h (315).

## 280. Сборка 316 — MIDI: две ручки валкодера, VFO A и VFO B

Оператор: «в MIDI-устройствах есть параметр VFO для валкодера. Нужно два параметра, VFO_A и VFO_B. Если настроен только любой один из них — работает как раньше, управление активным VFO; если настроены оба — каждый управляет своим VFO, вне зависимости от активности».

**Сделано.**
- Параметры MIDI: `rx.tuneKnob` теперь называется «VFO A Tune Knob» (id прежний, сохранённые привязки остаются за VFO A), новый `rx.tuneKnobB` — «VFO B Tune Knob».
- `MidiControlManager::isVfoTuneKnob` признаёт обе, поэтому у B та же обработка бесконечного энкодера: автоопределение кодировки, ступени без ускорения, совместимые режимы.
- `MainWindow::midiTuneKnobSlice(knobB)`: привязана только одна из ручек (любая) — активный слайс, как раньше; привязаны обе — A настраивает слайс A, B — слайс B (буква от id слайса, 0 = A), независимо от активного; если такого VFO нет — ничего.
- У ручки B своя цель настройки «в полёте» и свой таймер простоя (`m_midiTuneTargetMhzB`, `m_midiTuneIdleTimerB`) — обе ручки можно крутить одновременно. Сбросы при смене шага и при скачке частоты, а также учёт активной настройки (`MainWindow_Wiring`) охватывают обе.

Проверка компиляции с `-DHAVE_MIDI` (заглушка RtMidi в `scratchpad/stubs/rtmidi`) — без новых ошибок.

Файлы: MidiControlManager.cpp, MainWindow.h, MainWindow.cpp, MainWindow_Wiring.cpp, MainWindow_Controllers.cpp, ForkBuild.h (316).

## 281. Сборка 317 — трансвертеры для openHPSDR (хранятся в программе)

Оператор: «пользователи жалуются, что не могут настроить и использовать режим трансвертера. Посмотри, может у нас где-то обрезаны пути».

**Где были обрезаны пути.** У FlexRadio трансвертеры живут в самом радио: оно их хранит, присылает статусы `xvtr N` и само сдвигает гетеродины. У openHPSDR ничего этого нет, а у нас:
- `Hl2Backend` не реализовывал `requestTransverter` — создание/правка на странице Setup → Transverters отвечали Unsupported;
- статусов `xvtr N` никто не присылал — список трансвертеров в RadioModel всегда был пуст;
- `XvtrPolicy::evaluateBandTune` в ветке не-Flex не знал о трансвертерах и отказывал в любой частоте выше 55 МГц;
- кнопка диапазона в `MainWindow_Wiring` передавала пустой список трансвертеров (`{}`);
- пересчёта RF → ПЧ при записи частоты в регистры не было.

**Что сделано.**
- Новый `AnanXvtr.h`: хранилище `AnanXvtrStore` в AppSettings «AnanTransverters» (JSON): имя, RF, IF, ошибка гетеродина, усиление RX, макс. мощность, RX-only. Отдаёт статусы в том же виде `xvtr N` (order, name, rf_freq, if_freq, lo_error, rx_gain, max_power, rx_only, is_valid), поэтому страница настроек и меню диапазонов работают без изменений.
- Новый сигнал `IRadioBackend::transverterStatus`, RadioModel передаёт его в `onStatusReceived("xvtr N", ...)`.
- `Hl2Backend::requestTransverter`: создание, удаление (in_use=0), правка полей, сохранение, статус; при подключении — перезапись всех частот, частоты TX и полосовых фильтров. Список публикуется сразу после `connected()`.
- Пересчёт: ПЧ = RF − (rf − if + lo_error). Применяется, если RF выше собственного предела радио (55 МГц), а ПЧ попадает в его диапазон; при нескольких — ближайший по rf. Используется в `ncoCommandHz`, `dspShiftHz`, `pushAnanTunedFrequencies`, OC-байтах `applyBandFilter` (фильтры выбираются по ПЧ), `restorableRfHz`.
- `hzInAmateurBand`: на трансвертере передача разрешена только в любительских диапазонах VHF/UHF/SHF и запрещена при RX-only.
- `XvtrPolicy` (не-Flex) допускает частоту, если трансвертер переносит её ПЧ в диапазон радио; кнопка диапазона передаёт реальный список трансвертеров.

**Не сделано:** ограничение драйва по max_power, учёт rx_gain в S-метре и панораме, отдельная коммутация порта XVTR (используется обычный выбор антенны). На радио не проверено.

## 282. Сборка 318 — кнопка диапазона перестраивает только приёмник своей панорамы

Оператор: «если открыты два приёмника и в первом с панели панорамы кнопкой изменить бенд, то второй приёмник может тоже переключиться на другой бенд. Каждый приёмник должен управляться только своими кнопками».

**Причина.** В ветке без радио-band-stack (openHPSDR) `MainWindow::selectBand` получал `panId` панели, на которой нажата кнопка, но перестраивал `activeSlice()` — активный приёмник, где бы он ни стоял. Если активен RX2 (клик по его панораме или VFO), то кнопка 40m на панели RX1 переводила на 40m именно RX2, загружала его память диапазона, а центр (`requestPanCenter(panId, …)`) и ширину выставляла панораме RX1. Со стороны это выглядело как «второй приёмник тоже переключился».

**Исправление.** Приёмник выбирается по панораме кнопки: `DisplaySquelchRouting::displaySlice(slices, panId, activeSlice(), titleSliceId)` — активный, если он на этой панораме; иначе приёмник из заголовка панели; иначе первый на ней. Если на панораме приёмника нет — сообщение «No receiver on this panadapter to tune». В лог пишется `band button on pan … -> slice …`. Горячие клавиши диапазонов по-прежнему работают с активным приёмником (передают его же панораму). Общая метка `m_bandSettings.currentBand` частоты не меняет и оставлена.

Не путать со Slice Link (связка VFO между панорамами): при включённой связке перестраиваются оба — так и задумано.

## 283. Сборка 319 — кнопки меню панорамы переключают окна одним кликом

Оператор: «любой клик вне окна сначала закрывает окно — это работает, но усложни: при клике на другую кнопку из этой группы предыдущее окно закрывается и новое открывается. Правило клика на ту же кнопку для закрытия сохрани. При клике на стрелку просто всё закрывается. У окна WB иммунитет — у него собственный механизм закрытия, он не трогается, если уже открыт».

**Было (сборка 249, раздел 211).** `SpectrumOverlayMenu::closeSubPanelsOnOutsidePress` съедал любое нажатие вне открытой панели (Band, XVTR, ANT, DAX, Display, Memory) и только закрывал её; чтобы открыть другую панель, нужен был второй клик.

**Стало.** Перед закрытием проверяется, куда пришлось нажатие:
- кнопки Band, ANT, Display, Memory, DAX этого меню — нажатие пропускается к кнопке. Её обработчик (`toggleBandPanel` и др.: `hideAllSubPanels`, затем показ, если открыта была не она) закрывает прежнюю панель и открывает свою, а повторный клик по той же кнопке закрывает панель;
- стрелка (`m_toggleBtn`) — нажатие пропускается, `toggle()` сворачивает меню и закрывает все панели;
- WB — открытая панель закрывается, нажатие проходит к кнопке, окно Wideband Monitor открывается или поднимается (`showOrRaisePersistent`); само окно WB отсюда никогда не закрывается;
- +RX и любое другое место — как раньше: клик только закрывает панель.

## 284. Сборка 320 — панель банд-стека: элементы снова прежнего цвета

Оператор: «после сборки 314 на нижней панели изменился цвет элементов. Это в планы не входило, речь шла только о подсказках. Откати изменения для самих элементов».

**Причина — сборка 313 (раздел 277), не 314.** Сборка 314 добавила только правило `QToolTip` в стиль приложения, элементов оно не касается. А в 313 правило фона панели без селектора (`background: …`, оно действует на панель и на всё внутри) было заменено на `QWidget#bandStackPanel { … }` + `WA_StyledBackground`, а у области прокрутки и её содержимого снят autoFill. Тем самым элементы внутри панели перестали получать её фон — изменился их вид.

**Исправление.** `BandStackPanel` возвращён к состоянию до 313 (сборка 312 без её собственного стиля подсказок): правило снова действует на всю панель — записано как `* { background: {{color.background.0}}; }` (то же, что правило без селектора), имя объекта, `WA_StyledBackground` и снятие autoFill убраны. Чтобы подсказки при этом остались стандартными, в ту же таблицу стилей добавлено `QToolTip { color:#ffffff; background-color:#000000; border:1px solid #00b4d8; padding:3px 5px; }` — правило по типу специфичнее `*` и перекрывает фон панели только у подсказок. Файл отличается от сборки 312 только этой строкой стиля (и удалённым стилем подсказок 312).

## 285. Сборка 321 — TCI: счётчики потока I/Q и сведения о клиентах в аплете

Оператор: «запустил CW Skimmer, он подключается по TCI и требует I/Q. Подключение видно, управление видно, I/Q потока нет. Посмотри лог, какой режим запросила программа и почему не работает. Добавим в аплет информацию, какой режим запросил клиент, и что-то ещё важное».

**Лог (aethersdr-20261112-191724.log, Protocol 2).** Клиент — `TciCwSkimmer 0.7.5` (локальный). Он запрашивает только I/Q, без аудио и спектра: `iq_samplerate:96000; iq_start:0;`. Бэкенд отвечает «ANAN: TCI I/Q on for receiver 1 channel 1 at 96000 Hz, DDC 384000 Hz», клиенту уходит `iq_start:0;`. Через ~5,7 с клиент сам переключает `iq_samplerate:48000; iq_stop:0; iq_start:0;`, ещё через 4–6 с закрывает соединение кодом 1000 (штатно, со своей стороны) и через 1–13 с переподключается. Цикл повторяется. Ограничители сервера не срабатывали: нет «worker mailbox limit», «TCI output backlog», закрытий с кодом 1009. Похоже, что клиент не получает I/Q и перезапускается.

**Проверено по коду, ошибки не найдено.** P2Transport::deliverAlignedBlocks → `iqBlocksReady` → `Hl2Backend::tciIqTap` (сопряжение, перенос центра вида, P1PsResampler 384→96/48 кГц, кадры по 1024 комплексных отсчёта) → `tciIqReady` → `TciServer::onIqDataReady` (строковый connect, слот объявлен, предупреждений Qt нет) → `buildAudioFrame` → `TciClient::binarySink` → почтовый ящик `TciIoWorker` → `sendBinary`. Заголовок кадра совпадает с Thetis (`TCIServer.cs::buildStreamPayload`) поле в поле: receiver, rate, format 3, codec 0, crc 0, length = комплексные×2, type 0, channels 2, 8 нулевых слов. Отличие от Thetis одно: Thetis шлёт I/Q на аппаратной частоте (≤384 кГц) и `iq_samplerate` клиента только повторяет, мы следуем запросу клиента — кадр при этом несёт правильную частоту. В логе не было ни одного признака того, уходят ли кадры, поэтому причина по нему не устанавливается.

**Что добавлено.**
- `TciServer`: счётчики — кадры I/Q от радио (все каналы), и по каждому клиенту: кадры, отданные сокету, комплексные отсчёты, отказы (`sendBinaryMessage` < 0), частота в кадрах. Раз в секунду (5 тиков таймера статуса) пересчёт кадров/с и кС/с и сигнал `clientStatsChanged()`. Раз в 5 с строка «TCI I/Q stats: <программа> receiver(s) …, requested … Hz, announced … Hz, sent N frames (…/s, … kS/s), refused …; from the radio … frames in 5 s». Первый кадр клиенту — строка «TCI: first I/Q frame to …» с параметрами заголовка. Если I/Q включён более 3 с, а за секунду ничего не ушло — одно предупреждение с числом кадров от радио за ту же секунду: так лог различает «нет I/Q от радио» и «I/Q не доходит до клиента».
- `TciClientInfo` расширен: приёмники I/Q, частота, кадры/отказы/скорости, частота/каналы/формат аудио, спектр, последняя команда.
- Аплет TCI: блок по каждому клиенту (скрыт без клиентов): имя и версия программы; «I/Q RX1 at 96 kHz: 94 frames/s, 96.0 kS/s» (зелёным) или «no data sent (radio gives N frames/s | none from the radio)» (красным), отказы; «Audio RX… at 48 kHz, float32, stereo» или «Audio off»; spectrum / sensors / last: команда. Обновляется раз в секунду и при изменении подписок.

Следующий шаг: прогон со скиммером на сборке 321 и лог — по счётчикам станет видно, на каком участке пропадает поток.

## 286. Сборка 322 — TCI I/Q кадрами по 2048 пар, как в спецификации TCI

**Лог сборки 321 (aethersdr-20261112-194809.log).** Счётчики показали, что поток уходит: «first I/Q frame to TciCwSkimmer: receiver 0, 96000 Hz, float32, 2 channels, 1024 complex samples (header length 2048), 8256 bytes»; «sent … (95/s, 97.3 kS/s), refused 0; from the radio 461 frames in 5 s»; на 48 кГц — 47–48 кадров/с. Частота точная, отказов нет, кадры от радио приходят в том же темпе. Но клиент всё так же через 5 с переходит на 48 кГц (`iq_samplerate:48000; iq_stop; iq_start`), через 6 с закрывает соединение (1000) и переподключается — т.е. данные доходят, а клиент их не принимает.

**Причина (наиболее вероятная).** Поля заголовка совпадают с Thetis, Quisk/deskHPSDR и описанием Expert Electronics (length = число float, I и Q по отдельности). Отличается размер кадра: спецификация TCI объявляет поток как фиксированную структуру `DataStream { 16×uint32 заголовок; float data[4096]; }` — 2048 пар I/Q, 16448 байт; так шлёт ExpertSDR3, и так режет тап HL2 в основной ветке AetherSDR (PR #6859, «runs of 2048 IQ pairs»). Мы со сборки 222 слали 1024 пары, 8256 байт. Клиент, написанный под ESDR3 и принимающий кадр как эту структуру или проверяющий его размер, отбрасывает каждый кадр — для него I/Q нет, отсюда тайм-аут 5 с, попытка 48 кГц и переподключение.

**Исправление.** `Hl2Backend::tciIqTap`: `kFrameBytes = 2048 * 2 * sizeof(float)` — кадр I/Q из 2048 пар (16448 байт с заголовком), 43 мс на 48 кГц, 21 мс на 96 кГц. Заголовок прежний (length = 4096). Счётчики сборки 321 покажут в логе «2048 complex samples (header length 4096), 16448 bytes».

Если клиент и с этим не примет поток — следующая разница с Thetis: тот шлёт I/Q на аппаратной частоте приёмника, а `iq_samplerate` клиента только повторяет.

## 287. Сборка 323 — TCI объявляет протокол 2.0, как Thetis

**Лог сборки 322 (aethersdr-20261112-195401.log).** Кадры I/Q уже размера ESDR3 («2048 complex samples (header length 4096), 16448 bytes»), уходят 48 кадров/с, 98 кС/с на 96 кГц, отказов нет. Клиент TciCwSkimmer 0.7.5 ведёт себя без изменений и с точностью до миллисекунд: через 5,0 с после `iq_start` — `iq_samplerate:48000; iq_stop; iq_start`, через 6,0 с — закрытие 1000, переподключение. Значит, не размер кадра и не темп. Оператор: пользователь подтверждает, что этот скиммер работает с Thetis.

**Сравнение с Thetis (TCIServer.cs).**
- `iq_samplerate` клиента Thetis только повторяет, I/Q шлёт на аппаратной частоте с ней в заголовке — значит, клиент читает частоту из заголовка; наши кадры с запрошенной частотой ему подходят.
- Заголовок кадра (buildStreamPayload) совпадает с нашим поле в поле, length = комплексные × 2.
- **Отличие:** Thetis объявляет `protocol:<имя>,2.0;` (при эмуляции ESDR3 — `ExpertSDR3,2.0`), мы — `protocol:ExpertSDR3,1.5;` (из основной ветки, сборка 169, ради WSJT-X). В TCI 1.x поле length заголовка потока было двусмысленным (число отсчётов или число float), в 2.0 — число float. Клиент, получивший 1.5, может читать length 4096 как 4096 пар, ждать вдвое больше данных и браковать каждый кадр — ровно наблюдаемое: поток идёт, клиент его не видит. TCIskimmer заявлен как клиент «с полной поддержкой TCI 2.0».

**Исправление.** `TciProtocol::generateInitBurst`: `protocol:ExpertSDR3,2.0;`. Порядок пакета инициализации (раздел 4.1 спецификации) не менялся. WSJT-X различает сервер по имени «ExpertSDR3», а не по версии, и с Thetis в режиме эмуляции («ExpertSDR3,2.0») работает; автотесты основной ветки уже используют «ExpertSDR3,2.0». Наши кадры потока и так в разметке 2.0 (channels в слове 7).

Если и это не поможет — следующее отличие: Thetis шлёт в начальном состоянии `iq_stop:0; iq_stop:1;` и `if_limits` = ±половина аппаратной частоты.

## 288. Сборка 324 — TCI: «Center Lock when I/Q» и частота DDC по запросу скиммера

**Результат сборки 323.** После `protocol:ExpertSDR3,2.0` CW Skimmer (TciCwSkimmer.exe из «C:\Program Files\CW Skimmer») держит I/Q без перезапусков; на снимке экрана: «RX1 · center 7075.39 kHz · IQ 96 kHz», ось 7030–7120 кГц, декодер пишет. Ширина полная; «15.1 s» внизу — время водопада (по горизонтали), не ширина.

**Проблема.** Панорама 384 кГц, а поток I/Q — окно 96/48 кГц вокруг центра вида. Клик далеко от центра ставит VFO вне окна — скиммер его не видит. Thetis шлёт весь захват DDC на аппаратной частоте (≤384 кГц) и `iq_samplerate` клиента только повторяет; dds = центр DDC, if_limits = ±половина частоты. Начатый было перенос этой схемы отменён по решению оператора в пользу его варианта.

**Сделано (вариант оператора).**
- **Аплет TCI: «Center Lock when I/Q»** (AppSettings `TciCenterLockWhenIq`). Пока клиент TCI берёт I/Q с приёмника (`TciServer::iqSliceIds()`, по `clientsChanged`), на его панораме включается Center Lock (`setCenterLockForPan`, без сохранения): любой клик или перестройка держит фильтр в середине панорамы, а значит — в середине потока I/Q. На таких панорамах Center Lock центрирует **середину полосы фильтра**: VFO + (низ+верх)/2 (`snapCenterLockForSlice`) — смещение полосы от VFO учтено в любом режиме (CW ±250 → VFO, USB 100…2900 → VFO+1,5 кГц). Смена фильтра пересчитывает центр. Когда I/Q останавливается или галочку снимают, включённый так замок снимается; собственный Center Lock оператора остаётся и снова центрирует VFO (`MainWindow::updateTciIqCenterLock`, `m_tciIqCenterLockPans`).
- **Частота I/Q.** Если клиент просит `iq_samplerate` больше текущей частоты DDC — частота приёмника поднимается до запрошенной (глагол `anan.rate`, как кнопки частоты на панораме; зум сбрасывается). Если меньше — тап пересчитывает вниз, как раньше.

Не проверено на радио.

## 289. Сборка 325 — Center Lock без мелькания старой картинки; галочка под ползунком TX

Оператор: «работает, только кнопку в аплете перенеси выше, сразу под ползунок TX. И я поймал мельтешение: при включённом Center Lock кликаю по панораме, она перестраивается, но потом на долю секунды показывает старое значение. Посмотри пошагово».

**Пошагово (лог aethersdr-20261112-203613.log, на каждый клик: «vfo/dds старые» → «Backend refused operation pan display (announced)» → через 4 мс «vfo/dds новые»).**
1. Клик на ANAN → `edgePanTuneRequested` → `pushSliceFrequencyToOverlays` → `snapCenterLockForSlice` → `requestPanCenter(…)` с намерением **Range** → затем `setFrequency`.
2. `Hl2Backend::setPanCenter` намерение не различал и всё делал как при перетаскивании: центр вне досягаемости вида → NCO переставлялся **с упреждением** (центр + 0,8·запаса), вид оставался ~25 кГц в стороне от NCO, в спаде фильтра DDC; усреднение спектра при сдвиге NCO меньше половины полосы **не сбрасывалось** — несколько кадров показывали старую картинку на новой шкале.
3. Через 400 мс `settlePanViews()` возвращал вид в чистые 80 % — **второй** сдвиг NCO, снова со старым усреднением. Это и есть «через долю секунды показывает старое».
4. `IRadioBackend::requestPanDisplay` (базовая реализация, ею пользуется HL2/ANAN) после уже выполненного `setPanCenter` заново проверял «забор»; синхронный `emitPanState` внутри `setPanCenter` его сдвигал → возвращался `Unsupported` при применённом центре: предупреждение «refused», уведомление `commandDropped`, а Center Lock считал центр отложенным и не выставлял вид сразу.

**Исправление.**
- `Hl2Backend::setPanCenter`: для любого центра, кроме перетаскивания (Center Lock, раскрытие, центр при зуме) — в пределах чистых 80 % двигается только вид; дальше NCO ставится **прямо в центр** (без упреждения, `settlePanViews` делать нечего) и усреднение спектра сбрасывается (`dropPanAverage`). Перетаскивание — как прежде (сборка 209).
- `IRadioBackend::requestPanDisplay`: если забор сдвинулся после применённого центра — `Dispatched` (останавливаемся только перед шириной), а не `Unsupported`.
- Аплет TCI: «Center Lock when I/Q» — сразу под ползунком TX.

## 290. Сборка 326 — CTUN OFF в приёмнике для TCI I/Q (как в Thetis)

Оператор: «почему всё так сложно? В Thetis при выключенном CTUN фильтр стоит по центру, а перестраивается сам аппаратный приёмник». Разобрали: AetherSDR сделан под Flex, где приёмник и панорама — независимые объекты радио (по сути постоянный CTUN ON); Center Lock — надстройка интерфейса, имитирующая CTUN OFF двумя командами с двумя эхами; в бэкенде ANAN сверху ещё сдвиг вида внутри полосы DDC (сборка 209) с упреждением и досадкой. У настоящего CTUN OFF есть цена (каждая перестройка двигает DDC, короткий провал звука на мелком шаге), поэтому оператор решил: обычный приёмник не трогать, а для TCI с галкой «Center Lock when I/Q» сделать примитивное действие — скиммеры мелким шагом не перестраиваются.

**Бэкенд (Hl2Backend).**
- `Receiver::ctunOff`; глагол расширения `anan.ctun` {slice, on}.
- `setSliceFrequency`: при ctunOff — NCO = частота приёма (с RIT) + середина фильтра (`ctunNcoHz`), одна запись регистра через `moveNcoForView` (сдвиг DSP, notch, ncoMoved), сброс усреднения, сдвиг вида = 0. Без окна, упреждения и досадки; одна публикация состояния панорамы.
- `ctunCenter()`: то же после смены фильтра (`setSliceFilter`) и режима (`setSliceMode`) и при включении.
- `setPanCenter` с намерением Drag при ctunOff перестраивает приёмник (фильтр остаётся в центре), как перетаскивание в Thetis. Прочие запросы центра (кнопки диапазонов, зум) обрабатываются как раньше; следующая перестройка снова ставит фильтр в центр.

**Интерфейс.** На радио с бэкендом «hl2» галка «Center Lock when I/Q» больше не включает Center Lock: `MainWindow::updateTciIqCenterLock` шлёт `anan.ctun` вкл/выкл для приёмников, с которых клиент TCI берёт I/Q (`m_tciIqCtunSlices`), и повторяет после переподключения радио. На других радио остаётся эмуляция через Center Lock (сборка 324). Собственный Center Lock оператора на такой панораме центрирует середину фильтра — совпадает с NCO.

Не проверено на радио.

## 291. Сборка 327 — обычный Center Lock снова «мягкий»

Оператор: «при Center Lock при перестройке, при выключенных скиммере и TCI, тоже происходит перерасчёт панорамы и жёсткая перестройка аппаратного приёмника».

**Причина — сборка 325.** Она перевела все запросы центра, кроме перетаскивания, на «внутри чистых 80 % — только вид, дальше — NCO сразу в центр». При полном обзоре вид и так занимает все 80 %, запаса нет, и обычный Center Lock на каждом клике переставлял NCO. Правка делалась против мелькания при TCI; после сборки 326 этот случай обслуживает CTUN OFF в приёмнике, через `setPanCenter` он не идёт.

**Исправление.** Ветка сборки 325 в `Hl2Backend::setPanCenter` убрана: все намерения снова идут прежним путём (сборка 209) — вид двигается внутри захваченной полосы, NCO — только когда нужно. Остаются: CTUN OFF (326) с перетаскиванием, перестраивающим приёмник, и исправление ложного «отказа» в `IRadioBackend::requestPanDisplay` (325).

## 292. Сборка 328 — время в строке состояния тем же цветом, что подпись

Оператор: время в нижней строке выделяется одно — согласовать его с подписью «UTC Time:». Значение времени было `{{color.text.primary}}` (#eaf2fb в тёмной теме), подпись и остальные надписи строки — `{{color.text.secondary}}` (#c4d4e8). Теперь и значение — `color.text.secondary` (MainWindow.cpp, блок часов сборки 255).

## 293. Сборка 329 — флаг VFO: два ряда кнопок режимов; Marker и Filter Edge — в меню панорамы

Оператор: Marker — в меню правой кнопки панорамы, отдельной группой ниже Center Lock, со списком 1px / 3px / OFF как у Waterfall Time Markers; Filter Edge — туда же, с индикатором активности. Первую кнопку режима с выпадающим списком заменить обычной кнопкой с выбором режима по правой кнопке; добавить второй ряд кнопок режимов; проверить синхронизацию выбранного режима (кнопки, подпись под частотой, аплет RX Control).

**VfoWidget, вкладка режима.**
- Выпадающий список убран с флага (остаётся скрытым зеркалом модели). Вместо него — два ряда по четыре обычные кнопки (`kQuickModeButtons = 8`). Умолчания: LSB · USB · CW · AM / CWL · DIGL · DIGU · SAM.
- Правая кнопка на любой кнопке — выбор её режима из списка режимов радио (`SliceModel::modeList()`, на ANAN: USB LSB DSB DIGU DIGL AM SAM CW CWL FM), плюс SSB (USB↔LSB), DIG (DIGU↔DIGL), FDV; текущее назначение отмечено. Ключи настроек: первая кнопка — «ModeButton3», вторая–четвёртая — прежние «ModeButton0..2» (назначения сохраняются), второй ряд — «ModeButton4..7» (`quickModeKey`).
- Кнопка, режим которой радио не предлагает, неактивна.
- Подсветка следует только модели: после нажатия `updateQuickModeButtons()` показывает режим из модели, двух подсвеченных кнопок не бывает.
- Кнопки Marker и Filter Edge из сетки фильтров убраны; состояние (`setMarkerWidth`, `setFilterEdgesHidden`, по приёмнику, с сохранением) прежнее.

**SpectrumWidget, меню правой кнопки.** Новая группа под Center Lock (`addVfoMarkerControls`): подменю «VFO Marker» — 1 px / 3 px / Off (взаимоисключающие, с отметкой), и «Filter Edge» с галочкой. В общем меню — для активного приёмника, в меню флага приёмника — для этого приёмника.

**Синхронизация.** Все пути смены режима идут через `SliceModel::setMode` → `modeChanged`; на него подписаны подпись вкладки режима под частотой (`m_tabBtns[2]`), кнопки (`updateModeTab` → `updateQuickModeButtons`) и список режимов в RX Control (`RxApplet`). Режим, которого нет ни на одной кнопке (выбран в RX Control), виден в подписи и в RX Control, ни одна кнопка не подсвечена.

Не проверено на экране.

## 294. Сборка 330 — кнопка SSB при входе выбирает боковую по частоте

Оператор спросил, как работают назначения SSB (USB↔LSB) и DIG (DIGU↔DIGL); согласился, чтобы при входе боковая выбиралась по частоте, заметив, что кнопки диапазонов и так восстанавливают запомненный режим, а при свободной перестройке намерение пользователя неизвестно — поэтому выбор делается только в момент нажатия.

**Как было (наследие AetherSDR):** SSB — внутри USB/LSB нажатие меняет боковую, из другого режима всегда USB; надпись USB/LSB по текущему режиму. DIG — то же для DIGU/DIGL.

**Сделано.**
- Новый `core/SsbSideband.h`: `ssbSidebandFor(mhz)` — LSB ниже 10 МГц, USB с 10 МГц, USB на 60 м (5,25–5,45 МГц). FreeDV (сборка 310, `freeDvSidebandFor`) берёт правило оттуда же.
- Кнопка SSB на флаге VFO: внутри USB/LSB — смена боковой, как раньше; из другого режима — боковая по частоте приёмника на момент нажатия. Вне SSB надпись показывает боковую, которую выберет нажатие (обновляется при перестройке). Режим при перестройке не меняется никогда.
- DIG не тронут: цифровые режимы (FT8/FT4/PSK31/JS8) работают в USB на всех диапазонах, при входе — DIGU; DIGL — повторным нажатием.

## 295. Сборка 331 — повторное нажатие SSB/DIG снова меняет боковую

Оператор: «при повторном нажатии не меняется полоса; и при изменении моды по кнопкам как будто заново вся панорама пересчитывается».

**1. Повторное нажатие.** Ошибка из основной ветки AetherSDR: кнопки режимов — переключатели, нажатие на активную приходит как «отжата»; обработчик снова её нажимал и выходил, так что переключение USB↔LSB / DIGU↔DIGL в назначениях SSB и DIG не выполнялось никогда. Теперь для SSB и DIG нажатие на активную кнопку проходит дальше и меняет боковую; остальные кнопки как прежде.

**2. «Пересчёт панорамы».** Лог aethersdr-20261112-213921.log: на каждой смене CW↔SSB — «waterfall texture resized 1475x551 -> 1475x503» и обратно. Это панель расшифровки CW (`PanadapterApplet::m_cwPanel`, `MainWindow::refreshCwDecodeState`): при включённом декодере она показывается под панорамой только в CW, панорама меняет высоту, водопад пересоздаёт текстуру и перерисовывается. Приёмник не перестраивается. Вариант решения — на выбор оператора.

## 296. Сборка 332 — усреднение спектра сбрасывается при любом сдвиге NCO

Оператор: «при смене моды кнопками и при перетаскивании панорамы — перерасчёты панорамы; раньше такого не было, сломалось после переделки CTUN для TCI».

**Разбор.**
- Смена режима: в логе на каждом CW↔SSB — «waterfall texture resized 1475x551 -> 1475x503» и обратно. То же было уже в 20:14:16 на сборке 323 (до работы над CTUN): «modulation:0,cwr» → «551 -> 503». Это панель расшифровки CW (`PanadapterApplet::m_cwPanel`, `refreshCwDecodeState`; приём декодера включён по умолчанию, `CwDecodeSettings::rxEnabled()` = True, высота 80 px): при входе в CW она выезжает под панорамой, панорама меняет высоту, водопад пересоздаёт текстуру. Не регрессия CTUN.
- Перетаскивание: код `setPanCenter`/`setSliceFrequency` при выключенном CTUN OFF совпадает со сборкой 323 (diff a565bdf..HEAD — только ветки ctunOff и комментарий). «Отказов» при перетаскивании до сборки 325 в логах не было, так что изменение `IRadioBackend::requestPanDisplay` (325) на перетаскивание не влияет. Видимый «пересчёт» — давний изъян сборки 209: при сдвиге NCO меньше половины полосы (обычный случай при перетаскивании и при досадке `settlePanViews` через 400 мс) усреднение спектра не сбрасывалось, и кадры старого NCO рисовались на новой шкале.

**Исправление.** `Hl2Backend::moveNcoForView`: `dropPanAverage` при любом сдвиге NCO (было — только при скачке больше половины полосы). Мягкий режим (вид внутри захваченной полосы, NCO — только когда нужно) не меняется.

Открыт вопрос оператору: что делать с панелью CW при смене режима.

## 297. Сборка 333 — панель расшифровки CW поверх водопада

Оператор выбрал вариант (б): панель CW не должна менять высоту панорамы при входе в CW и выходе из него (это пересоздавало текстуру водопада, сборка 332, раздел 296).

**Сделано (PanadapterApplet).** `m_cwPanel` больше не в вертикальной раскладке тела панорамы и без `setFixedHeight`: `placeCwPanel()` кладёт его на низ области спектра — ширина спектра, сохранённая высота (`CwDecodeSettings::panelHeight`, не больше высоты спектра минус 60 px), поверх (raise). Позиция обновляется при изменении размера и положения спектра (`eventFilter`), при показе (`setCwPanelVisible`) и при перетаскивании ручки (`setCwPanelHeight`). Высота панорамы и водопада при смене режима не меняется; нижние строки водопада закрыты панелью, пока приёмник в CW. Ручка изменения высоты, шрифт, контекстное меню — как прежде.

## 298. Сборка 334 — откат сброса усреднения при каждом сдвиге NCO

Оператор: «при перетаскивании панорамы вижу редкие ненужные пересчёты FFT — убрать, чтобы было как раньше». Это сборка 332 (`dropPanAverage` при любом сдвиге NCO в `moveNcoForView`). Возвращено прежнее условие: сброс только при скачке больше половины полосы. `Hl2Backend.cpp` теперь отличается от сборки 323 только ветками CTUN OFF (сборка 326), которые без галки TCI не работают.

Лог aethersdr-20261112-215508.log подтверждает сборку 333: при переходе в CW (21:55:42, фильтр 500 Гц) текстура водопада больше не пересоздаётся.

Кнопки режимов: переключение боковой повторным нажатием есть только у кнопок, назначенных на SSB или DIG (по умолчанию ни одна так не назначена); уточняется у оператора, какую кнопку он нажимает.

## 299. Сборка 335 — шумовая полка берёт только накопленные бины

**Жалоба (12.11):** при перетаскивании панорамы «мельтешат» пики бинов; раньше этого не было. Оператор: «При дорисовке части панорамы расчёт шумовой полки берёт от неё данные и ошибается, потому что данные ещё не накоплены. Надо, чтобы брались только накопленные данные, как и все остальные».

**Причина.** При сдвиге NCO `Hl2Spectrum::shiftBins(k)` сдвигает усреднённый массив `m_avgPower` и заполняет открывшийся край значением крайнего бина. В этих бинах среднего ещё нет — одна копия. Шумовая полка `ananNoiseLineStep` (20-й перцентиль видимых бинов) брала их наравне с остальными. Отсюда скачки полки, а за ней — зелёной линии, Auto AGC, AUTO ATT и AGC-гейта; на экране это выглядело как мельтешение пиков.

**Исправление.**
- `Hl2Spectrum`: диапазон «ненакопленных» бинов `[m_freshLo, m_freshHi)` и момент их созревания `m_freshUntil` (по счётчику отсчётов `m_clockSamples`). При каждом `shiftBins` прежний диапазон сдвигается на −k и объединяется с новым краем. Время созревания — 3τ усреднения (не меньше 0,3 с); при усреднении по кадрам — 3·N·fftSize/rate. `dropAverage` диапазон сбрасывает. Добавлен доступ `unsettledBins(lo, hi)`.
- `Hl2RxDsp`: перед `emit spectrumReady` диапазон кладётся в атомик `m_unsettled`; публичный `unsettledBins()` читается из любого потока. Сигнатура `spectrumReady` не менялась (на неё завязаны тесты).
- `Hl2Backend::ananNoiseLineStep`: новые параметры `unsettledLo/Hi`. Ненакопленный край отрезается с той стороны окна, которую он перекрывает; если перекрыта середина — остаётся бо́льшая часть. Если осталось ≤16 бинов, кадр полку не даёт и держится прежнее значение. Лямбда `spectrumReady` передаёт `producer->unsettledBins()`.

Сам спектр и водопад не менялись: край по-прежнему дорисовывается сразу, исключён он только из статистики.

Файлы: ForkBuild.h, Hl2Spectrum.h/.cpp, Hl2RxDsp.h/.cpp, Hl2Backend.h/.cpp.

## 300. Сборка 336 — смена боковой повторным кликом на обычных кнопках модуляции

**Запрос (12.11):** переключение SSB/DIG работало только на кнопках с нейтральными значениями «SSB»/«DIG», и это неочевидно. Оператор: убрать нейтральные значения из выбора и сделать такое же переключение на обычных кнопках с указанной боковой.

**Изменения (VfoWidget):**
- Из меню по ПКМ убраны «SSB» и «DIG». Сохранённые ранее значения при загрузке превращаются в USB и DIGU.
- Повторный клик по активной кнопке переключает боковую полосу: USB↔LSB, DIGU↔DIGL, CW(CWU)↔CWL. Пара берётся из `sidebandPartner()`; если радио не предлагает парный режим, клик ничего не делает. На остальных режимах повторный клик, как и раньше, ничего не меняет.
- Подсветка следует за режимом приёмника. Если после переключения у нового режима есть своя кнопка (по умолчанию есть LSB и USB), горит она. Если своей кнопки нет, остаётся гореть нажатая кнопка и на ней написан текущий режим, поэтому следующий клик возвращает исходную боковую.
- Подсказка кнопки обновляется при смене назначения; для режимов с боковой в ней сказано про повторный клик.
- Удалены ветки SSB/DIG из логики клика и доступности; `ssbSidebandFor` в VfoWidget больше не используется (в MainWindow_DigitalModes остаётся).

Файлы: ForkBuild.h, VfoWidget.h/.cpp.

## 301. Релиз build 336 подготовлен (12.11.2026)

- docs/release-notes-build-336.md: сборки 303–336 по темам (TCI и CW Skimmer, кнопки режимов во флаге VFO, панорама, звук приёмника и DRM, трансвертеры, MIDI, процессор и память), с тем, что не проверено на радио. README.md: раздел «Build 302» заменён на «Build 336», build 302 добавлен в «Earlier notes».
- Архив AetherSDR-ANAN-build336-win64-portable.zip: архив build 302 с exe сборки 336 (Desktop\AetherSDR-Release\AetherSDR.exe, 12.11 22:14 местного, 77766656 байт, содержит «click again: the other sideband», «Center Lock when I/Q», «VFO B Tune Knob»), README-build336.md вместо README-build302.md; верхняя папка AetherSDR-ANAN-build336; 90 записей, 156055255 байт, SHA-256 58660596628ca947fa81bc8cf9e5ba695e445cfa627b32946610a5306c2df8a5. Сохранён в Desktop\AetherSDR-Release.
- Релиз (тег build-336 на main) создаёт оператор в веб-интерфейсе GitHub; пуш тега запускает сборку AppImage.

## 302. Сборка 337 — FreeDV: фильтр по ширине подмоды, по центру сигнала, флаг над фильтром

**Запрос (13.11):** в режиме FreeDV показывался обычный фильтр SSB, ширина которого не соответствует спектру сигнала. Нужно: без линии нулевой частоты, флаг симметрично над фильтром, ширина по подмоде, центр фильтра — центр настройки на сигнал. Образец — MicronSDR, но с нижней боковой ниже 10 МГц (у нас уже сделано в 310).

**MicronSDR** (`micronsdr.cpp` fdvBand, OnPitch; `Panarama.cpp`): приёмный фильтр — полоса подмоды с центром 1500 Гц от шкалы, плюс 100 Гц с каждой стороны на настройку; ширины модема: RADE 1500, 1600 — 1250, 700D — 1000, 700E — 1500 Гц. Кнопки фильтра в FDV заблокированы. «Pitch» = 1500 Гц: клик по панораме ставит на место клика центр сигнала. Всегда USB.

**Сделано.**
- `FdvState.h`: `fdvRxBandHz(subMode, lo, hi)` — те же ширины, центр `kFdvCenterHz` = 1500 Гц.
- `MainWindow::applyLocalFdvPassband()` (из тика 50 мс и при включении): фильтр приёмника = полоса подмоды на действующей боковой (USB: +lo…+hi; LSB: −hi…−lo). Смена подмоды, боковой, диапазона или сдвиг края мышью исправляются сразу — как в MicronSDR, фильтр в FDV не меняется оператором. Команда уходит один раз на изменение, повтор через 500 мс, если радио её не приняло.
- При выходе из FDV в USB/LSB возвращается SSB-фильтр, который был до включения (если входили не из SSB — 100…2900 Гц). При уходе в другой режим его полосу ставит бэкенд.
- `SpectrumWidget`: в оверлее слайса поле `fdvCenterHz` (±1500 Гц; `setSliceOverlayFdvCenter`). При нём: линия несущей и треугольник не рисуются; флаг VFO стоит по центру над фильтром (`repositionVfoFlags`: LockLeft от точки «центр + полширины флага»); одиночный и двойной клик настраивают так, что центр фильтра встаёт на место клика (`clickDialMhz`). Клик по споту не смещается: споты FreeDV дают частоту шкалы.
- Частота на флаге и в CAT — по-прежнему частота шкалы SSB (подавленная несущая), как принято для FreeDV.

Не менялось: фильтр передачи. На радио не проверено.

Файлы: ForkBuild.h, FdvState.h, SpectrumWidget.h/.cpp, MainWindow.h, MainWindow_DigitalModes.cpp.

## 303. Сборка 338 — FreeDV: поле принятого короткого текста под флагом

**Запрос (13.11):** добавить поле для приёма короткого текста FreeDV, прикрепить к низу флага; при раскрытии опций флага поле должно оставаться снизу.

**MicronSDR** (`micronsdr.cpp` ~10260, `fdv_api.h`): короткий текст — это позывной, до 8 символов: у RADE он приходит в кадре конца передачи (EOO), у 1600/700D/700E — «reliable text» в текстовом канале; libfdv отдаёт его в `fdv_status().callsign` со счётчиком `callsignSerial`. MicronSDR показывает его на S-метре. Свободный текст (до 60 символов, `fdv_read_text`) — отдельная панель, здесь не делался.

**У нас** приём уже был: `Hl2Backend::fdvTick` → `FdvState::rxCallsign` → `VfoWidget::setRadeCallsign`, но позывной показывался мелкой меткой в строке сведений флага и только после приёма.

**Сделано (VfoWidget).** Полоса высотой 20 px под флагом, на всю его ширину: `m_fdvTextLabel` — соседний с флагом виджет (как тень и кнопки закрытия), а не часть его раскладки. Положение ставит `syncFdvTextGeometry()` из перемещения, изменения размера и показа флага, поэтому при раскрытии вкладок полоса остаётся у нижнего края. Видна, пока включён FreeDV и флаг развёрнут; пустая показывает серое «RX text», принятый позывной — зелёным. Очищается при новом захвате синхронизации (как раньше метка). Щелчки мыши сквозь полосу проходят к панораме. Прежняя метка в строке сведений больше не показывается.

Файлы: ForkBuild.h, VfoWidget.h/.cpp.

## 304. Сборка 339 — FreeDV: фильтр передачи по подмоде, как в MicronSDR

**Запрос (13.11):** фильтр передачи в FreeDV сделать как в MicronSDR; там значения не совпали с документацией и подбирались измерением — взять их же.

**Было.** В FreeDV модулятор работал с голосовой полосой SSB (300…2700 Гц по умолчанию или полоса оператора из Phone-аплета). Она срезала края сигнала модема — у RADE он занимает 100…3000 Гц.

**MicronSDR** (`micronsdr.cpp` fdvTxBand): полоса всего сигнала модема с его скатами, измеренная (выход модема −25 дБ на краях; RADE: 99 % мощности в 100…3070 Гц): RADE 100…3000, 1600 — 700…2300, 700D — 600…2400, 700E — 400…2600 Гц.

**Сделано.**
- `FdvState.h`: `fdvTxBandHz(subMode, lo, hi)` — эта таблица.
- `Hl2Backend::effectiveTxPassband`: если FreeDV включён на передающем приёмнике (USB/LSB), возвращается полоса подмоды — раньше полосы оператора и полосы режима.
- `Hl2Backend::fdvTick`: при включении и выключении FreeDV на передающем приёмнике и при смене подмоды (`m_fdvTxBandKey`) вызывается `pushTxPassband` — модулятор получает новую полосу, Phone-аплет её показывает. При выходе из FreeDV возвращается полоса оператора или режима. Смена боковой и перенос TX на другой приёмник шли через `pushTxPassband` и раньше.

Полоса оператора (`m_txFilterLowHz/HighHz`) не затирается. На радио не проверено.

Файлы: ForkBuild.h, FdvState.h, Hl2Backend.h/.cpp.

## 305. Сборка 340 — FDV как отдельный режим; клик вне раскрытого флага только сворачивает его

**1. Жалоба (13.11):** «в режиме FDV не могу переключиться кнопкой на SSB — оно думает, что и так в SSB. Добавь FDV как отдельную моду».

**Причина.** FreeDV работает поверх USB/LSB приёмника, и интерфейс показывал режим приёмника: во флаге горела кнопка USB (или LSB), а кнопка FDV не горела никогда (`active = cur == assign`). Нажатие на горящую кнопку — это «повторный клик по активному режиму»: до 336 он ничего не делал, с 336 менял боковую, оставляя FDV включённым. В RX-аплете список режимов показывал USB, и выбор USB не был изменением.

**Исправлено.**
- `VfoWidget::fdvShown()`. Пока FreeDV включён на приёмнике флага: горит только кнопка FDV; USB/LSB не горят, и нажатие на них выключает FreeDV и ставит эту боковую (обычный путь: `radeActivated(false)` + `setMode`). Повторный клик по горящей FDV ничего не делает (боковую не меняет). Подпись вкладки режима под частотой — «FDV». `setRadeActive` обновляет кнопки и подпись.
- Кнопка FDV добавлена в набор по умолчанию: второй ряд CWL, DIGL, DIGU, **FDV** (вместо SAM). Назначенные оператором кнопки не меняются.
- `RxApplet::refreshModeDisplay()`: список режимов показывает FDV, пока FreeDV включён на его приёмнике (`FdvState::sliceId`), поэтому выбор USB/LSB — изменение и выключает FreeDV. Вызывается при привязке приёмника, смене режима, пересборке списка и из `activateLocalFdv` / `deactivateLocalFdv`.

**2. Запрос:** клик где угодно вне раскрытого окна панели частоты (флага) сворачивает его и больше ничего не делает — как для кнопок боковой панели панорамы (сборка 249).

**Сделано.** `VfoWidget::foldOnOutsidePress` — та же схема, что `SpectrumOverlayMenu::closeSubPanelsOnOutsidePress`: фильтр событий приложения, решение на нажатии уровня окна, до любого виджета. Если у флага открыта вкладка (DSP / режим / RIT / DAX) или выбор измерителя и нажатие пришлось вне прямоугольника флага в том же окне — вкладка закрывается, нажатие съедается. Нажатия в других окнах верхнего уровня (выпадающий список или меню, открытые из флага, диалоги) не трогаются.

Файлы: ForkBuild.h, VfoWidget.h/.cpp, RxApplet.h/.cpp, MainWindow_DigitalModes.cpp.

## 306. Сборка 341 — курсор не «просвечивает» сквозь флаг; в FDV края фильтра не тянутся; SNR слева от частоты

**Запрос (13.11, снимок флага в FDV):**
1. указатель мыши сквозь флаг иногда «наводится» на края фильтра и становится стрелкой влево-вправо — баг, убрать во всех режимах;
2. в FDV перестройка фильтра должна быть исключена, указатель на края не наводится;
3. во флаге в FDV: индикатор уровня ближе к частоте, SNR — выше, слева от частоты, в виде «SNR: __._ dB»; в остальных режимах как было.

**1. Причина.** `SpectrumWidget::setVfoCursorOverride` ставил курсор слайса (изменение размера / рука) самим флагам и всем их дочерним виджетам каждый раз, когда указатель был над краем или телом фильтра — в том числе когда край лежит ПОД флагом (опрос позиции в `renderGpuFrame`, обработчик движения, фильтр событий флага). Флаг показывал курсор действия, которое кликом по нему сделать нельзя. Теперь `setVfoCursorOverride` только снимает такие подмены: у флага всегда собственный курсор.

**2. FDV.** В оверлее с `fdvCenterHz != 0` края фильтра не считаются ручками: четыре места с `filterEdgeHitAtPixel` (курсор для активного и неактивного слайса, нажатие для активного и неактивного) дают 0; вся полоса, включая зоны у краёв, — тело фильтра (перетаскивание приёмника).

**3. Флаг в FDV (VfoWidget).** Строка сведений между частотой и измерителем (`m_radeInfoRow`: SNR, сдвиг частоты) больше не показывается — измеритель стоит сразу под частотой. Слева от частоты — значок `m_radeStatusLabel`: точка синхронизации и «SNR: 12.3 dB» (без синхронизации «SNR: --.- dB» серым; ниже 5 дБ жёлтым, иначе зелёным), `updateFdvBadge()`. Название подмоды — в подсказке значка и в аплете FDV. Попутно: значок был пуст до первой синхронизации (текст ставился только при смене состояния синхронизации) — теперь ставится при включении.

Файлы: ForkBuild.h, VfoWidget.h/.cpp, SpectrumWidget.cpp.

## 307. Сборка 342 — FDV: расстройка второй строкой под SNR; поле RX text в полной рамке

**Запрос (13.11):** строку SNR поднять выше, вторую строку тем же шрифтом отдать расстройке; поле «RX text» обвести рамкой полностью (и сверху), фон — как у окна флага.

**Сделано (VfoWidget).**
- Значок слева от частоты (`m_radeStatusLabel`) — высотой со строку частоты (30 px), текст от верха, две строки: «● SNR: 12.3 dB» и «Offset: +125 Hz». Вторая строка есть только при синхронизации и когда модем отдаёт расстройку (RADE, `setRadeFreqOffset` → `m_fdvOffsHz`); сбрасывается при потере синхронизации и смене подмоды.
- Полоса «RX text»: рамка со всех четырёх сторон, фон `#0a0a14` — тот же, которым `paintEvent` заливает флаг.

Расстройка — оценка модема, на сколько герц сигнал отстоит от настройки (`fdv_status().freqOffsetHz`); на декодирование не влияет, это индикатор точности настройки.

Файлы: ForkBuild.h, VfoWidget.h/.cpp.

## 308. Сборка 343 — FreeDV, передача: микрофон через свою обработку в кодер, выход кодера прямо на модулятор

**Замечание (13.11):** «в пути передачи FDV напутано: выход кодера идёт через микрофон на модулятор. Микрофон должен идти через обработку микрофона, а выход кодера — прямо на модулятор. Уровни, может, надо согласовать — посмотри, как в MicronSDR».

**Как было.**
- Микрофон ПК: микрофон → libfdv → усиление MIC (`Hl2TxDsp`, источник Microphone) → TXA: компрессор 10…20 дБ и левелер +10 дБ (включены всегда, `pushTxCompressor`) → модулятор.
- Микрофон радио: микрофон → MIC → libfdv → тот же компрессор и левелер TXA → модулятор.

То есть речевые регуляторы работали по сигналу модема (компрессор и левелер искажают OFDM), а кодер получал необработанный микрофон.

**MicronSDR** (`DspUnit::SetMode`, `txFdv`): в FDV сигнал модема идёт через TX DSP как DIGU «без речевой обработки (EQ, левелер, CPDR, COMP, gate) — ALC остаётся». Уровень: выход libfdv сделан с пиками около полной шкалы под ALC передатчика (`fdv_engine.cpp`: RADE_TX_GAIN 0.75, C2_TX_GAIN 1/16384) — дополнительного усиления нет.

**Стало.** микрофон → MIC → речевой компрессор → libfdv → модулятор.
- `Hl2Backend::fdvSpeechProcess`: компрессор PROC (`m_codecComp`, пресеты NOR / DX / DX+ по выбору, при выключенном PROC — NOR) с ограничителем −1 dBFS, перед кодером; сбрасывается в начале каждой передачи. Вызывается для микрофона ПК (`submitTxAudio`) и микрофона радио (`onFdvTx`).
- Микрофон ПК: усиление MIC применяется до кодера; после кодера блок помечается EngineGenerated — MIC на сигнал модема не действует (у микрофона радио так и было).
- `pushTxCompressor`: компрессор и левелер TXA выключены, пока FreeDV включён на передающем приёмнике (`m_fdvTxBandKey >= 0`); `fdvTick` вызывает его при включении/выключении FreeDV. ALC TXA остаётся, как в MicronSDR.
- Уровень сигнала модема на модулятор — как отдаёт libfdv, без добавок.

На радио не проверено: уровень речи на входе кодера и выходная мощность в FDV.

Файлы: ForkBuild.h, Hl2Backend.h/.cpp.

## 309. Сборка 344 — панорама передачи: разрешение в 4 раза выше (2049 точек на ±12 кГц)

**Запрос (13.11):** панорама передачи с очень низким горизонтальным разрешением; для приёма уже увеличивали, сделать максимально возможное и для передачи. Проверить: ширина для Protocol 1 — 48 кГц, для Protocol 2 — 192 кГц, отображаемая ширина должна быть одинакова.

**Проверка ширины.** Панораму передачи считает `anan::TxScope` из IQ передатчика. Оба протокола подают в него один и тот же поток 192 кГц: Protocol 2 — `P2Transport::accountTxSamples` из пакета DUC, Protocol 1 — та же `accountTxSamples` ДО дециматора 192→48 кГц (`m_p1TxDec`, отправка EP2). Показ — всегда центр ±12 кГц (`SpectrumWidget::enterTxZoom`, `kTxZoomSpanMhz` = 0,024). Ширина и разрешение на обоих протоколах одинаковы; менять нечего.

**Разрешение.** Было: БПФ 4096 точек при 192 кГц — бин 46,9 Гц, 513 точек на ±12 кГц, втрое меньше пикселей панорамы на весь экран. Стало: БПФ 16384 точки — бин 11,7 Гц, 2049 точек (точка на пиксель или мельче на любой панораме). Перекрытие 75 %, кадр — два БПФ: частота кадров прежняя, ~23 в секунду. Окно Блэкмана–Харриса и нормировка те же: тон полной шкалы — 0 dBFS.

**Стенд** (`scratchpad/txs.cpp`): тон +1500 Гц — 2049 бинов, пик в бине +1500,0 Гц, 0,00 dBFS, 45 кадров за 2 с; расчёт ~1,2 мс на БПФ (здесь), ~6 % одного ядра только во время передачи.

**К сведению.** Бин вчетверо уже, поэтому широкополосный сигнал (речь, FreeDV) читается на бин на 6 дБ ниже, чем раньше; тон (TUNE, CW) — как был. Окно БПФ — 85 мс вместо 21 мс: первый кадр после нажатия приходит примерно через 0,1 с.

Файлы: ForkBuild.h, AnanTxScope.h.

## 310. Сборка 345 — шкала панорамы передачи: верх 0 дБ = полная шкала на передатчик

**Замечание (13.11)** к сборке 344: уровень на панораме передачи стал ниже — плохо; чтобы оценивать уровень, шкале нужна опора: «может, верх 0 дБ — полная шкала, уходящая в передатчик?»

**Сделано.** Фиксированная шкала на передаче (`SpectrumWidget::setTransmitting`, `m_txFixedRange`): 0…−80 dBFS вместо +10…−60. Верхний край — полная шкала отсчётов, уходящих в радио: тон полной шкалы (TUNE, CW) доходит ровно до него, выше ничего быть не может.

**К сведению.** Спектр показывает мощность в полосе одного бина (11,7 Гц). Тон целиком попадает в бин и читается своим полным уровнем. Речь и FreeDV распределяют мощность по сотням бинов, поэтому на бин приходится на десятки дБ меньше полной шкалы, даже когда огибающая доходит до неё; так было и до 344, на 6 дБ выше. Полный уровень широкополосного сигнала по спектру не читается — его показывают измерители ALC/мощности; при желании можно добавить на панораму линию пика огибающей в той же шкале.

Файлы: ForkBuild.h, SpectrumWidget.h/.cpp.

## 311. Сборка 346 — панорама передачи: разрешение как у приёма, FFT AVG приёмника, линия раскачки передатчика

**Запросы (13.11):**
1. нужен инструмент, показывающий, насколько раскачивается передатчик — он всегда должен раскачиваться по максимуму, мощность меняется другим способом;
2. разрешение панорамы передачи — такое же, какое было бы в подобном виде на приёме;
3. настройки FFT приёмника применять и к передаче.

**1. Линия раскачки.** `anan::TxScope` считает пик огибающей |x| отсчётов, ушедших в радио с предыдущего кадра, и отдаёт его последним значением кадра (после бинов; `kBins + 1` значений — сигнатуры сигналов не менялись). `SpectrumWidget::setTxScopeFrame` отделяет его, держит максимум за секунду (дальше спад 1 дБ на кадр); `drawTxPeakLine` рисует пунктир на шкале передачи и подпись «TX drive peak −0.3 dB». 0 дБ (верх шкалы) — огибающая тона полной шкалы: столько шлёт TUNE и столько пропускает ALC. Зелёная в пределах 1 дБ от полной шкалы, янтарная ниже. Спектр этого показать не может: широкополосный сигнал на бин читается на десятки дБ ниже полной шкалы.

**2. Разрешение.** Правило приёма (`Hl2Backend::spectrumFftSizeFor`: около 4096 бинов на вид) для вида ±12 кГц даёт бин 5,86 Гц. Сделано так: децимация 192→48 кГц (КИХ 96 отводов, окно Блэкмана–Харриса, ровно до ±16 кГц, зеркала ниже −90 дБ) и БПФ 8192 — те же бины, что БПФ 32768 по потоку 192 кГц, при малой доле работы (расчёт идёт в потоке передачи). 4097 бинов (было 2049 в 344, 513 до неё), кадр на каждое БПФ при перекрытии 75 % — ~23 кадра/с.

**3. Настройки FFT.** FFT Smooth (сглаживание по частоте, `shapeFftTrace`) применяется при отрисовке любого кадра, в том числе передачи, — так было и раньше. FFT AVG (усреднение по времени) на передаче не действовал: стояло фиксированное сглаживание 0,5 между кадрами. Теперь `TxScope::setAverageMs` — постоянная времени FFT AVG панорамы передающего приёмника, закон как в `Hl2Spectrum` (alpha = 1 − exp(−dt/τ)), 0 = без усреднения. Передаётся из `Hl2Backend::fdvTick` (тик 50 мс) через `P2Transport::setTxScopeAverageMs`.

**Стенд** (`scratchpad/txs.cpp`): тон +1500 Гц — пик точно на +1500,0 Гц, −0,000 dBFS, пик огибающей 0,00 dBFS; несущая −6 дБ — −6,021 и −6,02; тон у края −11,9 кГц — −0,015 dBFS; тоны +40 и +60 кГц (зеркала) — −113 и −133 dBFS; 43 кадра за 2 с; ~2 % одного ядра (было ~6 % у БПФ 16384 сборки 344).

На радио не проверено.

Файлы: ForkBuild.h, AnanTxScope.h/.cpp, P2Transport.h/.cpp, MetisClient.h (комментарий), Hl2Backend.cpp, SpectrumWidget.h/.cpp.

## 312. Сборка 347 — боковое меню панорамы скрыто на время передачи; заметка о CORDIC передатчика

**Запрос (13.11):** подпись линии раскачки закрывается кнопками левой панели панорамы; скрывать их на время передачи — пользоваться ими на передаче никто не будет.

**Сделано.** `SpectrumWidget::enterTxZoom`: открытое окно бокового меню закрывается (`SpectrumOverlayMenu::closePanels`), само меню скрывается; `exitTxZoom` возвращает его. Только на панораме, которая показывает передачу (в DUP панорамы о передаче не знают — меню остаётся).

**Вопрос оператора:** в передатчике трансивера стоит CORDIC, его коэффициент передачи не равен 1 — не вносит ли раскачка до полной шкалы искажения?

**Разбор по `Odyssey_3_2.0_ANP/Sources/Odyssey.v`** (исходника `cpl_cordic` и `CicInterpM5` в папке нет):
- CORDIC — линейное звено: постоянное усиление K ≈ 1,6468 при любой амплитуде и фазе. Сам по себе он не искажает; вопрос только в запасе по разрядности после него.
- Тракт: IQ 24 бита → `CicInterpM5 #(RRRR 640, IBITS 24, OBITS 17, GBITS 38)` → `cpl_cordic #(IN_WIDTH 17)` → `C122_cordic_i_out[21:0]` → слово ЦАП `{~out[21], out[20:8]}`.
- Усиление интерполятора: 640⁴ / 2³⁸ = 0,6104 — это и есть компенсация CORDIC (1/1,6468 = 0,6072). Произведение 0,6104 × 1,6468 = 1,0052 (+0,045 дБ).
- Вывод: тон полной шкалы |x| = 1 даёт на выходе CORDIC на 0,5 % больше полной шкалы 22-битного слова, если принять, что вход 17 бит сдвигается на 5 «лишних» бит (как в стандартном `cpl_cordic` HPSDR: WR = IN_WIDTH + EXTRA_BITS + 1). Если выход модуля 23 бита, а провод 22 — старший бит теряется, и вершины синусоиды выше 0,995 полной шкалы «заворачиваются». Если в модуле есть запас или насыщение — проблемы нет.
- Что проверить в исходниках: в `cpl_cordic.v` — WR / OUT_WIDTH при IN_WIDTH = 17 и нет ли усечения при подключении к 22-битному проводу; в `CicInterpM5` — какие именно биты берутся на выход; есть ли насыщение перед ЦАП. Комментарий в Odyssey.v «overall cordic gain is Sqrt(2)*1.647 = 2.33» — худший случай I = Q = полная шкала (угол квадрата), его программа не посылает: ALC держит огибающую ≤ 1.
- Программа шлёт TUNE с амплитудой 1,0 (`kHl2CarrierAmplitude`) → 8388607 в 24 битах, речь — до потолка ALC 1,0.
- Если переполнение подтвердится — лечится либо в прошивке (насыщение / лишний бит), либо в программе потолком 0,99 (−0,09 дБ).

Файлы: ForkBuild.h, SpectrumOverlayMenu.h, SpectrumWidget.h/.cpp.

## 313. Тракт передачи прошивки Odyssey-3 2.0 ANP: переполнение на полной шкале (разбор, без сборки)

**Вопрос оператора (13.11):** можно ли раскачивать шкалу передатчика на полную? Исходники: `Desktop\Odyssey_3_2.0_ANP\Sources` (`Odyssey.v`, `Polyphase_FIR\CicInterpM5.v`, `cpl_cordic.v`), отчёт синтеза `Odyssey.map.rpt`.

**Тракт.** IQ 24 бита (порт 1029) → FIFO → `CicInterpM5 #(640, IBITS 24, OBITS 17, GBITS 38)` → `cpl_cordic #(IN_WIDTH 17)` → `wire signed [21:0] C122_cordic_i_out` → слово ЦАП `{~out[21], out[20:8]}` (14 бит).

- **Интерполятор:** усиление 640⁴ / 2⁴⁵ = 625/131072; полная шкала 24 бит (8388607) → ровно 40000 на 17-битном выходе (0,6104 шкалы).
- **CORDIC:** вход × 32 (5 «лишних» бит), регистры 24 бита, усиление 1,64676 (стадия 0 — поворот на π/4 с √2 — и есть нулевая итерация; комментарий «Sqrt(2)*1.647 = 2.33» в исходнике неверен). Проверено побитовой моделью модуля (`scratchpad/cordic_sim.py`).
- **Выход:** 40000 × 32 × 1,64676 = 2 107 853 при пределе 22-битного слова 2 097 152 — на 0,51 % (+0,044 дБ) больше.
- **Усечение:** выход модуля 24 бита, провод 22. Quartus это подтверждает: «Output port (24 bits) is wider than the port expression (22 bits); bits out_data_I[23..22] have no fanouts», OUT_WIDTH = 24. Насыщения нет — значения выше 2²¹ заворачиваются в противоположный знак.

**Модель слова ЦАП за период несущей (равномерные фазы):**

| Амплитуда IQ | Завёрнуто отсчётов ЦАП | Основная гармоника | Всё остальное |
|---|---|---|---|
| 1,0000 | 6,40 % | −2,50 дБ от шкалы ЦАП | −1,0 дБн |
| 0,9990 | 5,71 % | −2,20 дБ | −1,7 дБн |
| 0,9970 | 4,15 % | −1,55 дБ | −3,6 дБн |
| 0,9949 | 0 | 0,00 дБ | −80 дБн |
| 0,9900 | 0 | −0,04 дБ | −80 дБн |

**Вывод.** CORDIC линеен; искажает не он, а переполнение 22-битного слова без насыщения. До 0,9949 полной шкалы тракт чист (−80 дБн — шум квантования 14 бит). Выше — вершины несущей заворачиваются: на ровно полной шкале основная гармоника падает на 2,5 дБ, а мощность побочных почти равна полезной. На TUNE это гармоники несущей и их зеркала относительно 122,88 МГц (у несущей почти не видно, часть режет ФНЧ); на модулированном сигнале — срыв усиления на пиках огибающей выше 0,9949, то есть сплэттер.

Программа сейчас шлёт TUNE с амплитудой 1,0 и речь до потолка ALC 1,0 — в зоне переполнения.

**Лечение.** (а) Программа: потолок огибающей 0,99 (−0,087 дБ) на проводе для обоих протоколов; (б) прошивка: насыщение 24→22 бита перед словом ЦАП (или параметр OUT_WIDTH с округлением от старших бит и пересчёт масштаба). Признак для проверки: после (а) мощность на TUNE должна вырасти примерно на 2,5 дБ.

## 314. Сборка 348 — потолок раскачки 0,99 на проводе (переполнение тракта передачи прошивки)

**Решение оператора (13.11):** прошивку менять нельзя — на стандартной прошивке работают тысячи устройств; ограничивать только в программе. Вопрос: если у CORDIC передача 1,64, почему раскачивать можно до 0,99 — где компенсация?

**Где компенсация.** В интерполяторе перед CORDIC (`CicInterpM5`). Его «сырое» усиление — 640⁴ ≈ 2^37,29, а на выход берутся биты с 45-го (`y5[61:45]`), то есть делится на 2⁴⁵: итог 625/131072 от входа, или 0,6104 шкалы при переходе 24 → 17 бит. 0,6104 ≈ 1/1,638 — почти обратная величина усиления CORDIC 1,6468. Произведение 1,0051: тракт в целом имеет единичную передачу с ошибкой +0,5 %, и эта ошибка — всё превышение. Точной компенсации степенями двойки не получить: 640⁴ не степень двойки, выбран ближайший сдвиг.

**Сделано.** `P2Transport.cpp`, `limitTxDrive()` — на отсчётах, уходящих в провод, для обоих протоколов (Protocol 2 — перед `buildDucIq`; Protocol 1 — после дециматора 192→48, перед `ep2WriteTxIq`): огибающая ограничена единицей (радиально; срабатывает только при сбое — её держит ALC) и умножена на 0,99 (−0,087 дБ). Запас до порога 0,9949 — 0,5 %.

Панорама передачи и линия раскачки берут отсчёты до этого шага: 0 дБ на них — максимум, который шлёт программа (0,99 шкалы провода).

**Ожидаемое на радио** (не проверено): на TUNE основная гармоника вырастет примерно на 2,5 дБ (раньше 6,4 % отсчётов ЦАП заворачивались), пропадут побочные от заворота; на речи — нет срыва на пиках.

Файлы: ForkBuild.h, P2Transport.cpp.

## 5. Где брать удалённый код

Всё удалённое есть в теге `v26.10.4` на GitHub (`aethersdr/AetherSDR`) и в резервной копии
пользователя. Пример: `git show v26.10.4:src/core/backends/icom/IcomCivBackend.cpp`.
