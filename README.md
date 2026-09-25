# Zabbix-RDS-Monitoring
# rds-session.exe

Small native Windows x64 collector for Zabbix/Grafana session monitoring.

## Output

```json
[{"id":"2","user":"CONTOSO\\jdoe","session":"rdp-tcp#5","state":"Active","idle_seconds":120,"logon_time":"2026-09-25 17:42:00 +02:00"}]
```

See `ZABBIX_SETUP_EN.md` for installation and Zabbix configuration.

