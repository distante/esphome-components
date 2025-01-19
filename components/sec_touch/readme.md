# SEC-TOUCH ESPHome Component

This is a component that allows you to control your SEC-TOUCH ventilation controller (Dezentrale Lüftung Zentralregler SEC-Touch) from your ESPHome device.

(For now) It is limited to change the level of the fan pairs, or put them into their special modes (automatic, time, etc). There is no way to change the timing intervals for now. You need to make that in the SEC-TOUCH device itself.


## First of all thanks to:

- [Manuel-Siekmann](https://github.com/Manuel-Siekmann/) who did the heavy lifting of find out the communication protocol of the SEC-TOUCH device in his [VentilationSystem](https://github.com/Manuel-Siekmann/VentilationSystem) project.
- [Samuel Sieb](https://github.com/ssieb) who helped me to understand some basic c++ and ESPHome concepts and responded my questions on Discord.

# Configuration:

Add this to `your-device.yaml` file:
```yaml

external_components:
  - source:
      type: git
      url: https://github.com/distante/esphome-components
      ref: main

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

  # Enable fallback hotspot (captive portal) in case wifi connection fails
  ap:
    ssid: "Ventilation-Controller"
    password: "supersecretpassword"

captive_portal:

web_server: ## Just if you can to use the web server
  port: 80
  log: false
  version: 3
  
uart:
  id: sec_touch_uart
  tx_pin: 
    number: GPIO17 ## Replace with your TX pin
  rx_pin:
    number: GPIO16 ## Replace with your RX pin
  baud_rate: 28800

sec_touch:
  uart_id: sec_touch_uart
  update_interval: 5s # 5s is the default

fan: ## See the Fan order below
  - platform: sec_touch
    fan_number: 1
    name: "Fan 1"
  - platform: sec_touch
    fan_number: 2
    name: "Fan 2"
  # - platform: sec_touch
  #   fan_number: 3
  #   name: "Fan 3"
  # - platform: sec_touch
  #   fan_number: 4
  #   name: "Fan 4"
  # - platform: sec_touch
  #   fan_number: 5
  #   name: "Fan 5"
  # - platform: sec_touch
  #   fan_number: 6
  #   name: "Fan 6"
```

Notice that the fans numbers are ordered so:

|   --   |   --   |   --   |   --   |
|--------|--------|--------|----|
| Pair 1 | Pair 3 | Pair 5 | ℹ️ |
| Pair 2 | Pair 4 | Pair 6 | ⚙️  |


## :bulb: Setting The Fan Level above 6
Please check the [Special Fan level Values](#fan-pair-level-special-values) section to understand the special values for the fan level.

# Development

# Known Ids
## Command Ids
```
SET: 32
GET: 32800
```
## Fan Pair Level
Stored on `FAN_LEVEL_IDS` array as: 
```
173, 174, 175, 176, 177, 178
```
## Fan Pair Label/Name
Stored on `FAN_LABEL_IDS` array as: 

```
78, 79, 80, 81, 82, 83
```
---
### ⚠️ ID Mapping to Hardware Fan Pair
The Fan pairs are ordered from top to bottom, and left to right.

|   --   |   --   |   --   |   --   |
|--------|--------|--------|----|
| Level 173 | Level 175 | Level 177 | ℹ️ |
| Level 174 | Level 176 | Level 178 | ⚙️  |


# Fan Pair Level special values
Each Fan Pair can have a level from `0` to `10`. There, just `0` to `6` are _"real"_ levels, `7` to `10` are _"special"_ levels. A Level of `255` means there is not fan in that pair.

| Level    | Meaning  |
|----------|----------|
| 7        | Burst Ventilation / Stosslüften      |
| 8        | Automatic Humidity / Automatik Feuchte      |
| 9        | Automatic CO2 / Automatik CO2      |
| 10       | Automatic Time / Automatik Zeit      |
| 11       | Sleep / Schlummer      |
| 255      | Not Connected      |




# UART Messages
### GET Request Message
The structure of the GET Message is as follows  (special chars added manually):
```
[STX]32800[TAB]173[TAB]54142[ETX]
```

where:
- `32800` is the command id for a GET request.
- `173` is the property id of the fan pair.
- `54142` is the checksum.

### GET Response Message

After a GET request message is sent, the SEC-TOUCH sends two messages backs, an `ACK` and the response of our request. This is how the full returned buffer looks like (special chars added manually):
```log
[STX][ACK][ETX][STX]32[TAB]178[TAB]7[TAB]32627[ETX]
```
Where:
- `32` is (probably) the command id that can be used to set the level of the fan pair.
- `178` is the property id of the fan pair.
- `7` is the value assigned to that id.
- `32627` is the checksum(?).


```log
Byte received: 2   // STX 0x02
Byte received: 6   // ACK 0x06
Byte received: 10  // ETX 0x0A
Byte received: 2   // STX 0x02
Byte received: 51  // event_type?
Byte received: 50  // event_type?
Byte received: 9   // TAB 0x09
Byte received: 49  // id
Byte received: 55  // id
Byte received: 51  // id
Byte received: 9   // TAB 0x09
Byte received: 49  // value
Byte received: 48  // value
Byte received: 99  // TAB 0x09
Byte received: 52  // checksum?
Byte received: 50  // checksum?
Byte received: 54  // checksum?
Byte received: 57  // checksum?
Byte received: 54  // checksum?
Byte received: 10  // ETX 0x0A
Buffer: 
32	173	10	42696

```
---

:bulb: The Device expects for us to send an ACK after receiving that data.

---

:bulb: Sometimes we get an single `255` input value, we discard it as noise.

---

### SET Request Message
The structure of the SET Message is as follows  (special chars added manually):
```log
[STX]32[TAB]173[TAB]5[TAB]42625[ETX]
```

where:
- `32` is the command id for a SET request.
- `173` is the property id of the fan pair.
- `5` is the value assigned to that id.
- `42625` is the checksum.
- 
### SET Response Message
Sadly (IMHO) the SEC-TOUCH just sends an `ACK` message after receiving a SET message and sometimes it takes a couple of seconds for the SEC-TOUCH screen to update the new value, so the best we can do is to wait for the `ACK` message and then send a `GET` message to do a security sync of the new value.

```log
Byte received: 2   // STX 0x02
Byte received: 6   // ACK 0x06
Byte received: 10  // ETX 0x0A
```





### Update submodules
```
git submodule update --remote --merge
```