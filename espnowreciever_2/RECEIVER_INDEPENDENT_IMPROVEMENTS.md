# Receiver Independent Improvements
## ESPnowreceiver_2 - Codebase-Specific Enhancements

**Date**: February 26, 2026  
**Scope**: Improvements that benefit the receiver codebase **independently** of the transmitter  
**Note**: These improvements do NOT require changes to the transmitter codebase

---

## Overview

The receiver has several issues that are unique to its display-driven architecture and don't affect the transmitter. These improvements should be implemented in order of priority without waiting for transmitter changes.

---

## 1. Display Hardware Abstraction Layer (HAL)

### Priority: 🔴 **CRITICAL**
**Effort**: 2 days  
**Blocking**: Yes - must complete before display state refactoring

### Problem

Display logic is tightly coupled to TFT_eSPI hardware:

```cpp
// display_core.cpp - Global hardware object
TFT_eSPI tft;  // ← Global coupling

// Direct hardware calls scattered throughout
tft.fillScreen(TFT_BLACK);
tft.setTextColor(TFT_WHITE);
tft.drawString("Status: " + String(transmitter_connected), 10, 10);
```

This makes it impossible to:
- Test display logic without physical hardware
- Use different displays
- Add display simulation for debugging
- Swap TFT library versions

### Solution

Create a Display HAL (Hardware Abstraction Layer):

```cpp
// display/display_hal.h
class IDisplayDriver {
public:
    virtual ~IDisplayDriver() = default;
    
    // Primitives
    virtual void fill_screen(uint16_t color) = 0;
    virtual void draw_pixel(uint16_t x, uint16_t y, uint16_t color) = 0;
    virtual void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;
    virtual void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;
    
    // Text
    virtual void set_text_color(uint16_t fg, uint16_t bg = TFT_BLACK) = 0;
    virtual void set_cursor(uint16_t x, uint16_t y) = 0;
    virtual void set_text_size(uint8_t size) = 0;
    virtual void print(const char* text) = 0;
    virtual void println(const char* text) = 0;
    
    // Measurements
    virtual uint16_t text_width(const char* text) = 0;
    virtual uint16_t text_height() const = 0;
    
    // Rotation/orientation
    virtual void set_rotation(uint8_t r) = 0;
    virtual uint16_t get_width() const = 0;
    virtual uint16_t get_height() const = 0;
};

// display/tft_espi_driver.h
class TftEspiDriver : public IDisplayDriver {
public:
    explicit TftEspiDriver(TFT_eSPI& tft) : tft_(tft) {}
    
    void fill_screen(uint16_t color) override {
        tft_.fillScreen(color);
    }
    
    void draw_pixel(uint16_t x, uint16_t y, uint16_t color) override {
        tft_.drawPixel(x, y, color);
    }
    
    // ... implement others ...
    
private:
    TFT_eSPI& tft_;
};

// display/mock_display_driver.h - For testing
class MockDisplayDriver : public IDisplayDriver {
public:
    void fill_screen(uint16_t color) override {
        last_fill_color = color;
        render_history.push_back("fill_screen");
    }
    
    // ... implement others ...
    
    // Testing interface
    uint16_t last_fill_color;
    std::vector<std::string> render_history;
};
```

### Update Display Core

```cpp
// display_core.cpp - BEFORE
void display_init() {
    tft.init();
    tft.setRotation(ROTATION);
    tft.fillScreen(TFT_BLACK);
}

void display_show_status(bool transmitter_connected) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(transmitter_connected ? "Connected" : "Disconnected", 10, 10);
}

// display_core.cpp - AFTER
class DisplayCore {
public:
    explicit DisplayCore(IDisplayDriver& driver) : driver_(driver) {}
    
    void init() {
        // No hardware-specific calls
        driver_.fill_screen(COLOR_BACKGROUND);
        update_splash_screen();
    }
    
    void show_status(bool transmitter_connected) {
        driver_.fill_screen(COLOR_BACKGROUND);
        driver_.set_text_color(COLOR_TEXT);
        
        const char* status = transmitter_connected ? "Connected" : "Disconnected";
        driver_.set_cursor(10, 10);
        driver_.println(status);
    }
    
private:
    IDisplayDriver& driver_;
    
    static constexpr uint16_t COLOR_BACKGROUND = TFT_BLACK;
    static constexpr uint16_t COLOR_TEXT = TFT_WHITE;
    
    void update_splash_screen() {
        // Render splash using driver_ interface
    }
};

// In main.cpp
TFT_eSPI tft;
TftEspiDriver hw_driver(tft);
DisplayCore display(hw_driver);

void setup() {
    display.init();
}
```

