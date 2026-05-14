#!/usr/bin/env python3
"""
Subaru SSM1 ECU Data Parser and Plotter
Parses sample_data.txt and converts raw values using mapping formulas,
then plots the data using matplotlib.
"""

import re
import matplotlib.pyplot as plt
from collections import defaultdict

# Conversion formulas based on possible_mapping.txt
# Maps parameter name to (formula function, unit label)
CONVERSIONS = {
    'BatteryVoltage': (lambda v: v * 0.08, 'Volts'),
    'VehicleSpeed': (lambda v: v * 2, 'km/h'),
    'EngineSpeed': (lambda v: v * 25, 'RPM'),
    'CoolantTemp': (lambda v: v - 50, '°C'),
    'IgnitionAdvance': (lambda v: v, 'degrees'),
    'AirflowSensor': (lambda v: (v * 100) / 255, '%'),
    'EngineLoad': (lambda v: v, 'load'),
    'ThrottlePosition': (lambda v: (v * 100) / 255, '%'),
    'InjectorPulseWidth': (lambda v: v * 256 / 1000, 'ms'),
    'ISUDutyValve': (lambda v: (v * 100) / 255, '%'),
    'O2Average': (lambda v: v * 5000 / 512, 'mV'),
    'KnockCorrection': (lambda v: v, 'correction'),
    'AFCorrection': (lambda v: v - 128, 'correction'),
    'AtmosphericPressure': (lambda v: v, 'raw'),
    'InputSwitches': (lambda v: v, 'raw'),
    'IOSwitches': (lambda v: v, 'raw'),
}

def parse_sample_data(filename):
    """Parse the sample_data.txt file and extract all parameter readings."""
    data = defaultdict(list)
    
    # Pattern to match lines like: [0] 0x1335 (BatteryVoltage): Raw=156 (0x9C)
    pattern = r'\[(\d+)\]\s+0x[0-9A-Fa-f]+\s+\((\w+)\):\s+Raw=(\d+)'
    
    with open(filename, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                index = int(match.group(1))
                param_name = match.group(2)
                raw_value = int(match.group(3))
                data[param_name].append(raw_value)
    
    return data

def convert_data(raw_data):
    """Convert raw values to real values using the conversion formulas."""
    converted = {}
    
    for param_name, raw_values in raw_data.items():
        if param_name in CONVERSIONS:
            formula, unit = CONVERSIONS[param_name]
            converted[param_name] = {
                'values': [formula(v) for v in raw_values],
                'unit': unit,
                'raw': raw_values
            }
        else:
            # No conversion available, use raw values
            converted[param_name] = {
                'values': raw_values,
                'unit': 'raw',
                'raw': raw_values
            }
    
    return converted

def plot_data(converted_data):
    """Plot all parameters using matplotlib."""
    # Filter out switch data (not useful for plotting as line graphs)
    plot_params = {k: v for k, v in converted_data.items() 
                   if k not in ['InputSwitches', 'IOSwitches']}
    
    num_params = len(plot_params)
    
    # Create subplots - 2 columns
    cols = 2
    rows = (num_params + 1) // cols
    
    fig, axes = plt.subplots(rows, cols, figsize=(14, 3 * rows))
    fig.suptitle('Subaru SSM1 ECU Data', fontsize=14, fontweight='bold')
    
    axes = axes.flatten() if num_params > 2 else [axes] if num_params == 1 else axes
    
    for idx, (param_name, data) in enumerate(plot_params.items()):
        ax = axes[idx]
        values = data['values']
        unit = data['unit']
        
        # Create sample numbers for x-axis
        samples = list(range(len(values)))
        
        ax.plot(samples, values, 'b-', linewidth=1.5, marker='o', markersize=3)
        ax.set_title(param_name, fontweight='bold')
        ax.set_xlabel('Sample #')
        ax.set_ylabel(unit)
        ax.grid(True, alpha=0.3)
        
        # Add min/max/avg annotations
        if values:
            min_val = min(values)
            max_val = max(values)
            avg_val = sum(values) / len(values)
            ax.axhline(y=avg_val, color='r', linestyle='--', alpha=0.5, label=f'Avg: {avg_val:.1f}')
            ax.legend(loc='upper right', fontsize=8)
    
    # Hide empty subplots
    for idx in range(len(plot_params), len(axes)):
        axes[idx].set_visible(False)
    
    plt.tight_layout()
    plt.savefig('ecu_data_plot.png', dpi=150, bbox_inches='tight')
    print("Saved plot to ecu_data_plot.png")
    plt.show()

def print_summary(converted_data):
    """Print a summary of all converted values."""
    print("\n" + "=" * 60)
    print("ECU DATA SUMMARY (Converted Values)")
    print("=" * 60)
    
    for param_name, data in converted_data.items():
        values = data['values']
        unit = data['unit']
        
        if values:
            min_val = min(values)
            max_val = max(values)
            avg_val = sum(values) / len(values)
            print(f"\n{param_name}:")
            print(f"  Samples: {len(values)}")
            print(f"  Min: {min_val:.2f} {unit}")
            print(f"  Max: {max_val:.2f} {unit}")
            print(f"  Avg: {avg_val:.2f} {unit}")

def main():
    import os
    
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    sample_file = os.path.join(script_dir, 'sample_data.txt')
    
    print("Parsing sample_data.txt...")
    raw_data = parse_sample_data(sample_file)
    
    if not raw_data:
        print("ERROR: No data found in sample_data.txt")
        return
    
    print(f"Found {len(raw_data)} parameters with data")
    
    print("Converting raw values...")
    converted_data = convert_data(raw_data)
    
    # Print summary
    print_summary(converted_data)
    
    # Plot the data
    print("\nGenerating plots...")
    plot_data(converted_data)

if __name__ == '__main__':
    main()
