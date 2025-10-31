# ESP32 WiFi Touch Sensor Logger with FlashDB - Modular Architecture

## Overview

This document outlines the complete modular architecture and implementation plan for an ESP32 WiFi Touch Sensor Logger project using FlashDB. The project demonstrates modern embedded software development practices with clear separation of concerns, hardware abstraction layers (HAL), and unit testing capabilities.

**Project Goals:**

- Implement modular, unit-testable embedded software architecture
- Integrate FlashDB for persistent touch event logging
- Create hardware abstraction layers for ESP32 peripherals
- Build WiFi STA/AP connectivity with web-based control interface
- Implement capacitive touch sensor detection on multiple pins
- Enable LED control functionality with web UI
- Provide CSV export and real-time monitoring of touch events
- Establish comprehensive unit testing on hardware
- Set up USB debugging for development workflow

**Key Components:**

- **ESP32-S3 WiFi LoRa 32 V3** microcontroller board
- **Modular HAL Architecture** with clear separation of hardware/software concerns
- **FlashDB** embedded database (KVDB for configuration, TSDB for touch event logs)
- **Capacitive Touch Sensors** on configurable ESP32 pins
- **LED Control** via GPIO for visual feedback and testing
- **WiFi STA/AP modes** with web server for remote access
- **RESTful API** for FlashDB operations, LED control, and data export
- **Web UI** served from SPIFFS with touch event visualization and LED control
- **Unity Framework** for hardware-based unit testing
- **USB Debugging** via OpenOCD for development

**Architecture Principles:**

- **Separation of Concerns**: Hardware interaction separated from business logic
- **Unit Testability**: All modules designed for isolated testing
- **Modularity**: Clear interfaces between components
- **Hardware Abstraction**: HAL layer isolates platform-specific code
- **Error Handling**: Comprehensive error management throughout
- **Documentation**: Detailed API documentation and usage examples

## System Architecture

### Hardware Architecture

- **Microcontroller**: ESP32-S3 WiFi LoRa 32 V3 (240MHz dual-core, 8MB flash, 320KB RAM)
- **Flash Memory**: 8MB total, partitioned for:
  - Application firmware (~1MB)
  - SPIFFS filesystem for web UI (~2MB)
  - FlashDB partition (32KB for KVDB/TSDB)
  - NVS and PHY data
- **Connectivity**: WiFi (2.4GHz), Bluetooth 5.0, LoRa radio module
- **LED Output**: GPIO-controlled LED for visual feedback (GPIO 35)
- **Touch Sensors**: Capacitive touch pads on configurable GPIO pins

### Software Architecture

#### Core Design Principles

1. **Modular Design**: Clear separation between hardware abstraction, business logic, and application layers
2. **Hardware Abstraction Layer (HAL)**: Platform-specific code isolated for easy testing and portability
3. **Dependency Injection**: Modules accept interfaces rather than concrete implementations
4. **Error Handling**: Comprehensive error propagation with detailed error codes
5. **Unit Testability**: All modules designed for isolated testing with mock dependencies

