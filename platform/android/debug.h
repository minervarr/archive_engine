#pragma once
#include "arc/log.hh"

// Logs entry of a scope; use at top of JNI functions.
#define ARC_TRACE(func) ARC_LOGD("[TRACE] " func " enter")