### Benefits

- ✅ Can test display logic without hardware
- ✅ Can swap display types (TFT vs OLED vs e-ink)
- ✅ Can run display in simulator for debugging
- ✅ Clean separation of concerns
- ✅ Easier to refactor display logic

### Code Changes Required

**Files to create**:
- `src/display/display_hal.h` - Interface definition
- `src/display/tft_espi_driver.h` - TFT_eSPI implementation
- `src/display/display_core.cpp` - Refactor to use HAL

**Files to modify**:
- `src/main.cpp` - Create driver and pass to DisplayCore
- `src/display/display_led.cpp` - Update to use HAL
- `src/display/display_page.cpp` - Update to use HAL

**Dependencies**:
- None - this is pure abstraction, doesn't affect transmitter

---

## 2. Global Volatile Encapsulation

### Priority: 🔴 **CRITICAL**
**Effort**: 1.5 days  
**Blocking**: Partially - should do before state machine refactoring

### Problem

Global volatile variables bypass state management:

```cpp
// common.h
volatile bool transmitter_connected = false;  // ← Global access everywhere
volatile uint8_t received_soc = 0;            // ← Race conditions possible
volatile bool data_received = false;           // ← No timestamps

// Used everywhere without control
if (transmitter_connected) { ... }
received_soc = new_value;
```

### Solution

Create a data structure with proper synchronization:

```cpp
// connection_state.h
class ConnectionState {
public:
    static ConnectionState& instance();
    
    // Connection status
    bool is_transmitter_connected() const;
    void set_transmitter_connected(bool connected);
    
    // Data updates
    uint8_t get_soc() const;
    void set_soc(uint8_t soc);
    
    uint16_t get_voltage() const;
    void set_voltage(uint16_t voltage);
    
    uint16_t get_power() const;
    void set_power(uint16_t power);
    
    // Timestamps
    uint32_t last_data_received_ms() const;
    bool is_data_stale(uint32_t timeout_ms) const;
    
    // Listeners
    using ConnectionChangedCallback = std::function<void(bool)>;
    void on_connection_changed(ConnectionChangedCallback callback);
    
    // Statistics
    uint32_t get_connection_uptime_ms() const;
    uint32_t get_connection_count() const;
    
private:
    ConnectionState();
    
    mutable SemaphoreHandle_t mutex_;
    
    bool transmitter_connected_;
    uint32_t connection_start_time_;
    uint32_t connection_count_;
    
    uint8_t soc_;
    uint16_t voltage_;
    uint16_t power_;
    uint32_t last_data_time_;
    
    std::vector<ConnectionChangedCallback> callbacks_;
};
```

### Implementation