#### Module Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐  │
│  │  Touch Logger   │  │   Web Server    │  │  WiFi Mgr   │  │
│  │   Service       │  │    Service      │  │  Service    │  │
│  └─────────────────┘  └─────────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────────┐
│                   Business Logic Layer                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────┐  │
│  │ Touch Sensor    │  │   FlashDB       │  │   REST      │  │
│  │   Manager       │  │   Manager       │  │   API       │  │
│  └─────────────────┘  └─────────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────────┐
│                Hardware Abstraction Layer                   │
│  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────────┐  │
│  │ GPIO│  │Touch│  │WiFi │  │HTTP │  │SPIFFS│  │ FlashDB │  │
│  │ HAL │  │ HAL │  │ HAL │  │ HAL │  │ HAL  │  │   HAL   │  │
│  └─────┘  └─────┘  └─────┘  └─────┘  └─────┘  └─────────┘  │
└─────────────────────────────────────────────────────────────┘
```

#### Core Components

1. **ESP-IDF Framework**: Native Espressif framework with FreeRTOS
2. **FlashDB Library**: Embedded database with FAL (Flash Abstraction Layer)
   - **KVDB**: Key-value storage for configuration and metadata
   - **TSDB**: Time-series database for timestamped touch events
3. **FAL Library**: `armink/FAL@^0.5.0` - Flash abstraction layer
4. **Custom FAL Port**: ESP32-specific flash operations implementation
5. **SPIFFS Filesystem**: Compressed web UI file storage
6. **Unity Testing Framework**: Hardware-based unit testing
7. **OpenOCD**: USB debugging and JTAG interface

#### Web Architecture

- **Client-Side Rendering**: Web UI runs entirely in browser
- **RESTful API**: HTTP endpoints for FlashDB operations and CSV export
- **Data Flow**:
  1. User connects to ESP32 WiFi AP
  2. Browser loads static files from SPIFFS
  3. JavaScript fetches touch logs via REST API
  4. Real-time visualization and CSV export
- **Benefits**: Minimal ESP32 processing, responsive UI, standard web development

#### Testing & Debugging Architecture

- **Unit Testing**: Isolated testing of each module with mocked dependencies
- **Integration Testing**: End-to-end testing of module interactions
- **Hardware Testing**: Unity framework running directly on ESP32
- **USB Debugging**: OpenOCD with breakpoints and variable inspection
- **PlatformIO Integration**: Automated build/test/debug workflow

## Prerequisites

- **Hardware**: ESP32-S3 WiFi LoRa 32 V3 board
- **Software**: PlatformIO IDE with ESP-IDF framework
- **Tools**: USB cable, capacitive touch sensors, JTAG debugger (optional)
- **Knowledge**: C programming, embedded systems, modular design patterns
- **Libraries**: FlashDB, Unity testing framework, ESP-IDF components

## Module Specifications

### Hardware Abstraction Layer (HAL)

#### GPIO HAL (`hal_gpio.h/.c`)
- **Purpose**: Abstract GPIO operations for LED control and general I/O
- **Functions**:
  - `hal_gpio_init_pin()` - Initialize GPIO pin with configuration
  - `hal_gpio_set_level()` - Set GPIO high/low
  - `hal_gpio_get_level()` - Read GPIO state
  - `hal_gpio_toggle()` - Toggle GPIO state
- **Testing**: Mockable interface for unit testing

#### Touch HAL (`hal_touch.h/.c`)
- **Purpose**: Capacitive touch sensor abstraction
- **Functions**:
  - `hal_touch_init()` - Initialize touch sensors on specified pins
  - `hal_touch_read()` - Read touch sensor values
  - `hal_touch_is_touched()` - Check if sensor is touched
  - `hal_touch_set_threshold()` - Configure touch thresholds
- **Testing**: Mockable for sensor simulation

#### WiFi HAL (`hal_wifi.h/.c`)
- **Purpose**: WiFi connectivity abstraction (STA/AP modes)
- **Functions**:
  - `hal_wifi_init()` - Initialize WiFi with configuration
  - `hal_wifi_connect_sta()` - Connect to WiFi network
  - `hal_wifi_start_ap()` - Start access point
  - `hal_wifi_get_status()` - Get connection status
- **Testing**: Mockable WiFi operations

#### SPIFFS HAL (`hal_spiffs.h/.c`)
- **Purpose**: Filesystem operations abstraction
- **Functions**:
  - `hal_spiffs_init()` - Initialize SPIFFS
  - `hal_spiffs_read_file()` - Read file contents
  - `hal_spiffs_write_file()` - Write file
  - `hal_spiffs_list_dir()` - List directory contents
- **Testing**: Mockable filesystem operations

#### FlashDB HAL (`hal_flashdb.h/.c`)
- **Purpose**: Database operations abstraction
- **Functions**:
  - `hal_flashdb_init()` - Initialize FlashDB instances
  - `hal_flashdb_kv_set()` - Key-value store operations
  - `hal_flashdb_kv_get()` - Key-value retrieve operations
  - `hal_flashdb_tsdb_append()` - Time-series data append
  - `hal_flashdb_tsdb_query()` - Time-series data query
- **Testing**: Mockable database operations

### Business Logic Modules

#### Touch Sensor Manager (`touch_manager.h/.c`)
- **Purpose**: High-level touch sensor management and event logging
- **Functions**:
  - `touch_manager_init()` - Initialize with HAL dependencies
  - `touch_manager_start_monitoring()` - Begin touch monitoring
  - `touch_manager_get_events()` - Retrieve logged events
  - `touch_manager_clear_events()` - Clear event log
- **Dependencies**: Touch HAL, FlashDB HAL, Timer HAL

#### FlashDB Manager (`flashdb_manager.h/.c`)
- **Purpose**: Database operations coordination
- **Functions**:
  - `flashdb_manager_init()` - Initialize KVDB and TSDB
  - `flashdb_manager_store_touch_event()` - Store touch events
  - `flashdb_manager_get_touch_logs()` - Retrieve touch logs
  - `flashdb_manager_export_csv()` - Export data as CSV
- **Dependencies**: FlashDB HAL

#### REST API Module (`rest_api.h/.c`)
- **Purpose**: HTTP endpoint handling for web interface
- **Functions**:
  - `rest_api_init()` - Initialize API endpoints
  - `rest_api_register_endpoints()` - Register HTTP handlers
  - `rest_api_handle_touch_logs()` - Handle log retrieval requests
  - `rest_api_handle_csv_export()` - Handle CSV export requests
- **Dependencies**: HTTP HAL, FlashDB Manager

### Application Services

#### Touch Logger Service (`touch_logger_service.h/.c`)
- **Purpose**: Main application service coordinating touch logging
- **Functions**:
  - `touch_logger_service_init()` - Initialize all components
  - `touch_logger_service_start()` - Start monitoring and web server
  - `touch_logger_service_stop()` - Stop all operations
- **Dependencies**: All managers and HAL modules

#### Web Server Service (`web_server_service.h/.c`)
- **Purpose**: HTTP server management and routing
- **Functions**:
  - `web_server_service_init()` - Start HTTP server
  - `web_server_service_serve_static()` - Serve web UI files
  - `web_server_service_handle_api()` - Route API requests
- **Dependencies**: HTTP HAL, SPIFFS HAL, REST API

## Implementation Status

### Current Status (Completed)

- [x] ESP32-S3 WiFi LoRa 32 V3 board setup with ESP-IDF
- [x] Modular HAL architecture design with clear separation of concerns
- [x] Unit-testable module interfaces with dependency injection
- [x] FlashDB library integration (`kuba2k2/FlashDB@^2024.12.30`)
- [x] Custom FAL porting layer for ESP32 flash operations
- [x] Basic KVDB/TSDB examples with persistent storage
- [x] Custom partition table (32KB flashdb partition)
- [x] Successful firmware build and flash to hardware
- [x] Serial monitoring working (115200 baud)
- [x] CMakeLists.txt filtering to exclude C++ warnings

### In Progress (FlashDB-integration Branch)

- [ ] Create HAL modules (GPIO, Touch, WiFi, SPIFFS, FlashDB)
- [ ] Implement Touch Sensor Manager with event logging
- [ ] Build FlashDB Manager for KVDB/TSDB operations
- [ ] Develop REST API module for HTTP endpoints
- [ ] Create Touch Logger Service coordinating all components
- [ ] Implement Web Server Service with static file serving
- [ ] Add capacitive touch sensor support on configurable pins
- [ ] Integrate WiFi STA/AP modes with web interface
- [ ] Create web UI for touch log viewing and CSV export
- [ ] Implement comprehensive unit testing framework
- [ ] Set up USB debugging with OpenOCD/JTAG

### Planned

- [ ] Hardware unit testing with Unity framework on ESP32
- [ ] USB debugging setup with breakpoints and variable inspection
- [ ] Performance optimization and memory management
- [ ] Error handling and recovery mechanisms
- [ ] Documentation updates and usage examples
- [ ] Integration testing of all modules
- [ ] Documentation updates and workflow automation

---

## Implementation Guide

### Phase 1: HAL Layer Development

#### Step 1.1: Create HAL Module Structure

1. Create directory structure:
```
include/hal/
src/hal/
```

2. Implement HAL modules following the specifications above:
   - `hal_gpio.h/.c` - GPIO operations abstraction
   - `hal_touch.h/.c` - Touch sensor abstraction
   - `hal_wifi.h/.c` - WiFi connectivity abstraction
   - `hal_spiffs.h/.c` - Filesystem abstraction
   - `hal_flashdb.h/.c` - Database abstraction

3. Each HAL module must:
   - Provide mockable interfaces for unit testing
   - Include comprehensive error handling
   - Follow consistent naming conventions
   - Include detailed documentation

#### Step 1.2: HAL Testing Setup

1. Configure Unity testing framework in `platformio.ini`:

```ini
[env:test]
platform = espressif32
board = heltec_wifi_lora_32_V3
framework = espidf
lib_deps =
    throwtheswitch/Unity@^2.5.2
