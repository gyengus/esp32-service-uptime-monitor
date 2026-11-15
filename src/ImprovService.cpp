#include "ImprovService.h"
#include <Esp.h>
#include <WiFi.h>
#include <Elog.h>
#include <cstring>

#include "Config.h"
#include "Storage.h"
#include "NetworkContext.h"
#include "HostnameUtils.h"
#include "LedService.h"
#include "globals.h"
#include "version.h"
#include "devices.h"

static const char IMPROV_HEADER[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

ImprovService& ImprovService::getInstance() {
    static ImprovService instance;
    return instance;
}

void ImprovService::resetParser() {
    _primaryCtx.reset();
    _secondaryCtx.reset();
    _provisionedInSession = false;
}

void ImprovService::handleSerial() {
    handleStream(Serial, _primaryCtx);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && !defined(UNIT_TEST)
    handleStream(Serial0, _secondaryCtx);
#endif
}

void ImprovService::handleStream(Stream& stream) {
    handleStream(stream, _primaryCtx);
}

void ImprovService::handleStream(Stream& stream, improv::StreamContext& ctx) {
    while (stream.available() > 0) {
        int c = stream.read();
        if (c < 0) {
            break;
        }
        unsigned long now = millis();
        if (ctx.pos > 0 && (now - ctx.lastByteMillis > improv::IMPROV_PACKET_TIMEOUT_MS)) {
            Logger.debug(MAINLOG, "Improv serial packet timeout after %u bytes, resetting parser", (unsigned int)ctx.pos);
            ctx.pos = 0;
        }
        ctx.lastByteMillis = now;
        parseByte(stream, ctx, static_cast<uint8_t>(c));
    }
}

void ImprovService::parseByte(Stream& stream, improv::StreamContext& ctx, uint8_t byte) {
    if (ctx.pos < 6) {
        if (byte == static_cast<uint8_t>(IMPROV_HEADER[ctx.pos])) {
            ctx.buffer[ctx.pos++] = byte;
        } else if (byte == static_cast<uint8_t>('I')) {
            ctx.buffer[0] = byte;
            ctx.pos = 1;
        } else {
            ctx.pos = 0;
        }
        return;
    }

    if (ctx.pos == 6) {
        if (byte == improv::IMPROV_SERIAL_VERSION) {
            ctx.buffer[ctx.pos++] = byte;
        } else if (byte == static_cast<uint8_t>('I')) {
            ctx.buffer[0] = byte;
            ctx.pos = 1;
        } else {
            ctx.pos = 0;
        }
        return;
    }

    if (ctx.pos == 7 || ctx.pos == 8) {
        ctx.buffer[ctx.pos++] = byte;
        return;
    }

    uint8_t dataLen = ctx.buffer[8];
    size_t expectedPayloadEnd = 9 + static_cast<size_t>(dataLen);

    if (ctx.pos < expectedPayloadEnd) {
        ctx.buffer[ctx.pos++] = byte;
        return;
    }

    // Current byte is the checksum byte at index (9 + dataLen)
    uint8_t calculatedChecksum = 0;
    for (size_t i = 0; i < expectedPayloadEnd; i++) {
        calculatedChecksum += ctx.buffer[i];
    }

    uint8_t packetType = ctx.buffer[7];
    ctx.pos = 0;

    if (byte != calculatedChecksum) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv serial checksum mismatch (expected: 0x%02X, got: 0x%02X)", calculatedChecksum, byte);
        return;
    }

    if (packetType == improv::TYPE_RPC) {
        handleRpc(stream, &ctx.buffer[9], dataLen);
    } else {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv serial unsupported packet type: 0x%02X", packetType);
    }
}

void ImprovService::handleRpc(Stream& stream, const uint8_t* data, size_t length) {
    if (length < 2) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv RPC packet too short (%u bytes)", (unsigned int)length);
        return;
    }

    uint8_t command = data[0];
    uint8_t rpcDataLen = data[1];
    if (static_cast<size_t>(2 + rpcDataLen) > length) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv RPC payload length mismatch (cmd: 0x%02X, declared: %u, actual: %u)",
                       command, rpcDataLen, (unsigned int)length);
        return;
    }

    switch (command) {
        case improv::CMD_WIFI_SETTINGS:
            handleWifiSettings(stream, data, length);
            break;
        case improv::CMD_GET_CURRENT_STATE:
            handleGetCurrentState(stream);
            break;
        case improv::CMD_GET_DEVICE_INFO:
            handleGetDeviceInfo(stream);
            break;
        case improv::CMD_GET_WIFI_NETWORKS:
            handleGetWifiNetworks(stream);
            break;
        case improv::CMD_HOSTNAME:
            handleHostname(stream, data, length);
            break;
        case improv::CMD_DEVICE_NAME:
            handleDeviceName(stream, data, length);
            break;
        case improv::CMD_GET_NETWORK_STATE:
            handleGetNetworkState(stream);
            break;
        default:
            sendError(stream, improv::ERROR_UNKNOWN_RPC);
            Logger.debug(MAINLOG, "Improv unknown RPC command: 0x%02X", command);
            break;
    }
}

