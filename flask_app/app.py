#!/usr/bin/env python3
"""
Subaru SSM1 ECU Live Monitor - Flask Backend
Reads data from Teensy over serial and serves it via WebSocket
Now with Trouble Code (DTC) support
"""

import os
import re
import json
import threading
import time
from collections import deque
from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO
import serial
import serial.tools.list_ports

app = Flask(__name__)
app.config['SECRET_KEY'] = 'subaru_ssm1_secret'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# Teensy serial port - uses udev symlink
TEENSY_PORT = '/dev/teensy'

# Conversion formulas from b10scan.asm (jecs2 for 1992 NA)
CONVERSIONS = {
    'BatteryVoltage': {'formula': lambda v: v * 0.08, 'unit': 'V', 'min': 10, 'max': 16},
    'VehicleSpeed': {'formula': lambda v: v * 1.25, 'unit': 'mph', 'min': 0, 'max': 180},
    'EngineSpeed': {'formula': lambda v: v * 25, 'unit': 'RPM', 'min': 0, 'max': 8000},
    'CoolantTemp': {'formula': lambda v: v * 1.8 - 58, 'unit': '°F', 'min': -40, 'max': 280},
    'IgnitionAdvance': {'formula': lambda v: v, 'unit': '°BTDC', 'min': 0, 'max': 60},
    'AirflowSensor': {'formula': lambda v: v * 0.02, 'unit': 'V', 'min': 0, 'max': 5},
    'EngineLoad': {'formula': lambda v: v, 'unit': '', 'min': 0, 'max': 255},
    'ThrottlePosition': {'formula': lambda v: v * 0.02, 'unit': 'V', 'min': 0, 'max': 5},
    'InjectorPulseWidth': {'formula': lambda v: v * 0.256, 'unit': 'ms', 'min': 0, 'max': 65},
    'ISUDutyValve': {'formula': lambda v: (v * 100) / 255, 'unit': '%', 'min': 0, 'max': 100},
    'O2Average': {'formula': lambda v: v * 0.02, 'unit': 'V', 'min': 0, 'max': 5},
    'KnockCorrection': {'formula': lambda v: (v - 128) * 0.25, 'unit': '°', 'min': -32, 'max': 32},
    'AFCorrection': {'formula': lambda v: (v - 128) / 1.28, 'unit': '%', 'min': -100, 'max': 100},
    'AtmosphericPressure': {'formula': lambda v: v * 8, 'unit': 'torr', 'min': 500, 'max': 800},
    'InputSwitches': {'formula': lambda v: v, 'unit': '', 'min': 0, 'max': 255},
    'IOSwitches': {'formula': lambda v: v, 'unit': '', 'min': 0, 'max': 255},
}

# DTC code descriptions (from b10scan.asm)
DTC_DESCRIPTIONS = {
    '11': 'Crank Position Sensor',
    '12': 'Starter Switch',
    '13': 'Cam Position Sensor',
    '14': 'Injector #1',
    '15': 'Injector #2',
    '16': 'Injector #3',
    '17': 'Injector #4',
    '21': 'Coolant Temperature Sensor',
    '22': 'Knock Sensor',
    '23': 'MAF Sensor',
    '24': 'IAC Valve',
    '31': 'Throttle Position Sensor',
    '32': 'Oxygen Sensor',
    '33': 'Vehicle Speed Sensor',
    '35': 'Purge Solenoid',
    '41': 'Fuel Trim',
    '42': 'Idle Switch',
    '44': 'Wastegate Control (Turbo)',
    '45': 'Barometric Pressure',
    '49': 'Wrong MAF Installed',
    '51': 'Neutral Switch',
    '52': 'Park Switch',
}

# Data storage - keep last 100 samples for each parameter
MAX_HISTORY = 100
data_history = {param: deque(maxlen=MAX_HISTORY) for param in CONVERSIONS}
current_values = {param: None for param in CONVERSIONS}
connection_status = {'connected': False, 'port': None, 'error': None}

# DTC storage
dtc_data = {
    'active': [],
    'stored': [],
    'lastRead': None,
    'reading': False
}

# Serial connection
ser = None
serial_thread = None
running = False


def find_teensy_port():
    """Return the Teensy serial port path."""
    import os
    if os.path.exists(TEENSY_PORT):
        return TEENSY_PORT
    # Fallback to scanning ports
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'teensy' in port.description.lower() or 'usb serial' in port.description.lower():
            return port.device
        if 'ttyACM' in port.device or 'ttyUSB' in port.device:
            return port.device
    if ports:
        return ports[0].device
    return None


