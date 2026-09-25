/*
 * rds-session.exe
 * Native, language-independent Windows session collector for Zabbix/Grafana.
 *
 * Output (UTF-8 JSON):
 * [{"id":"2","session_uid":"2-1790348520","user":"DOMAIN\\\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
 *
 * Uses only documented Windows APIs from kernel32.dll and wtsapi32.dll.
 * No PowerShell, .NET, runtime installation, temporary DLLs, or Add-Type.
 */

#define NULL ((void*)0)
#define TRUE 1
#define FALSE 0

#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define CP_UTF8 65001U

#define WINSTATIONNAME_LENGTH 32
#define DOMAIN_LENGTH 17
#define USERNAME_LENGTH 20

/* WTS_INFO_CLASS values used by this program. */
#define WTSUserName 5
#define WTSWinStationName 6
#define WTSDomainName 7
#define WTSSessionInfo 24

typedef void* HANDLE;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned short WCHAR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWCH;
typedef const char* LPCCH;
typedef char* LPSTR;
typedef long long LONGLONG;

typedef enum _WTS_CONNECTSTATE_CLASS {
    WTSActive = 0,
    WTSConnected = 1,
    WTSConnectQuery = 2,
    WTSShadow = 3,
    WTSDisconnected = 4,
    WTSIdle = 5,
    WTSListen = 6,
    WTSReset = 7,
    WTSDown = 8,
    WTSInit = 9
} WTS_CONNECTSTATE_CLASS;

typedef struct _WTS_SESSION_INFOW {
    DWORD SessionId;
    LPWSTR pWinStationName;
    WTS_CONNECTSTATE_CLASS State;
} WTS_SESSION_INFOW;

typedef struct _WTSINFOW {
    WTS_CONNECTSTATE_CLASS State;
    DWORD SessionId;
    DWORD IncomingBytes;
    DWORD OutgoingBytes;
    DWORD IncomingFrames;
    DWORD OutgoingFrames;
    DWORD IncomingCompressedBytes;
    DWORD OutgoingCompressedBytes;
    WCHAR WinStationName[WINSTATIONNAME_LENGTH];
    WCHAR Domain[DOMAIN_LENGTH];
    WCHAR UserName[USERNAME_LENGTH + 1];
    LONGLONG ConnectTime;
    LONGLONG DisconnectTime;
    LONGLONG LastInputTime;
    LONGLONG LogonTime;
    LONGLONG CurrentTime;
} WTSINFOW;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SYSTEMTIME {
    unsigned short wYear;
    unsigned short wMonth;
    unsigned short wDayOfWeek;
    unsigned short wDay;
    unsigned short wHour;
    unsigned short wMinute;
    unsigned short wSecond;
    unsigned short wMilliseconds;
} SYSTEMTIME;

/* Ensure our ABI matches the x64 Windows SDK structure layouts. */
typedef char assert_wts_session_info_size[(sizeof(WTS_SESSION_INFOW) == 24) ? 1 : -1];
typedef char assert_wtsinfo_size[(sizeof(WTSINFOW) == 216) ? 1 : -1];

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, DWORD* lpNumberOfBytesWritten, LPVOID lpOverlapped);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
__declspec(dllimport) void __stdcall ExitProcess(unsigned int uExitCode);
__declspec(dllimport) BOOL __stdcall FileTimeToSystemTime(const FILETIME* lpFileTime, SYSTEMTIME* lpSystemTime);
__declspec(dllimport) BOOL __stdcall SystemTimeToTzSpecificLocalTime(const void* lpTimeZoneInformation, const SYSTEMTIME* lpUniversalTime, SYSTEMTIME* lpLocalTime);
__declspec(dllimport) BOOL __stdcall SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, FILETIME* lpFileTime);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, BOOL* lpUsedDefaultChar);

__declspec(dllimport) BOOL __stdcall WTSEnumerateSessionsW(HANDLE hServer, DWORD Reserved, DWORD Version, WTS_SESSION_INFOW** ppSessionInfo, DWORD* pCount);
__declspec(dllimport) BOOL __stdcall WTSQuerySessionInformationW(HANDLE hServer, DWORD SessionId, int WTSInfoClass, LPWSTR* ppBuffer, DWORD* pBytesReturned);
__declspec(dllimport) void __stdcall WTSFreeMemory(LPVOID pMemory);

#define OUTPUT_CAPACITY 262144U
#define UTF8_TEMP_CAPACITY 4096

