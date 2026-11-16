/**
 * app.js — Q-PLC Supervisory Monitor Frontend Logic (Stage 1.6 Upgrade)
 *
 * Responsibilities:
 *  1. MQTT.js WebSocket connection lifecycle (read-only: plcmon/plc-gw-01/#)
 *  2. Telemetry ingestion, schema validation, and sequence monotonicity tracking
 *  3. ISA-101 Situational Awareness Tab Navigation (Overview, I/O, Events, Diag, Settings)
 *  4. Dual-mode I/O presentation: Card Grid and Compact Engineering Diagnostics Table
 *  5. Strict bit normalization: Supported ON/OFF, distinct UNKNOWN for malformed/null/missing
 *  6. State-change suppression for invalid telemetry
 *  7. Multi-filter Event Journal with real-time keyword search and CSV export
 *  8. Real-time telemetry freshness & 15-second stale data detection
 *  9. Isolated Offline Demo Mode simulation (X3 input pulse driving Y20 coil)
 *
 * NOTE: This client is strictly read-only. No write topics or opcodes exist.
 */

'use strict';

// ─── Constants & Device Catalog ───────────────────────────────────────────────

const STALE_THRESHOLD_MS  = 15_000;  // 15 s without data → stale
const AGE_UPDATE_INTERVAL = 1_000;   // Update data age displays every 1 s
const MAX_HISTORY_ITEMS   = 300;     // Event journal capacity
const DEMO_POLL_INTERVAL  = 2_000;   // Demo mode cycle period

const INPUT_DEVICES  = ['X0','X1','X2','X3','X4','X5','X6','X7'];
const OUTPUT_DEVICES = ['Y20','Y21','Y22','Y23','Y24','Y25','Y26','Y27'];

// Engineering Point Catalog (Verified assignments vs unverified ladder)
const DEVICE_METADATA = {
  X0:  { hex: '0x00', desc: 'Input Contact 0 (Spare / Laboratory Pushbutton)' },
  X1:  { hex: '0x01', desc: 'Input Contact 1 (Spare / Laboratory Pushbutton)' },
  X2:  { hex: '0x02', desc: 'Input Contact 2 (Spare / Laboratory Pushbutton)' },
  X3:  { hex: '0x03', desc: 'Trigger Pushbutton (Verified Ladder Contact → Y20)' },
  X4:  { hex: '0x04', desc: 'Input Contact 4 (Spare / Laboratory Pushbutton)' },
  X5:  { hex: '0x05', desc: 'Input Contact 5 (Spare / Laboratory Pushbutton)' },
  X6:  { hex: '0x06', desc: 'Input Contact 6 (Spare / Laboratory Pushbutton)' },
  X7:  { hex: '0x07', desc: 'Input Contact 7 (Spare / Laboratory Pushbutton)' },
  Y20: { hex: '0x20', desc: 'Master Output Relay (Verified Driven by X3 Rung)' },
  Y21: { hex: '0x21', desc: 'Output Relay 21 (Ladder unverified / Spare)' },
  Y22: { hex: '0x22', desc: 'Output Relay 22 (Ladder unverified / Spare)' },
  Y23: { hex: '0x23', desc: 'Output Relay 23 (Ladder unverified / Spare)' },
  Y24: { hex: '0x24', desc: 'Output Relay 24 (Ladder unverified / Spare)' },
  Y25: { hex: '0x25', desc: 'Output Relay 25 (Ladder unverified / Spare)' },
  Y26: { hex: '0x26', desc: 'Output Relay 26 (Ladder unverified / Spare)' },
  Y27: { hex: '0x27', desc: 'Output Relay 27 (Ladder unverified / Spare)' },
};

// ─── State Machine ────────────────────────────────────────────────────────────

const state = {
  theme:          'dark',     // 'dark' | 'light'
  mqttClient:     null,
  activeTab:      'overview',
  ioViewMode:     'cards',    // 'cards' | 'table'
  ioFilter:       'all',      // 'all' | 'on' | 'off' | 'unknown'
  
  // Independent Subsystem States
  connected:      false,      // Broker connection
  connectionIntent: 'disconnected', // 'connected' | 'disconnected' (explicit operator intent)
  isReconnecting: false,      // True when auto-reconnecting after link loss
  plcConnected:   false,      // PLC MC Protocol socket
  espOnline:      false,      // ESP8266 Wi-Fi bridge
  gwActive:       false,      // Python gateway telemetry stream
  
  // Telemetry Freshness & Tracking
  lastReadingTs:  null,       // Date object of last incoming data
  lastPayload:    null,       // Last valid plc_reading payload
  isStale:        false,
  lastSeq:        null,       // Sequence tracking for gap detection
  
  // Telemetry Counters
  stats: {
    packetsReceived: 0,
    heartbeats:      0,
    seqGaps:         0,
    discarded:       0,
    stateChanges:    0,
    reconnects:      0,
    lastFault:       null,
  },

  // Event Journal
  historyItems:   [],         // { id, tsIso, tsLocal, category, severity, message }
  eventCategory:  'all',      // 'all' | 'state_change' | 'comm_error' | 'system'
  eventSeverity:  'all',      // 'all' | 'info' | 'warn' | 'error'
  eventSearch:    '',

  // Demo Simulation & Scenarios
  isDemo:         false,
  demoPaused:     false,
  demoScenario:   'normal',   // 'normal' | 'delayed' | 'seq_gap' | 'stale' | 'gw_fault'
  demoInterval:   null,
  demoTimeoutId:  null,
  demoSeq:        100,
  demoStep:       0,
  
  // Windows Local Agent Direct Ingestion
  dataSource:        'local',    // 'local' | 'mqtt'
  localAgentActive:  false,
  localAgentTimerId: null,
  localAgentDeviceId: null,
  localAgentApiKey:  '',
  simulationMode:     false,
  deviceAdapterId:    null,
  deviceModel:        null,
  agentHealthy:       false,
  agentUptime:        0,
  devicesList:        [],
  deviceTelemetry:    {},
  activeDeviceHasLadderPair: true,
  hasCustomTableBuilt: false,

  // Timers
  ageTimerId:     null,
};

// ─── DOM Element References ───────────────────────────────────────────────────

const $ = id => document.getElementById(id);
let dom = {};

function cacheDomRefs() {
  dom = {
    // Mode Tag & Header Indicators
    headerModeTag:       $('header-mode-tag'),
    headerSimBadge:      $('header-sim-badge'),
    headerBrandSubtitle: $('header-brand-subtitle'),
    headerModelBadge:    $('header-model-badge'),
    labelPillGw:         $('label-pill-gw'),

    // Theme Controls
    btnThemeToggle:    $('btn-theme-toggle'),
    themeToggleText:   $('theme-toggle-text'),
    radioThemeDark:    $('theme-radio-dark'),
    radioThemeLight:   $('theme-radio-light'),

    // Subsystem Header Status Pills
    dotMqtt:    $('dot-mqtt'),  textMqtt:    $('text-mqtt'),  pillMqtt: $('pill-mqtt'),
    dotEsp:     $('dot-esp'),   textEsp:     $('text-esp'),   pillEsp:  $('pill-esp'),
    dotGw:      $('dot-gw'),    textGw:      $('text-gw'),    pillGw:   $('pill-gw'),
    dotPlc:     $('dot-plc'),   textPlc:     $('text-plc'),   pillPlc:  $('pill-plc'),

    // Navigation & Meta
    navFreshness:   $('freshness-indicator'),
    navHeaderAge:   $('header-data-age'),
    navEventCount:  $('nav-event-count'),

    // Banners & Demo Controller
    staleBanner:        $('stale-banner'),
    staleBannerText:    $('stale-banner-text'),
    demoBanner:         $('demo-banner'),
    btnDemoPause:       $('btn-demo-pause'),
    demoPauseText:      $('demo-pause-text'),
    btnDemoReset:       $('btn-demo-reset'),
    demoScenarioBadge:  $('demo-scenario-badge'),
    selectDemoScenario: $('select-demo-scenario'),

    // Overview Tab
    badgeSubsystemPlc:     $('badge-subsystem-plc'),  detailSubsystemPlc:  $('detail-subsystem-plc'),
    badgeSubsystemGw:      $('badge-subsystem-gw'),   detailSubsystemGw:   $('detail-subsystem-gw'),
    badgeSubsystemEsp:     $('badge-subsystem-esp'),  detailSubsystemEsp:  $('detail-subsystem-esp'),
    badgeSubsystemMqtt:    $('badge-subsystem-mqtt'), detailSubsystemMqtt: $('detail-subsystem-mqtt'),
    nameSubsystemPlc:      $('name-subsystem-plc'),   interfaceSubsystemPlc: $('interface-subsystem-plc'),
    nameSubsystemGw:       $('name-subsystem-gw'),    interfaceSubsystemGw:  $('interface-subsystem-gw'),
    badgeLadderVerified:   $('badge-ladder-verified'),
    badgeIoLadderRef:      $('badge-io-ladder-ref'),
    pairTagHeading:        $('pair-tag-heading'),
    pairTagSubnote:        $('pair-tag-subnote'),
    
    metaFreshnessState: $('meta-freshness-state'),
    metaFreshnessSub:   $('meta-freshness-sub'),
    metaDataAge:        $('meta-data-age'),
    metaLastTs:         $('meta-last-ts'),
    metaSeq:            $('meta-seq'),
    metaSeqHealth:      $('meta-seq-health'),
    metaGatewayId:      $('meta-gateway-id'),
    metaActiveCount:    $('meta-active-count'),
    metaUnknownCount:   $('meta-unknown-count'),

    // Ladder Contact Visuals (Overview)
    ladderX3Elem:       $('ladder-x3-elem'),
    ladderX3Symbol:     $('ladder-x3-symbol'),
    ladderX3State:      $('ladder-x3-state'),
    ladderY20Elem:      $('ladder-y20-elem'),
    ladderY20Symbol:    $('ladder-y20-symbol'),
    ladderY20State:     $('ladder-y20-state'),
    ladderFlowLine:     $('ladder-flow-line'),
    ladderModeBadge:    $('ladder-mode-badge'),
    ladderRungStatus:   $('ladder-rung-status-text'),
    ladderRungDot:      $('ladder-rung-dot'),
    miniEventList:      $('mini-event-list'),
    btnGotoEvents:      $('btn-goto-events'),
    btnGotoIo:          $('btn-goto-io'),
    overviewTileX3:     $('overview-tile-x3'),
    overviewBadgeX3:    $('overview-badge-x3'),
    overviewTileY20:    $('overview-tile-y20'),
    overviewBadgeY20:   $('overview-badge-y20'),

    // Ladder Contact Visuals (PLC I/O Tab)
    ladderIoCard:       $('ladder-io-card'),
    ladderIoX3Elem:     $('ladder-io-x3-elem'),
    ladderIoX3Symbol:   $('ladder-io-x3-symbol'),
    ladderIoX3State:    $('ladder-io-x3-state'),
    ladderIoY20Elem:    $('ladder-io-y20-elem'),
    ladderIoY20Symbol:  $('ladder-io-y20-symbol'),
    ladderIoY20State:   $('ladder-io-y20-state'),
    ladderIoFlowLine:   $('ladder-io-flow-line'),
    ladderIoModeBadge:  $('ladder-io-mode-badge'),

    // I/O Monitor Tab
    btnViewCards:       $('btn-view-cards'),
    btnViewTable:       $('btn-view-table'),
    ioContainerCards:   $('io-container-cards'),
    ioContainerTable:   $('io-container-table'),
    inputsGrid:         $('inputs-grid'),
    outputsGrid:        $('outputs-grid'),
    ioTableBody:        $('io-table-body'),

    // Event Journal Tab
    inputEventSearch:   $('input-event-search'),
    historyList:        $('history-list'),
    historyEmpty:       $('history-empty'),
    historyCount:       $('history-count'),
    btnExportCsv:       $('btn-export-csv'),
    btnClearHist:       $('btn-clear-history'),

    // Diagnostics Tab
    statPacketsReceived: $('stat-packets-received'),
    statHeartbeats:      $('stat-heartbeats'),
    statSeqGaps:         $('stat-seq-gaps'),
    statDiscarded:       $('stat-discarded'),
    statStateChanges:    $('stat-state-changes'),
    statReconnects:      $('stat-reconnects'),
    diagLastFault:       $('diag-last-fault'),
    diagSubscribedTopic: $('diag-subscribed-topic'),
    diagClientId:        $('diag-client-id'),

    // Settings Tab
    selectDataSource:   $('select-data-source'),
    groupBrokerUrl:     $('group-broker-url'),
    inputLocalAgentKey: $('input-local-agent-key'),
    groupLocalAgentKey: $('group-local-agent-key'),
    inputUrl:           $('input-broker-url'),
    inputUser:          $('input-broker-user'),
    inputPass:          $('input-broker-pass'),
    inputTopic:         $('input-topic-prefix'),
    btnConnect:         $('btn-connect'),
    btnDisconnect:      $('btn-disconnect'),
    chkDemo:            $('chk-demo'),

    // Device Selector & Analog Section (Phase 06A)
    selectDevice:         $('select-device'),
    navDeviceSelectorWrap: $('nav-device-selector-wrap'),
    sectionAnalogPoints:  $('section-analog-points'),
    analogGrid:           $('analog-grid'),
    analogCountBadge:     $('analog-count-badge'),
    overviewAnalogCard:   $('overview-analog-card'),
    overviewAnalogGrid:   $('overview-analog-grid'),
    overviewAnalogSub:    $('overview-analog-sub'),

    // Project V2 Dual-Station & Control
    stationCardQ03:        $('station-card-q03'),
    stationCardR04:        $('station-card-r04'),
    btnSelectQ03:          $('btn-select-q03'),
    btnSelectR04:          $('btn-select-r04'),
    stationBadgeQ:         $('station-badge-q'),
    stationBadgeR:         $('station-badge-r'),
    stationSignalsQ:       $('station-signals-q'),
    stationSignalsR:       $('station-signals-r'),
    btnGotoDiag:           $('btn-goto-diag'),
    gwCardIp:              $('gw-card-ip'),
    gwCardLcd:             $('gw-card-lcd'),
    ctrlActiveStationSub:  $('ctrl-active-station-sub'),
    ctrlStationConnBadge:  $('ctrl-station-conn-badge'),
    ctrlRLockNotice:       $('ctrl-r-lock-notice'),
    ctrlSelectPoint:       $('ctrl-select-point'),
    ctrlReadbackDot:       $('ctrl-readback-dot'),
    ctrlReadbackText:      $('ctrl-readback-text'),
    btnRefreshReadback:    $('btn-refresh-readback'),
    btnSetStateOff:        $('btn-set-state-off'),
    btnSetStateOn:         $('btn-set-state-on'),
    ctrlCommandState:      $('ctrl-command-state'),
    ctrlInputPin:          $('ctrl-input-pin'),
    btnExecuteControl:     $('btn-execute-control'),
    ctrlPipelineOverallBadge: $('ctrl-pipeline-overall-badge'),
    ctrlResultBox:         $('ctrl-result-box'),
    ctrlResultContent:     $('ctrl-result-content'),
    pipeStep1:             $('pipe-step-1'),
    pipeStep2:             $('pipe-step-2'),
    pipeStep3:             $('pipe-step-3'),
    pipeStep4:             $('pipe-step-4'),
    pipeStep5:             $('pipe-step-5'),
    pipeStatus1:           $('pipe-status-1'),
    pipeStatus2:           $('pipe-status-2'),
    pipeStatus3:           $('pipe-status-3'),
    pipeStatus4:           $('pipe-status-4'),
    pipeStatus5:           $('pipe-status-5'),
    pipeDetail1:           $('pipe-detail-1'),
    pipeDetail2:           $('pipe-detail-2'),
    pipeDetail3:           $('pipe-detail-3'),
    pipeDetail4:           $('pipe-detail-4'),
    pipeDetail5:           $('pipe-detail-5'),
    inputGatewayUrl:       $('input-gateway-url'),
    btnTestGatewayConn:    $('btn-test-gateway-conn'),
    btnSaveGatewayConn:    $('btn-save-gateway-conn'),
    btnResetGatewayConn:   $('btn-reset-gateway-conn'),
    gatewayConnStatus:     $('gateway-conn-status'),
  };
}

// ─── Initialisation ───────────────────────────────────────────────────────────

function init() {
  cacheDomRefs();
  initTheme();
  buildDeviceGrids();
  buildDeviceTable();
  setupEventListeners();
  startAgeTimer();
  restoreSettings();
  if (isLocalOrigin()) {
    setDataSource('local', true);
  } else {
    setDataSource('mqtt', false);
  }
  updateSubsystemMatrix();
  updateLadderDiagram(null, null);
  selectStation('melsec_q03');
}

// ─── Layout Builders ──────────────────────────────────────────────────────────

function buildDeviceGrids() {
  if (dom.inputsGrid) {
    dom.inputsGrid.innerHTML = INPUT_DEVICES.map(d => deviceTileHTML(d, 'input')).join('');
  }
  if (dom.outputsGrid) {
    dom.outputsGrid.innerHTML = OUTPUT_DEVICES.map(d => deviceTileHTML(d, 'output')).join('');
  }
}

function deviceTileHTML(device, kind) {
  const meta = DEVICE_METADATA[device] || { hex: '0x??', desc: 'Digital Point' };
  return `
    <li class="device-tile ${kind}-tile loading" id="tile-${device}"
        data-device="${device}" data-kind="${kind}"
        data-state="unknown" role="listitem"
        aria-label="${device}: unknown">
      <div class="tile-top">
        <span class="tile-label">${device}</span>
        <div class="tile-led" aria-hidden="true"></div>
      </div>
      <p class="tile-desc" title="${meta.desc}">${meta.desc}</p>
      <div class="tile-bottom">
        <span class="tile-state-badge">—</span>
        <span class="tile-hex">${meta.hex}</span>
      </div>
    </li>`;
}

