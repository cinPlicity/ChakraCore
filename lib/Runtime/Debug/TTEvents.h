
//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once

#if ENABLE_TTD

//Ideally we want this to be a multiple of 8 when added to the tag sizes in the EventLogEntry struct.
//It should also be of a size that allows us to inline the event data for the most common events without being too wasteful on other events.
#define EVENT_INLINE_DATA_BYTE_COUNT 36

namespace TTD
{
    //An exception class for controlled aborts from the runtime to the toplevel TTD control loop
    class TTDebuggerAbortException
    {
    private:
        //An integer code to describe the reason for the abort -- 0 invalid, 1 end of log, 2 request etime move, 3 uncaught exception (propagate to top-level)
        const uint32 m_abortCode;

        //An optional target event time -- intent is interpreted based on the abort code
        const int64 m_optEventTime;

        //An optional -- and static string message to include
        const LPCWSTR m_staticAbortMessage;

        TTDebuggerAbortException(uint32 abortCode, int64 optEventTime, LPCWSTR staticAbortMessage);

    public:
        ~TTDebuggerAbortException();

        static TTDebuggerAbortException CreateAbortEndOfLog(LPCWSTR staticMessage);
        static TTDebuggerAbortException CreateTopLevelAbortRequest(int64 targetEventTime, LPCWSTR staticMessage);
        static TTDebuggerAbortException CreateUncaughtExceptionAbortRequest(int64 targetEventTime, LPCWSTR staticMessage);

        bool IsEndOfLog() const;
        bool IsEventTimeMove() const;
        bool IsTopLevelException() const;

        int64 GetTargetEventTime() const;

        LPCWSTR GetStaticAbortMessage() const;
    };

    //A struct for tracking time events in a single method
    struct SingleCallCounter
    {
        Js::FunctionBody* Function;

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        LPCWSTR Name; //only added for debugging can get rid of later.
#endif

        uint64 EventTime; //The event time when the function was called
        uint64 FunctionTime; //The function time when the function was called
        uint64 LoopTime; //The current loop taken time for the function

#if ENABLE_TTD_STACK_STMTS
        int32 LastStatementIndex; //The previously executed statement
        uint64 LastStatementLoopTime; //The previously executed statement

        int32 CurrentStatementIndex; //The currently executing statement
        uint64 CurrentStatementLoopTime; //The currently executing statement

                                         //bytecode range of the current stmt
        uint32 CurrentStatementBytecodeMin;
        uint32 CurrentStatementBytecodeMax;
#endif
    };

    //A class to represent a source location
    class TTDebuggerSourceLocation
    {
    private:
        //The time aware parts of this location
        int64 m_etime;  //-1 indicates an INVALID location
        int64 m_ftime;  //-1 indicates any ftime is OK
        int64 m_ltime;  //-1 indicates any ltime is OK

        //The document
        wchar* m_sourceFile; //temp use until we make docid stable
        uint32 m_docid;

        //The position of the function in the document
        uint32 m_functionLine;
        uint32 m_functionColumn;

        //The location in the fnuction
        uint32 m_line;
        uint32 m_column;

    public:
        TTDebuggerSourceLocation();
        TTDebuggerSourceLocation(const TTDebuggerSourceLocation& other);
        ~TTDebuggerSourceLocation();

        bool HasValue() const;
        void Clear();
        void SetLocation(const TTDebuggerSourceLocation& other);
        void SetLocation(const SingleCallCounter& callFrame);
        void SetLocation(int64 etime, int64 ftime, int64 ltime, Js::FunctionBody* body, ULONG line, LONG column);

        int64 GetRootEventTime() const;
        int64 GetFunctionTime() const;
        int64 GetLoopTime() const;

        Js::FunctionBody* ResolveAssociatedSourceInfo(Js::ScriptContext* ctx);
        uint32 GetLine() const;
        uint32 GetColumn() const;
    };

    //////////////////

