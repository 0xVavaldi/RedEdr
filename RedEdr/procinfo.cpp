#include <stdio.h>
#include <windows.h>
#include <dbghelp.h>
#include <wintrust.h>
#include <Softpub.h>
#include <wincrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <winternl.h>
#include <cwchar>
#include <wchar.h>

#include <psapi.h>
#include <tchar.h>
#include <stdio.h>
#include <vector>
#include <winternl.h>

#include "loguru.hpp"
#include "config.h"
#include "procinfo.h"
#include "cache.h"


#pragma comment(lib, "ntdll.lib")


typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength);


// Returns: Process PEB ProcessParameters CommandLine
BOOL GetProcessCommandLine_Peb(Process *process, HANDLE hProcess) {
    BOOL bSuccess = FALSE;
    std::wstring commandLinePeb = L"";
    
    HMODULE hNtDll = GetModuleHandle(L"ntdll.dll");
    pNtQueryInformationProcess NtQueryInformationProcess = (pNtQueryInformationProcess)GetProcAddress(hNtDll, "NtQueryInformationProcess");
    if (!NtQueryInformationProcess) {
        //LOG_F(ERROR, "Error: Could not get NtQueryInformationProcess for %d, error: %d", process->id, GetLastError());
        return FALSE;
    }

    PROCESS_BASIC_INFORMATION pbi;
    ULONG returnLength;
    NTSTATUS status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength);
    if (status != 0) {
        //LOG_F(ERROR, "Error: Could not NtQueryInformationProcess for %d, error: %d", status, GetLastError());
        return FALSE;
    }

    PEB peb;
    if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(peb), NULL)) {
        // this is the first read, and may fail. 
        // dont spam log messages
        //LOG_F(DEBUG, "Error: Could not ReadProcessMemory1 for %d, error: %d", process->id, GetLastError());
        return FALSE;
    }

    RTL_USER_PROCESS_PARAMETERS procParams;
    if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &procParams, sizeof(procParams), NULL)) {
        //LOG_F(ERROR, "Error: Could not ReadProcessMemory2 for %d, error: %d", process->id, GetLastError());
        return FALSE;
    }

    std::vector<wchar_t> commandLine(procParams.CommandLine.Length / sizeof(wchar_t));
    if (!ReadProcessMemory(hProcess, procParams.CommandLine.Buffer, commandLine.data(), procParams.CommandLine.Length, NULL)) {
        //LOG_F(ERROR, "Error: Could not ReadProcessMemory3 for %d, error: %d", process->id, GetLastError());
    }
    else {
        process->commandline.assign(commandLine.begin(), commandLine.end());
        bSuccess = TRUE;
    }

    return bSuccess;
}


// Returns: GetProcessImageFileName 
BOOL GetProcessImagePath_ProcessImage(Process *process, HANDLE hProcess) {
    wchar_t _str[MAX_PATH] = { 0 };
    LPWSTR str = _str;  // LPWSTR is *wchar_t

    if (GetProcessImageFileName(hProcess, str, MAX_PATH)) {
        process->image_path = std::wstring(str);
        return TRUE;
    }
    else {
        process->image_path = std::wstring(L"unknown");
        return FALSE;
    }
}


// Function to extract the working directory from the environment block
std::wstring GetWorkingDirectoryFromEnvironmentBlock(const std::vector<wchar_t>& environmentBlock) {
    const wchar_t* env = environmentBlock.data();
    while (*env) {
        std::wstring envVar(env);
        if (envVar.find(L"CD=") == 0) {
            return envVar.substr(3); // Extract the directory path after "CD="
        }
        env += wcslen(env) + 1; // Move to the next environment variable
    }
    return L"";
}

// Function to get the working directory of a process
BOOL GetProcessWorkingDirectory(Process* process, HANDLE hProcess) {
    return TRUE;
}


BOOL GetProcessParentPid(Process *process, HANDLE hProcess) {
    PROCESS_BASIC_INFORMATION pbi;
    ULONG returnLength;
    HMODULE hNtDll = GetModuleHandle(L"ntdll.dll");
    pNtQueryInformationProcess NtQueryInformationProcess =
        (pNtQueryInformationProcess)GetProcAddress(hNtDll, "NtQueryInformationProcess");

    if (NtQueryInformationProcess == nullptr) {
        LOG_F(ERROR, "Failed to get NtQueryInformationProcess address");
        return FALSE;
    }

    NTSTATUS status = NtQueryInformationProcess(
        hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength);
    if (status != 0) {
        LOG_F(ERROR, "NtQueryInformationProcess failed: %d", status);
        return FALSE;
    }

    DWORD parentPid = (DWORD)pbi.Reserved3; // InheritedFromUniqueProcessId lol
    process->parent_pid = parentPid;

    return TRUE;
}


Process* MakeProcess(DWORD pid) {
    Process* process = new Process(pid);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        //LOG_F(WARNING, "Could not open process pid: %lu error %lu", pid, GetLastError());
        return process;
    }

    // Checks
    // name: vmmem, vmmemWSL

    // ppid
    if (!GetProcessParentPid(process, hProcess)) {
        LOG_F(WARNING, "GetProcessParentPid error: %lu: %ls", pid, process->image_path.c_str());
        CloseHandle(hProcess);
        return process;
    }

    // Process Image (ProcessImage)
    if (!GetProcessImagePath_ProcessImage(process, hProcess)) {
        LOG_F(WARNING, "GetProcessImagePath_ProcessImage error: %lu: %ls", pid, process->image_path.c_str());
        CloseHandle(hProcess);
        return process;
    }

    // Command Line (PEB)
    if (!GetProcessCommandLine_Peb(process, hProcess)) {
        //LOG_F(WARNING, "GetProcessCommandLine_Peb error: %lu: %ls", pid, process->image_path.c_str());
        process->commandline.assign(L"<unknown>");
        CloseHandle(hProcess);
        return process;
    }

    // Working dir (broken?)
    if (!GetProcessWorkingDirectory(process, hProcess)) {
        LOG_F(WARNING, "GetProcessWorkingDirectory error: %lu: %ls", pid, process->image_path.c_str());
        CloseHandle(hProcess);
        return process;
    }

    // CHECK: Observe
    BOOL observe = FALSE;
    //if (wcsstr(process->image_path.c_str(), (LPCWSTR)g_config.targetExeName)) {
    if (process->image_path.find(g_config.targetExeName) != std::wstring::npos) {
        // CHECK: Process name
        LOG_F(INFO, "Observe CMD: %d %ls", pid, process->image_path.c_str());
        observe = TRUE;
    } else if (g_cache.containsObject(process->parent_pid)) { // dont recursively resolve all
        // CHECK: Parent observed?
        if (g_cache.getObject(process->parent_pid)->doObserve()) {
            LOG_F(INFO, "Observe PID: %d (because PPID %d): %ls", pid, process->parent_pid, process->image_path.c_str());
            observe = TRUE;
        }
    }
    else {
        //LOG_F(INFO, "Dont observe: %ls because %ls", process->image_path.c_str(), g_config.targetExeName);
    }
    process->observe = observe;

    CloseHandle(hProcess);
    return process;
}