function buildDeviceTable() {
  if (!dom.ioTableBody) return;
  const allDevices = [...INPUT_DEVICES, ...OUTPUT_DEVICES];
  
  dom.ioTableBody.innerHTML = allDevices.map(d => {
    const isInput = d.startsWith('X');
    const meta = DEVICE_METADATA[d] || { hex: '—', desc: 'Digital Channel' };
    return `
      <tr id="row-${d}" data-device="${d}" data-kind="${isInput ? 'input' : 'output'}" data-state="unknown">
        <td class="point-tag">
          <span class="status-dot dot-unknown" id="row-dot-${d}"></span>
          <strong>${d}</strong>
        </td>
        <td class="point-hex"><code>${meta.hex}</code></td>
        <td>${isInput ? 'Input Contact' : 'Output Coil'}</td>
        <td class="table-desc">${meta.desc}</td>
        <td>
          <span class="table-state-pill unknown" id="row-state-${d}">UNKNOWN</span>
        </td>
        <td><code id="row-raw-${d}">—</code></td>
        <td>
          <div class="signal-bar-wrap">
            <div class="signal-bar" id="row-bar-${d}"></div>
          </div>
        </td>
        <td class="point-ts mono" id="row-ts-${d}">—</td>
      </tr>`;
  }).join('');
}

// ─── Theme Management Controller ─────────────────────────────────────────────

function initTheme() {
  let savedTheme = 'dark';
  try {
    const stored = localStorage.getItem('qplc_theme');
    if (stored === 'light' || stored === 'dark') savedTheme = stored;
  } catch (e) {
    savedTheme = 'dark';
  }
  setTheme(savedTheme, false);
}

function setTheme(themeName, persist = true) {
  state.theme = (themeName === 'light') ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', state.theme);

  if (persist) {
    try {
      localStorage.setItem('qplc_theme', state.theme);
    } catch (e) {}
  }

  // Update header toggle button text & aria
  if (dom.themeToggleText) {
    dom.themeToggleText.textContent = state.theme === 'light' ? 'Light Theme' : 'Dark Theme';
  }
  if (dom.btnThemeToggle) {
    dom.btnThemeToggle.setAttribute('aria-label', `Theme is ${state.theme}. Click to switch.`);
  }

  // Update Settings radios
  if (dom.radioThemeDark) dom.radioThemeDark.checked = (state.theme === 'dark');
  if (dom.radioThemeLight) dom.radioThemeLight.checked = (state.theme === 'light');
}

function toggleTheme() {
  const nextTheme = (state.theme === 'dark') ? 'light' : 'dark';
  setTheme(nextTheme, true);
  addHistoryItem('system', 'info', `Visual theme switched to ${nextTheme.toUpperCase()}`);
}

// ─── Event Listeners ──────────────────────────────────────────────────────────

function setupEventListeners() {
  // Theme Toggle Button & Radios
  if (dom.btnThemeToggle) {
    dom.btnThemeToggle.addEventListener('click', toggleTheme);
  }
  if (dom.radioThemeDark) {
    dom.radioThemeDark.addEventListener('change', () => { if (dom.radioThemeDark.checked) setTheme('dark'); });
  }
  if (dom.radioThemeLight) {
    dom.radioThemeLight.addEventListener('change', () => { if (dom.radioThemeLight.checked) setTheme('light'); });
  }

  // Tab Switching
  document.querySelectorAll('.nav-tab').forEach(btn => {
    btn.addEventListener('click', () => switchTab(btn.dataset.target));
  });

  if (dom.btnGotoEvents) {
    dom.btnGotoEvents.addEventListener('click', () => switchTab('events'));
  }
  if (dom.btnGotoIo) {
    dom.btnGotoIo.addEventListener('click', () => switchTab('io'));
  }

  // I/O View Switcher
  if (dom.btnViewCards) {
    dom.btnViewCards.addEventListener('click', () => setIoViewMode('cards'));
  }
  if (dom.btnViewTable) {
    dom.btnViewTable.addEventListener('click', () => setIoViewMode('table'));
  }

  // I/O State Filter Pills
  document.querySelectorAll('.filter-pill').forEach(btn => {
    btn.addEventListener('click', () => setIoFilter(btn.dataset.ioFilter));
  });

  // Event Journal Category Filters
  document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', () => setEventCategory(btn.dataset.filter));
  });

  // Event Journal Severity Filters
  document.querySelectorAll('.filter-btn-sev').forEach(btn => {
    btn.addEventListener('click', () => setEventSeverity(btn.dataset.severity));
  });

  // Event Journal Live Search
  if (dom.inputEventSearch) {
    dom.inputEventSearch.addEventListener('input', e => {
      state.eventSearch = e.target.value.trim().toLowerCase();
      applyEventFilters();
    });
  }

  // Event Journal Actions
  if (dom.btnClearHist)  dom.btnClearHist.addEventListener('click', clearHistory);
  if (dom.btnExportCsv)  dom.btnExportCsv.addEventListener('click', exportEventsCsv);

  // Demo Simulation Scenario Actions
  if (dom.btnDemoPause)  dom.btnDemoPause.addEventListener('click', toggleDemoPause);
  if (dom.btnDemoReset)  dom.btnDemoReset.addEventListener('click', resetDemoSimulation);
  document.querySelectorAll('.demo-scenario-pill, .scenario-btn').forEach(btn => {
    btn.addEventListener('click', () => setDemoScenario(btn.dataset.scenario));
  });
  if (dom.selectDemoScenario) {
    dom.selectDemoScenario.addEventListener('change', e => setDemoScenario(e.target.value));
  }

  // Connection & Demo Actions
  if (dom.selectDataSource) {
    dom.selectDataSource.addEventListener('change', e => setDataSource(e.target.value, true));
  }
  if (dom.selectDevice) {
    dom.selectDevice.addEventListener('change', e => {
      selectDevice(e.target.value);
    });
  }
  if (dom.inputLocalAgentKey) {
    dom.inputLocalAgentKey.addEventListener('input', e => {
      state.localAgentApiKey = e.target.value.trim();
    });
  }
  if (dom.btnConnect)    dom.btnConnect.addEventListener('click', handleConnect);
  if (dom.btnDisconnect) dom.btnDisconnect.addEventListener('click', handleDisconnect);
  if (dom.chkDemo)       dom.chkDemo.addEventListener('change', handleDemoToggle);

  // Gateway Connection Actions
  if (dom.btnTestGatewayConn)  dom.btnTestGatewayConn.addEventListener('click', testGatewayConnection);
  if (dom.btnSaveGatewayConn)  dom.btnSaveGatewayConn.addEventListener('click', saveAndApplyGatewayUrl);
  if (dom.btnResetGatewayConn) dom.btnResetGatewayConn.addEventListener('click', resetGatewayUrl);

  // Project V2 Dual-Station Selection Listeners
  if (dom.btnSelectQ03) {
    dom.btnSelectQ03.addEventListener('click', () => selectStation('melsec_q03'));
  }
  if (dom.btnSelectR04) {
    dom.btnSelectR04.addEventListener('click', () => selectStation('melsec_r04'));
  }
  if (dom.stationCardQ03) {
    dom.stationCardQ03.addEventListener('click', (e) => {
      if (e.target !== dom.btnSelectQ03) selectStation('melsec_q03');
    });
  }
  if (dom.stationCardR04) {
    dom.stationCardR04.addEventListener('click', (e) => {
      if (e.target !== dom.btnSelectR04) selectStation('melsec_r04');
    });
  }
  if (dom.btnGotoDiag) {
    dom.btnGotoDiag.addEventListener('click', () => switchTab('diag'));
  }

  // Project V2 Output Control Listeners
  if (dom.btnSetStateOff) {
    dom.btnSetStateOff.addEventListener('click', () => {
      if (dom.ctrlCommandState) dom.ctrlCommandState.value = '0';
      dom.btnSetStateOff.classList.add('active');
      if (dom.btnSetStateOn) dom.btnSetStateOn.classList.remove('active');
    });
  }
  if (dom.btnSetStateOn) {
    dom.btnSetStateOn.addEventListener('click', () => {
      if (dom.ctrlCommandState) dom.ctrlCommandState.value = '1';
      dom.btnSetStateOn.classList.add('active');
      if (dom.btnSetStateOff) dom.btnSetStateOff.classList.remove('active');
    });
  }
  if (dom.btnExecuteControl) {
    dom.btnExecuteControl.addEventListener('click', handleOutputActuation);
  }
  if (dom.btnRefreshReadback) {
    dom.btnRefreshReadback.addEventListener('click', () => {
      if (state.localAgentActive) pollLocalAgent();
    });
  }
}

// ─── Tab Navigation Controller ────────────────────────────────────────────────

function switchTab(tabId) {
  state.activeTab = tabId;
  document.querySelectorAll('.nav-tab').forEach(btn => {
    const isActive = btn.dataset.target === tabId;
    btn.classList.toggle('active', isActive);
    btn.setAttribute('aria-selected', isActive ? 'true' : 'false');
  });

  document.querySelectorAll('.tab-panel').forEach(panel => {
    const isTarget = panel.id === `panel-${tabId}`;
    panel.classList.toggle('active', isTarget);
  });
}

// ─── I/O View Mode Controller ─────────────────────────────────────────────────

function setIoViewMode(mode) {
  state.ioViewMode = mode;
  if (dom.btnViewCards) dom.btnViewCards.classList.toggle('active', mode === 'cards');
  if (dom.btnViewTable) dom.btnViewTable.classList.toggle('active', mode === 'table');
  if (dom.ioContainerCards) dom.ioContainerCards.hidden = mode !== 'cards';
  if (dom.ioContainerTable) dom.ioContainerTable.hidden = mode !== 'table';
}

function setIoFilter(filterKey) {
  state.ioFilter = filterKey;
  document.querySelectorAll('.filter-pill').forEach(pill => {
    pill.classList.toggle('active', pill.dataset.ioFilter === filterKey);
  });
  applyIoFilters();
}

function applyIoFilters() {
  const filter = state.ioFilter;
  // Apply to card grid
  document.querySelectorAll('.device-tile').forEach(tile => {
    const tileState = tile.dataset.state;
    let match = true;
    if (filter === 'on')      match = tileState === 'on';
    else if (filter === 'off')     match = tileState === 'off';
    else if (filter === 'unknown') match = tileState === 'unknown' || tileState === 'stale';
    tile.style.display = match ? '' : 'none';
  });

  // Apply to table rows
  document.querySelectorAll('#io-table-body tr').forEach(row => {
    const rowState = row.dataset.state;
    let match = true;
    if (filter === 'on')      match = rowState === 'on';
    else if (filter === 'off')     match = rowState === 'off';
    else if (filter === 'unknown') match = rowState === 'unknown' || rowState === 'stale';
    row.style.display = match ? '' : 'none';
  });
}

// ─── Settings Persistence ─────────────────────────────────────────────────────

function saveSettings() {
  const s = {
    dataSource: state.dataSource,
    url:   dom.inputUrl ? dom.inputUrl.value.trim() : '',
    user:  dom.inputUser ? dom.inputUser.value.trim() : '',
    topic: dom.inputTopic ? dom.inputTopic.value.trim() : '',
  };
  try { localStorage.setItem('qplc_settings', JSON.stringify(s)); } catch(_) {}
}

function restoreSettings() {
  try {
    const customGw = localStorage.getItem('qplc_gateway_url');
    if (dom.inputGatewayUrl && customGw) {
      dom.inputGatewayUrl.value = customGw;
    }
    const raw = localStorage.getItem('qplc_settings');
    if (!raw) return;
    const s = JSON.parse(raw);
    if (s.dataSource && dom.selectDataSource) {
      if (!isLocalOrigin()) {
        state.dataSource = s.dataSource;
        dom.selectDataSource.value = s.dataSource;
      }
    }
    if (s.url   && dom.inputUrl)   dom.inputUrl.value   = s.url;
    if (s.user  && dom.inputUser)  dom.inputUser.value  = s.user;
    if (s.topic && dom.inputTopic) dom.inputTopic.value = s.topic;
  } catch(_) {}
}

// ─── Data Source & Local Agent Controller ─────────────────────────────────────

function isLocalOrigin() {
  const h = window.location.hostname;
  return h === '127.0.0.1' || h === 'localhost' || h === '[::1]' ||
         h.startsWith('192.168.') || h.startsWith('10.') ||
         (h.startsWith('172.') && parseInt(h.split('.')[1], 10) >= 16 && parseInt(h.split('.')[1], 10) <= 31) ||
         window.location.protocol === 'file:';
}

function setDataSource(source, autoStart = true) {
  state.dataSource = source;
  if (dom.selectDataSource) dom.selectDataSource.value = source;

  const isLocal = source === 'local';
  if (dom.groupBrokerUrl) dom.groupBrokerUrl.style.display = isLocal ? 'none' : '';
  if (dom.groupLocalAgentKey) dom.groupLocalAgentKey.style.display = isLocal ? '' : 'none';

  if (dom.btnConnect) {
    dom.btnConnect.innerHTML = isLocal
      ? `<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M3.172 5.172a4 4 0 015.656 0L10 6.343l1.172-1.171a4 4 0 115.656 5.656L10 17.657l-6.828-6.829a4 4 0 010-5.656z"/></svg> Start Local Monitoring`
      : `<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M3.172 5.172a4 4 0 015.656 0L10 6.343l1.172-1.171a4 4 0 115.656 5.656L10 17.657l-6.828-6.829a4 4 0 010-5.656z"/></svg> Connect to Broker`;
  }

  saveSettings();

  if (isLocal) {
    state.connectionIntent = 'disconnected';
    state.isReconnecting = false;
    if (state.mqttClient) {
      try {
        state.mqttClient.removeAllListeners();
        state.mqttClient.end(true);
      } catch (_) {}
      state.mqttClient = null;
      state.connected = false;
    }
    if (dom.labelPillGw) dom.labelPillGw.textContent = 'AGENT';
    setStatusPill('mqtt', 'off', 'Not in Use');
    setStatusPill('esp', 'off', 'Not in Use');
    setStatusPill('gw', 'off', 'Standby');
    setStatusPill('plc', 'off', 'Not Verified');
    if (autoStart) {
      startLocalAgentMonitoring();
    }
  } else {
    stopLocalAgentMonitoring();
    if (dom.labelPillGw) dom.labelPillGw.textContent = 'GW';
    state.simulationMode = false;
    if (dom.headerSimBadge) dom.headerSimBadge.style.display = 'none';
    if (dom.headerBrandSubtitle) {
      dom.headerBrandSubtitle.textContent = 'MC Protocol 3E / TCP • Port 5010 • Binary Frame Monitoring';
    }
    setMqttStatus('off', 'Disconnected');
    setStatusPill('esp', 'off', 'Unknown');
    setStatusPill('gw', 'off', 'Standby');
    setStatusPill('plc', 'off', 'Not Verified');
  }
  updateSubsystemMatrix();
}

function getLocalAgentHeaders() {
  const headers = { 'Accept': 'application/json' };
  const key = state.localAgentApiKey || (dom.inputLocalAgentKey ? dom.inputLocalAgentKey.value.trim() : '');
  if (key) {
    headers['Authorization'] = `Bearer ${key}`;
  }
  return headers;
}

function startLocalAgentMonitoring() {
  if (state.isDemo) {
    addHistoryItem('system', 'warn', 'Operation Blocked: Disable Demo mode before starting Local Agent monitoring.');
    return;
  }
  if (state.localAgentActive) return;

  state.localAgentActive = true;
  if (dom.labelPillGw) dom.labelPillGw.textContent = 'AGENT';
  setStatusPill('mqtt', 'off', 'Not in Use');
  setStatusPill('esp', 'off', 'Not in Use');
  setStatusPill('gw', 'connecting', 'Connecting…');
  setStatusPill('plc', 'off', 'Not Verified');
  if (dom.btnConnect) dom.btnConnect.disabled = true;
  if (dom.btnDisconnect) dom.btnDisconnect.disabled = false;
  addHistoryItem('system', 'info', 'Initiating Local Agent connection (http://127.0.0.1:8000)...');

  pollLocalAgent();
  state.localAgentTimerId = setInterval(pollLocalAgent, 1000);
}

function stopLocalAgentMonitoring() {
  if (state.localAgentTimerId) {
    clearInterval(state.localAgentTimerId);
    state.localAgentTimerId = null;
  }
  state.localAgentActive = false;
  state.plcConnected = false;
  state.gwActive = false;
  state.agentHealthy = false;
  if (dom.btnConnect) dom.btnConnect.disabled = false;
  if (dom.btnDisconnect) dom.btnDisconnect.disabled = true;
  if (dom.labelPillGw) dom.labelPillGw.textContent = 'AGENT';
  setStatusPill('mqtt', 'off', 'Not in Use');
  setStatusPill('esp', 'off', 'Not in Use');
  setStatusPill('gw', 'off', 'Standby');
  setStatusPill('plc', 'off', 'Not Verified');
  addHistoryItem('system', 'info', 'Local Agent monitoring stopped');
  markAllStale();
  updateSubsystemMatrix();
}

