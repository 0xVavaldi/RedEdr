#include <windows.h>
#include <evntrace.h>
#include <tdh.h>
#include <combaseapi.h>

#include <tdh.h>
#include <stdio.h>
#include <stdlib.h>
#include <evntrace.h>

#include <sddl.h>

#include "emitter.h"
#include "objcache.h"
#include "utils.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "tdh.lib")


/* NOTE: Most copied from RedEdr/etwreader.cpp */


wchar_t* SessionName = L"RedEdrPplServiceEtwTiReader";
BOOL enabled_consumer = FALSE;

BOOL seen_etwti_event = FALSE;

void enable_consumer(BOOL e) {
    log_message(L"Consumer: Enable: %d", e);
    enabled_consumer = e;
}


void PrintProperties(wchar_t *eventName, PEVENT_RECORD eventRecord) {
    DWORD bufferSize = 0;
    PTRACE_EVENT_INFO eventInfo = NULL;
    TDHSTATUS status = TdhGetEventInformation(eventRecord, 0, NULL, eventInfo, &bufferSize);
    if (status == ERROR_INSUFFICIENT_BUFFER) {
        eventInfo = (PTRACE_EVENT_INFO)malloc(bufferSize);
        status = TdhGetEventInformation(eventRecord, 0, NULL, eventInfo, &bufferSize);
    }
    if (ERROR_SUCCESS != status) {
        log_message(L"Consumer: TdhGetEventInformation failed\n");
        if (eventInfo) {
            free(eventInfo);
        }
        return;
    }

    wchar_t output [1024] = { 0 }; // Buffer to accumulate output
    swprintf(output, sizeof(output) / sizeof(output[0]),
        L"type:etw;time:%lld;pid:%lu;thread_id:%lu;event:%s;provider_name:Microsoft-Windows-Threat-Intelligence;",
        eventRecord->EventHeader.TimeStamp.QuadPart,
        eventRecord->EventHeader.ProcessId,
        eventRecord->EventHeader.ThreadId,
        eventName);

    /*if (eventInfo->ProviderNameOffset) {
        wcscat_s(output, sizeof(output) / sizeof(output[0]), (wchar_t*)((PBYTE)eventInfo + eventInfo->ProviderNameOffset));
    }
    else {
        wcscat_s(output, sizeof(output) / sizeof(output[0]), L"Unknown");
    }
    wcscat_s(output, sizeof(output) / sizeof(output[0]), L";");*/

    for (DWORD i = 0; i < eventInfo->TopLevelPropertyCount; i++) {
        PROPERTY_DATA_DESCRIPTOR dataDescriptor;
        dataDescriptor.PropertyName = (ULONGLONG)((PBYTE)eventInfo + eventInfo->EventPropertyInfoArray[i].NameOffset);
        dataDescriptor.ArrayIndex = ULONG_MAX;

        bufferSize = 0;
        status = TdhGetPropertySize(eventRecord, 0, NULL, 1, &dataDescriptor, &bufferSize);
        if (status != ERROR_SUCCESS) {
            continue;
        }

        BYTE* propertyBuffer = (BYTE*)malloc(bufferSize);
        status = TdhGetProperty(eventRecord, 0, NULL, 1, &dataDescriptor, bufferSize, propertyBuffer);
        if (status != ERROR_SUCCESS) {
            free(propertyBuffer);
            continue;
        }

        wcscat_s(output, sizeof(output) / sizeof(output[0]), (wchar_t*)((PBYTE)eventInfo + eventInfo->EventPropertyInfoArray[i].NameOffset));
        wcscat_s(output, sizeof(output) / sizeof(output[0]), L":");

        switch (eventInfo->EventPropertyInfoArray[i].nonStructType.InType) {
        case TDH_INTYPE_UINT32:
            swprintf(output + wcslen(output), 32, L"%lu;", *(PULONG)propertyBuffer);
            break;
        case TDH_INTYPE_UINT64:
            swprintf(output + wcslen(output), 32, L"%llu;", *(PULONG64)propertyBuffer);
            break;
        case TDH_INTYPE_UNICODESTRING:
            wcscat_s(output, sizeof(output) / sizeof(output[0]), (PCWSTR)propertyBuffer);
            wcscat_s(output, sizeof(output) / sizeof(output[0]), L";");
            break;
        case TDH_INTYPE_ANSISTRING: {
            size_t convertedChars = 0;
            wchar_t convertedString[256];
            mbstowcs_s(&convertedChars, convertedString, sizeof(convertedString) / sizeof(convertedString[0]), (PCSTR)propertyBuffer, bufferSize);
            wcscat_s(output, sizeof(output) / sizeof(output[0]), convertedString);
            wcscat_s(output, sizeof(output) / sizeof(output[0]), L";");
            break;
        }
        case TDH_INTYPE_POINTER:
            swprintf(output + wcslen(output), 32, L"0x%p;", *(PVOID*)propertyBuffer);
            break;
        case TDH_INTYPE_FILETIME: {
            FILETIME fileTime = *(PFILETIME)propertyBuffer;
            SYSTEMTIME stUTC, stLocal;
            FileTimeToSystemTime(&fileTime, &stUTC);
            SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
            swprintf(output + wcslen(output), 64, L"%04d/%02d/%02d %02d:%02d:%02d;",
                stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
            break;
        }

        case TDH_INTYPE_INT8:
            swprintf(output + wcslen(output), 32, L"%d;", *(PCHAR)propertyBuffer);
            break;
        case TDH_INTYPE_UINT8:
            swprintf(output + wcslen(output), 32, L"%u;", *(PUCHAR)propertyBuffer);
            break;
        case TDH_INTYPE_INT16:
            swprintf(output + wcslen(output), 32, L"%d;", *(PSHORT)propertyBuffer);
            break;
        case TDH_INTYPE_UINT16:
            swprintf(output + wcslen(output), 32, L"%u;", *(PUSHORT)propertyBuffer);
            break;
        case TDH_INTYPE_INT32:
            swprintf(output + wcslen(output), 32, L"%d;", *(PLONG)propertyBuffer);
            break;
        case TDH_INTYPE_INT64:
            swprintf(output + wcslen(output), 32, L"%lld;", *(PLONGLONG)propertyBuffer);
            break;
        case TDH_INTYPE_FLOAT:
            swprintf(output + wcslen(output), 32, L"%f;", *(PFLOAT)propertyBuffer);
            break;
        case TDH_INTYPE_DOUBLE:
            swprintf(output + wcslen(output), 32, L"%lf;", *(DOUBLE*)propertyBuffer);
            break;
        case TDH_INTYPE_BOOLEAN:
            swprintf(output + wcslen(output), 32, L"%d;", *(PBOOL)propertyBuffer);
            break;
        case TDH_INTYPE_BINARY: {
            // Print each byte in hexadecimal
            for (ULONG j = 0; j < bufferSize; j++) {
                swprintf(output + wcslen(output), 4, L"%02X", ((PBYTE)propertyBuffer)[j]);
            }
            wcscat_s(output, sizeof(output) / sizeof(output[0]), L";");
            break;
        }
        case TDH_INTYPE_GUID: {
            GUID* guid = (GUID*)propertyBuffer;
            swprintf(output + wcslen(output), 64, L"{%08x-%04x-%04x-%04x-%012x};", guid->Data1, guid->Data2, guid->Data3, *((USHORT*)guid->Data4), *((ULONG*)&guid->Data4[2]));
            break;
        }
        case TDH_INTYPE_SYSTEMTIME: {
            SYSTEMTIME* st = (SYSTEMTIME*)propertyBuffer;
            swprintf(output + wcslen(output), 64, L"%04d-%02d-%02d %02d:%02d:%02d;", st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond);
            break;
        }
        case TDH_INTYPE_HEXINT32:
            swprintf(output + wcslen(output), 32, L"0x%08X;", *(PULONG)propertyBuffer);
            break;
        case TDH_INTYPE_HEXINT64:
            swprintf(output + wcslen(output), 32, L"0x%016llX;", *(PULONG64)propertyBuffer);
            break;
        case TDH_INTYPE_SID: {
            PSID sid = (PSID)propertyBuffer;
            WCHAR sidString[256];
            if (ConvertSidToStringSid(sid, &sidString)) {
                wcscat_s(output, sizeof(output) / sizeof(output[0]), sidString);
                wcscat_s(output, sizeof(output) / sizeof(output[0]), L";");
            }
            break;
        }

        default:
            swprintf(output + wcslen(output), 32, L"%d:0x%x;",
                eventInfo->EventPropertyInfoArray[i].nonStructType.InType,
                propertyBuffer
                );

            //wcscat_s(output, sizeof(output) / sizeof(output[0]), L"(Unknown type);");
            break;
        }

        free(propertyBuffer);
    }

    if (eventInfo) {
        free(eventInfo);
    }

    // Output the accumulated string
    //wprintf(L"%s\n", output);

    SendEmitterPipe(output);
}


// Only for testing
void WINAPI EventRecordCallbackKernelProcess(PEVENT_RECORD eventRecord) {
    if (eventRecord == NULL || !enabled_consumer) {
        return;
    }
    if (!seen_etwti_event) {
        seen_etwti_event = TRUE;
        log_message("Consumer: Got a ETW-TI message, all is working");
    }
    DWORD processId = eventRecord->EventHeader.ProcessId;
    struct my_hashmap* obj = get_obj(processId);
    if (!obj->value) {
        return;
    }

    wchar_t id[32];

    switch (eventRecord->EventHeader.EventDescriptor.Id) {
    case 1:
        wcsncpy_s(id, 32, L"StartProcess", _TRUNCATE);
        break;
    case 3:
        wcsncpy_s(id, 32, L"StartThread", _TRUNCATE);
        break;
    //case 5:
    //    wcsncpy_s(id, 32, L"LoadImage", _TRUNCATE);
    //    break;
    default:
        return;
    }

    PrintProperties(id, eventRecord);
}


void WINAPI EventRecordCallbackTi(PEVENT_RECORD eventRecord) {
    if (eventRecord == NULL || !enabled_consumer) {
        return;
    }
    DWORD processId = eventRecord->EventHeader.ProcessId;
    struct my_hashmap* obj = get_obj(processId);
    if (!obj->value) {
        return;
    }

    wchar_t id[128];
    //swprintf_s(buffer, sizeof(buffer) / sizeof(buffer[0]), L"<%d>", eventRecord->EventHeader.EventDescriptor.Id);

    switch (eventRecord->EventHeader.EventDescriptor.Id) {
    case 1:
        wcsncpy_s(id, 128, L"ALLOCVM_REMOTE", _TRUNCATE);
        break;
    case 2:
        wcsncpy_s(id, 128, L"PROTECTVM_REMOTE", _TRUNCATE);
        break;
    case 3:
        wcsncpy_s(id, 128, L"MAPVIEW_REMOTE", _TRUNCATE);
        break;
    case 4:
        wcsncpy_s(id, 128, L"QUEUEUSERAPC_REMOTE", _TRUNCATE);
        break;
    case 5:
        wcsncpy_s(id, 128, L"SETTHREADCONTEXT_REMOTE", _TRUNCATE);
        break;
    case 6:
        wcsncpy_s(id, 128, L"ALLOCVM_LOCAL", _TRUNCATE);
        break;
    case 7:
        wcsncpy_s(id, 128, L"PROTECTVM_LOCAL", _TRUNCATE);
        break;
    case 8:
        wcsncpy_s(id, 128, L"MAPVIEW_LOCAL", _TRUNCATE);
        break;
    case 9:
        wcsncpy_s(id, 128, L"???", _TRUNCATE);
        break;
    case 10:
        wcsncpy_s(id, 128, L"???", _TRUNCATE);
        break;
    case 11:
        wcsncpy_s(id, 128, L"READVM_LOCAL", _TRUNCATE);
        break;
    case 12:
        wcsncpy_s(id, 128, L"WRITEVM_LOCAL", _TRUNCATE);
        break;
    case 13:
        wcsncpy_s(id, 128, L"READVM_REMOTE", _TRUNCATE);
        break;
    case 14:
        wcsncpy_s(id, 128, L"WRITEVM_REMOTE", _TRUNCATE);
        break;
    case 15:
        wcsncpy_s(id, 128, L"SUSPEND_THREAD", _TRUNCATE);
        break;
    case 16:
        wcsncpy_s(id, 128, L"RESUME_THREAD", _TRUNCATE);
        break;
    case 17:
        wcsncpy_s(id, 128, L"SUSPEND_PROCESS", _TRUNCATE);
        break;
    case 18:
        wcsncpy_s(id, 128, L"RESUME_PROCESS", _TRUNCATE);
        break;
    case 19:
        wcsncpy_s(id, 128, L"FREEZE_PROCESS", _TRUNCATE);
        break;
    case 20:
        wcsncpy_s(id, 128, L"THAW_PROCESS", _TRUNCATE);
        break;
    case 21:
        wcsncpy_s(id, 128, L"ALLOCVM_REMOTE_KERNEL_CALLER", _TRUNCATE);
        break;
    case 22:
        wcsncpy_s(id, 128, L"PROTECTVM_REMOTE_KERNEL_CALLER", _TRUNCATE);
        break;
    case 23:
        wcsncpy_s(id, 128, L"MAPVIEW_REMOTE_KERNEL_CALLER", _TRUNCATE);
        break;
    case 24:
        wcsncpy_s(id, 128, L"QUEUEUSERAPC_REMOTE_KERNEL_CALLER", _TRUNCATE);
        break;
    case 25:
        wcsncpy_s(id, 128, L"SETTHREADCONTEXT_REMOTE_KERNEL_CALLER", _TRUNCATE);
        break;
    case 26:
        wcsncpy_s(id, 128, L"ALLOCVM_LOCAL_KERNEL_CALLER", _TRUNCATE);
        break;
    case 27:
        wcsncpy_s(id, 128, L"PROTECTVM_LOCAL_KERNEL_CALLER", _TRUNCATE);
        break;
    case 28:
        wcsncpy_s(id, 128, L"MAPVIEW_LOCAL_KERNEL_CALLER", _TRUNCATE);
        break;
    case 29:
        wcsncpy_s(id, 128, L"DRIVER_OBJECT_LOAD", _TRUNCATE);
        break;
    case 30:
        wcsncpy_s(id, 128, L"DRIVER_OBJECT_UNLOAD", _TRUNCATE);
        break;
    case 31:
        wcsncpy_s(id, 128, L"DEVICE_OBJECT_LOAD", _TRUNCATE);
        break;
    case 32:
        wcsncpy_s(id, 128, L"DEVICE_OBJECT_UNLOAD", _TRUNCATE);
        break;
    default:
        wcsncpy_s(id, 128, L"???", _TRUNCATE);
        break;
    }

    PrintProperties(id, eventRecord);
}


DWORD WINAPI TraceProcessingThread(LPVOID param) {
    SetupTrace(L"{f4e1897c-bb5d-5668-f1d8-040f4d8dd344}", &EventRecordCallbackTi, L"Microsoft-Windows-Threat-Intelligence");
    
    // For testing only:
    //setup_trace(L"{22fb2cd6-0e7b-422b-a0c7-2fad1fd0e716}", &EventRecordCallbackKernelProcess, L"Microsoft-Windows-Kernel-Process");
    return 0;
}


void StartEtwtiReader() {
    TraceProcessingThread(NULL);
}


typedef void (WINAPI* EventRecordCallbackFuncPtr)(PEVENT_RECORD);

TRACEHANDLE SessionHandle;
TRACEHANDLE TraceHandle;


EVENT_TRACE_PROPERTIES* makeSessionProperties(size_t session_name_len) {
    EVENT_TRACE_PROPERTIES* sessionProperties;
    ULONG bufferSize = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + ((session_name_len + 1) * sizeof(wchar_t)));
    sessionProperties = (EVENT_TRACE_PROPERTIES*)malloc(bufferSize);
    if (sessionProperties == NULL) {
        log_message(L"Consumer error: Allocating");
        return NULL;
    }
    ZeroMemory(sessionProperties, bufferSize);
    sessionProperties->Wnode.BufferSize = bufferSize;
    sessionProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    sessionProperties->Wnode.ClientContext = 1;  // QPC clock resolution
    sessionProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    sessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return sessionProperties;
}