```cpp
// connection_state.cpp
ConnectionState& ConnectionState::instance() {
    static ConnectionState instance;
    return instance;
}

ConnectionState::ConnectionState()
    : transmitter_connected_(false)
    , connection_start_time_(0)
    , connection_count_(0)
    , soc_(0)
    , voltage_(0)
    , power_(0)
    , last_data_time_(millis()) {
    
    mutex_ = xSemaphoreCreateMutex();
}

bool ConnectionState::is_transmitter_connected() const {
    SemaphoreGuard lock(mutex_);
    return transmitter_connected_;
}

void ConnectionState::set_transmitter_connected(bool connected) {
    {
        SemaphoreGuard lock(mutex_);
        
        if (transmitter_connected_ == connected) {
            return;  // No change
        }
        
        transmitter_connected_ = connected;
        
        if (connected) {
            connection_start_time_ = millis();
            connection_count_++;
        }
    }
    
    // Notify callbacks outside lock
    for (auto& callback : callbacks_) {
        callback(connected);
    }
}

uint8_t ConnectionState::get_soc() const {
    SemaphoreGuard lock(mutex_);
    return soc_;
}

void ConnectionState::set_soc(uint8_t soc) {
    SemaphoreGuard lock(mutex_);
    soc_ = soc;
    last_data_time_ = millis();
}

bool ConnectionState::is_data_stale(uint32_t timeout_ms) const {
    SemaphoreGuard lock(mutex_);
    return (millis() - last_data_time_) > timeout_ms;
}

// SemaphoreGuard helper
class SemaphoreGuard {
public:
    explicit SemaphoreGuard(SemaphoreHandle_t sem)
        : semaphore_(sem) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
    }
    
    ~SemaphoreGuard() {
        xSemaphoreGive(semaphore_);
    }
    
private:
    SemaphoreHandle_t semaphore_;
};
```

### Update Call Sites

```cpp
// BEFORE - All over the codebase
if (transmitter_connected) {
    // show status
}
received_soc = new_soc;

// AFTER - Cleaner API
auto& conn_state = ConnectionState::instance();
if (conn_state.is_transmitter_connected()) {
    // show status
}
conn_state.set_soc(new_soc);

// Can also subscribe to changes
conn_state.on_connection_changed([](bool connected) {
    if (connected) {
        LOG_INFO("Transmitter connected");
    }
});
```

### Benefits

- ✅ **Thread-safe**: Mutex-protected access
- ✅ **No race conditions**: Proper synchronization
- ✅ **Change notifications**: Can react to state changes
- ✅ **Timestamps**: Know when data is stale
- ✅ **Testable**: Can be mocked for unit tests
- ✅ **Statistics**: Track connection uptime

### Code Changes Required

**Files to create**:
- `src/state/connection_state.h` - Connection state manager
- `src/state/connection_state.cpp` - Implementation

**Files to modify**:
- `src/espnow/rx_connection_handler.cpp` - Use new API
- `src/api_handlers.cpp` - Use new API
- `src/display/monitor2_page.cpp` - Use new API
- `src/mqtt/mqtt_client.cpp` - Use new API

**Dependencies**:
- None - purely local improvement

---

## 3. Remove Debug Serial.printf Code

### Priority: 🔴 **CRITICAL**
**Effort**: 2 hours  
**Blocking**: No - independent improvement

### Problem

Production code contains debug output:

```cpp
// mqtt_client.cpp:237-272
void on_message(...) {
    // ...
    Serial.printf("DEBUG: Received MQTT message\n");
    Serial.printf("Topic: %s, Payload: %s\n", topic.c_str(), payload.c_str());
    Serial.printf("Connection state: %d\n", (int)transmitter_connected);
    // ...
}
```

### Solution

Replace with proper logging system:

```cpp
// BEFORE
Serial.printf("Received MQTT message\n");
Serial.printf("Topic: %s\n", topic.c_str());

// AFTER
LOG_DEBUG("[MQTT] Received message on %s", topic.c_str());
```

**Note**: Logging system already exists in receiver (`LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`)

### Benefits

- ✅ **Better output control**: Can enable/disable by level
- ✅ **Consistent format**: All logs use same format
- ✅ **Timestamps**: All logs include timing
- ✅ **Module names**: Can filter by component
- ✅ **Performance**: Less serial I/O overhead

---

## 4. Display Static Variables Refactoring

### Priority: 🟡 **HIGH**
**Effort**: 1.5 days  
**Blocking**: Yes - after Display HAL

### Problem

Static variables in display functions hide state:

