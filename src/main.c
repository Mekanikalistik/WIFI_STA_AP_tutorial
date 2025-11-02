#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_spiffs.h"
#include <dirent.h>
#include <dirent.h>

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_ESP_WIFI_STA_SSID "mywifissid"
*/

/* STA Configuration */
#define EXAMPLE_ESP_WIFI_STA_SSID "your_wifi_ssid"
#define EXAMPLE_ESP_WIFI_STA_PASSWD "your_wifi_password"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5

/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID "ESP32_AP"
#define EXAMPLE_ESP_WIFI_AP_PASSWD ""
#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4

/* GPIO Configuration */
#define LED_GPIO_PIN GPIO_NUM_35

/* WiFi State Machine - STA-First, AP-on-Failure */
typedef enum
{
    WIFI_STATE_STA_ATTEMPTING,       // STA is trying to connect
    WIFI_STATE_STA_CONNECTED,        // STA successfully connected
    WIFI_STATE_STA_FAILED_AP_ACTIVE, // All STA attempts failed, AP is active
    WIFI_STATE_AP_ACTIVE             // AP is currently active (for other reasons)
} wifi_state_t;

/* Retry Configuration */
#define STA_MAX_RETRY_ATTEMPTS 3 // Maximum STA retry attempts before enabling AP
#define STA_RETRY_DELAY_MS 5000  // Delay between retries (5 seconds)

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
static const char *TAG_HTTP = "HTTP Server";

/* WiFi State Management */
static wifi_state_t current_wifi_state = WIFI_STATE_STA_ATTEMPTING;
static int sta_retry_count = 0;
static bool ap_enabled = false;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

/* FreeRTOS event group to signal when we are connected/disconnected */
static EventGroupHandle_t s_wifi_event_group;

/* HTTP Server handle */
static httpd_handle_t server = NULL;

/* WiFi scan results */
static wifi_ap_record_t ap_records[20];
static uint16_t ap_count = 0;
static uint16_t scan_number = 0;

/* LED state */
static bool led_state = false;