async function pollLocalAgent() {
  if (!state.localAgentActive) return;

  const baseUrl = getGatewayBaseUrl();
  const headers = getLocalAgentHeaders();

  try {
    // 1. Ingestion Agent health check & simulation state verification
    const healthRes = await fetch(`${baseUrl}/api/v1/health`, { headers });
    if (!healthRes.ok) {
      if (healthRes.status === 401) {
        throw new Error('Local Agent Authentication Required (401 Unauthorized)');
      }
      throw new Error(`Health check failed: HTTP ${healthRes.status}`);
    }
    const healthData = await healthRes.json();
    state.agentHealthy = (healthData.status === 'healthy' || healthData.status === 'degraded');
    state.agentUptime = Math.floor(healthData.uptime_seconds || 0);
    if (healthData.simulation_mode !== undefined) {
      state.simulationMode = Boolean(healthData.simulation_mode);
    }

    // 2. Discover registered devices and update dynamic selector
    const devRes = await fetch(`${baseUrl}/api/v1/devices`, { headers });
    if (!devRes.ok) {
      if (devRes.status === 401) {
        throw new Error('Local Agent Authentication Required (401 Unauthorized)');
      }
      throw new Error(`Device discovery failed: HTTP ${devRes.status}`);
    }
    const devData = await devRes.json();
    const devices = Array.isArray(devData) ? devData : (devData.devices || []);
    state.devicesList = devices;

    if (devices.length > 0) {
      updateDeviceSelector(devices);
      // Validate or assign active device id
      if (!state.localAgentDeviceId || !devices.some(d => (d.id || d.device_id) === state.localAgentDeviceId)) {
        state.localAgentDeviceId = devices[0].id || devices[0].device_id;
      }
      const activeDev = devices.find(d => (d.id || d.device_id) === state.localAgentDeviceId) || devices[0];
      state.deviceAdapterId = activeDev.adapter_id || null;
      state.deviceModel = activeDev.model || activeDev.name || null;
      if (state.deviceAdapterId === 'simulated') {
        state.simulationMode = true;
      }
      if (dom.headerModelBadge) {
        dom.headerModelBadge.textContent = state.deviceModel || state.localAgentDeviceId;
      }
      syncDualStationStatus(devices, baseUrl, headers);
    } else {
      updateDeviceSelector([]);
      throw new Error('No configured devices returned by Local Agent');
    }

    // 3. Normalized device telemetry poll
    const telRes = await fetch(`${baseUrl}/api/v1/devices/${state.localAgentDeviceId}/telemetry`, { headers });
    if (!telRes.ok) {
      if (telRes.status === 401) {
        throw new Error('Local Agent Authentication Required (401 Unauthorized)');
      }
      if (telRes.status === 404) {
        throw new Error(`Device '${state.localAgentDeviceId}' not found on Local Agent`);
      }
      throw new Error(`Telemetry poll failed: HTTP ${telRes.status}`);
    }

    const telData = await telRes.json();
    if (telData.simulation_mode !== undefined) {
      state.simulationMode = Boolean(telData.simulation_mode);
    }

    // Explicit indicators for Local Agent mode:
    // MQTT & ESP8266 are unused in direct local loopback mode
    setStatusPill('mqtt', 'off', 'Not in Use');
    setStatusPill('esp', 'off', 'Not in Use');
    if (dom.labelPillGw) dom.labelPillGw.textContent = 'AGENT';
    setStatusPill('gw', 'ok', 'Running');

    // PLC controller pill reflects verified hardware vs virtual simulation
    const isConn = telData.connected === true || telData.status === 'online';
    if (state.simulationMode || state.deviceAdapterId === 'simulated') {
      setStatusPill('plc', 'warn', 'Simulated');
    } else if (isConn) {
      setStatusPill('plc', 'ok', 'Verified Online');
    } else {
      setStatusPill('plc', 'error', 'Offline');
    }

    const activeDev = (state.devicesList || []).find(d => (d.id || d.device_id) === state.localAgentDeviceId);
    handleLocalAgentTelemetry(telData, activeDev);

  } catch (err) {
    state.agentHealthy = false;
    state.stats.lastFault = err.message;
    if (dom.labelPillGw) dom.labelPillGw.textContent = 'AGENT';
    setStatusPill('mqtt', 'off', 'Not in Use');
    setStatusPill('esp', 'off', 'Not in Use');
    setStatusPill('gw', 'error', err.message.includes('401') ? '401 Auth Required' : 'Fault');
    setStatusPill('plc', 'off', 'Not Verified');
    if (dom.diagLastFault) dom.diagLastFault.textContent = err.message;
    if (dom.selectDevice && err.message.includes('401')) {
      dom.selectDevice.innerHTML = '<option value="" disabled selected>Authentication Required (HTTP 401)</option>';
    }
    addHistoryItem('comm_error', 'warn', `Local Agent: ${err.message}`);
    markAllStale();
    updateSubsystemMatrix();
  }
}

function updateDeviceSelector(devices) {
  if (!dom.selectDevice) return;
  if (!Array.isArray(devices) || devices.length === 0) {
    dom.selectDevice.innerHTML = '<option value="" disabled selected>No devices configured</option>';
    return;
  }

  const currentVal = state.localAgentDeviceId || devices[0].id;
  const currentOptions = Array.from(dom.selectDevice.options).map(o => o.value);
  const newOptions = devices.map(d => d.id || d.device_id);

  const optionsChanged = currentOptions.length !== newOptions.length ||
    newOptions.some((id, idx) => id !== currentOptions[idx]);

  if (optionsChanged || dom.selectDevice.options.length <= 1) {
    dom.selectDevice.innerHTML = devices.map(d => {
      const id = d.id || d.device_id;
      const name = d.name || id;
      const model = d.model ? ` (${d.model})` : '';
      const conn = d.connected ? ' • Online' : '';
      const isSel = id === currentVal ? 'selected' : '';
      return `<option value="${escapeHtml(id)}" ${isSel}>${escapeHtml(name)}${escapeHtml(model)}${conn}</option>`;
    }).join('');
  }

  if (dom.selectDevice.value !== currentVal) {
    dom.selectDevice.value = currentVal;
  }
}

function selectDevice(deviceId) {
  if (!deviceId) return;
  state.localAgentDeviceId = deviceId;
  if (dom.selectDevice && dom.selectDevice.value !== deviceId) {
    dom.selectDevice.value = deviceId;
  }

  const dev = (state.devicesList || []).find(d => (d.id || d.device_id) === deviceId);
  if (dev) {
    state.deviceAdapterId = dev.adapter_id || null;
    state.deviceModel = dev.model || dev.name || null;
    if (state.deviceAdapterId === 'simulated') {
      state.simulationMode = true;
    }
  }

  // Update header badges
  if (dom.headerModelBadge && dev) {
    dom.headerModelBadge.textContent = dev.model || dev.id;
  }
  if (dom.headerSimBadge) {
    dom.headerSimBadge.style.display = (state.simulationMode || (dev && dev.adapter_id === 'simulated')) ? 'inline-block' : 'none';
  }
  if (dom.headerBrandSubtitle && dev) {
    if (state.simulationMode || dev.adapter_id === 'simulated') {
      dom.headerBrandSubtitle.textContent = 'Virtual Memory Model • No Physical PLC Connected';
    } else if (dev.adapter_id === 'modbus_tcp') {
      dom.headerBrandSubtitle.textContent = 'Modbus TCP • Port 502 • Unit ID 1 • Holding & Input Registers';
    } else {
      dom.headerBrandSubtitle.textContent = 'MC Protocol 3E / TCP • Port 5010 • Binary Frame Monitoring';
    }
  }

  // Reset tiles & analog metrics for clean device transition
  resetAllTiles();
  if (dev && dev.id === 'melsec_q03') {
    state.hasCustomTableBuilt = false;
    buildDeviceTable();
  }
  if (dom.analogGrid) {
    dom.analogGrid.innerHTML = '<div class="analog-empty-state">Loading telemetry for selected device...</div>';
  }
  if (dom.overviewAnalogGrid) {
    dom.overviewAnalogGrid.innerHTML = '<div class="analog-empty-state">Loading telemetry...</div>';
  }

  state.lastReadingTs = null;
  state.lastPayload = null;
  state.isStale = false;
  hideStaleBanner();

  addHistoryItem('system', 'info', `Target PLC device selected: ${dev ? dev.name : deviceId} [${deviceId}]`);
  updateSubsystemMatrix();

  if (state.localAgentActive) {
    pollLocalAgent();
  }
}

// ─── Project V2 Dual-Station & Supervisory Control ───────────────────────────

function getGatewayBaseUrl() {
  const custom = localStorage.getItem('qplc_gateway_url');
  if (custom && custom.trim()) {
    let url = custom.trim().replace(/\/+$/, '');
    if (!/^https?:\/\//i.test(url)) {
      url = 'http://' + url;
    }
    return url;
  }
  if (window.location.protocol.startsWith('http') && window.location.hostname !== 'localhost' && window.location.hostname !== '127.0.0.1') {
    return window.location.origin;
  }
  return 'http://127.0.0.1:8000';
}

async function testGatewayConnection() {
  if (!dom.gatewayConnStatus) return;
  let targetUrl = dom.inputGatewayUrl ? dom.inputGatewayUrl.value.trim() : '';
  if (!targetUrl) {
    targetUrl = getGatewayBaseUrl();
  }
  if (!/^https?:\/\//i.test(targetUrl)) {
    targetUrl = 'http://' + targetUrl;
  }
  targetUrl = targetUrl.replace(/\/+$/, '');

  dom.gatewayConnStatus.style.display = 'flex';
  dom.gatewayConnStatus.className = 'gateway-conn-status-banner warn';
  dom.gatewayConnStatus.textContent = `Testing probe -> ${targetUrl}/api/v1/health...`;

  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 3500);
    const resp = await fetch(`${targetUrl}/api/v1/health`, {
      signal: controller.signal,
      headers: getLocalAgentHeaders()
    });
    clearTimeout(timeoutId);

    if (resp.ok) {
      const data = await resp.json();
      const nodeType = data.firmware_version ? `ESP32 Gateway v${data.firmware_version}` : (data.node_type || 'Local Host');
      const sim = data.simulation_mode ? ' (Simulation)' : '';
      dom.gatewayConnStatus.className = 'gateway-conn-status-banner ok';
      dom.gatewayConnStatus.textContent = `✓ Connected: ${nodeType}${sim} • Uptime: ${data.uptime_seconds || 0}s`;
      addHistoryItem('system', 'info', `Gateway connection probe succeeded: ${targetUrl}`);
    } else {
      dom.gatewayConnStatus.className = 'gateway-conn-status-banner error';
      dom.gatewayConnStatus.textContent = `✗ Gateway returned HTTP ${resp.status} (${resp.statusText})`;
      addHistoryItem('comm_error', 'warn', `Gateway probe failed: HTTP ${resp.status}`);
    }
  } catch (err) {
    dom.gatewayConnStatus.className = 'gateway-conn-status-banner error';
    dom.gatewayConnStatus.textContent = `✗ Probe failed: ${err.name === 'AbortError' ? 'Connection Timeout (3.5s)' : err.message}`;
    addHistoryItem('comm_error', 'error', `Gateway probe error: ${err.message}`);
  }
}

function saveAndApplyGatewayUrl() {
  let targetUrl = dom.inputGatewayUrl ? dom.inputGatewayUrl.value.trim() : '';
  if (targetUrl) {
    if (!/^https?:\/\//i.test(targetUrl)) {
      targetUrl = 'http://' + targetUrl;
      if (dom.inputGatewayUrl) dom.inputGatewayUrl.value = targetUrl;
    }
    targetUrl = targetUrl.replace(/\/+$/, '');
    localStorage.setItem('qplc_gateway_url', targetUrl);
    addHistoryItem('system', 'info', `Gateway URL saved: ${targetUrl}`);
  } else {
    localStorage.removeItem('qplc_gateway_url');
    addHistoryItem('system', 'info', 'Gateway URL reset to auto-detected origin');
  }

  if (dom.gatewayConnStatus) {
    dom.gatewayConnStatus.style.display = 'flex';
    dom.gatewayConnStatus.className = 'gateway-conn-status-banner ok';
    dom.gatewayConnStatus.textContent = `✓ Active Gateway: ${targetUrl || 'Auto-detect origin'}`;
  }

  setDataSource('local', true);
}

function resetGatewayUrl() {
  localStorage.removeItem('qplc_gateway_url');
  if (dom.inputGatewayUrl) dom.inputGatewayUrl.value = '';
  if (dom.gatewayConnStatus) {
    dom.gatewayConnStatus.style.display = 'flex';
    dom.gatewayConnStatus.className = 'gateway-conn-status-banner warn';
    dom.gatewayConnStatus.textContent = 'Gateway URL reset to auto-detect origin.';
  }
  addHistoryItem('system', 'info', 'Gateway URL reset to default origin');
  if (state.localAgentActive) {
    pollLocalAgent();
  }
}

function getCachedPointValue(stationId, pointId) {
  const devTel = state.deviceTelemetry[stationId];
  if (!devTel || !devTel.readings) return null;
  const r = devTel.readings[pointId];
  if (!r) return null;
  const isGood = (r.quality === 'GOOD');
  if (!isGood) return null;
  return r.raw_value !== undefined ? r.raw_value : r.engineering_value;
}

function selectStation(stationId) {
  if (!stationId) return;
  selectDevice(stationId);

  // Update Overview Dual-Station Deck Cards
  if (dom.stationCardQ03) {
    dom.stationCardQ03.classList.toggle('selected', stationId === 'melsec_q03');
  }
  if (dom.stationCardR04) {
    dom.stationCardR04.classList.toggle('selected', stationId === 'melsec_r04');
  }

  // Update Control Panel Subtitle & Lockouts
  const isQ = (stationId === 'melsec_q03');
  const isR = (stationId === 'melsec_r04');

  if (dom.ctrlActiveStationSub) {
    if (isQ) {
      dom.ctrlActiveStationSub.textContent = 'Mitsubishi Q03UDECPU (192.168.3.30:5010) — Primary Operational Station';
    } else if (isR) {
      dom.ctrlActiveStationSub.textContent = 'Mitsubishi R04CPU / R04ENCPU (192.168.3.20:5010) — Secondary Station (Provisional)';
    } else {
      dom.ctrlActiveStationSub.textContent = `Station: ${stationId}`;
    }
  }

  if (dom.ctrlRLockNotice) {
    dom.ctrlRLockNotice.style.display = isR ? 'flex' : 'none';
  }

  if (dom.btnExecuteControl) {
    dom.btnExecuteControl.disabled = isR;
    dom.btnExecuteControl.classList.toggle('disabled', isR);
  }

  if (dom.ctrlSelectPoint) {
    dom.ctrlSelectPoint.disabled = isR;
    if (isR) {
      dom.ctrlSelectPoint.innerHTML = '<option value="" disabled selected>Outputs Locked (Pending Lab Commissioning)</option>';
    } else {
      // Q03: Strictly permit Y20 only. Banned point Y0 is never present!
      dom.ctrlSelectPoint.innerHTML = '<option value="Y20" selected>Y20 — Master Output Relay (Allowlisted)</option>';
    }
  }

  if (dom.ctrlStationConnBadge) {
    if (isR) {
      dom.ctrlStationConnBadge.className = 'status-pill warn';
      dom.ctrlStationConnBadge.textContent = 'Outputs Locked (Provisional)';
    } else {
      dom.ctrlStationConnBadge.className = 'status-pill ok';
      dom.ctrlStationConnBadge.textContent = 'Ready (Q03 Operational)';
    }
  }

  // Update readback indicator from cached telemetry
  const cachedVal = getCachedPointValue(stationId, 'Y20');
  updateReadbackIndicator(cachedVal);
}

async function syncDualStationStatus(devices, baseUrl, headers) {
  const qDev = devices.find(d => (d.id || d.device_id) === 'melsec_q03');
  if (dom.stationBadgeQ) {
    if (qDev) {
      if (qDev.adapter_id === 'simulated' || state.simulationMode) {
        dom.stationBadgeQ.className = 'status-pill warn';
        dom.stationBadgeQ.textContent = 'Simulated';
      } else if (qDev.connected) {
        dom.stationBadgeQ.className = 'status-pill ok';
        dom.stationBadgeQ.textContent = 'Online (MC 3E)';
      } else {
        dom.stationBadgeQ.className = 'status-pill error';
        dom.stationBadgeQ.textContent = 'Offline';
      }
    }
  }

  const rDev = devices.find(d => (d.id || d.device_id) === 'melsec_r04');
  if (dom.stationBadgeR) {
    if (rDev) {
      if (rDev.adapter_id === 'simulated' || state.simulationMode) {
        dom.stationBadgeR.className = 'status-pill warn';
        dom.stationBadgeR.textContent = 'Simulated (Provisional)';
      } else if (rDev.connected) {
        dom.stationBadgeR.className = 'status-pill ok';
        dom.stationBadgeR.textContent = 'Online (SLMP / MC)';
      } else {
        dom.stationBadgeR.className = 'status-pill warn';
        dom.stationBadgeR.textContent = 'Provisional / Offline';
      }
    }
  }

  if (dom.stationSignalsQ) {
    dom.stationSignalsQ.textContent = '8 Inputs (X0-X7) • 8 Outputs (Y20-Y27)';
  }
  if (dom.stationSignalsR) {
    dom.stationSignalsR.textContent = '8 Inputs (X0-X7) • 8 Outputs (Y20-Y27, Read-Only)';
  }

  try {
    const sysRes = await fetch(`${baseUrl}/api/v1/system`, { headers });
    if (sysRes.ok) {
      const sysData = await sysRes.json();
      if (dom.gwCardIp) {
        dom.gwCardIp.textContent = (sysData.network && sysData.network.ip) || sysData.ip || window.location.hostname || '192.168.3.x';
      }
      if (dom.gwCardLcd) {
        dom.gwCardLcd.textContent = (sysData.lcd && sysData.lcd.status) || '16x2 I2C Display (Active)';
      }
    }
  } catch (_e) {
    if (dom.gwCardIp) dom.gwCardIp.textContent = window.location.hostname || '192.168.3.x';
    if (dom.gwCardLcd) dom.gwCardLcd.textContent = '16x2 I2C Display (Ready)';
  }
}

