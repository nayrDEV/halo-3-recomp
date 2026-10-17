// Guest threads: allocating a thread's guest-visible state (stack, KPCR,
// KTHREAD, TLS), binding it to a host thread, starting threads for
// ExCreateThread, user APC delivery, thread exit, and the TLS slot
// allocator. Kernel object side of threads (waits, suspend counts) is in
// objects.cpp.

#include "thread.h"
#include "module.h"
#include "objects.h"

#include "../memory.h"

#include <ppc_recomp_shared.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <execinfo.h>
#if !defined(__ANDROID__)
extern "C" char __executable_start;
#else
#include <dlfcn.h>
#endif

namespace h3::kernel
{
    namespace
    {
        thread_local GuestThread* t_currentThread = nullptr;

        // Host thread of each guest thread (by KTHREAD), for
        // DumpHostBacktrace.
        std::mutex g_hostThreadMutex;
        std::map<uint32_t, pthread_t> g_hostThreads;
        std::map<uint32_t, pid_t> g_hostTids;

        // DumpHostBacktrace's hand-off: the target thread's SIGUSR2 handler
        // fills these.
        constexpr int kMaxBacktraceFrames = 48;
        void* g_backtrace[kMaxBacktraceFrames];
        std::atomic<int> g_backtraceFrames{ -1 };

#if defined(__ANDROID__)
        using BacktraceFunction = int (*)(void**, int);
        BacktraceFunction g_backtraceFunction = reinterpret_cast<BacktraceFunction>(dlsym(RTLD_DEFAULT, "backtrace"));
#endif
        void OnBacktraceSignal(int)
        {
#if defined(__ANDROID__)
            if (g_backtraceFunction)
                g_backtraceFrames.store(g_backtraceFunction(g_backtrace, kMaxBacktraceFrames));
#else
            g_backtraceFrames.store(backtrace(g_backtrace, kMaxBacktraceFrames));
#endif
        }

        std::mutex g_tlsMutex;
        std::vector<bool> g_tlsUsed;

        std::atomic<uint32_t> g_nextThreadId{ 1 };

        // Thrown by ExitCurrentThread, caught at the thread's entry point.
        struct ThreadExit
        {
            uint32_t exitCode;
        };

        // Wraps guest thread state in a kernel Thread object (what handles and waits
        // refer to) and registers it.
        std::shared_ptr<Thread> MakeThreadObject(GuestThread* guest)
        {
            auto thread = std::make_shared<Thread>();
            thread->guest = guest;
            thread->threadId = g_nextThreadId++;
            BindGuestObject(thread, guest->kthread);
            return thread;
        }
    }

    namespace
    {
        // Smallest stack a guest thread gets (Xenia uses the same floor);
        // some titles pass 0 or tiny sizes.
        constexpr uint32_t kMinStackSize = 16 * 1024;
        // Bytes left free at the top of a new stack: room for the back chain
        // and the LR save area that callees write above r1.
        constexpr uint32_t kStackTopReserve = 0x100;
    }

    GuestThread* AllocateGuestThread(uint32_t stackSize)
    {
        auto* thread = new GuestThread();

        stackSize = (std::max(stackSize, kMinStackSize) + kPageSize4K - 1) & ~(kPageSize4K - 1);
        thread->stackLimit = SystemAlloc(stackSize);
        thread->stackBase = thread->stackLimit + stackSize;

        // Static TLS first, then the dynamic slots (Xenia's layout). The
        // static block starts as a copy of the XEX's TLS template.
        uint32_t staticSize = AlignUp(g_module.tlsDataSize, 16);  // keep the slots 16-byte aligned
        thread->tlsStatic = SystemAlloc(staticSize + g_module.tlsSlotCount * 4);
        thread->tlsSlots = thread->tlsStatic + staticSize;
        if (g_module.tlsRawDataAddress != 0)
        {
            memcpy(GuestPtr<uint8_t>(thread->tlsStatic), GuestPtr<uint8_t>(g_module.tlsRawDataAddress),
                g_module.tlsRawDataSize);
        }

        thread->kthread = SystemAlloc(kKThreadSize);

        thread->pcr = SystemAlloc(kPcrSize);
        GuestRef<uint32_t>(thread->pcr + kPcrTlsPtr) = thread->tlsStatic;
        GuestRef<uint32_t>(thread->pcr + kPcrSelf) = thread->pcr;
        GuestRef<uint32_t>(thread->pcr + kPcrStackBase) = thread->stackBase;
        GuestRef<uint32_t>(thread->pcr + kPcrStackLimit) = thread->stackLimit;
        GuestRef<uint32_t>(thread->pcr + kPcrCurrentThread) = thread->kthread;
        *GuestPtr<uint8_t>(thread->pcr + kPcrCurrentCpu) = 0;

        return thread;
    }

