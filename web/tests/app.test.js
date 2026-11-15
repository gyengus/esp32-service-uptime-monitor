import { describe, it, expect, beforeAll, beforeEach, afterEach, vi } from 'vitest';
import fs from 'fs';
import path from 'path';

describe('Uptime Monitor Web UI - app.js', () => {
  let $;

  beforeAll(async () => {
    const htmlPath = path.resolve(__dirname, '../index.html');
    const html = fs.readFileSync(htmlPath, 'utf8');
    document.documentElement.innerHTML = html;
    $ = window.$;
    await import('../static/app.js');

    Object.defineProperties(window, {
      allEntities: { get() { return window.get_allEntities(); }, set(v) { window.set_allEntities(v); }, configurable: true },
      currentFilter: { get() { return window.get_currentFilter(); }, set(v) { window.set_currentFilter(v); }, configurable: true },
      entityToDelete: { get() { return window.get_entityToDelete(); }, set(v) { window.set_entityToDelete(v); }, configurable: true }
    });
  });

  beforeEach(() => {
    vi.useFakeTimers();

    // Reset modals and inputs
    window.set_currentFilter('ALL');
    window.set_entityToDelete('');
    $('#entityForm')[0]?.reset();
    $('#id').val('');
    $('#name').val('');
    $('#address').val('');
    $('#icon').val('');
    $('#notification').prop('checked', false);
    $('#disabled').prop('checked', false);

    // Mock AJAX
    $.ajax = vi.fn().mockImplementation((options) => {
      if (options.success) {
        options.success({});
      }
      return {
        fail: vi.fn().mockReturnThis(),
        always: vi.fn().mockImplementation((cb) => { cb(); return this; })
      };
    });
    $.getJSON = vi.fn().mockImplementation((url, cb) => {
      if (url === '/api/summary') {
        cb({ total: 3, online: 2, offline: 1 });
      } else if (url === '/api/monitoredentities') {
        cb([
          { name: 'Alpha', address: 'http://alpha.local', icon: 'mdi-server', isOnline: true, notification: true, disabled: false, lastChanged: 1700000000 },
          { name: 'Beta', address: 'https://beta.local', icon: 'mdi-web', isOnline: false, notification: false, disabled: false, lastChanged: 1700000100 },
          { name: 'Gamma', address: 'http://gamma.local', icon: 'mdi-database', isOnline: false, notification: true, disabled: true, lastChanged: 0 }
        ]);
      } else if (url.startsWith('/api/monitoredentity/')) {
        cb({
          name: 'Alpha',
          address: 'http://alpha.local',
          icon: 'mdi-server',
          notification: true,
          disabled: false
        });
      } else if (url === '/api/sysinfo') {
        cb({
          uptime: 3665,
          rssi: -55,
          freeHeap: 120000,
          temperature: 42.5,
          version: '1.2.3',
          commit: 'abcdef1',
          buildDate: '2026-11-09',
          configEnabled: true
        });
      }
      return {
        fail: vi.fn().mockReturnThis(),
        always: vi.fn().mockImplementation((alwaysCb) => { alwaysCb(); return this; })
      };
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  describe('formatUptime helper', () => {
    it('formats minutes correctly', () => {
      expect(window.formatUptime(60)).toBe('1m');
      expect(window.formatUptime(120)).toBe('2m');
    });

    it('formats hours and minutes', () => {
      expect(window.formatUptime(3665)).toBe('1h 1m');
    });

    it('formats days, hours and minutes', () => {
      expect(window.formatUptime(90060)).toBe('1d 1h 1m');
    });

    it('returns -- for negative or non-number values', () => {
      expect(window.formatUptime(-1)).toBe('--');
      expect(window.formatUptime(NaN)).toBe('--');
      expect(window.formatUptime('invalid')).toBe('--');
    });
  });

  describe('loadSummary', () => {
    it('updates navigation stat counters', () => {
      window.loadSummary();
      expect($('#navTotal').text()).toBe('3');
      expect($('#navOnline').text()).toBe('2');
      expect($('#navOffline').text()).toBe('1');
    });
  });

  describe('loadEntities and renderEntities', () => {
    it('renders all entities in alphabetical order with correct status classes', () => {
      window.loadEntities();
      const cards = $('#entityGrid .monitoredEntity');
      expect(cards.length).toBe(3);

      // Alpha is online -> text-bg-success
      expect(cards.eq(0).hasClass('text-bg-success')).toBe(true);
      expect(cards.eq(0).text()).toContain('Alpha');

      // Beta is offline -> text-bg-danger
      expect(cards.eq(1).hasClass('text-bg-danger')).toBe(true);
      expect(cards.eq(1).text()).toContain('Beta');

      // Gamma is disabled -> text-bg-secondary
      expect(cards.eq(2).hasClass('text-bg-secondary')).toBe(true);
      expect(cards.eq(2).text()).toContain('Gamma');
    });

    it('filters by ONLINE status when #filterOnline is clicked', () => {
      window.loadEntities();
      $('#filterOnline').trigger('click');

      const cards = $('#entityGrid .monitoredEntity');
      expect(cards.length).toBe(1);
      expect(cards.eq(0).text()).toContain('Alpha');
      expect($('#filterOnline').hasClass('badge-active')).toBe(true);
    });

    it('filters by OFFLINE status when #filterOffline is clicked', () => {
      window.loadEntities();
      $('#filterOffline').trigger('click');

      const cards = $('#entityGrid .monitoredEntity');
      expect(cards.length).toBe(1);
      expect(cards.eq(0).text()).toContain('Beta');
      expect($('#filterOffline').hasClass('badge-active')).toBe(true);
    });

    it('displays contextual empty state when there are no entities and when filter produces no results', () => {
      window.loadEntities();
      window.allEntities = [];
      window.renderEntities();
      expect($('#entityGrid').text()).toContain('No monitored services yet');
      expect($('#emptyAddButton').length).toBe(1);

      window.allEntities = [
        { name: 'Alpha', address: 'http://alpha.local', icon: 'mdi-server', isOnline: true, notification: false, disabled: false, lastChanged: 1700000000 }
      ];
      $('#filterOffline').trigger('click');
      expect($('#entityGrid').text()).toContain('No services match the current filter');
      expect($('#emptyShowAllButton').length).toBe(1);

      $('#emptyShowAllButton').trigger('click');
      expect(window.get_currentFilter()).toBe('ALL');
      expect($('#entityGrid .monitoredEntity').length).toBe(1);
    });

    it('renders status badges and accessible contrast classes for Pending and colored cards', () => {
      window.set_allEntities([
        { name: 'OnlineSvc', address: 'http://online.local', icon: 'mdi-server', isOnline: true, notification: false, disabled: false, lastChanged: 1700000000 },
        { name: 'OfflineSvc', address: 'http://offline.local', icon: 'mdi-server', isOnline: false, notification: false, disabled: false, lastChanged: 1700000000 },
        { name: 'PendingSvc', address: 'http://pending.local', icon: 'mdi-server', isOnline: false, notification: false, disabled: false, lastChanged: 0 },
        { name: 'DisabledSvc', address: 'http://disabled.local', icon: 'mdi-server', isOnline: false, notification: false, disabled: true, lastChanged: 0 }
      ]);
      window.renderEntities();

      const pendingCard = $('.monitoredEntity[data-entity-name="PendingSvc"]');
      expect(pendingCard.hasClass('text-bg-light')).toBe(true);
      expect(pendingCard.find('a').hasClass('link-dark')).toBe(true);
      expect(pendingCard.text()).toContain('Pending');

      const onlineCard = $('.monitoredEntity[data-entity-name="OnlineSvc"]');
      expect(onlineCard.find('a').hasClass('link-light')).toBe(true);
      expect(onlineCard.text()).toContain('Online');

      expect($('.monitoredEntity[data-entity-name="OfflineSvc"]').text()).toContain('Offline');
      expect($('.monitoredEntity[data-entity-name="DisabledSvc"]').text()).toContain('Disabled');
    });

    it('updates aria-pressed on filter buttons', () => {
      window.loadEntities();
      expect($('#filterAll').attr('aria-pressed')).toBe('true');
      expect($('#filterOnline').attr('aria-pressed')).toBe('false');

      $('#filterOnline').trigger('click');
      expect($('#filterAll').attr('aria-pressed')).toBe('false');
      expect($('#filterOnline').attr('aria-pressed')).toBe('true');
    });

    it('renders clickable address links with http/https normalization', () => {
      window.loadEntities();
      const links = $('#entityGrid a');
      expect(links.length).toBe(3);
      expect(links.eq(0).attr('href')).toBe('http://alpha.local');
      expect(links.eq(1).attr('href')).toBe('https://beta.local');
    });
  });

  describe('System Statistics (loadSystemStats)', () => {
    it('updates system info footer and sets green RSSI for strong signal', () => {
      window.loadSystemStats();
      expect($('#uptimeVal').text()).toBe('1h 1m');
      expect($('#rssiVal').text()).toBe('-55 dBm');
      expect($('#rssiVal').hasClass('text-success')).toBe(true);
      expect($('#versionVal').text()).toBe('1.2.3');
      expect($('#configButton').css('display')).not.toBe('none');
    });

    it('sets warning class for medium signal and danger for weak signal', () => {
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        cb({ uptime: 100, rssi: -70, freeHeap: 50000, temperature: 30, configEnabled: false });
      });
      window.loadSystemStats();
      expect($('#rssiVal').hasClass('text-warning')).toBe(true);
      expect($('#configButton').css('display')).toBe('none');

      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        cb({ uptime: 100, rssi: -85, freeHeap: 50000, temperature: 30, configEnabled: false });
      });
      window.loadSystemStats();
      expect($('#rssiVal').hasClass('text-danger')).toBe(true);
    });
  });

  describe('Entity Modal and CRUD actions', () => {
    it('resets form when modal is hidden', () => {
      $('#name').val('Temp');
      $('#id').val('Temp');
      $('#disabledEnabled').prop('checked', true);

      $('#entityModal').trigger('hidden.bs.modal');
      expect($('#name').val()).toBe('');
      expect($('#id').val()).toBe('');
      expect($('#disabledEnabled').is(':checked')).toBe(false);
    });

    it('validates form on save: does not submit when required fields are missing', () => {
      $('#name').val('');
      $('#address').val('');
      $('#saveButton').trigger('click');

      expect($('#entityForm').hasClass('was-validated')).toBe(true);
      expect($.ajax).not.toHaveBeenCalled();
    });

    it('submits POST /api/monitoredentity when creating a new entity', () => {
      $('#name').val('NewService');
      $('#address').val('http://new.service');
      $('#icon').val('mdi-server');
      $('#notificationsEnabled').prop('checked', true);
      $('#disabledEnabled').prop('checked', false);
      $('#id').val('');

      $('#saveButton').trigger('click');

      expect($.ajax).toHaveBeenCalledWith(expect.objectContaining({
        url: '/api/monitoredentity',
        type: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({
          name: 'NewService',
          address: 'http://new.service',
          icon: 'mdi-server',
          notification: true,
          disabled: false
        })
      }));
    });

    it('submits PUT /api/monitoredentity/{id} when updating an existing entity', () => {
      $('#name').val('UpdatedService');
      $('#address').val('http://updated.service');
      $('#icon').val('mdi-cloud');
      $('#notificationsEnabled').prop('checked', false);
      $('#disabledEnabled').prop('checked', true);
      $('#id').val('OldService');

      $('#saveButton').trigger('click');

      expect($.ajax).toHaveBeenCalledWith(expect.objectContaining({
        url: '/api/monitoredentity/OldService',
        type: 'PUT',
        contentType: 'application/json'
      }));
    });

    it('populates modal fields when edit button is clicked', () => {
      window.loadEntities();
      $('.btn-edit').first().trigger('click');

      expect($.getJSON).toHaveBeenCalledWith('/api/monitoredentity/Alpha', expect.any(Function));
      expect($('#name').val()).toBe('Alpha');
      expect($('#address').val()).toBe('http://alpha.local');
      expect($('#id').val()).toBe('Alpha');
    });

    it('opens delete confirmation modal and executes DELETE', () => {
      window.loadEntities();
      $('.btn-delete').first().trigger('click');
      expect(window.entityToDelete).toBe('Alpha');
      expect($('#deleteEntityNameDisplay').text()).toBe('Alpha');

      $('#confirmDeleteButton').trigger('click');
      expect($.ajax).toHaveBeenCalledWith(expect.objectContaining({
        url: '/api/monitoredentity/Alpha',
        type: 'DELETE'
      }));
    });
  });

  describe('Toast Notifications', () => {
    it('shows success and error toasts', () => {
      window.showSuccess('Operation successful');
      expect($('#successToast .toast-body').text()).toBe('Operation successful');
      expect($('#successToast').hasClass('show')).toBe(true);

      window.showError('Operation failed');
      expect($('#errorToast .toast-body').text()).toBe('Operation failed');
      expect($('#errorToast').hasClass('show')).toBe(true);
    });
  });

  describe('WebSocket Real-Time Updates & Pulse Animation', () => {
    it('updates connection status dot', () => {
      window.setWsStatus(true);
      expect($('#wsStatusDot').hasClass('bg-success')).toBe(true);
      expect($('#wsStatusDot').attr('title')).toContain('Live');

      window.setWsStatus(false);
      expect($('#wsStatusDot').hasClass('bg-danger')).toBe(true);
      expect($('#wsStatusDot').attr('title')).toContain('Disconnected');
    });

    it('handles init message correctly', () => {
      window.handleWsMessage({
        type: 'init',
        entities: [
          { name: 'WsEntity1', address: 'http://ws1.local', icon: 'mdi-lan', isOnline: true, notification: false, disabled: false, lastChanged: 1700000000 }
        ],
        summary: { total: 1, online: 1, offline: 0 },
        sysinfo: { uptime: 200, rssi: -50, freeHeap: 80000, temperature: 40, version: '2.0.0', configEnabled: false }
      });

      expect(window.get_allEntities().length).toBe(1);
      expect($('#entityGrid .monitoredEntity').length).toBe(1);
      expect($('#navTotal').text()).toBe('1');
      expect($('#uptimeVal').text()).toBe('3m');
    });

    it('handles entity_update message and triggers pulse animation on status change', () => {
      window.set_allEntities([
        { name: 'Server1', address: 'http://server1.local', icon: 'mdi-server', isOnline: true, notification: false, disabled: false, lastChanged: 100 }
      ]);
      window.renderEntities();

      const card = $('.monitoredEntity[data-entity-name="Server1"]');
      expect(card.hasClass('text-bg-success')).toBe(true);

      // Status change to offline -> should update class and trigger pulse animation
      window.handleWsMessage({
        type: 'entity_update',
        entity: { name: 'Server1', address: 'http://server1.local', icon: 'mdi-server', isOnline: false, notification: false, disabled: false, lastChanged: 200 }
      });

      const updatedCard = $('.monitoredEntity[data-entity-name="Server1"]');
      expect(updatedCard.hasClass('text-bg-danger')).toBe(true);
      expect(updatedCard.hasClass('card-status-pulse')).toBe(true);

      // Fast-forward timer to clear pulse class
      vi.advanceTimersByTime(900);
      expect(updatedCard.hasClass('card-status-pulse')).toBe(false);
    });

    it('handles entity_delete message and removes card from DOM', () => {
      window.set_allEntities([
        { name: 'KeepMe', address: 'http://keep.local', icon: 'mdi-server', isOnline: true, notification: false, disabled: false, lastChanged: 100 },
        { name: 'DeleteMe', address: 'http://del.local', icon: 'mdi-server', isOnline: true, notification: false, disabled: false, lastChanged: 100 }
      ]);
      window.renderEntities();
      expect($('#entityGrid .monitoredEntity').length).toBe(2);

      window.handleWsMessage({
        type: 'entity_delete',
        name: 'DeleteMe'
      });

      expect(window.get_allEntities().length).toBe(1);
      expect($('#entityGrid .monitoredEntity').length).toBe(1);
      expect($('.monitoredEntity[data-entity-name="DeleteMe"]').length).toBe(0);
    });

    it('handles real-time summary update message', () => {
      window.handleWsMessage({
        type: 'summary',
        data: { total: 10, online: 8, offline: 2 }
      });

      expect($('#navTotal').text()).toBe('10');
      expect($('#navOnline').text()).toBe('8');
      expect($('#navOffline').text()).toBe('2');
    });

    it('handles real-time sysinfo update message', () => {
      window.handleWsMessage({
        type: 'sysinfo',
        data: { uptime: 7200, rssi: -45, freeHeap: 90000, temperature: 44.2, configEnabled: true }
      });

      expect($('#uptimeVal').text()).toBe('2h 0m');
      expect($('#rssiVal').text()).toBe('-45 dBm');
      expect($('#tempVal').text()).toBe('44.2 °C');
      expect($('#tempContainer').css('display')).not.toBe('none');
      expect($('#configButton').css('display')).not.toBe('none');
    });

    it('hides temperature container when temperature is not supported or missing', () => {
      window.handleWsMessage({
        type: 'sysinfo',
        data: { uptime: 7200, rssi: -45, freeHeap: 90000, configEnabled: true }
      });

      expect($('#tempContainer').css('display')).toBe('none');
    });
  });

  describe('Terminal Drawer & Log Streaming', () => {
    let mockWs;

    beforeEach(() => {
      mockWs = {
        readyState: 1, // WebSocket.OPEN
        send: vi.fn(),
        close: vi.fn()
      };
      window.set_ws(mockWs);
      window.clearTerminal();
      window.toggleTerminal(false);
    });

    it('toggles terminal drawer open and closed, sending subscription messages', () => {
      expect($('#terminalDrawer').hasClass('open')).toBe(false);

      // Open terminal
      $('#btnToggleTerminal').click();
      expect($('#terminalDrawer').hasClass('open')).toBe(true);
      expect(mockWs.send).toHaveBeenCalledWith(JSON.stringify({ type: 'subscribe_logs', enabled: true }));

      // Close terminal via close button
      $('#btnTerminalClose').click();
      expect($('#terminalDrawer').hasClass('open')).toBe(false);
      expect(mockWs.send).toHaveBeenCalledWith(JSON.stringify({ type: 'subscribe_logs', enabled: false }));
    });

    it('renders incoming log lines with appropriate severity classes and updates line count', () => {
      window.handleWsMessage({ type: 'log', line: '2026-11-09 [DEBUG] Scanning WiFi networks' });
      window.handleWsMessage({ type: 'log', line: '2026-11-09 [INFO] WiFi connected successfully' });
      window.handleWsMessage({ type: 'log', line: '2026-11-09 [WARNING] High memory usage detected' });
      window.handleWsMessage({ type: 'log', line: '2026-11-09 [ERROR] Connection timed out' });

      const entries = $('#terminalLogs .terminal-log-entry');
      expect(entries.length).toBe(4);
      expect(entries.eq(0).hasClass('terminal-log-level-debug')).toBe(true);
      expect(entries.eq(1).hasClass('terminal-log-level-info')).toBe(true);
      expect(entries.eq(2).hasClass('terminal-log-level-warning')).toBe(true);
      expect(entries.eq(3).hasClass('terminal-log-level-error')).toBe(true);

      expect($('#terminalLineCount').text()).toBe('4 lines');
    });

    it('clears terminal logs on clear button click', () => {
      window.handleWsMessage({ type: 'log', line: 'Some test log message' });
      expect($('#terminalLogs .terminal-log-entry').length).toBe(1);

      $('#btnTerminalClear').click();
      expect($('#terminalLogs .terminal-log-entry').length).toBe(0);
      expect($('#terminalLineCount').text()).toBe('0 lines');
    });

    it('handles log_level message and sends set_log_level on dropdown change, disabled when locked in release', () => {
      // Remote device informs about log level
      window.handleWsMessage({ type: 'log_level', name: 'WARNING', level: 5 });
      expect($('#terminalLogLevel').val()).toBe('WARNING');

      // Disabled in release build where canChangeLogLevel is false
      window.handleWsMessage({ type: 'init', canChangeLogLevel: false, logLevelName: 'INFO' });
      expect($('#terminalLogLevel').prop('disabled')).toBe(true);
      expect($('#terminalLogLevel').val()).toBe('INFO');
      mockWs.send.mockClear();
      $('#terminalLogLevel').val('DEBUG').trigger('change');
      expect(mockWs.send).not.toHaveBeenCalled();

      // Enabled in dev mode where canChangeLogLevel is true
      window.handleWsMessage({ type: 'init', canChangeLogLevel: true, logLevelName: 'DEBUG' });
      expect($('#terminalLogLevel').prop('disabled')).toBe(false);
      expect($('#terminalLogLevel').val()).toBe('DEBUG');
      $('#terminalLogLevel').val('ERROR').trigger('change');
      expect(mockWs.send).toHaveBeenCalledWith(JSON.stringify({ type: 'set_log_level', level: 'ERROR' }));
    });
  });

  describe('System Metrics Modal', () => {
    it('places the modal trigger button in the header menu and removes it from the footer', () => {
      expect($('footer #btnSysinfoModal').length).toBe(0);
      const $btn = $('#headerMenuDropdown #menuSysinfoButton');
      expect($btn.length).toBe(1);
      expect($btn.attr('data-bs-target')).toBe('#sysinfoModal');
    });

    it('formats byte values accurately', () => {
      expect(window.formatBytes(512)).toBe('512 B');
      expect(window.formatBytes(2048)).toBe('2.0 KB');
      expect(window.formatBytes(2 * 1024 * 1024)).toBe('2.00 MB');
      expect(window.formatBytes(4 * 1024 * 1024 * 1024)).toBe('4.00 GB');
    });

    it('loads and renders detailed system metrics including maxFreeBlock, SD storage, and UI version mismatch', () => {
      const sampleDetails = {
        uptime: 7200,
        rssi: -52,
        temperature: 43.2,
        localTime: '2026-11-11 11:30:00',
        timeSynced: true,
        configEnabled: true,
        device: {
          board: 'ESP32-S3-CAM',
          chipModel: 'ESP32-S3',
          chipRevision: 2,
          chipCores: 2,
          cpuFreqMHz: 240,
          resetReason: 'POWERON',
          efuseMac: 'AA:BB:CC:DD:EE:FF',
          hasTempSensor: true
        },
        memory: {
          heapSize: 400000,
          freeHeap: 200000,
          minFreeHeap: 150000,
          maxAllocHeap: 120000,
          maxFreeBlock: 110592,
          psramSize: 8388608,
          freePsram: 7340032,
          minFreePsram: 7000000,
          maxAllocPsram: 4194304,
          flashSize: 16777216,
          flashSpeed: 80000000,
          sketchSize: 1258291,
          freeSketchSpace: 2097152
        },
        storage: {
          backend: 'SD_MMC',
          available: true,
          cardType: 'SDHC',
          cardSizeBytes: 15931539456,
          totalBytes: 15900000000,
          usedBytes: 536870912,
          freeBytes: 15363129088
        },
        network: {
          mode: 'STA',
          connected: true,
          ssid: 'HomeLab',
          bssid: '11:22:33:44:55:66',
          channel: 6,
          rssi: -52,
          ip: '192.168.1.50',
          subnetMask: '255.255.255.0',
          gateway: '192.168.1.1',
          dns: '192.168.1.1',
          mac: 'AA:BB:CC:DD:EE:FF',
          hostname: 'uptime-mon',
          wsClients: 2
        },
        time: {
          localTime: '2026-11-11 11:30:00',
          timeSynced: true,
          sntpServer: 'pool.ntp.org',
          timezoneOffset: 1,
          uptime: 7200
        },
        monitoring: {
          checkInterval: 60,
          lastCheckDurationMs: 420,
          lastCheckTime: '2026-11-11 11:29:50',
          stackHighWaterBytes: 3072
        },
        firmware: {
          version: '1.0.0',
          commit: 'abc1234',
          buildDate: 'Nov 11 2026',
          uiVersion: '0.9.9-oldhash',
          expectedUiVersion: '1.0.0-abc1234',
          otaPartition: 'app0',
          sdkVersion: 'v5.1',
          coreVersion: '3.0.0',
          configEnabled: true
        }
      };

      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        if (url === '/api/sysinfo/details') {
          cb(sampleDetails);
        }
        return { fail: vi.fn().mockReturnThis(), always: vi.fn().mockImplementation((a) => { a(); return this; }) };
      });

      window.loadDetailedSysinfo();

      expect($('#sysBoard').text()).toBe('ESP32-S3-CAM');
      expect($('#sysChip').text()).toBe('ESP32-S3 (rev 2)');
      expect($('#sysResetReason').text()).toBe('POWERON');
      expect($('#sysMaxFreeBlock').text()).toBe('108.0 KB');
      expect($('#sysStorageBackend').text()).toBe('SD_MMC');
      expect($('#sysStorageStatus').text()).toBe('Mounted');
      expect($('#sysSdCardInfo').text()).toContain('SDHC');
      expect($('#sysNetIp').text()).toBe('192.168.1.50');
      expect($('#sysNetHostname a').attr('href')).toBe('http://uptime-mon.local');
      expect($('#sysUiVersion').text()).toContain('0.9.9-oldhash');
      expect($('#sysUiVersion').text()).toContain('Mismatch');
      expect($('#sysOtaPartition').text()).toBe('app0');
    });

    it('updates maxFreeBlock and live modal fields on incoming WS sysinfo message', () => {
      window.handleWsMessage({
        type: 'sysinfo',
        data: {
          uptime: 3600,
          heapSize: 400000,
          freeHeap: 250000,
          minFreeHeap: 180000,
          maxAllocHeap: 140000,
          maxFreeBlock: 131072,
          psramSize: 8388608,
          freePsram: 7340032,
          minFreePsram: 7100000,
          maxAllocPsram: 4194304,
          rssi: -68,
          wsClients: 3,
          lastCheckDurationMs: 315,
          lastCheckTime: '2026-11-11 11:59:50',
          stackHighWaterBytes: 3584,
          localTime: '2026-11-11 12:00:00',
          timeSynced: true,
          configEnabled: false
        }
      });

      expect($('#sysModalUptime').text()).toBe('1h 0m');
      expect($('#sysMaxFreeBlock').text()).toBe('128.0 KB');
      expect($('#sysMinFreeHeap').text()).toBe('175.8 KB');
      expect($('#sysNetWsClients').text()).toBe('3');
      expect($('#sysLastCheck').text()).toBe('315 ms @ 11:59:50');
      expect($('#sysMonStack').text()).toBe('3.5 KB');
      expect($('#sysLocalTime').text()).toContain('2026-11-11 12:00:00');
      expect($('#sysConfigMode').text()).toBe('Locked');
    });

    it('copies diagnostics JSON to clipboard when Copy JSON button is clicked', () => {
      const writeTextMock = vi.fn().mockResolvedValue(undefined);
      Object.assign(navigator, { clipboard: { writeText: writeTextMock } });

      window.set_lastDetailedSysinfo({ device: { board: 'ESP32-S3' } });
      $('#btnCopySysinfo').click();

      expect(writeTextMock).toHaveBeenCalledTimes(1);
      expect(writeTextMock.mock.calls[0][0]).toContain('"board": "ESP32-S3"');
    });

    it('changes progress bar colors from green to yellow and red based on usage thresholds', () => {
      // < 70% -> bg-success
      window.handleWsMessage({
        type: 'sysinfo',
        data: { heapSize: 1000, freeHeap: 500, psramSize: 1000, freePsram: 500 }
      });
      expect($('#sysHeapBar').hasClass('bg-success')).toBe(true);
      expect($('#sysPsramBar').hasClass('bg-success')).toBe(true);
      expect($('#sysPsramBar').hasClass('bg-info')).toBe(false);

      // 75% used -> bg-warning
      window.handleWsMessage({
        type: 'sysinfo',
        data: { heapSize: 1000, freeHeap: 250, psramSize: 1000, freePsram: 250 }
      });
      expect($('#sysHeapBar').hasClass('bg-warning')).toBe(true);
      expect($('#sysPsramBar').hasClass('bg-warning')).toBe(true);

      // 90% used -> bg-danger
      window.handleWsMessage({
        type: 'sysinfo',
        data: { heapSize: 1000, freeHeap: 100, psramSize: 1000, freePsram: 100 }
      });
      expect($('#sysHeapBar').hasClass('bg-danger')).toBe(true);
      expect($('#sysPsramBar').hasClass('bg-danger')).toBe(true);
    });
  });

  describe('Unified Icon Search, Dashboard Icons & MDI Icons', () => {
    beforeEach(() => {
      window.set_dashboardIconsCatalog(null);
      window.set_mdiIconsCatalog(null);
      $('#iconSuggestions').addClass('d-none');
      $('#iconSuggestionsGrid').empty();
      $('#iconSuggestionsStatus').text('');
      globalThis.fetch = vi.fn().mockRejectedValue(new Error('No fetch mock configured'));
    });

    it('resolves mdi-*, di:*, di:*.png, and https:// icon formats correctly', () => {
      expect(window.resolveIconConfig('mdi-database')).toEqual({ type: 'mdi', className: 'mdi-database' });

      expect(window.resolveIconConfig('di:proxmox')).toEqual({
        type: 'image',
        url: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/svg/proxmox.svg',
        fallbackUrls: [
          'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/webp/proxmox.webp',
          'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/proxmox.png'
        ]
      });

      expect(window.resolveIconConfig('di:1panel.png')).toEqual({
        type: 'image',
        url: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/1panel.png',
        fallbackUrls: []
      });

      expect(window.resolveIconConfig('https://example.com/logo.png')).toEqual({
        type: 'image',
        url: 'https://example.com/logo.png',
        fallbackUrls: []
      });
    });

    it('sanitizes invalid or malicious icon strings to default mdi-server', () => {
      expect(window.resolveIconConfig('di:../etc/passwd')).toEqual({ type: 'mdi', className: 'mdi-server' });
      expect(window.resolveIconConfig('javascript:alert(1)')).toEqual({ type: 'mdi', className: 'mdi-server' });
      expect(window.resolveIconConfig('"><script>alert(1)</script>')).toEqual({ type: 'mdi', className: 'mdi-server' });
    });

    it('parses MDI icon names from CSS text', () => {
      const parsed = window.parseMdiCssText('.mdi-server::before{content:"\\F048B"}.mdi-router-wireless::before{content:"\\F0469"}.mdi-server::before{content:"dup"}');
      expect(parsed.map((i) => i.iconValue)).toEqual(['mdi-router-wireless', 'mdi-server']);
      expect(parsed[0].source).toBe('MDI');
    });

    it('renders Dashboard Icons as <img> on entity cards and falls back on image load errors', () => {
      window.set_allEntities([
        { name: 'Proxmox', address: 'https://pve.local', icon: 'di:proxmox', isOnline: true, notification: false, disabled: false, lastChanged: 1700000000 },
        { name: 'Router', address: 'http://router.local', icon: 'mdi-router-wireless', isOnline: true, notification: false, disabled: false, lastChanged: 1700000000 }
      ]);
      window.renderEntities();

      const pveCard = $('.monitoredEntity[data-entity-name="Proxmox"]');
      const img = pveCard.find('img.entity-img-icon');
      expect(img.length).toBe(1);
      expect(img.attr('src')).toBe('https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/svg/proxmox.svg');

      // First error falls back to webp
      img[0].dispatchEvent(new Event('error'));
      expect(pveCard.find('img.entity-img-icon').attr('src')).toBe('https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/webp/proxmox.webp');

      // Second error falls back to png
      pveCard.find('img.entity-img-icon')[0].dispatchEvent(new Event('error'));
      expect(pveCard.find('img.entity-img-icon').attr('src')).toBe('https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/proxmox.png');

      // Third error replaces broken img with mdi-server span
      pveCard.find('img.entity-img-icon')[0].dispatchEvent(new Event('error'));
      expect(pveCard.find('img.entity-img-icon').length).toBe(0);
      expect(pveCard.find('span.mdi.mdi-server.entityIcon').length).toBe(1);

      // Regular MDI entity keeps span rendering
      const routerCard = $('.monitoredEntity[data-entity-name="Router"]');
      expect(routerCard.find('span.mdi.mdi-router-wireless.entityIcon').length).toBe(1);
    });

    it('updates live icon preview when #icon input changes', () => {
      $('#icon').val('di:home-assistant').trigger('input');
      const previewImg = $('#iconPreview img.entity-img-icon');
      expect(previewImg.length).toBe(1);
      expect(previewImg.attr('src')).toBe('https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/svg/home-assistant.svg');

      $('#icon').val('mdi-nas').trigger('input');
      expect($('#iconPreview span.mdi.mdi-nas').length).toBe(1);
    });

    it('loads both DI and MDI catalogs on focus/input, filters unified & prefixed queries, and selects an icon', async () => {
      globalThis.fetch = vi.fn().mockImplementation(async (url) => {
        if (String(url).includes('tree.json')) {
          return {
            ok: true,
            json: async () => ({
              svg: ['proxmox.svg', 'home-assistant.svg'],
              webp: ['proxmox.webp', 'only-webp.webp'],
              png: ['proxmox.png', 'only-png.png']
            })
          };
        }
        return {
          ok: true,
          text: async () => '.mdi-home::before{content:"a"}.mdi-server::before{content:"b"}.mdi-proxmox-fake::before{content:"c"}'
        };
      });

      // Focusing #icon opens suggestions and loads both catalogs
      $('#icon').trigger('focus');
      expect($('#iconSuggestions').hasClass('d-none')).toBe(false);

      await vi.waitFor(() => {
        expect($('#iconSuggestionsGrid .icon-suggestion-item').length).toBe(7);
      });

      // Plain query "home" returns both DI (home-assistant) and MDI (mdi-home)
      $('#icon').val('home').trigger('input');
      const homeValues = $('#iconSuggestionsGrid .icon-suggestion-item')
        .map((_, el) => $(el).attr('data-icon-value'))
        .get();
      expect(homeValues).toContain('di:home-assistant');
      expect(homeValues).toContain('mdi-home');

      // Prefix "mdi-" narrows exclusively to MDI
      $('#icon').val('mdi-serv').trigger('input');
      const mdiValues = $('#iconSuggestionsGrid .icon-suggestion-item')
        .map((_, el) => $(el).attr('data-icon-value'))
        .get();
      expect(mdiValues).toEqual(['mdi-server']);

      // Prefix "di:" narrows exclusively to Dashboard Icons
      $('#icon').val('di:only').trigger('input');
      const diValues = $('#iconSuggestionsGrid .icon-suggestion-item')
        .map((_, el) => $(el).attr('data-icon-value'))
        .get();
      expect(diValues).toEqual(['di:only-png.png', 'di:only-webp.webp']);

      // Clicking a suggestion populates #icon, updates preview, and closes suggestions
      $('#iconSuggestionsGrid .icon-suggestion-item[data-icon-value="di:only-png.png"]').trigger('click');
      expect($('#icon').val()).toBe('di:only-png.png');
      expect($('#iconSuggestions').hasClass('d-none')).toBe(true);
      expect($('#iconPreview img.entity-img-icon').attr('src')).toBe('https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/only-png.png');

      // Typing a direct URL hides the suggestion panel
      $('#icon').val('https://example.com/icon.svg').trigger('input');
      expect($('#iconSuggestions').hasClass('d-none')).toBe(true);
    });

    it('shows helpful fallback message when both icon catalogs fail to load', async () => {
      globalThis.fetch = vi.fn().mockRejectedValue(new Error('Network error'));

      $('#icon').val('proxmox').trigger('focus');
      await vi.waitFor(() => {
        expect($('#iconSuggestionsStatus').text()).toContain('Icon catalogs unavailable');
      });
    });
  });

  describe('Header Dropdown Menu', () => {
    it('toggles dropdown open/closed via menu button, outside click, item click, and Escape', () => {
      const $menu = $('#headerMenuDropdown');
      const $btn = $('#headerMenuButton');

      expect($menu.hasClass('show')).toBe(false);
      expect($btn.attr('aria-expanded')).toBe('false');

      // Open on menu button click
      $btn.trigger('click');
      expect($menu.hasClass('show')).toBe(true);
      expect($btn.attr('aria-expanded')).toBe('true');

      // Close on outside click
      $(document).trigger('click');
      expect($menu.hasClass('show')).toBe(false);
      expect($btn.attr('aria-expanded')).toBe('false');

      // Open again and close on Escape
      $btn.trigger('click');
      expect($menu.hasClass('show')).toBe(true);
      $(document).trigger($.Event('keydown', { key: 'Escape' }));
      expect($menu.hasClass('show')).toBe(false);

      // Open again and close on menu item click
      $btn.trigger('click');
      expect($menu.hasClass('show')).toBe(true);
      $('#menuSysinfoButton').trigger('click');
      expect($menu.hasClass('show')).toBe(false);
    });

    it('contains required menu links and syncs config badge and mobile version', () => {
      expect($('#menuApiDocsLink').attr('href')).toBe('/api/docs');
      expect($('#menuLicenseLink').attr('href')).toBe('/license');
      expect($('#menuGithubRepoLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor');
      expect($('#menuGithubPagesLink').attr('href')).toBe('https://esp32-service-uptime-monitor.com/');
      expect($('#menuSysinfoButton').attr('data-bs-target')).toBe('#sysinfoModal');
      expect($('#btnToggleTerminal').length).toBe(1);

      window.updateSysinfoUI({ version: '1.2.3', commit: 'abc1234', configEnabled: true });
      expect($('#configButton').css('display')).not.toBe('none');
      expect($('#headerConfigBadge').hasClass('d-none')).toBe(false);
      expect($('#menuVersionVal').text()).toBe('1.2.3');
      expect($('#menuCommitVal').text()).toBe('#abc1234');
      expect($('#versionVal').text()).toBe('1.2.3');
      expect($('#commitVal').text()).toBe('#abc1234');

      // Check footer links
      expect($('#versionLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/releases/tag/v1.2.3');
      expect($('#commitLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/commit/abc1234');
      expect($('#versionLink').attr('target')).toBe('_blank');
      expect($('#commitLink').attr('target')).toBe('_blank');

      // Check mobile menu links
      expect($('#menuVersionLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/releases/tag/v1.2.3');
      expect($('#menuCommitLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/commit/abc1234');
      expect($('#menuVersionLink').attr('target')).toBe('_blank');
      expect($('#menuCommitLink').attr('target')).toBe('_blank');

      // Check system metrics modal links
      expect($('#sysFwVersionLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/releases/tag/v1.2.3');
      expect($('#sysCommitLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/commit/abc1234');
      expect($('#sysFwVersionLink').attr('target')).toBe('_blank');
      expect($('#sysCommitLink').attr('target')).toBe('_blank');
      expect($('#sysFwVersionLink').hasClass('text-light')).toBe(false);
      expect($('#sysFwVersionLink').hasClass('text-reset')).toBe(true);

      window.updateSysinfoUI({ configEnabled: false });
      expect($('#configButton').css('display')).toBe('none');
      expect($('#headerConfigBadge').hasClass('d-none')).toBe(true);
      expect($('#configCountdownBadge').hasClass('d-none')).toBe(true);
    });

    it('manages config mode countdown badge and transitions to danger style when <= 60s', () => {
      vi.useFakeTimers();

      window.updateSysinfoUI({ configEnabled: true, configTimeoutRemaining: 120 });
      expect($('#configButton').css('display')).not.toBe('none');
      expect($('#headerConfigBadge').hasClass('d-none')).toBe(false);
      expect($('#configCountdownBadge').hasClass('d-none')).toBe(false);
      expect($('#configCountdownTime').text()).toBe('02:00');
      expect($('#configMenuCountdown').text()).toBe('02:00');
      expect($('#configCountdownBadge').hasClass('bg-warning')).toBe(true);

      vi.advanceTimersByTime(61000); // 120 - 61 = 59s
      expect($('#configCountdownTime').text()).toBe('00:59');
      expect($('#configCountdownBadge').hasClass('bg-danger')).toBe(true);

      vi.advanceTimersByTime(59000); // 0s
      expect($('#configCountdownBadge').hasClass('d-none')).toBe(true);
      expect($('#configButton').css('display')).toBe('none');

      vi.useRealTimers();
    });

    it('generates GitHub release and commit URLs correctly', () => {
      const repoUrl = window.GITHUB_REPO_URL;
      expect(repoUrl).toBe('https://github.com/gyengus/esp32-service-uptime-monitor');

      expect(window.getGithubReleaseUrl('1.2.3')).toBe(`${repoUrl}/releases/tag/v1.2.3`);
      expect(window.getGithubReleaseUrl('v1.2.3')).toBe(`${repoUrl}/releases/tag/v1.2.3`);
      expect(window.getGithubReleaseUrl('0.0.1-SNAPSHOT')).toBe(`${repoUrl}/releases/tag/v0.0.1-SNAPSHOT`);
      expect(window.getGithubReleaseUrl('--')).toBe(`${repoUrl}/releases`);
      expect(window.getGithubReleaseUrl('')).toBe(`${repoUrl}/releases`);
      expect(window.getGithubReleaseUrl(null)).toBe(`${repoUrl}/releases`);

      expect(window.getGithubCommitUrl('abc1234')).toBe(`${repoUrl}/commit/abc1234`);
      expect(window.getGithubCommitUrl('#abc1234')).toBe(`${repoUrl}/commit/abc1234`);
      expect(window.getGithubCommitUrl('--')).toBe(repoUrl);
      expect(window.getGithubCommitUrl('')).toBe(repoUrl);
      expect(window.getGithubCommitUrl(null)).toBe(repoUrl);
    });

    it('renders detailed sysinfo modal with release and commit links', () => {
      window.renderDetailedSysinfoModal({
        firmware: {
          version: '2.0.1',
          commit: 'fedcba9',
          uiVersion: '2.0.1-fedcba9',
          expectedUiVersion: '2.0.1-fedcba9',
          buildDate: '2026-11-13'
        }
      });

      expect($('#sysFwVersion').text()).toBe('2.0.1');
      expect($('#sysFwVersionLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/releases/tag/v2.0.1');
      expect($('#sysCommit').text()).toBe('#fedcba9');
      expect($('#sysCommitLink').attr('href')).toBe('https://github.com/gyengus/esp32-service-uptime-monitor/commit/fedcba9');
    });

    it('sets aria-label alongside title on wsStatusDot', () => {
      expect($('#menuLogsButton').length).toBe(0);
      window.setWsStatus(true);
      expect($('#wsStatusDot').attr('aria-label')).toBe('Live (WebSocket connected)');
      window.setWsStatus(false);
      expect($('#wsStatusDot').attr('aria-label')).toBe('Disconnected (Reconnecting...)');
    });
  });

  describe('UI Event Handlers and Interactive Controls', () => {
    it('handles filter buttons click and updates active entity view', () => {
      $('#filterOffline').trigger('click');
      expect(window.get_currentFilter()).toBe('OFFLINE');

      $('#filterOnline').trigger('click');
      expect(window.get_currentFilter()).toBe('ONLINE');

      $('#filterAll').trigger('click');
      expect(window.get_currentFilter()).toBe('ALL');
    });

    it('handles empty state buttons to add entity or reset filter', () => {
      const origModal = $.fn.modal;
      $.fn.modal = vi.fn();

      $('<button id="emptyShowAllButton"></button>').appendTo('body').trigger('click').remove();
      expect(window.get_currentFilter()).toBe('ALL');

      $('<button id="emptyAddButton"></button>').appendTo('body').trigger('click').remove();
      expect($.fn.modal).toHaveBeenCalledWith('show');

      $.fn.modal = origModal;
    });

    it('toggles and manages terminal drawer through UI buttons and controls', () => {
      // Toggle button
      $('#btnToggleTerminal').trigger('click');
      expect(window.get_terminalOpen()).toBe(true);

      // Close button
      $('#btnTerminalClose').trigger('click');
      expect(window.get_terminalOpen()).toBe(false);

      // Clear button
      window.appendLogLine('Test log line 1');
      window.appendLogLine('Test log line 2');
      expect(window.get_terminalLogsBuffer().length).toBe(2);

      $('#btnTerminalClear').trigger('click');
      expect(window.get_terminalLogsBuffer().length).toBe(0);
      expect($('#terminalLineCount').text()).toBe('0 lines');

      // Autoscroll checkbox
      $('#terminalAutoScroll').prop('checked', false).trigger('change');
      $('#terminalAutoScroll').prop('checked', true).trigger('change');

      // Log level change
      const mockWs = { send: vi.fn(), readyState: 1 };
      window.set_ws(mockWs);

      $('#terminalLogLevel').prop('disabled', false).val('DEBUG').trigger('change');
      expect(mockWs.send).toHaveBeenCalledWith(JSON.stringify({ type: 'set_log_level', level: 'DEBUG' }));

      // When disabled, change is ignored
      mockWs.send.mockClear();
      $('#terminalLogLevel').prop('disabled', true).val('INFO').trigger('change');
      expect(mockWs.send).not.toHaveBeenCalled();
    });

    it('closes header menu on Escape keydown', () => {
      window.toggleHeaderMenu(true);
      expect($('#headerMenuDropdown').hasClass('show')).toBe(true);

      $(document).trigger($.Event('keydown', { key: 'Escape' }));
      expect($('#headerMenuDropdown').hasClass('show')).toBe(false);
    });

    it('manages sysinfo modal lifecycle and auto-refresh timer', () => {
      vi.useFakeTimers();
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        if (cb) cb({ system: {}, firmware: {} });
        return { fail: vi.fn().mockReturnThis(), always: vi.fn().mockReturnThis() };
      });

      $('#sysinfoModal').trigger('show.bs.modal');
      expect($.getJSON).toHaveBeenCalledWith('/api/sysinfo/details', expect.any(Function));

      // Auto-refresh interval (30000ms)
      vi.advanceTimersByTime(30000);
      expect($.getJSON).toHaveBeenCalledTimes(2);

      // Refresh button click
      $('#btnRefreshSysinfo').trigger('click');
      expect($.getJSON).toHaveBeenCalledTimes(3);

      // Hide modal clears timer
      $('#sysinfoModal').trigger('hidden.bs.modal');
      vi.advanceTimersByTime(30000);
      expect($.getJSON).toHaveBeenCalledTimes(3);

      vi.useRealTimers();
    });

    it('copies diagnostics to clipboard with success or error fallback', async () => {
      vi.useRealTimers();
      window.set_lastDetailedSysinfo({ system: { freeHeap: 50000 } });

      // 1. Success path
      const writeTextMock = vi.fn().mockResolvedValue();
      const origClipboard = navigator.clipboard;
      Object.defineProperty(navigator, 'clipboard', {
        value: { writeText: writeTextMock },
        configurable: true,
        writable: true
      });

      $('#btnCopySysinfo').trigger('click');
      await new Promise(r => setTimeout(r, 30));
      expect(writeTextMock).toHaveBeenCalledWith(expect.stringContaining('50000'));
      expect($('#successToast .toast-body').text()).toContain('copied to clipboard');

      // 2. Clipboard writeText rejection
      writeTextMock.mockRejectedValueOnce(new Error('Permission denied'));
      $('#btnCopySysinfo').trigger('click');
      await new Promise(r => setTimeout(r, 30));
      expect($('#errorToast .toast-body').text()).toBe('Failed to copy diagnostics');

      // 3. Clipboard API unavailable
      navigator.clipboard = undefined;
      $('#btnCopySysinfo').trigger('click');
      expect($('#errorToast .toast-body').text()).toBe('Clipboard API unavailable');

      Object.defineProperty(navigator, 'clipboard', {
        value: origClipboard,
        configurable: true,
        writable: true
      });
      vi.useFakeTimers();
    });

    it('formats bytes across various units and boundary cases', () => {
      expect(window.formatBytes(0)).toBe('0 B');
      expect(window.formatBytes(-50)).toBe('--');
      expect(window.formatBytes(null)).toBe('--');
      expect(window.formatBytes(500)).toBe('500 B');
      expect(window.formatBytes(1024)).toBe('1.0 KB');
      expect(window.formatBytes(1048576)).toBe('1.00 MB');
      expect(window.formatBytes(1073741824)).toBe('1.00 GB');
    });

    it('formats uptime seconds correctly for short and zero intervals', () => {
      expect(window.formatUptime(0)).toBe('0m');
      expect(window.formatUptime(45)).toBe('0m');
      expect(window.formatUptime(-1)).toBe('--');
      expect(window.formatUptime(NaN)).toBe('--');
      expect(window.formatUptime(86400)).toBe('1d 0h 0m');
      expect(window.formatUptime(3600)).toBe('1h 0m');
      expect(window.formatUptime(120)).toBe('2m');
      expect(window.formatUptime(86400 + 7200 + 180)).toBe('1d 2h 3m');
    });

    it('determines progress bar class based on thresholds', () => {
      expect(window.getUsageBarClass(90)).toBe('bg-danger');
      expect(window.getUsageBarClass(85)).toBe('bg-danger');
      expect(window.getUsageBarClass(75)).toBe('bg-warning');
      expect(window.getUsageBarClass(70)).toBe('bg-warning');
      expect(window.getUsageBarClass(50)).toBe('bg-success');
    });

    it('sets progress bar accurately including zero or invalid totals', () => {
      const $bar = $('<div></div>');
      window.setProgressBar($bar, 0, 0);
      expect($bar.css('width')).toBe('0%');
      expect($bar.hasClass('bg-secondary')).toBe(true);

      window.setProgressBar($bar, 50, 100);
      expect($bar.css('width')).toBe('50%');
      expect($bar.hasClass('bg-success')).toBe(true);

      window.setProgressBar($bar, 90, 100);
      expect($bar.css('width')).toBe('90%');
      expect($bar.hasClass('bg-danger')).toBe(true);
    });

    it('formats RSSI HTML with signal icons and labels', () => {
      expect(window.formatRssiHtml(null)).toBe('--');
      expect(window.formatRssiHtml(NaN)).toBe('--');
      expect(window.formatRssiHtml(-55)).toContain('-55 dBm');
      expect(window.formatRssiHtml(-75)).toContain('-75 dBm');
      expect(window.formatRssiHtml(-90)).toContain('-90 dBm');
    });
  });
});