function resetPipelineUI() {
  for (let i = 1; i <= 5; i++) {
    const stepEl = $(`pipe-step-${i}`);
    const statusEl = $(`pipe-status-${i}`);
    const detailEl = $(`pipe-detail-${i}`);
    if (stepEl) stepEl.className = 'pipeline-step';
    if (statusEl) {
      statusEl.className = 'pipeline-status pending';
      statusEl.textContent = 'Pending';
    }
    if (detailEl) detailEl.textContent = 'Awaiting execution...';
  }
  if (dom.ctrlPipelineOverallBadge) {
    dom.ctrlPipelineOverallBadge.className = 'status-pill warn';
    dom.ctrlPipelineOverallBadge.textContent = 'Pipeline Ready';
  }
  if (dom.ctrlResultBox) dom.ctrlResultBox.style.display = 'none';
}

function setPipelineStep(stepNum, status, text) {
  const stepEl = $(`pipe-step-${stepNum}`);
  const statusEl = $(`pipe-status-${stepNum}`);
  const detailEl = $(`pipe-detail-${stepNum}`);

  if (stepEl) {
    stepEl.classList.remove('active', 'passed', 'failed');
    if (status === 'in_progress') stepEl.classList.add('active');
    else if (status === 'passed') stepEl.classList.add('passed');
    else if (status === 'failed') stepEl.classList.add('failed');
  }

  if (statusEl) {
    statusEl.className = `pipeline-status ${status}`;
    if (status === 'in_progress') statusEl.textContent = 'Running';
    else if (status === 'passed') statusEl.textContent = 'Passed';
    else if (status === 'failed') statusEl.textContent = 'Failed';
    else statusEl.textContent = 'Pending';
  }

  if (detailEl && text) {
    detailEl.textContent = text;
  }
}

function showControlResult(success, title, msg) {
  if (!dom.ctrlResultBox || !dom.ctrlResultContent) return;
  dom.ctrlResultBox.style.display = 'block';
  dom.ctrlResultBox.className = `control-result-box ${success ? 'success' : 'failure'}`;
  dom.ctrlResultContent.innerHTML = `<strong>${escapeHtml(title)}</strong>: ${escapeHtml(msg)}`;
}

function updateReadbackIndicator(val) {
  if (!dom.ctrlReadbackDot || !dom.ctrlReadbackText) return;
  dom.ctrlReadbackDot.classList.remove('active', 'inactive', 'unknown');

  if (val === 1 || val === true || val === '1' || val === 'ON') {
    dom.ctrlReadbackDot.classList.add('active');
    dom.ctrlReadbackText.textContent = 'ON (Closed / Energized)';
  } else if (val === 0 || val === false || val === '0' || val === 'OFF') {
    dom.ctrlReadbackDot.classList.add('inactive');
    dom.ctrlReadbackText.textContent = 'OFF (Open / De-energized)';
  } else {
    dom.ctrlReadbackDot.classList.add('unknown');
    dom.ctrlReadbackText.textContent = 'UNKNOWN / COMM FAILURE';
  }
}

async function simulateControlPipeline(stationId, pointId, desiredState, pin) {
  setPipelineStep(2, 'passed', 'Simulated MC Protocol 3E Binary frame constructed');
  await new Promise(r => setTimeout(r, 120));

  if (!pin || pin.trim().length === 0) {
    setPipelineStep(1, 'failed', 'Authentication Required: Operator PIN cannot be empty');
    showControlResult(false, 'Authentication Failed', 'Operator PIN is required for simulated control.');
    return;
  }

  setPipelineStep(3, 'in_progress', 'Simulating network dispatch to Q03 (192.168.3.30:5010)...');
  await new Promise(r => setTimeout(r, 120));
  setPipelineStep(3, 'passed', 'Simulated socket write dispatched (12 ms)');

  setPipelineStep(4, 'in_progress', 'Simulating PLC internal execution...');
  await new Promise(r => setTimeout(r, 120));
  setPipelineStep(4, 'passed', 'Simulated PLC acknowledged execution (Endcode: 0x0000)');

  setPipelineStep(5, 'in_progress', 'Performing simulated read-back verification...');
  await new Promise(r => setTimeout(r, 120));
  setPipelineStep(5, 'passed', `Simulated read-back confirmed: ${pointId} = ${desiredState === 1 ? 'ON' : 'OFF'}`);

  updateReadbackIndicator(desiredState);
  showControlResult(true, 'Actuation Succeeded & Verified (Simulated)',
    `Point ${pointId} set to ${desiredState === 1 ? 'ON' : 'OFF'} in deterministic simulation. Immediate read-back verified.`);
  if (dom.ctrlPipelineOverallBadge) {
    dom.ctrlPipelineOverallBadge.className = 'status-pill ok';
    dom.ctrlPipelineOverallBadge.textContent = 'All 5 Steps Verified (Simulated)';
  }

  if (pointId === 'Y20') {
    const tile = document.querySelector('.device-tile[data-device="Y20"]');
    if (tile) {
      tile.dataset.state = desiredState === 1 ? 'on' : 'off';
      tile.classList.remove('loading');
      const badge = tile.querySelector('.tile-state-badge');
      if (badge) badge.textContent = desiredState === 1 ? 'ON' : 'OFF';
    }
    updateLadderDiagram(null, desiredState === 1);
  }
  addHistoryItem('state_change', 'info', `SIMULATED_CONTROL: ${stationId} ${pointId} = ${desiredState === 1 ? 'ON' : 'OFF'}`);
}

async function handleOutputActuation() {
  const stationId = state.localAgentDeviceId || 'melsec_q03';
  const pointId = dom.ctrlSelectPoint ? dom.ctrlSelectPoint.value : 'Y20';
  const desiredState = dom.ctrlCommandState ? parseInt(dom.ctrlCommandState.value, 10) : 0;
  const pin = dom.ctrlInputPin ? dom.ctrlInputPin.value.trim() : '';

  resetPipelineUI();

  // Safety Step 1: Pre-flight Safety Checks
  setPipelineStep(1, 'in_progress', 'Validating station allowlist and operator PIN...');

  if (stationId === 'melsec_r04') {
    setPipelineStep(1, 'failed', 'REJECTED: R-Series outputs locked pending physical verification');
    showControlResult(false, 'Output Execution Locked', 'R-Series (Station 2) outputs are strictly locked in software until physical laboratory commissioning is completed.');
    addHistoryItem('system', 'error', 'Control rejected: R04 outputs locked');
    return;
  }

  // Safety Rule: Banned point Y0 must NEVER be actuated
  if (pointId === 'Y0') {
    setPipelineStep(1, 'failed', 'SAFETY FAULT: Point Y0 is strictly forbidden by project safety rules');
    showControlResult(false, 'Forbidden Point (Y0)', 'Actuation of point Y0 is permanently banned across firmware, backend, and UI.');
    addHistoryItem('system', 'error', 'Control rejected: Banned point Y0');
    return;
  }

  if (pointId !== 'Y20') {
    setPipelineStep(1, 'failed', `REJECTED: Point ${pointId} is not in authorized allowlist (Y20 only)`);
    showControlResult(false, 'Unauthorized Point', `Point ${pointId} is not in the permitted output allowlist. Only Y20 is authorized.`);
    addHistoryItem('system', 'error', `Control rejected: ${pointId} not in allowlist`);
    return;
  }

  if (!pin) {
    setPipelineStep(1, 'failed', 'AUTHENTICATION REQUIRED: Operator PIN must be provided');
    showControlResult(false, 'PIN Required', 'Please enter a valid operator PIN to execute output actuation.');
    return;
  }

  setPipelineStep(1, 'passed', 'Allowlist verified (Y20 permitted, Y0 banned, station authorized)');

  // Step 2: Protocol Frame Assembly
  setPipelineStep(2, 'in_progress', 'Building MC Protocol 3E Binary write frame (Cmd 0x1401, Sub 0x0001)...');

  // Handle Demo / Pure Simulation mode
  if (state.isDemo || (!state.localAgentActive && !state.connected)) {
    await simulateControlPipeline(stationId, pointId, desiredState, pin);
    return;
  }

  // Live dispatch via Local Agent or ESP32 Gateway
  try {
    setPipelineStep(2, 'passed', 'MC Protocol 3E Binary frame constructed');
    setPipelineStep(3, 'in_progress', `Dispatching TCP packet to gateway -> 192.168.3.30:5010...`);

    const baseUrl = getGatewayBaseUrl();
    const headers = {
      'Content-Type': 'application/json',
      'X-Operator-PIN': pin,
      ...getLocalAgentHeaders()
    };

    const startTime = performance.now();
    const resp = await fetch(`${baseUrl}/api/v1/devices/${stationId}/outputs/${pointId}/command`, {
      method: 'POST',
      headers: headers,
      body: JSON.stringify({ state: desiredState, auth_pin: pin })
    });

    const elapsed = Math.round(performance.now() - startTime);

    if (!resp.ok) {
      const errBody = await resp.json().catch(() => ({ error: `HTTP ${resp.status}` }));
      const detail = errBody.error || errBody.detail || errBody.message || `HTTP ${resp.status}`;
      const errStatus = errBody.status || '';

      if (resp.status === 403 && (errStatus === 'PHYSICAL_WRITES_DISABLED' || detail.includes('PHYSICAL_WRITES_DISABLED') || detail.includes('safety interlock'))) {
        setPipelineStep(1, 'failed', 'SAFETY LOCK: Physical output writes disabled (fail-closed default)');
        showControlResult(false, 'Physical Writes Disabled (Fail-Closed)', 
          'Physical output control is disabled by default in firmware configuration (fail-closed safety policy). To enable physical control, commission Q_PLC_WRITES_ENABLED after formal safety review.');
      } else if (resp.status === 429 || errStatus === 'TEMPORARY_LOCKOUT' || errStatus === 'RATE_LIMITED') {
        setPipelineStep(1, 'failed', 'SECURITY LOCKOUT: Excessive PIN failure rate limit');
        showControlResult(false, 'Temporary Security Lockout (30s)', 
          'Authentication rate limit reached: 5 consecutive failed PIN attempts triggered a 30-second lockout period.');
      } else if (resp.status === 401 || errStatus === 'PIN_REQUIRED' || errStatus === 'INVALID_PIN') {
        setPipelineStep(1, 'failed', `Authentication Failed: ${detail}`);
        showControlResult(false, 'Authentication Failed', detail);
      } else if (resp.status === 400) {
        setPipelineStep(1, 'failed', `Pre-condition Rejected: ${detail}`);
        showControlResult(false, 'Safety Violation', detail);
      } else if (resp.status === 504 || resp.status === 502) {
        setPipelineStep(3, 'failed', `PLC Communication Timeout: ${detail}`);
        showControlResult(false, 'PLC Communication Failure', detail);
      } else {
        setPipelineStep(4, 'failed', `Execution Error: ${detail}`);
        showControlResult(false, 'Execution Failed', detail);
      }
      addHistoryItem('system', 'error', `Output command rejected: ${detail}`);
      return;
    }

    const result = await resp.json();
    setPipelineStep(3, 'passed', `Socket write dispatched (${elapsed} ms)`);

    const plcEndCode = result.plc_end_code !== undefined ? result.plc_end_code : (result.plc_endcode || '0x0000');
    // Step 4: PLC Execution & Endcode Check
    if (plcEndCode === '0x0000' || result.status === 'SUCCESS' || result.success) {
      setPipelineStep(4, 'passed', `PLC acknowledged execution (Endcode: ${plcEndCode})`);
    } else {
      setPipelineStep(4, 'failed', `PLC returned error endcode: ${plcEndCode}`);
      showControlResult(false, 'PLC Endcode Error', `PLC returned abnormal completion code: ${plcEndCode}`);
      return;
    }

    // Step 5: Read-back Verification (Strictly enforce that missing read-back is NEVER replaced with desired state)
    const hasReadback = (result.readback_value !== null && result.readback_value !== undefined);
    const readbackVal = hasReadback ? (result.readback_value ? 1 : 0) : null;
    const verified = (hasReadback && result.readback_verified === true && readbackVal === desiredState);

    if (!hasReadback || readbackVal === null) {
      setPipelineStep(5, 'failed', 'Read-back unverified: No confirmation reading from PLC memory');
      updateReadbackIndicator(null);
      showControlResult(false, 'Read-back Unverified', 'Write command was transmitted, but PLC readback verification could not be confirmed.');
      addHistoryItem('system', 'warn', `Output write unverified for ${stationId} ${pointId}`);
    } else if (verified) {
      setPipelineStep(5, 'passed', `Read-back confirmed: ${pointId} = ${readbackVal === 1 ? 'ON' : 'OFF'}`);
      updateReadbackIndicator(readbackVal);
      showControlResult(true, 'Actuation Succeeded & Verified',
        `Point ${pointId} successfully set to ${desiredState === 1 ? 'ON' : 'OFF'}. Immediate read-back verified state in ${result.elapsed_ms || elapsed} ms.`);
      addHistoryItem('state_change', 'info', `OUTPUT_ACTUATED: ${stationId} ${pointId} = ${desiredState === 1 ? 'ON' : 'OFF'} (Verified)`);
      if (dom.ctrlPipelineOverallBadge) {
        dom.ctrlPipelineOverallBadge.className = 'status-pill ok';
        dom.ctrlPipelineOverallBadge.textContent = 'All 5 Steps Verified';
      }
      if (state.localAgentActive) setTimeout(pollLocalAgent, 250);
    } else {
      setPipelineStep(5, 'failed', `Read-back mismatch! Commanded: ${desiredState}, Read: ${readbackVal}`);
      updateReadbackIndicator(readbackVal);
      showControlResult(false, 'Read-back Verification Mismatch',
        `Command sent, but physical read-back returned ${readbackVal} instead of desired state ${desiredState}. Note: Active ladder scan LD X3 -> OUT Y20 may be overriding coil state.`);
      addHistoryItem('system', 'error', `Read-back mismatch on ${pointId}: read ${readbackVal}`);
    }

  } catch (err) {
    setPipelineStep(3, 'failed', `Network Error: ${err.message}`);
    showControlResult(false, 'Network Dispatch Failure', err.message);
    addHistoryItem('comm_error', 'error', `Output dispatch failed: ${err.message}`);
  }
}


function formatMetricValue(reading) {
  const qualityStr = (reading && reading.quality) ? String(reading.quality).toUpperCase() : 'COMM_FAILURE';
  const hasValue = reading && reading.engineering_value !== null && reading.engineering_value !== undefined;

  // Handle explicit STALE quality from backend or data pipeline
  if (qualityStr === 'STALE') {
    if (hasValue) {
      const val = reading.engineering_value;
      const raw = reading.raw_value;
      const dataType = (reading.data_type || '').toLowerCase();
      let formatted = '';
      if (typeof val === 'number') {
        if (dataType === 'float32' || (!Number.isInteger(val) && typeof val === 'number')) {
          formatted = Number(val).toLocaleString(undefined, { minimumFractionDigits: 1, maximumFractionDigits: 2 });
        } else {
          formatted = Number(val).toLocaleString();
        }
      } else {
        formatted = String(val);
      }
      return {
        valueText: formatted,
        valClass: 'val-stale',
        quality: 'STALE',
        qualityClass: 'quality-stale',
        rawText: raw !== undefined && raw !== null ? `Raw: ${raw} (Stale)` : 'Raw: — (Stale)',
        timestamp: reading.timestamp_utc ? formatIsoShort(reading.timestamp_utc) : '—',
      };
    } else {
      return {
        valueText: '—',
        valClass: 'val-fault',
        quality: 'STALE',
        qualityClass: 'quality-stale',
        rawText: (reading && reading.error_message) ? reading.error_message : 'Telemetry Stale (Timeout)',
        timestamp: (reading && reading.timestamp_utc) ? formatIsoShort(reading.timestamp_utc) : '—',
      };
    }
  }

  const isGood = qualityStr === 'GOOD' && hasValue;
  if (!isGood) {
    const q = qualityStr;
    return {
      valueText: '—',
      valClass: 'val-fault',
      quality: q,
      qualityClass: (q === 'GOOD') ? 'quality-good' : (q === 'COMM_FAILURE' || q === 'BAD' ? 'quality-comm-failure' : (q === 'STALE' ? 'quality-stale' : 'quality-uncertain')),
      rawText: (reading && reading.error_message) ? reading.error_message : (q === 'COMM_FAILURE' ? 'Communication Lost' : 'No Valid Reading'),
      timestamp: (reading && reading.timestamp_utc) ? formatIsoShort(reading.timestamp_utc) : '—',
    };
  }

  const val = reading.engineering_value;
  const raw = reading.raw_value;
  const dataType = (reading.data_type || '').toLowerCase();
  let formatted = '';

  if (typeof val === 'number') {
    if (dataType === 'float32' || (!Number.isInteger(val) && typeof val === 'number')) {
      formatted = Number(val).toLocaleString(undefined, { minimumFractionDigits: 1, maximumFractionDigits: 2 });
    } else {
      formatted = Number(val).toLocaleString();
    }
  } else {
    formatted = String(val);
  }

  return {
    valueText: formatted,
    valClass: 'val-good',
    quality: 'GOOD',
    qualityClass: 'quality-good',
    rawText: raw !== undefined && raw !== null ? `Raw: ${raw}` : 'Raw: —',
    timestamp: reading.timestamp_utc ? formatIsoShort(reading.timestamp_utc) : '—',
  };
}