    void BindGuestThread(GuestThread* thread, PPCContext& ctx)
    {
        ctx.r1.u64 = thread->stackBase - kStackTopReserve;
        ctx.r13.u64 = thread->pcr;
        // The recompiled code keeps its own copy of the host FP control
        // register and writes it back whole when it changes the flush mode.
        // Left at zero, that write would unmask every FP exception (SIGFPE on
        // the next inexact result), so start from the host's real value.
        ctx.fpscr.csr = ctx.fpscr.getcsr();
        t_currentThread = thread;
        std::lock_guard lock(g_hostThreadMutex);
        g_hostThreads[thread->kthread] = pthread_self();
        g_hostTids[thread->kthread] = pid_t(syscall(SYS_gettid));
    }

    void DumpHostBacktrace(uint32_t kthread)
    {
        pthread_t target;
        pid_t targetTid = 0;
        {
            std::lock_guard lock(g_hostThreadMutex);
            auto found = g_hostThreads.find(kthread);
            if (found == g_hostThreads.end())
            {
                fprintf(stderr, "[kernel] no host thread registered for kthread %08X\n", kthread);
                return;
            }
            target = found->second;
            auto tid = g_hostTids.find(kthread);
            if (tid != g_hostTids.end())
                targetTid = tid->second;
        }
        static std::mutex dumpMutex;  // one dump at a time (shared buffer)
        std::lock_guard lock(dumpMutex);
        static const bool installed = [] {
            struct sigaction action{};
            action.sa_handler = OnBacktraceSignal;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_RESTART;
            return sigaction(SIGUSR2, &action, nullptr) == 0;
        }();
        if (!installed)
            return;
        g_backtraceFrames.store(-1);
#if defined(__ANDROID__)
        if (targetTid <= 0 || syscall(SYS_tgkill, getpid(), targetTid, SIGUSR2) != 0)
            return;
#else
        if (pthread_kill(target, SIGUSR2) != 0)
            return;
#endif
        constexpr int kWaitMs = 200;
        for (int i = 0; i < kWaitMs && g_backtraceFrames.load() < 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        int frames = g_backtraceFrames.load();
#if !defined(__ANDROID__)
        uintptr_t executable = reinterpret_cast<uintptr_t>(&__executable_start);
        fprintf(stderr, "[kernel]   thread %08X host stack (executable offsets; nm -C -n to name):", kthread);
        for (int i = 0; i < frames; i++)
            fprintf(stderr, " %lx", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_backtrace[i]) - executable));
#else
        fprintf(stderr, "[kernel]   thread %08X host stack:", kthread);
        for (int i = 0; i < frames; i++)
        {
            Dl_info info{};
            if (dladdr(g_backtrace[i], &info) && info.dli_sname)
                fprintf(stderr, " %s+0x%lx", info.dli_sname,
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(g_backtrace[i]) - reinterpret_cast<uintptr_t>(info.dli_saddr)));
            else
                fprintf(stderr, " %p", g_backtrace[i]);
        }