#define EXIT_OK 0U
#define EXIT_ENUMERATION_FAILED 2U
#define EXIT_OUTPUT_OVERFLOW 3U
#define EXIT_WRITE_FAILED 4U

static char g_output[OUTPUT_CAPACITY];
static DWORD g_output_len = 0;
static BOOL g_output_overflow = FALSE;
static char g_utf8_temp[UTF8_TEMP_CAPACITY];

static DWORD ascii_len(const char* s)
{
    DWORD n = 0;
    if (!s) return 0;
    while (s[n] != 0) ++n;
    return n;
}

static BOOL write_all(HANDLE handle, const char* buffer, DWORD length)
{
    DWORD offset = 0;

    if (handle == NULL || buffer == NULL) return FALSE;

    while (offset < length) {
        DWORD written = 0;
        if (!WriteFile(handle, buffer + offset, length - offset, &written, NULL)) {
            return FALSE;
        }
        if (written == 0) {
            return FALSE;
        }
        offset += written;
    }

    return TRUE;
}

static void write_error_with_code(const char* message, DWORD code)
{
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    char digits[16];
    int n = 0;

    if (stderr_handle == NULL) return;

    write_all(stderr_handle, "rds-session: ", 13);
    write_all(stderr_handle, message, ascii_len(message));

    if (code != 0) {
        write_all(stderr_handle, " (Win32 error ", 14);
        do {
            digits[n++] = (char)('0' + (code % 10U));
            code /= 10U;
        } while (code > 0 && n < (int)sizeof(digits));
        while (n > 0) {
            char c = digits[--n];
            write_all(stderr_handle, &c, 1);
        }
        write_all(stderr_handle, ")", 1);
    }

    write_all(stderr_handle, "\n", 1);
}

static void out_char(char c)
{
    if (g_output_overflow) return;

    if (g_output_len < OUTPUT_CAPACITY) {
        g_output[g_output_len++] = c;
    } else {
        g_output_overflow = TRUE;
    }
}

static void out_ascii(const char* s)
{
    if (!s) return;
    while (*s) {
        out_char(*s++);
    }
}

static void out_u64(unsigned long long value)
{
    char digits[32];
    int n = 0;
    if (value == 0) {
        out_char('0');
        return;
    }
    while (value > 0 && n < (int)sizeof(digits)) {
        digits[n++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (n > 0) {
        out_char(digits[--n]);
    }
}

static void out_2(unsigned int v)
{
    out_char((char)('0' + ((v / 10U) % 10U)));
    out_char((char)('0' + (v % 10U)));
}

static void out_4(unsigned int v)
{
    out_char((char)('0' + ((v / 1000U) % 10U)));
    out_char((char)('0' + ((v / 100U) % 10U)));
    out_char((char)('0' + ((v / 10U) % 10U)));
    out_char((char)('0' + (v % 10U)));
}

static int wide_len(const WCHAR* s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n] != 0 && n < 32767) ++n;
    return n;
}

static void out_json_escaped_utf8_bytes(const char* s, int len)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': out_ascii("\\\""); break;
            case '\\': out_ascii("\\\\"); break;
            case '\b': out_ascii("\\b"); break;
            case '\f': out_ascii("\\f"); break;
            case '\n': out_ascii("\\n"); break;
            case '\r': out_ascii("\\r"); break;
            case '\t': out_ascii("\\t"); break;
            default:
                if (c < 0x20U) {
                    out_ascii("\\u00");
                    out_char(hex[(c >> 4) & 0x0F]);
                    out_char(hex[c & 0x0F]);
                } else {
                    out_char((char)c);
                }
                break;
        }
    }
}

static void out_json_wide(const WCHAR* s)
{
    int wlen = wide_len(s);
    int bytes;
    if (wlen <= 0) return;

    bytes = WideCharToMultiByte(CP_UTF8, 0, s, wlen, g_utf8_temp, UTF8_TEMP_CAPACITY, NULL, NULL);
    if (bytes > 0) {
        out_json_escaped_utf8_bytes(g_utf8_temp, bytes);
    }
}

static const char* state_name(WTS_CONNECTSTATE_CLASS state)
{
    switch (state) {
        case WTSActive: return "Active";
        case WTSConnected: return "Connected";
        case WTSConnectQuery: return "ConnectQuery";
        case WTSShadow: return "Shadow";
        case WTSDisconnected: return "Disconnected";
        case WTSIdle: return "Idle";
        case WTSListen: return "Listen";
        case WTSReset: return "Reset";
        case WTSDown: return "Down";
        case WTSInit: return "Init";
        default: return "Unknown";
    }
}