```cpp
// display_core.cpp - ~180 lines of state hidden in function
void display_centered_proportional_number(float number) {
    static float last_number = 0;
    static uint32_t update_time = 0;
    static char buffer[32] = {};
    static uint16_t last_x = 0;
    static bool force_redraw = true;
    
    // 40+ lines of state management
    if (force_redraw || abs(number - last_number) > 0.5) {
        // Redraw logic
        last_number = number;
        force_redraw = false;
    }
}
```

### Solution

Convert to object with clear state:

```cpp
// display/widgets/number_widget.h
class NumberWidget {
public:
    NumberWidget(IDisplayDriver& driver, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    
    void set_value(float value);
    void update();  // Call on redraw
    
    void set_font_size(uint8_t size);
    void set_colors(uint16_t fg, uint16_t bg);
    void set_precision(uint8_t decimal_places);
    
private:
    IDisplayDriver& driver_;
    
    uint16_t x_, y_, width_, height_;
    float current_value_;
    float last_rendered_value_;
    uint32_t last_update_time_;
    
    uint16_t fg_color_, bg_color_;
    uint8_t font_size_;
    uint8_t precision_;
    
    bool needs_redraw() const;
    void render_value();
};

// display/widgets/progress_bar_widget.h
class ProgressBarWidget {
public:
    ProgressBarWidget(IDisplayDriver& driver, uint16_t x, uint16_t y, uint16_t width);
    
    void set_value(uint8_t percentage);  // 0-100
    void update();
    
    void set_colors(uint16_t fg, uint16_t bg);
    void set_label(const char* label);
    
private:
    IDisplayDriver& driver_;
    
    uint16_t x_, y_, width_;
    uint8_t current_percentage_;
    uint8_t last_rendered_percentage_;
    
    uint16_t fg_color_, bg_color_;
    char label_[32];
    
    bool needs_redraw() const;
    void render_bar();
};
```

### Usage in Display Pages

```cpp
// display/pages/status_page.h
class StatusPage {
public:
    explicit StatusPage(IDisplayDriver& driver);
    
    void update(const ConnectionState& state);
    void render();
    
private:
    NumberWidget soc_display_;
    NumberWidget voltage_display_;
    NumberWidget power_display_;
    ProgressBarWidget battery_bar_;
    
    IDisplayDriver& driver_;
};

// display/pages/status_page.cpp
void StatusPage::update(const ConnectionState& state) {
    soc_display_.set_value(state.get_soc());
    voltage_display_.set_value(state.get_voltage() / 100.0f);
    power_display_.set_value(state.get_power() / 1000.0f);
    battery_bar_.set_value(state.get_soc());
}

void StatusPage::render() {
    driver_.fill_screen(COLOR_BACKGROUND);
    
    soc_display_.update();
    voltage_display_.update();
    power_display_.update();
    battery_bar_.update();
}
```

### Benefits

- ✅ **Testable**: Each widget can be tested independently
- ✅ **Reusable**: Widgets can be used on multiple pages
- ✅ **Clear state**: No hidden statics
- ✅ **Composable**: Pages build from widgets
- ✅ **Maintainable**: Logic is localized

---

## 5. API Handler File Splitting

### Priority: 🟡 **MEDIUM**
**Effort**: 1 day  
**Blocking**: No - independent improvement

### Problem

[api_handlers.cpp](../../../espnowreciever_2/src/webserver/api_handlers.cpp) is 2354 lines - monolithic and hard to navigate.

### Solution

Split into multiple files by concern:

```
src/webserver/
├── api/
│   ├── api_data_handler.h/cpp          (Status, measurements - 200 lines)
│   ├── api_config_handler.h/cpp        (Settings, configuration - 400 lines)
│   ├── api_connection_handler.h/cpp    (Transmitter state - 150 lines)
│   ├── api_system_handler.h/cpp        (System, OTA, reset - 300 lines)
│   ├── api_diagnostic_handler.h/cpp    (Logs, stats, debug - 200 lines)
│   └── api_base.h                      (Shared utilities - 100 lines)
└── api_handlers.cpp                    (Router, 100 lines)
```

### Benefits

