#pragma once

// A detached worker that keeps the binary containing its code loaded until
// the thread has fully exited. ScoreView runs inside a plugin module the host
// may FreeLibrary/dlclose while a detached worker (the microphone opener
// waiting on a consent dialog or a blocking device call) is still running;
// without a module reference the worker would resume into unmapped code.
//
// The worker takes its own reference on the containing module before it
// starts, and that reference is released only after no frame of module code
// remains on the thread: on Windows through FreeLibraryAndExitThread, on
// POSIX through a thread-specific-data destructor that is dlclose itself (it
// runs inside the C runtime after the thread's start routine has returned).
// The owner never joins, so shutdown stays non-blocking.
//
// Internal to draxul-scoreview-runtime (no public header).

#include <functional>

namespace draxul
{
namespace scoreview
{

// Starts `body` on a new detached thread that retains this module for its
// whole lifetime. Throws std::system_error when the thread cannot start (the
// module reference is released first), matching std::thread.
void start_module_retaining_thread(std::function<void()> body);

} // namespace scoreview
} // namespace draxul
