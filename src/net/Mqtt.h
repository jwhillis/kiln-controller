#pragma once

void mqttBegin();
void mqttLoop();
bool mqttConnected();
void mqttPublishAlarm(const char* msg);
