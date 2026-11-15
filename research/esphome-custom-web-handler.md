# Serving a custom HTML page + JSON (+ SSE) from an ESPHome external component on ESP32

Research for [#23](https://github.com/distante/esphome-components/issues/23) (part of [#22](https://github.com/distante/esphome-components/issues/22)). Facts only; no design decision.

**Sources.** Primary source is the vendored submodule `esphome/` at commit `4b8e06b5` (`__version__ = "2026.6.0-dev"`, `esphome/esphome/const.py:7`). All `file:line` pointers below are relative to `esphome/esphome/components/` in that commit unless stated otherwise. Upstream comparison: latest release `2026.10.2` and `dev` (`2026.12.0-dev`), read via the GitHub API on 2026-11-15. ESP-IDF facts come from `esp_http_server.h` in ESP-IDF 5.5.4 (the ESP-IDF version ESPHome recommends, `esp32/__init__.py:758`) and from the [ESP-IDF HTTP server docs](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/protocols/esp_http_server.html). User docs: [esphome.io/components/web_server](https://esphome.io/components/web_server/) and [esphome.io/components/prometheus](https://esphome.io/components/prometheus/).

## TL;DR

- **Supported path:** depend on `web_server_base` (`AUTO_LOAD`), make the component an `AsyncWebHandler`, and in `setup()` call `base->init(); base->add_handler(this);`. `prometheus` works exactly this way and does not need `web_server:`.
- **On ESP32, Arduino and ESP-IDF builds use the same server.** It is ESPHome's own `web_server_idf` shim over ESP-IDF `esp_http_server`, not the ESPAsyncWebServer library. So the API is the shim's subset: no `server->on(...)`, no `beginResponse_P`, no `AsyncJsonResponse`, and no ESPAsyncWebServer `AsyncEventSource`.
- **Auth:** `add_handler()` wraps your handler in basic auth only when `web_server: auth:` is configured. `add_handler_without_auth()` skips the wrap. With no `web_server:` block there is no auth.
- **Static asset:** a gzipped `constexpr uint8_t[]` served with `beginResponse(200, "text/html", data, size)` plus a `Content-Encoding: gzip` header. On ESP32 there is no copy to RAM, and the flash cost is the gzip size. Example: the captive portal page is 1,462 B gzip.
- **SSE:** the ESP32 `AsyncEventSource` only compiles with `USE_WEBSERVER` and is hard-wired to `web_server::WebServer`. An external component cannot reuse it for its own stream. It can either poll a JSON endpoint (simplest), or hand-roll SSE on the raw `httpd_req_t` the way `AsyncEventSourceResponse` does.
- **Biggest gotchas:**
  - Handlers run on the **httpd task**, not the ESPHome main loop.
  - There is a single httpd task, so a slow handler blocks every client.
  - Response bodies built with `AsyncResponseStream` are held entirely in heap.
  - The vendored shim maps every status other than 200, 404 and 409 to **500**.
  - In the vendored version, POST bodies that are not form-encoded are **not read**.
  - The first handler whose `canHandle()` returns true wins, and `web_server` owns `/`.

## 1. How the server is wired

### `web_server_base` is the shared server

- `web_server_base/__init__.py:14-19`: `AUTO_LOAD` is `["web_server_idf"]` on ESP32 and `["async_tcp"]` on other Arduino targets.
- `web_server_base/__init__.py:55-58`: on ESP32 it returns early, so the ESPAsyncWebServer library (`:82`, `ESP32Async/ESPAsyncWebServer 3.9.6`) is **only** pulled in for non-ESP32 Arduino targets.
- `web_server_base/web_server_base.h:9-14`: under `USE_ESP32` it includes `web_server_idf/web_server_idf.h`, otherwise `<ESPAsyncWebServer.h>`. `PlatformString` is `std::string` on ESP32 and `String` on other Arduino targets (`:16-20`).
- So for ESP32, the framework choice (Arduino or ESP-IDF) **does not change** the HTTP server. Both use `esp_http_server` through the shim. The Arduino-only code paths in `web_server` (`DeferredUpdateEventSource`, `web_server/web_server.h:95-110`) are `#if !defined(USE_ESP32) && defined(USE_ARDUINO)`, meaning ESP8266, RP2040 and LibreTiny only.
- `web_server_base` has no YAML options of its own besides `id` (`web_server_base/__init__.py:40-47`). It registers 1 listening socket (`:28-37`).
- The port defaults to 80 (`web_server_base.h:139`). Only `web_server` sets it from YAML (`web_server/__init__.py:301`). An external component could call `set_port()` itself.
- The global pointer is `web_server_base::global_web_server_base` (`web_server_base/__init__.py:53`, `web_server_base.cpp:6`).

### `WebServerBase` API (vendored)

`web_server_base/web_server_base.h:91-145`, `web_server_base.cpp:8-22`:

| Member | Behaviour |
|---|---|
| `init()` | First call creates `AsyncWebServer(port_)`, adds the default header `Access-Control-Allow-Origin: *`, calls `begin()`, then registers every handler queued so far. Later calls only bump a refcount (`:93-109`). |
| `deinit()` | Decrements the refcount. At 0 it deletes the server (`:110-116`). Only `captive_portal::end()` calls it (`captive_portal.h:39`). |
| `add_handler(h)` | If `USE_WEBSERVER_AUTH` is defined and a username is set, wraps `h` in `AuthMiddlewareHandler`, then calls `add_handler_without_auth` (`.cpp:8-15`). |
| `add_handler_without_auth(h)` | Pushes `h` to `handlers_`, and to the live server if `init()` already ran (`.cpp:17-22`). The header comment says to use it only for intentionally unauthenticated endpoints (`.h:125-132`). |
| `set_port` / `get_port` | `.h:134-135` |

The handler interface is `web_server_idf.h:243-257` on ESP32. It has these virtuals: `canHandle(req) const`, `handleRequest(req)`, `handleUpload`, `handleBody` and `isRequestHandlerTrivial`. The shim's `AsyncWebServer` exposes only `addHandler`, `onNotFound`, `begin`, `end` and `get_server()`, which returns the raw `httpd_handle_t` (`web_server_idf.h:210-241`).

### Dispatch on ESP32

`web_server_idf/web_server_idf.cpp:120-162`, `AsyncWebServer::begin()`:

- Uses `HTTPD_DEFAULT_CONFIG()` with `stack_size + 256` (`:126-127`).
- Uses a wildcard `uri_match_fn` that always returns true (`:129`).
- Turns on `lru_purge_enable = true` (`:134`) and sets a custom `close_fn` (`:136`).
- Registers exactly 3 catch-all URI handlers: GET, POST and OPTIONS (`:138-160`). PUT, DELETE and PATCH never reach an ESPHome handler.

The ESP-IDF 5.5.4 defaults behind that (`esp_http_server.h:53-82`) are:

- `stack_size 4096`, so ESPHome's httpd task stack is **4,352 B** in the vendored version.
- `max_open_sockets 7`, which the vendored version does not override.
- `max_resp_headers 8`.
- `recv_wait_timeout` and `send_wait_timeout` of 5 s.
- `task_priority tskIDLE_PRIORITY+5`.

`request_handler_()` (`:218-237`) walks `handlers_` in registration order. The **first** handler whose `canHandle()` returns true gets `handleRequest()`. If none matches, `onNotFound` runs, else the request ends with `ESP_ERR_NOT_FOUND`. `AsyncWebServerRequest` is a stack object built per request (`:214`, `:220`).

## 2. How existing components do it

| Component | Registration | Path matching | Response style |
|---|---|---|---|
| **prometheus** (no `web_server` needed) | Python: `AUTO_LOAD = ["web_server_base"]`, `cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(WebServerBase)`, passes the parent to the constructor (`prometheus/__init__.py:8`, `:26-28`, `:40-44`). C++: `setup(){ base_->init(); base_->add_handler(this); }`, priority `WIFI - 1.0f` (`prometheus/prometheus_handler.h:55-62`). | `GET` + `url_to(buf) == "/metrics"` on ESP32 (`prometheus_handler.h:42-51`) | `beginResponseStream("text/plain; ...")`, many `print()` calls, then `send(stream)` (`prometheus_handler.cpp:8`, `:109`) |
| **captive_portal** | `start()` calls `base_->init()` then `add_handler_without_auth(this)` (`captive_portal/captive_portal.cpp:70-74`). Setup priority `WIFI + 1.0f` (`:129-132`). | While active, claims **every GET** (`captive_portal.h:46-51`) | Gzipped flash page: `beginResponse(200,"text/html",INDEX_GZ,sizeof(INDEX_GZ))` + `addHeader("Content-Encoding","gzip")` (`captive_portal.cpp:115-125`). JSON built by hand with `beginResponseStream("application/json")` + `printf` (`:12-47`). Defers NVS writes to the main loop "to avoid NVS operations from HTTP thread" (`:58-62`). |
| **web_server OTA** | `global_web_server_base->add_handler(new OTARequestHandler(this))` in `setup()` (`web_server/ota/ota_web_server.cpp:243-254`) | `POST /update` (`:34-58`) | `beginResponse(200,"text/plain",msg)` (`:223-240`) |
| **web_server** | `setup()`: `base_->init()`. On ESP32 it calls `add_handler(&events_)` and then `add_handler(this)` (`web_server/web_server.cpp:398-415`). Priority `WIFI - 1.0f` (`:455`). | Claims `/`, `/0.css`, `/0.js`, the private-network-access `OPTIONS` request, and `/<domain>/...` for every compiled entity domain (`web_server.cpp:2325-2470`). `/events` is claimed by the separate `AsyncEventSource` on ESP32 (`web_server_idf.h:333-338`). | Index from flash (`:458-480`). Entity JSON via `request->send(200,"application/json",data.c_str())` (e.g. `:624-637`). |

## 3. Request and response API on ESP32 (the shim)

`web_server_idf/web_server_idf.h:109-206`:

- **Request accessors:**
  - `method()`
  - `url_to(std::span<char, URL_BUF_SIZE>)`: URL without the query string, URL-decoded. `URL_BUF_SIZE = CONFIG_HTTPD_MAX_URI_LEN + 1` (`:116-119`).
  - `url()` is `ESPDEPRECATED` and slated for removal in 2026.9.0 (`:120-125`). It is gone in upstream 2026.10.2.
  - `hasParam` / `getParam`, `hasArg` / `arg` (query string and form body), `get_header`, `hasHeader`, `contentLength`.
  - `operator httpd_req_t *()` (`:188`), which gives the raw ESP-IDF request.
- **Responses:**
  - `send(code, content_type, const char *content)`: one `httpd_resp_send` (`:140-147`).
  - `beginResponse(code, type)` (empty), `beginResponse(code, type, std::string)` (copies the string into the response object), and `beginResponse(code, type, const uint8_t *data, size_t size)` (**no copy**, a pointer into flash, `AsyncWebServerResponseProgmem`, `:96-107`, `:161-166`).
  - `beginResponseStream(type)`: an `AsyncResponseStream` that **appends everything into a `std::string`** (`:79-94`).
  - `response->addHeader(name, value)` calls `httpd_resp_set_hdr` directly (`web_server_idf.cpp:444-446`). The strings must stay valid until the response is sent, so use literals.
  - `send(response)`: a single `httpd_resp_send(data, size)` (`web_server_idf.h:137-139`). The shim has **no chunked-response API**.
- **Status codes:** `init_response_()` maps only 200, 404 and 409. **Every other code becomes `500`** (`web_server_idf.cpp:272-288`). It also adds `Accept-Ranges: none` and the default `Access-Control-Allow-Origin: *` (`:290-297`).
- **POST bodies** (`web_server_idf.cpp:164-216`):
  - `Content-Length` is required (411 otherwise).
  - `application/x-www-form-urlencoded` bodies are read into a `std::string` and capped at `CONFIG_HTTPD_MAX_REQ_HDR_LEN`, which `web_server_idf/__init__.py:14` sets to 1024 B (400 above that).
  - `multipart/form-data` is only handled with `USE_WEBSERVER_OTA`.
  - **Any other content type, for example `application/json`, logs a warning and falls through to the GET dispatch without reading the body** (`:185-189`). A handler that wants a JSON body has to call `httpd_req_recv` on the raw `httpd_req_t` itself. `handleBody()` is never called on ESP32 in this version.
- **JSON helpers:** `components/json` provides `json::JsonBuilder` and `json::build_json`, which return a `SerializationBuffer<640>` that uses the stack first (`json/json_util.h:21`, `:153-154`). `web_server` uses these. Using them means adding `json` to `AUTO_LOAD`, as `web_server` does (`web_server/__init__.py:41`).

## 4. With or without `web_server:`

| Aspect | Without `web_server:` | With `web_server:` |
|---|---|---|
| Custom handler works | Yes. `prometheus` only auto-loads `web_server_base` (`prometheus/__init__.py:8`). The esphome.io prometheus page shows a `web_server:` block in its example but does not say it is required. | Yes |
| Port | 80, fixed unless the component calls `set_port()` | `web_server: port:` (`web_server/__init__.py:184`, `:301`) |
| Path conflicts | None, so `/` is free | `/`, `/events`, `/0.css`, `/0.js` and `/<entity-domain>/...` are taken. A custom handler registered **after** `web_server` never sees those paths. Registered **before**, it shadows them. Order follows setup priority, then registration order. |
| Auth available | No. `USE_WEBSERVER_AUTH` is only defined by `web_server: auth:` (`web_server/__init__.py:322-325`). | Yes, via `add_handler()` |
| `AsyncEventSource` class compiled | No. It is inside `#ifdef USE_WEBSERVER` (`web_server_idf.h:259-368`). | Yes, but bound to `WebServer` (see §7) |
| Sockets | Component must register its own (see §8) | `web_server` reserves 5 TCP (`web_server/__init__.py:143-152`) |

`captive_portal`, when active in AP fallback mode, claims **every GET** (`captive_portal.h:46-51`). Whether a custom path still works during the portal depends on whether the custom handler was registered first. `captive_portal::start()` runs at runtime, while custom handlers register in `setup()`.

## 5. Auth inheritance

- **Vendored version:**
  - `add_handler()` wraps the handler in `internal::AuthMiddlewareHandler` when `USE_WEBSERVER_AUTH` is defined and the username is non-empty (`web_server_base.cpp:8-15`, `web_server_base.h:54-86`).
  - The wrapper checks `request->authenticate(user, pass)`, and on failure calls `requestAuthentication()`, which sends a 401 (`web_server_idf.cpp:305-374`). It does this for `handleRequest`, `handleUpload` and `handleBody`. `canHandle` is **not** gated: path matching runs before auth.
  - The credentials come from `web_server: auth:` (`web_server/__init__.py:322-325`). The scheme is HTTP Basic.
  - Result: a component that uses `add_handler()` **inherits** `web_server` basic auth automatically. With `add_handler_without_auth()` it never does. With no `web_server: auth:` there is nothing to inherit.
- **Upstream differences:**
  - **2026.10.2:** adds `auth: type: basic|digest`. The docs say the default "will change to `digest` in ESPHome `2027.1.0`". Credentials become `const char*` on ESP32. The shim's `requestAuthentication()` loses its `realm` parameter.
  - **2026.10.2 also adds `allowed_origins`** for `web_server`. esphome.io: "By default, the server rejects cross-origin browser requests", and requests without an `Origin` header are always allowed.
  - **dev (2026.12.0-dev):** `AuthMiddlewareHandler` becomes `GuardMiddlewareHandler` in `web_server_base`. It is applied to **every** `add_handler()` handler and enforces the origin policy **and** auth. A custom handler added with `add_handler()` would then reject cross-origin browser requests unless the origin is allowed. Same-origin requests (a page served by the device calling its own JSON) are unaffected. `add_handler_without_auth()` skips both checks.

## 6. Embedding a static HTML/JS asset

- **The pattern** is a `constexpr uint8_t NAME[] PROGMEM = {0x1f,0x8b,...}` holding gzip bytes, served with `beginResponse(200, "text/html", NAME, sizeof(NAME))` + `addHeader("Content-Encoding", "gzip")` (`captive_portal.cpp:115-125`, `web_server.cpp:458-468`).
  - On ESP32, `PROGMEM` is a no-op and the array lives in flash `.rodata`. Reads are direct pointer dereferences (`core/hal.h:40-48`).
  - `AsyncWebServerResponseProgmem` keeps only the pointer and size, so serving it copies **nothing** into RAM. `httpd_resp_send` streams from flash.
- **Two ways to generate it:**
  1. **Pre-generated header** checked into the repo. `captive_portal/captive_index.h` and `web_server/server_index_v{2,3}.h` are "Generated from https://github.com/esphome/esphome-webserver". They provide `INDEX_GZ` or `INDEX_BR`, chosen by a `compression: gzip|br` option.
  2. **At codegen time in Python.** `web_server/__init__.py:274-285` (`add_resource_as_progmem`) reads a file, runs `gzip.compress()`, and emits `cg.add_global(cg.RawExpression("constexpr uint8_t ESPHOME_WEBSERVER_<NAME>[n] PROGMEM = {...}"))` plus a `_SIZE` constant. `web_server` uses this for `css_include` / `js_include` (`:326-335`). An external component's `to_code()` can do the same thing with its own file (`CORE.relative_config_path` or a path next to `__init__.py`).
- **Size cost** is roughly the compressed byte count in flash, plus nothing in RAM. Measured in the vendored tree:

  | Asset | gzip | Brotli |
  |---|---|---|
  | Captive portal page (`captive_index.h:9`, `:89`) | 1,462 B | 1,110 B |
  | `web_server` v3 `local: true` bundle (`server_index_v3.h:12`, `:4110`) | 77,803 B | 67,967 B |

  esphome.io notes that Brotli is "about 10% smaller". All browsers accept `Content-Encoding: br` only over HTTPS. This is general browser behaviour, not stated in ESPHome sources. ESPHome nevertheless defaults `captive_portal` and `web_server` `compression` to `gzip` in this version (`captive_portal/__init__.py`, `web_server/__init__.py:208`).
- **`web_server` `local:` mode** (`USE_WEBSERVER_LOCAL`, `web_server/__init__.py:337-338`) is only about the stock UI bundle. It is irrelevant to a custom page, except as a flash-budget reference (about 68 to 78 kB).

## 7. Server-Sent Events vs polling

### What exists on ESP32 (vendored)

- `web_server_idf::AsyncEventSource` and `AsyncEventSourceResponse` (`web_server_idf.h:259-368`, `web_server_idf.cpp:483-900`) are only compiled `#ifdef USE_WEBSERVER`.
- The constructor takes a `web_server::WebServer *` (`web_server_idf.h:329`).
- On connect, each session sends `web_server`'s config JSON, its sorting groups, and **iterates all entities** through `ListEntitiesIterator` (`web_server_idf.cpp:598-621`).
- `message_generator_t` is typed against `WebServer` (`web_server_idf.h:263`).
- `WebServer` is `final` (`web_server.h:191`), and its `events_` member is `protected` (`web_server.h:577-582`). There is **no public API** to push custom events onto `/events`.
- Conclusion: an external component **cannot reuse the shipped SSE class** for its own stream without dragging in `web_server` and its entity stream. This still holds in upstream 2026.10.2 and dev, where the constructor is `AsyncEventSource(StringRef url, WebServer *ws)`.

### How the shipped SSE works (a template for hand-rolling)

`web_server_idf.cpp:483-494`, `:578-596`, `:659-859`:

1. **In `handleRequest`, on the httpd task:**
   - Set status 200, `Content-Type: text/event-stream`, `Cache-Control: no-cache`, `Connection: keep-alive` and the default headers.
   - `httpd_resp_send_chunk(req, "\r\n", 2)` sends the headers and starts chunked mode.
   - Store a context in `req->sess_ctx`, with `req->free_ctx` as the close callback.
   - Save `req->handle` and `httpd_req_to_sockfd(req)`.
   - `httpd_sess_set_send_override(hd, fd, nonblocking_send)`.
   - **Return immediately**, so the httpd task is not blocked.
2. **From the main loop:** format each event as a hand-built HTTP chunk (`<8-hex-len>\r\n` + `event:` / `data:` lines + `\r\n`). Write it with `httpd_socket_send(hd, fd, ...)` and keep any unsent tail for the next loop. After 2,500 consecutive would-block sends (about 20 s) the session is dropped (`web_server_idf.h:319`).
3. **Close:** `free_ctx` (on the httpd task) only zeroes the stored fd atomically. The main loop deletes the session later, which avoids races (`:623-631`, `:499-523`).
4. New sessions are handed from the httpd task to the main loop through a mutex-guarded pending list plus `enable_loop_soon_any_context()` (`:483-494`, `:525-551`).

Other raw ESP-IDF options exist and are not used by ESPHome:

- `httpd_req_async_handler_begin()` / `httpd_req_async_handler_complete()` copy a request for use on another task (`esp_http_server.h:848`). The ESP-IDF docs describe these for "multiple long-running simultaneous requests".
- WebSockets would need a URI registered with `is_websocket` and `CONFIG_HTTPD_WS_SUPPORT`. ESPHome registers only its 3 catch-all handlers.

### Trade-offs (facts, no decision)

| | Polling a JSON endpoint | Hand-rolled SSE |
|---|---|---|
| Code needed | Plain `AsyncWebHandler` using the shim API only | Raw `httpd_*` calls, chunk framing, main-loop pump, close handling. About 150 lines, modelled on `AsyncEventSourceResponse`. |
| Socket use | Short-lived. Keep-alive reuse depends on the browser. LRU purge reclaims sockets. | One socket held per open tab for as long as it is open |
| httpd task | Busy only while a response is built | Busy only in the initial handshake. Sends come from the main loop. |
| RAM | One response body per request (heap if `AsyncResponseStream`) | Per session: a context object plus a pending-send buffer that grows to the largest unsent event |
| Latency | Poll interval | Push on change |
| Auth / origin | Same as any handler | Same wrapper at handshake time only |
| Framework coverage | Identical on Arduino and ESP-IDF for ESP32 | Same, since both use `esp_http_server` |

## 8. RAM, concurrency, large responses, other gotchas

- **Threading:**
  - All `canHandle` and `handleRequest` calls run on the **ESP-IDF httpd task** (priority `tskIDLE_PRIORITY+5`), concurrently with ESPHome's main loop. They do not run in `loop()`.
  - `captive_portal` explicitly `defer()`s work to the main loop (`captive_portal.cpp:58-62`), and `web_server` does the same for entity actions (e.g. `web_server.cpp:756`).
  - Reading plain fields from the httpd task is what `web_server` does for GET state (`web_server.cpp:624-637`). Anything that mutates state, or reads structures the main loop is mutating (vectors, strings, the sniffer's capture buffer), needs `defer()` or a lock.
- **Concurrency:**
  - `esp_http_server` has one server task. The ESP-IDF docs say sockets are "selected in a round robin fashion in the server task loop", and a handler runs to completion before the next request.
  - So a slow `handleRequest` (big JSON build, blocking send) stalls **every** HTTP client, `web_server`'s included.
  - Blocking sends wait up to `send_wait_timeout` = 5 s.
- **Sockets:**
  - Vendored: `max_open_sockets` is the ESP-IDF default of **7**, with LRU purge on (`web_server_idf.cpp:126-136`).
  - The lwIP socket pool (`CONFIG_LWIP_MAX_SOCKETS`) is sized from what components register with `socket.consume_sockets(n, name)` during validation, with a floor of 10 (`socket/__init__.py:48-68`, `esp32/__init__.py:383-384`, `:1621-1670`).
  - An external component that expects its own clients should call `socket.consume_sockets(...)` in its schema, the way `web_server` (5) and `captive_portal` (3 TCP + 1 UDP) do.
  - Upstream dev adds `web_server_base.add_httpd_clients(count, consumer)`, which does that **and** raises `max_open_sockets` (`WEB_SERVER_MAX_OPEN_SOCKETS`).
- **RAM per request:**
  - The `AsyncWebServerRequest` lives on the stack. Response objects are `new`ed and freed in `~AsyncWebServerRequest` (`web_server_idf.cpp:240-245`). Parsed params are heap objects.
  - The body cost depends on the response style:
    - Flash response: 0 B for the body.
    - `beginResponse(..., std::string)`: one heap copy of the body.
    - `AsyncResponseStream`: the whole body grows in one heap `std::string` (repeated reallocs).
  - The httpd stack is 4,352 B (vendored). `url_to` alone needs a `CONFIG_HTTPD_MAX_URI_LEN + 1` stack buffer, which is 513 B with the ESP-IDF default of 512. Upstream 2026.10.2 and later set the stack to 4,512 B.
  - The ESPHome guidelines vendored in `esphome/CLAUDE.md` §7 treat heap allocation after `setup()` as a long-term reliability bug, because of fragmentation.
- **Large and chunked responses:** the shim has no streaming API (`send(response)` is a single `httpd_resp_send`). For bodies too big to buffer, use the raw request:
  - Call `httpd_resp_set_status`, `httpd_resp_set_type` and `httpd_resp_set_hdr` yourself. The protected `init_response_` will not run, so the `Access-Control-Allow-Origin` default header is not added unless you add it.
  - Call `httpd_resp_send_chunk(req, buf, len)` repeatedly, then end with `httpd_resp_send_chunk(req, nullptr, 0)`, which ESP-IDF requires.
  - This is how the SSE code starts its stream (`web_server_idf.cpp:585`).
  - Each chunk blocks the httpd task until it is sent or 5 s pass.
- **Other limits and quirks:**
  - **Header limit:** `max_resp_headers = 8` per response (ESP-IDF default). ESPHome already uses 2 (`Accept-Ranges` and `Access-Control-Allow-Origin`).
  - **Status codes:** only 200, 404 and 409 survive; anything else becomes 500 (vendored). Upstream dev adds 204, 400, 401, 403 and 422.
  - **Methods:** only GET, POST and OPTIONS reach handlers.
  - **POST JSON bodies:** not read in the vendored version (see §3). Upstream 2026.10.2 and later deliver them to `handleBody()` through `handle_raw_body_`.
  - **CORS:** vendored sends `Access-Control-Allow-Origin: *` on every shim response (`web_server_base.h:102`).
- **Setup order:** `init()` before or after `add_handler()` both work, since queued handlers are added in `init()`. Components that serve HTTP use setup priority `WIFI - 1.0f` (`prometheus_handler.h:59-62`, `web_server.cpp:455`).
- **Non-ESP32 targets** (not needed here) use the real ESPAsyncWebServer, with `beginResponse_P`, `url()` returning `String`, and a different SSE implementation (`web_server.h:95-180`). Components that support both branch with `#ifdef USE_ESP32` (for example `prometheus_handler.h:44-50`).

## 9. Upstream drift summary (vendored 2026.6.0-dev → 2026.10.2 → dev 2026.12.0-dev)

| Area | Vendored | 2026.10.2 | dev |
|---|---|---|---|
| `url()` on ESP32 | deprecated | removed, use `url_to()` | removed |
| Auth | basic only | `type: basic\|digest` (default switches to digest in 2027.1.0) | same |
| Cross-origin | `ACAO: *`, no check | `web_server: allowed_origins`, default rejects cross-origin to web_server endpoints | origin guard moved into `web_server_base::add_handler()`, so it applies to custom handlers too |
| Non-form POST body | not read, falls to GET dispatch | `handle_raw_body_` calls `handleBody()` | same |
| httpd stack | 4096+256 | 4512 | 4512 |
| `max_open_sockets` | ESP-IDF default 7 | — | `WEB_SERVER_MAX_OPEN_SOCKETS` via `add_httpd_clients()` |
| Status codes | 200/404/409 else 500 | (not checked) | + 204/400/401/403/422 |
| `send(response)` | inline `httpd_resp_send` | — | applies default headers at send time |
| `AsyncEventSource` | `USE_WEBSERVER`-only, needs `WebServer*` | same (`StringRef url`) | same |

"—" means not checked. Code written against the vendored shim that uses `url_to()`, `beginResponse` and `addHeader`, and does not depend on 500-for-everything, compiles against the newer versions as far as the diffs show. The API surface used by `prometheus` and `captive_portal` is unchanged.

## Uncertain / not verified

- No firmware was built or run. RAM figures are from code reading, not heap measurements.
- `httpd_req_async_handler_begin` exists in ESP-IDF 5.5.4 (`esp_http_server.h:848`), but nothing in ESPHome uses it, and its interaction with the shim is untested.
- The `2026.10.2` column was only diffed for `web_server_base` and `web_server_idf` (plus a grep of `web_server/__init__.py`). Cells marked "—" were not checked.
- The claim that browsers accept Brotli only over HTTPS is general browser behaviour, not stated in an ESPHome source.