/* Forward declarations */
static void handle_sta_failure(void);
static void enable_ap_broadcasting(void);
static void disable_ap_broadcasting(void);
static bool load_sta_config(wifi_config_t *wifi_config);
static void save_sta_config_to_nvs(const char *ssid, const char *password);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Station " MACSTR " joined, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Station " MACSTR " left, AID=%d, reason:%d",
                     MAC2STR(event->mac), event->aid, event->reason);
            break;
        }
        case WIFI_EVENT_STA_START:
        {
            ESP_LOGI(TAG_STA, "Station started");
            // Start connection attempt in station mode
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGI(TAG_STA, "STA disconnected, reason: %d", event->reason);

            if (current_wifi_state == WIFI_STATE_STA_CONNECTED)
            {
                // STA was connected but got disconnected, try to reconnect
                ESP_LOGI(TAG_STA, "Attempting to reconnect STA...");
                current_wifi_state = WIFI_STATE_STA_ATTEMPTING;
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                // Add delay before reconnecting
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
            }
            else if (current_wifi_state == WIFI_STATE_STA_ATTEMPTING)
            {
                // STA attempt failed, handle failure with retry logic
                ESP_LOGI(TAG_STA, "STA connection attempt failed, handling failure...");
                handle_sta_failure();
            }
            break;
        }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG_HTTP, "STA CONNECTION SUCCESSFUL!");
        ESP_LOGI(TAG_HTTP, "WebUI accessible at: http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG_HTTP, "Also accessible as: http://iotlogger.local (if hostname resolution works)");

        /* If we're in AP mode and STA connects, switch back to STA-only */
        if (ap_enabled)
        {
            ESP_LOGI(TAG_STA, "STA connected while AP was active - switching to STA-only mode...");
            disable_ap_broadcasting();
        }

        sta_retry_count = 0;
        current_wifi_state = WIFI_STATE_STA_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
/* Enable AP broadcasting */
static void enable_ap_broadcasting(void)
{
    if (!ap_enabled)
    {
        ESP_LOGI(TAG_AP, "Enabling AP broadcasting...");

        /* Create AP netif if it doesn't exist */
        if (ap_netif == NULL)
        {
            ap_netif = esp_netif_create_default_wifi_ap();
        }

        ap_enabled = true;
        current_wifi_state = WIFI_STATE_STA_FAILED_AP_ACTIVE;

        /* Configure AP mode */
        wifi_config_t wifi_ap_config = {
            .ap = {
                .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
                .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
                .channel = EXAMPLE_ESP_WIFI_CHANNEL,
                .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
                .max_connection = EXAMPLE_MAX_STA_CONN,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {
                    .required = false,
                },
            },
        };

        if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0)
        {
            wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        /* Set WiFi mode to APSTA to enable AP */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

        ESP_LOGI(TAG_AP, "AP broadcasting enabled successfully");
    }
}

/* Check if STA configuration exists in NVS and load it */
static bool load_sta_config(wifi_config_t *wifi_config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("WiFi", NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG_STA, "No WiFi configuration found");
        return false;
    }

    char ssid[32] = {0};
    char password[64] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    ret = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (ret != ESP_OK)
    {
        nvs_close(nvs_handle);
        ESP_LOGI(TAG_STA, "No SSID found in NVS");
        return false;
    }

    nvs_get_str(nvs_handle, "password", password, &password_len);
    nvs_close(nvs_handle);

    /* Configure STA with loaded credentials */
    memset(wifi_config, 0, sizeof(wifi_config_t));
    wifi_config->sta = (wifi_sta_config_t){
        .ssid = "",
        .password = "",
        .scan_method = WIFI_ALL_CHANNEL_SCAN,
        .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
    };

    strncpy((char *)wifi_config->sta.ssid, ssid, sizeof(wifi_config->sta.ssid) - 1);
    strncpy((char *)wifi_config->sta.password, password, sizeof(wifi_config->sta.password) - 1);

    ESP_LOGI(TAG_STA, "Loaded STA configuration for SSID: %s", ssid);
    return true;
}

/* Save STA configuration to NVS */
static void save_sta_config_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("WiFi", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_str(nvs_handle, "ssid", ssid);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to save SSID: %s", esp_err_to_name(ret));
    }

    ret = nvs_set_str(nvs_handle, "password", password);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to save password: %s", esp_err_to_name(ret));
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to commit NVS changes: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG_STA, "STA configuration saved to NVS");
}

/* Disable AP broadcasting and return to STA-only mode */
static void disable_ap_broadcasting(void)
{
    if (ap_enabled)
    {
        ESP_LOGI(TAG_AP, "Disabling AP broadcasting, switching to STA-only mode...");

        ap_enabled = false;

        /* Switch back to STA-only mode */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        /* Destroy AP netif if it exists */
        if (ap_netif != NULL)
        {
            esp_netif_destroy(ap_netif);
            ap_netif = NULL;
        }

        ESP_LOGI(TAG_AP, "AP broadcasting disabled successfully, now in STA-only mode");
    }
}

static void handle_sta_failure(void)
{
    sta_retry_count++;

    if (sta_retry_count < STA_MAX_RETRY_ATTEMPTS)
    {
        ESP_LOGI(TAG_STA, "STA connection failed (attempt %d/%d), retrying in %d ms...",
                 sta_retry_count, STA_MAX_RETRY_ATTEMPTS, STA_RETRY_DELAY_MS);

        // Reset event bits for retry
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);

        // Wait before retry
        vTaskDelay(pdMS_TO_TICKS(STA_RETRY_DELAY_MS));

        // Start next attempt
        ESP_LOGI(TAG_STA, "Retrying STA connection...");
        esp_wifi_connect();
    }
    else
    {
        ESP_LOGI(TAG_STA, "All STA connection attempts failed (%d/%d). Enabling AP broadcasting.",
                 sta_retry_count, STA_MAX_RETRY_ATTEMPTS);

        // All attempts failed, enable AP broadcasting
        enable_ap_broadcasting();

        // Set failure bit
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
}