void ImprovService::handleWifiSettings(Stream& stream, const uint8_t* data, size_t length) {
    if (length < 4) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv WIFI_SETTINGS packet too short (%u bytes)", (unsigned int)length);
        return;
    }

    uint8_t ssidLen = data[2];
    if (static_cast<size_t>(3 + ssidLen) >= length) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv WIFI_SETTINGS invalid SSID length: %u", ssidLen);
        return;
    }

    uint8_t passLen = data[3 + ssidLen];
    if (static_cast<size_t>(4 + ssidLen + passLen) > length || ssidLen == 0 || ssidLen > 32 || passLen > 64) {
        sendError(stream, improv::ERROR_INVALID_RPC);
        Logger.warning(MAINLOG, "Improv WIFI_SETTINGS invalid credentials length (ssid: %u, pass: %u)", ssidLen, passLen);
        return;
    }

    String ssid;
    ssid.reserve(ssidLen);
    for (uint8_t i = 0; i < ssidLen; i++) {
        ssid += static_cast<char>(data[3 + i]);
    }

    String password;
    password.reserve(passLen);
    for (uint8_t i = 0; i < passLen; i++) {
        password += static_cast<char>(data[4 + ssidLen + i]);
    }

    Logger.info(MAINLOG, "Improv RPC [0x01 WIFI_SETTINGS]: provisioning SSID '%s'...", ssid.c_str());

    sendError(stream, improv::ERROR_NONE);
    sendState(stream, improv::STATE_PROVISIONING);

    if (storage.fileExists(CONFIG_FILE_PATH)) {
        config.loadConfig();
    }
    config_t cfg = config.getConfig();
    cfg.hostname = HostnameUtils::cleanHostname(cfg.hostname);

    bool connected = networkContext.connect(ssid, password, cfg.hostname, WIFI_CONNECT_TIMEOUT, isAPMode);
    if (!connected) {
        sendError(stream, improv::ERROR_UNABLE_TO_CONNECT);
        sendState(stream, improv::STATE_AUTHORIZED);
        Logger.error(MAINLOG, "Improv Wi-Fi failed to connect to SSID: %s", ssid.c_str());
        return;
    }

    cfg.wifiSSID = ssid;
    cfg.wifiPassword = password;
    if (!config.saveConfig(cfg)) {
        sendError(stream, improv::ERROR_UNKNOWN);
        sendState(stream, improv::STATE_AUTHORIZED);
        Logger.error(MAINLOG, "Improv Wi-Fi failed to save configuration");
        return;
    }

    _provisionedInSession = true;
    isConfigFunctionsEnabled = true;
    resetConfigModeActivity();

    String url = getDeviceUrl();
    sendState(stream, improv::STATE_PROVISIONED);
    std::vector<String> urls = { url };
    sendRpcResponse(stream, improv::CMD_WIFI_SETTINGS, urls);
    stream.flush();

    Logger.info(MAINLOG, "Improv Wi-Fi provisioned successfully (SSID: %s, URL: %s), restarting...", ssid.c_str(), url.c_str());
    shouldRestart = true;
}

void ImprovService::handleGetCurrentState(Stream& stream) {
    sendError(stream, improv::ERROR_NONE);
    if (isProvisioned()) {
        String url = getDeviceUrl();
        sendState(stream, improv::STATE_PROVISIONED);
        std::vector<String> urls = { url };
        sendRpcResponse(stream, improv::CMD_GET_CURRENT_STATE, urls);
        Logger.debug(MAINLOG, "Improv RPC [0x02 GET_CURRENT_STATE]: STATE_PROVISIONED (%s)", url.c_str());
    } else {
        sendState(stream, improv::STATE_AUTHORIZED);
        Logger.debug(MAINLOG, "Improv RPC [0x02 GET_CURRENT_STATE]: STATE_AUTHORIZED");
    }
}