- ✅ **Modular**: Each handler is independent
- ✅ **Navigable**: Easier to find code
- ✅ **Testable**: Each handler can be tested
- ✅ **Maintainable**: Smaller files
- ✅ **Extensible**: Easy to add new handlers

---

## 6. System State Machine Enhancement

### Priority: 🟡 **MEDIUM**
**Effort**: 1 day  
**Blocking**: No - independent improvement

### Problem

`SystemState` enum exists but is underutilized:

```cpp
// state_machine.h
enum class SystemState {
    BOOTING,
    TEST_MODE,
    WAITING_FOR_TRANSMITTER,
    NORMAL_OPERATION,
    ERROR_STATE
};
```

But used only for display transitions, not timeout management.

### Solution

Use state machine for timeout handling:

```cpp
// state_machine.h
enum class SystemState {
    BOOTING,                      // Initializing hardware
    TEST_MODE,                    // Self-test mode
    WAITING_FOR_TRANSMITTER,      // No connection yet
    NORMAL_OPERATION,             // Connected and operating
    DEGRADED_MODE,                // Partial connectivity
    NETWORK_ERROR,                // Network issues
    DATA_STALE_ERROR,             // No data received
    ERROR_STATE                   // Unrecoverable error
};

class SystemStateManager {
public:
    static SystemStateManager& instance();
    
    SystemState get_state() const;
    void update();  // Call regularly (e.g., every 100ms)
    
    void on_state_changed(std::function<void(SystemState)> callback);
    
    // Timeout tracking
    uint32_t get_state_duration_ms() const;
    bool is_timeout() const;
    
private:
    SystemState current_state_;
    uint32_t state_entry_time_;
    
    static constexpr uint32_t BOOT_TIMEOUT_MS = 30000;      // 30s
    static constexpr uint32_t TX_WAIT_TIMEOUT_MS = 60000;   // 60s
    static constexpr uint32_t DATA_STALE_TIMEOUT_MS = 10000; // 10s
    
    void check_state_transitions();
    void transition_to(SystemState new_state);
};
```

### Implementation

```cpp
// state_machine.cpp
void SystemStateManager::update() {
    uint32_t now = millis();
    uint32_t elapsed = now - state_entry_time_;
    
    // Check for state-specific timeouts
    switch (current_state_) {
        case SystemState::BOOTING:
            if (elapsed > BOOT_TIMEOUT_MS) {
                LOG_WARN("[STATE] Boot timeout, entering error state");
                transition_to(SystemState::ERROR_STATE);
            }
            break;
            
        case SystemState::WAITING_FOR_TRANSMITTER:
            if (elapsed > TX_WAIT_TIMEOUT_MS) {
                LOG_WARN("[STATE] Transmitter wait timeout");
                transition_to(SystemState::NETWORK_ERROR);
            }
            break;
            
        case SystemState::NORMAL_OPERATION: {
            auto& conn = ConnectionState::instance();
            
            if (!conn.is_transmitter_connected()) {
                transition_to(SystemState::WAITING_FOR_TRANSMITTER);
            }
            else if (conn.is_data_stale(DATA_STALE_TIMEOUT_MS)) {
                transition_to(SystemState::DATA_STALE_ERROR);
            }
            break;
        }
        
        case SystemState::ERROR_STATE:
            // Wait for manual intervention or timeout recovery
            if (elapsed > 30000) {  // Retry after 30 seconds
                LOG_INFO("[STATE] Error recovery attempt");
                transition_to(SystemState::BOOTING);
            }
            break;
    }
}

void SystemStateManager::transition_to(SystemState new_state) {
    if (current_state_ == new_state) return;
    
    LOG_INFO("[STATE] Transition %d → %d",
            (int)current_state_, (int)new_state);
    
    current_state_ = new_state;
    state_entry_time_ = millis();
    
    // Notify listeners
    for (auto& callback : callbacks_) {
        callback(new_state);
    }
}
```

### Benefits

