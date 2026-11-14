#include "AddressValidator.h"

namespace {

enum class Ipv4ParseResult { NotNumeric, Invalid, Ok };

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

int hexValue(char c) {
    if (isDigit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

// Accepts the inet_aton number forms: decimal, 0x-hex and 0-octal.
bool parseNumberPart(const String& part, uint64_t& value) {
    if (part.length() == 0) return false;

    uint32_t base = 10;
    size_t start = 0;
    if (part.length() > 1 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X')) {
        base = 16;
        start = 2;
        if (part.length() == 2) return false;
    } else if (part.length() > 1 && part[0] == '0') {
        base = 8;
        start = 1;
    }

    value = 0;
    for (size_t i = start; i < part.length(); i++) {
        char c = part[i];
        if (base == 16 ? !isHexDigit(c) : !isDigit(c)) return false;
        uint32_t digit = hexValue(c);
        if (digit >= base) return false;
        value = value * base + digit;
        if (value > 0xFFFFFFFFULL) return false;
    }
    return true;
}

bool looksNumeric(const String& part) {
    if (part.length() == 0) return false;
    if (isDigit(part[0])) return true;
    return false;
}

Ipv4ParseResult parseIPv4Literal(String host, uint32_t& out) {
    if (host.endsWith(".")) host.remove(host.length() - 1);

    String parts[4];
    size_t count = 0;
    int start = 0;
    while (true) {
        int dot = host.indexOf('.', start);
        String part = (dot == -1) ? host.substring(start) : host.substring(start, dot);
        if (count == 4) {
            // more than 4 labels: only a problem when it is meant to be an address
            String last = (dot == -1) ? part : host.substring(host.lastIndexOf('.') + 1);
            return looksNumeric(last) ? Ipv4ParseResult::Invalid : Ipv4ParseResult::NotNumeric;
        }
        parts[count++] = part;
        if (dot == -1) break;
        start = dot + 1;
    }

    if (!looksNumeric(parts[count - 1])) return Ipv4ParseResult::NotNumeric;

    uint64_t values[4];
    for (size_t i = 0; i < count; i++) {
        if (!parseNumberPart(parts[i], values[i])) return Ipv4ParseResult::Invalid;
    }

    uint64_t result = 0;
    for (size_t i = 0; i + 1 < count; i++) {
        if (values[i] > 255) return Ipv4ParseResult::Invalid;
        result |= values[i] << (24 - 8 * i);
    }
    uint64_t lastMax = (1ULL << (8 * (5 - count))) - 1;
    if (values[count - 1] > lastMax) return Ipv4ParseResult::Invalid;
    result |= values[count - 1];

    out = static_cast<uint32_t>(result);
    return Ipv4ParseResult::Ok;
}

// Parses a (lowercase) IPv6 literal without brackets or zone id into eight groups.
bool parseIPv6(const String& text, uint16_t groups[8]) {
    if (text.length() < 2 || text.length() > 45) return false;

    String head = text;
    String tail = "";
    bool hasCompression = false;
    int compression = text.indexOf("::");
    if (compression != -1) {
        if (text.indexOf("::", compression + 1) != -1) return false;
        hasCompression = true;
        head = text.substring(0, compression);
        tail = text.substring(compression + 2);
    }

    uint16_t headGroups[8];
    uint16_t tailGroups[8];
    size_t headCount = 0;
    size_t tailCount = 0;

    auto parseSection = [](const String& section, uint16_t* dest, size_t& count) -> bool {
        if (section.length() == 0) return true;
        int start = 0;
        while (true) {
            int colon = section.indexOf(':', start);
            String token = (colon == -1) ? section.substring(start) : section.substring(start, colon);
            bool isLast = (colon == -1);

            if (isLast && token.indexOf('.') != -1) {
                uint32_t v4;
                if (parseIPv4Literal(token, v4) != Ipv4ParseResult::Ok) return false;
                if (token.indexOf('x') != -1 || token.indexOf('X') != -1) return false;
                if (count + 2 > 8) return false;
                dest[count++] = static_cast<uint16_t>(v4 >> 16);
                dest[count++] = static_cast<uint16_t>(v4 & 0xFFFF);
                return true;
            }

            if (token.length() == 0 || token.length() > 4) return false;
            uint32_t value = 0;
            for (size_t i = 0; i < token.length(); i++) {
                if (!isHexDigit(token[i])) return false;
                value = value * 16 + hexValue(token[i]);
            }
            if (count >= 8) return false;
            dest[count++] = static_cast<uint16_t>(value);
            if (isLast) return true;
            start = colon + 1;
        }
    };

    if (!parseSection(head, headGroups, headCount)) return false;
    if (!parseSection(tail, tailGroups, tailCount)) return false;

    size_t total = headCount + tailCount;
    if (hasCompression ? total > 7 : total != 8) return false;

    for (size_t i = 0; i < 8; i++) groups[i] = 0;
    for (size_t i = 0; i < headCount; i++) groups[i] = headGroups[i];
    for (size_t i = 0; i < tailCount; i++) groups[8 - tailCount + i] = tailGroups[i];
    return true;
}

bool isForbiddenIPv6(const uint16_t g[8]) {
    bool upperZero = (g[0] | g[1] | g[2] | g[3] | g[4]) == 0;
    if (upperZero && g[5] == 0 && g[6] == 0 && (g[7] == 0 || g[7] == 1)) return true;  // :: and ::1
    if (upperZero && (g[5] == 0xFFFF || g[5] == 0)) {
        // IPv4-mapped (::ffff:a.b.c.d) and deprecated IPv4-compatible (::a.b.c.d) forms
        uint32_t v4 = (static_cast<uint32_t>(g[6]) << 16) | g[7];
        return AddressValidator::isForbiddenIPv4(v4);
    }
    if ((g[0] & 0xFFC0) == 0xFE80) return true;  // link-local
    if ((g[0] & 0xFF00) == 0xFF00) return true;  // multicast
    return false;
}

bool isValidHostname(const String& host) {
    if (host.length() == 0 || host.length() > 253) return false;
    for (size_t i = 0; i < host.length(); i++) {
        char c = host[i];
        bool allowed = isDigit(c) || (c >= 'a' && c <= 'z') || c == '-' || c == '.' || c == '_';
        if (!allowed) return false;
    }
    return true;
}

bool isValidPort(const String& port) {
    if (port.length() == 0 || port.length() > 5) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < port.length(); i++) {
        if (!isDigit(port[i])) return false;
        value = value * 10 + (port[i] - '0');
    }
    return value >= 1 && value <= 65535;
}

bool hasControlOrSpace(const String& text) {
    for (size_t i = 0; i < text.length(); i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x20 || c == 0x7F || c == '\\') return true;
    }
    return false;
}

}  // namespace

