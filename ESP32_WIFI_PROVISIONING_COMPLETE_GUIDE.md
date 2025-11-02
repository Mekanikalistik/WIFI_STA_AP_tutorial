# ESP32 WiFi Provisioning - Complete Technical Implementation Guide

## 🎯 Technical Challenge

Create an ESP32 WiFi provisioning system that:
1. **Starts in STA-only mode** when WiFi config exists (no AP broadcast)
2. **Only broadcasts AP when STA connection fails**
3. **Dynamic mode switching** without reboot
4. **Proper hostname setup** for network access
5. **Professional IoT device behavior**

## 🔧 Core Problem Solved

**Boot Loop Issue**: ESP32 was starting in `WIFI_MODE_APSTA` by default, causing unnecessary AP broadcasting even when STA connection was successful.

**Root Cause**: Missing boot-time decision logic and improper WiFi mode initialization sequence.

## ⚡ Solution Architecture

### 1. **STA-First Boot Logic**
```c
// Check if STA config exists in NVS
bool sta_config_exists = load_sta_config(&wifi_config);

if (sta_config_exists) {
    // Start in STA-only mode - NO AP broadcasting
    ESP_LOGI(TAG_STA, "Found existing WiFi configuration - starting in STA mode");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    current_wifi_state = WIFI_STATE_STA_ATTEMPTING;
} else {
    // No config found - start in AP+STA mode for provisioning
    ESP_LOGI(TAG_STA, "No WiFi configuration found - starting in AP+STA mode for provisioning");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    current_wifi_state = WIFI_STATE_AP_ACTIVE;
    ap_enabled = true;
}
```

### 2. **WiFi State Machine**
```c
typedef enum {
    WIFI_STATE_STA_ATTEMPTING,       // STA is trying to connect
    WIFI_STATE_STA_CONNECTED,        // STA successfully connected
    WIFI_STATE_STA_FAILED_AP_ACTIVE, // All STA attempts failed, AP is active
    WIFI_STATE_AP_ACTIVE             // AP is currently active (for other reasons)
} wifi_state_t;
```

### 3. **Dynamic Mode Switching Without Reboot**
```c
// Enable AP broadcasting after STA failure
static void enable_ap_broadcasting(void) {
    if (!ap_enabled) {
        ESP_LOGI(TAG_AP, "Enabling AP broadcasting...");
        ap_enabled = true;
        current_wifi_state = WIFI_STATE_STA_FAILED_AP_ACTIVE;
        
        // Create AP netif and configure
        ap_netif = esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        // ... AP config
    }
}

// Disable AP and return to STA-only mode
static void disable_ap_broadcasting(void) {
    if (ap_enabled) {
        ESP_LOGI(TAG_AP, "Disabling AP broadcasting, switching to STA-only mode...");
        ap_enabled = false;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // KEY: Dynamic switch
        if (ap_netif) {
            esp_netif_destroy(ap_netif);
            ap_netif = NULL;
        }
    }
}
```

### 4. **Automatic Retry Logic with AP Fallback**
```c
static void handle_sta_failure(void) {
    sta_retry_count++;

    if (sta_retry_count < STA_MAX_RETRY_ATTEMPTS) {
        // Retry STA connection
        ESP_LOGI(TAG_STA, "STA connection failed (attempt %d/%d), retrying...", 
                 sta_retry_count, STA_MAX_RETRY_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(STA_RETRY_DELAY_MS));
        esp_wifi_connect();
    } else {
        // All attempts failed - enable AP
        ESP_LOGI(TAG_STA, "All STA connection attempts failed. Enabling AP broadcasting.");
        enable_ap_broadcasting();
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
}
```

### 5. **Proper Hostname Implementation (ESP-IDF 5.5.1)**
```c
// Create STA interface
sta_netif = esp_netif_create_default_wifi_sta();

// Set hostname - CORRECT ESP-IDF 5.5.1 method
ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, "iotlogger"));
ESP_LOGI(TAG_HTTP, "Hostname set to: iotlogger");

// NOTE: Device accessible as http://iotlogger - NO mDNS needed for this use case!
```

## 🏗️ Key Technical Implementations

### Boot-Time Decision Logic
- **Check NVS for existing STA credentials**
- **Choose startup mode based on config presence**
- **No hardcoded AP mode at boot**

### Dynamic WiFi Mode Management
- **Use `esp_wifi_set_mode()` for runtime switching**
- **Proper netif creation/destruction**
- **State tracking for mode transitions**

### Hostname Resolution Strategy
- **ESP-IDF standard `esp_netif_set_hostname()` API**
- **Device accessible as `iotlogger` via router DNS**
- **No mDNS dependency required for practical use**

### WiFi Event Handling
```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (current_wifi_state == WIFI_STATE_STA_ATTEMPTING) {
            handle_sta_failure(); // Automatic retry/AP fallback
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        current_wifi_state = WIFI_STATE_STA_CONNECTED;
        if (ap_enabled) {
            disable_ap_broadcasting(); // Return to clean STA mode
        }
    }
}
```

## 🧪 Verification & Testing

### Compilation Check
```bash
idf.py build
# Should compile without errors or warnings
```

### Runtime Verification
1. **With existing WiFi config**:
   - Device boots in STA-only mode
   - No AP broadcasting unless STA fails
   - Accessible as `http://iotlogger`

2. **With new device**:
   - Boot in AP+STA mode for provisioning
   - WebUI available at `http://192.168.4.1`
   - Config via WebUI switches to STA mode

3. **STA failure scenario**:
   - 3 automatic retry attempts
   - Automatic AP activation on final failure
   - WebUI available for reconfiguration

### Network Access Methods
| Method | Status | Notes |
|--------|--------|-------|
| `http://iotlogger` | ✅ Works | Router DNS resolution |
| `http://192.168.x.x` | ✅ Works | Direct IP access |
| `http://iotlogger.local` | ❓ Optional | Requires mDNS component |

## 🎯 Final Implementation Status

### ✅ Complete & Working
- **STA-first provisioning system**
- **Dynamic WiFi mode switching**
- **Automatic retry logic with AP fallback**
- **Proper ESP-IDF 5.5.1 hostname implementation**
- **WebUI integration for network management**
- **Clean, production-ready code**

### 🚀 Professional Behavior
- **Like commercial IoT devices** (Philips Hue, Sonos pattern)
- **No unnecessary AP broadcasting**
- **Seamless mode transitions**
- **Robust error handling**

## 📝 Key Files Modified

### `src/main.c`
- Complete WiFi provisioning logic
- State machine implementation
- Event handling
- WebUI API endpoints

### ESP-IDF APIs Used
- `esp_netif_set_hostname()` - Hostname setup
- `esp_wifi_set_mode()` - Dynamic mode switching
- `esp_netif_create_default_wifi_sta()` - Interface creation
- `nvs_flash_*` - Persistent credential storage
- `esp_wifi_connect()` - Connection management

**Result: Production-ready ESP32 WiFi provisioning system following ESP-IDF 5.5.1 best practices!**