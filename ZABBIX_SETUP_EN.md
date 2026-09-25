# Native Windows RDS Sessions -> Zabbix -> Grafana

Default polling interval: **5 minutes**.

This project uses a small native **x64 Windows executable** and the documented Windows Remote Desktop Services API (WTS API). It does not parse `quser`, start PowerShell, or use .NET / Add-Type during polling.

The recommended setup is:

```text
rds-session.exe
      |
      v
Windows: RDS sessions raw
      |
      +-- RDS: Total sessions
      +-- RDS: Active sessions
      +-- RDS: Disconnected sessions
      |
      +-- RDS: Session discovery
             +-- State
             +-- Idle time
             +-- Logon time
```

Only the master item executes `rds-session.exe`. All other values are dependent items derived from the returned JSON.

## 1. Install the collector on each Windows Server

Create the scripts directory if it does not exist:

```text
C:\Program Files\Zabbix Agent 2\scripts
```

Copy:

```text
rds-session.exe
```

to:

```text
C:\Program Files\Zabbix Agent 2\scripts\rds-session.exe
```

Copy:

```text
rds-session.conf
```

to:

```text
C:\Program Files\Zabbix Agent 2\zabbix_agent2.d\rds-session.conf
```

If Windows marks the downloaded EXE as blocked, run once:

```powershell
Unblock-File "C:\Program Files\Zabbix Agent 2\scripts\rds-session.exe"
```

Restart Zabbix Agent 2:

```powershell
Restart-Service "Zabbix Agent 2"
```

Test the executable directly:

```powershell
& "C:\Program Files\Zabbix Agent 2\scripts\rds-session.exe"
$LASTEXITCODE
```

A successful run should return JSON and exit code `0`.

Then test the UserParameter through Zabbix Agent 2:

```powershell
& "C:\Program Files\Zabbix Agent 2\zabbix_agent2.exe" -t windows.rds.sessions.get
```

Example output:

```json
[{"id":"2","user":"CONTOSO\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
```

The program emits UTF-8 JSON, stable English state names, and a locale-independent local timestamp with UTC offset.

If WTS session enumeration fails, the executable exits with a non-zero code and writes a diagnostic message to stderr instead of returning a misleading empty JSON array.

## 2. Import the Zabbix template

Import:

```text
zabbix_template_rds_sessions.yaml
```

The template export format is **Zabbix 7.0**.

In Zabbix:

```text
Data collection -> Templates -> Import
```

Import the YAML file and then link:

```text
Windows RDS sessions by Zabbix agent 2
```

to each Windows Server that should be monitored.

The template can be linked together with the standard Windows by Zabbix agent / agent active template.

## 3. What the template creates

### Master item

```text
RDS: Sessions raw
Key: windows.rds.sessions.get
Type: Zabbix agent
Type of information: Text
Update interval: 5m
History: 1d
```

This is the **only item that executes rds-session.exe**.

To change the polling frequency, change only this item's update interval.

### Summary dependent items

```text
RDS: Total sessions
RDS: Active sessions
RDS: Disconnected sessions
```

They update whenever the master item receives a new JSON payload and do not execute the collector again.

Default history:

```text
30d
```

### Session discovery

The dependent LLD rule:

```text
RDS: Session discovery
```

discovers:

| LLD macro | JSON field |
|---|---|
| `{#SESSIONID}` | `id` |
| `{#USER}` | `user` |
| `{#SESSION}` | `session` |

A session that disappears from the JSON is disabled immediately and its discovered items are deleted after **1 hour**.

## 4. Per-session items

For every discovered session, Zabbix creates three dependent items.

Example for session ID 2:

```text
RDS session [2] CONTOSO\jdoe (rdp-tcp#5): State
RDS session [2] CONTOSO\jdoe (rdp-tcp#5): Idle time
RDS session [2] CONTOSO\jdoe (rdp-tcp#5): Logon time
```

### State

Example value:

```text
Active
```

The value is stored as text. Unchanged values are kept with a 1-hour heartbeat to avoid unnecessary history growth.

### Idle time

Example value:

```text
120
```

Units:

```text
s
```

Zabbix can display this as a human-readable duration.

This is the primary metric for determining whether an active session has recently received keyboard or mouse input.

### Logon time

Example value:

```text
2026-09-25 17:42:00 +02:00
```

The value is stored as text because the collector deliberately includes the server-local time and UTC offset.

Unchanged values are kept with a 1-hour heartbeat.

## 5. Tags

The template uses tags so RDS data can be filtered cleanly in Zabbix and Grafana.

Summary items use:

```text
Application = RDS Sessions
component   = rds
scope       = summary
```

Discovered session items additionally expose:

```text
component = rds-session
user      = {#USER}
session   = {#SESSION}
sessionid = {#SESSIONID}
```

This makes it possible to filter or group data by user, session or Session ID without parsing long item names.

## 6. Returned session states

The EXE returns WTS state names in English:

```text
Active
Connected
ConnectQuery
Shadow
Disconnected
Idle
Listen
Reset
Down
Init
```

Sessions without an interactive user are ignored.

Console sessions may be returned as well as RDP sessions.

## 7. Collector exit codes

| Code | Meaning |
|---:|---|
| 0 | Success |
| 2 | WTS session enumeration failed |
| 3 | JSON output exceeded the internal 256 KiB buffer |
| 4 | Writing JSON to stdout failed |

A non-zero exit code is intentional. It prevents collection failures from being represented as a valid empty session list.

## 8. Binary details

- Architecture: **Windows x64**
- Runtime: **native Win32**
- Required system DLLs: `KERNEL32.dll`, `WTSAPI32.dll`
- No PowerShell process during polling
- No .NET dependency
- No Add-Type
- No temporary compiled DLLs
- No network communication performed by the EXE

The C source is included as `rds-session.c` for audit and rebuild purposes.
