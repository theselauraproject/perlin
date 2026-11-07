#include "executor/executor.h"

#include <wasmedge/wasmedge.h>

extern "C" void PerlinWasmEdgeStop(WasmEdge_ExecutorContext* executor) noexcept {
    if (executor)
        reinterpret_cast<WasmEdge::Executor::Executor*>(executor)->stop();
}
