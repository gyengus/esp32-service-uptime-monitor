#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <ESPAsyncWebServer.h>

// Regisztrálja a /upload és /list útvonalakat
void registerFileHandlers(AsyncWebServer &server);

#endif // FILE_MANAGER_H
