# Repository Notes

- Work in this checkout only. Do not edit or depend on a sibling `../accesskit` checkout for the direct SWELL AT-SPI branch.
- The Linux SWELL sample app is the main AT-SPI harness: `WDL/swell/sample_project`.
- Default build with AT-SPI: `make -C WDL/swell/sample_project -j2`.
- Build without AT-SPI: `make -C WDL/swell/sample_project -j2 NOATSPI=1`.
- Clean sample outputs: `make -C WDL/swell/sample_project clean`.
- Debug logging is off by default. Enable it with `SWELL_ATSPI_DEBUG=1`; logs go to `/tmp/swell-atspi-$PID.log` unless `SWELL_ATSPI_LOG=/tmp/swell-atspi-myapp.log` is set.
- Manual Orca packet: run `SWELL_ATSPI_DEBUG=1 SWELL_ATSPI_LOG=/tmp/swell-atspi-myapp.log ./myapp` from `WDL/swell/sample_project`, tab through every control, type in both edit fields, exercise slider/progress/menu/combo/list/tab controls, then collect the log and `tools/atspi_events.py` output.
- Useful inspection commands from repo root: `tools/atspi_dump.py --apps`, then a direct dump for `MyApp`; `tools/atspi_events.py --all-object-events --seconds 30` while interacting with the app.
