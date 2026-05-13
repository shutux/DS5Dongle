//
// WiFi AP mode web configuration server for DS5Dongle
//

#include "wifi_server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

#include "config.h"
#include "bt.h"
#include "platform.h"
#include "dhcpserver.h"
#include "dnsserver.h"

#define WIFI_AP_SSID    "Oh-My-Controller"
#define WIFI_AP_PASS    NULL  // Open network
#define HTTP_PORT       80
#define MAX_REQUEST_SIZE 1024

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static struct tcp_pcb *http_pcb = nullptr;

// ── Minimal embedded HTML config page ──────────────────────────────
static const char INDEX_HTML[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DS5Dongle Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui;background:#1a1a2e;color:#e0e0e0;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:1.3em;margin-bottom:16px;color:#fff}
.f{margin-bottom:12px}
label{display:block;font-size:.85em;color:#aaa;margin-bottom:4px}
input,select{width:100%;padding:8px;border:1px solid #333;border-radius:6px;background:#16213e;color:#fff;font-size:1em}
input[type=range]{padding:4px 0}
.row{display:flex;gap:8px;margin-top:16px}
button{flex:1;padding:10px;border:none;border-radius:6px;font-size:1em;cursor:pointer;font-weight:600}
.btn-save{background:#0f3460;color:#fff}
.btn-apply{background:#533483;color:#fff}
.btn-recon{background:#e94560;color:#fff}
.msg{text-align:center;padding:8px;margin-top:8px;border-radius:6px;font-size:.9em;display:none}
.ok{background:#1b4332;display:block}.err{background:#6b1d1d;display:block}
.sw{position:relative;display:inline-block;width:44px;height:24px}
.sw input{opacity:0;width:0;height:0}
.sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#333;border-radius:24px;transition:.3s}
.sl:before{content:"";position:absolute;height:18px;width:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}
input:checked+.sl{background:#533483}
input:checked+.sl:before{transform:translateX(20px)}
.rv{text-align:right;font-size:.85em;color:#7ec8e3;min-width:3em}
.rr{display:flex;align-items:center;gap:8px}
</style></head><body>
<h1>DS5Dongle Config</h1>
<div id="app">Loading...</div>
<script>
const $ = s => document.querySelector(s);
const F = [
  {k:'haptics_gain',l:'Haptics Gain',t:'range',min:1,max:2,step:0.1},
  {k:'speaker_volume',l:'Speaker Volume',t:'range',min:1,max:2,step:0.1},
  {k:'inactive_time',l:'Inactive Time (min)',t:'range',min:10,max:60,step:1},
  {k:'disable_inactive_disconnect',l:'Disable Auto Disconnect',t:'bool'},
  {k:'disable_pico_led',l:'Disable Pico LED',t:'bool'},
  {k:'polling_rate_mode',l:'Polling Rate',t:'select',opts:['250 Hz','500 Hz','Real-time']},
  {k:'audio_buffer_length',l:'Audio Buffer Length',t:'range',min:16,max:128,step:1},
  {k:'controller_mode',l:'Controller Mode',t:'select',opts:['DualSense','DualSense Edge','Switch Pro']}
];
let cfg={};
async function load(){
  try{
    const r=await fetch('/api/config');
    cfg=await r.json();
    render();
  }catch(e){msg('Load failed','err')}
}
function render(){
  let h='';
  F.forEach(f=>{
    h+='<div class="f"><label>'+f.l+'</label>';
    if(f.t==='range'){
      h+='<div class="rr"><input type="range" min="'+f.min+'" max="'+f.max+'" step="'+f.step+'" value="'+cfg[f.k]+'" oninput="upd(\''+f.k+'\',parseFloat(this.value));this.nextElementSibling.textContent=this.value"><span class="rv">'+cfg[f.k]+'</span></div>';
    }else if(f.t==='bool'){
      h+='<label class="sw"><input type="checkbox" '+(cfg[f.k]?'checked':'')+' onchange="upd(\''+f.k+'\',this.checked?1:0)"><span class="sl"></span></label>';
    }else if(f.t==='select'){
      h+='<select onchange="upd(\''+f.k+'\',parseInt(this.value))">';
      f.opts.forEach((o,i)=>h+='<option value="'+i+'"'+(cfg[f.k]===i?' selected':'')+'>'+o+'</option>');
      h+='</select>';
    }
    h+='</div>';
  });
  h+='<div class="row"><button class="btn-save" onclick="save()">Save to Flash</button><button class="btn-recon" onclick="recon()">Reconnect</button></div><div id="msg" class="msg"></div>';
  $('#app').innerHTML=h;
}
function upd(k,v){cfg[k]=v;send()}
async function send(){
  try{await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});msg('Updated','ok')}catch(e){msg('Error','err')}
}
async function save(){
  try{await fetch('/api/save',{method:'POST'});msg('Saved!','ok')}catch(e){msg('Error','err')}
}
async function recon(){
  try{await fetch('/api/reconnect',{method:'POST'});msg('Reconnecting...','ok')}catch(e){msg('Error','err')}
}
function msg(t,c){const m=$('#msg');m.textContent=t;m.className='msg '+c;setTimeout(()=>m.style.display='none',2000)}
load();
</script></body></html>)rawhtml";

// ── JSON helpers ───────────────────────────────────────────────────

static int config_to_json(char *buf, size_t buflen) {
    const Config_body &c = get_config();
    return snprintf(buf, buflen,
        "{\"haptics_gain\":%.1f,"
        "\"speaker_volume\":%.1f,"
        "\"inactive_time\":%u,"
        "\"disable_inactive_disconnect\":%u,"
        "\"disable_pico_led\":%u,"
        "\"polling_rate_mode\":%u,"
        "\"audio_buffer_length\":%u,"
        "\"controller_mode\":%u}",
        (double)c.haptics_gain,
        (double)c.speaker_volume,
        c.inactive_time,
        c.disable_inactive_disconnect,
        c.disable_pico_led,
        c.polling_rate_mode,
        c.audio_buffer_length,
        c.controller_mode);
}

// Minimal JSON number parser: find "key": and return the value after colon
static bool json_get_float(const char *json, const char *key, float *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    *out = strtof(p, nullptr);
    return true;
}

static bool json_get_u8(const char *json, const char *key, uint8_t *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    *out = (uint8_t)strtoul(p, nullptr, 10);
    return true;
}

static void json_to_config(const char *json) {
    Config_body c = get_config(); // copy current
    json_get_float(json, "haptics_gain", &c.haptics_gain);
    json_get_float(json, "speaker_volume", &c.speaker_volume);
    json_get_u8(json, "inactive_time", &c.inactive_time);
    json_get_u8(json, "disable_inactive_disconnect", &c.disable_inactive_disconnect);
    json_get_u8(json, "disable_pico_led", &c.disable_pico_led);
    json_get_u8(json, "polling_rate_mode", &c.polling_rate_mode);
    json_get_u8(json, "audio_buffer_length", &c.audio_buffer_length);
    json_get_u8(json, "controller_mode", &c.controller_mode);
    set_config(c);
}

// ── HTTP response helpers ──────────────────────────────────────────

static err_t send_response(struct tcp_pcb *pcb, const char *status,
                           const char *content_type, const char *body, int body_len) {
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, content_type, body_len);

    tcp_write(pcb, header, hlen, TCP_WRITE_FLAG_COPY);
    if (body_len > 0 && body) {
        tcp_write(pcb, body, body_len, TCP_WRITE_FLAG_COPY);
    }
    tcp_output(pcb);
    return ERR_OK;
}

static err_t send_json_ok(struct tcp_pcb *pcb, const char *json, int len) {
    return send_response(pcb, "200 OK", "application/json", json, len);
}

static err_t send_ok(struct tcp_pcb *pcb) {
    const char *body = "{\"ok\":true}";
    return send_response(pcb, "200 OK", "application/json", body, strlen(body));
}

static err_t send_404(struct tcp_pcb *pcb) {
    const char *body = "Not Found";
    return send_response(pcb, "404 Not Found", "text/plain", body, strlen(body));
}

// ── HTTP request handler ───────────────────────────────────────────

static void handle_request(struct tcp_pcb *pcb, const char *request, uint16_t len) {
    // Parse method and path
    bool is_get = (strncmp(request, "GET ", 4) == 0);
    bool is_post = (strncmp(request, "POST ", 5) == 0);
    bool is_options = (strncmp(request, "OPTIONS ", 8) == 0);

    if (is_options) {
        // CORS preflight
        char resp[256];
        int n = snprintf(resp, sizeof(resp),
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
        tcp_write(pcb, resp, n, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        return;
    }

    // Extract path (skip method + space)
    const char *path_start = strchr(request, ' ');
    if (!path_start) { send_404(pcb); return; }
    path_start++;
    const char *path_end = strchr(path_start, ' ');
    if (!path_end) { send_404(pcb); return; }
    int path_len = path_end - path_start;

    // Find request body (after \r\n\r\n)
    const char *body = strstr(request, "\r\n\r\n");
    if (body) body += 4;

    // Route: GET /
    if (is_get && path_len == 1 && path_start[0] == '/') {
        send_response(pcb, "200 OK", "text/html; charset=utf-8",
                      INDEX_HTML, sizeof(INDEX_HTML) - 1);
        return;
    }

    // Route: GET /api/config
    if (is_get && path_len == 11 && strncmp(path_start, "/api/config", 11) == 0) {
        char json[256];
        int n = config_to_json(json, sizeof(json));
        send_json_ok(pcb, json, n);
        return;
    }

    // Route: POST /api/config
    if (is_post && path_len == 11 && strncmp(path_start, "/api/config", 11) == 0) {
        if (body && body < request + len) {
            json_to_config(body);
            printf("[WiFi] Config updated via HTTP\n");
        }
        send_ok(pcb);
        return;
    }

    // Route: POST /api/save
    if (is_post && path_len == 9 && strncmp(path_start, "/api/save", 9) == 0) {
        config_save();
        printf("[WiFi] Config saved to flash via HTTP\n");
        send_ok(pcb);
        return;
    }

    // Route: POST /api/reconnect
    if (is_post && path_len == 14 && strncmp(path_start, "/api/reconnect", 14) == 0) {
        printf("[WiFi] Reconnect requested via HTTP\n");
        send_ok(pcb);
        // Schedule reconnect after response is sent
        bt_disconnect();
        platform_detect_start();
        return;
    }

    // Captive portal detection - redirect common probe URLs to /
    if (is_get && (strncmp(path_start, "/generate_204", 13) == 0 ||
                   strncmp(path_start, "/hotspot-detect", 15) == 0 ||
                   strncmp(path_start, "/connecttest", 12) == 0 ||
                   strncmp(path_start, "/ncsi", 5) == 0 ||
                   strncmp(path_start, "/redirect", 9) == 0 ||
                   strncmp(path_start, "/canonical", 10) == 0)) {
        char resp[256];
        int n = snprintf(resp, sizeof(resp),
            "HTTP/1.1 302 Found\r\n"
            "Location: http://192.168.4.1/\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
        tcp_write(pcb, resp, n, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        return;
    }

    send_404(pcb);
}

// ── TCP callbacks ──────────────────────────────────────────────────

static err_t http_close_conn(struct tcp_pcb *pcb) {
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    tcp_close(pcb);
    return ERR_OK;
}

static err_t http_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg;
    (void)len;
    http_close_conn(pcb);
    return ERR_OK;
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;

    if (p == nullptr || err != ERR_OK) {
        if (p) pbuf_free(p);
        http_close_conn(pcb);
        return ERR_OK;
    }

    // Copy request data
    uint16_t len = p->tot_len;
    if (len > MAX_REQUEST_SIZE - 1) len = MAX_REQUEST_SIZE - 1;

    char request[MAX_REQUEST_SIZE];
    pbuf_copy_partial(p, request, len, 0);
    request[len] = '\0';

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Set up sent callback to close after response
    tcp_sent(pcb, http_sent_cb);

    handle_request(pcb, request, len);
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err) {
    (void)arg;
    (void)err;
    // Connection already freed by lwIP
}

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || newpcb == nullptr) {
        return ERR_VAL;
    }

    tcp_arg(newpcb, nullptr);
    tcp_recv(newpcb, http_recv_cb);
    tcp_err(newpcb, http_err_cb);

    return ERR_OK;
}

// ── LED debug helpers ──────────────────────────────────────────────

static void led_blink(int count, int ms = 150) {
    for (int i = 0; i < count; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        sleep_ms(ms);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        sleep_ms(ms);
    }
}

// ── Public API ─────────────────────────────────────────────────────

bool wifi_server_init() {
    printf("[WiFi] Starting AP mode: SSID=%s\n", WIFI_AP_SSID);

    cyw43_arch_enable_ap_mode(WIFI_AP_SSID, WIFI_AP_PASS, CYW43_AUTH_OPEN);

    // Use hardcoded AP IP (same as CYW43_DEFAULT_IP_AP_ADDRESS)
    // The driver sets 192.168.4.1/24 on the AP netif automatically.
    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);

    // Start DHCP server (matches pico-examples/access_point exactly)
    dhcp_server_init(&dhcp_server, (ip_addr_t*)&gw, (ip_addr_t*)&mask);
    printf("[WiFi] DHCP server started\n");

    // DNS captive portal - all domains resolve to our IP
    dns_server_init(&dns_server, (ip_addr_t*)&gw);

    led_blink(2); // 2 blinks = AP + DHCP + DNS ready

    // Start HTTP server
    http_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!http_pcb) {
        printf("[WiFi] Failed to create HTTP PCB\n");
        return false;
    }

    err_t err = tcp_bind(http_pcb, IP_ADDR_ANY, HTTP_PORT);
    if (err != ERR_OK) {
        printf("[WiFi] Failed to bind HTTP port %d: %d\n", HTTP_PORT, err);
        tcp_close(http_pcb);
        http_pcb = nullptr;
        return false;
    }

    http_pcb = tcp_listen_with_backlog(http_pcb, 1);
    if (!http_pcb) {
        printf("[WiFi] Failed to listen\n");
        return false;
    }

    tcp_accept(http_pcb, http_accept_cb);
    printf("[WiFi] HTTP server listening on port %d\n", HTTP_PORT);
    printf("[WiFi] Connect to WiFi '%s' and open http://192.168.4.1/\n", WIFI_AP_SSID);

    return true;
}

void wifi_server_deinit() {
    if (http_pcb) {
        tcp_close(http_pcb);
        http_pcb = nullptr;
    }
    dns_server_deinit(&dns_server);
    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_disable_ap_mode();
    printf("[WiFi] AP mode stopped\n");
}
