import { describe, it, expect, beforeAll, beforeEach, afterEach, vi } from 'vitest';
import fs from 'fs';
import path from 'path';

describe('Uptime Monitor Web UI - app.config.js', () => {
  let $;
  let mockGetConfigResponse;

  function setupMocks() {
    mockGetConfigResponse = {
      wifiSSID: 'HomeNetwork',
      wifiPassword: 'secretpassword',
      sntpServer: 'pool.ntp.org',
      offsetHours: 2,
      checkInterval: 45,
      discordWebhookURL: 'https://discord.com/api/webhooks/123/abc',
      hostname: 'esp32-monitor',
      ledBrightness: 40
    };

    $.ajax = vi.fn().mockImplementation((options) => {
      if (options.url === '/api/config' && (options.type === 'GET' || !options.type)) {
        if (mockGetConfigResponse === null) {
          if (options.error) {
            options.error({ responseText: JSON.stringify({ message: 'Failed to fetch config' }) }, 500, 'Error');
          }
        } else {
          if (options.success) {
            options.success(mockGetConfigResponse);
          }
        }
      } else if (options.url === '/api/config' && options.type === 'POST') {
        if (options.success) {
          options.success({ restarting: true });
        }
      } else if (options.url === '/api/config/factoryReset') {
        if (options.success) {
          options.success({ message: 'Factory reset initiated' });
        }
      } else if (options.url === '/api/config/backup') {
        if (options.success) {
          options.success({ wifiSSID: 'HomeNetwork' });
        }
      } else if (options.url === '/api/config/restore') {
        if (options.success) {
          options.success({ message: 'Restored successfully' });
        }
      } else if (options.url === '/api/config/test-led') {
        if (options.success) {
          options.success({ status: 'success', message: 'LED test started', brightness: 50, duration: 3000 });
        }
      } else if (options.url === '/api/web/upload-archive') {
        if (options.success) {
          options.success({ status: 'success', message: 'Web UI updated successfully' });
        }
      } else if (options.url === '/ota/start?mode=fr') {
        if (options.success) {
          options.success();
        }
      }
      return {
        fail: vi.fn().mockReturnThis(),
        always: vi.fn().mockImplementation((cb) => { cb(); return this; })
      };
    });

    $.post = vi.fn().mockImplementation((url, data) => {
      const deferred = {
        done: vi.fn().mockImplementation((cb) => {
          if (data && data.url && data.url.includes('success-test')) {
            cb({ message: 'ok' });
          }
          return deferred;
        }),
        fail: vi.fn().mockImplementation((cb) => {
          if (data && data.url && !data.url.includes('success-test')) {
            cb({ responseText: 'Invalid webhook URL' });
          }
          return deferred;
        }),
        always: vi.fn().mockImplementation((cb) => {
          cb();
          return deferred;
        })
      };
      return deferred;
    });

    $.getJSON = vi.fn().mockImplementation((url, cb) => {
      return {
        fail: vi.fn().mockReturnThis()
      };
    });
  }

  beforeAll(async () => {
    const htmlPath = path.resolve(__dirname, '../config.html');
    const html = fs.readFileSync(htmlPath, 'utf8');
    document.documentElement.innerHTML = html;
    $ = window.$;

    // Mock Blob and URL methods for backup test
    globalThis.Blob = class {
      constructor(parts, options) {
        this.parts = parts;
        this.options = options;
      }
    };
    globalThis.URL.createObjectURL = vi.fn().mockReturnValue('blob:mock-url');
    globalThis.URL.revokeObjectURL = vi.fn();

    setupMocks();
    await import('../static/app.config.js');
    document.dispatchEvent(new window.Event('DOMContentLoaded'));
    await new Promise(r => setTimeout(r, 50));
  });

  beforeEach(() => {
    vi.useFakeTimers();
    setupMocks();
  });

  afterEach(() => {
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  describe('Configuration Loading', () => {
    it('populates form fields with loaded configuration', () => {
      expect($('#wifiSSID').val()).toBe('HomeNetwork');
      expect($('#wifiPassword').val()).toBe('secretpassword');
      expect($('#sntpServer').val()).toBe('pool.ntp.org');
      expect($('#offsetHours').val()).toBe('2');
      expect($('#checkInterval').val()).toBe('45');
      expect($('#discordWebhookURL').val()).toBe('https://discord.com/api/webhooks/123/abc');
      expect($('#hostname').val()).toBe('esp32-monitor');
      expect($('#ledBrightness').val()).toBe('40');
      expect($('#ledBrightnessVal').text()).toBe('40%');

      // wifiSSID is not empty, rows should be visible
      expect($('#factoryResetRow').css('display')).not.toBe('none');
      expect($('#backupRow').css('display')).not.toBe('none');
    });

    it('shows error toast when configuration fetch fails', () => {
      mockGetConfigResponse = null;
      // Re-trigger GET /api/config error
      $.ajax({
        type: 'GET',
        url: '/api/config',
        dataType: 'json',
        error: (xhr) => window.showError(xhr, "Can't fetch configuration")
      });

      expect($('#errorToast .toast-body').text()).toBe('Failed to fetch config');
      expect($('#errorToast').hasClass('show')).toBe(true);
    });
  });

  describe('Save Configuration', () => {
    it('validates form on save and prevents submit when required fields are empty', () => {
      $('#wifiSSID').val('');
      $('#saveButton').trigger('click');

      expect($('#configForm').hasClass('was-validated')).toBe(true);
      // $.ajax POST should not have been called with save data
      const postCalls = $.ajax.mock.calls.filter(c => c[0].url === '/api/config' && c[0].type === 'POST');
      expect(postCalls.length).toBe(0);
      expect($('#saveButton').prop('disabled')).toBe(false);
    });

    it('submits JSON payload and displays restart notification on success', () => {
      $('#wifiSSID').val('OfficeWiFi');
      $('#wifiPassword').val('securePass');
      $('#sntpServer').val('time.google.com');
      $('#offsetHours').val('1');
      $('#checkInterval').val('60');
      $('#discordWebhookURL').val('https://discord.com/api/webhooks/999/xyz');
      $('#hostname').val('service-monitor');
      $('#ledBrightness').val('75');

      $('#saveButton').trigger('click');

      const postCall = $.ajax.mock.calls.find(c => c[0].url === '/api/config' && c[0].type === 'POST');
      expect(postCall).toBeDefined();

      const payload = JSON.parse(postCall[0].data);
      expect(payload).toEqual({
        wifiSSID: 'OfficeWiFi',
        wifiPassword: 'securePass',
        sntpServer: 'time.google.com',
        offsetHours: 1,
        checkInterval: 60,
        discordWebhookURL: 'https://discord.com/api/webhooks/999/xyz',
        hostname: 'service-monitor',
        ledBrightness: 75
      });

      expect($('#successToast .toast-body').text()).toContain('The device will restart.');
      expect($('#saveButton').prop('disabled')).toBe(false);
    });

    it('handles save error and re-enables save button', () => {
      $.ajax.mockImplementationOnce((options) => {
        if (options.error) {
          options.error({ responseText: JSON.stringify({ message: 'Write error' }) }, 500, 'Error');
        }
        return { fail: vi.fn().mockReturnThis(), always: vi.fn() };
      });

      $('#wifiSSID').val('ValidSSID');
      $('#wifiPassword').val('ValidPass');
      $('#saveButton').trigger('click');

      expect($('#errorToast .toast-body').text()).toBe('Write error');
      expect($('#saveButton').prop('disabled')).toBe(false);
    });
  });

  describe('Wi-Fi Scanning', () => {
    it('manages UI state during scan and polls until status is done', () => {
      let pollCount = 0;
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        pollCount++;
        if (pollCount === 1) {
          cb({ status: 'scanning' });
        } else {
          cb({
            status: 'done',
            networks: [
              { ssid: 'NetworkA', rssi: -60 },
              { ssid: 'NetworkA', rssi: -50 }, // Duplicate with stronger signal
              { ssid: 'NetworkB', rssi: -75 }
            ]
          });
        }
        return { fail: vi.fn().mockReturnThis() };
      });

      $('#scanWifiButton').trigger('click');

      expect($('#btnSpinner').hasClass('d-none')).toBe(false);
      expect($('#btnText').text()).toBe('Scanning');
      expect($('#scanWifiButton').prop('disabled')).toBe(true);

      // Advance timer for poll
      vi.advanceTimersByTime(1000);

      const networkDivs = $('#wifiNetworksContainer .wifi-network');
      expect(networkDivs.length).toBe(2); // NetworkA deduplicated, NetworkB
      expect(networkDivs.eq(0).text()).toContain('NetworkA');
      expect(networkDivs.eq(0).text()).toContain('-50 dBm'); // Picked stronger RSSI
      expect(networkDivs.eq(1).text()).toContain('NetworkB');

      // Finalize scan state
      expect($('#btnSpinner').hasClass('d-none')).toBe(true);
      expect($('#btnText').text()).toBe('Scan');
      expect($('#scanWifiButton').prop('disabled')).toBe(false);
    });

    it('handles empty scan results', () => {
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        cb({ status: 'done', networks: [] });
        return { fail: vi.fn().mockReturnThis() };
      });

      $('#scanWifiButton').trigger('click');

      expect($('#wifiNetworksContainer').text()).toContain('No networks found');
    });

    it('selects SSID when network element is clicked', () => {
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        cb({
          status: 'done',
          networks: [{ ssid: 'SelectedWiFi', rssi: -45 }]
        });
        return { fail: vi.fn().mockReturnThis() };
      });

      $('#scanWifiButton').trigger('click');

      const netDiv = $('#wifiNetworksContainer .wifi-network').first();
      netDiv.trigger('click');

      expect($('#wifiSSID').val()).toBe('SelectedWiFi');
    });

    it('finalizes scan after exceeding 20 attempts', () => {
      $.getJSON = vi.fn().mockImplementation((url, cb) => {
        cb({ status: 'scanning' });
        return { fail: vi.fn().mockReturnThis() };
      });

      $('#scanWifiButton').trigger('click');

      // Advance through 21 polling intervals
      for (let i = 0; i < 22; i++) {
        vi.advanceTimersByTime(1000);
      }

      expect($('#scanWifiButton').prop('disabled')).toBe(false);
      expect($('#btnText').text()).toBe('Scan');
    });
  });

  describe('Discord Webhook Test', () => {
    it('shows error feedback when webhook URL is blank', () => {
      $('#discordWebhookURL').val('');
      $('#testDiscordButton').trigger('click');

      const feedback = $('#testDiscordFeedback');
      expect(feedback.text()).toContain('Please enter a webhook URL first');
      expect(feedback.hasClass('text-danger')).toBe(true);
    });

    it('sends test request and displays success feedback', () => {
      $('#discordWebhookURL').val('https://discord.com/api/webhooks/success-test');
      $('#testDiscordButton').trigger('click');

      const postCall = $.post.mock.calls.find(c => c[0] === '/api/testDiscord');
      expect(postCall).toBeDefined();
      expect(postCall[1]).toEqual({ url: 'https://discord.com/api/webhooks/success-test' });

      const feedback = $('#testDiscordFeedback');
      expect(feedback.text()).toContain('Test message sent!');
      expect(feedback.hasClass('text-success')).toBe(true);
      expect($('#testDiscordButton').prop('disabled')).toBe(false);
    });

    it('displays error feedback when test request fails', () => {
      $('#discordWebhookURL').val('https://discord.com/api/webhooks/invalid');
      $('#testDiscordButton').trigger('click');

      const feedback = $('#testDiscordFeedback');
      expect(feedback.text()).toBe('Invalid webhook URL');
      expect(feedback.hasClass('text-danger')).toBe(true);
    });
  });

  describe('Factory Reset', () => {
    it('opens confirmation modal on factory reset button click', () => {
      $('#factoryResetButton').trigger('click');
      expect($.fn.modal).toHaveBeenCalledWith('show');
    });

    it('sends POST /api/config/factoryReset on confirmation', () => {
      $('#confirmFactoryResetButton').trigger('click');

      const postCall = $.ajax.mock.calls.find(c => c[0].url === '/api/config/factoryReset');
      expect(postCall).toBeDefined();
      expect($('#successToast .toast-body').text()).toBe('Factory reset initiated');
    });
  });

  describe('Backup & Restore', () => {
    it('downloads backup file on backup button click', () => {
      const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});

      $('#backupButton').trigger('click');

      expect($.ajax).toHaveBeenCalledWith(expect.objectContaining({
        type: 'GET',
        url: '/api/config/backup'
      }));
      expect(globalThis.URL.createObjectURL).toHaveBeenCalled();
      expect(clickSpy).toHaveBeenCalled();
      expect($('#successToast .toast-body').text()).toContain('Backup downloaded successfully');

      clickSpy.mockRestore();
    });

    it('triggers file selection when restore button is clicked', () => {
      const clickSpy = vi.spyOn($('#restoreFile')[0], 'click');
      $('#restoreButton').trigger('click');
      expect(clickSpy).toHaveBeenCalled();
    });

    it('uploads backup file and shows restart notification on restore', () => {
      const mockFile = new File(['{"config":{}}'], 'backup.json', { type: 'application/json' });
      const event = { target: { files: [mockFile] } };

      $('#restoreFile').trigger($.Event('change', event));

      const restoreCall = $.ajax.mock.calls.find(c => c[0].url === '/api/config/restore');
      expect(restoreCall).toBeDefined();
      expect(restoreCall[0].type).toBe('POST');
      expect(restoreCall[0].processData).toBe(false);
      expect(restoreCall[0].contentType).toBe(false);
      expect($('#successToast .toast-body').text()).toContain('The device will restart.');
    });

    it('triggers file selection when update web button is clicked', () => {
      const clickSpy = vi.spyOn($('#webArchiveFile')[0], 'click');
      $('#updateWebButton').trigger('click');
      expect(clickSpy).toHaveBeenCalled();
    });

    it('uploads web archive file and displays success notification', () => {
      const mockZip = new File(['PK...'], 'web.zip', { type: 'application/zip' });
      const event = { target: { files: [mockZip] } };

      $('#webArchiveFile').trigger($.Event('change', event));

      const updateCall = $.ajax.mock.calls.find(c => c[0].url === '/api/web/upload-archive');
      expect(updateCall).toBeDefined();
      expect(updateCall[0].type).toBe('POST');
      expect(updateCall[0].processData).toBe(false);
      expect(updateCall[0].contentType).toBe(false);
      expect($('#successToast .toast-body').text()).toContain('Web UI updated successfully');
    });

    it('triggers file selection when update firmware button is clicked', () => {
      const clickSpy = vi.spyOn($('#firmwareBinaryFile')[0], 'click');
      $('#updateFirmwareButton').trigger('click');
      expect(clickSpy).toHaveBeenCalled();
    });

    it('starts OTA and uploads firmware binary file with progress', () => {
      const mockXHR = {
        open: vi.fn(),
        send: vi.fn().mockImplementation(function() {
          this.status = 200;
          if (this.onload) this.onload();
        }),
        upload: {
          onprogress: null
        }
      };
      const origXHR = globalThis.XMLHttpRequest;
      globalThis.XMLHttpRequest = vi.fn().mockImplementation(function() {
        return mockXHR;
      });

      const mockBin = new File(['firmware binary bytes'], 'firmware.bin', { type: 'application/octet-stream' });
      const event = { target: { files: [mockBin] } };

      $('#firmwareBinaryFile').trigger($.Event('change', event));

      const otaStartCall = $.ajax.mock.calls.find(c => c[0].url === '/ota/start?mode=fr');
      expect(otaStartCall).toBeDefined();
      expect(mockXHR.open).toHaveBeenCalledWith('POST', '/ota/upload', true);
      expect(mockXHR.send).toHaveBeenCalled();
      expect($('#successToast .toast-body').text()).toContain('Firmware update successful');

      globalThis.XMLHttpRequest = origXHR;
    });
  });

  describe('LED Brightness and Testing', () => {
    it('updates brightness label when slider value changes', () => {
      $('#ledBrightness').val('65');
      $('#ledBrightness').trigger('input');
      expect($('#ledBrightnessVal').text()).toBe('65%');
    });

    it('triggers test LED API and shows feedback on click', () => {
      vi.useFakeTimers();
      $('#ledBrightness').val('80');
      $('#testLedButton').trigger('click');

      const testCall = $.ajax.mock.calls.find(c => c[0].url === '/api/config/test-led');
      expect(testCall).toBeDefined();
      expect(JSON.parse(testCall[0].data)).toEqual({ brightness: 80, duration: 3000 });

      expect($('#testLedFeedback').text()).toContain('LED test active (cyan, 80%)');
      expect($('#testLedFeedback').hasClass('text-success')).toBe(true);
      expect($('#testLedButton').prop('disabled')).toBe(true);

      vi.advanceTimersByTime(3100);
      expect($('#testLedButton').prop('disabled')).toBe(false);
      vi.useRealTimers();
    });

    it('handles test LED API failure gracefully', () => {
      $.ajax.mockImplementationOnce((options) => {
        if (options.url === '/api/config/test-led') {
          if (options.error) {
            options.error({ responseText: JSON.stringify({ message: 'Forbidden' }) }, 403, 'Forbidden');
          }
        }
        return { fail: vi.fn().mockReturnThis(), always: vi.fn() };
      });

      $('#testLedButton').trigger('click');
      expect($('#testLedButton').prop('disabled')).toBe(false);
      expect($('#testLedFeedback').text()).toContain('Failed to start LED test');
      expect($('#testLedFeedback').hasClass('text-danger')).toBe(true);
    });
  });
});