build_flags =
    -D UNITY_INCLUDE_CONFIG_H
    -D CONFIG_UNITY_ENABLED
```

2. Create test files for each HAL module:
   - `test/test_hal_gpio.c`
   - `test/test_hal_touch.c`
   - `test/test_hal_wifi.c`
   - `test/test_hal_spiffs.c`
   - `test/test_hal_flashdb.c`

### Phase 2: Business Logic Implementation

#### Step 2.1: Implement Manager Modules

1. Create Touch Sensor Manager:
   - Initialize touch sensors on specified pins
   - Monitor touch events with debouncing
   - Log events to FlashDB with timestamps
   - Provide event retrieval interface

2. Create FlashDB Manager:
   - Initialize KVDB and TSDB instances
   - Provide high-level database operations
   - Handle data serialization/deserialization
   - Implement CSV export functionality

3. Create REST API Module:
   - Register HTTP endpoints for data access
   - Handle JSON serialization/deserialization
   - Implement CORS headers for web UI
   - Provide error response formatting

#### Step 2.2: Service Layer Implementation

1. Touch Logger Service:
   - Coordinate all components initialization
   - Manage application lifecycle
   - Handle configuration persistence
   - Provide main application interface

2. Web Server Service:
   - Initialize HTTP server on port 80
   - Serve static web UI files from SPIFFS
   - Route API requests to REST handlers
   - Handle file compression and caching

### Phase 3: Integration and Testing

#### Step 3.1: Component Integration

1. Update `main.c` to use modular architecture:
   - Initialize HAL layers
   - Create manager instances with dependency injection
   - Start services in correct order
   - Handle application lifecycle

2. Update `CMakeLists.txt` for new module structure:
   - Add include paths for HAL headers
   - Configure component dependencies
   - Set up test component compilation

#### Step 3.2: Web UI Development

1. Update `WEBUI/index.html` for touch logging interface:
   - Add touch sensor status display
   - Implement real-time event log viewer
   - Add CSV export functionality
   - Include sensor configuration options

2. Update `WEBUI/script.js` for API integration:
   - Fetch touch events from REST API
   - Display real-time sensor status
   - Handle CSV download requests
   - Implement touch sensor configuration

#### Step 3.3: Testing and Validation

1. Unit Testing:
   - Test each HAL module in isolation
   - Mock dependencies for business logic testing
   - Validate error handling paths
   - Run tests on hardware with Unity

2. Integration Testing:
   - Test module interactions
   - Validate end-to-end data flow
   - Test web UI functionality
   - Verify FlashDB persistence

3. Performance Testing:
   - Measure touch detection latency
   - Monitor memory usage
   - Test concurrent operations
   - Validate FlashDB performance

---

## Section 2: Basic Examples

### Step 2.1: Key-Value Storage Demo

1. Create `src/kv_demo.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>