void ImprovService::handleGetDeviceInfo(Stream& stream) {
    sendError(stream, improv::ERROR_NONE);
    String hostname = getEffectiveHostname();
    String chipModel = String(ESP.getChipModel());

    std::vector<String> info = {
        "ESP32 Service Uptime Monitor",
        String(FIRMWARE_VERSION),
        chipModel,
        hostname
    };
    sendRpcResponse(stream, improv::CMD_GET_DEVICE_INFO, info);
    Logger.debug(MAINLOG, "Improv RPC [0x03 GET_DEVICE_INFO]: version=%s, chip=%s, host=%s",
                 FIRMWARE_VERSION, chipModel.c_str(), hostname.c_str());
}

void ImprovService::handleGetWifiNetworks(Stream& stream) {
    Logger.debug(MAINLOG, "Improv RPC [0x04 GET_WIFI_NETWORKS]: starting Wi-Fi scan...");
    sendError(stream, improv::ERROR_NONE);

    int n = WiFi.scanComplete();
    if (n < 0) {
        n = WiFi.scanNetworks(false, false);
    }

    std::vector<String> seenSsids;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }

        bool duplicate = false;
        for (const String& seen : seenSsids) {
            if (seen == ssid) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        seenSsids.push_back(ssid);

        String rssi = String(WiFi.RSSI(i));
        String auth = (WiFi.encryptionType(i) == 0) ? "NO" : "YES";
        sendRpcResponse(stream, improv::CMD_GET_WIFI_NETWORKS, { ssid, rssi, auth });
    }

    if (n >= 0) {
        WiFi.scanDelete();
    }

    // Empty response signals end of network list
    sendRpcResponse(stream, improv::CMD_GET_WIFI_NETWORKS, {});
    Logger.debug(MAINLOG, "Improv RPC [0x04 GET_WIFI_NETWORKS]: sent %u unique networks", (unsigned int)seenSsids.size());
}

void ImprovService::handleHostname(Stream& stream, const uint8_t* data, size_t length) {
    uint8_t rpcDataLen = data[1];
    if (rpcDataLen == 0) {
        sendError(stream, improv::ERROR_NONE);
        String hostname = getEffectiveHostname();
        sendRpcResponse(stream, improv::CMD_HOSTNAME, { hostname });
        Logger.debug(MAINLOG, "Improv RPC [0x05 HOSTNAME GET]: %s", hostname.c_str());
        return;
    }

    String rawHostname;
    rawHostname.reserve(rpcDataLen);
    for (uint8_t i = 0; i < rpcDataLen && (2 + i) < length; i++) {
        rawHostname += static_cast<char>(data[2 + i]);
    }
    String cleaned = HostnameUtils::cleanHostname(rawHostname);
    if (cleaned.length() == 0) {
        sendError(stream, improv::ERROR_BAD_HOSTNAME);
        Logger.warning(MAINLOG, "Improv RPC [0x05 HOSTNAME SET]: rejected invalid hostname '%s'", rawHostname.c_str());
        return;
    }

    if (storage.fileExists(CONFIG_FILE_PATH)) {
        config.loadConfig();
    }
    config_t cfg = config.getConfig();
    cfg.hostname = cleaned;
    config.saveConfig(cfg);

    sendError(stream, improv::ERROR_NONE);
    sendRpcResponse(stream, improv::CMD_HOSTNAME, { cleaned });
    Logger.info(MAINLOG, "Improv RPC [0x05 HOSTNAME SET]: updated hostname to '%s'", cleaned.c_str());
}

void ImprovService::handleDeviceName(Stream& stream, const uint8_t* data, size_t length) {
    uint8_t rpcDataLen = data[1];
    sendError(stream, improv::ERROR_NONE);
    if (rpcDataLen == 0) {
        String hostname = getEffectiveHostname();
        sendRpcResponse(stream, improv::CMD_DEVICE_NAME, { hostname });
        Logger.debug(MAINLOG, "Improv RPC [0x06 DEVICE_NAME GET]: %s", hostname.c_str());
        return;
    }

    String rawName;
    rawName.reserve(rpcDataLen);
    for (uint8_t i = 0; i < rpcDataLen && (2 + i) < length; i++) {
        rawName += static_cast<char>(data[2 + i]);
    }
    String cleaned = HostnameUtils::cleanHostname(rawName);
    if (storage.fileExists(CONFIG_FILE_PATH)) {
        config.loadConfig();
    }
    config_t cfg = config.getConfig();
    cfg.hostname = cleaned;
    config.saveConfig(cfg);

    sendRpcResponse(stream, improv::CMD_DEVICE_NAME, { cleaned });
    Logger.info(MAINLOG, "Improv RPC [0x06 DEVICE_NAME SET]: updated device name to '%s'", cleaned.c_str());
}