def parse_line(line):
    """Parse a line from the Teensy and extract parameter data."""
    # Pattern: [0] 0x1335 (BatteryVoltage): Raw=156 (0x9C)
    pattern = r'\[(\d+)\]\s+0x[0-9A-Fa-f]+\s+\((\w+)\):\s+Raw=(\d+)'
    match = re.search(pattern, line)
    if match:
        param_name = match.group(2)
        raw_value = int(match.group(3))
        return param_name, raw_value
    return None, None


def parse_dtc_line(line):
    """Parse a DTC line from the Teensy."""
    global dtc_data
    
    # Check for DTC reading start
    if '[DTC] Reading Trouble Codes' in line:
        dtc_data['reading'] = True
        dtc_data['active'] = []
        dtc_data['stored'] = []
        return True
    
    # Check for active code
    # Pattern: [DTC] ACTIVE: 11-Crank
    active_match = re.search(r'\[DTC\] ACTIVE: (\d+)-(\w+)', line)
    if active_match:
        code = active_match.group(1)
        name = active_match.group(2)
        desc = DTC_DESCRIPTIONS.get(code, name)
        dtc_data['active'].append({'code': code, 'name': name, 'description': desc})
        return True
    
    # Check for stored code
    stored_match = re.search(r'\[DTC\] STORED: (\d+)-(\w+)', line)
    if stored_match:
        code = stored_match.group(1)
        name = stored_match.group(2)
        desc = DTC_DESCRIPTIONS.get(code, name)
        dtc_data['stored'].append({'code': code, 'name': name, 'description': desc})
        return True
    
    # Check for "No codes" messages
    if '[DTC] No active codes' in line or '[DTC] No stored codes' in line:
        return True
    
    # Check for DTC reading complete (when we see the dashes again)
    if dtc_data['reading'] and '---' in line:
        dtc_data['reading'] = False
        dtc_data['lastRead'] = time.time()
        # Emit DTC update to all clients
        socketio.emit('dtc_update', dtc_data, namespace='/')
        print(f"[DTC] Active: {len(dtc_data['active'])}, Stored: {len(dtc_data['stored'])}")
        return True
    
    return False


def serial_reader():
    """Background thread that reads from serial port."""
    global ser, running, connection_status
    
    while running:
        if ser is None or not ser.is_open:
            # Try to connect
            port = find_teensy_port()
            if port:
                try:
                    ser = serial.Serial(port, 115200, timeout=1)
                    connection_status = {'connected': True, 'port': port, 'error': None}
                    print(f"Connected to {port}")
                    socketio.emit('connection_status', connection_status, namespace='/')
                except Exception as e:
                    connection_status = {'connected': False, 'port': None, 'error': str(e)}
                    socketio.emit('connection_status', connection_status, namespace='/')
                    time.sleep(2)
                    continue
            else:
                connection_status = {'connected': False, 'port': None, 'error': 'No serial port found'}
                socketio.emit('connection_status', connection_status, namespace='/')
                time.sleep(2)
                continue
        
        try:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                print(f"[SERIAL] {line}")  # Print raw data to terminal
                
                # Check if it's a DTC line first
                if '[DTC]' in line or (dtc_data['reading'] and '---' in line):
                    parse_dtc_line(line)
                    continue
                
                param_name, raw_value = parse_line(line)
                
                if param_name and param_name in CONVERSIONS:
                    conv = CONVERSIONS[param_name]
                    converted_value = conv['formula'](raw_value)
                    
                    # Store in history
                    timestamp = time.time()
                    data_history[param_name].append({
                        'time': timestamp,
                        'value': converted_value,
                        'raw': raw_value
                    })
                    current_values[param_name] = {
                        'value': converted_value,
                        'raw': raw_value,
                        'unit': conv['unit'],
                        'min': conv['min'],
                        'max': conv['max']
                    }
                    
                    # Emit to connected clients
                    emit_data = {
                        'param': param_name,
                        'value': converted_value,
                        'raw': raw_value,
                        'unit': conv['unit'],
                        'time': timestamp
                    }
                    print(f"[EMIT] {param_name}: {converted_value} {conv['unit']}")
                    socketio.emit('data_update', emit_data, namespace='/')
            else:
                time.sleep(0.01)  # Small delay when no data
        except serial.SerialException as e:
            print(f"Serial error: {e}")
            connection_status = {'connected': False, 'port': None, 'error': str(e)}
            socketio.emit('connection_status', connection_status, namespace='/')
            if ser:
                ser.close()
            ser = None
            time.sleep(2)
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(0.1)


@app.route('/')
def index():
    """Serve the main page."""
    return render_template('index.html', parameters=list(CONVERSIONS.keys()))