/* Initialize GPIO for LED */
void gpio_init_led(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_GPIO_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_GPIO_PIN, 0); // Start with LED off
    ESP_LOGI(TAG_HTTP, "LED GPIO initialized on pin %d", LED_GPIO_PIN);
}

/* Initialize SPIFFS */
esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG_HTTP, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG_HTTP, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG_HTTP, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG_HTTP, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG_HTTP, "SPIFFS partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}

/* HTTP GET handler for root page */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG_HTTP, "HTTP Request: GET / (redirecting to /index.html)");
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/index.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Max length a file path can have on storage */
#define FILE_PATH_MAX 256

/* Scratch buffer size */
#define SCRATCH_BUFSIZE 8192

struct file_server_data
{
    /* Base path of file storage */
    char base_path[32];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest)
    {
        size_t quest_len = quest - uri;
        if (quest_len < pathlen)
            pathlen = quest_len;
    }
    const char *hash = strchr(uri, '#');
    if (hash)
    {
        size_t hash_len = hash - uri;
        if (hash_len < pathlen)
            pathlen = hash_len;
    }

    if (base_pathlen + pathlen + 1 > destsize)
    {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir)
    {
        ESP_LOGE(TAG_HTTP, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
                             "<table class=\"fixed\" border=\"1\">"
                             "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
                             "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
                             "<tbody>");

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL)
    {
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        if (stat(entrypath, &entry_stat) == -1)
        {
            ESP_LOGE(TAG_HTTP, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG_HTTP, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR)
        {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td><td>");
        httpd_resp_sendstr_chunk(req, entrytype);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
        httpd_resp_sendstr_chunk(req, "</td></tr>\n");
    }
    closedir(dir);

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (strstr(filename, ".html"))
    {
        return httpd_resp_set_type(req, "text/html");
    }
    else if (strstr(filename, ".css"))
    {
        return httpd_resp_set_type(req, "text/css");
    }
    else if (strstr(filename, ".js"))
    {
        return httpd_resp_set_type(req, "application/javascript");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* HTTP GET handler for serving files from SPIFFS */
static esp_err_t file_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename)
    {
        ESP_LOGE(TAG_HTTP, "Filename is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/')
    {
        return http_resp_dir_html(req, filepath);
    }

    if (stat(filepath, &file_stat) == -1)
    {
        ESP_LOGE(TAG_HTTP, "Failed to stat file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd)
    {
        ESP_LOGE(TAG_HTTP, "Failed to read existing file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_HTTP, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    // Add debug logging for HTTP requests
    ESP_LOGI(TAG_HTTP, "HTTP Request: %s %s", req->method == HTTP_GET ? "GET" : "POST", req->uri);

    // Retrieve the pointer to scratch buffer for temporary storage
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do
    {
        // Read file in chunks into the scratch buffer
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0)
        {
            // Send the buffer contents as HTTP response chunk
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG_HTTP, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    // Close file after sending complete
    fclose(fd);
    ESP_LOGI(TAG_HTTP, "File sending complete");

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* HTTP GET handler for WiFi scan */
static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG_HTTP, "WiFi scan requested");

    // Perform WiFi scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Get scan results
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&scan_number));
    if (scan_number > 20)
    {
        scan_number = 20;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&scan_number, ap_records));
    ap_count = scan_number;

    // Create JSON response
    char *json_response = NULL;
    size_t json_size = 0;

    // Calculate buffer size needed
    json_size += 20; // {"networks":[]}
    for (int i = 0; i < ap_count && i < 20; i++)
    {
        json_size += 150; // Rough estimate per network (increased for channel)
    }

    json_response = malloc(json_size);
    if (!json_response)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    strcpy(json_response, "{\"networks\":[");

    for (int i = 0; i < ap_count && i < 20; i++)
    {
        char network_entry[250];
        char auth_str[20];

        // Convert authmode to string
        switch (ap_records[i].authmode)
        {
        case WIFI_AUTH_OPEN:
            strcpy(auth_str, "open");
            break;
        case WIFI_AUTH_WPA_PSK:
            strcpy(auth_str, "wpa");
            break;
        case WIFI_AUTH_WPA2_PSK:
            strcpy(auth_str, "wpa2");
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            strcpy(auth_str, "wpa_wpa2");
            break;
        case WIFI_AUTH_WPA3_PSK:
            strcpy(auth_str, "wpa3");
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            strcpy(auth_str, "wpa2_wpa3");
            break;
        default:
            strcpy(auth_str, "unknown");
            break;
        }

        snprintf(network_entry, sizeof(network_entry),
                 "{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":\"%s\",\"channel\":%d}%s",
                 ap_records[i].ssid,
                 ap_records[i].rssi,
                 auth_str,
                 ap_records[i].primary,
                 (i < ap_count - 1 && i < 19) ? "," : "");

        strcat(json_response, network_entry);
    }

    strcat(json_response, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));

    free(json_response);
    ESP_LOGI(TAG_HTTP, "WiFi scan completed, found %d networks", ap_count);
    return ESP_OK;
}

/* HTTP POST handler for LED control */
static esp_err_t led_control_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse JSON
    if (strstr(buf, "\"state\":\"on\"") || strstr(buf, "\"state\":true"))
    {
        led_state = true;
        gpio_set_level(LED_GPIO_PIN, 1);
        ESP_LOGI(TAG_HTTP, "LED turned ON");
    }
    else if (strstr(buf, "\"state\":\"off\"") || strstr(buf, "\"state\":false"))
    {
        led_state = false;
        gpio_set_level(LED_GPIO_PIN, 0);
        ESP_LOGI(TAG_HTTP, "LED turned OFF");
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid state");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

/* HTTP POST handler for WiFi STA configuration */
static esp_err_t wifi_config_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse JSON for SSID and password
    char ssid[32] = {0};
    char password[64] = {0};

    // Simple JSON parsing - extract ssid and password
    char *ssid_start = strstr(buf, "\"ssid\":\"");
    char *pass_start = strstr(buf, "\"password\":\"");

    if (ssid_start && pass_start)
    {
        ssid_start += 8; // Skip "ssid":"
        char *ssid_end = strchr(ssid_start, '"');
        if (ssid_end)
        {
            size_t ssid_len = ssid_end - ssid_start;
            if (ssid_len < sizeof(ssid))
            {
                strncpy(ssid, ssid_start, ssid_len);
                ssid[ssid_len] = '\0';
            }
        }

        pass_start += 12; // Skip "password":"
        char *pass_end = strchr(pass_start, '"');
        if (pass_end)
        {
            size_t pass_len = pass_end - pass_start;
            if (pass_len < sizeof(password))
            {
                strncpy(password, pass_start, pass_len);
                password[pass_len] = '\0';
            }
        }

        ESP_LOGI(TAG_STA, "WiFi configuration received - SSID: %s", ssid);

        // Save configuration to NVS
        save_sta_config_to_nvs(ssid, password);

        // Reset state and attempt connection
        sta_retry_count = 0;
        current_wifi_state = WIFI_STATE_STA_ATTEMPTING;

        // Disable AP if currently enabled
        if (ap_enabled)
        {
            ESP_LOGI(TAG_STA, "Disconnecting AP clients and switching to STA-only mode...");
            disable_ap_broadcasting();
        }

        // Load config and attempt connection
        wifi_config_t wifi_config;
        if (load_sta_config(&wifi_config))
        {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_connect());

            ESP_LOGI(TAG_STA, "WiFi configuration saved and connection attempt started");
            ESP_LOGI(TAG_HTTP, "IMPORTANT: Device will now switch to STA mode and connect to: %s", ssid);
            ESP_LOGI(TAG_HTTP, "After connection, access via: http://iotlogger.local");
            ESP_LOGI(TAG_HTTP, "If .local doesn't work, device IP will be shown in serial output");

            char response_msg[500];
            snprintf(response_msg, sizeof(response_msg),
                     "{\"success\":true,\"message\":\"✅ WiFi configured successfully!\\n\\n🔄 Device is switching to STA mode...\\n📶 Connecting to: %s\\n\\n🌐 After connection, access via:\\n• http://iotlogger.local\\n• Or check serial output for IP address\\n\\n⚠️ Disconnect from ESP32_AP network and connect to your WiFi network!\"}",
                     ssid);

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, response_msg);
            return ESP_OK;
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load configuration");
            return ESP_FAIL;
        }
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid WiFi configuration");
    return ESP_FAIL;
}

/* HTTP GET handler for LED status */
static esp_err_t led_status_get_handler(httpd_req_t *req)
{
    char response[50];
    snprintf(response, sizeof(response), "{\"state\":%s}", led_state ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/* HTTP GET handler for WiFi connection status */
static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);

    char response[300];
    const char *state_str;

    switch (current_wifi_state)
    {
    case WIFI_STATE_STA_ATTEMPTING:
        state_str = "connecting";
        break;
    case WIFI_STATE_STA_CONNECTED:
        state_str = "connected";
        break;
    case WIFI_STATE_STA_FAILED_AP_ACTIVE:
        state_str = "failed_ap_active";
        break;
    case WIFI_STATE_AP_ACTIVE:
        state_str = "ap_active";
        break;
    default:
        state_str = "unknown";
        break;
    }

    if (ret == ESP_OK)
    {
        snprintf(response, sizeof(response),
                 "{\"connected\":true,\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"state\":\"%s\",\"retry_count\":%d,\"ap_enabled\":%s}",
                 ap_info.ssid, ap_info.rssi, ap_info.primary, state_str, sta_retry_count, ap_enabled ? "true" : "false");
    }
    else
    {
        snprintf(response, sizeof(response),
                 "{\"connected\":false,\"state\":\"%s\",\"retry_count\":%d,\"ap_enabled\":%s,\"error\":\"%s\"}",
                 state_str, sta_retry_count, ap_enabled ? "true" : "false",
                 current_wifi_state == WIFI_STATE_STA_FAILED_AP_ACTIVE ? "All STA attempts failed" : "Not connected");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/* HTTP POST handler for manual STA retry */
static esp_err_t wifi_retry_post_handler(httpd_req_t *req)
{
    char buf[50];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse JSON for retry action
    if (strstr(buf, "\"action\":\"retry\""))
    {
        ESP_LOGI(TAG_STA, "Manual STA retry requested via API");

        /* Reset retry count and state for new attempt */
        sta_retry_count = 0;
        current_wifi_state = WIFI_STATE_STA_ATTEMPTING;

        /* Disable AP broadcasting for new STA connection attempt */
        disable_ap_broadcasting();

        if (current_wifi_state != WIFI_STATE_STA_CONNECTED)
        {
            ESP_LOGI(TAG_STA, "Starting manual STA connection retry...");

            /* Ensure we're in STA-only mode */
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            esp_wifi_connect();

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Starting STA connection retry...\"}");
            return ESP_OK;
        }
        else
        {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Already connected to STA\"}");
            return ESP_OK;
        }
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid action");
        return ESP_FAIL;
    }
}

/* Start HTTP server */
static httpd_handle_t start_webserver(void)
{
    static struct file_server_data *server_data = NULL;

    /* Validate file server has not been started */
    if (server_data)
    {
        ESP_LOGE(TAG_HTTP, "File server already started");
        return NULL;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data)
    {
        ESP_LOGE(TAG_HTTP, "Failed to allocate memory for server data");
        return NULL;
    }
    strlcpy(server_data->base_path, "/spiffs", sizeof(server_data->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG_HTTP, "Starting HTTP Server on port: '%d'", config.server_port);
    ESP_LOGI(TAG_HTTP, "Open browser to: http://192.168.4.1");
    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "Failed to start file server!");
        return NULL;
    }

    /* URI handler for root page redirect */
    httpd_uri_t root_redirect = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &root_redirect);

    /* API handlers - register these before the wildcard handler */
    httpd_uri_t wifi_scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &wifi_scan);

    httpd_uri_t led_control = {
        .uri = "/api/led/control",
        .method = HTTP_POST,
        .handler = led_control_post_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &led_control);

    httpd_uri_t led_status = {
        .uri = "/api/led/status",
        .method = HTTP_GET,
        .handler = led_status_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &led_status);

    httpd_uri_t wifi_config = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = wifi_config_post_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &wifi_config);

    httpd_uri_t wifi_status = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = wifi_status_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &wifi_status);

    httpd_uri_t wifi_retry = {
        .uri = "/api/wifi/retry",
        .method = HTTP_POST,
        .handler = wifi_retry_post_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &wifi_retry);

    /* URI handler for getting uploaded files - register wildcard handler last */
    httpd_uri_t file_download = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = server_data // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    ESP_LOGI(TAG_HTTP, "Web server started successfully");
    return server;
}