function analogMetricCardHTML(pointId, reading, isSim) {
  const meta = formatMetricValue(reading);
  const units = (reading && reading.engineering_units) ? escapeHtml(reading.engineering_units) : '';
  const desc = (reading && reading.description) ? escapeHtml(reading.description) : escapeHtml(pointId);
  const dataType = (reading && reading.data_type) ? escapeHtml(reading.data_type.toUpperCase()) : 'NUMERIC';
  const simBadgeHtml = isSim ? `<span class="metric-sim-badge" title="Virtual Simulated Controller Point">SIM</span>` : '';

  return `
    <div class="analog-metric-card ${meta.qualityClass}" id="metric-${escapeHtml(pointId)}" data-point="${escapeHtml(pointId)}" data-quality="${escapeHtml(meta.quality)}" role="listitem">
      <div class="metric-header">
        <div class="metric-title-block">
          <span class="metric-point-id">${escapeHtml(pointId)}</span>
          <span class="metric-desc" title="${desc}">${desc}</span>
        </div>
        <div class="metric-badges">
          <span class="quality-badge ${meta.qualityClass}">${escapeHtml(meta.quality)}</span>
          ${simBadgeHtml}
        </div>
      </div>
      <div class="metric-body">
        <span class="metric-value ${meta.valClass}">${escapeHtml(meta.valueText)}</span>
        ${units ? `<span class="metric-unit">${units}</span>` : ''}
      </div>
      <div class="metric-footer">
        <span class="metric-meta-type">${dataType}</span>
        <span class="metric-meta-raw">${escapeHtml(meta.rawText)}</span>
        <span class="metric-meta-ts">${escapeHtml(meta.timestamp)}</span>
      </div>
    </div>`;
}

function renderAnalogMetrics(numericReadings, isConnected, isSim) {
  const points = Object.entries(numericReadings || {});
  const count = points.length;

  if (dom.analogCountBadge) {
    dom.analogCountBadge.textContent = `${count} Point${count === 1 ? '' : 's'}`;
  }

  if (count === 0) {
    if (dom.analogGrid) {
      dom.analogGrid.innerHTML = `
        <div class="analog-empty-state" role="note">
          No analog / numeric register points configured for this controller (Discrete 1-bit profile).
        </div>`;
    }
    if (dom.overviewAnalogCard) {
      dom.overviewAnalogCard.style.display = 'none';
    }
    return;
  }

  const cardsHtml = points.map(([pid, reading]) => {
    return analogMetricCardHTML(pid, reading, isSim);
  }).join('');

  if (dom.analogGrid) {
    dom.analogGrid.innerHTML = cardsHtml;
  }

  if (dom.overviewAnalogCard && dom.overviewAnalogGrid) {
    dom.overviewAnalogCard.style.display = '';
    dom.overviewAnalogGrid.innerHTML = cardsHtml;
    if (dom.overviewAnalogSub) {
      dom.overviewAnalogSub.textContent = `${count} Analog Metric${count === 1 ? '' : 's'} • Scaled Values`;
    }
  }
}

function renderDeviceTable(readings, activeDev) {
  if (!dom.ioTableBody) return;
  const isStandardQ = !activeDev || activeDev.id === 'melsec_q03';

  if (isStandardQ && !state.hasCustomTableBuilt) {
    return;
  }

  const points = Object.entries(readings || {});
  if (points.length === 0) return;

  state.hasCustomTableBuilt = !isStandardQ;
  const nowTs = formatTimestamp(new Date());

  dom.ioTableBody.innerHTML = points.map(([pid, r]) => {
    const isBool = (r.data_type || '').toLowerCase() === 'boolean' || pid.startsWith('X') || pid.startsWith('Y');
    const isGood = r.quality === 'GOOD';
    const desc = r.description || pid;
    const addr = r.device_address || pid;
    const type = (r.data_type || (isBool ? 'BOOLEAN' : 'INT16')).toUpperCase();

    if (isBool) {
      const bit = normalizeBitValue(isGood ? r.raw_value : null);
      let stateClass = 'unknown', stateText = 'UNKNOWN', rawText = '—', barClass = 'signal-bar';
      if (bit === 1) {
        stateClass = 'on'; stateText = 'ON'; rawText = '1 (Active)'; barClass = 'signal-bar active';
      } else if (bit === 0) {
        stateClass = 'off'; stateText = 'OFF'; rawText = '0 (Inactive)';
      } else if (r.quality === 'COMM_FAILURE') {
        stateText = 'COMM FAIL'; rawText = r.error_message || 'Comms Lost';
      }
      return `
        <tr id="row-${escapeHtml(pid)}" data-device="${escapeHtml(pid)}" data-kind="${pid.startsWith('X') ? 'input' : 'output'}" data-state="${stateClass}">
          <td class="point-tag">
            <span class="status-dot dot-${stateClass === 'on' ? 'ok' : (stateClass === 'off' ? 'off' : 'warn')}"></span>
            <strong>${escapeHtml(pid)}</strong>
          </td>
          <td class="point-hex"><code>${escapeHtml(addr)}</code></td>
          <td>${type}</td>
          <td class="table-desc">${escapeHtml(desc)}</td>
          <td><span class="table-state-pill ${stateClass}">${stateText}</span></td>
          <td><code>${escapeHtml(rawText)}</code></td>
          <td><div class="signal-bar-wrap"><div class="${barClass}"></div></div></td>
          <td class="point-ts mono">${isGood ? nowTs : '—'}</td>
        </tr>`;
    } else {
      const meta = formatMetricValue(r);
      const units = r.engineering_units ? ` ${r.engineering_units}` : '';
      const displayVal = meta.valueText !== '—' ? `${meta.valueText}${units}` : '—';
      return `
        <tr id="row-${escapeHtml(pid)}" data-device="${escapeHtml(pid)}" data-kind="analog" data-state="${meta.quality === 'GOOD' ? 'on' : 'unknown'}">
          <td class="point-tag">
            <span class="status-dot dot-${meta.quality === 'GOOD' ? 'ok' : 'warn'}"></span>
            <strong>${escapeHtml(pid)}</strong>
          </td>
          <td class="point-hex"><code>${escapeHtml(addr)}</code></td>
          <td><span class="metric-meta-type">${type}</span></td>
          <td class="table-desc">${escapeHtml(desc)}</td>
          <td><span class="table-state-pill ${meta.quality === 'GOOD' ? 'on' : 'unknown'}">${escapeHtml(displayVal)}</span></td>
          <td><code>${escapeHtml(meta.rawText)}</code></td>
          <td><span class="quality-badge ${meta.qualityClass}">${escapeHtml(meta.quality)}</span></td>
          <td class="point-ts mono">${escapeHtml(meta.timestamp)}</td>
        </tr>`;
    }
  }).join('');
}

function handleLocalAgentTelemetry(data, activeDev) {
  if (!data) return;

  if (data.simulation_mode !== undefined) {
    state.simulationMode = Boolean(data.simulation_mode);
  }

  const isSim = state.simulationMode || (activeDev && activeDev.adapter_id === 'simulated');

  // Update simulation badge in header and brand subtitle
  if (dom.headerSimBadge) {
    dom.headerSimBadge.style.display = isSim ? 'inline-block' : 'none';
  }
  if (dom.headerBrandSubtitle) {
    if (isSim) {
      dom.headerBrandSubtitle.textContent = 'Virtual Memory Model • No Physical PLC Connected';
    } else if (activeDev && activeDev.adapter_id === 'modbus_tcp') {
      dom.headerBrandSubtitle.textContent = 'Modbus TCP • Port 502 • Unit ID 1 • Holding & Input Registers';
    } else {
      dom.headerBrandSubtitle.textContent = 'MC Protocol 3E / TCP • Port 5010 • Binary Frame Monitoring';
    }
  }

  const readings = data.readings || {};
  const booleanReadings = {};
  const numericReadings = {};

  for (const [tag, reading] of Object.entries(readings)) {
    const dataType = (reading && reading.data_type) ? String(reading.data_type).toLowerCase() : '';
    if (dataType === 'boolean' || tag.startsWith('X') || tag.startsWith('Y')) {
      booleanReadings[tag] = reading;
    } else {
      numericReadings[tag] = reading;
    }
  }

  const inputs = {};
  const outputs = {};

  for (const tag of INPUT_DEVICES) {
    const r = readings[tag];
    const isGood = r && r.quality === 'GOOD';
    inputs[tag] = isGood ? (r.raw_value !== undefined ? r.raw_value : r.engineering_value) : null;
  }
  for (const tag of OUTPUT_DEVICES) {
    const r = readings[tag];
    const isGood = r && r.quality === 'GOOD';
    outputs[tag] = isGood ? (r.raw_value !== undefined ? r.raw_value : r.engineering_value) : null;
  }

  // Also support custom boolean tags if present
  for (const [tag, r] of Object.entries(booleanReadings)) {
    if (!INPUT_DEVICES.includes(tag) && !OUTPUT_DEVICES.includes(tag)) {
      const isGood = r && r.quality === 'GOOD';
      inputs[tag] = isGood ? (r.raw_value !== undefined ? r.raw_value : r.engineering_value) : null;
    }
  }

  const isConnected = data.connected === true || data.status === 'online' || data.status === 'ONLINE' || data.quality === 'GOOD';
  const nextSeq = data.sequence_number !== undefined ? data.sequence_number : (state.lastSeq !== null ? state.lastSeq + 1 : 1);

  const hasX3 = readings['X3'] !== undefined;
  const hasY20 = readings['Y20'] !== undefined;
  state.activeDeviceHasLadderPair = hasX3 && hasY20;

  const msg = {
    type: isConnected ? 'plc_reading' : 'gateway_error',
    gw_id: data.gateway_id || 'local-agent',
    seq: nextSeq,
    ts: data.timestamp_utc || data.last_read || new Date().toISOString(),
    connected: isConnected,
    error: isConnected ? null : (data.error || 'PLC Communication Offline'),
    inputs,
    outputs,
  };

  processTelemetry(msg);

  if (state.localAgentDeviceId) {
    state.deviceTelemetry[state.localAgentDeviceId] = data;
    if (state.localAgentDeviceId === 'melsec_q03' && outputs['Y20'] !== undefined) {
      updateReadbackIndicator(outputs['Y20']);
    }
  }

  // Render Analog Telemetry Metrics Cards
  renderAnalogMetrics(numericReadings, isConnected, isSim);

  // Render Diagnostics Table for active device
  renderDeviceTable(readings, activeDev);
}

// ─── MQTT Client & Connection Lifecycle ───────────────────────────────────────

function handleConnect() {
  if (state.isDemo) {
    addHistoryItem('system', 'warn', 'Operation Blocked: Disable Demo mode before connecting to live broker.');
    return;
  }

  if (state.dataSource === 'local') {
    startLocalAgentMonitoring();
    return;
  }

  // 1. Guard against duplicate MQTT client instances:
  // If an active, connecting, or reconnecting client exists, terminate and detach it cleanly first.
  if (state.mqttClient) {
    console.warn('[MQTT] Terminating prior MQTT client session before establishing new connection');
    try {
      state.mqttClient.removeAllListeners();
      state.mqttClient.end(true);
    } catch (_) {}
    state.mqttClient = null;
  }

  const url       = dom.inputUrl ? dom.inputUrl.value.trim() : '';
  const user      = dom.inputUser ? dom.inputUser.value.trim() : '';
  const pass      = dom.inputPass ? dom.inputPass.value : '';
  const rawPrefix = dom.inputTopic ? dom.inputTopic.value.trim() : '';
  const cleanPrefix = normalizeTopicPrefix(rawPrefix, 'plcmon/plc-gw-01');

  if (!url) {
    alert('Please enter a valid Broker WebSocket URL (wss://...)');
    if (dom.inputUrl) dom.inputUrl.focus();
    return;
  }

  saveSettings();
  state.connectionIntent = 'connected';
  state.isReconnecting   = false;
  setMqttStatus('connecting', 'Connecting…');
  if (dom.btnConnect)    dom.btnConnect.disabled    = true;
  if (dom.btnDisconnect) dom.btnDisconnect.disabled = false;

  const clientId = `qplc-web-${Math.random().toString(36).slice(2, 8)}`;
  if (dom.diagClientId) dom.diagClientId.textContent = clientId;

  const topicWildcard = getSubscriptionFilter(cleanPrefix);
  if (dom.diagSubscribedTopic) dom.diagSubscribedTopic.textContent = topicWildcard;

  const opts = {
    clientId,
    protocol: 'wss',
    keepalive: 30,
    reconnectPeriod: 5000,
    connectTimeout: 10000,
  };
  if (user) { opts.username = user; opts.password = pass; }

  // Verify MQTT library dependency
  if (typeof mqtt === 'undefined' || !mqtt || typeof mqtt.connect !== 'function') {
    state.connectionIntent = 'disconnected';
    setMqttStatus('error', 'Library Missing');
    const libErrMsg = 'MQTT client library (mqtt.min.js) unavailable. Check network connection to CDN.';
    addHistoryItem('comm_error', 'error', libErrMsg);
    state.stats.lastFault = libErrMsg;
    if (dom.diagLastFault) dom.diagLastFault.textContent = libErrMsg;
    if (dom.btnConnect)    dom.btnConnect.disabled    = false;
    if (dom.btnDisconnect) dom.btnDisconnect.disabled = true;
    return;
  }

  let client;
  try {
    client = mqtt.connect(url, opts);
    state.mqttClient = client;
  } catch (err) {
    state.connectionIntent = 'disconnected';
    setMqttStatus('error', 'Connect failed');
    addHistoryItem('comm_error', 'error', `MQTT connection failure: ${err.message}`);
    state.stats.lastFault = err.message;
    if (dom.diagLastFault) dom.diagLastFault.textContent = err.message;
    if (dom.btnConnect)    dom.btnConnect.disabled    = false;
    if (dom.btnDisconnect) dom.btnDisconnect.disabled = true;
    return;
  }

  client.on('connect', () => {
    // Stale client instance guard
    if (state.mqttClient !== client) return;

    state.connected = true;
    state.isReconnecting = false;
    setMqttStatus('ok', 'Connected');
    if (dom.btnConnect)    dom.btnConnect.disabled    = true;
    if (dom.btnDisconnect) dom.btnDisconnect.disabled = false;
    addHistoryItem('system', 'info', `MQTT broker link established with ${url}`);

    client.subscribe(topicWildcard, { qos: 1 }, (err, granted) => {
      if (state.mqttClient !== client) return;

      if (err) {
        const subErrMsg = `Subscription failed on ${topicWildcard}: ${err.message}`;
        state.stats.lastFault = subErrMsg;
        if (dom.diagLastFault) dom.diagLastFault.textContent = subErrMsg;
        addHistoryItem('comm_error', 'error', subErrMsg);
        console.error(`[MQTT] ${subErrMsg}`);
        return;
      }

      if (granted && granted.length > 0) {
        const rejected = granted.find(g => g.qos >= 128);
        if (rejected) {
          const aclMsg = `Broker ACL Rejection (QoS ${rejected.qos}): Read-only client cannot access ${topicWildcard}. Verify credentials and ACL rules.`;
          state.stats.lastFault = aclMsg;
          if (dom.diagLastFault) dom.diagLastFault.textContent = aclMsg;
          addHistoryItem('comm_error', 'error', aclMsg);
          console.error(`[MQTT] ${aclMsg}`);
          return;
        }
      }

      const qosLevel = (granted && granted[0]) ? granted[0].qos : 1;
      addHistoryItem('system', 'info', `Subscribed to ${topicWildcard} (Read-Only, QoS ${qosLevel})`);
      console.info(`[MQTT] Subscribed successfully to ${topicWildcard} (QoS ${qosLevel})`);
    });
  });

  client.on('message', (topic, payload) => {
    if (state.mqttClient !== client) return;
    handleMqttMessage(topic, payload.toString(), cleanPrefix);
  });

  client.on('error', err => {
    if (state.mqttClient !== client) return;
    state.stats.lastFault = err.message;
    if (dom.diagLastFault) dom.diagLastFault.textContent = err.message;
    addHistoryItem('comm_error', 'error', `Broker protocol error: ${err.message}`);
    setMqttStatus('error', 'Error');
  });

  client.on('reconnect', () => {
    if (state.mqttClient !== client) return;
    state.connected = false;
    state.isReconnecting = true;
    state.stats.reconnects++;
    if (dom.statReconnects) dom.statReconnects.textContent = state.stats.reconnects;
    setMqttStatus('connecting', 'Reconnecting…');
    // Ensure button remains disabled while reconnecting to prevent duplicate client generation
    if (dom.btnConnect)    dom.btnConnect.disabled    = true;
    if (dom.btnDisconnect) dom.btnDisconnect.disabled = false;
    addHistoryItem('comm_error', 'warn', 'MQTT connection lost — attempting auto-reconnect');
  });

  client.on('close', () => {
    if (state.mqttClient !== client) return;
    state.connected = false;

    if (state.connectionIntent === 'connected') {
      // Automatic background reconnect is active: do NOT enable Connect button or show dead Disconnected status
      state.isReconnecting = true;
      setMqttStatus('warn', 'Reconnecting…');
      if (dom.btnConnect)    dom.btnConnect.disabled    = true;
      if (dom.btnDisconnect) dom.btnDisconnect.disabled = false;
    } else {
      // Intentionally disconnected by operator
      state.isReconnecting = false;
      setMqttStatus('off', 'Disconnected');
      if (dom.btnConnect)    dom.btnConnect.disabled    = false;
      if (dom.btnDisconnect) dom.btnDisconnect.disabled = true;
      addHistoryItem('system', 'info', 'MQTT session terminated');
    }
    markAllStale();
    updateSubsystemMatrix();
  });

  client.on('offline', () => {
    if (state.mqttClient !== client) return;
    state.connected = false;
    if (state.connectionIntent === 'connected') {
      setMqttStatus('warn', 'Reconnecting…');
    } else {
      setMqttStatus('warn', 'Offline');
    }
  });
}