fdb_kvdb_t kvdb;

void setup() {
    Serial.begin(115200);

    // Initialize FlashDB KVDB (adjust partition as needed)
    fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);

    // Store values
    fdb_kv_set_blob(&kvdb, "temp", "25.5");
    fdb_kv_set_blob(&kvdb, "humidity", "60");

    // Retrieve and print
    char value[32];
    fdb_kv_get_blob(&kvdb, "temp", value, sizeof(value));
    Serial.println(value);
}

void loop() {
    delay(1000);
}
```

2. Set as main file in `platformio.ini`: `src_filter = +<kv_demo.cpp>`
3. Build and upload

**Error Handling**: If init fails, check flash partition configuration in ESP32 menuconfig

**Checkpoint**: Serial output shows stored values

### Step 2.2: Time Series Data Storage Demo

1. Create `src/ts_demo.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>

fdb_tsdb_t tsdb;

void setup() {
    Serial.begin(115200);

    // Initialize TSDB
    fdb_tsdb_init(&tsdb, "sensor", "fdb_tsdb1", NULL, NULL);

    // Add time series data
    fdb_tsdb_append(&tsdb, 1, "25.5");  // timestamp, value
    fdb_tsdb_append(&tsdb, 2, "26.0");
}

void loop() {
    // Query recent data
    fdb_blob_t blob;
    fdb_tsdb_query(&tsdb, 0, fdb_tsdb_query_count(&tsdb), &blob);
    Serial.println((char*)blob.buf);
    delay(5000);
}
```

2. Update `platformio.ini` src_filter
3. Build and upload

**Error Handling**: Ensure sufficient flash space for TSDB

**Checkpoint**: Time series data prints correctly

---

## Section 3: Small IoT Project - Sensor Data Logger

### Step 3.1: Implement Sensor Logger

1. Add DHT sensor library to `platformio.ini`:

```
lib_deps =
    kuba2k2/FlashDB @ ^2.0.0
    adafruit/DHT sensor library @ ^1.4.4