void app_main(void)
{
    /* Initialize TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize event group */
    s_wifi_event_group = xEventGroupCreate();

    /* Register Event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Create STA interface */
    sta_netif = esp_netif_create_default_wifi_sta();

    /* Set hostname for network discovery - ESP-IDF standard approach */
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, "iotlogger"));
    ESP_LOGI(TAG_HTTP, "Hostname set to: iotlogger (accessible as iotlogger.local)");

    /* Try to load existing STA configuration */
    wifi_config_t wifi_config;
    bool sta_config_exists = load_sta_config(&wifi_config);

    if (sta_config_exists)
    {
        /* Start in STA mode with existing configuration */
        ESP_LOGI(TAG_STA, "Found existing WiFi configuration - starting in STA mode");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        current_wifi_state = WIFI_STATE_STA_ATTEMPTING;
    }
    else
    {
        /* No configuration found - start in AP+STA mode for provisioning */
        ESP_LOGI(TAG_STA, "No WiFi configuration found - starting in AP+STA mode for provisioning");
        ESP_LOGI(TAG_STA, "AP broadcasts for WebUI access, STA can scan for available networks");

        /* Create both STA and AP interfaces */
        ap_netif = esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_netif_set_hostname(ap_netif, "iotlogger"));

        /* Configure AP mode */
        wifi_config_t wifi_ap_config = {
            .ap = {
                .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
                .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
                .channel = EXAMPLE_ESP_WIFI_CHANNEL,
                .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
                .max_connection = EXAMPLE_MAX_STA_CONN,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {
                    .required = false,
                },
            },
        };

        if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0)
        {
            wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        /* Configure STA mode with default settings for scanning */
        wifi_config_t wifi_sta_config = {
            .sta = {
                .ssid = "",
                .password = "",
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .failure_retry_cnt = 0, // No auto-retry in provisioning mode
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
        };

        /* Start in AP+STA mode - AP for WebUI, STA for scanning */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        current_wifi_state = WIFI_STATE_AP_ACTIVE;
        ap_enabled = true;

        ESP_LOGI(TAG_HTTP, "AP is broadcasting: %s (IP: http://192.168.4.1)", EXAMPLE_ESP_WIFI_AP_SSID);
        ESP_LOGI(TAG_HTTP, "WebUI can now scan for available WiFi networks!");
    }

    /* Initialize GPIO for LED */
    gpio_init_led();

    /* Initialize SPIFFS */
    ESP_ERROR_CHECK(init_spiffs());

    /* Start HTTP server */
    server = start_webserver();

    ESP_LOGI(TAG_HTTP, "ESP32 WiFi System started!");
    if (sta_config_exists)
    {
        ESP_LOGI(TAG_HTTP, "Mode: STA - attempting to connect to saved network");
    }
    else
    {
        ESP_LOGI(TAG_HTTP, "Mode: AP Provisioning - configure WiFi via WebUI at http://192.168.4.1");
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}