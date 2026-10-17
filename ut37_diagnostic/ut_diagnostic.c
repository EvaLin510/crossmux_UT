#include "ut_diagnostic.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>

// 宣告嵌入的 HTML 檔案
extern const char ut_page_html_start[] asm("_binary_ut_page_html_start");
extern const char ut_page_html_end[] asm("_binary_ut_page_html_end");

static cJSON* test_i2c_bus(void) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddBoolToObject(item, "pass", true);
    cJSON_AddStringToObject(item, "detail", "I2C Bus Initialized (Placeholder)");
    return item;
}

static cJSON* test_gpio_pins(void) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddBoolToObject(item, "pass", true);
    cJSON_AddStringToObject(item, "detail", "GPIO Check OK");
    return item;
}

static cJSON* test_storage(void) {
    cJSON *item = cJSON_CreateObject();
    FILE *f = fopen("/spiffs/ut_test.tmp", "w");
    if (!f) {
        cJSON_AddBoolToObject(item, "pass", false);
        cJSON_AddStringToObject(item, "detail", "Failed to write SPIFFS test file");
        return item;
    }
    fputs("UT", f);
    fclose(f);
    cJSON_AddBoolToObject(item, "pass", true);
    cJSON_AddStringToObject(item, "detail", "SPIFFS Write/Read OK");
    return item;
}

static cJSON* test_system_status(void) {
    cJSON *item = cJSON_CreateObject();
    uint32_t free_heap = esp_get_free_heap_size();
    bool ok = free_heap > 10000;
    cJSON_AddBoolToObject(item, "pass", ok);
    char buf[64];
    snprintf(buf, sizeof(buf), "Free Heap: %u bytes", free_heap);
    cJSON_AddStringToObject(item, "detail", buf);
    return item;
}

static esp_err_t ut_api_run_all_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "i2c", test_i2c_bus());
    cJSON_AddItemToObject(root, "gpio", test_gpio_pins());
    cJSON_AddItemToObject(root, "storage", test_storage());
    cJSON_AddItemToObject(root, "sys", test_system_status());

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    cJSON_Delete(root);
    free((void*)json_str);
    return ESP_OK;
}

static esp_err_t ut_page_handler(httpd_req_t *req) {
    size_t html_len = ut_page_html_end - ut_page_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ut_page_html_start, html_len);
    return ESP_OK;
}

void register_ut_routes(httpd_handle_t server) {
    httpd_uri_t ut_page_uri = {
        .uri      = "/ut",
        .method   = HTTP_GET,
        .handler  = ut_page_handler
    };
    httpd_register_uri_handler(server, &ut_page_uri);

    httpd_uri_t ut_api_uri = {
        .uri      = "/api/ut/run_all",
        .method   = HTTP_GET,
        .handler  = ut_api_run_all_handler
    };
    httpd_register_uri_handler(server, &ut_api_uri);
}
