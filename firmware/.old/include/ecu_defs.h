#ifndef ECU_DEFS_H
#define ECU_DEFS_H

#include <stdint.h>
#include <string.h>

// Subaru SSM1 ECU Address Definitions
// Select your ECU by uncommenting ONE of the following defines:

// #define ECU_744014   // 1995 JDM WRX STi Version 2
// #define ECU_744011   // 1995 JDM WRX STi
// #define ECU_7136     // 1991 Turbo
// #define ECU_7236     // 1991-1994 Turbo
// #define ECU_7031     // 1990-1991 5MT NA
// #define ECU_7032     // 1990-1991 4EAT NA
// #define ECU_7232     // 1992 NA
#define ECU_7232A5      // 1992 UK Legacy EJ22 (Australia/UK)
// #define ECU_7432A1   // 1994 EJ22
// #define ECU_7332     // 1993-1994 NA
// #define ECU_A30117   // 1999 Forester EURO 2.0
// #define ECU_7225     // 1992 SVX
// #define ECU_7425     // 1994 SVX

//=============================================================================
// ECU Parameter Structure
//=============================================================================

struct EcuParam {
    uint16_t addr;
    const char* name;
};

//=============================================================================
// Hardcoded parameter mappings (raw -> engineering units)
//
// These conversion formulas are fixed properties of the SSM1 protocol, keyed by
// parameter name and shared across all ECU tables below. They are NOT user
// editable - the user's calibration_slope / calibration_offset are applied on
// top of this, downstream (in the app). See resources/possible_mapping.txt.
//
//     engineering = raw * scale + offset
//=============================================================================
struct ParamMapping {
    const char* name;
    float scale;
    float offset;
    const char* unit;
};

const ParamMapping paramMappings[] = {
    {"EngineSpeed",         25.0f,          0.0f,    "rpm"},   // rpm = v*25
    {"VehicleSpeed",        2.0f,           0.0f,    "km/h"},  // km/h = v*2 (metric; PWA may show mph)
    {"ThrottlePosition",    100.0f/255.0f,  0.0f,    "%"},     // % = v*100/255
    {"O2Average",           5000.0f/512.0f, 0.0f,    "mV"},    // mV = v*5000/512
    {"O2Minimum",           5000.0f/256.0f, 0.0f,    "mV"},    // mV = v*5000/256
    {"O2Maximum",           5000.0f/256.0f, 0.0f,    "mV"},    // mV = v*5000/256
    {"IgnitionAdvance",     1.0f,           0.0f,    "deg"},   // advance = v
    {"EngineLoad",          1.0f,           0.0f,    ""},      // load = v
    {"InjectorPulseWidth",  256.0f/1000.0f, 0.0f,    "ms"},    // ms = v*256/1000
    {"KnockCorrection",     1.0f,           0.0f,    "deg"},   // correction = v
    {"AFCorrection",        1.0f,           -128.0f, ""},      // afc = v-128
    {"BatteryVoltage",      0.08f,          0.0f,    "V"},     // volts = v*0.08
    {"CoolantTemp",         1.0f,           -50.0f,  "C"},     // tempC = v-50 (metric; PWA may show F)
    {"AirflowSensor",       100.0f/255.0f,  0.0f,    "%"},     // % = v*100/255
    {"ISUDutyValve",        100.0f/255.0f,  0.0f,    "%"},     // duty% = v*100/255
    {"AtmosphericPressure", 1.0f,           0.0f,    "kPa"},   // (no documented formula)
    // Any param not listed falls back to identity (scale 1, offset 0, no unit).
};

// Returns the mapping for a param, or an identity mapping if none is defined.
inline ParamMapping getParamMapping(const char* name) {
    int n = sizeof(paramMappings) / sizeof(paramMappings[0]);
    for (int i = 0; i < n; i++) {
        if (strcmp(paramMappings[i].name, name) == 0) return paramMappings[i];
    }
    return ParamMapping{name, 1.0f, 0.0f, ""};
}

//=============================================================================
// Trouble Code Structure (from b10scan.asm)
// Each byte contains 8 DTCs as bits
//=============================================================================
struct TroubleCodeDef {
    uint16_t activeAddr[3];   // 3 bytes of active codes
    uint16_t storedAddr[3];   // 3 bytes of stored codes  
    uint16_t clearAddr;       // Address to write for clearing
    uint8_t clearValue;       // Value to write (0x00 or inverted)
};

