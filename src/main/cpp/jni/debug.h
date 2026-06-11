#pragma once
#include "ae/log.hh"

// Logs entry of a scope; use at top of JNI functions.
#define AE_TRACE(func) AE_LOGD("[TRACE] " func " enter")