```

2. Create `src/sensor_logger.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>
#include <DHT.h>

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
fdb_tsdb_t tsdb;

void setup() {
    Serial.begin(115200);
    dht.begin();

    fdb_tsdb_init(&tsdb, "sensors", "fdb_tsdb1", NULL, NULL);
}

void loop() {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
        char data[32];
        sprintf(data, "%.1f,%.1f", temp, hum);
        fdb_tsdb_append(&tsdb, millis(), data);
        Serial.println(data);
    }
    delay(30000);  // 30 second intervals
}
```

3. Build and upload

**Error Handling**: Connect DHT11 to GPIO 4, check sensor wiring if readings fail

**Checkpoint**: Serial shows temperature/humidity data logging

### Step 3.2: Add Data Retrieval Web Interface

1. Add WiFi and web server libraries
2. Extend code to serve data via HTTP endpoint

**Checkpoint**: Access data via browser at ESP32 IP

---

## Section 4: Unit Testing on Hardware

### Step 4.1: Set Up Unity Testing Framework

1. Add to `platformio.ini`:

```
lib_deps =
    kuba2k2/FlashDB @ ^2.0.0
    Unity @ ^2.5.2
```

2. Enable test environment:

```
[env:test]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps = Unity @ ^2.5.2 kuba2k2/FlashDB @ ^2.0.0
build_flags = -D UNITY_INCLUDE_CONFIG_H
```

### Step 4.2: Create Hardware Tests

1. Create `test/test_flashdb.cpp`:

```cpp
#include <unity.h>
#include <flashdb.h>

fdb_kvdb_t test_kvdb;

void setUp() {
    fdb_kvdb_init(&test_kvdb, "test", "fdb_kvdb_test", NULL, NULL);
}

void test_kv_operations() {
    // Test set/get
    fdb_kv_set_blob(&test_kvdb, "test_key", "test_value");
    char value[32];
    fdb_kv_get_blob(&test_kvdb, "test_key", value, sizeof(value));
    TEST_ASSERT_EQUAL_STRING("test_value", value);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_kv_operations);
    UNITY_END();
}

void loop() {}
```

2. Run tests: `PlatformIO: Test` (Ctrl+Alt+T)

**Error Handling**: Tests run on hardware, monitor serial for results

**Checkpoint**: All tests pass, serial shows "OK" for each test

---

## Section 5: USB Debugging Setup

### Step 5.1: Configure JTAG Debugging

1. Install OpenOCD: `pio platform install espressif32 --with-package framework-arduinoespressif32`
2. In `platformio.ini`, add:

```
[env:debug]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
debug_tool = esp-prog
debug_init_break = tbreak setup
```

3. Connect JTAG debugger to ESP32-S3 pins

### Step 5.2: Debug Session

1. Set breakpoints in FlashDB code (e.g., in `fdb_kv_set_blob`)
2. Start debug: `PlatformIO: Start Debugging` (F5)
3. Step through code, inspect variables

**Error Handling**: Ensure ESP32-S3 has JTAG pins exposed

**Checkpoint**: Breakpoints trigger, variables inspectable

---

## Section 6: Automated Workflow Documentation

### Step 6.1: Create Workflow Scripts

1. Add to `platformio.ini`:

```
[env:flash-monitor]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
; Custom target for flash + monitor
extra_scripts = post:flash_and_monitor.py
```

2. Create `flash_and_monitor.py`:

```python
import subprocess
ImportExtraScript