// DTC bit definitions - same for all ECUs (from b10scan.asm)
// Byte 1 (index 0): bits 0-6
//   bit 0 = Code 11 (Crank sensor)
//   bit 1 = Code 12 (Starter switch)
//   bit 2 = Code 13 (Cam sensor)
//   bit 3 = Code 14 (Injector #1)
//   bit 4 = Code 15 (Injector #2)
//   bit 5 = Code 16 (Injector #3)
//   bit 6 = Code 17 (Injector #4)
//
// Byte 2 (index 1): bits 0-7
//   bit 0 = Code 21 (Coolant temp)
//   bit 1 = Code 22 (Knock sensor)
//   bit 2 = Code 23 (MAF sensor)
//   bit 3 = Code 24 (IAC valve)
//   bit 4 = Code 31 (TPS)
//   bit 5 = Code 32 (Oxygen sensor)
//   bit 6 = Code 33 (Vehicle speed)
//   bit 7 = Code 35 (Purge solenoid)
//
// Byte 3 (index 2): bits vary by ECU
//   bit 0 = Code 41 (Fuel trim)
//   bit 1 = Code 42 (Idle switch)
//   bit 3 = Code 44 (Wastegate - turbo only)
//   bit 4 = Code 45 (Baro pressure)
//   bit 5 = Code 49 (Wrong MAF)
//   bit 6 = Code 51 (Neutral switch)
//   bit 7 = Code 52 (Park switch)

//=============================================================================
// 1995 JDM WRX STi Version 2 (ROM ID: 744014)
//=============================================================================
#ifdef ECU_744014
#define ECU_ROM_ID "744014"
#define ECU_NAME "1995 JDM WRX STi Version 2"

// TODO: Find correct DTC addresses for this ECU
const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346}, {0x134B, 0x134A, 0x1349}, 0x135E, 0x00
};

const EcuParam ecuParams[] = {
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},
    {0x1329, "ThrottlePosition"},
    {0x130A, "O2Average"},
    {0x1053, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1054, "KnockCorrection"},
    {0x1055, "AFCorrection"},
    {0x1341, "ManifoldPressure"},
    {0x1344, "BoostSolenoidDutyCycle"},
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1308, "AirflowSensor"},
    {0x1307, "ISUDutyValve"},
    {0x1340, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1995 JDM WRX STi (ROM ID: 744011)
//=============================================================================
#ifdef ECU_744011
#define ECU_ROM_ID "744011"
#define ECU_NAME "1995 JDM WRX STi"

const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346}, {0x134B, 0x134A, 0x1349}, 0x135E, 0x00
};

const EcuParam ecuParams[] = {
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},
    {0x1329, "ThrottlePosition"},
    {0x130A, "O2Average"},
    {0x1053, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1054, "KnockCorrection"},
    {0x1055, "AFCorrection"},
    {0x1341, "ManifoldPressure"},
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1308, "AirflowSensor"},
    {0x1307, "ISUDutyValve"},
    {0x1340, "AtmosphericPressure"},
    {0x1343, "InputSwitches"},
    {0x1344, "IOSwitches"},
};
#endif

//=============================================================================
// 1991 Turbo (ROM ID: 7136) - Early Turbo / Hitachi
// From b10scan.asm early_turbo_params
//=============================================================================
#ifdef ECU_7136
#define ECU_ROM_ID "7136"
#define ECU_NAME "1991 Turbo"

const TroubleCodeDef troubleCodes = {
    {0x0047, 0x0048, 0x0049},  // Active (hitachi_u1, u2, u3)
    {0x1604, 0x1605, 0x1606},  // Stored (hitachi_m1_n, m2_n, m3_n)
    0x15F0, 0xFF               // Clear at 15F0, invert value
};

