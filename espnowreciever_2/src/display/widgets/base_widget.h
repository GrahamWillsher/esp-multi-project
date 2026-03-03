/**
 * Base Widget Interface
 * Abstract base for all display widgets
 */

#pragma once

#include <hal/display/idisplay_driver.h>
#include <cstdint>

namespace Display {
namespace Widgets {

/**
 * Base widget class for all display elements
 * Provides common positioning, visibility, and update interface
 */
class BaseWidget {
public:
    BaseWidget(HAL::IDisplayDriver* driver, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
        : driver_(driver), 
          x_(x), 
          y_(y), 
          width_(width), 
          height_(height),
          visible_(true),
          dirty_(true) {}
    
    virtual ~BaseWidget() = default;
    
    // Core update method - call every frame
    virtual void update() = 0;
    
    // Force redraw on next update
    void mark_dirty() { dirty_ = true; }
    
    // Show/hide widget
    void set_visible(bool visible) { 
        if (visible_ != visible) {
            visible_ = visible;
            dirty_ = true;
        }
    }
    
    bool is_visible() const { return visible_; }
    
    // Position accessors
    uint16_t get_x() const { return x_; }
    uint16_t get_y() const { return y_; }
    uint16_t get_width() const { return width_; }
    uint16_t get_height() const { return height_; }
    
    // Reposition widget
    void set_position(uint16_t x, uint16_t y) {
        if (x_ != x || y_ != y) {
            x_ = x;
            y_ = y;
            dirty_ = true;
        }
    }

protected:
    HAL::IDisplayDriver* driver_;
    
    uint16_t x_;
    uint16_t y_;
    uint16_t width_;
    uint16_t height_;
    
    bool visible_;
    bool dirty_;
    
    // Check if widget needs redrawing
    bool needs_redraw() const { return dirty_ && visible_; }
    
    // Clear dirty flag after rendering
    void clear_dirty() { dirty_ = false; }
};

} // namespace Widgets
} // namespace Display
