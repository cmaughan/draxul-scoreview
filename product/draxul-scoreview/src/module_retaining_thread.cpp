#include "module_retaining_thread.h"

#include <memory>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>

#include <thread>
#endif

namespace draxul
{
namespace scoreview
{

namespace
{

// Any address inside this binary identifies the module to retain.
void module_anchor() {}

#ifdef _WIN32

struct RetainedThread
{
    std::function<void()> body;
    HMODULE module = nullptr;
};

DWORD WINAPI retained_thread_main(LPVOID parameter)
{
    HMODULE module = nullptr;
    {
        std::unique_ptr<RetainedThread> context(static_cast<RetainedThread*>(parameter));
        module = context->module;
        context->body();
    } // the body and its captures are destroyed while the module is still held
    if (module != nullptr)
        FreeLibraryAndExitThread(module, 0); // never returns into this module
    return 0;
}

#else

// The key's destructor is dlclose itself, so the final module release runs
// in the C runtime's thread-exit path after the start routine (the last
// module frame) has returned. dlclose's int result is ignored there; the
// pointer conversion matches the destructor slot's calling convention on
// every supported ABI.
bool module_release_key(pthread_key_t& out)
{
    struct Key
    {
        pthread_key_t key{};
        bool valid = false;
    };
    static const Key key = []() {
        Key created;
        using Destructor = void (*)(void*);
        created.valid = pthread_key_create(&created.key,
                            reinterpret_cast<Destructor>(reinterpret_cast<void*>(&::dlclose)))
            == 0;
        return created;
    }();
    out = key.key;
    return key.valid;
}

void* retain_this_module()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&module_anchor), &info) == 0 || info.dli_fname == nullptr)
        return nullptr;
    // RTLD_NOLOAD: take a reference on the already-loaded image only.
    return dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
}

void release_at_thread_exit(void* module)
{
    if (module == nullptr)
        return;
    // If no safe release point can be armed, the reference is deliberately
    // kept: leaving the module mapped is the only option that cannot crash.
    pthread_key_t key{};
    if (module_release_key(key))
        (void)pthread_setspecific(key, module);
}

#endif

} // namespace

void start_module_retaining_thread(std::function<void()> body)
{
#ifdef _WIN32
    auto context = std::make_unique<RetainedThread>();
    context->body = std::move(body);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&module_anchor), &context->module))
        context->module = nullptr;
    HANDLE thread = CreateThread(nullptr, 0, &retained_thread_main, context.get(), 0, nullptr);
    if (thread == nullptr)
    {
        const DWORD error = GetLastError();
        if (context->module != nullptr)
            FreeLibrary(context->module);
        throw std::system_error(static_cast<int>(error), std::system_category(),
            "module-retaining thread");
    }
    context.release(); // owned by retained_thread_main now
    CloseHandle(thread); // detached
#else
    void* module = retain_this_module();
    try
    {
        std::thread([module, body = std::move(body)]() mutable {
            body();
            body = nullptr; // destroy captures while the module is still held
            release_at_thread_exit(module);
        }).detach();
    }
    catch (...)
    {
        if (module != nullptr)
            dlclose(module);
        throw;
    }
#endif
}

} // namespace scoreview
} // namespace draxul
