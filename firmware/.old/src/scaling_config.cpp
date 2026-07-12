#include "scaling_config.h"
#include "logger.h"

// Global instance
ScalingConfigClass ScalingConfig;

//=============================================================================
// Constructor
//=============================================================================

ScalingConfigClass::ScalingConfigClass() : _count(0) {
    // Initialize all factors to defaults
    for (int i = 0; i < MAX_SCALING_PARAMS; i++) {
        _factors[i].name[0] = '\0';
        _factors[i].multiplier = 1.0f;
        _factors[i].offset = 0.0f;
        _factors[i].factor = -1;
    }
}

//=============================================================================
// Public Methods
//=============================================================================

void ScalingConfigClass::begin() {
    Log.info("ScalingConfig: Initializing...");
    if (!load()) {
        Log.info("ScalingConfig: No saved config, using defaults");
        resetToDefaults();
    } else {
        Log.infof("ScalingConfig: Loaded %d parameters", _count);
    }
}

float ScalingConfigClass::getMultiplier(const char* paramName) {
    int idx = _findIndex(paramName);
    if (idx >= 0) {
        return _factors[idx].multiplier;
    }
    return 1.0f;  // Default multiplier
}

float ScalingConfigClass::getOffset(const char* paramName) {
    int idx = _findIndex(paramName);
    if (idx >= 0) {
        return _factors[idx].offset;
    }
    return 0.0f;  // Default offset
}

bool ScalingConfigClass::setScaling(const char* paramName, float multiplier, float offset, int factor) {
    int idx = _findIndex(paramName);
    if (idx < 0) {
        idx = _addParameter(paramName);
        if (idx < 0) {
            Log.errorf("ScalingConfig: Cannot add param %s - storage full", paramName);
            return false;
        }
    }

    _factors[idx].multiplier = multiplier;
    _factors[idx].offset = offset;
    if (factor >= 0) _factors[idx].factor = factor;  // negative = leave unchanged
    Log.infof("ScalingConfig: Set %s slope=%.4f off=%.2f factor=%d",
              paramName, multiplier, offset, _factors[idx].factor);
    return true;
}

int ScalingConfigClass::getFactor(const char* paramName) {
    int idx = _findIndex(paramName);
    return (idx >= 0) ? _factors[idx].factor : -1;
}

bool ScalingConfigClass::save() {
    _prefs.begin("scaling", false);  // false = read/write mode
    
    // Write magic number and count
    _prefs.putUInt("magic", SCALING_MAGIC);
    _prefs.putInt("count", _count);
    
    // Write each factor
    for (int i = 0; i < _count; i++) {
        char keyName[16], keyMult[16], keyOff[16];
        snprintf(keyName, sizeof(keyName), "n%d", i);
        snprintf(keyMult, sizeof(keyMult), "m%d", i);
        snprintf(keyOff, sizeof(keyOff), "o%d", i);
        
        char keyFac[16];
        snprintf(keyFac, sizeof(keyFac), "f%d", i);

        _prefs.putString(keyName, _factors[i].name);
        _prefs.putFloat(keyMult, _factors[i].multiplier);
        _prefs.putFloat(keyOff, _factors[i].offset);
        _prefs.putInt(keyFac, _factors[i].factor);
    }
    
    _prefs.end();
    Log.infof("ScalingConfig: Saved %d parameters to flash", _count);
    return true;
}

bool ScalingConfigClass::load() {
    _prefs.begin("scaling", true);  // true = read-only mode
    
    // Check magic number
    uint32_t magic = _prefs.getUInt("magic", 0);
    if (magic != SCALING_MAGIC) {
        _prefs.end();
        return false;
    }
    
    _count = _prefs.getInt("count", 0);
    if (_count > MAX_SCALING_PARAMS) {
        _count = MAX_SCALING_PARAMS;
    }
    
    // Read each factor
    for (int i = 0; i < _count; i++) {
        char keyName[16], keyMult[16], keyOff[16];
        snprintf(keyName, sizeof(keyName), "n%d", i);
        snprintf(keyMult, sizeof(keyMult), "m%d", i);
        snprintf(keyOff, sizeof(keyOff), "o%d", i);
        
        String name = _prefs.getString(keyName, "");
        strncpy(_factors[i].name, name.c_str(), sizeof(_factors[i].name) - 1);
        _factors[i].name[sizeof(_factors[i].name) - 1] = '\0';
        
        char keyFac[16];
        snprintf(keyFac, sizeof(keyFac), "f%d", i);

        _factors[i].multiplier = _prefs.getFloat(keyMult, 1.0f);
        _factors[i].offset = _prefs.getFloat(keyOff, 0.0f);
        _factors[i].factor = _prefs.getInt(keyFac, -1);
    }
    
    _prefs.end();
    return true;
}

void ScalingConfigClass::resetToDefaults() {
    _count = 0;
    for (int i = 0; i < MAX_SCALING_PARAMS; i++) {
        _factors[i].name[0] = '\0';
        _factors[i].multiplier = 1.0f;
        _factors[i].offset = 0.0f;
        _factors[i].factor = -1;
    }
    Log.info("ScalingConfig: Reset to defaults");
}

float ScalingConfigClass::applyScaling(const char* paramName, float rawValue) {
    int idx = _findIndex(paramName);
    if (idx >= 0) {
        return (rawValue * _factors[idx].multiplier) + _factors[idx].offset;
    }
    return rawValue;  // No scaling configured
}

void ScalingConfigClass::toJson(JsonDocument& doc) {
    JsonArray arr = doc["scaling"].to<JsonArray>();
    
    for (int i = 0; i < _count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = _factors[i].name;
        obj["multiplier"] = _factors[i].multiplier;
        obj["offset"] = _factors[i].offset;
        obj["factor"] = _factors[i].factor;
    }
    
    doc["scalingCount"] = _count;
}

bool ScalingConfigClass::fromJson(JsonDocument& doc) {
    if (!doc["scaling"].is<JsonArray>()) {
        return false;
    }
    
    JsonArray arr = doc["scaling"];
    
    for (JsonObject obj : arr) {
        const char* name = obj["name"];
        float mult = obj["multiplier"] | 1.0f;
        float off = obj["offset"] | 0.0f;
        int fac = obj["factor"] | -1;

        if (name && strlen(name) > 0) {
            setScaling(name, mult, off, fac);
        }
    }
    
    return true;
}

//=============================================================================
// Private Methods
//=============================================================================

int ScalingConfigClass::_findIndex(const char* paramName) {
    for (int i = 0; i < _count; i++) {
        if (strcmp(_factors[i].name, paramName) == 0) {
            return i;
        }
    }
    return -1;
}

int ScalingConfigClass::_addParameter(const char* paramName) {
    if (_count >= MAX_SCALING_PARAMS) {
        return -1;
    }
    
    int idx = _count;
    strncpy(_factors[idx].name, paramName, sizeof(_factors[idx].name) - 1);
    _factors[idx].name[sizeof(_factors[idx].name) - 1] = '\0';
    _factors[idx].multiplier = 1.0f;
    _factors[idx].offset = 0.0f;
    _factors[idx].factor = -1;
    _count++;
    
    return idx;
}