#endif
        fprintf(stderr, "\n");
    }

    void DumpAllHostThreads()
    {
        std::vector<uint32_t> threads;
        {
            std::lock_guard lock(g_hostThreadMutex);
            for (const auto& [kthread, host] : g_hostThreads)
                threads.push_back(kthread);
        }
        for (uint32_t kthread : threads)
        {
            fprintf(stderr, "[watchdog] thread %08X\n", kthread);
            DumpHostBacktrace(kthread);
        }
    }

    GuestThread* CurrentThread()
    {
        return t_currentThread;
    }

    std::shared_ptr<Thread> AdoptHostThread(PPCContext& ctx, uint32_t stackSize)
    {
        GuestThread* guest = AllocateGuestThread(stackSize);
        BindGuestThread(guest, ctx);
        auto thread = MakeThreadObject(guest);
        RegisterThreadObject(thread);
        return thread;
    }

    std::shared_ptr<Thread> StartGuestThread(uint32_t stackSize, uint32_t xapiStartup, uint32_t start,
        uint32_t context, bool suspended)
    {
        auto thread = MakeThreadObject(AllocateGuestThread(stackSize));
        thread->suspendCount = suspended ? 1 : 0;
        fprintf(stderr, "[thread] start id %u start %08X xapi %08X suspended %d kthread %08X\n",
            thread->threadId, start, xapiStartup, suspended ? 1 : 0, thread->guest->kthread);

        std::thread([thread, xapiStartup, start, context]
        {
            char threadName[16];
            snprintf(threadName, sizeof(threadName), "H3Guest%u", thread->threadId);
            pthread_setname_np(pthread_self(), threadName);
            RegisterThreadObject(thread);

            {
                auto lock = DispatcherLock();
                WaitDispatcher(lock, [&] { return thread->suspendCount == 0; });
            }

            PPCContext ctx{};
            BindGuestThread(thread->guest, ctx);

            uint32_t exitCode;
            try
            {
                uint32_t entry = xapiStartup ? xapiStartup : start;
                ctx.r3.u64 = xapiStartup ? start : context;
                ctx.r4.u64 = context;
                (PPC_LOOKUP_FUNC(g_base, entry))(ctx, g_base);
                exitCode = ctx.r3.u32;
            }
            catch (const ThreadExit& exit)
            {
                exitCode = exit.exitCode;
            }

            auto lock = DispatcherLock();
            thread->exited = true;
            thread->exitCode = exitCode;
            thread->SyncHeader();
            NotifyDispatcher();
        }).detach();

        return thread;
    }

    void DeliverUserApcs(PPCContext& ctx)
    {
        auto thread = CurrentThreadObject();
        for (;;)
        {
            Thread::UserApc apc;
            {
                auto lock = DispatcherLock();
                if (thread->userApcs.empty())
                    return;
                apc = thread->userApcs.front();
                thread->userApcs.pop_front();
            }
            PPCContext apcCtx = ctx;
            apcCtx.r3.u64 = apc.arg1;
            apcCtx.r4.u64 = apc.arg2;
            apcCtx.r5.u64 = apc.arg3;
            (PPC_LOOKUP_FUNC(g_base, apc.routine))(apcCtx, g_base);
        }
    }

    uint32_t ResumeThread(Thread& thread)
    {
        auto lock = DispatcherLock();
        uint32_t previous = thread.suspendCount;
        fprintf(stderr, "[thread] resume id %u previous %u\n", thread.threadId, previous);
        if (thread.suspendCount > 0 && --thread.suspendCount == 0)
            NotifyDispatcher();
        return previous;
    }

    void ExitCurrentThread(uint32_t exitCode)
    {
        throw ThreadExit{ exitCode };
    }

    uint32_t TlsAlloc()
    {
        std::lock_guard lock(g_tlsMutex);
        g_tlsUsed.resize(g_module.tlsSlotCount);
        for (uint32_t slot = 0; slot < g_tlsUsed.size(); slot++)
        {
            if (!g_tlsUsed[slot])
            {
                g_tlsUsed[slot] = true;
                return slot;
            }
        }
        return kTlsOutOfIndexes;
    }

    bool TlsFree(uint32_t slot)
    {
        std::lock_guard lock(g_tlsMutex);
        if (slot >= g_tlsUsed.size() || !g_tlsUsed[slot])
            return false;
        g_tlsUsed[slot] = false;
        return true;
    }
}