- ✅ **Robust**: Automatic timeout detection
- ✅ **Testable**: State machine behavior is explicit
- ✅ **Observable**: Can log all state transitions
- ✅ **Graceful degradation**: Recognizes partial failures
- ✅ **Recovery**: Can retry from error states

---

## 7. SSE Connection Error Handling

### Priority: 🟡 **MEDIUM**
**Effort**: 8 hours  
**Blocking**: No - independent improvement

### Problem

Server-Sent Events (SSE) error handling lacks exponential backoff:

```cpp
// monitor2_page.cpp - Browser JavaScript
const eventSource = new EventSource('/api/sse');

eventSource.addEventListener('error', () => {
    // Just logs error, retries immediately
    console.error('Connection lost');
    // Browser auto-reconnects after 1 second (no backoff)
});
```

If server is down, browser hammers it with reconnection attempts.

### Solution

Implement client-side exponential backoff:

```cpp
// web/js/sse_client.js
class SSEClient {
    constructor(url, handlers) {
        this.url = url;
        this.handlers = handlers;
        this.eventSource = null;
        this.reconnectDelay = 1000;  // Start at 1 second
        this.maxDelay = 30000;       // Cap at 30 seconds
        this.isConnected = false;
    }
    
    connect() {
        this.eventSource = new EventSource(this.url);
        
        this.eventSource.addEventListener('data', (event) => {
            this.resetReconnectDelay();
            this.isConnected = true;
            
            const data = JSON.parse(event.data);
            if (this.handlers.onData) {
                this.handlers.onData(data);
            }
        });
        
        this.eventSource.addEventListener('status', (event) => {
            const status = JSON.parse(event.data);
            if (this.handlers.onStatus) {
                this.handlers.onStatus(status);
            }
        });
        
        this.eventSource.addEventListener('error', (event) => {
            this.isConnected = false;
            this.eventSource.close();
            
            if (this.handlers.onError) {
                this.handlers.onError(event);
            }
            
            // Schedule reconnection with exponential backoff
            console.warn(`SSE Error, reconnecting in ${this.reconnectDelay}ms`);
            setTimeout(() => this.connect(), this.reconnectDelay);
            
            this.reconnectDelay = Math.min(
                this.reconnectDelay * 1.5,
                this.maxDelay
            );
        });
    }
    
    resetReconnectDelay() {
        this.reconnectDelay = 1000;
    }
    
    disconnect() {
        if (this.eventSource) {
            this.eventSource.close();
        }
    }
}

// Usage
const sse = new SSEClient('/api/sse', {
    onData: (data) => {
        console.log('Received data:', data);
        updateDisplay(data);
    },
    onStatus: (status) => {
        console.log('Status:', status);
    },
    onError: (error) => {
        console.error('Connection error');
        updateUIOffline();
    }
});

sse.connect();
```

### Server-Side Enhancement

Also send reconnection hints from server:

```cpp
// webserver.cpp
void send_sse_data() {
    // Include hint about when to retry
    uint32_t retry_ms = 5000;  // Suggest 5 second retry
    
    client.printf("retry: %u\n", retry_ms);
    client.printf("data: %s\n\n", json_data.c_str());
}

// On server error
void handle_sse_error() {
    uint32_t retry_ms = 30000;  // Suggest 30 second retry on error
    client.printf("retry: %u\n", retry_ms);
    client.printf("data: {\"error\": \"Server error\"}\n\n");
}
```

### Benefits

- ✅ **Reduces server load**: Exponential backoff prevents hammering
- ✅ **Better UX**: Clear offline/online status
- ✅ **Server hints**: Server can suggest retry timing
- ✅ **Automatic recovery**: Reconnects when server is back

---

## 8. Magic Number Extraction

### Priority: 🟢 **LOW**
**Effort**: 4 hours  
**Blocking**: No - independent improvement

### Problem

Magic numbers scattered throughout display code:

```cpp
// display_core.cpp
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define ROTATION 1

// display_proportional_number
const int CHAR_WIDTH = 120;
const int CHAR_HEIGHT = 16;
const int MARGIN_TOP = 6;
const int MARGIN_LEFT = 4;

// font setup
text_size = 2;
text_color = 0xFFFF;  // White
bg_color = 0x0000;    // Black

// Layout
x_pos = DISPLAY_WIDTH / 2;  // Center - magic
y_pos = DISPLAY_HEIGHT / 2;
```

### Solution

Create configuration header:

```cpp
// config/display_config.h
namespace DisplayConfig {
    // Hardware
    constexpr uint16_t DISPLAY_WIDTH = 320;
    constexpr uint16_t DISPLAY_HEIGHT = 240;
    constexpr uint8_t DISPLAY_ROTATION = 1;
    
    // Colors
    constexpr uint16_t COLOR_BACKGROUND = 0x0000;  // Black
    constexpr uint16_t COLOR_TEXT = 0xFFFF;        // White
    constexpr uint16_t COLOR_ACCENT = 0x001F;      // Blue
    constexpr uint16_t COLOR_WARNING = 0xF800;     // Red
    constexpr uint16_t COLOR_SUCCESS = 0x07E0;     // Green
    
    // Layout
    constexpr uint16_t CONTENT_X = 20;
    constexpr uint16_t CONTENT_Y = 20;
    constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - 40;
    constexpr uint16_t CONTENT_HEIGHT = DISPLAY_HEIGHT - 40;
    
    // Typography
    constexpr uint8_t FONT_SIZE_SMALL = 1;
    constexpr uint8_t FONT_SIZE_NORMAL = 2;
    constexpr uint8_t FONT_SIZE_LARGE = 3;
    
    // Widget dimensions
    constexpr uint16_t NUMBER_DISPLAY_WIDTH = 120;
    constexpr uint16_t NUMBER_DISPLAY_HEIGHT = 40;
    constexpr uint16_t PROGRESS_BAR_HEIGHT = 20;
    
    // Timing
    constexpr uint32_t PAGE_TRANSITION_MS = 5000;
    constexpr uint32_t SPINNER_UPDATE_MS = 100;
    constexpr uint32_t DATA_UPDATE_INTERVAL_MS = 500;
}

// Usage
tft.fillScreen(DisplayConfig::COLOR_BACKGROUND);
tft.setTextColor(DisplayConfig::COLOR_TEXT);

const auto center_x = DisplayConfig::DISPLAY_WIDTH / 2;
const auto center_y = DisplayConfig::DISPLAY_HEIGHT / 2;
```

### Benefits

- ✅ **Consistency**: All colors/sizes use same definitions
- ✅ **Maintainability**: Change once, updates everywhere
- ✅ **Clarity**: Meaningful names instead of numbers
- ✅ **Portability**: Easy to adapt for different displays

---

## 9. Naming Convention Standardization

### Priority: 🟢 **LOW**
**Effort**: 4 hours  
**Blocking**: No - independent improvement

### Problem

Inconsistent naming styles:

```cpp
// Boolean functions mixed
bool isConnected();        // camelCase
bool get_transmitter_state();  // snake_case
bool is_data_available();  // snake_case
bool hasNewMessage();      // camelCase
bool connectToTransmitter();   // camelCase (verb)

// Variables mixed
uint8_t soc;              // Short name
uint8_t transmitter_connected;  // Full name
bool tx_active;           // Abbreviated
bool receiver_enabled;     // Full
```

### Solution

Adopt consistent convention (snake_case for receiver):

```cpp
// Setters/getters
class ConnectionState {
public:
    // Getters (query state)
    bool is_transmitter_connected() const;
    uint8_t get_state_of_charge() const;
    uint32_t get_last_update_time_ms() const;
    
    // Setters (modify state)
    void set_transmitter_connected(bool connected);
    void set_state_of_charge(uint8_t soc);
    
    // Checkers (predicate)
    bool has_data_available() const;
    bool is_data_stale(uint32_t timeout_ms) const;
    
    // Actions (commands)
    void reset_connection();
    void force_update();
};

// Free functions
bool validate_mac_address(const uint8_t* mac);
void log_connection_state(const ConnectionState& state);
std::string format_mac_address(const uint8_t* mac);
```

