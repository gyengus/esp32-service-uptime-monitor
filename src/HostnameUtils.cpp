#include "HostnameUtils.h"
#include "Config.h"

String HostnameUtils::cleanHostname(const String &rawInput) {
    String input = rawInput;

    // 1. Replace multi-byte UTF-8 Hungarian accents (lowercase & uppercase)
    input.replace("á", "a"); input.replace("Á", "a");
    input.replace("é", "e"); input.replace("É", "e");
    input.replace("í", "i"); input.replace("Í", "i");
    input.replace("ó", "o"); input.replace("Ó", "o");
    input.replace("ö", "o"); input.replace("Ö", "o");
    input.replace("ő", "o"); input.replace("Ő", "o");
    input.replace("ú", "u"); input.replace("Ú", "u");
    input.replace("ü", "u"); input.replace("Ü", "u");
    input.replace("ű", "u"); input.replace("Ű", "u");

    String output = "";
    output.reserve(input.length() + 1);

    for (size_t i = 0; i < input.length(); i++) {
        uint8_t c = (uint8_t)input[i];

        // ISO-8859-2 / Windows-1250 single-byte fallback mappings
        if (c == 0xE1 || c == 0xC1) { c = 'a'; }
        else if (c == 0xE9 || c == 0xC9) { c = 'e'; }
        else if (c == 0xED || c == 0xCD) { c = 'i'; }
        else if (c == 0xF3 || c == 0xD3 || c == 0xF6 || c == 0xD6 || c == 0xF5 || c == 0xD5) { c = 'o'; }
        else if (c == 0xFA || c == 0xDA || c == 0xFC || c == 0xDC || c == 0xFB || c == 0xDB) { c = 'u'; }

        // Lowercase ASCII
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }

        // Allowed chars: a-z, 0-9, '-', '_'
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            output += (char)c;
        } else if (c == ' ' || c == '.' || c == '/') {
            // Avoid consecutive underscores
            if (output.length() > 0 && output[output.length() - 1] != '_' && output[output.length() - 1] != '-') {
                output += '_';
            }
        }
    }

    // Trim leading '_' and '-'
    while (output.length() > 0 && (output.startsWith("_") || output.startsWith("-"))) {
        output.remove(0, 1);
    }

    // Trim trailing '_' and '-'
    while (output.length() > 0 && (output.endsWith("_") || output.endsWith("-"))) {
        output.remove(output.length() - 1);
    }

    // Enforce max DNS label length (63 characters)
    if (output.length() > 63) {
        output = output.substring(0, 63);
        while (output.length() > 0 && (output.endsWith("_") || output.endsWith("-"))) {
            output.remove(output.length() - 1);
        }
    }

    if (output.length() == 0) {
        output = DEFAULT_HOSTNAME;
    }

    return output;
}
