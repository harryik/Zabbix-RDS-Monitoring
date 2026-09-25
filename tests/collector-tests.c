/*
 * Deterministic collector tests: replace only WTS calls, then exercise the
 * same collect_sessions path used by the production executable.
 */
#define RDS_SESSION_TEST
#define mainCRTStartup collector_entry
#include "../rds-session.c"
#undef mainCRTStartup

#define MOCK_OK 0
#define MOCK_USER_FAILURE 1
#define MOCK_INFO_FAILURE 2
#define MOCK_EMPTY_USER 3
#define MOCK_ENUM_FAILURE 4
#define MOCK_NO_LOGON_TIME 5

static int mock_mode = MOCK_OK;
static WCHAR mock_user[] = {'j', 'd', 'o', 'e', 0};
static WCHAR mock_empty_user[] = {0};
static WCHAR mock_domain[] = {'C', 'O', 'N', 'T', 'O', 'S', 'O', 0};
static WCHAR mock_station[] = {'r', 'd', 'p', '-', 't', 'c', 'p', '#', '1', 0};
static WTS_SESSION_INFOW mock_session;
static WTSINFOW mock_info;

BOOL __stdcall WTSEnumerateSessionsW(
    HANDLE server, DWORD reserved, DWORD version,
    WTS_SESSION_INFOW** sessions, DWORD* count)
{
    (void)server;
    (void)reserved;
    (void)version;
    if (mock_mode == MOCK_ENUM_FAILURE) return FALSE;
    *sessions = &mock_session;
    *count = 1;
    return TRUE;
}

BOOL __stdcall WTSQuerySessionInformationW(
    HANDLE server, DWORD id, int info_class, LPWSTR* buffer, DWORD* bytes)
{
    (void)server;
    (void)id;
    if (info_class == WTSUserName) {
        if (mock_mode == MOCK_USER_FAILURE) return FALSE;
        *buffer = (mock_mode == MOCK_EMPTY_USER) ? mock_empty_user : mock_user;
        *bytes = (mock_mode == MOCK_EMPTY_USER) ? (DWORD)sizeof(mock_empty_user) : (DWORD)sizeof(mock_user);
        return TRUE;
    }
    if (info_class == WTSDomainName) {
        *buffer = mock_domain;
        *bytes = (DWORD)sizeof(mock_domain);
        return TRUE;
    }
    if (info_class == WTSWinStationName) {
        *buffer = mock_station;
        *bytes = (DWORD)sizeof(mock_station);
        return TRUE;
    }
    if (info_class == WTSSessionInfo) {
        if (mock_mode == MOCK_INFO_FAILURE) return FALSE;
        *buffer = (LPWSTR)&mock_info;
        *bytes = (DWORD)sizeof(mock_info);
        return TRUE;
    }
    return FALSE;
}

void __stdcall WTSFreeMemory(LPVOID memory)
{
    (void)memory;
}

static BOOL contains(const char* data, DWORD data_len, const char* needle)
{
    DWORD needle_len = ascii_len(needle);
    DWORD i;
    DWORD j;
    for (i = 0; i + needle_len <= data_len; ++i) {
        for (j = 0; j < needle_len && data[i + j] == needle[j]; ++j) { }
        if (j == needle_len) return TRUE;
    }
    return FALSE;
}

static DWORD run_case(int mode)
{
    DWORD error = 0;
    mock_mode = mode;
    mock_session.SessionId = 2;
    mock_session.pWinStationName = mock_station;
    mock_session.State = WTSActive;
    mock_info.State = WTSActive;
    mock_info.LogonTime = (mode == MOCK_NO_LOGON_TIME) ? 0 : 132537600000000000LL;
    mock_info.CurrentTime = 132537612000000000LL;
    mock_info.LastInputTime = mock_info.CurrentTime - 1200000000LL;
    g_output_len = 0;
    g_output_overflow = FALSE;
    return collect_sessions(&error);
}

static void require(BOOL condition, const char* message)
{
    if (!condition) {
        write_error_with_code(message, 0);
        ExitProcess(1);
    }
}

void test_mainCRTStartup(void)
{
    require(run_case(MOCK_OK) == EXIT_OK, "normal collection failed");
    require(contains(g_output, g_output_len, "\"session_uid\":\"2-"), "session UID missing");
    require(!contains(g_output, g_output_len, "\"session_uid\":\"2-0\""), "invalid session UID");
    require(contains(g_output, g_output_len, "\"idle_seconds\":120"), "idle time incorrect");

    require(run_case(MOCK_EMPTY_USER) == EXIT_OK, "empty user should be skipped");
    require(g_output_len == 3 && g_output[0] == '[' && g_output[1] == ']' && g_output[2] == '\n',
            "empty user did not produce an empty array");

    require(run_case(MOCK_USER_FAILURE) == EXIT_SESSION_QUERY_FAILED, "user query failure was ignored");
    require(run_case(MOCK_INFO_FAILURE) == EXIT_SESSION_QUERY_FAILED, "WTSINFO failure was ignored");
    require(run_case(MOCK_NO_LOGON_TIME) == EXIT_SESSION_QUERY_FAILED, "missing logon time was ignored");
    require(run_case(MOCK_ENUM_FAILURE) == EXIT_ENUMERATION_FAILED, "enumeration failure was ignored");
    ExitProcess(0);
}
