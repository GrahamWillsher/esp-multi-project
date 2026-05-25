#include "runtime/runtime_context.h"

RuntimeContext& RuntimeContext::instance() {
    static RuntimeContext instance;
    return instance;
}

void RuntimeContext::set_tx_soc(uint8_t soc) {
    tx_soc_ = soc;
}
