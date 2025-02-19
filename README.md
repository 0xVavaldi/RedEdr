﻿# RedEdr

Display events from Windows to see the detection surface of your malware.

Same data as an EDR sees. 

* Find the telemetry your malware generates
* Verify your anti-EDR techniques work
* Debug and analyze malware

RedEdr will observe one process, and identify malicious patterns. 
A normal EDR will observe all processes, and identify malicious processes. 

It generates JSON files like [this](https://github.com/dobin/RedEdr/tree/master/Data)
collecting all telemetry of some C2. 


## Implemented Telemetry Consumers

* ETW
  * Microsoft-Windows-Kernel-Process
  * Microsoft-Windows-Kernel-Audit-API-Calls
  * Microsoft-Windows-Security-Auditing
    * needs SYSTEM
    * restrictions apply, configure group policy
  * And defender
    * Microsoft-Antimalware-Engine
    * Microsoft-Antimalware-RTP
    * Microsoft-Antimalware-AMFilter
    * Microsoft-Antimalware-Scan-Interface
    * Microsoft-Antimalware-Protection
* ETW-TI (Threat Intelligence) with a PPL service via ELAM driver

* Kernel Callbacks
  * PsSetCreateProcessNotifyRoutine
  * PsSetCreateThreadNotifyRoutine
  * PsSetLoadImageNotifyRoutine
  * (ObRegisterCallbacks, not used atm)

* AMSI-style ntdll.dll hooking 
  * from kernelspace (KAPC from LoadImage callback)
  * from userspace (ETW based, unreliable)

* Callstacks
  * On ntdll.dll hook invocation
 
* Loaded DLL's
  * On process create

* process information:
  * PEB (on process create)


## Installation

Use a dedicated VM for RedEdr. Tested on unlicensed (no Defender) Win10 Pro. 

Change Windows boot options to enable self-signed kernel drivers and reboot.
As admin cmd:
```
bcdedit /set testsigning on
bcdedit -debug on
```

If you use Hyper-V, uncheck "Security -> Enable Secure Boot". 

Extract release.zip into `C:\RedEdr`. **No other directories are supported.**

Start terminal as local admin.

Change into `C:\RedEdr` and run `.\RedEdr.exe`:
```
PS C:\rededr> .\RedEdr.exe
Maldev event recorder
Usage:
  RedEdr [OPTION...]

  -t, --trace arg     Process name to trace
  -e, --etw           Input: Consume ETW Events
  -g, --etwti         Input: Consume ETW-TI Events
  -m, --mplog         Input: Consume Defender mplog file
  -k, --kernel        Input: Consume kernel callback events
  -i, --inject        Input: Consume DLL injection
  -c, --dllcallstack  Input: Enable DLL injection hook callstacks
  -w, --web           Output: Web server
  -u, --hide          Output: Hide messages (performance. use with --web)
  -1, --krnload       Kernel Module: Load
  -2, --krnunload     Kernel Module: Unload
  -4, --pplstart      PPL service: load
  -5, --pplstop       PPL service: stop
  -l, --dllreader     Debug: DLL reader but no injection (for manual
                      injection tests)
  -d, --debug         Enable debugging
  -h, --help          Print usage

```

Try: `.\RedEdr.exe --all --trace otepad`, and then start notepad 
(will be `notepad.exe` on Windows 10, `Notepad.exe` on Windows 11).


## Standard Usage

RedEdr will trace all processes containing by process image name (exe path). And its children, recursively. 

Enable all consumers, and provide as web on `http://localhost:8080`, 
and disable output logging for performance:
```
PS > .\RedEdr.exe --all --web --hide --trace notepad.exe
```


### Kernel Callbacks

Kernel module callbacks. And KAPC DLL injection: 
```
PS > .\RedEdr.exe --kernel --inject --trace notepad.exe
```

This requires self-signed kernel modules to load. 


### ETW 

Only ETW, no kernel module:
```
PS > .\RedEdr.exe --etw --trace notepad.exe
```

If you want ETW Microsoft-Windows-Security-Auditing, start as SYSTEM (`psexec -i -s cmd.exe`). 
See `gpedit.msc -> Computer Configuration -> Windows Settings -> Security Settings -> Advanced Audit Policy Configuration -> System Audit Policies - Local Group Policy object`
for settings to log.


### ETWI-TI

ETW-TI requires an ELAM driver to start `RedEdrPplService`, 
and therefore requires self signed kernel driver option. 

Make a snapshot of your VM before doing this. Currently its 
not possible to remove the PPL service again. 

```
PS > .\RedEdr.exe --etwti --trace notepad.exe
```


## Example Output

See `Data/` directory:
* [Data](https://github.com/dobin/RedEdr/tree/master/Data)


## Hacking

```
  RedEdr.exe                                                                                       
┌────────────┐                    ┌─────────────────┐                                             
│            │   KERNEL_PIPE      │                 │    KERNEL_PIPE: Events (wchar)              
│            │◄───────────────────┤   Kernel Module │                                             
│ Pipe Server│                    │                 │    IOCTL: Config (MY_DRIVER_DATA):          
│            ├───────────────────►│                 │             filename                        
│            │   IOCTL            └─────────────────┘             enable                          
│            │                                                                                    
│            │                                                                                    
│            │                                                                                    
│            │                                                                                    
│            │                    ┌─────────────────┐                                             
│            │   DLL_PIPE         │                 │  DLL_PIPE: 1: Config (wchar)   RedEdr -> DLL
│ Pipe Server│◄───────────────────┤  Injected DLL   │                 "callstack:1;"              
│            │                    │                 │                                             
│            │                    │                 │           >1: Events (wchar)   RedEdr <- DLL
│            │                    └─────────────────┘                                             
│            │                                                                                    
│            │                                                                                    
│            │                                                                                    
│            │                    ┌─────────────────┐                                             
│            │   PPL_PIPE         │                 │  DLL_PIPE: Events (wchar)                   
│ Pipe Server│◄───────────────────┤  ETW-TI Service │                                             
│            │                    │  PPL            │                                             
│            │   SERVICE_PIPE     │                 │  SERVICE_PIPE: Config (wchar)               
│ Pipe Client├───────────────────►│                 │                  "start:<process name>"     
│            │                    └─────────────────┘                                             
│            │                                                                                    
│            │                    ┌─────────────────┐                                             
│            │◄───────────────────┤                 │                                             
│            │                    │  ETW            │                                             
│            │                    │                 │                                             
│            │                    │                 │                                             
│            │                    └─────────────────┘                                             
│            │                                                                                    
│            │                                                                                    
└────────────┘                                                                                    
```

* https://github.com/dobin/RedEdr/blob/master/RedEdrDriver/kcallbacks.c
* https://github.com/dobin/RedEdr/blob/master/RedEdrDll/dllmain.cpp
* https://github.com/dobin/RedEdr/blob/master/RedEdr/etwreader.cpp
* https://github.com/dobin/RedEdr/blob/master/RedEdr/dllreader.cpp
* https://github.com/dobin/RedEdr/blob/master/RedEdr/kernelreader.cpp


## Compiling 

Use VS2022. 

To compile the kernel driver: 
* Install WDK (+SDK): https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk

After compiling solution (all "Debug"), you should have: 
* C:\RedEdr\RedEdr.exe: The userspace component
* C:\RedEdr\RedEdrDriver\*: The kernel module
* C:\RedEdr\RedEdrDll.dll: The injectable DLL (amsi.dll)


## Todo

More consumers:
* Kernel ETW?
* Kernel minifilter?
* AMSI provider


## Based on

Based on MyDumbEdr
* GPLv3
* https://sensepost.com/blog/2024/sensecon-23-from-windows-drivers-to-an-almost-fully-working-edr/
* https://github.com/sensepost/mydumbedr
* patched https://github.com/dobin/mydumbedr
* which seems to use: https://github.com/CCob/SylantStrike/tree/master/SylantStrike

With KAPC injection from:
* https://github.com/0xOvid/RootkitDiaries/
* No license

To run as PPL: 
* https://github.com/pathtofile/PPLRunner/
* No license


## Libraries used

* https://github.com/jarro2783/cxxopts, MIT
* https://github.com/yhirose/cpp-httplib, MIT
* https://github.com/nlohmann/json, MIT
