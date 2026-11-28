import esphome.config_validation as cv

# The v1.4.0 text sensor platform. Any entry fails with this message, whatever its keys.
CONFIG_SCHEMA = cv.invalid(
    """sec_touch_sniffer moved out of text_sensor in v2.0.0.
Replace the text_sensor entry with:

  sec_touch_sniffer:
  web_server_base:   # or web_server:

scan_switch, scan_start/scan_end and web_page are gone:
start scans and pick the range on the page at /sniffer.
https://github.com/distante/esphome-components/blob/main/components/sec_touch/readme.md#sniffer"""
)