namespace AddressValidator {

bool isForbiddenIPv4(uint32_t ip) {
    uint8_t first = static_cast<uint8_t>(ip >> 24);
    uint8_t second = static_cast<uint8_t>(ip >> 16);
    if (first == 0) return true;                     // 0.0.0.0/8
    if (first == 127) return true;                   // loopback
    if (first == 169 && second == 254) return true;  // link-local, cloud metadata
    if (first >= 224) return true;                   // multicast, reserved, broadcast
    return false;
}

bool isForbiddenIPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint32_t ip = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
                  (static_cast<uint32_t>(c) << 8) | d;
    return isForbiddenIPv4(ip);
}

bool isValidMonitoredUrl(const String& url) {
    if (url.length() == 0 || url.length() > MAX_ENTITY_ADDRESS_LENGTH) return false;
    if (hasControlOrSpace(url)) return false;

    String lowerUrl = url;
    lowerUrl.toLowerCase();

    size_t schemeLength;
    if (lowerUrl.startsWith("http://")) {
        schemeLength = 7;
    } else if (lowerUrl.startsWith("https://")) {
        schemeLength = 8;
    } else {
        return false;
    }

    String rest = lowerUrl.substring(schemeLength);
    int authorityEnd = rest.length();
    const char delimiters[] = {'/', '?', '#'};
    for (char delimiter : delimiters) {
        int index = rest.indexOf(delimiter);
        if (index != -1 && index < authorityEnd) authorityEnd = index;
    }
    String authority = rest.substring(0, authorityEnd);
    if (authority.length() == 0 || authority.indexOf('@') != -1) return false;

    String host;
    String port = "";
    bool isBracketed = authority.startsWith("[");
    if (isBracketed) {
        int close = authority.indexOf(']');
        if (close == -1) return false;
        host = authority.substring(1, close);
        String afterBracket = authority.substring(close + 1);
        if (afterBracket.length() > 0) {
            if (afterBracket[0] != ':') return false;
            port = afterBracket.substring(1);
            if (!isValidPort(port)) return false;
        }
    } else {
        int colon = authority.indexOf(':');
        if (colon != -1) {
            if (authority.indexOf(':', colon + 1) != -1) return false;
            host = authority.substring(0, colon);
            port = authority.substring(colon + 1);
            if (!isValidPort(port)) return false;
        } else {
            host = authority;
        }
    }

    if (host.length() == 0) return false;

    if (isBracketed) {
        uint16_t groups[8];
        if (!parseIPv6(host, groups)) return false;
        return !isForbiddenIPv6(groups);
    }

    if (host == "localhost" || host.endsWith(".localhost")) return false;

    uint32_t ip;
    switch (parseIPv4Literal(host, ip)) {
        case Ipv4ParseResult::Ok:
            return !isForbiddenIPv4(ip);
        case Ipv4ParseResult::Invalid:
            return false;
        case Ipv4ParseResult::NotNumeric:
            return isValidHostname(host);
    }
    return false;
}

bool isValidEntityName(const String& name) {
    if (name.length() == 0 || name.length() > MAX_ENTITY_NAME_LENGTH) return false;
    if (name[0] == ' ' || name[name.length() - 1] == ' ') return false;
    for (size_t i = 0; i < name.length(); i++) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        bool allowed = isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       c == ' ' || c == '_' || c == '-' || c == '.' || c >= 0x80;
        if (!allowed) return false;
    }
    return true;
}

bool isValidEntityIcon(const String& icon) {
    if (icon.length() > MAX_ENTITY_ICON_LENGTH) return false;
    for (size_t i = 0; i < icon.length(); i++) {
        unsigned char c = static_cast<unsigned char>(icon[i]);
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

}  // namespace AddressValidator
