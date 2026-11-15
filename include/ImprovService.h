#ifndef _IMPROV_SERVICE_H_
#define _IMPROV_SERVICE_H_

#include <Arduino.h>
#include <vector>

namespace improv {

static const uint8_t IMPROV_SERIAL_VERSION = 0x01;
static const unsigned long IMPROV_PACKET_TIMEOUT_MS = 1000;

enum ImprovPacketType : uint8_t {
    TYPE_CURRENT_STATE = 0x01,
    TYPE_ERROR_STATE   = 0x02,
    TYPE_RPC           = 0x03,
    TYPE_RPC_RESPONSE  = 0x04
};

enum ImprovState : uint8_t {
    STATE_STOPPED      = 0x00,
    STATE_AUTHORIZED   = 0x02,
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED  = 0x04
};

enum ImprovError : uint8_t {
    ERROR_NONE              = 0x00,
    ERROR_INVALID_RPC       = 0x01,
    ERROR_UNKNOWN_RPC       = 0x02,
    ERROR_UNABLE_TO_CONNECT = 0x03,
    ERROR_NOT_AUTHORIZED    = 0x04,
    ERROR_BAD_HOSTNAME      = 0x05,
    ERROR_UNKNOWN           = 0xFF
};

// Official Improv Wi-Fi Serial v1 RPC Command IDs (https://www.improv-wifi.com/serial/)
enum ImprovCommand : uint8_t {
    CMD_WIFI_SETTINGS     = 0x01,
    CMD_GET_CURRENT_STATE = 0x02,
    CMD_GET_DEVICE_INFO   = 0x03,
    CMD_GET_WIFI_NETWORKS = 0x04,
    CMD_HOSTNAME          = 0x05,
    CMD_DEVICE_NAME       = 0x06,
    CMD_GET_NETWORK_STATE = 0x07
};

struct StreamContext {
    uint8_t buffer[270];
    size_t pos = 0;
    unsigned long lastByteMillis = 0;

    void reset() {
        pos = 0;
        lastByteMillis = 0;
    }
};

} // namespace improv

class ImprovService {
public:
    ImprovService(const ImprovService&) = delete;
    ImprovService& operator=(const ImprovService&) = delete;
    static ImprovService& getInstance();

    void handleSerial();
    void handleStream(Stream& stream);
    void handleStream(Stream& stream, improv::StreamContext& ctx);
    void resetParser();

private:
    ImprovService() : _provisionedInSession(false) {}

    improv::StreamContext _primaryCtx;
    improv::StreamContext _secondaryCtx;
    bool _provisionedInSession;

    void parseByte(Stream& stream, improv::StreamContext& ctx, uint8_t byte);
    void handleRpc(Stream& stream, const uint8_t* data, size_t length);

    void handleWifiSettings(Stream& stream, const uint8_t* data, size_t length);
    void handleGetCurrentState(Stream& stream);
    void handleGetDeviceInfo(Stream& stream);
    void handleGetWifiNetworks(Stream& stream);
    void handleHostname(Stream& stream, const uint8_t* data, size_t length);
    void handleDeviceName(Stream& stream, const uint8_t* data, size_t length);
    void handleGetNetworkState(Stream& stream);

    void sendState(Stream& stream, improv::ImprovState state);
    void sendError(Stream& stream, improv::ImprovError error);
    void sendRpcResponse(Stream& stream, improv::ImprovCommand command, const std::vector<String>& strings);
    void sendPacket(Stream& stream, improv::ImprovPacketType type, const uint8_t* data, uint8_t length);

    bool isProvisioned() const;
    String getDeviceUrl();
    String getEffectiveHostname();
};

extern ImprovService& improvService;

#endif // _IMPROV_SERVICE_H_
