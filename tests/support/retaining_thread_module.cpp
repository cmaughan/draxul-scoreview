// A minimal loadable module that hosts a module-retaining worker the way the
// ScoreView plugin module hosts its microphone opener (see
// scoreview_microphone_tests.cpp). Deliberately free of ScoreView/SDL/ObjC
// dependencies so the platform loader can genuinely unmap it.

#include "module_retaining_thread.h"
#include "support/retaining_thread_module.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#define SCOREVIEW_RETAINING_EXPORT extern "C" __declspec(dllexport)
#else
#define SCOREVIEW_RETAINING_EXPORT extern "C" __attribute__((visibility("default")))
#endif

SCOREVIEW_RETAINING_EXPORT void scoreview_retaining_test_start(ScoreviewRetainingGate* gate)
{
    draxul::scoreview::start_module_retaining_thread([gate]() {
        gate->entered.store(true);
        // Module code stays on this thread's stack while the host unloads.
        while (!gate->released.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        gate->finished.store(true);
        // Keep executing module code after the host could observe
        // completion, as the real opener does on its way out.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });
}