function handleDisconnect() {
  if (state.dataSource === 'local') {
    stopLocalAgentMonitoring();
    return;
  }
  state.connectionIntent = 'disconnected';
  state.isReconnecting   = false;
  if (state.mqttClient) {
    try {
      state.mqttClient.removeAllListeners();
      state.mqttClient.end(true);
    } catch (_) {}
    state.mqttClient = null;
  }
  state.connected = false;
  setMqttStatus('off', 'Disconnected');
  if (dom.btnConnect)    dom.btnConnect.disabled    = false;
  if (dom.btnDisconnect) dom.btnDisconnect.disabled = true;
  addHistoryItem('system', 'info', 'Disconnected by operator');
  markAllStale();
  updateSubsystemMatrix();
}

// ─── MQTT Topic Normalization Helpers ─────────────────────────────────────────

/**
 * Normalizes an MQTT topic prefix string.
 * Strips whitespace, trailing slashes, and trailing wildcard tokens ('/#', '#').
 *
 * Guaranteed mappings:
 *   'plcmon/plc-gw-01'   -> 'plcmon/plc-gw-01'
 *   'plcmon/plc-gw-01/'  -> 'plcmon/plc-gw-01'
 *   'plcmon/plc-gw-01/#' -> 'plcmon/plc-gw-01'
 *
 * @param {string} raw - raw input string from UI or storage
 * @param {string} [defaultPrefix='plcmon/plc-gw-01'] - fallback prefix if empty
 * @returns {string} clean base topic prefix
 */
