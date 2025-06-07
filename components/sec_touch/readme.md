# SEC-TOUCH ESPHome Component
This is a component that allows you to control your SEC-TOUCH ventilation controller (Dezentrale Lüftung Zentralregler SEC-Touch) from your ESP device and integrate it to Home Assistant.

<div class="text-center">
  <img src="images/sec-touch-panel.webp" alt="SEC-TOUCH Panel" />
</div>

(For now) It is limited to change the level of the fan pairs, or put them into their special modes (automatic, time, etc). There is no way to change the timing intervals for now. You need to make that in the SEC-TOUCH device itself.

The SEC-Touch device is always the source of true. No fan state restoration is done after a power loss.

## First of all thanks to:

- [Manuel Siekmann](https://github.com/Manuel-Siekmann/) who did the heavy lifting of find out the communication protocol of the SEC-TOUCH device in his [VentilationSystem](https://github.com/Manuel-Siekmann/VentilationSystem) project.
- [Samuel Sieb](https://github.com/ssieb) who helped me to understand some basic c++ and ESPHome concepts and responded my questions on Discord.

# Do it at your own Risk! 

The SEC-Touch has no open API or documentation for their UART interfaces, so I can no and DO NOT offer any warranty, everything here is reverse engineered. I have no affiliation with the company that produces the SEC-TOUCH device nor do I want this component to be used for financial gains. The only reason I developed this is because the official alternative is needs internet to works, which I do not want for a device that costs +200€ EURO.

If you decide to use this component a **fan damage can not be ruled out**. **You** are the only responsible in case something goes wrong. 

Yes Really.

# Hardware.

## Required
- An ESP32 or ESP8266 device.
  - I use [this one with USB C](https://amzn.to/40R7ee3).
- 4 Pin Pluggable Terminals
-  [4 core](https://amzn.to/3EK0uao) or [3 core cable](https://amzn.to/4jS36Dm) (depending on how you can/want to power the ESP device)

## Optional
- [Breakout board](https://amzn.to/416UuBA) if you do not want to solder stuff
- [PCB board with header connectors](https://amzn.to/40TYcNl) if you want to solder everything (like I did, see bellow).
- [Din Rail Mounting clips](https://amzn.to/4aS99nc) to mount the device if needed. 
# Connection

The SEC-TOUCH has an "PC" port at the right bottom part. It has an `3.x` volts output. It worked to power my ESP32 device when I was using Software UART, but when I switched to Hardware UART it didn't work anymore. When I measured the voltage it was around `3.2v` tops.

I recommend you to power your ESP32 device using another source.

That said, probably it has enough power for an ESP8266 device that uses software UART.

### ESP32 cable connection (using external power )
The same but without the 3.3v connection. 😬

|ESP32  | SEC-Touch| 
|--- | ---| 
|GND | GND (1st from the left)|
|GPIO 17 | RX (2nd from the left)| 
|GPIO 16 | TX (3rd from the left)|

### ESP8266 connection
<div class="text-center">
  <img src="https://github.com/Manuel-Siekmann/VentilationSystem/raw/main/doc/sketch.jpg" alt="https://github.com/Manuel-Siekmann/VentilationSystem/raw/main/doc/sketch.jpg" />
</div>
# Installation
## Final Installation
<div class="text-center">
  <img src="images/final_install.webp" alt="Final Installation" />
</div>


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


web_server:
  port: 80
  local: true
  log: false
  version: 3
  sorting_groups:
    - id: group_1
      name: Fan Group 1
      sorting_weight: -100
    - id: group_2
      name: Fan Group 2
      sorting_weight: -99
    - id: group_3
      name: Fan Group 3
      sorting_weight: -98
    - id: group_4
      name: Fan Group 4
      sorting_weight: -97
    - id: group_5
      name: Fan Group 5
      sorting_weight: -96
    - id: group_6
      name: Fan Group 6
      sorting_weight: -95
    - id: group_settings
      name: Configuration
      sorting_weight: -94
  
uart:
  id: sec_touch_uart
  tx_pin: 
    number: GPIO17
  rx_pin:
    number: GPIO16
  baud_rate: 28800

sec_touch:
  uart_id: sec_touch_uart
  update_interval: 5s # 5s is the default

fan:
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 1
    name: "Fan 1"
    web_server:
      sorting_group_id: group_1
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 2
    name: "Fan 2"
    web_server:
      sorting_group_id: group_2
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 3
    name: "Fan 3"
    web_server:
      sorting_group_id: group_3
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 4
    name: "Fan 4"
    web_server:
      sorting_group_id: group_4
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 5
    name: "Fan 5"
    web_server:
      sorting_group_id: group_5
  - platform: sec_touch
    icon: "mdi:fan"
    fan_number: 6
    name: "Fan 6"
    web_server:
      sorting_group_id: group_6


button:
  - platform: sec_touch
    program_text_update:
      name: "Program Labels Update"
      icon: "mdi:book-refresh"
  - platform: restart
    name: "Restart"
    
text_sensor:
  - platform: sec_touch
    fan_number: 1
    label_text:
      name: "Label Fan 1"
      web_server:
        sorting_group_id: group_1
    mode_text:
      name: "Mode Fan 1"
      web_server:
        sorting_group_id: group_1
  - platform: sec_touch
    fan_number: 2
    label_text:
      name: "Label Fan 2"
      web_server:
        sorting_group_id: group_2
    mode_text:
      name: "Mode Fan 2"
      web_server:
        sorting_group_id: group_2
  - platform: sec_touch
    fan_number: 3
    label_text:
      name: "Label Fan 3"
      web_server:
        sorting_group_id: group_3
    mode_text:
      name: "Mode Fan 3"
      web_server:
        sorting_group_id: group_3
  - platform: sec_touch
    fan_number: 4
    label_text:
      name: "Label Fan 4"
      web_server:
        sorting_group_id: group_4
    mode_text:
      name: "Mode Fan 4"
      web_server:
        sorting_group_id: group_4
  - platform: sec_touch
    fan_number: 5
    label_text:
      name: "Label Fan 5"
      web_server:
        sorting_group_id: group_5
    mode_text:
      name: "Mode Fan 5"
      web_server:
        sorting_group_id: group_5
  - platform: sec_touch
    fan_number: 6
    label_text:
      name: "Label Fan 6"
      web_server:
        sorting_group_id: group_6
    mode_text:
      name: "Mode Fan 6"
      web_server:
        sorting_group_id: group_6

```

Notice that the fans numbers are ordered so:

|   --   |   --   |   --   |   --   |
|--------|--------|--------|----|
| Pair 1 | Pair 3 | Pair 5 | ℹ️ |
| Pair 2 | Pair 4 | Pair 6 | ⚙️  |


## :bulb: Setting The Fan Level above 6
Please check the [Special Fan level Values](#fan-pair-level-special-values) section to understand the special values for the fan level.

# Web Server preview

<div class="text-center">
  <img src="images/web-server.webp" alt="ESPHome Webserver" />
</div>

# Home Assistant
Home assistant has the problem that all fans show their speed as percentage. But we do not have percentage values, we have levels from `0` to `11`. From which some of those values are special ones.

For now we have to "live with" that, selecting presets will carry the fan speed/percent to their corresponding level, but going into `NORMAL` mode will put the speed to 1.

My recommendation is to use your own custom card to control the de fans.

## Custom HA Cards Example

Here I am using [lovelace-mushroom](https://github.com/piitaya/lovelace-mushroom), [lovelace-card-mod](https://github.com/thomasloven/lovelace-card-mod) and [service-call-tile-feature](https://github.com/Nerwyn/service-call-tile-feature). On a Sections board.

If you are using the configuration written above, you just need to search and replace `fan_1` with the corresponding fan number to get more cards.

<div class="text-center">
  <img src="images/ha-dashboard.webp" alt="Home Assistant Dashboard" />
</div>


```yaml
type: grid
cards:
  - type: heading
    heading_style: title
    heading: Heading
    card_mod:
      style: |
        .container .title {
           color: transparent !important;
        }

        .container .title:after {
          content: "{{ states('sensor.esp_ventilation_controller_label_fan_1') }}";
          position: absolute;
          left: 0px;
          color: var(--primary-text-color);
        }
  - type: custom:mushroom-fan-card
    entity: fan.esp_ventilation_controller_fan_1
    show_percentage_control: false
    fill_container: false
    icon_animation: true
    collapsible_controls: false
    secondary_info: last-updated
    primary_info: state
    grid_options:
      columns: 3
      rows: 2
    layout: vertical
    hold_action:
      action: more-info
    card_mod:
      style:
        mushroom-state-info$: |
          .primary {
            color: transparent !important;
            position: relative;
          }
          .primary:after {
            content: "{{ 'Unknown' if state_attr('fan.esp_ventilation_controller_fan_1', 'percentage') | int(0) == 255 else 'Off' if states('fan.esp_ventilation_controller_fan_1') == 'off' else 'On' }}";
            position: absolute;
            left: 0px;
            color: var(--primary-text-color);
            width:100%
          }
  - features:
      - style: dropdown
        type: fan-preset-modes
      - type: custom:service-call
        entries:
          - type: slider
            entity_id: fan.esp_ventilation_controller_fan_1
            range:
              - 9.09
              - 54.55
            tap_action:
              action: perform-action
              target:
                entity_id:
                  - fan.esp_ventilation_controller_fan_1
              confirmation: false
              perform_action: fan.set_percentage
              data:
                percentage: |
                  {{ value | int  }} 
            step: 9.090909090909092
            unit_of_measurement: U
            label: >-
              {% set percentage =
              state_attr('fan.esp_ventilation_controller_fan_1', 'percentage') |
              float(0) %}

              {% if percentage == 0 %}
                  Off
              {% elif percentage <= (6 / 11 * 100) %}
                  Speed {{ ((percentage / 100) * 10) | round(0, 'floor') + 1 }}
              {% else %}
                  Special Mode
              {% endif %}
            value_attribute: percentage
            autofill_entity_id: true
            thumb: flat
    type: tile
    entity: fan.esp_ventilation_controller_fan_1
    grid_options:
      columns: 9
      rows: 3
    icon_tap_action:
      action: none
    tap_action:
      action: none
    hold_action:
      action: none
    card_mod:
      style: |
        ha-card {
         height: auto !important;
        }
        .container .content {
          padding-bottom: 4px;  
        }
        .container .content > *{
          display:none;
        }
column_span: 1

```
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
Each Fan Pair can have a level from `0` to `11`. There, just `0` to `6` are _"real"_ levels, `7` to `11` are _"special"_ levels. A Level of `255` means there is not fan in that pair.

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
- `42625` is the checksum (probably).

### SET Response Message
Sadly (IMHO) the SEC-TOUCH just sends an `ACK` message after receiving a SET message and sometimes it takes a couple of seconds for the SEC-TOUCH screen to update the new value, so the best we can do is to wait for the `ACK` message and then send a `GET` message to do a security sync of the new value.

```log
Byte received: 2   // STX 0x02
Byte received: 6   // ACK 0x06
Byte received: 10  // ETX 0x0A
```

# Unknown Messages

## Id 170
Sometimes we get this message (id `170`):
```
32	170	0	2137
```

At least when captured, it was a "response" for id `174`

```log
[13:21:28][D][sec-touch-uart:400]:   [process_data] Incoming message buffer for get task(32800) targetType LEVEL and id 174
[13:21:28][D][sec-touch-uart:403]:   [process_data] buffer 
32	170	0	2137
[13:21:28][D][sec-touch-uart:422]:   [process_data] incoming.returned_id: 170	, incoming.returned_value: 0	
[13:21:28][D][sec-touch-uart:437]:   [process_data] returned_id: 170
[13:21:28][D][sec-touch-uart:441]:   [process_data] returned_value: 0
[13:21:28][D][sec-touch-uart:448]:   [process_data] queue is not empty continue
[13:21:28][E][sec-touch-uart:458]:   [process_data] ID mismatch. Task Failed
[13:21:28][D][sec-touch:303]: [FAILED Task] targetType "LEVEL" and id "174"
```

This is the configuration log:
```log
[13:27:23][C][sec-touch:025]: SEC-Touch:
[13:27:23][C][sec-touch:026]:   total_register_fans: 3
[13:27:23][C][sec-touch:030]:   - Fan Property ID: 173
[13:27:23][C][sec-touch:030]:   - Fan Property ID: 174
[13:27:23][C][sec-touch:030]:   - Fan Property ID: 177
```


# Update submodules
```
git submodule update --remote --merge
```