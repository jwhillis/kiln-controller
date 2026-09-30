#pragma once

void controlStart();
bool controlBusy();          // firing or autotune — block OTA
void controlApplyPidGains();
void controlAcceptTune();    // apply Tyreus-Luyben from last tune
