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
