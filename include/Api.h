#ifndef _API_H_
#define _API_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "HostnameUtils.h"

/**
 * @openapi
 * openapi: 3.0.3
 * info:
 *   title: ESP32 Service Uptime Monitor API
 *   version: auto
 *   description: |
 *     REST API exposed by the ESP32 Service Uptime Monitor firmware.
 *
 *     ### Device Address
 *     Set your device IP address or mDNS hostname (for example `192.168.0.118` or `esp32-service-uptime-monitor.local`)
 *     in the **Servers -> `host`** field below before executing requests.
 *     If calling a local HTTP device from HTTPS GitHub Pages is blocked by your browser, allow *Local Network Access / Insecure content* in site settings or copy the generated `curl` command.
 *
 *     ### Configuration Protection (BOOT Button)
 *     Configuration and management endpoints return `403 Forbidden` when configuration mode is locked.
 *     Press the hardware **BOOT** button on the device (or switch into AP setup mode) to unlock them.
 *
 *     ### Real-Time WebSocket & OTA
 *     - Live status updates and log streams are available over WebSocket at `ws://{host}/ws`.
 *     - Firmware OTA updates are served natively at `/ota/start` and `/ota/upload`.
 * servers:
 *   - url: http://{host}
 *     description: ESP32 device on local network (HTTP)
 *     variables:
 *       host:
 *         default: esp32-service-uptime-monitor.local
 *         description: Device IP address or mDNS hostname (e.g. 192.168.0.118 or esp32-service-uptime-monitor.local)
 * tags:
 *   - name: System
 *     description: Health checks and hardware/network diagnostics
 *   - name: Monitoring
 *     description: Monitored services management and uptime summary
 *   - name: Configuration
 *     description: Device settings, backup/restore, Wi-Fi scan, and hardware tests (requires configuration mode)
 *   - name: Updates
 *     description: Firmware OTA and Web UI archive updates
 *   - name: File Manager
 *     description: Storage filesystem browsing and file operations
 * components:
 *   schemas:
 *     StatusResponse:
 *       type: object
 *       properties:
 *         status:
 *           type: string
 *           example: success
 *         message:
 *           type: string
 *     ErrorResponse:
 *       type: object
 *       properties:
 *         error:
 *           type: string
 *           example: Not found
 *     HealthResponse:
 *       type: object
 *       properties:
 *         status:
 *           type: string
 *           example: UP
 *     SummaryResponse:
 *       type: object
 *       properties:
 *         total:
 *           type: integer
 *           example: 5
 *         online:
 *           type: integer
 *           example: 4
 *         offline:
 *           type: integer
 *           example: 1
 *     LedTestResponse:
 *       type: object
 *       properties:
 *         status:
 *           type: string
 *           example: success
 *         message:
 *           type: string
 *           example: LED test started
 *         brightness:
 *           type: integer
 *           minimum: 0
 *           maximum: 100
 *           example: 50
 *         duration:
 *           type: integer
 *           example: 3000
 *     Config:
 *       type: object
 *       properties:
 *         wifiSSID:
 *           type: string
 *           example: MyWiFi
 *         wifiPassword:
 *           type: string
 *           description: Wi-Fi password (masked as ****** on read)
 *           example: secret123
 *         sntpServer:
 *           type: string
 *           maxLength: 64
 *           example: pool.ntp.org
 *         offsetHours:
 *           type: integer
 *           minimum: -12
 *           maximum: 14
 *           example: 1
 *         checkInterval:
 *           type: integer
 *           minimum: 5
 *           maximum: 255
 *           description: Check interval in seconds
 *           example: 60
 *         discordWebhookURL:
 *           type: string
 *           description: Discord webhook URL (token suffix masked as ****** on read)
 *           example: https://discord.com/api/webhooks/...
 *         hostname:
 *           type: string
 *           description: RFC 1123 mDNS hostname without .local suffix
 *           example: esp32-service-uptime-monitor
 *         otaToken:
 *           type: string
 *           description: PlatformIO OTA update authentication token (minimum 8 characters; masked on read)
 *           example: "******"
 *         logLevel:
 *           type: integer
 *           minimum: 0
 *           maximum: 11
 *           example: 8
 *         logLevelName:
 *           type: string
 *           example: DEBUG
 *         ledBrightness:
 *           type: integer
 *           minimum: 0
 *           maximum: 100
 *           example: 50
 *         configEnabled:
 *           type: boolean
 *           description: Whether configuration mode is currently active
 *           example: true
 *         configTimeoutRemaining:
 *           type: integer
 *           description: Remaining configuration mode timeout in seconds
 *           example: 600
 *     MonitoredEntity:
 *       type: object
 *       properties:
 *         name:
 *           type: string
 *           maxLength: 63
 *           example: Router
 *         address:
 *           type: string
 *           maxLength: 127
 *           example: http://192.168.0.1/
 *         icon:
 *           type: string
 *           maxLength: 127
 *           description: Material Design Icons class (mdi-*), Dashboard Icons slug (di:*), or icon URL
 *           example: mdi-router-wireless
 *         isOnline:
 *           type: boolean
 *           example: true
 *         notification:
 *           type: boolean
 *           example: true
 *         disabled:
 *           type: boolean
 *           example: false
 *         lastChanged:
 *           type: integer
 *           format: int64
 *           description: Unix timestamp of the last status transition
 *           example: 1700000000
 *     MonitoredEntityInput:
 *       type: object
 *       required:
 *         - name
 *         - address
 *       properties:
 *         name:
 *           type: string
 *           maxLength: 63
 *           example: Router
 *         address:
 *           type: string
 *           maxLength: 127
 *           example: http://192.168.0.1/
 *         icon:
 *           type: string
 *           maxLength: 127
 *           description: Material Design Icons class (mdi-*), Dashboard Icons slug (di:*), or icon URL
 *           example: mdi-router-wireless
 *         notification:
 *           type: boolean
 *           default: true
 *         disabled:
 *           type: boolean
 *           default: false
 *         isOnline:
 *           type: boolean
 *           default: false
 *         lastChanged:
 *           type: integer
 *           format: int64
 *     BackupPayload:
 *       type: object
 *       required:
 *         - config
 *         - entities
 *       properties:
 *         config:
 *           $ref: '#/components/schemas/Config'
 *         entities:
 *           type: array
 *           items:
 *             $ref: '#/components/schemas/MonitoredEntity'
 *     WifiNetwork:
 *       type: object
 *       properties:
 *         ssid:
 *           type: string
 *         rssi:
 *           type: integer
 *         bssid:
 *           type: string
 *         channel:
 *           type: integer
 *         encryptionType:
 *           type: integer
 *     WifiScanResponse:
 *       type: object
 *       properties:
 *         status:
 *           type: string
 *           enum: [scanning, in progress, done]
 *         networks:
 *           type: array
 *           items:
 *             $ref: '#/components/schemas/WifiNetwork'
 *     BasicSysinfo:
 *       type: object
 *       properties:
 *         temperature:
 *           type: number
 *           format: float
 *         heapSize:
 *           type: integer
 *         freeHeap:
 *           type: integer
 *         minFreeHeap:
 *           type: integer
 *         maxAllocHeap:
 *           type: integer
 *         maxFreeBlock:
 *           type: integer
 *         psramSize:
 *           type: integer
 *         freePsram:
 *           type: integer
 *         minFreePsram:
 *           type: integer
 *         maxAllocPsram:
 *           type: integer
 *         rssi:
 *           type: integer
 *         wsClients:
 *           type: integer
 *         uptime:
 *           type: integer
 *         localTime:
 *           type: string
 *         timeSynced:
 *           type: boolean
 *         lastCheckDurationMs:
 *           type: integer
 *         lastCheckTime:
 *           type: string
 *         stackHighWaterBytes:
 *           type: integer
 *         version:
 *           type: string
 *         buildDate:
 *           type: string
 *         commit:
 *           type: string
 *         configEnabled:
 *           type: boolean
 *         configTimeoutRemaining:
 *           type: integer
 *           description: Remaining configuration mode timeout in seconds
 *           example: 600
 *     DetailedSysinfo:
 *       allOf:
 *         - $ref: '#/components/schemas/BasicSysinfo'
 *         - type: object
 *           properties:
 *             device:
 *               type: object
 *               properties:
 *                 board:
 *                   type: string
 *                   example: ESP32-S3-CAM
 *                 chipModel:
 *                   type: string
 *                   example: ESP32-S3
 *                 chipRevision:
 *                   type: integer
 *                 chipCores:
 *                   type: integer
 *                 cpuFreqMHz:
 *                   type: integer
 *                 resetReason:
 *                   type: string
 *                   example: POWERON
 *                 efuseMac:
 *                   type: string
 *                   example: AA:BB:CC:DD:EE:FF
 *                 hasTempSensor:
 *                   type: boolean
 *             memory:
 *               type: object
 *               properties:
 *                 heapSize:
 *                   type: integer
 *                 freeHeap:
 *                   type: integer
 *                 minFreeHeap:
 *                   type: integer
 *                 maxAllocHeap:
 *                   type: integer
 *                 maxFreeBlock:
 *                   type: integer
 *                 psramSize:
 *                   type: integer
 *                 freePsram:
 *                   type: integer
 *                 minFreePsram:
 *                   type: integer
 *                 maxAllocPsram:
 *                   type: integer
 *                 flashSize:
 *                   type: integer
 *                 flashSpeed:
 *                   type: integer
 *                 sketchSize:
 *                   type: integer
 *                 freeSketchSpace:
 *                   type: integer
 *             storage:
 *               type: object
 *               properties:
 *                 backend:
 *                   type: string
 *                   example: SD_MMC
 *                 available:
 *                   type: boolean
 *                 cardType:
 *                   type: string
 *                 cardSizeBytes:
 *                   type: integer
 *                   format: int64
 *                 totalBytes:
 *                   type: integer
 *                   format: int64
 *                 usedBytes:
 *                   type: integer
 *                   format: int64
 *                 freeBytes:
 *                   type: integer
 *                   format: int64
 *             network:
 *               type: object
 *               properties:
 *                 mode:
 *                   type: string
 *                   enum: [STA, AP]
 *                 connected:
 *                   type: boolean
 *                 ssid:
 *                   type: string
 *                 bssid:
 *                   type: string
 *                 channel:
 *                   type: integer
 *                 rssi:
 *                   type: integer
 *                 ip:
 *                   type: string
 *                 subnetMask:
 *                   type: string
 *                 gateway:
 *                   type: string
 *                 dns:
 *                   type: string
 *                 mac:
 *                   type: string
 *                 hostname:
 *                   type: string
 *                 wsClients:
 *                   type: integer
 *             time:
 *               type: object
 *               properties:
 *                 localTime:
 *                   type: string
 *                 timeSynced:
 *                   type: boolean
 *                 sntpServer:
 *                   type: string
 *                 timezoneOffset:
 *                   type: integer
 *                 uptime:
 *                   type: integer
 *             monitoring:
 *               type: object
 *               properties:
 *                 checkInterval:
 *                   type: integer
 *                 lastCheckDurationMs:
 *                   type: integer
 *                 lastCheckTime:
 *                   type: string
 *                 stackHighWaterBytes:
 *                   type: integer
 *             firmware:
 *               type: object
 *               properties:
 *                 version:
 *                   type: string
 *                 commit:
 *                   type: string
 *                 buildDate:
 *                   type: string
 *                 uiVersion:
 *                   type: string
 *                 expectedUiVersion:
 *                   type: string
 *                 otaPartition:
 *                   type: string
 *                 sdkVersion:
 *                   type: string
 *                 coreVersion:
 *                   type: string
 *                 configEnabled:
 *                   type: boolean
 *     FileEntry:
 *       type: object
 *       properties:
 *         name:
 *           type: string
 *         isDir:
 *           type: boolean
 *         size:
 *           type: integer
 */
class Api {
public:
    Api(AsyncWebServer& server);
    void registerEndpoints();
    static String cleanHostname(const String &input) {
        return HostnameUtils::cleanHostname(input);
    }
private:
    AsyncWebServer &server;
    String _pendingOtaMd5;
    String _otaUploadError;
    size_t _otaReceivedBytes = 0;
    bool _otaMutexHeld = false;
    bool _otaTaskSuspended = false;
    bool _otaStorageUnmounted = false;
    void registerConfigEndpoints();
    void registerOtaEndpoints();
    void registerCommonEndpoints();
    void registerSystemInformationEndpoints();
    void registerServicesEndpoints();
    void restoreOtaState();
};

#endif // _API_H_
