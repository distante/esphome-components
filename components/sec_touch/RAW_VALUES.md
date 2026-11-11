# Sniffer Report: Raw String Values, Date and Clock

**Date:** 2026-11-11
**Device:** `esp32-power-meter` (sec_touch protocol, controller firmware `SWZ0021E21H`)
**Method:** 1 active sniffer scan over property IDs 1–255, raw buffers taken from the `DEBUG` log

---

## Finding

**Property values are strings on the wire, and many of them are not integers.**
The incoming parser runs every value through `atoi`, which stops at the first
non-digit. The sniffer text sensor and the ID table in the [readme](readme.md#what-i-have-found-so-far)
therefore show truncated values:

| ID | On the wire   | Parsed (`atoi`) |
|----|---------------|-----------------|
| 1  | `11.11.26`    | `11`            |
| 2  | `13:32:08`    | `13`            |
| 3  | `SWZ0021E21H` | `0`             |
| 48 | `0A00`        | `0`             |

The upstream dump (`1=23, 2=16`) fits the same pattern: day 23, hour 16.

**Property `1` is the controller's date, property `2` its clock.**

| ID | Format     | Example    |
|----|------------|------------|
| 1  | `DD.MM.YY` | `11.11.26` |
| 2  | `HH:MM:SS` | `13:32:08` |

At the time of the scan, the controller clock was within a few seconds of local time
(CET, after the switch from summer time). Whether the controller switches between summer
and winter time by itself is unknown.

### Writing date and clock

**Both are writable** with a plain SET in the same format (tested 2026-11-11):

| Sent                            | Answer | Read back right after |
|---------------------------------|--------|-----------------------|
| `32<TAB>2<TAB>13:47:40`         | ACK    | `13:47:00`            |
| `32<TAB>1<TAB>12.11.26`         | ACK    | `12.11.26`            |

- The controller only takes hours and minutes: seconds are reset to `00` on every
  write. To sync to the second, send `HH:MM:00` exactly on the minute boundary.
- The clock keeps running normally after a write (`13:47:00` → `13:47:23` 23 s later).
- `SetDataTask` currently can't send these: it only accepts known IDs, and its
  `char value[8]` holds 7 characters, so `13:47:00` would go out as `13:47:0`.

---

## Other decoded IDs

Hypotheses from the raw strings, unverified unless noted:

| ID       | Raw value                       | Hypothesis                                                                                        |
|----------|---------------------------------|---------------------------------------------------------------------------------------------------|
| 3        | `SWZ0021E21H`                   | Software version                                                                                  |
| 10       | `ETA0032E23G`                   | Hardware / device version                                                                         |
| 13, 16   | `-----------`                   | Empty text fields of the same width as 3 and 10                                                   |
| 20, 21   | `00.00.00`, `00:00:00`          | A second date/time pair, unset (last error? filter change?)                                       |
| 55       | `0.360.00:00`                   | Unknown composite value                                                                           |
| 101–152  | `03`, `09:00:00`, `22:00:00`, … | Time programs ("Automatik Zeit"): blocks of 11 IDs starting at 101, 112, 123, 134, 145, each with switching times `HH:MM:SS` |
| 99, 100  | `-50.0`, `50.0`                 | Decimal values (a range, maybe temperature)                                                       |

Values with leading zeros (`5=0100`, `7=01`, `85=00`) are strings as well. A SET must
presumably reproduce them verbatim, like `0A00` / `0800` for summer ventilation
(see [SUMMER_VENTILATION.md](SUMMER_VENTILATION.md)).

---

## Raw values

Not in the list: registered listeners (48, 78–82, 173–178), IDs whose log lines were
dropped by the API log stream (31, 32, 153–172, 179–185, 190–192) and IDs 209–255,
which never answered. ID 193, which timed out in the upstream scan, answered this time.

<details>
<summary>Raw values (copy-paste ready)</summary>

```
1=11.11.26, 2=13:32:08, 3=SWZ0021E21H, 4=00, 5=0100, 6=1, 7=01, 8=01, 9=00, 10=ETA0032E23G, 11=00, 12=00, 13=-----------, 14=00, 15=00, 16=-----------, 17=00, 18=00, 19=00, 20=00.00.00, 21=00:00:00, 22=40, 23=32, 24=28, 25=25, 26=21, 27=0, 28=61, 29=70, 30=74, 33=100, 34=50, 35=50, 36=50, 37=50, 38=50, 39=50, 40=50, 41=50, 42=50, 43=50, 44=50, 45=50, 46=50, 47=75, 49=1200, 50=400, 51=20, 52=70, 53=10, 54=20, 55=0.360.00:00, 56=120, 57=0, 58=1, 59=95, 60=7, 61=7, 62=7, 63=7, 64=7, 65=0, 66=1, 67=2, 68=3, 69=4, 70=5, 71=6, 72=0, 73=0, 74=0, 75=0, 76=0, 77=0, 83=0, 84=0, 85=00, 86=0, 87=00, 88=0, 89=00, 90=0, 91=0, 92=100, 93=0, 94=6, 95=0, 96=100, 97=250, 98=2000, 99=-50.0, 100=50.0, 101=03, 102=09:30:00, 103=09:00:00, 104=15:00:00, 105=20:00:00, 106=22:00:00, 107=3, 108=7, 109=0, 110=0, 111=0, 112=03, 113=09:00:00, 114=09:30:00, 115=15:00:00, 116=20:00:00, 117=22:00:00, 118=7, 119=3, 120=0, 121=0, 122=0, 123=03, 124=09:00:00, 125=09:30:00, 126=15:00:00, 127=20:00:00, 128=23:00:00, 129=7, 130=3, 131=0, 132=0, 133=2, 134=03, 135=09:00:00, 136=09:30:00, 137=15:00:00, 138=20:00:00, 139=22:00:00, 140=7, 141=3, 142=0, 143=0, 144=3, 145=03, 146=09:00:00, 147=09:30:00, 148=15:00:00, 149=20:00:00, 150=22:00:00, 151=7, 152=3, 186=31, 187=27, 188=18, 189=10, 193=74, 194=85, 195=100, 196=0, 197=36, 198=31, 199=27, 200=23, 201=18, 202=10, 203=64, 204=70, 205=74, 206=80, 207=85, 208=100
```

</details>

---

## How to reproduce

The sniffer text sensor only sees the `atoi` result. The raw string is in the `DEBUG` log
of `sec_touch`:

```log
[D][sec_touch:453]:   [process_data] buffer: \x0232\t2\t13:32:08\t<crc>
```

Enable the sniffer with an active scan (see [readme](readme.md#full-example-with-active-scan)),
set `logger: level: DEBUG` with `sec_touch.uart: WARN` to keep the volume manageable, stream
the log to a file (`esphome logs <config>.yaml > scan.log`) and extract the
`[process_data] buffer:` lines. At `DEBUG`, the API log stream drops lines under load, so
some IDs may be missing from a single scan.
