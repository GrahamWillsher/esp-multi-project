#pragma once

#include "common.h"

// Note: SystemState and ErrorSeverity enums are defined in common.h
// This header just declares the functions that operate on them

void transition_to_state(SystemState new_state);
void handle_error(ErrorSeverity severity, const char* component, const char* message);