### Naming Guidelines

```
Getters:          get_<property>() or is_<property>()
Setters:          set_<property>()
Predicates:       is_<condition>(), has_<thing>()
Actions:          <verb>_<object>() - e.g., reset_connection()
Constructors:     ClassName() - matches class name
Helper functions: lower_snake_case
Variables:        lower_snake_case
Constants:        UPPER_SNAKE_CASE
Classes:          PascalCase
Enums:            PascalCase
Enum values:      UPPER_SNAKE_CASE
```

---

## 10. JavaScript Consolidation

### Priority: 🟢 **LOW**
**Effort**: 3 hours  
**Blocking**: No - independent improvement

### Problem

Duplicate JavaScript code across multiple HTML pages:

```html
<!-- monitor2_page.html -->
<script>
function updateDisplay(data) { ... }
function formatNumber(n) { ... }
const eventSource = new EventSource('/api/sse');
</script>

<!-- settings_page.html -->
<script>
function updateDisplay(data) { ... }  // ← Duplicate
function formatNumber(n) { ... }      // ← Duplicate
function fetchSettings() { ... }
</script>

<!-- diagnostic_page.html -->
<script>
function formatNumber(n) { ... }      // ← Duplicate (3rd time!)
function fetchLogs() { ... }
</script>
```

### Solution

Extract shared code:

```
web/
├── js/
│   ├── shared_utils.js      (formatNumber, updateDisplay, etc.)
│   ├── sse_client.js        (SSE connection logic)
│   ├── api_client.js        (Fetch wrapper)
│   ├── ui_helpers.js        (DOM manipulation)
│   └── display_updater.js   (Display refresh logic)
├── html/
│   ├── monitor2_page.html
│   ├── settings_page.html
│   └── diagnostic_page.html
```

```html
<!-- Shared header in all pages -->
<script src="/js/shared_utils.js"></script>
<script src="/js/sse_client.js"></script>
<script src="/js/api_client.js"></script>
<script src="/js/ui_helpers.js"></script>

<!-- Page-specific script -->
<script src="/js/monitor2_page.js"></script>
```

```javascript
// js/shared_utils.js
function formatNumber(value, decimals = 1) {
    return value.toFixed(decimals);
}

function formatCurrency(value) {
    return '$' + value.toFixed(2);
}

function formatPercent(value) {
    return value.toFixed(1) + '%';
}

function createTableRow(cells) {
    const tr = document.createElement('tr');
    cells.forEach(cell => {
        const td = document.createElement('td');
        td.textContent = cell;
        tr.appendChild(td);
    });
    return tr;
}
```

### Benefits

- ✅ **DRY principle**: Single source of truth
- ✅ **Maintenance**: Change once, updates everywhere
- ✅ **Load time**: Browser caches shared scripts
- ✅ **Testability**: Shared functions can be tested independently

---

## Summary

### Implementation Order

**Week 1** (Critical fixes):
1. Display HAL → Blocks further display work
2. Global Volatile Encapsulation → Needed for state management
3. Remove Serial.printf → Quick win

**Week 2** (High priority):
4. Display Static Variables → Depends on HAL
5. API Handler Splitting → Modular improvements
6. System State Machine → Uses ConnectionState

**Week 3** (Medium priority):
7. SSE Error Handling → Browser-side only
8. Magic Numbers → Clarity improvement

**Week 4** (Low priority):
9. Naming Standardization → Code polish
10. JavaScript Consolidation → Maintenance improvement

### Total Effort

- **Critical**: 5.5 days
- **High**: 2.5 days
- **Medium**: 1.25 days
- **Low**: 1 day

**Total**: ~10 days for all receiver-specific improvements

### Key Points

- **All improvements are independent** - don't require transmitter changes
- **Display HAL must be done first** - blocks multiple other improvements
- **Global encapsulation is second** - needed for proper state management
- **Can parallelize**: Some improvements can be done simultaneously by different developers