    namespace NSLogEvents
    {
        //An enumeration of the event kinds in the system
        enum class EventKind : uint32
        {
            Invalid = 0x0,
            //Tags for internal engine events
            SnapshotTag,
            TopLevelCodeTag,
            TelemetryLogTag,
            DoubleTag,
            StringTag,
            RandomSeedTag,
            PropertyEnumTag,
            SymbolCreationTag,
            ExternalCallTag,
            //JsRTActionTag is a marker for where the JsRT actions begin
            JsRTActionTag,

#if !INT32VAR
            CreateIntegerActionTag,
#endif
            CreateNumberActionTag,
            CreateBooleanActionTag,
            CreateStringActionTag,
            CreateSymbolActionTag,

            VarConvertToNumberActionTag,
            VarConvertToBooleanActionTag,
            VarConvertToStringActionTag,
            VarConvertToObjectActionTag,

            AddRootRefActionTag,
            RemoveRootRefActionTag,
            LocalRootClearActionTag,

            AllocateObjectActionTag,
            AllocateExternalObjectActionTag,
            AllocateArrayActionTag,
            AllocateArrayBufferActionTag,
            AllocateFunctionActionTag,
            GetAndClearExceptionActionTag,
            GetPropertyActionTag,
            GetIndexActionTag,
            GetOwnPropertyInfoActionTag,
            GetOwnPropertiesInfoActionTag,
            DefinePropertyActionTag,
            DeletePropertyActionTag,
            SetPrototypeActionTag,
            SetPropertyActionTag,
            SetIndexActionTag,
            GetTypedArrayInfoActionTag,
            ConstructCallActionTag,
            CallbackOpActionTag,
            CodeParseActionTag,
            CallExistingFunctionActionTag,
            Limit
        };

        typedef void(*fPtr_EventLogActionEntryInfoExecute)(const EventLogEntry* evt, Js::ScriptContext* ctx);