static void filetime_i64_to_filetime(LONGLONG value, FILETIME* ft)
{
    unsigned long long u = (unsigned long long)value;
    ft->dwLowDateTime = (DWORD)(u & 0xFFFFFFFFULL);
    ft->dwHighDateTime = (DWORD)((u >> 32) & 0xFFFFFFFFULL);
}

static unsigned long long filetime_to_u64(const FILETIME* ft)
{
    return ((unsigned long long)ft->dwHighDateTime << 32) | (unsigned long long)ft->dwLowDateTime;
}

static unsigned long long filetime_to_unix_seconds(LONGLONG value)
{
    const unsigned long long windows_to_unix_epoch = 116444736000000000ULL;
    unsigned long long u;

    if (value <= 0) return 0ULL;

    u = (unsigned long long)value;
    if (u <= windows_to_unix_epoch) return 0ULL;

    return (u - windows_to_unix_epoch) / 10000000ULL;
}

static void out_logon_time_local(LONGLONG value)
{
    FILETIME ft_utc;
    FILETIME ft_utc_sys;
    FILETIME ft_local_sys;
    SYSTEMTIME st_utc;
    SYSTEMTIME st_local;
    long long offset_seconds = 0;
    unsigned long long u_utc;
    unsigned long long u_local;
    unsigned long long offset_abs;

    if (value <= 0) return;
    filetime_i64_to_filetime(value, &ft_utc);
    if (!FileTimeToSystemTime(&ft_utc, &st_utc)) return;
    if (!SystemTimeToTzSpecificLocalTime(NULL, &st_utc, &st_local)) return;

    if (SystemTimeToFileTime(&st_utc, &ft_utc_sys) && SystemTimeToFileTime(&st_local, &ft_local_sys)) {
        u_utc = filetime_to_u64(&ft_utc_sys);
        u_local = filetime_to_u64(&ft_local_sys);
        if (u_local >= u_utc) {
            offset_seconds = (long long)((u_local - u_utc) / 10000000ULL);
        } else {
            offset_seconds = -(long long)((u_utc - u_local) / 10000000ULL);
        }
    }

    out_4(st_local.wYear);
    out_char('-');
    out_2(st_local.wMonth);
    out_char('-');
    out_2(st_local.wDay);
    out_char(' ');
    out_2(st_local.wHour);
    out_char(':');
    out_2(st_local.wMinute);
    out_char(':');
    out_2(st_local.wSecond);
    out_char(' ');

    if (offset_seconds < 0) {
        out_char('-');
        offset_abs = (unsigned long long)(-offset_seconds);
    } else {
        out_char('+');
        offset_abs = (unsigned long long)offset_seconds;
    }
    out_2((unsigned int)(offset_abs / 3600ULL));
    out_char(':');
    out_2((unsigned int)((offset_abs % 3600ULL) / 60ULL));
}

static unsigned long long get_idle_seconds(const WTSINFOW* info)
{
    unsigned long long current;
    unsigned long long last;
    if (!info || info->CurrentTime <= 0 || info->LastInputTime <= 0) return 0ULL;
    current = (unsigned long long)info->CurrentTime;
    last = (unsigned long long)info->LastInputTime;
    if (current <= last) return 0ULL;
    return (current - last) / 10000000ULL;
}

static BOOL query_string(DWORD session_id, int info_class, LPWSTR* buffer)
{
    DWORD bytes = 0;
    *buffer = NULL;
    if (!WTSQuerySessionInformationW(NULL, session_id, info_class, buffer, &bytes)) {
        *buffer = NULL;
        return FALSE;
    }
    return TRUE;
}

static BOOL query_info(DWORD session_id, WTSINFOW** info)
{
    DWORD bytes = 0;
    LPWSTR raw = NULL;
    *info = NULL;
    if (!WTSQuerySessionInformationW(NULL, session_id, WTSSessionInfo, &raw, &bytes)) {
        return FALSE;
    }
    if (raw == NULL || bytes < (DWORD)sizeof(WTSINFOW)) {
        if (raw) WTSFreeMemory(raw);
        return FALSE;
    }
    *info = (WTSINFOW*)raw;
    return TRUE;
}

