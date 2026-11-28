# SEC-Touch

Talking to a SEC-Touch ventilation panel over UART, and sniffing its unknown properties so they can be mapped.

## Language

**Property**:
A numbered setting or reading on the panel, addressed by its property ID.
_Avoid_: Register, field

**Raw value**:
A property's value exactly as the panel sends it, byte for byte, padding included.
_Avoid_: Value (when the parsed integer could be meant), state

**Property record**:
Everything the sniffer knows about one property: its raw value, its recent history of changes and its query status.
_Avoid_: Sniffed entry, discovered ID, observation

**Change**:
A raw value that differs byte for byte from the property's previous raw value. The first value seen for a property is not a change.
_Avoid_: Update

**Query status**:
The outcome of the sniffer's latest scan query for a property: unqueried, ok, nak or timeout.
_Avoid_: Unresponsive, state

**Known property**:
A property the SEC-Touch component can turn into a control, whether or not this configuration uses it. The set is fixed by the component, not by the user.
_Avoid_: Supported ID, mapped ID

**Registered property**:
A known property that an entity in this configuration actually uses. Every registered property is known; a known property may be unregistered.
_Avoid_: Configured ID, used ID

**Unknown property**:
A property that is not known: the target of reverse-engineering.
_Avoid_: New ID, unmapped ID

**Sniffer goal**:
What a sniffing session is for. It picks both which properties a scan queries and which ones the page shows. **Discover** (the default) targets unknown properties, to find new controls. **Check** targets known properties, so a user can confirm their panel sends what the component expects.
_Avoid_: Mode (passive and scan are modes), preset, filter

**Experiment**:
An investigation of what one panel setting (e.g. "fan 1 mode") does to the unknown properties: a series of captures, each taken with the setting at a named value.
_Avoid_: Mark, baseline, snapshot, session

**Capture**:
A scan stored in an experiment, together with the label of the value the setting was at. A capture is **full** (the experiment's whole range) or **narrowed** (only its current candidates).
_Avoid_: Snapshot, sample

**Label**:
The name a user gives the setting's value for a capture ("boost"). Two labels are the same after trimming outer whitespace, ignoring case.
_Avoid_: Tag, value name

**Candidate**:
An unknown, unmuted property whose captures in an experiment show a round trip (one raw value, a different one under another label, then the first value under the first label again) and give the same raw value for every capture with the same label.
_Avoid_: Match, hit, suspect

**Corroborated candidate**:
A candidate for which two labels have each been captured at least twice with agreeing raw values. Only a corroborated candidate can get a write probe.
_Avoid_: Confirmed, verified

**Disqualified property**:
A property whose captures with the same label disagree in an experiment, so it can't be a candidate there.
_Avoid_: Noise, rejected

**Write probe**:
Writing a raw value already observed for a corroborated candidate, reading the property back to see whether the write took, then restoring the original.
_Avoid_: Write test, set test

**Verdict**:
A write probe's result: **writable** (reading the property back shows the written value), **not writable** (the panel acknowledged or refused the write, and reading the property back shows only the original), **unclear** (anything else), or **nothing written** (the original couldn't be read, or already equals the value). "Writable" means only this verdict.
_Avoid_: Pass/fail, result

**Restore**:
Writing a property's original raw value back after a write probe and reading it back to check.
_Avoid_: Undo, rollback

**Time program**:
A fan pair's daily schedule on the panel: 5 switch times that set the pair's level while the pair is in Automatik Zeit.
_Avoid_: Zeitprogramm (panel word), schedule, timer

**Switch time**:
One of the 5 entries of a time program: a time of day, a level and whether it is enabled.
_Avoid_: Schaltzeit (panel word), slot, entry

**Enabled**:
Whether a switch time takes part in its time program. A switch time that isn't enabled never takes effect.
_Avoid_: Aktiv (panel word), active

**Level** (of a switch time):
The fan level a switch time sets when it takes effect: one of the fan's levels 0 to 9.
_Avoid_: Modus (panel word), mode (a pair's mode is Normal, Automatik Zeit and so on)

**Switch time in effect**:
The enabled switch time whose level the time program runs now: the latest one at or before the panel's time, counting from yesterday's last one before today's first.
_Avoid_: Current switch time, active switch time

**Running level**:
The level a pair's time program runs now: the level of the switch time in effect. A pair has one only while it is in Automatik Zeit.
_Avoid_: Current level, program level
