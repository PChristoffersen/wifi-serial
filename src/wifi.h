#pragma once

#include <unistd.h>

void wifi_init();

bool wifi_join(const char *ssid, const char *password, uint timeout_ms);
bool wifi_restore();
