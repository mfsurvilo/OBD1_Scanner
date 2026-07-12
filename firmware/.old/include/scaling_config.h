#ifndef SCALING_CONFIG_H
#define SCALING_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

//=============================================================================
// Scaling Factor Configuration
// Stores custom scaling multipliers for each parameter in flash/EEPROM
//=============================================================================

// Maximum number of parameters we can store scaling for
#define MAX_SCALING_PARAMS 20

// Magic number to verify valid data in storage
#define SCALING_MAGIC 0x5343414C  // "SCAL"

struct ScalingFactor {
    char name[24];      // Parameter name (e.g., "EngineSpeed")
    float multiplier;   // calibration_slope (default 1.0)
    float offset;       // calibration_offset (default 0.0)
    int   factor;       // polling frequency factor (default -1 = not set)
};

class ScalingConfigClass {
public:
    ScalingConfigClass();
    
    // Initialize - load from flash
    void begin();
    
    // Get scaling factor for a parameter (returns 1.0 if not found)
    float getMultiplier(const char* paramName);
    float getOffset(const char* paramName);
    
    // Set scaling factor for a parameter (factor < 0 leaves the poll factor unchanged)
    bool setScaling(const char* paramName, float multiplier, float offset = 0.0f, int factor = -1);

    // Get stored poll factor for a parameter (-1 if not set)
    int getFactor(const char* paramName);
    
    // Save all current scaling factors to flash
    bool save();
    
    // Load scaling factors from flash
    bool load();
    
    // Reset to defaults (all multipliers = 1.0, offsets = 0.0)
    void resetToDefaults();
    
    // Apply scaling to a raw value
    float applyScaling(const char* paramName, float rawValue);
    
    // Get all scaling factors as JSON for sending to clients
    void toJson(JsonDocument& doc);
    
    // Set scaling factors from JSON received from client
    bool fromJson(JsonDocument& doc);
    
    // Get count of configured parameters
    int getCount() const { return _count; }

private:
    Preferences _prefs;
    ScalingFactor _factors[MAX_SCALING_PARAMS];
    int _count;
    
    int _findIndex(const char* paramName);
    int _addParameter(const char* paramName);
};

// Global instance
extern ScalingConfigClass ScalingConfig;

#endif // SCALING_CONFIG_H