void ImprovService::handleGetNetworkState(Stream& stream) {
    sendError(stream, improv::ERROR_NONE);
    // Bit 0 (1): IS_ONLINE, Bit 1 (2): SUPPORTS_WIFI
    if (isProvisioned()) {
        String url = getDeviceUrl();
        sendRpcResponse(stream, improv::CMD_GET_NETWORK_STATE, { "3", url });
        Logger.debug(MAINLOG, "Improv RPC [0x07 GET_NETWORK_STATE]: online (flags=3, url=%s)", url.c_str());
    } else {
        sendRpcResponse(stream, improv::CMD_GET_NETWORK_STATE, { "2" });
        Logger.debug(MAINLOG, "Improv RPC [0x07 GET_NETWORK_STATE]: offline (flags=2)");
    }
}

void ImprovService::sendState(Stream& stream, improv::ImprovState state) {
    uint8_t stateByte = static_cast<uint8_t>(state);
    sendPacket(stream, improv::TYPE_CURRENT_STATE, &stateByte, 1);
}

void ImprovService::sendError(Stream& stream, improv::ImprovError error) {
    uint8_t errorByte = static_cast<uint8_t>(error);
    sendPacket(stream, improv::TYPE_ERROR_STATE, &errorByte, 1);
}

void ImprovService::sendRpcResponse(Stream& stream, improv::ImprovCommand command, const std::vector<String>& strings) {
    uint8_t payload[255];
    payload[0] = static_cast<uint8_t>(command);
    size_t offset = 2;

    for (const String& s : strings) {
        size_t len = s.length();
        if (offset + 1 + len > sizeof(payload)) {
            if (offset + 1 >= sizeof(payload)) {
                break;
            }
            len = sizeof(payload) - offset - 1;
        }
        payload[offset++] = static_cast<uint8_t>(len);
        if (len > 0) {
            memcpy(&payload[offset], s.c_str(), len);
            offset += len;
        }
    }

    payload[1] = static_cast<uint8_t>(offset - 2);
    sendPacket(stream, improv::TYPE_RPC_RESPONSE, payload, static_cast<uint8_t>(offset));
}

void ImprovService::sendPacket(Stream& stream, improv::ImprovPacketType type, const uint8_t* data, uint8_t length) {
    uint8_t packet[267];
    // Leading newline ensures line-based serial parsers (such as improv-wifi-serial-sdk)
    // always recognize the IMPROV header at the start of a fresh line even after log output.
    packet[0] = '\n';
    memcpy(&packet[1], IMPROV_HEADER, 6);
    packet[7] = improv::IMPROV_SERIAL_VERSION;
    packet[8] = static_cast<uint8_t>(type);
    packet[9] = length;

    if (length > 0 && data != nullptr) {
        memcpy(&packet[10], data, length);
    }

    uint8_t checksum = 0;
    size_t bodyLen = 9 + static_cast<size_t>(length);
    for (size_t i = 0; i < bodyLen; i++) {
        checksum += packet[1 + i];
    }

    packet[1 + bodyLen] = checksum;
    packet[2 + bodyLen] = '\n';

    stream.write(packet, 3 + bodyLen);
}

bool ImprovService::isProvisioned() const {
    return (!isAPMode && networkContext.isConnected()) ||
           (_provisionedInSession && networkContext.isConnected());
}

String ImprovService::getDeviceUrl() {
    return "http://" + getEffectiveHostname() + ".local/";
}

String ImprovService::getEffectiveHostname() {
    if (storage.fileExists(CONFIG_FILE_PATH) && config.getConfig().wifiSSID.length() == 0) {
        config.loadConfig();
    }
    String hostname = config.getConfig().hostname;
    if (hostname.length() == 0) {
        hostname = DEFAULT_HOSTNAME;
    }
    return HostnameUtils::cleanHostname(hostname);
}

ImprovService& improvService = ImprovService::getInstance();
