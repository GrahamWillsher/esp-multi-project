#pragma once

#include <stddef.h>

namespace BootstrapPhaseRunner {

using PhaseFn = void (*)();

struct Phase {
    const char* name;
    PhaseFn run;
};

void run_phases(const Phase* phases, size_t count);

} // namespace BootstrapPhaseRunner