BOOL ShutdownEtwtiReader() {
    log_message(L"Consumer: Stopping EtwTracing");
    ULONG status;
    EVENT_TRACE_PROPERTIES* sessionProperties;

    sessionProperties = makeSessionProperties(wcslen(SessionName));
    if (SessionHandle != NULL) {
        log_message(L"Consumer: Stop Session with ControlTrace(EVENT_TRACE_CONTROL_STOP)");
        status = ControlTrace(SessionHandle, SessionName, sessionProperties, EVENT_TRACE_CONTROL_STOP);
        if (status != ERROR_SUCCESS) {
            log_message(L"Consumer: Failed to stop trace");
        }
        else {
            log_message(L"Consumer: ControlTrace stopped");
        }
        SessionHandle = NULL;
    }
    free(sessionProperties);

    if (TraceHandle != INVALID_PROCESSTRACE_HANDLE) {
        log_message(L"Consumer: CloseTrace()");

        status = CloseTrace(TraceHandle);
        if (status == ERROR_CTX_CLOSE_PENDING) {
            // The call was successful. The ProcessTrace function will stop 
            // after it has processed all real-time events in its buffers 
            // (it will not receive any new events).
            log_message(L"Consumer: CloseTrace() success but pending");
        }
        else if (status == ERROR_SUCCESS) {
            log_message(L"Consumer: CloseTrace() success");
        }
        else {
            log_message(L"Consumer: CloseTrace() failed: %d", status);
        }
        TraceHandle = INVALID_PROCESSTRACE_HANDLE;
    }

    return TRUE;
}


