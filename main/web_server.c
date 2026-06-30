#include "web_server.h"
#include "salary_cfg.h"
#include "app_state.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "WEB";

/* 配置页:只含薪资参数 + 今日打卡。AI 用量功能已停用(见 ai_usage.c 注释)。*/
static const char *HTML_FMT =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>设置</title>"
"<style>body{font-family:sans-serif;max-width:460px;margin:18px auto;padding:0 12px}"
"h3{margin:14px 0 6px}label{display:block;margin:10px 0 4px;color:#444}"
"input{width:100%;padding:8px;font-size:15px;box-sizing:border-box}"
".cb{display:flex;align-items:center;gap:8px;margin:10px 0 4px;color:#444}"
".cb input{width:auto}"
".hint{color:#999;font-size:12px;margin:-2px 0 8px}"
"button{margin-top:14px;padding:11px;font-size:16px;width:100%;background:#0a7;color:#fff;border:0;border-radius:8px}"
".p{background:#f5f5f5;padding:10px;border-radius:8px;margin-top:14px;color:#333;font-size:14px}</style>"
"</head><body>"
"<form method='post' action='/save'>"
"<h3>💰 薪资参数</h3>"
"<label>月薪(元)</label><input type=number name='m' value='%.0f'>"
"<label>月工作天数</label><input type=number name='d' value='%d'>"
"<label>每天工时(小时)</label><input type=number step=0.1 name='h' value='%.1f'>"
"<label>上班时间</label><input type=time name='s' value='%02d:%02d'>"
"<label>下班时间</label><input type=time name='e' value='%02d:%02d'>"
"<label>午休时长(小时)</label><input type=number step=0.1 name='u' value='%.1f'>"
"<label class='cb'>午休算入工时<input type=checkbox name='c' value='1' %s></label>"
"<div class='hint'>勾选:下班=上班+工时;不勾:顺延午休</div>"
"<button type='submit'>保存</button></form>"
"<div class='p'>📅 今日打卡: %s</div>"
"</body></html>";

static esp_err_t get_handler(httpd_req_t *req)
{
    const salary_cfg_t *c = cfg_get();
    const app_view_t   *v = app_state_get();

    char punch[48];
    if (v->punched) {
        snprintf(punch, sizeof(punch), "已打卡  上班 %02d:%02d  下班 %02d:%02d",
                 v->punch_sh, v->punch_sm, v->punch_eh, v->punch_em);
    } else {
        snprintf(punch, sizeof(punch), "未打卡");
    }

    char *html = malloc(2400);
    if (!html) return ESP_FAIL;
    int n = snprintf(html, 2400, HTML_FMT,
        c->salary_month, c->work_days, c->hours_per_day,
        c->work_start_min / 60, c->work_start_min % 60,
        c->work_end_min / 60,   c->work_end_min % 60,
        c->lunch_min / 60.0,
        c->lunch_counts ? "checked" : "",
        punch);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, n);
    free(html);
    return ESP_OK;
}

/* 从表单 body 取 name= 的值(URL decode %XX),成功返回 true */
static bool field_str(const char *body, const char *name, char *out, int outlen)
{
    char pat[16];
    snprintf(pat, sizeof(pat), "%s=", name);
    const char *p = strstr(body, pat);
    if (!p) return false;
    p += strlen(pat);
    int i = 0;
    while (*p && *p != '&' && i < outlen - 1) {
        if (p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], 0 };
            out[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else if (p[0] == '+') {
            out[i++] = ' ';
            p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = 0;
    return true;
}

/* 从 JSON body 取 "key":数字(电脑推送 /push 用) */
static bool parse_int_field(const char *json, const char *key, int *out)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) { *out = atoi(p); return true; }
    return false;
}

/* 电脑推送:{"cpu":C,"gpu":G,"mem":M}。AI 字段已停用,不再解析。*/
static esp_err_t post_push_handler(httpd_req_t *req)
{
    char buf[256];
    int total = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int len = httpd_req_recv(req, buf, total);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed"); return ESP_OK; }
    buf[len] = 0;

    int cpu = -1, gpu = -1, mem = -1;
    parse_int_field(buf, "cpu", &cpu);
    parse_int_field(buf, "gpu", &gpu);
    parse_int_field(buf, "mem", &mem);
    if (cpu >= 0 || gpu >= 0 || mem >= 0) {
        app_state_set_pc(cpu >= 0 ? cpu : 0, gpu >= 0 ? gpu : 0, mem >= 0 ? mem : 0);
    }
    ESP_LOGI(TAG, "推送: pc=cpu%d gpu%d mem%d", cpu, gpu, mem);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t post_save_handler(httpd_req_t *req)
{
    char buf[512];
    int total = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int len = httpd_req_recv(req, buf, total);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_OK;
    }
    buf[len] = 0;

    salary_cfg_t c = *cfg_get();
    char v[128];
    if (field_str(buf, "m", v, sizeof(v))) c.salary_month  = (float)atof(v);
    if (field_str(buf, "d", v, sizeof(v))) c.work_days     = atoi(v);
    if (field_str(buf, "h", v, sizeof(v))) c.hours_per_day = (float)atof(v);
    if (field_str(buf, "s", v, sizeof(v)) && strlen(v) >= 4)
        c.work_start_min = atoi(v) * 60 + atoi(v + 3);
    if (field_str(buf, "e", v, sizeof(v)) && strlen(v) >= 4)
        c.work_end_min   = atoi(v) * 60 + atoi(v + 3);
    if (field_str(buf, "u", v, sizeof(v))) c.lunch_min = (int)((float)atof(v) * 60);
    c.lunch_counts = field_str(buf, "c", v, sizeof(v)) ? 1 : 0;

    cfg_set(&c);
    ESP_LOGI(TAG, "参数已更新并保存到 NVS");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server 启动失败");
        return;
    }
    static const httpd_uri_t get_uri  = { .uri = "/",     .method = HTTP_GET,  .handler = get_handler };
    static const httpd_uri_t post_uri = { .uri = "/save", .method = HTTP_POST, .handler = post_save_handler };
    static const httpd_uri_t push_uri = { .uri = "/push", .method = HTTP_POST, .handler = post_push_handler };
    httpd_register_uri_handler(server, &get_uri);
    httpd_register_uri_handler(server, &post_uri);
    httpd_register_uri_handler(server, &push_uri);
    ESP_LOGI(TAG, "Web 配置页就绪(含 /push 接收电脑占用)");
}
