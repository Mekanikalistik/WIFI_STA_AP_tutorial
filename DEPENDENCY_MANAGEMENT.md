# ESP-IDF Dependency Management in PlatformIO

## Minimum Effort Method

For ESP-IDF projects in PlatformIO, use **only `idf_component.yml`** for all dependencies. This is the designed, automatic method.

## Quick Setup

1. **Create `main/idf_component.yml`**:
```yaml
dependencies:
  espressif/mdns: "^1.8.2"
  espressif/cjson: "^1.7.19"
```

2. **Build with `pio run`** - dependencies download automatically

## Key Rules

- **ESP-IDF components** → `idf_component.yml`
- **Arduino libraries** → `platformio.ini` `lib_deps`
- **Never duplicate** same dependency in both files
- **No manual CMake editing** for dependencies

## Files You Need

- `platformio.ini` - Project config (no dependencies)
- `main/idf_component.yml` - All ESP-IDF dependencies
- `main/CMakeLists.txt` - Keep basic REQUIRES (driver, lwip if needed)

## Build Commands

```bash
pio run        # Auto-resolves all dependencies
pio run -t clean && pio run  # Force fresh dependency download
```

## Why This Works

- PlatformIO automatically downloads ESP Component Registry components
- CMake integrates them without manual configuration
- No duplicate declarations needed
- Dependencies resolve during build, not coding