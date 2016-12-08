//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "JsrtPch.h"
#include "jsrtHelper.h"
#include "Base/ThreadContextTlsEntry.h"
#include "Base/ThreadBoundThreadContextManager.h"
#include "Core/ConfigParser.h"
#include "Core/AtomLockGuids.h"

#ifdef DYNAMIC_PROFILE_STORAGE
#include "Language/DynamicProfileStorage.h"
#endif

#ifdef VTUNE_PROFILING
#include "Base/VTuneChakraProfile.h"
#endif

#ifdef ENABLE_JS_ETW
#include "Base/EtwTrace.h"
#endif

void ChakraBinaryAutoSystemInfoInit(AutoSystemInfo * autoSystemInfo)
{
    autoSystemInfo->buildDateHash = JsUtil::CharacterBuffer<char>::StaticGetHashCode(__DATE__, _countof(__DATE__));
    autoSystemInfo->buildTimeHash = JsUtil::CharacterBuffer<char>::StaticGetHashCode(__TIME__, _countof(__TIME__));
}

// todo: We need an interface for thread/process exit.
// At the moment we do handle thread exit for non main threads on xplat
// However, it could be nice/necessary to provide an interface to make sure
// we cover additional edge cases.

#ifdef _WIN32
#if defined(CHAKRA_STATIC_LIBRARY)
static ATOM  lockedDll = 0;
#else
extern ATOM  lockedDll = 0;
#endif
#else
#include <pthread.h>
static pthread_key_t s_threadLocalDummy;
#endif

THREAD_LOCAL bool s_threadWasEntered = false;

_NOINLINE void DISPOSE_CHAKRA_CORE_THREAD(void *_)
{
    free(_);
    ThreadBoundThreadContextManager::DestroyContextAndEntryForCurrentThread();
}

_NOINLINE bool InitializeProcess()
{
    if(s_threadWasEntered) return true;
#if !defined(_WIN32)
    pthread_key_create(&s_threadLocalDummy, DISPOSE_CHAKRA_CORE_THREAD);
#endif

// setup the cleanup
// we do not track the main thread. When it exits do the cleanup below
atexit([]() {
    ThreadBoundThreadContextManager::DestroyContextAndEntryForCurrentThread();

    JsrtRuntime::Uninitialize();

    // thread-bound entrypoint should be able to get cleanup correctly, however tlsentry
    // for current thread might be left behind if this thread was initialized.
    ThreadContextTLSEntry::CleanupThread();
    ThreadContextTLSEntry::CleanupProcess();
});

// Attention: shared library is handled under (see ChakraCore/ChakraCoreDllFunc.cpp)
// todo: consolidate similar parts from shared and static library initialization
#ifndef _WIN32
    PAL_InitializeChakraCore(0, NULL);
#endif

    HMODULE mod = GetModuleHandleW(NULL);

    AutoSystemInfo::SaveModuleFileName(mod);

#if defined(_M_IX86) && !defined(__clang__)
    // Enable SSE2 math functions in CRT if SSE2 is available
#pragma prefast(suppress:6031, "We don't require SSE2, but will use it if available")
    _set_SSE2_enable(TRUE);
#endif

    {
        CmdLineArgsParser parser;
        ConfigParser::ParseOnModuleLoad(parser, mod);
    }

#ifdef ENABLE_JS_ETW
    EtwTrace::Register();
#endif
    ValueType::Initialize();
    ThreadContext::GlobalInitialize();

    // Needed to make sure that only ChakraCore is loaded into the process
    // This is unnecessary on Linux since there aren't other flavors of
    // Chakra binaries that can be loaded into the process
#ifdef _WIN32
    char16 *engine = szChakraCoreLock;
    if (::FindAtom(szChakraLock) != 0)
    {
        AssertMsg(FALSE, "Expecting to load chakracore.dll but process already loaded chakra.dll");
        Binary_Inconsistency_fatal_error();
    }
    lockedDll = ::AddAtom(engine);
    AssertMsg(lockedDll, "Failed to lock chakracore.dll");
#endif // _WIN32

#ifdef ENABLE_BASIC_TELEMETRY
    g_TraceLoggingClient = NoCheckHeapNewStruct(TraceLoggingClient);
#endif


    return true;
}

void ChakraCoreAutoInitialize()
{

#if defined(CHAKRA_STATIC_LIBRARY) || !defined(_WIN32)
    // We do also initialize the process part here
    // This is thread safe by the standard
    // Let's hope compiler doesn't fail
    static bool _has_init = InitializeProcess();

    if (!_has_init) // do not assert this.
    {
        abort();
    }

    if (s_threadWasEntered) return;
    s_threadWasEntered = true;

#ifdef DYNAMIC_PROFILE_STORAGE
    DynamicProfileStorage::Initialize();
#endif

#ifdef HEAP_TRACK_ALLOC
    HeapAllocator::InitializeThread();
#endif

#ifndef _WIN32
    // put something into key to make sure destructor is going to be called
    pthread_setspecific(s_threadLocalDummy, malloc(1));
#endif
#endif
}