BOOL SetupTrace(const wchar_t* guid, EventRecordCallbackFuncPtr func, const wchar_t* info) {
    ULONG status;
    GUID providerGuid;
    SessionHandle = NULL;
    TraceHandle = INVALID_PROCESSTRACE_HANDLE;

    log_message(L"Consumer: Setup ETW-TI Reader");

    // Convert CLSID
    if (CLSIDFromString(guid, &providerGuid) != NOERROR) {
        log_message(L"Consumer: error: Invalid provider GUID format");
        return FALSE;
    }

    // StartTrace -> SessionHandle
    EVENT_TRACE_PROPERTIES* sessionProperties = makeSessionProperties(wcslen(SessionName));
    status = StartTrace(&SessionHandle, SessionName, sessionProperties);
    if (status != ERROR_SUCCESS) {
        log_message(L"Consumer: Failed to start trace: %d", status);
        free(sessionProperties);

        if (status == ERROR_ALREADY_EXISTS) {
            log_message(L"Consumer: Trace already exists");
        }

        return FALSE;
    }

    // EnableProvider
    status = EnableTraceEx2(SessionHandle,  &providerGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);
    if (status != ERROR_SUCCESS) {
        log_message(L"Consumer: Failed to enable provider: %d", status);
        if (status == ERROR_ACCESS_DENIED) {
            log_message(L"Consumer: No permission");
        }
        return FALSE;
    }

    // OpenTrace
    EVENT_TRACE_LOGFILE traceLogfile;
    ZeroMemory(&traceLogfile, sizeof(EVENT_TRACE_LOGFILE));
    traceLogfile.LoggerName = SessionName;
    traceLogfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    traceLogfile.EventRecordCallback = func;
    TraceHandle = OpenTrace(&traceLogfile);
    if (TraceHandle == INVALID_PROCESSTRACE_HANDLE) {
        log_message(L"Consumer: Failed to open trace: %d", GetLastError());
        free(sessionProperties);
        return FALSE;
    }
    free(sessionProperties);

    // Start it (blocking)
    TRACEHANDLE traceHandles[] = { TraceHandle };
    log_message(L"Consumer: ETW Listening start");
    status = ProcessTrace(traceHandles, 1, 0, 0);
    if (status != ERROR_SUCCESS) {
        log_message(L"Consumer: ProcessTrace() failed with error: %d", status);
    }

    log_message(L"Consumer: ETW Listening stopped");
    return TRUE;
}