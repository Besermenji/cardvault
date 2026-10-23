#include "unity_config.h"

#include <Arduino.h>

void unityOutputStart(unsigned long baudrate) { Serial.begin(baudrate); }
void unityOutputChar(unsigned int c) { Serial.write(c); }
void unityOutputFlush(void) { Serial.flush(); }
void unityOutputComplete(void) { Serial.flush(); }  // never Serial.end(): keeps USB alive
