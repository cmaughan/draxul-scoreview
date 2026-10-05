#pragma once

// Shared between draxul-test-scoreview-runtime and the tiny loadable module
// built from retaining_thread_module.cpp: the module starts a
// start_module_retaining_thread() worker that parks on this gate, so the test
// can unload the module while module code is still running on that worker.

#include <atomic>

struct ScoreviewRetainingGate
{
    std::atomic<bool> entered{ false };
    std::atomic<bool> released{ false };
    std::atomic<bool> finished{ false };
};

using ScoreviewRetainingStartFn = void (*)(ScoreviewRetainingGate* gate);

#define SCOREVIEW_RETAINING_START_SYMBOL "scoreview_retaining_test_start"