const EcuParam ecuParams[] = {
    {0x140B, "EngineSpeed"},
    {0x154B, "VehicleSpeed"},
    {0x1487, "ThrottlePosition"},
    {0x1403, "O2Average"},
    {0x1489, "IgnitionAdvance"},
    {0x1414, "EngineLoad"},
    {0x142A, "InjectorPulseWidth"},
    {0x1530, "KnockCorrection"},
    {0x1488, "AFCorrection"},
    {0x00BE, "ManifoldPressure"},
    {0x144D, "BoostSolenoidDutyCycle"},
    {0x1404, "BatteryVoltage"},
    {0x1405, "CoolantTemp"},
    {0x1400, "AirflowSensor"},
    {0x158A, "ISUDutyValve"},
    {0x1516, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1991-1994 Turbo (ROM ID: 7236)
// From b10scan.asm turbo_params
//=============================================================================
#ifdef ECU_7236
#define ECU_ROM_ID "7236"
#define ECU_NAME "1991-1994 Turbo"

const TroubleCodeDef troubleCodes = {
    {0x0047, 0x0048, 0x0049},  // Active
    {0x1664, 0x1665, 0x1666},  // Stored (hitachi_m1_t, m2_t, m3_t)
    0x1650, 0x00               // Clear at 1650, write zero
};

const EcuParam ecuParams[] = {
    {0x140B, "EngineSpeed"},
    {0x154B, "VehicleSpeed"},
    {0x1487, "ThrottlePosition"},
    {0x1403, "O2Average"},
    {0x1489, "IgnitionAdvance"},
    {0x1414, "EngineLoad"},
    {0x15F0, "InjectorPulseWidth"},
    {0x1530, "KnockCorrection"},
    {0x1488, "AFCorrection"},
    {0x00BE, "ManifoldPressure"},
    {0x144D, "BoostSolenoidDutyCycle"},
    {0x1404, "BatteryVoltage"},
    {0x1405, "CoolantTemp"},
    {0x1400, "AirflowSensor"},
    {0x158A, "ISUDutyValve"},
    {0x1516, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1990-1991 5MT NA (ROM ID: 7031) - Hitachi
// From b10scan.asm hitachi_params
//=============================================================================
#ifdef ECU_7031
#define ECU_ROM_ID "7031"
#define ECU_NAME "1990-1991 5MT NA"

const TroubleCodeDef troubleCodes = {
    {0x0047, 0x0048, 0x0049},  // Active
    {0x1604, 0x1605, 0x1606},  // Stored
    0x1600, 0xFF               // Clear, invert value
};

const EcuParam ecuParams[] = {
    {0x140B, "EngineSpeed"},
    {0x154B, "VehicleSpeed"},
    {0x1487, "ThrottlePosition"},
    {0x1403, "O2Average"},
    {0x1489, "IgnitionAdvance"},
    {0x1414, "EngineLoad"},
    {0x142A, "InjectorPulseWidth"},
    {0x1530, "KnockCorrection"},
    {0x1488, "AFCorrection"},
    {0x1404, "BatteryVoltage"},
    {0x1405, "CoolantTemp"},
    {0x1400, "AirflowSensor"},
    {0x158A, "ISUDutyValve"},
    {0x140A, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1990-1991 4EAT NA (ROM ID: 7032) - JECS1
// From b10scan.asm jecs1_params
//=============================================================================
#ifdef ECU_7032
#define ECU_ROM_ID "7032"
#define ECU_NAME "1990-1991 4EAT NA"

const TroubleCodeDef troubleCodes = {
    {0x4407, 0x4406, 0x4405},  // Active (jecs1_u1, u2, u3)
    {0x440A, 0x4409, 0x4408},  // Stored (jecs1_m1, m2, m3)
    0x443F, 0xFF               // Clear, invert value
};

const EcuParam ecuParams[] = {
    {0x43BC, "EngineSpeed"},
    {0x4781, "VehicleSpeed"},
    {0x4784, "ThrottlePosition"},
    {0x43CF, "O2Average"},
    {0x43C8, "IgnitionAdvance"},
    {0x43AA, "EngineLoad"},
    {0x43AB, "InjectorPulseWidth"},
    {0x440D, "KnockCorrection"},
    {0x43CE, "AFCorrection"},
    {0x4780, "BatteryVoltage"},
    {0x4782, "CoolantTemp"},
    {0x43AD, "AirflowSensor"},
    {0x43E3, "ISUDutyValve"},
    {0x4787, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1992 NA (ROM ID: 7232) - JECS2
//=============================================================================
#ifdef ECU_7232
#define ECU_ROM_ID "7232"
#define ECU_NAME "1992 NA"

const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346},  // Active
    {0x134B, 0x134A, 0x1349},  // Stored
    0x135E, 0x00               // Clear addr, value
};

const EcuParam ecuParams[] = {
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},
    {0x1329, "ThrottlePosition"},
    {0x1310, "O2Average"},
    {0x1323, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1328, "KnockCorrection"},
    {0x133E, "AFCorrection"},
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1307, "AirflowSensor"},
    {0x1314, "ISUDutyValve"},
    {0x1340, "AtmosphericPressure"},
    {0x1343, "InputSwitches"},
    {0x1344, "IOSwitches"},
};
#endif

//=============================================================================
// 1992 UK Legacy EJ22 - Australia/UK (ROM ID: 7232A5)
// Uses JECS2 addresses from b10scan.asm
//=============================================================================
#ifdef ECU_7232A5
#define ECU_ROM_ID "7232A5"
#define ECU_NAME "1992 UK Legacy EJ22"

// Trouble code addresses for JECS2 (from b10scan.asm jecs2_* definitions)
const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346},  // Active trouble codes (u1, u2, u3)
    {0x134B, 0x134A, 0x1349},  // Stored trouble codes (m1, m2, m3)
    0x135E,                     // Clear address (jecs2_clear)
    0x00                        // Clear value (write zero)
};

const EcuParam ecuParams[] = {
    // Fast polling - rapidly changing values
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},

    // Medium polling - moderate change rate
    {0x1329, "ThrottlePosition"},
    {0x1310, "O2Average"},
    {0x1323, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1328, "KnockCorrection"},
    {0x133E, "AFCorrection"},

    // Slow polling - slow changing values
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1307, "AirflowSensor"},
    {0x1314, "ISUDutyValve"},

    // Very slow polling - rarely changing
    {0x1340, "AtmosphericPressure"},
    {0x1343, "InputSwitches"},
    {0x1344, "IOSwitches"},
};
#endif

