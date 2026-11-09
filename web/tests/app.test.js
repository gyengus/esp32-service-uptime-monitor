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

    it('displays "Nothing to show." when filter produces no results', () => {
      window.loadEntities();
      window.allEntities = [];
      window.renderEntities();
      expect($('#entityGrid').text()).toContain('Nothing to show.');
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
});