@app.route('/api/status')
def get_status():
    """Get current connection status."""
    return jsonify(connection_status)


@app.route('/api/current')
def get_current():
    """Get current values for all parameters."""
    return jsonify(current_values)


@app.route('/api/history/<param>')
def get_history(param):
    """Get history for a specific parameter."""
    if param in data_history:
        return jsonify(list(data_history[param]))
    return jsonify([])


@app.route('/api/download_csv')
def download_csv():
    """Download all historical data as CSV."""
    import io
    import csv
    from flask import Response
    from datetime import datetime
    
    output = io.StringIO()
    writer = csv.writer(output)
    
    # Write header
    writer.writerow(['Timestamp', 'Parameter', 'Value', 'Raw', 'Unit'])
    
    # Collect all data with timestamps
    all_data = []
    for param_name, history in data_history.items():
        conv = CONVERSIONS.get(param_name, {})
        unit = conv.get('unit', '')
        for entry in history:
            all_data.append({
                'time': entry['time'],
                'param': param_name,
                'value': entry['value'],
                'raw': entry['raw'],
                'unit': unit
            })
    
    # Sort by timestamp
    all_data.sort(key=lambda x: x['time'])
    
    # Write rows
    for row in all_data:
        timestamp = datetime.fromtimestamp(row['time']).isoformat()
        writer.writerow([timestamp, row['param'], row['value'], row['raw'], row['unit']])
    
    output.seek(0)
    
    filename = f"ecu_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    
    return Response(
        output.getvalue(),
        mimetype='text/csv',
        headers={'Content-Disposition': f'attachment; filename={filename}'}
    )


@app.route('/api/ports')
def get_ports():
    """List available serial ports."""
    ports = serial.tools.list_ports.comports()
    return jsonify([{'device': p.device, 'description': p.description} for p in ports])


@app.route('/api/dtc')
def get_dtc():
    """Get current DTC data."""
    return jsonify(dtc_data)


@app.route('/api/dtc/read', methods=['POST'])
def read_dtc():
    """Request DTC read from ECU."""
    global ser
    if ser and ser.is_open:
        try:
            ser.write(b'DTC\n')
            return jsonify({'status': 'ok', 'message': 'DTC read requested'})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)}), 500
    return jsonify({'status': 'error', 'message': 'Not connected'}), 503


@app.route('/api/dtc/clear', methods=['POST'])
def clear_dtc():
    """Request DTC clear from ECU."""
    global ser
    if ser and ser.is_open:
        try:
            ser.write(b'CLEAR\n')
            return jsonify({'status': 'ok', 'message': 'DTC clear requested'})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)}), 500
    return jsonify({'status': 'error', 'message': 'Not connected'}), 503


@socketio.on('connect')
def handle_connect():
    """Handle client connection."""
    print("Client connected")
    socketio.emit('connection_status', connection_status)
    socketio.emit('current_values', current_values)
    socketio.emit('dtc_update', dtc_data)


@socketio.on('disconnect')
def handle_disconnect():
    """Handle client disconnection."""
    print("Client disconnected")


@socketio.on('request_dtc_read')
def handle_dtc_read():
    """Handle DTC read request from websocket."""
    global ser
    if ser and ser.is_open:
        try:
            ser.write(b'DTC\n')
            socketio.emit('dtc_status', {'status': 'reading'})
        except Exception as e:
            socketio.emit('dtc_status', {'status': 'error', 'message': str(e)})
    else:
        socketio.emit('dtc_status', {'status': 'error', 'message': 'Not connected'})


@socketio.on('request_dtc_clear')
def handle_dtc_clear():
    """Handle DTC clear request from websocket."""
    global ser
    if ser and ser.is_open:
        try:
            ser.write(b'CLEAR\n')
            socketio.emit('dtc_status', {'status': 'clearing'})
        except Exception as e:
            socketio.emit('dtc_status', {'status': 'error', 'message': str(e)})
    else:
        socketio.emit('dtc_status', {'status': 'error', 'message': 'Not connected'})


def start_serial_thread():
    """Start the serial reader thread."""
    global serial_thread, running
    running = True
    serial_thread = threading.Thread(target=serial_reader, daemon=True)
    serial_thread.start()


def stop_serial_thread():
    """Stop the serial reader thread."""
    global running, ser
    running = False
    if ser and ser.is_open:
        ser.close()


if __name__ == '__main__':
    print("Starting Subaru SSM1 ECU Monitor...")
    print("Open http://localhost:5000 in your browser")
    start_serial_thread()
    try:
        socketio.run(app, host='0.0.0.0', port=5000, debug=False, allow_unsafe_werkzeug=True)
    finally:
        stop_serial_thread()