//=============================================================================
// 1994 EJ22 (ROM ID: 7432A1) - Uses JECS2 addresses
//=============================================================================
#ifdef ECU_7432A1
#define ECU_ROM_ID "7432A1"
#define ECU_NAME "1994 EJ22"

const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346},  // Active
    {0x134B, 0x134A, 0x1349},  // Stored
    0x135E, 0x00               // Clear
};

const EcuParam ecuParams[] = {
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},
    {0x1329, "ThrottlePosition"},
    {0x1310, "O2Average"},
    {0x1323, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1328, "KnockCorrection"},
    {0x133E, "AFCorrection"},
    {0x130A, "FuelTableLookup"},
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1307, "AirflowSensor"},
    {0x1314, "ISUDutyValve"},
    {0x1340, "AtmosphericPressure"},
    {0x1343, "InputSwitches"},
    {0x1344, "IOSwitches"},
};
#endif

//=============================================================================
// 1993-1994 NA (ROM ID: 7332) - JECS3
// From b10scan.asm jecs3_params - no barometric pressure
//=============================================================================
#ifdef ECU_7332
#define ECU_ROM_ID "7332"
#define ECU_NAME "1993-1994 NA"

const TroubleCodeDef troubleCodes = {
    {0x1348, 0x1347, 0x1346},  // Active (same as jecs2)
    {0x134B, 0x134A, 0x1349},  // Stored
    0x135E, 0x00               // Clear (jecs2_clear)
};

const EcuParam ecuParams[] = {
    {0x1338, "EngineSpeed"},
    {0x1336, "VehicleSpeed"},
    {0x1329, "ThrottlePosition"},
    {0x1310, "O2Average"},
    {0x1323, "IgnitionAdvance"},
    {0x1305, "EngineLoad"},
    {0x1306, "InjectorPulseWidth"},
    {0x1328, "KnockCorrection"},
    {0x133E, "AFCorrection"},
    {0x1335, "BatteryVoltage"},
    {0x1337, "CoolantTemp"},
    {0x1307, "AirflowSensor"},
    {0x1314, "ISUDutyValve"},
    {0x1343, "InputSwitches"},
    {0x1344, "IOSwitches"},
};
#endif

//=============================================================================
// 1999 Forester EURO 2.0 (ROM ID: A30117)
// Note: Different address space, DTC addresses unknown
//=============================================================================
#ifdef ECU_A30117
#define ECU_ROM_ID "A30117"
#define ECU_NAME "1999 Forester EURO 2.0"

// DTC addresses for this ECU are unknown - placeholders
const TroubleCodeDef troubleCodes = {
    {0x0000, 0x0000, 0x0000},  // Unknown
    {0x0000, 0x0000, 0x0000},
    0x0000, 0x00
};

