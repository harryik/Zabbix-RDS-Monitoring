# Zabbix-RDS-Monitoring

Small native Windows x64 collector for Zabbix/Grafana session monitoring.

The collector uses the documented Windows WTS API directly. It does not parse `quser`, start PowerShell, require .NET, or compile temporary code during polling.

## Output

Example UTF-8 JSON:

```json
[{"id":"2","user":"CONTOSO\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
```

Sessions without an interactive user are ignored. Console sessions may be returned as well as RDP sessions.

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

Diagnostic messages are written to stderr.

## Zabbix

See [ZABBIX_SETUP_EN.md](ZABBIX_SETUP_EN.md) for installation and Zabbix configuration.

The recommended design is one master item running `rds-session.exe`, with dependent items and low-level discovery using the returned JSON.