def flash_and_monitor(source, target, env):
    # Flash
    env.Execute("pio run -t upload")
    # Monitor
    env.Execute("pio device monitor")
```

### Step 6.2: Usage Guide

- Flash and monitor: `pio run -e flash-monitor`
- Debug: `pio debug`
- Test: `pio test`

**Checkpoint**: Automated commands work end-to-end

---

## Development Workflow

### Branch Strategy

- `main`: Stable production code (WiFi STA/AP functionality)
- `FlashDB-integration`: Feature branch for modular FlashDB integration
- Feature branches for specific enhancements
- Release branches for stable versions

### Testing Strategy

1. **Unit Testing**: Each module tested in isolation with mocked dependencies
2. **Integration Testing**: Module interactions validated
3. **Hardware Testing**: Unity framework on ESP32 for hardware-specific tests
4. **System Testing**: End-to-end functionality validation

### Code Quality Standards

- **Modularity**: Clear separation of concerns with defined interfaces
- **Documentation**: Comprehensive API documentation and usage examples
- **Error Handling**: Consistent error propagation and recovery
- **Testing**: Minimum 80% code coverage for critical paths
- **Performance**: Memory-efficient operations with profiling

### Build and Deployment

1. **Development**: Use PlatformIO for local development and testing
2. **CI/CD**: Automated testing and building on GitHub Actions
3. **Deployment**: OTA updates for firmware distribution
4. **Monitoring**: Serial logging and web-based status monitoring

## API Reference

### REST Endpoints

- `GET /api/touch/logs` - Retrieve touch event logs
- `GET /api/touch/logs/csv` - Export touch logs as CSV
- `POST /api/touch/config` - Configure touch sensor parameters
- `POST /api/led/control` - Control LED state (on/off)
- `GET /api/led/status` - Get current LED state
- `GET /api/system/status` - Get system status and statistics
- `POST /api/system/reset` - Reset touch event database

### Data Formats

#### Touch Event Log Entry
```json
{
  "timestamp": 1640995200,
  "pin": 4,
  "event": "touch_detected",
  "duration": 150,
  "user_id": "user_001"
}
```

#### System Status Response
```json
{
  "uptime": 3600,
  "total_events": 1250,
  "flash_usage": 65,
  "wifi_connected": true,
  "touch_sensors_active": 7
}
```

## Troubleshooting

### Common Issues

1. **FlashDB Initialization Fails**
   - Check partition table configuration
   - Verify FAL port implementation
   - Ensure sufficient flash space

2. **Touch Sensors Not Responding**
   - Verify GPIO pin configurations
   - Check touch sensor connections
   - Adjust touch thresholds

3. **WiFi Connection Issues**
   - Verify credentials in FlashDB
   - Check antenna connections
   - Monitor signal strength

4. **Web UI Not Loading**
   - Verify SPIFFS filesystem integrity
   - Check HTTP server configuration
   - Validate static file paths

### Debug Procedures

1. **Enable Verbose Logging**: Set log level to DEBUG in ESP-IDF menuconfig
2. **USB Debugging**: Use OpenOCD with GDB for breakpoint debugging
3. **Memory Analysis**: Monitor heap usage and stack depth
4. **Performance Profiling**: Use ESP-IDF profiling tools

## Future Enhancements

- **Machine Learning**: Touch pattern recognition for user identification
- **Cloud Integration**: AWS IoT Core connectivity for data synchronization
- **Advanced Analytics**: Web-based dashboard with charts and statistics
- **OTA Updates**: Secure firmware update mechanism
- **Power Management**: Deep sleep modes with touch wake-up
- **Security**: Encrypted data storage and secure API endpoints

## Contributing

1. Fork the repository
2. Create a feature branch from `main`
3. Implement changes with comprehensive tests
4. Update documentation as needed
5. Submit a pull request with detailed description

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Commit Message

"feat: Implement modular ESP32 Touch Sensor Logger with FlashDB integration, HAL architecture, and comprehensive testing framework"
