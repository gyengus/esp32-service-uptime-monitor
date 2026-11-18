#ifndef WEB_SECURITY_H
#define WEB_SECURITY_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "globals.h"
#include "Config.h"

class WebSecurity {
public:
    static String normalizeHost(String h) {
        h.toLowerCase();
        h.trim();
        if (h.startsWith("http://")) {
            h = h.substring(7);
        } else if (h.startsWith("https://")) {
            h = h.substring(8);
        }
        int slashIdx = h.indexOf('/');
        if (slashIdx != -1) {
            h = h.substring(0, slashIdx);
        }
        int colonIdx = h.indexOf(':');
        if (colonIdx != -1) {
            String port = h.substring(colonIdx + 1);
            if (port == "80") {
                h = h.substring(0, colonIdx);
            }
        }
        return h;
    }

    static bool isLocalDomainSuffix(const String& suffix) {
        return suffix == "local" || suffix == "lan" || suffix == "home" ||
               suffix == "home.arpa" || suffix == "localdomain" ||
               suffix == "internal" || suffix == "intra" || suffix == "fritz.box";
    }

    static bool isHostnameWithAllowedSuffix(const String& hostOnly, const String& baseName) {
        if (baseName.length() == 0) return false;
        if (hostOnly == baseName) return true;
        if (hostOnly.startsWith(baseName + ".")) {
            String suffix = hostOnly.substring(baseName.length() + 1);
            return isLocalDomainSuffix(suffix);
        }
        return false;
    }

    static bool isHostAllowed(const String& rawHost) {
        String host = normalizeHost(rawHost);
        int colonIdx = host.indexOf(':');
        String hostOnly = (colonIdx != -1) ? host.substring(0, colonIdx) : host;

        if (hostOnly == "localhost" || hostOnly == "127.0.0.1") {
            return true;
        }

        String myHostname = config.getConfig().hostname;
        myHostname.toLowerCase();
        if (isHostnameWithAllowedSuffix(hostOnly, myHostname)) {
            return true;
        }

        String defaultHost = String(DEFAULT_HOSTNAME);
        defaultHost.toLowerCase();
        if (isHostnameWithAllowedSuffix(hostOnly, defaultHost)) {
            return true;
        }

        if (isAPMode) {
            String apIp = WiFi.softAPIP().toString();
            if (hostOnly == apIp || hostOnly == "192.168.4.1") {
                return true;
            }
        }

        String staIp = WiFi.localIP().toString();
        if (staIp.length() > 0 && staIp != "0.0.0.0" && hostOnly == staIp) {
            return true;
        }

        return false;
    }

    static bool isRequestOriginTrusted(AsyncWebServerRequest *request) {
        if (!request) return true;

        String host = request->host();
        if (host.length() > 0 && !isHostAllowed(host)) {
            return false;
        }

        if (request->hasHeader("Sec-Fetch-Site")) {
            const AsyncWebHeader* sfs = request->getHeader("Sec-Fetch-Site");
            if (sfs && sfs->value().equalsIgnoreCase("cross-site")) {
                return false;
            }
        }

        if (request->hasHeader("Origin")) {
            const AsyncWebHeader *originHeader = request->getHeader("Origin");
            if (!originHeader) return false;
            String origin = originHeader->value();
            if (origin.length() == 0 || origin.equalsIgnoreCase("null")) {
                return false;
            }
            String normalizedOrigin = normalizeHost(origin);
            String normalizedHost = normalizeHost(host);
            if (!normalizedOrigin.equalsIgnoreCase(normalizedHost)) {
                return false;
            }
        } else if (request->hasHeader("Referer")) {
            const AsyncWebHeader *refererHeader = request->getHeader("Referer");
            if (refererHeader) {
                String referer = refererHeader->value();
                if (referer.length() > 0) {
                    String normalizedReferer = normalizeHost(referer);
                    String normalizedHost = normalizeHost(host);
                    if (!normalizedReferer.equalsIgnoreCase(normalizedHost)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    static bool isJsonContentType(AsyncWebServerRequest *request) {
        if (!request) return false;
        String ct = request->contentType();
        if (ct.length() == 0 && request->hasHeader("Content-Type")) {
            const AsyncWebHeader* h = request->getHeader("Content-Type");
            if (h) ct = h->value();
        }
        ct.toLowerCase();
        return ct.startsWith("application/json");
    }
};

#endif // WEB_SECURITY_H
