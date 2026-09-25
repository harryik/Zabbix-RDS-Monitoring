# Zabbix-RDS-Monitoring

Small native Windows x64 collector for Zabbix/Grafana session monitoring.

The collector uses the documented Windows WTS API directly. It does not parse `quser`, start PowerShell, require .NET, or compile temporary code during polling.

## Output

Example UTF-8 JSON:

```json
[{"id":"2","session_uid":"2-1790350920","user":"CONTOSO\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
```

`id` remains the Windows Session ID and is intentionally emitted as a string.

`session_uid` identifies a specific session instance. It is built from:

```text
<Windows Session ID>-<logon time as Unix epoch seconds>
```

For example:

```text
2-1790350920
```

This prevents Zabbix history from being mixed when Windows later reuses the same Session ID for another login.

Sessions without an interactive user are ignored. Console sessions may be returned as well as RDP sessions.

## Recommended Zabbix design

Use the included importable template:

```text
zabbix_template_rds_sessions.yaml
```

It creates one master item that executes the collector and dependent items for:

```text
RDS: Total sessions
RDS: Active sessions
RDS: Disconnected sessions
```

Low-level discovery identifies session instances using `session_uid` and creates three items for every detected session:

```text
State
Idle time
Logon time
```

Default history retention:

```text
Raw JSON:          1d
Summary counters: 30d
Per-session data:  7d
```

Lost session resources are disabled immediately and deleted after 1 hour.

The master item validates that the collector returned a JSON array. A warning trigger fires after 15 minutes without a valid value (three default polling intervals). The collector is still executed only once per polling interval.

The current template export format is **Zabbix 7.0**.

## Building

Requirements:

- Visual Studio Build Tools or Visual Studio
- Desktop development with C++
- Windows 10/11 SDK

Open **x64 Native Tools Command Prompt for VS** and run:

```cmd
cl /nologo /O2 /GS- /TC rds-session.c /link /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup /MACHINE:X64 kernel32.lib wtsapi32.lib /OUT:rds-session.exe
```

The resulting executable is native x64 and links only against Windows system APIs used by the collector.

You can inspect its dependencies with:

```cmd
dumpbin /dependents rds-session.exe
```

## Exit codes

The collector deliberately fails instead of returning a misleading empty session list when it cannot collect valid data.

| Code | Meaning |
|---:|---|
| 0 | Success |
| 2 | WTS session enumeration failed |
| 3 | JSON output exceeded the internal buffer |
| 4 | Writing JSON to stdout failed |
| 5 | Failed to query complete data for a user session |

Diagnostic messages are written to stderr. Zabbix UserParameter does not use the process exit code to mark a text item unsupported; the template's master-item preprocessing validates the JSON output instead.

## Zabbix

See [ZABBIX_SETUP_EN.md](ZABBIX_SETUP_EN.md) for installation, template import and item details.

The recommended design is one master item running `rds-session.exe`, with dependent items and low-level discovery using the returned JSON.