const EcuParam ecuParams[] = {
    {0x0009, "EngineSpeed"},
    {0x0008, "VehicleSpeed"},
    {0x000F, "ThrottlePosition"},
    {0x0012, "O2Average"},
    {0x000B, "IgnitionAdvance"},
    {0x000D, "EngineLoad"},
    {0x0010, "InjectorPulseWidth"},
    {0x0015, "KnockCorrection"},
    {0x001C, "AFCorrection"},
    {0x0020, "ManifoldPressure"},
    {0x0022, "BoostSolenoidDutyCycle"},
    {0x0007, "BatteryVoltage"},
    {0x000A, "CoolantTemp"},
    {0x000C, "AirflowSensor"},
    {0x0011, "ISUDutyValve"},
    {0x0013, "O2Minimum"},
    {0x0014, "O2Maximum"},
    {0x001F, "AtmosphericPressure"},
};
#endif

//=============================================================================
// 1992 SVX (ROM ID: 7225)
// Different address space (10xx/12xx). O2 params are the RIGHT sensor, and
// "O2Average" is the current reading, not a true average. InjectorPulseWidth /
// Atmospheric / Manifold / Boost are unavailable on this ECU (omitted).
// DTC addresses unknown - placeholders. (From SelectMonitor.ini, Phil Skuse.)
//=============================================================================
#ifdef ECU_7225
#define ECU_ROM_ID "7225"
#define ECU_NAME "1992 SVX"

const TroubleCodeDef troubleCodes = {
    {0x0000, 0x0000, 0x0000},  // Active (unknown)
    {0x0000, 0x0000, 0x0000},  // Stored (unknown)
    0x0000, 0x00               // Clear (unknown)
};

const EcuParam ecuParams[] = {
    {0x1290, "EngineSpeed"},
    {0x12A4, "VehicleSpeed"},
    {0x128C, "ThrottlePosition"},
    {0x1291, "O2Average"},
    {0x10A2, "IgnitionAdvance"},
    {0x1282, "EngineLoad"},
    {0x12A7, "KnockCorrection"},
    {0x12AD, "AFCorrection"},
    {0x12A2, "BatteryVoltage"},
    {0x1185, "CoolantTemp"},
    {0x1283, "AirflowSensor"},
    {0x129D, "ISUDutyValve"},
    {0x12B1, "O2Minimum"},
    {0x12B0, "O2Maximum"},
};
#endif

//=============================================================================
// 1994 SVX (ROM ID: 7425) - same address map as the 1992 SVX
//=============================================================================
#ifdef ECU_7425
#define ECU_ROM_ID "7425"
#define ECU_NAME "1994 SVX"

const TroubleCodeDef troubleCodes = {
    {0x0000, 0x0000, 0x0000},  // Active (unknown)
    {0x0000, 0x0000, 0x0000},  // Stored (unknown)
    0x0000, 0x00               // Clear (unknown)
};

const EcuParam ecuParams[] = {
    {0x1290, "EngineSpeed"},
    {0x12A4, "VehicleSpeed"},
    {0x128C, "ThrottlePosition"},
    {0x1291, "O2Average"},
    {0x10A2, "IgnitionAdvance"},
    {0x1282, "EngineLoad"},
    {0x12A7, "KnockCorrection"},
    {0x12AD, "AFCorrection"},
    {0x12A2, "BatteryVoltage"},
    {0x1185, "CoolantTemp"},
    {0x1283, "AirflowSensor"},
    {0x129D, "ISUDutyValve"},
    {0x12B1, "O2Minimum"},
    {0x12B0, "O2Maximum"},
};
#endif

//=============================================================================
// DTC Code Names (from b10scan.asm)
// Universal across all ECUs - maps bit positions to code names
//=============================================================================

static const char* dtcNames[] = {
  // Byte 1 (bits 0-6)
  "11-Crank", "12-StartSw", "13-Cam", "14-Inj1", "15-Inj2", "16-Inj3", "17-Inj4", nullptr,
  // Byte 2 (bits 0-7)  
  "21-Temp", "22-Knock", "23-MAF", "24-IAC", "31-TPS", "32-O2", "33-VSS", "35-Purge",
  // Byte 3 (bits 0-7)
  "41-FuelTrim", "42-IdleSw", nullptr, "44-WGC", "45-Baro", "49-WrongMAF", "51-NeutSw", "52-ParkSw"
};

//=============================================================================
// Fallback / Error Check
//=============================================================================
#ifndef ECU_ROM_ID
#error "No ECU defined! Uncomment one ECU_xxxx define at the top of ecu_defs.h"
#endif

#endif // ECU_DEFS_H