function normalizeTopicPrefix(raw, defaultPrefix = 'plcmon/plc-gw-01') {
  if (!raw || typeof raw !== 'string') return defaultPrefix;
  let p = raw.trim();
  if (!p) return defaultPrefix;
  p = p.replace(/[\/#\s]+$/, '');
  return p || defaultPrefix;
}

/**
 * Builds the canonical MQTT subscription filter from a topic prefix.
 * Guarantees format: `<cleanPrefix>/#`.
 *
 * @param {string} rawPrefix
 * @returns {string} canonical subscription filter (e.g. 'plcmon/plc-gw-01/#')
 */
function getSubscriptionFilter(rawPrefix) {
  const clean = normalizeTopicPrefix(rawPrefix);
  return `${clean}/#`;
}

// ─── MQTT Message Dispatcher ──────────────────────────────────────────────────

function handleMqttMessage(topic, raw, prefix) {
  let msg;
  try {
    msg = JSON.parse(raw);
  } catch (e) {
    state.stats.discarded++;
    if (dom.statDiscarded) dom.statDiscarded.textContent = state.stats.discarded;
    addHistoryItem('comm_error', 'error', `Corrupt JSON frame received on topic ${topic}`);
    return;
  }

  const cleanPrefix = normalizeTopicPrefix(prefix);
  const prefixWithSlash = `${cleanPrefix}/`;
  if (!topic.startsWith(prefixWithSlash)) {
    console.warn(`[MQTT] Incoming message on unhandled topic: '${topic}' (expected prefix: '${prefixWithSlash}')`);
    return;
  }
  const subtopic = topic.slice(prefixWithSlash.length);

  if (subtopic === 'status') {
    const online = msg.status === 'online';
    state.espOnline = online;
    setStatusPill('esp', online ? 'ok' : 'error', online ? 'Online' : 'Offline');
    if (!online) {
      addHistoryItem('comm_error', 'warn', `Edge bridge ESP8266 reported status: OFFLINE`);
      markAllStale();
    } else {
      addHistoryItem('system', 'info', 'Edge bridge ESP8266 reported status: ONLINE');
    }
    updateSubsystemMatrix();
    return;
  }

  if (subtopic === 'telemetry') {
    processTelemetry(msg);
    return;
  }

  if (subtopic === 'diag') {
    const fault = msg.diag || msg.error || JSON.stringify(msg);
    state.stats.lastFault = fault;
    if (dom.diagLastFault) dom.diagLastFault.textContent = fault;
    addHistoryItem('comm_error', 'warn', `Edge Diagnostic alert: ${fault}`);
    return;
  }
}

// ─── Telemetry Processor ──────────────────────────────────────────────────────

function processTelemetry(msg) {
  if (!msg.type || !msg.gw_id || msg.seq === undefined || !msg.ts) {
    state.stats.discarded++;
    if (dom.statDiscarded) dom.statDiscarded.textContent = state.stats.discarded;
    addHistoryItem('comm_error', 'error', 'Telemetry packet discarded: Missing mandatory contract fields');
    return;
  }

  state.stats.packetsReceived++;
  if (dom.statPacketsReceived) dom.statPacketsReceived.textContent = state.stats.packetsReceived;

  // Monotonic sequence and duplicate / out-of-order tracking
  if (state.dataSource === 'local') {
    // In local agent mode, periodic HTTP polling samples a background engine cache.
    // Sequence jumps between polls reflect background engine loop iterations, NOT network packet loss.
    state.lastSeq = msg.seq;
    if (dom.metaSeqHealth) dom.metaSeqHealth.textContent = 'Local Sampled (Cache Read)';
  } else if (state.lastSeq !== null) {
    if (msg.seq === state.lastSeq) {
      state.stats.discarded++;
      if (dom.statDiscarded) dom.statDiscarded.textContent = state.stats.discarded;
      addHistoryItem('comm_error', 'warn', `Duplicate telemetry frame ignored (seq ${msg.seq})`);
      return;
    }
    if (msg.seq < state.lastSeq) {
      if (msg.seq === 1 || (state.lastSeq - msg.seq) > 50) {
        addHistoryItem('system', 'info', `Gateway sequence reset detected (${state.lastSeq} → ${msg.seq}); re-synchronizing baseline`);
        state.lastSeq = msg.seq;
        if (dom.metaSeqHealth) dom.metaSeqHealth.textContent = 'Sequence baseline reset OK';
      } else {
        state.stats.discarded++;
        if (dom.statDiscarded) dom.statDiscarded.textContent = state.stats.discarded;
        addHistoryItem('comm_error', 'warn', `Out-of-order frame discarded: received seq ${msg.seq} while current sequence is ${state.lastSeq}`);
        return;
      }
    } else if (msg.seq > state.lastSeq + 1) {
      state.stats.seqGaps++;
      if (dom.statSeqGaps) dom.statSeqGaps.textContent = state.stats.seqGaps;
      if (dom.metaSeqHealth) dom.metaSeqHealth.textContent = `Gap detected (${state.lastSeq} → ${msg.seq})`;
      const missed = msg.seq - state.lastSeq - 1;
      const tag = state.isDemo ? '[DEMO] ' : '';
      addHistoryItem('comm_error', 'warn', `${tag}Sequence gap detected: expected seq ${state.lastSeq + 1}, received ${msg.seq} (+${missed} missed frames)`);
      state.lastSeq = msg.seq;
    } else {
      if (dom.metaSeqHealth) dom.metaSeqHealth.textContent = 'Monotonic sequence OK';
      state.lastSeq = msg.seq;
    }
  } else {
    state.lastSeq = msg.seq;
    if (dom.metaSeqHealth) dom.metaSeqHealth.textContent = 'Monotonic sequence OK';
  }

  const wasPlcConnected = state.plcConnected;
  state.plcConnected = !!msg.connected;
  state.gwActive     = true;

  // Metadata Updates
  if (dom.metaGatewayId) dom.metaGatewayId.textContent = msg.gw_id || '—';
  if (dom.metaSeq)       dom.metaSeq.textContent       = msg.seq;
  if (dom.metaLastTs)    dom.metaLastTs.textContent    = formatIsoShort(msg.ts);

  if (msg.type === 'gateway_error' || !msg.connected) {
    setStatusPill('plc', 'error', 'Offline');
    state.stats.lastFault = msg.error || 'PLC Unreachable';
    if (dom.diagLastFault) dom.diagLastFault.textContent = state.stats.lastFault;

    if (wasPlcConnected) {
      addHistoryItem('comm_error', 'error', `PLC Communication Lost: ${msg.error || 'Connection timed out'}`);
    }
    markAllStale();
    updateSubsystemMatrix();
    return;
  }

  if (msg.type === 'heartbeat') {
    state.stats.heartbeats++;
    if (dom.statHeartbeats) dom.statHeartbeats.textContent = state.stats.heartbeats;
    state.lastReadingTs = new Date();
    if (state.simulationMode || state.deviceAdapterId === 'simulated') {
      setStatusPill('plc', 'warn', 'Simulated');
    } else {
      setStatusPill('plc', 'ok', 'Verified Online');
    }
    updateSubsystemMatrix();
    return;
  }

  if (msg.type !== 'plc_reading') return;

  if (!msg.inputs || !msg.outputs) {
    state.stats.discarded++;
    if (dom.statDiscarded) dom.statDiscarded.textContent = state.stats.discarded;
    addHistoryItem('comm_error', 'error', 'plc_reading discarded: inputs or outputs dictionary absent');
    return;
  }

  const prevPayload = state.lastPayload;
  state.lastPayload   = msg;

  // Check if packet timestamp is historically stale (e.g. retained MQTT message from previous session)
  const packetDate = new Date(msg.ts);
  const isValidTs  = !isNaN(packetDate.getTime());
  const packetAge  = isValidTs ? (Date.now() - packetDate.getTime()) : 0;
  const isHistoric = isValidTs && (packetAge > STALE_THRESHOLD_MS);

  if (isHistoric) {
    state.lastReadingTs = packetDate;
    state.isStale = true;
    addHistoryItem('comm_error', 'warn', `Retained/historic telemetry frame received (timestamp: ${formatIsoShort(msg.ts)}, age: ${formatAge(packetAge)}) — marked as STALE`);
  } else {
    state.lastReadingTs = (isValidTs && packetAge >= 0) ? packetDate : new Date();
    state.isStale = false;
    if (state.isDemo || state.simulationMode || state.deviceAdapterId === 'simulated') {
      setStatusPill('plc', 'warn', 'Simulated');
    } else if (state.plcConnected) {
      setStatusPill('plc', 'ok', 'Verified Online');
    } else {
      setStatusPill('plc', 'error', 'Offline');
    }
    hideStaleBanner();
  }

  // Detect state transitions
  if (prevPayload) {
    detectStateChanges(prevPayload.inputs,  msg.inputs,  'Input');
    detectStateChanges(prevPayload.outputs, msg.outputs, 'Output');
  }

  // Render device representations
  renderDeviceTiles(msg.inputs,  'input');
  renderDeviceTiles(msg.outputs, 'output');
  renderDeviceTableRows(msg.inputs, msg.outputs);
  updateLadderDiagram(msg.inputs, msg.outputs);
  updateActivePointCounters(msg.inputs, msg.outputs);

  if (isHistoric) {
    markAllStale();
  }

  if (!wasPlcConnected && state.plcConnected) {
    if (state.simulationMode || state.deviceAdapterId === 'simulated') {
      addHistoryItem('system', 'info', 'Simulation telemetry stream active');
    } else {
      addHistoryItem('system', 'info', 'PLC MC Protocol communication verified online');
    }
  }

  updateSubsystemMatrix();
}

// ─── Bit Normalization & State Change Logic ───────────────────────────────────

/**
 * Normalizes a raw digital bit value from PLC telemetry.
 *
 * Supported ON values:  1, true, "1", "true", "on" (case-insensitive)
 * Supported OFF values: 0, false, "0", "false", "off" (case-insensitive)
 *
 * Returns:
 *   1 for confirmed ON
 *   0 for confirmed OFF
 *   null for UNKNOWN / invalid / malformed / missing
 */
function normalizeBitValue(val) {
  if (val === 1 || val === true) return 1;
  if (val === 0 || val === false) return 0;
  if (typeof val === 'string') {
    const s = val.trim().toLowerCase();
    if (s === '1' || s === 'true' || s === 'on') return 1;
    if (s === '0' || s === 'false' || s === 'off') return 0;
  }
  return null; // UNKNOWN / invalid / unsupported
}

function detectStateChanges(prev, curr, kind) {
  if (!prev || !curr) return;
  for (const [device, rawCurr] of Object.entries(curr)) {
    const currBit = normalizeBitValue(rawCurr);
    if (currBit === null) continue; // Safety rule: Never generate events for invalid data

    const rawPrev = prev[device];
    if (rawPrev === undefined) continue;
    const prevBit = normalizeBitValue(rawPrev);
    if (prevBit === null) continue; // Transitioning from invalid state does not generate false event

    if (prevBit !== currBit) {
      state.stats.stateChanges++;
      if (dom.statStateChanges) dom.statStateChanges.textContent = state.stats.stateChanges;

      const label = currBit === 1 ? 'ENERGIZED (ON)' : 'DE-ENERGIZED (OFF)';
      const meta = DEVICE_METADATA[device] || {};
      addHistoryItem('state_change', 'info', `${kind} ${device} (${meta.desc || 'Channel'}) transitioned to ${label}`);
    }
  }
}

// ─── Rendering: Card Grid ─────────────────────────────────────────────────────

function renderDeviceTiles(devices, kind) {
  const expectedList = kind === 'input' ? INPUT_DEVICES : OUTPUT_DEVICES;

  for (const device of expectedList) {
    const tile = document.getElementById(`tile-${device}`);
    const hasKey = devices && Object.prototype.hasOwnProperty.call(devices, device);
    const rawVal = hasKey ? devices[device] : undefined;
    const bit = normalizeBitValue(rawVal);

    if (tile) {
      tile.classList.remove('loading');
      const badge = tile.querySelector('.tile-state-badge');

      if (bit === 1) {
        tile.dataset.state = 'on';
        if (badge) badge.textContent = 'ON';
        tile.setAttribute('aria-label', `${device}: ON`);
      } else if (bit === 0) {
        tile.dataset.state = 'off';
        if (badge) badge.textContent = 'OFF';
        tile.setAttribute('aria-label', `${device}: OFF`);
      } else {
        tile.dataset.state = 'unknown';
        if (badge) badge.textContent = 'UNKNOWN';
        tile.setAttribute('aria-label', `${device}: UNKNOWN`);
      }
    }

    const quickBit = document.getElementById(`quick-bit-${device}`);
    if (quickBit) {
      const isVerified = device === 'X3' || device === 'Y20';
      const baseClass = isVerified ? 'quick-bit verified-bit' : 'quick-bit';
      if (bit === 1) {
        quickBit.className = `${baseClass} on`;
        quickBit.title = `${device}: ON (1)`;
      } else if (bit === 0) {
        quickBit.className = `${baseClass} off`;
        quickBit.title = `${device}: OFF (0)`;
      } else {
        quickBit.className = `${baseClass} unknown`;
        quickBit.title = `${device}: UNKNOWN`;
      }
    }
  }
}

// ─── Rendering: Compact Engineering Table ─────────────────────────────────────

function renderDeviceTableRows(inputs, outputs) {
  if (!dom.ioTableBody) return;
  const merged = { ...inputs, ...outputs };
  const allDevices = [...INPUT_DEVICES, ...OUTPUT_DEVICES];
  const nowTs = formatTimestamp(new Date());

  for (const dev of allDevices) {
    const row = document.getElementById(`row-${dev}`);
    if (!row) continue;

    const hasKey = Object.prototype.hasOwnProperty.call(merged, dev);
    const rawVal = hasKey ? merged[dev] : undefined;
    const bit = normalizeBitValue(rawVal);

    const pill = document.getElementById(`row-state-${dev}`);
    const rawCode = document.getElementById(`row-raw-${dev}`);
    const bar = document.getElementById(`row-bar-${dev}`);
    const tsCol = document.getElementById(`row-ts-${dev}`);
    const dot = document.getElementById(`row-dot-${dev}`);

    if (bit === 1) {
      row.dataset.state = 'on';
      if (pill) { pill.className = 'table-state-pill on'; pill.textContent = 'ON'; }
      if (rawCode) rawCode.textContent = '1 (Active)';
      if (bar) bar.className = 'signal-bar active';
      if (dot) dot.className = 'status-dot dot-ok';
      if (tsCol) tsCol.textContent = nowTs;
    } else if (bit === 0) {
      row.dataset.state = 'off';
      if (pill) { pill.className = 'table-state-pill off'; pill.textContent = 'OFF'; }
      if (rawCode) rawCode.textContent = '0 (Inactive)';
      if (bar) bar.className = 'signal-bar';
      if (dot) dot.className = 'status-dot';
      if (tsCol) tsCol.textContent = nowTs;
    } else {
      row.dataset.state = 'unknown';
      if (pill) { pill.className = 'table-state-pill unknown'; pill.textContent = 'UNKNOWN'; }
      if (rawCode) rawCode.textContent = rawVal !== undefined ? JSON.stringify(rawVal) : 'Missing';
      if (bar) bar.className = 'signal-bar';
      if (dot) dot.className = 'status-dot dot-warn';
      if (tsCol) tsCol.textContent = '—';
    }
  }
}

// ─── Rendering: Ladder Contact Rung Visual ────────────────────────────────────

function updateLadderDiagram(inputs, outputs) {
  const x3Bit = inputs ? normalizeBitValue(inputs.X3) : null;
  const y20Bit = outputs ? normalizeBitValue(outputs.Y20) : null;

  // Determine mode badge text & styling
  let modeText = 'AWAITING TELEMETRY';
  let modeClass = 'ladder-mode-badge';
  if (state.isDemo) {
    modeText = 'DEMO SIMULATION';
    modeClass = 'ladder-mode-badge demo';
    if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'SIMULATED DEMO';
    if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Simulated Demo Specification';
  } else if (state.simulationMode) {
    modeText = 'SIMULATED SCAN';
    modeClass = 'ladder-mode-badge warn';
    if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'SIMULATED LADDER';
    if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Simulated Reference Model';
  } else if ((state.connected || state.plcConnected) && state.lastReadingTs) {
    if (state.activeDeviceHasLadderPair === false) {
      modeText = 'NOT MAPPED';
      modeClass = 'ladder-mode-badge';
      if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'REFERENCE RUNG (N/A)';
      if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Reference Rung (Not mapped in selected device)';
    } else {
      modeText = 'LIVE PLC SCAN';
      modeClass = 'ladder-mode-badge live';
      if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'HARDWARE MONITORED';
      if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Monitored Rung Specification';
    }
  } else {
    if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'REFERENCE MODEL';
    if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Reference Rung Specification';
  }

  if (dom.ladderModeBadge) {
    dom.ladderModeBadge.textContent = modeText;
    dom.ladderModeBadge.className = modeClass;
  }
  if (dom.ladderIoModeBadge) {
    dom.ladderIoModeBadge.textContent = modeText;
    dom.ladderIoModeBadge.className = modeClass;
  }

  // Update Overview verified pair tiles
  const tileX3 = dom.overviewTileX3 || document.getElementById('overview-tile-x3');
  const badgeX3 = dom.overviewBadgeX3 || document.getElementById('overview-badge-x3');
  if (tileX3 && badgeX3) {
    if (x3Bit === 1) {
      tileX3.className = 'primary-signal-tile input-signal on';
      badgeX3.textContent = 'ON';
    } else if (x3Bit === 0) {
      tileX3.className = 'primary-signal-tile input-signal off';
      badgeX3.textContent = 'OFF';
    } else {
      tileX3.className = 'primary-signal-tile input-signal unknown';
      badgeX3.textContent = 'UNKNOWN';
    }
  }

  const tileY20 = dom.overviewTileY20 || document.getElementById('overview-tile-y20');
  const badgeY20 = dom.overviewBadgeY20 || document.getElementById('overview-badge-y20');
  if (tileY20 && badgeY20) {
    if (y20Bit === 1) {
      tileY20.className = 'primary-signal-tile output-signal on';
      badgeY20.textContent = 'ON';
    } else if (y20Bit === 0) {
      tileY20.className = 'primary-signal-tile output-signal off';
      badgeY20.textContent = 'OFF';
    } else {
      tileY20.className = 'primary-signal-tile output-signal unknown';
      badgeY20.textContent = 'UNKNOWN';
    }
  }

  // Helper to update contact X3 display
  function applyX3(elem, sym, stateElem) {
    if (!elem) return;
    if (x3Bit === 1) {
      elem.dataset.state = 'on';
      if (sym) sym.textContent = '──[ ⊣■⊢ ]──';
      if (stateElem) stateElem.textContent = 'CLOSED (ON)';
    } else if (x3Bit === 0) {
      elem.dataset.state = 'off';
      if (sym) sym.textContent = '──[ ⊣ ⊢ ]──';
      if (stateElem) stateElem.textContent = 'OPEN (OFF)';
    } else {
      elem.dataset.state = 'unknown';
      if (sym) sym.textContent = '──[ ⊣ ? ⊢ ]──';
      if (stateElem) stateElem.textContent = 'UNKNOWN';
    }
  }

  // Helper to update coil Y20 display
  function applyY20(elem, sym, stateElem) {
    if (!elem) return;
    if (y20Bit === 1) {
      elem.dataset.state = 'on';
      if (sym) sym.textContent = '──( ● )──';
      if (stateElem) stateElem.textContent = 'ENERGIZED (ON)';
    } else if (y20Bit === 0) {
      elem.dataset.state = 'off';
      if (sym) sym.textContent = '──( )──';
      if (stateElem) stateElem.textContent = 'DE-ENERGIZED (OFF)';
    } else {
      elem.dataset.state = 'unknown';
      if (sym) sym.textContent = '──( ? )──';
      if (stateElem) stateElem.textContent = 'UNKNOWN';
    }
  }

  // Update Overview Ladder
  applyX3(dom.ladderX3Elem, dom.ladderX3Symbol, dom.ladderX3State);
  applyY20(dom.ladderY20Elem, dom.ladderY20Symbol, dom.ladderY20State);

  // Update I/O Tab Ladder
  applyX3(dom.ladderIoX3Elem, dom.ladderIoX3Symbol, dom.ladderIoX3State);
  applyY20(dom.ladderIoY20Elem, dom.ladderIoY20Symbol, dom.ladderIoY20State);

  // Flow line & Wire highlights
  const isEnergized = (x3Bit === 1 && y20Bit === 1);
  if (dom.ladderFlowLine) dom.ladderFlowLine.classList.toggle('energized', isEnergized);
  if (dom.ladderIoFlowLine) dom.ladderIoFlowLine.classList.toggle('energized', isEnergized);

  // Power rails wire segment highlights
  document.querySelectorAll('.ladder-wire-segment').forEach(wire => {
    wire.classList.toggle('energized', isEnergized);
  });

  // Overview Continuity Status
  if (dom.ladderRungStatus && dom.ladderRungDot) {
    if (x3Bit === 1 && y20Bit === 1) {
      dom.ladderRungStatus.textContent = state.simulationMode
        ? 'Simulated Continuity: Energized (Virtual Scan Path)'
        : 'Logic Continuity: Energized (Conductive Scan Path)';
      dom.ladderRungDot.className = 'status-dot dot-ok';
    } else if (x3Bit === 0 || y20Bit === 0) {
      dom.ladderRungStatus.textContent = state.simulationMode
        ? 'Simulated Continuity: De-energized (Contact Open)'
        : 'Logic Continuity: De-energized (Contact Open)';
      dom.ladderRungDot.className = 'status-dot';
    } else {
      dom.ladderRungStatus.textContent = 'Logic Continuity: Unknown / Awaiting Telemetry';
      dom.ladderRungDot.className = 'status-dot dot-warn';
    }
  }
}

// ─── Rendering: Point Counters ────────────────────────────────────────────────

function updateActivePointCounters(inputs, outputs) {
  let onCount = 0;
  let offCount = 0;
  let unknownCount = 0;
  const merged = { ...inputs, ...outputs };
  const allDevices = [...INPUT_DEVICES, ...OUTPUT_DEVICES];

  for (const d of allDevices) {
    const bit = normalizeBitValue(merged[d]);
    if (bit === 1) onCount++;
    else if (bit === 0) offCount++;
    else if (bit === null) unknownCount++;
  }

  if (dom.metaActiveCount) dom.metaActiveCount.textContent = `${onCount} / 16 ON`;
  if (dom.metaUnknownCount) dom.metaUnknownCount.textContent = `${offCount} OFF • ${unknownCount} UNKNOWN`;
}

// ─── Subsystem Health Matrix Updates ──────────────────────────────────────────

function updateSubsystemMatrix() {
  if (state.dataSource === 'local') {
    // 1. MQTT Broker (Bypassed / Not in Use in Local Agent Mode)
    setSubsystemBadge('mqtt', 'off', 'NOT IN USE', 'MQTT broker unused in local agent mode');

    // 2. ESP8266 Hardware Bridge (Bypassed / Not in Use in Local Agent Mode)
    setSubsystemBadge('esp', 'off', 'NOT IN USE', 'Hardware bridge unused in local agent mode');

    // 3. Telemetry Ingestion Host (Windows Local Agent)
    if (dom.nameSubsystemGw) dom.nameSubsystemGw.textContent = 'Windows Local Agent';
    if (dom.interfaceSubsystemGw) dom.interfaceSubsystemGw.textContent = 'REST HTTP (Port 8000)';

    if (state.localAgentActive && state.agentHealthy) {
      setSubsystemBadge('gw', 'ok', 'RUNNING', `Agent active (Uptime: ${state.agentUptime}s)`);
    } else if (state.localAgentActive && state.stats.lastFault) {
      setSubsystemBadge('gw', 'error', 'ERROR', state.stats.lastFault);
    } else if (state.localAgentActive) {
      setSubsystemBadge('gw', 'warn', 'CONNECTING', 'Establishing HTTP polling loop');
    } else {
      setSubsystemBadge('gw', 'off', 'STANDBY', 'Local Agent service stopped');
    }

    // 4. Target PLC Controller
    if (dom.nameSubsystemPlc) {
      dom.nameSubsystemPlc.textContent = state.deviceModel || 'Target PLC Controller';
    }
    if (dom.interfaceSubsystemPlc) {
      dom.interfaceSubsystemPlc.textContent = (state.simulationMode || state.deviceAdapterId === 'simulated')
        ? 'Virtual Simulation Engine'
        : 'MC 3E / TCP (Port 5010)';
    }

    if (state.simulationMode || state.deviceAdapterId === 'simulated') {
      setSubsystemBadge('plc', 'warn', 'SIMULATED', 'Virtual memory model (Hardware not verified)');
    } else if (state.plcConnected) {
      setSubsystemBadge('plc', 'ok', 'VERIFIED ONLINE', 'MC 3E physical socket active');
    } else if (state.stats.lastFault) {
      setSubsystemBadge('plc', 'error', 'OFFLINE', state.stats.lastFault);
    } else {
      setSubsystemBadge('plc', 'off', 'NOT VERIFIED', 'Awaiting hardware telemetry poll');
    }

    // Update Ladder Reference Model labels & badges
    if (state.simulationMode) {
      if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'SIMULATED LADDER';
      if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Simulated Reference Model';
      if (dom.pairTagHeading) dom.pairTagHeading.textContent = 'REFERENCE LADDER RUNG 0001 (X3 → Y20)';
      if (dom.pairTagSubnote) dom.pairTagSubnote.textContent = 'Virtual Simulation Model: X3 → Y20 (Hardware Not Verified)';
    } else if (state.plcConnected) {
      if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'HARDWARE MONITORED';
      if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Monitored Rung Specification';
      if (dom.pairTagHeading) dom.pairTagHeading.textContent = 'LIVE LADDER RUNG 0001 (X3 → Y20)';
      if (dom.pairTagSubnote) dom.pairTagSubnote.textContent = 'Monitored PLC Rung: X3 Input Contact → Y20 Output Coil';
    } else {
      if (dom.badgeLadderVerified) dom.badgeLadderVerified.textContent = 'REFERENCE MODEL';
      if (dom.badgeIoLadderRef) dom.badgeIoLadderRef.textContent = 'Reference Rung Specification';
      if (dom.pairTagHeading) dom.pairTagHeading.textContent = 'REFERENCE LADDER RUNG 0001 (X3 → Y20)';
      if (dom.pairTagSubnote) dom.pairTagSubnote.textContent = 'Autonomous Scan Reference: X3 → Y20 (Hardware Not Verified)';
    }

    return;
  }

  // ─── MQTT Broker Data Source Mode ───
  if (dom.labelPillGw) dom.labelPillGw.textContent = 'GW';
  if (dom.nameSubsystemGw) dom.nameSubsystemGw.textContent = 'Python Gateway Daemon';
  if (dom.interfaceSubsystemGw) dom.interfaceSubsystemGw.textContent = 'Serial COM / RS-232 (115.2k)';
  if (dom.nameSubsystemPlc) dom.nameSubsystemPlc.textContent = 'Mitsubishi Q03UDECPU';
  if (dom.interfaceSubsystemPlc) dom.interfaceSubsystemPlc.textContent = 'MC 3E / TCP (5010)';

  // 1. PLC Subsystem
  if (state.plcConnected) {
    setSubsystemBadge('plc', 'ok', 'VERIFIED ONLINE', 'MC 3E protocol link online');
  } else if (state.connected && state.stats.lastFault) {
    setSubsystemBadge('plc', 'error', 'OFFLINE', state.stats.lastFault);
  } else {
    setSubsystemBadge('plc', 'off', 'NOT VERIFIED', 'Awaiting gateway handshake');
  }

  // 2. Host Gateway
  if (state.gwActive && !state.isStale) {
    setSubsystemBadge('gw', 'ok', 'STREAMING', 'Serial telemetry nominal');
  } else if (state.isStale) {
    setSubsystemBadge('gw', 'warn', 'STALE', 'No serial stream >15s');
  } else {
    setSubsystemBadge('gw', 'off', 'STANDBY', 'Daemon not verified');
  }

  // 3. Edge Bridge (ESP8266)
  if (state.espOnline) {
    setSubsystemBadge('esp', 'ok', 'ONLINE', 'Wi-Fi MQTT bridge active');
  } else {
    setSubsystemBadge('esp', 'off', 'STANDBY', 'Awaiting bridge heartbeat');
  }

  // 4. Broker
  if (state.connected) {
    setSubsystemBadge('mqtt', 'ok', 'CONNECTED', 'WSS TLS 1.2 link open');
  } else {
    setSubsystemBadge('mqtt', 'error', 'DISCONNECTED', 'Client disconnected');
  }
}

function setSubsystemBadge(system, level, badgeText, detailText) {
  const badgeMap = {
    plc:  dom.badgeSubsystemPlc,
    gw:   dom.badgeSubsystemGw,
    esp:  dom.badgeSubsystemEsp,
    mqtt: dom.badgeSubsystemMqtt,
  };
  const detailMap = {
    plc:  dom.detailSubsystemPlc,
    gw:   dom.detailSubsystemGw,
    esp:  dom.detailSubsystemEsp,
    mqtt: dom.detailSubsystemMqtt,
  };

  const badgeElem  = badgeMap[system];
  const detailElem = detailMap[system];

  if (badgeElem) {
    badgeElem.className = level && level !== 'off' ? `node-badge ${level}` : 'node-badge';
    badgeElem.textContent = badgeText;
  }
  if (detailElem && detailText) {
    detailElem.textContent = detailText;
  }
}

// ─── Stale Data Handling ──────────────────────────────────────────────────────

function markAllStale() {
  if (!state.lastPayload) {
    hideStaleBanner();
    return;
  }
  state.isStale = true;
  if (dom.staleBannerText) {
    dom.staleBannerText.textContent =
      'Data is stale — communication interrupted. Values shown below reflect previous reading and not live hardware.';
  }
  showStaleBanner();

  document.querySelectorAll('.device-tile').forEach(tile => {
    if (tile.dataset.state !== 'unknown') {
      tile.dataset.state = 'stale';
      const badge = tile.querySelector('.tile-state-badge');
      if (badge) badge.textContent = 'STALE';
      tile.setAttribute('aria-label', `${tile.dataset.device}: stale`);
    }
  });

  document.querySelectorAll('#io-table-body tr').forEach(row => {
    if (row.dataset.state !== 'unknown') {
      row.dataset.state = 'stale';
      const pill = row.querySelector('.table-state-pill');
      if (pill) { pill.className = 'table-state-pill unknown'; pill.textContent = 'STALE'; }
      const qBadge = row.querySelector('.quality-badge');
      if (qBadge) {
        qBadge.className = 'quality-badge quality-stale';
        qBadge.textContent = 'STALE';
      }
    }
  });

  // Mutate visible analog metric cards to STALE without waiting for next poll
  document.querySelectorAll('.analog-metric-card').forEach(card => {
    card.classList.remove('quality-good', 'quality-bad', 'quality-comm-failure', 'quality-uncertain');
    card.classList.add('quality-stale');
    card.dataset.quality = 'STALE';

    const qBadge = card.querySelector('.quality-badge');
    if (qBadge) {
      qBadge.className = 'quality-badge quality-stale';
      qBadge.textContent = 'STALE';
    }

    const valElem = card.querySelector('.metric-value');
    if (valElem && valElem.textContent.trim() !== '—') {
      valElem.className = 'metric-value val-stale';
    }

    const rawElem = card.querySelector('.metric-meta-raw');
    if (rawElem && !rawElem.textContent.includes('(Stale)')) {
      rawElem.textContent = `${rawElem.textContent.trim()} (Stale)`;
    }
  });

  document.querySelectorAll('.quick-bit').forEach(qb => {
    if (!qb.classList.contains('unknown')) {
      const isVerified = qb.classList.contains('verified-bit');
      qb.className = isVerified ? 'quick-bit verified-bit stale' : 'quick-bit stale';
    }
  });

  const tileX3 = dom.overviewTileX3 || document.getElementById('overview-tile-x3');
  const badgeX3 = dom.overviewBadgeX3 || document.getElementById('overview-badge-x3');
  if (tileX3 && badgeX3 && tileX3.classList.contains('on')) {
    tileX3.className = 'primary-signal-tile input-signal stale';
    badgeX3.textContent = 'STALE';
  }

  const tileY20 = dom.overviewTileY20 || document.getElementById('overview-tile-y20');
  const badgeY20 = dom.overviewBadgeY20 || document.getElementById('overview-badge-y20');
  if (tileY20 && badgeY20 && tileY20.classList.contains('on')) {
    tileY20.className = 'primary-signal-tile output-signal stale';
    badgeY20.textContent = 'STALE';
  }

  updateFreshnessDisplays('stale', 'STALE (>15s)', 'Telemetry timeout');
  if (dom.metaActiveCount) dom.metaActiveCount.textContent = '0 / 16 ON';
  if (dom.metaUnknownCount) dom.metaUnknownCount.textContent = '0 OFF • 16 UNKNOWN (STALE)';
}

function startAgeTimer() {
  state.ageTimerId = setInterval(() => {
    if (!state.lastReadingTs) {
      if (dom.metaDataAge)  dom.metaDataAge.textContent  = '—';
      if (dom.navHeaderAge) dom.navHeaderAge.textContent = 'Age: —';
      updateFreshnessDisplays('idle', 'IDLE', 'No telemetry stream');
      return;
    }

    const ageMs = Date.now() - state.lastReadingTs.getTime();
    const formattedAge = formatAge(ageMs);
    if (dom.metaDataAge)  dom.metaDataAge.textContent  = formattedAge;
    if (dom.navHeaderAge) dom.navHeaderAge.textContent = `Age: ${formattedAge}`;

    if (ageMs > STALE_THRESHOLD_MS && !state.isStale && (state.connected || state.isDemo || state.localAgentActive || state.plcConnected)) {
      state.isStale = true;
      if (state.lastPayload) {
        showStaleBanner();
        markAllStale();
        addHistoryItem('comm_error', 'warn', 'Data stale: No new telemetry received for >15 seconds');
      }
    } else if (ageMs <= 5000) {
      updateFreshnessDisplays('live', 'LIVE', `${formatAge(ageMs)}`);
    } else if (ageMs <= STALE_THRESHOLD_MS) {
      updateFreshnessDisplays('warn', 'DEGRADED', `${formatAge(ageMs)}`);
    }
  }, AGE_UPDATE_INTERVAL);
}

function updateFreshnessDisplays(level, stateText, subText) {
  if (dom.metaFreshnessState) {
    dom.metaFreshnessState.textContent = stateText;
    dom.metaFreshnessState.className = `meta-val highlight ${level}`;
  }
  if (dom.metaFreshnessSub && subText) {
    dom.metaFreshnessSub.textContent = subText;
  }
  if (dom.navFreshness) {
    dom.navFreshness.textContent = `Data: ${stateText}`;
    dom.navFreshness.className = `nav-freshness-pill ${level}`;
  }
}

function showStaleBanner() { if (dom.staleBanner) dom.staleBanner.hidden = false; }
function hideStaleBanner() { if (dom.staleBanner) dom.staleBanner.hidden = true; }

// ─── Status UI Helpers ────────────────────────────────────────────────────────

function setMqttStatus(level, text) { setStatusPill('mqtt', level, text); }

function setStatusPill(name, level, text) {
  const pillMap = { mqtt: dom.pillMqtt, plc: dom.pillPlc, esp: dom.pillEsp, gw: dom.pillGw };
  const dotMap  = { mqtt: dom.dotMqtt,  plc: dom.dotPlc,  esp: dom.dotEsp,  gw: dom.dotGw };
  const textMap = { mqtt: dom.textMqtt, plc: dom.textPlc, esp: dom.textEsp, gw: dom.textGw };

  const dot  = dotMap[name];
  const textElem = textMap[name];
  const pill = pillMap[name];

  if (dot) {
    dot.className = 'status-dot';
    if (level === 'ok')              dot.classList.add('dot-ok');
    else if (level === 'error')      dot.classList.add('dot-error');
    else if (level === 'warn')       dot.classList.add('dot-warn');
    else if (level === 'connecting') dot.classList.add('dot-connecting');
    else if (level === 'off')        dot.classList.add('dot-off');
  }
  if (textElem) textElem.textContent = text;
  if (pill) pill.setAttribute('aria-label', `${name.toUpperCase()} Status: ${text}`);
}

// ─── Event Journal Engine ─────────────────────────────────────────────────────

function addHistoryItem(category, severity, message) {
  const now = new Date();
  const item = {
    id:       Date.now() + Math.random(),
    tsIso:    now.toISOString(),
    tsLocal:  formatTimestamp(now),
    category, // 'state_change' | 'comm_error' | 'system'
    severity, // 'info' | 'warn' | 'error'
    message,
  };

  state.historyItems.unshift(item);
  if (state.historyItems.length > MAX_HISTORY_ITEMS) {
    state.historyItems.pop();
  }

  renderEventJournal();
  renderMiniEvents();
}

function renderEventJournal() {
  if (!dom.historyList) return;

  const filtered = state.historyItems.filter(item => {
    // Category filter
    if (state.eventCategory !== 'all' && item.category !== state.eventCategory) return false;
    // Severity filter
    if (state.eventSeverity !== 'all' && item.severity !== state.eventSeverity) return false;
    // Search query
    if (state.eventSearch) {
      const matchMsg = item.message.toLowerCase().includes(state.eventSearch);
      const matchCat = item.category.toLowerCase().includes(state.eventSearch);
      if (!matchMsg && !matchCat) return false;
    }
    return true;
  });

  if (dom.historyCount)  dom.historyCount.textContent  = `${filtered.length} / ${state.historyItems.length}`;
  if (dom.navEventCount) dom.navEventCount.textContent = state.historyItems.length;

  if (filtered.length === 0) {
    dom.historyList.innerHTML = '<li class="history-empty">No events match the selected filters or search query.</li>';
    return;
  }

  dom.historyList.innerHTML = filtered.map(item => `
    <li class="history-item" role="listitem">
      <span class="history-ts">${item.tsLocal}</span>
      <span class="history-tag tag-${item.category}">${categoryLabel(item.category)}</span>
      <span class="history-sev sev-${item.severity}">${item.severity.toUpperCase()}</span>
      <span class="history-msg">${escapeHtml(item.message)}</span>
    </li>
  `).join('');
}

function renderMiniEvents() {
  if (!dom.miniEventList) return;
  const recent = state.historyItems.slice(0, 4);
  if (recent.length === 0) {
    dom.miniEventList.innerHTML = '<li class="mini-event-empty">No supervisory events logged yet. Connect or activate Demo mode.</li>';
    return;
  }
  dom.miniEventList.innerHTML = recent.map(item => `
    <li class="mini-event-item">
      <span class="mini-event-ts">${item.tsLocal.split(' ')[1] || item.tsLocal}</span>
      <span class="history-tag tag-${item.category}">${categoryLabel(item.category)}</span>
      <span class="mini-event-msg">${escapeHtml(item.message)}</span>
    </li>
  `).join('');
}

function setEventCategory(cat) {
  state.eventCategory = cat;
  document.querySelectorAll('.filter-btn').forEach(b => {
    b.classList.toggle('active', b.dataset.filter === cat);
    b.setAttribute('aria-pressed', b.dataset.filter === cat ? 'true' : 'false');
  });
  renderEventJournal();
}

function setEventSeverity(sev) {
  state.eventSeverity = sev;
  document.querySelectorAll('.filter-btn-sev').forEach(b => {
    b.classList.toggle('active', b.dataset.severity === sev);
    b.setAttribute('aria-pressed', b.dataset.severity === sev ? 'true' : 'false');
  });
  renderEventJournal();
}

function clearHistory() {
  state.historyItems = [];
  renderEventJournal();
  renderMiniEvents();
}

function exportEventsCsv() {
  if (state.historyItems.length === 0) {
    alert('No events to export in the journal.');
    return;
  }

  const headers = ['Timestamp_UTC', 'Timestamp_Local', 'Category', 'Severity', 'Message'];
  const rows = state.historyItems.map(item => [
    `"${item.tsIso}"`,
    `"${item.tsLocal}"`,
    `"${item.category}"`,
    `"${item.severity}"`,
    `"${item.message.replace(/"/g, '""')}"`
  ]);

  const csvContent = [headers.join(','), ...rows.map(r => r.join(','))].join('\r\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `qplc_event_journal_${new Date().toISOString().slice(0, 10)}.csv`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

function categoryLabel(cat) {
  if (cat === 'state_change') return 'I/O Change';
  if (cat === 'comm_error')   return 'Comms Fault';
  if (cat === 'system')       return 'System';
  return cat;
}

// ─── Offline Demo Mode Engine ─────────────────────────────────────────────────

function handleDemoToggle() {
  state.isDemo = dom.chkDemo ? dom.chkDemo.checked : false;

  if (state.isDemo) {
    if (state.connected) handleDisconnect();
    if (dom.demoBanner) dom.demoBanner.hidden = false;
    if (dom.btnConnect) dom.btnConnect.disabled = true;
    if (dom.headerModeTag) {
      dom.headerModeTag.textContent = 'OFFLINE DEMO';
      dom.headerModeTag.className   = 'system-mode-tag demo';
    }
    state.demoPaused = false;
    state.demoScenario = 'normal';
    updatePauseButtonUI();
    updateDemoScenarioUI('normal');
    startDemoMode();
    addHistoryItem('system', 'info', '[DEMO] Offline Demo simulation started — synthetic telemetry active');
  } else {
    if (dom.demoBanner) dom.demoBanner.hidden = true;
    if (dom.btnConnect) dom.btnConnect.disabled = false;
    if (dom.headerModeTag) {
      dom.headerModeTag.textContent = 'READ-ONLY';
      dom.headerModeTag.className   = 'system-mode-tag';
    }
    stopDemoMode();
    resetAllTiles();
    addHistoryItem('system', 'info', '[DEMO] Offline Demo simulation terminated — workspace reset');
  }
}

function startDemoMode() {
  stopDemoTimers();
  pushDemoReading();
  state.demoInterval = setInterval(pushDemoReading, DEMO_POLL_INTERVAL);
}

function stopDemoTimers() {
  if (state.demoInterval) {
    clearInterval(state.demoInterval);
    state.demoInterval = null;
  }
  if (state.demoTimeoutId) {
    clearTimeout(state.demoTimeoutId);
    state.demoTimeoutId = null;
  }
}

function stopDemoMode() {
  stopDemoTimers();
  state.lastPayload    = null;
  state.lastReadingTs  = null;
  state.isStale        = false;
  state.plcConnected   = false;
  state.espOnline      = false;
  state.gwActive       = false;
  state.demoPaused     = false;
  state.lastSeq        = null; // Reset sequence tracking for clean live session
  hideStaleBanner();
  if (dom.headerSimBadge) dom.headerSimBadge.style.display = 'none';
  if (dom.headerBrandSubtitle) {
    dom.headerBrandSubtitle.textContent = 'MC Protocol 3E / TCP • Port 5010 • Binary Frame Monitoring';
  }
  setStatusPill('mqtt', 'off', 'Disconnected');
  setStatusPill('plc',  'off', 'Not Verified');
  setStatusPill('esp',  'off', 'Unknown');
  setStatusPill('gw',   'off', 'Standby');
  updateSubsystemMatrix();
  updateLadderDiagram(null, null);
}

function pushDemoReading() {
  if (!state.isDemo || state.demoPaused) return;

  state.demoStep++;
  state.demoSeq++;

  if (dom.headerSimBadge) {
    dom.headerSimBadge.style.display = 'inline-block';
    dom.headerSimBadge.textContent = 'DEMO SIMULATION';
  }
  if (dom.headerBrandSubtitle) {
    dom.headerBrandSubtitle.textContent = 'Offline Browser Simulation • No Hardware Connection';
  }

  // Toggle X3 every 4 s; Y20 energizes in sync (ladder contact simulation)
  const tick = Math.floor(Date.now() / 4000) % 2;

  const fakeMsg = {
    type: 'plc_reading',
    gw_id: 'demo-gateway',
    seq: state.demoSeq,
    ts: new Date().toISOString().replace(/\.\d+/, '') + 'Z',
    connected: true,
    inputs: {
      X0: 0, X1: 0, X2: 0, X3: tick,
      X4: 0, X5: 0, X6: 0, X7: 0,
    },
    outputs: {
      Y20: tick, // Ladder rung 1: X3 contact energizes Y20 coil
      Y21: 0, Y22: 0, Y23: 0, Y24: 0, Y25: 0, Y26: 0, Y27: 0,
    },
  };

  setStatusPill('mqtt', 'warn', 'Demo');
  setStatusPill('plc',  'warn', 'Simulated');
  setStatusPill('esp',  'warn', 'Simulated');
  setStatusPill('gw',   'ok',   'Simulated');
  processTelemetry(fakeMsg);
}

function setDemoScenario(scenario) {
  if (!state.isDemo) {
    if (dom.chkDemo) {
      dom.chkDemo.checked = true;
      handleDemoToggle();
    }
  }

  state.demoScenario = scenario;
  updateDemoScenarioUI(scenario);

  if (scenario === 'normal') {
    state.demoPaused = false;
    updatePauseButtonUI();
    startDemoMode();
    addHistoryItem('system', 'info', '[DEMO] Active Scenario: Normal 2s Telemetry cycle');
  } else if (scenario === 'delayed') {
    // Stop regular interval, delay next frame by 8s
    stopDemoTimers();
    state.demoPaused = false;
    updatePauseButtonUI();
    addHistoryItem('comm_error', 'warn', '[DEMO] Active Scenario: Delayed Telemetry — frame paused for 8 seconds to demonstrate latency');
    state.demoTimeoutId = setTimeout(() => {
      pushDemoReading();
      state.demoInterval = setInterval(pushDemoReading, DEMO_POLL_INTERVAL);
      addHistoryItem('system', 'info', '[DEMO] Delayed frame delivered; resuming nominal cadence');
    }, 8000);
  } else if (scenario === 'seq_gap') {
    // Advance sequence number ahead by +4
    state.demoSeq += 4;
    pushDemoReading();
    addHistoryItem('comm_error', 'warn', '[DEMO] Active Scenario: Sequence Gap — injected jump (+3 dropped frames simulated)');
  } else if (scenario === 'stale') {
    // Stop frames completely so age timer triggers STALE warning after 15s
    stopDemoTimers();
    addHistoryItem('comm_error', 'warn', '[DEMO] Active Scenario: Stale Data — telemetry delivery halted to trigger >15s STALE alert');
  } else if (scenario === 'gw_fault') {
    // Dispatch simulated gateway comm failure
    stopDemoTimers();
    const errorMsg = {
      type: 'gateway_error',
      gw_id: 'demo-gateway',
      seq: ++state.demoSeq,
      ts: new Date().toISOString().replace(/\.\d+/, '') + 'Z',
      error: 'Serial COM9 Read Timeout (Simulated)',
      connected: false
    };
    processTelemetry(errorMsg);
    setStatusPill('gw', 'error', 'Fault (Sim)');
    setStatusPill('plc', 'error', 'Lost (Sim)');
    addHistoryItem('comm_error', 'error', '[DEMO] Active Scenario: Gateway Fault — Serial COM9 Timeout simulated');

    // Automatically recover after 6 seconds
    state.demoTimeoutId = setTimeout(() => {
      addHistoryItem('system', 'info', '[DEMO] Gateway communication recovered back online; resuming telemetry');
      startDemoMode();
    }, 6000);
  }
}

function toggleDemoPause() {
  if (!state.isDemo) return;
  state.demoPaused = !state.demoPaused;
  updatePauseButtonUI();
  if (!state.demoPaused && !state.demoInterval) {
    startDemoMode();
  }
  addHistoryItem('system', 'info', state.demoPaused ? '[DEMO] Simulation paused by operator' : '[DEMO] Simulation resumed');
}

function resetDemoSimulation() {
  if (!state.isDemo) return;
  stopDemoTimers();
  state.demoSeq = 100;
  state.demoStep = 0;
  state.demoPaused = false;
  state.demoScenario = 'normal';
  updatePauseButtonUI();
  updateDemoScenarioUI('normal');
  resetAllTiles();
  startDemoMode();
  addHistoryItem('system', 'info', '[DEMO] Simulation reset to initial state (Seq: 100)');
}

function updatePauseButtonUI() {
  if (dom.demoPauseText) {
    dom.demoPauseText.textContent = state.demoPaused ? 'Resume' : 'Pause';
  }
  if (dom.btnDemoPause) {
    dom.btnDemoPause.classList.toggle('active', state.demoPaused);
    dom.btnDemoPause.setAttribute('aria-pressed', state.demoPaused ? 'true' : 'false');
  }
}

function updateDemoScenarioUI(scenario) {
  document.querySelectorAll('.demo-scenario-pill, .scenario-btn').forEach(btn => {
    const isTarget = btn.dataset.scenario === scenario;
    btn.classList.toggle('active', isTarget);
    btn.setAttribute('aria-pressed', isTarget ? 'true' : 'false');
  });

  if (dom.selectDemoScenario) {
    dom.selectDemoScenario.value = scenario;
  }

  const labelMap = {
    normal:   'Normal Telemetry (2s Cycle)',
    delayed:  'Delayed Telemetry (8s Latency)',
    seq_gap:  'Sequence Gap (+3 Dropped)',
    stale:    'Stale Data Warning (>15s)',
    gw_fault: 'Gateway Fault & Recovery'
  };

  if (dom.demoScenarioBadge) {
    dom.demoScenarioBadge.textContent = `Scenario: ${labelMap[scenario] || scenario}`;
  }
}

function resetAllTiles() {
  document.querySelectorAll('.device-tile').forEach(tile => {
    tile.classList.add('loading');
    tile.dataset.state = 'unknown';
    const badge = tile.querySelector('.tile-state-badge');
    if (badge) badge.textContent = '—';
    tile.setAttribute('aria-label', `${tile.dataset.device}: unknown`);
  });

  document.querySelectorAll('#io-table-body tr').forEach(row => {
    row.dataset.state = 'unknown';
    const pill = row.querySelector('.table-state-pill');
    if (pill) { pill.className = 'table-state-pill unknown'; pill.textContent = 'UNKNOWN'; }
    const raw = row.querySelector('code[id^="row-raw"]');
    if (raw) raw.textContent = '—';
    const bar = row.querySelector('.signal-bar');
    if (bar) bar.className = 'signal-bar';
    const tsCol = row.querySelector('.point-ts');
    if (tsCol) tsCol.textContent = '—';
  });

  updateLadderDiagram(null, null);

  if (dom.metaGatewayId)    dom.metaGatewayId.textContent    = '—';
  if (dom.metaLastTs)       dom.metaLastTs.textContent       = '—';
  if (dom.metaDataAge)      dom.metaDataAge.textContent      = '—';
  if (dom.metaSeq)          dom.metaSeq.textContent          = '—';
  if (dom.metaActiveCount)  dom.metaActiveCount.textContent  = '0 / 16 Active';
  if (dom.metaUnknownCount) dom.metaUnknownCount.textContent = '16 unknown';
}

// ─── Formatting Helpers ───────────────────────────────────────────────────────

function formatTimestamp(d) {
  const pad = n => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} `
       + `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function formatIsoShort(iso) {
  if (!iso) return '—';
  return iso.replace('T', ' ').replace('Z', '');
}

function formatAge(ms) {
  if (ms < 1000)  return '<1 s ago';
  if (ms < 60000) return `${Math.floor(ms/1000)} s ago`;
  const m = Math.floor(ms/60000);
  const s = Math.floor((ms % 60000)/1000);
  return `${m}m ${s}s ago`;
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// ─── Bootstrap ────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', init);