        typedef void(*fPtr_EventLogEntryInfoUnload)(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        typedef void(*fPtr_EventLogEntryInfoEmit)(const EventLogEntry* evt, FileWriter* writer, LPCWSTR uri, ThreadContext* threadContext);
        typedef void(*fPtr_EventLogEntryInfoParse)(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //A base struct for our event log entries -- we will use the kind tags as v-table values 
        struct EventLogEntry
        {
            byte EventData[EVENT_INLINE_DATA_BYTE_COUNT];

            //The kind of the event
            EventKind EventKind;

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
            //The event time for this event
            int64 EventTimeStamp;

            //The time at which this event occoured
            double EventBeginTime;
            double EventEndTime;
#endif
        };

        template <typename T, EventKind tag>
        const T* GetInlineEventDataAs(const EventLogEntry* evt)
        {
            static_assert(sizeof(T) < EVENT_INLINE_DATA_BYTE_COUNT, "Data is too large for inline representation!!!");
            AssertMsg(evt->EventKind == tag, "Bad tag match!");

            return static_cast<T*>(evt->EventData);
        }

        template <typename T, EventKind tag>
        T* GetInlineEventDataAs(EventLogEntry* evt)
        {
            static_assert(sizeof(T) < EVENT_INLINE_DATA_BYTE_COUNT, "Data is too large for inline representation!!!");
            AssertMsg(evt->EventKind == tag, "Bad tag match!");

            return static_cast<T*>(event->EventData);
        }

        //Helpers for initializing, emitting and parsing the basic event data
        void EventLogEntry_Initialize(EventLogEntry* evt, EventKind tag, int64 etime, Js::HiResTimer& timer);
        void EventLogEntry_Done(EventLogEntry* evt, Js::HiResTimer& timer);
        void EventLogEntry_Emit(const EventLogEntry* evt, fPtr_EventLogEntryInfoEmit emitFP, FileWriter* writer, LPCWSTR uri, ThreadContext* threadContext, NSTokens::Separator separator);
        void EventLogEntry_Parse(EventLogEntry* evt, fPtr_EventLogEntryInfoParse parseFP, bool readSeperator, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //////////////////

        //A struct that represents snapshot events
        struct SnapshotEventLogEntry
        {
            //The timestamp we should restore to 
            int64 RestoreTimestamp;

            //The snapshot (we many persist this to disk and inflate back in later)
            SnapShot* Snap;
        };

        void SnapshotEventLogEntry_UnloadEventMemory(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        void SnapshotEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void SnapshotEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        void SnapshotEventLogEntry_EnsureSnapshotDeserialized(EventLogEntry* evt, LPCWSTR uri, ThreadContext* threadContext);
        void SnapshotEventLogEntry_UnloadSnapshot(EventLogEntry* evt);

        //////////////////

        //A struct that represents a top level code load event
        struct CodeLoadEventLogEntry
        {
            //The code counter id for the TopLevelFunctionBodyInfo
            uint64 BodyCounterId;
        };

        void CodeLoadEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void CodeLoadEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //A struct that represents telemetry events from the user code
        struct TelemetryEventLogEntry
        {
            //A string that contains all of the info that is logged
            TTString InfoString;

            //Do we want to print the msg or just record it internally
            bool DoPrint;
        };

        void TelemetryEventLogEntry_UnloadEventMemory(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        void TelemetryEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void TelemetryEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //////////////////

        //A struct that represents the generation of random seeds
        struct RandomSeedEventLogEntry
        {
            //The values associated with the event
            uint64 Seed0;
            uint64 Seed1;
        };

        void RandomSeedEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void RandomSeedEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //A struct that represents a simple event that needs a double value (e.g. date values)
        struct DoubleEventLogEntry
        {
            //The value associated with the event
            double DoubleValue;
        };

        void DoubleEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void DoubleEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //A struct that represents a simple event that needs a string value (e.g. date values)
        struct StringValueEventLogEntry
        {
            //The value associated with the event
            TTString StringValue;
        };

        void StringValueEventLogEntry_UnloadEventMemory(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        void StringValueEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void StringValueEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //////////////////

        //A struct that represents a single enumeration step for properties on a dynamic object
        struct PropertyEnumStepEventLogEntry
        {
            //The return code, property id, and attributes returned
            BOOL ReturnCode;
            Js::PropertyId Pid;
            Js::PropertyAttributes Attributes;

            //Optional property name string (may need to actually use later if pid can be Constants::NoProperty)
            //Always set if if doing extra diagnostics otherwise only as needed
            TTString PropertyString;
        };

        void PropertyEnumStepEventLogEntry_UnloadEventMemory(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        void PropertyEnumStepEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void PropertyEnumStepEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //////////////////

        //A struct that represents the creation of a symbol (which we need to make sure gets the correct property id)
        struct SymbolCreationEventLogEntry
        {
            //The property id of the created symbol
            Js::PropertyId Pid;
        };

        void SymbolCreationEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void SymbolCreationEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);

        //////////////////

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
    //A struct containing additional information on the external call
        struct ExternalCallEventLogEntryDiagInfo
        {
            //the function name for the function that is invoked
            TTString FunctionName;

            //The last event time that is nested in this external call
            int64 LastNestedEvent;
        };
#endif

        //A struct for logging calls from Chakra to an external function (e.g., record start of external execution and later any argument information)
        struct ExternalCallEventLogEntry
        {
            //The root nesting depth
            int32 RootNestingDepth;

            uint32 ArgCount;
            TTDVar* ArgArray;

            //The return value of the external call
            TTDVar ReturnValue;

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
            ExternalCallEventLogEntryDiagInfo* DiagInfo;
#endif

            //
            //TODO: later we should record more detail on the script exception for inflation if needed
            //
            bool HasScriptException;
            bool HasTerminiatingException;
        };

        void ExternalCallEventLogEntry_ProcessDiagInfo(EventLogEntry* evt, int64 lastNestedEvent, Js::JavascriptFunction* function, UnlinkableSlabAllocator& alloc);
        void ExternalCallEventLogEntry_ProcessArgs(EventLogEntry* evt, int32 rootDepth, uint32 argc, Js::Var* argv, UnlinkableSlabAllocator& alloc);
        void ExternalCallEventLogEntry_ProcessReturn(EventLogEntry* evt, Js::Var res, bool hasScriptException, bool hasTerminiatingException);

        void ExternalCallEventLogEntry_UnloadEventMemory(EventLogEntry* evt, UnlinkableSlabAllocator& alloc);
        void ExternalCallEventLogEntry_Emit(const EventLogEntry* evt, LPCWSTR uri, FileWriter* writer, ThreadContext* threadContext);
        void ExternalCallEventLogEntry_Parse(EventLogEntry* evt, ThreadContext* threadContext, FileReader* reader, UnlinkableSlabAllocator& alloc);
    }
}

#endif
