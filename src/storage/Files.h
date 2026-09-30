#pragma once
#include <Arduino.h>
#include "../control/Profile.h"

bool filesBegin();
bool filesOk();

bool filesLoadProfile(const String& name, Profile& p, String& err);
bool filesSaveProfile(const Profile& p, String& err);
bool filesDeleteProfile(const String& name);
String filesListProfilesJson();

String filesListJson(const char* dir);
bool filesDelete(const String& path);
bool filesEnsureDirs();

void filesSeedExamples();
