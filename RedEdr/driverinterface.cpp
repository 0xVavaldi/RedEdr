#include <stdio.h>
#include <windows.h>
#include <cwchar>  // For wcstol
#include <cstdlib> // For exit()

#include "../Shared/common.h"
#include "loguru.hpp"
#include "config.h"
#include "procinfo.h"
#include "dllinjector.h"
#include "driverinterface.h"


int ioctl_enable_kernel_module(int enable, wchar_t* target) {
    if (enable) {
        LOG_F(INFO, "Send IOCTL to kernel module: Enable: %ls", target);
    }
    else {
        LOG_F(INFO, "Send IOCTL to kernel module: Disable");
    }

    HANDLE hDevice = CreateFile(L"\\\\.\\RedEdr",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        LOG_F(ERROR, "Failed to open device. Error: %d", GetLastError());
        return 0;
    }

    MY_DRIVER_DATA dataToSend = { 0 }; // all zero means all disabled by chance
    if (enable) {
        wcscpy_s(dataToSend.filename, target);
        dataToSend.flag = 1;
    }
    char buffer_incoming[128] = { 0 };
    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(hDevice,
        IOCTL_MY_IOCTL_CODE,
        (LPVOID)&dataToSend,
        (DWORD)sizeof(dataToSend),
        buffer_incoming,
        sizeof(buffer_incoming),
        &bytesReturned,
        NULL);
    if (!success) {
        LOG_F(ERROR, "DeviceIoControl failed. Error: %d", GetLastError());
        CloseHandle(hDevice);
        return 0;
    }

    LOG_F(INFO, "Received from driver: %i: %s", bytesReturned, buffer_incoming);

    CloseHandle(hDevice);
    return 1;
}


BOOL LoadDriver() {
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    LPCWSTR driverName = g_config.driverName;
    LPCWSTR driverPath = g_config.driverPath;
    BOOL ret = FALSE;

    // Open the Service Control Manager
    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) {
        LOG_F(ERROR, "OpenSCManager failed. Error: %lu\n", GetLastError());
        return FALSE;
    }

    // Create the service (driver)
    hService = CreateService(
        hSCManager,              // SCM handle
        driverName,              // Name of the service
        driverName,              // Display name
        SERVICE_ALL_ACCESS,      // Desired access
        SERVICE_KERNEL_DRIVER,   // Service type (kernel driver)
        SERVICE_DEMAND_START,    // Start type (on demand)
        SERVICE_ERROR_NORMAL,    // Error control type
        driverPath,              // Path to the driver executable
        NULL,                    // No load ordering group
        NULL,                    // No tag identifier
        NULL,                    // No dependencies
        NULL,                    // LocalSystem account
        NULL                     // No password
    );

    if (!hService) {
        if (GetLastError() == ERROR_SERVICE_EXISTS) {
            LOG_F(INFO, "Service already exists. Opening existing service...\n");
            hService = OpenService(hSCManager, driverName, SERVICE_ALL_ACCESS);
            if (!hService) {
                LOG_F(ERROR, "OpenService failed. Error: %lu\n", GetLastError());
                ret = FALSE;
                goto cleanup;
            }
        }
        else {
            LOG_F(ERROR, "CreateService failed. Error: %lu\n", GetLastError());
            ret = FALSE;
            goto cleanup;
        }
    }

    // Start the service (load the driver)
    if (!StartService(hService, 0, NULL)) {
        if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            LOG_F(ERROR, "StartService failed. Error: %lu\n", GetLastError());
            ret = FALSE;
            goto cleanup;
        }
        else {
            ret = FALSE;
            LOG_F(INFO, "Service already running.\n");
        }
    }
    else {
        ret = TRUE;
        LOG_F(INFO, "Service started successfully.\n");
    }

cleanup:
    if (hService) CloseServiceHandle(hService);
    if (hSCManager) CloseServiceHandle(hSCManager);

    return ret;
}


BOOL UnloadDriver() {
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    SERVICE_STATUS status;
    LPCWSTR driverName = g_config.driverName;
    BOOL ret = FALSE;

    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) {
        LOG_F(ERROR, "OpenSCManager failed. Error: %lu\n", GetLastError());
        return FALSE;
    }

    hService = OpenService(hSCManager, driverName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!hService) {
        LOG_F(ERROR, "OpenService failed. Error: %lu\n", GetLastError());
        ret = FALSE;
        goto cleanup;
    }

    if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
        LOG_F(INFO, "Service stopped successfully.\n");
        ret = TRUE;
    }
    else if (GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
        LOG_F(INFO, "Service is not running.\n");
        ret = TRUE;
    }
    else {
        LOG_F(ERROR, "ControlService failed. Error: %lu\n", GetLastError());
        ret = FALSE;
        goto cleanup;
    }

    if (!DeleteService(hService)) {
        LOG_F(ERROR, "DeleteService failed. Error: %lu\n", GetLastError());
        ret = FALSE;
        goto cleanup;
    }
    else {
        LOG_F(INFO, "Service deleted successfully.\n");
    }

cleanup:
    if (hService) CloseServiceHandle(hService);
    if (hSCManager) CloseServiceHandle(hSCManager);

    return ret;
}

BOOL CheckDriverStatus() {
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    SERVICE_STATUS_PROCESS status;
    DWORD bytesNeeded;
    LPCWSTR driverName = g_config.driverName;
    BOOL ret = FALSE;

    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) {
        LOG_F(ERROR, "OpenSCManager failed. Error: %lu\n", GetLastError());
        return FALSE;
    }

    hService = OpenService(hSCManager, driverName, SERVICE_QUERY_STATUS);
    if (!hService) {
        LOG_F(ERROR, "OpenService failed. Error: %lu\n", GetLastError());
        ret = FALSE;
        goto cleanup;
    }

    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
        LOG_F(ERROR, "Service status:\n");
        LOG_F(ERROR, "  PID: %lu\n", status.dwProcessId);
        LOG_F(ERROR, "  State: %lu\n", status.dwCurrentState);
        ret = TRUE;
    }
    else {
        LOG_F(ERROR, "QueryServiceStatusEx failed. Error: %lu\n", GetLastError());
        ret = FALSE;
    }

cleanup:
    if (hService) CloseServiceHandle(hService);
    if (hSCManager) CloseServiceHandle(hSCManager);

    return ret;
}