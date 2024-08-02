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

#include <psapi.h>
#include <tchar.h>
#include <stdio.h>

#pragma comment(lib, "ntdll.lib")


typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength);


BOOL GetProcessCommandLine(DWORD dwPID, std::wstring& cmdLine) {
    BOOL bSuccess = FALSE;
    
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
    if (!hProcess) {
        std::cerr << "Could not open process: " << dwPID << "  Error: " << GetLastError() << std::endl;
        return FALSE;
    }

    HMODULE hNtDll = GetModuleHandle(L"ntdll.dll");
    pNtQueryInformationProcess NtQueryInformationProcess = (pNtQueryInformationProcess)GetProcAddress(hNtDll, "NtQueryInformationProcess");
    if (!NtQueryInformationProcess) {
        std::cerr << "Could not get NtQueryInformationProcess: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return FALSE;
    }

    PROCESS_BASIC_INFORMATION pbi;
    ULONG returnLength;
    NTSTATUS status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength);
    if (status != 0) {
        std::cerr << "NtQueryInformationProcess failed: " << status << std::endl;
        CloseHandle(hProcess);
        return FALSE;
    }

    PEB peb;
    if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(peb), NULL)) {
        std::cerr << "ReadProcessMemory failed for PEB: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return FALSE;
    }

    RTL_USER_PROCESS_PARAMETERS procParams;
    if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &procParams, sizeof(procParams), NULL)) {
        std::cerr << "ReadProcessMemory failed for ProcessParameters: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return FALSE;
    }

    std::vector<wchar_t> commandLine(procParams.CommandLine.Length / sizeof(wchar_t));
    if (!ReadProcessMemory(hProcess, procParams.CommandLine.Buffer, commandLine.data(), procParams.CommandLine.Length, NULL)) {
        std::cerr << "ReadProcessMemory failed for command line: " << GetLastError() << std::endl;
    }
    else {
        cmdLine.assign(commandLine.begin(), commandLine.end());
        bSuccess = TRUE;
    }

    CloseHandle(hProcess);
    return bSuccess;
}


// Function to get the command line of a process
BOOL GetProcessCommandLine2(DWORD dwPID, LPTSTR lpCmdLine, DWORD dwSize) {
    BOOL bSuccess = FALSE;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
    if (hProcess != NULL) {
        if (GetProcessImageFileName(hProcess, lpCmdLine, dwSize)) {
            bSuccess = TRUE;
        }
        CloseHandle(hProcess);
    }
    return bSuccess;
}

// Function to get the working directory of a process
BOOL GetProcessWorkingDirectory2(DWORD dwPID, LPTSTR lpDirectory, DWORD dwSize) {
    BOOL bSuccess = FALSE;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
    if (hProcess != NULL) {
        HMODULE hMod;
        DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            if (GetModuleFileNameEx(hProcess, hMod, lpDirectory, dwSize)) {
                // Remove the executable name to get the directory
                TCHAR* p = _tcsrchr(lpDirectory, '\\');
                if (p) {
                    *p = '\0';
                    bSuccess = TRUE;
                }
            }
        }
        CloseHandle(hProcess);
    }
    return bSuccess;
}