static void write_session_json(
    DWORD id,
    const WCHAR* domain,
    const WCHAR* user,
    const WCHAR* session_name,
    WTS_CONNECTSTATE_CLASS state,
    unsigned long long idle_seconds,
    LONGLONG logon_time)
{
    unsigned long long logon_epoch = filetime_to_unix_seconds(logon_time);

    out_ascii("{\"id\":\"");
    out_u64((unsigned long long)id);
    out_ascii("\",\"session_uid\":\"");
    out_u64((unsigned long long)id);
    out_char('-');
    out_u64(logon_epoch);
    out_ascii("\",\"user\":\"");

    if (domain && domain[0] != 0) {
        out_json_wide(domain);
        /* JSON needs two backslashes to represent DOMAIN\\user. */
        out_ascii("\\\\");
    }
    out_json_wide(user);

    out_ascii("\",\"session\":\"");
    if (session_name && session_name[0] != 0) {
        out_json_wide(session_name);
    } else {
        out_char('-');
    }

    out_ascii("\",\"state\":\"");
    out_ascii(state_name(state));
    out_ascii("\",\"idle_seconds\":");
    out_u64(idle_seconds);
    out_ascii(",\"logon_time\":\"");
    out_logon_time_local(logon_time);
    out_ascii("\"}");
}

static DWORD collect_sessions(DWORD* win32_error)
{
    WTS_SESSION_INFOW* sessions = NULL;
    DWORD count = 0;
    DWORD i;
    int first = 1;

    *win32_error = 0;

    if (!WTSEnumerateSessionsW(NULL, 0, 1, &sessions, &count)) {
        *win32_error = GetLastError();
        return EXIT_ENUMERATION_FAILED;
    }

    out_char('[');

    if (sessions != NULL) {
        for (i = 0; i < count; ++i) {
            LPWSTR user = NULL;
            LPWSTR domain = NULL;
            LPWSTR session_name = NULL;
            WTSINFOW* info = NULL;
            WTS_CONNECTSTATE_CLASS state = sessions[i].State;
            unsigned long long idle_seconds = 0ULL;
            LONGLONG logon_time = 0;

            if (!query_string(sessions[i].SessionId, WTSUserName, &user) || user == NULL || user[0] == 0) {
                if (user) WTSFreeMemory(user);
                continue;
            }

            query_string(sessions[i].SessionId, WTSDomainName, &domain);
            query_string(sessions[i].SessionId, WTSWinStationName, &session_name);

            if (query_info(sessions[i].SessionId, &info) && info != NULL) {
                state = info->State;
                idle_seconds = get_idle_seconds(info);
                logon_time = info->LogonTime;
            }

            if (!first) out_char(',');
            first = 0;

            write_session_json(
                sessions[i].SessionId,
                domain,
                user,
                (session_name && session_name[0] != 0) ? session_name : sessions[i].pWinStationName,
                state,
                idle_seconds,
                logon_time);

            if (info) WTSFreeMemory(info);
            if (session_name) WTSFreeMemory(session_name);
            if (domain) WTSFreeMemory(domain);
            if (user) WTSFreeMemory(user);

            if (g_output_overflow) break;
        }

        WTSFreeMemory(sessions);
    }

    if (g_output_overflow) {
        return EXIT_OUTPUT_OVERFLOW;
    }

    out_char(']');
    out_char('\n');

    if (g_output_overflow) {
        return EXIT_OUTPUT_OVERFLOW;
    }

    return EXIT_OK;
}

void mainCRTStartup(void)
{
    HANDLE stdout_handle;
    DWORD win32_error = 0;
    DWORD result = collect_sessions(&win32_error);

    if (result == EXIT_ENUMERATION_FAILED) {
        write_error_with_code("failed to enumerate Windows sessions", win32_error);
        ExitProcess(EXIT_ENUMERATION_FAILED);
    }

    if (result == EXIT_OUTPUT_OVERFLOW) {
        write_error_with_code("JSON output exceeded the internal 256 KiB buffer", 0);
        ExitProcess(EXIT_OUTPUT_OVERFLOW);
    }

    stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!write_all(stdout_handle, g_output, g_output_len)) {
        win32_error = GetLastError();
        write_error_with_code("failed to write JSON to stdout", win32_error);
        ExitProcess(EXIT_WRITE_FAILED);
    }

    ExitProcess(EXIT_OK);
}
