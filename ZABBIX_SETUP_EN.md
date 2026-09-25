# Native Windows RDS Sessions -> Zabbix -> Grafana

Default polling interval: **5 minutes**.

This version uses a small native **x64 Windows executable** and the documented Windows Remote Desktop Services API (WTS API). It does not parse `quser`, does not start PowerShell, and does not use `.NET` or `Add-Type`.

## 1. Files on each Windows Server

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

Then test it through Zabbix Agent 2:

```powershell
& "C:\Program Files\Zabbix Agent 2\zabbix_agent2.exe" -t windows.rds.sessions.get
```

Example output:

```json
[{"id":"2","user":"CONTOSO\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
```

The program emits UTF-8 JSON, stable English state names, and a locale-independent local timestamp with UTC offset.

If WTS session enumeration fails, the executable exits with a non-zero code and writes a diagnostic message to stderr instead of returning a misleading empty JSON array.

## 2. Master item

Create this item in the Windows template:

```text
Name: Windows: RDS sessions raw
Type: Zabbix agent
Key: windows.rds.sessions.get
Type of information: Text
Update interval: 5m
History: 1d
```

This is the only item that runs `rds-session.exe`.

To change the refresh rate later, change only the **Update interval** on this master item, for example `1m`, `5m`, or `10m`.

## 3. Active session counter

Create a dependent item:

```text
Name: RDS: Active sessions
Type: Dependent item
Key: windows.rds.sessions.active
Master item: Windows: RDS sessions raw
Type of information: Numeric (unsigned)
```

Preprocessing -> JSONPath:

```text
$[?(@.state == "Active")].length()
```

Do not configure a separate update interval. It updates whenever the master item receives new JSON.

## 4. Session discovery

Create a discovery rule:

```text
Name: Windows: RDS session discovery
Type: Dependent item
Key: windows.rds.sessions.discovery
Master item: Windows: RDS sessions raw
Disable lost resources: Immediately
Delete lost resources: After 1h
```

Add these LLD macros:

| Macro | JSONPath |
|---|---|
| `{#SESSIONID}` | `$.id` |
| `{#USER}` | `$.user` |
| `{#SESSION}` | `$.session` |
| `{#STATE}` | `$.state` |
| `{#LOGONTIME}` | `$.logon_time` |

## 5. Item prototype

Inside the discovery rule create an item prototype:

```text
Name: RDS session [{#SESSIONID}]: User={#USER}; Session={#SESSION}; State={#STATE}; Logon={#LOGONTIME}
Type: Dependent item
Key: windows.rds.session.idle[{#SESSIONID}]
Master item: Windows: RDS sessions raw
Type of information: Numeric (unsigned)
Units: s
History: 1d
```

Preprocessing -> JSONPath:

```text
$[?(@.id == "{#SESSIONID}")].idle_seconds.first()
```

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
- No `Add-Type`
- No temporary compiled DLLs
- No network communication performed by the EXE

The C source is included as `rds-session.c` for audit/rebuild purposes.
