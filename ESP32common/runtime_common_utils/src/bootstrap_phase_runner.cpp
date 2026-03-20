#include <runtime_common_utils/bootstrap_phase_runner.h>

namespace BootstrapPhaseRunner {

void run_phases(const Phase* phases, size_t count) {
    if (phases == nullptr || count == 0) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        if (phases[i].run != nullptr) {
            phases[i].run();
        }
    }
}

} // namespace BootstrapPhaseRunner
