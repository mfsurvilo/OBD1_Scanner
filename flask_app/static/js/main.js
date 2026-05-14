// Subaru SSM1 ECU Monitor - Frontend JavaScript

const socket = io({
    transports: ['websocket', 'polling']
});

// Debug: Log all socket events
socket.onAny((eventName, ...args) => {
    console.log(`[SOCKET EVENT] ${eventName}:`, args);
});

// Chart.js configuration
const chartConfig = {
    EngineSpeed: { label: 'RPM', color: '#00ff88', min: 0, max: 8000 },
    VehicleSpeed: { label: 'Speed (km/h)', color: '#00aaff', min: 0, max: 200 },
    CoolantTemp: { label: 'Coolant (°C)', color: '#ff6644', min: 0, max: 120 },
    ThrottlePosition: { label: 'Throttle (%)', color: '#ffaa00', min: 0, max: 100 },
    O2Average: { label: 'O2 (mV)', color: '#aa88ff', min: 0, max: 1000 },
    AFCorrection: { label: 'AF Correction', color: '#ff88aa', min: -50, max: 50 }
};

// Gauge configuration
const gaugeConfig = {
    EngineSpeed: { min: 0, max: 8000, warningThreshold: 6000, dangerThreshold: 7000 },
    VehicleSpeed: { min: 0, max: 200 },
    CoolantTemp: { min: -20, max: 120, warningThreshold: 95, dangerThreshold: 105 },
    ThrottlePosition: { min: 0, max: 100 },
    BatteryVoltage: { min: 10, max: 16, warningThreshold: 11, dangerThreshold: 10.5, invertWarning: true },
    O2Average: { min: 0, max: 1000 },
    IgnitionAdvance: { min: 0, max: 60 },
    AFCorrection: { min: -128, max: 127 }
};

// Data storage for charts
const chartData = {};

// Data storage for CSV export - stores all data with timestamps
const csvData = [];
let sampleCount = 0;
const charts = {};
const MAX_POINTS = 50;
let chartsInitialized = false;

// Initialize charts
function initCharts() {
    console.log('Initializing charts...');
    Object.keys(chartConfig).forEach(param => {
        const ctx = document.getElementById(`chart-${param}`);
        if (!ctx) {
            console.log(`Chart canvas not found for ${param}`);
            return;
        }

        chartData[param] = {
            labels: [],
            data: []
        };

        const config = chartConfig[param];
        charts[param] = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: config.label,
                    data: [],
                    borderColor: config.color,
                    backgroundColor: config.color + '33',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.3,
                    pointRadius: 0
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: {
                    duration: 0
                },
                scales: {
                    x: {
                        display: false
                    },
                    y: {
                        min: config.min,
                        max: config.max,
                        grid: {
                            color: '#333'
                        },
                        ticks: {
                            color: '#888'
                        }
                    }
                },
                plugins: {
                    legend: {
                        labels: {
                            color: '#eaeaea'
                        }
                    }
                }
            }
        });
    });
}

// Update gauge display
function updateGauge(param, value) {
    const gaugeEl = document.getElementById(`gauge-${param}`);
    const valueEl = document.getElementById(`value-${param}`);
    
    if (!gaugeEl) {
        // No gauge for this param (might be a secondary value)
        return;
    }
    
    if (!valueEl) {
        console.log(`Value element not found for ${param}`);
        return;
    }

    const config = gaugeConfig[param] || { min: 0, max: 100 };
    const percentage = Math.max(0, Math.min(1, (value - config.min) / (config.max - config.min)));
    
    // Arc length calculation (half circle = 251.2 units for our SVG)
    const arcLength = 251.2;
    const offset = arcLength * (1 - percentage);
    
    gaugeEl.style.strokeDashoffset = offset;
    
    // Apply warning/danger colors
    gaugeEl.classList.remove('warning', 'danger');
    if (config.invertWarning) {
        if (config.dangerThreshold && value <= config.dangerThreshold) {
            gaugeEl.classList.add('danger');
        } else if (config.warningThreshold && value <= config.warningThreshold) {
            gaugeEl.classList.add('warning');
        }
    } else {
        if (config.dangerThreshold && value >= config.dangerThreshold) {
            gaugeEl.classList.add('danger');
        } else if (config.warningThreshold && value >= config.warningThreshold) {
            gaugeEl.classList.add('warning');
        }
    }
    
    // Update value text
    valueEl.textContent = value.toFixed(value < 10 ? 1 : 0);
}

// Update chart with new data
function updateChart(param, value) {
    if (!charts[param]) {
        // Chart doesn't exist for this param (might not be one of the plotted ones)
        return;
    }

    const chart = charts[param];
    const now = new Date();
    const timeLabel = now.toLocaleTimeString();

    chart.data.labels.push(timeLabel);
    chart.data.datasets[0].data.push(value);

    // Keep only last MAX_POINTS
    if (chart.data.labels.length > MAX_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }

    chart.update('none');
    console.log(`Chart ${param} updated: ${chart.data.datasets[0].data.length} points`);
}

// Update secondary value display
function updateValue(param, value, unit) {
    const valueEl = document.getElementById(`value-${param}`);
    if (!valueEl) return;
    
    let displayValue = value.toFixed(value < 10 ? 2 : 1);
    if (unit) {
        displayValue += ` ${unit}`;
    }
    valueEl.textContent = displayValue;
}

// Socket event handlers
socket.on('connect', () => {
    console.log('WebSocket connected to server');
});

socket.on('disconnect', () => {
    console.log('WebSocket disconnected from server');
    document.getElementById('connection-status').className = 'status disconnected';
    document.getElementById('connection-status').textContent = 'Disconnected';
    document.getElementById('port-info').textContent = '';
});

socket.on('connect_error', (error) => {
    console.log('WebSocket connection error:', error);
});

socket.on('connection_status', (status) => {
    console.log('Connection status:', status);
    const statusEl = document.getElementById('connection-status');
    const portEl = document.getElementById('port-info');
    
    if (status.connected) {
        statusEl.className = 'status connected';
        statusEl.textContent = 'Connected';
        portEl.textContent = status.port || '';
    } else {
        statusEl.className = 'status disconnected';
        statusEl.textContent = 'Disconnected';
        portEl.textContent = status.error || '';
    }
});

socket.on('data_update', (data) => {
    const { param, value, unit, raw, time } = data;
    console.log(`Data: ${param} = ${value} ${unit}`);  // Debug log
    
    // Store for CSV export
    csvData.push({
        timestamp: new Date(time * 1000).toISOString(),
        param: param,
        value: value,
        raw: raw,
        unit: unit
    });
    sampleCount++;
    
    // Update sample count display
    const countEl = document.getElementById('data-count');
    if (countEl) {
        countEl.textContent = `${sampleCount} samples`;
    }
    
    // Update gauge if it exists
    updateGauge(param, value);
    
    // Update chart if it exists
    updateChart(param, value);
    
    // Update value display
    updateValue(param, value, unit);
});

socket.on('current_values', (values) => {
    Object.entries(values).forEach(([param, data]) => {
        if (data && data.value !== null) {
            updateGauge(param, data.value);
            updateValue(param, data.value, data.unit);
        }
    });
});

// View toggle
document.getElementById('toggle-view').addEventListener('click', function() {
    const gaugesView = document.getElementById('gauges-view');
    const graphsView = document.getElementById('graphs-view');
    
    if (gaugesView.classList.contains('active')) {
        gaugesView.classList.remove('active');
        graphsView.classList.add('active');
        this.textContent = 'Switch to Gauges';
    } else {
        graphsView.classList.remove('active');
        gaugesView.classList.add('active');
        this.textContent = 'Switch to Graphs';
    }
});

// CSV Download function
function downloadCSV() {
    if (csvData.length === 0) {
        alert('No data to download yet. Wait for data to come in.');
        return;
    }
    
    // Create CSV header
    const headers = ['Timestamp', 'Parameter', 'Value', 'Raw', 'Unit'];
    
    // Create CSV rows
    const rows = csvData.map(row => [
        row.timestamp,
        row.param,
        row.value,
        row.raw,
        row.unit
    ].join(','));
    
    // Combine header and rows
    const csvContent = [headers.join(','), ...rows].join('\n');
    
    // Create download
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.setAttribute('href', url);
    
    // Generate filename with timestamp
    const now = new Date();
    const filename = `ecu_data_${now.toISOString().replace(/[:.]/g, '-')}.csv`;
    link.setAttribute('download', filename);
    
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
    
    console.log(`Downloaded ${csvData.length} samples as ${filename}`);
}

// DTC (Trouble Code) Handling
let dtcData = {
    active: [],
    stored: [],
    lastRead: null
};

function toggleDtcPanel() {
    const panel = document.getElementById('dtc-panel');
    panel.classList.toggle('collapsed');
}

function updateDtcDisplay(data) {
    dtcData = data;
    
    const activeContainer = document.getElementById('dtc-active');
    const storedContainer = document.getElementById('dtc-stored');
    const indicator = document.getElementById('dtc-indicator');
    const lastReadEl = document.getElementById('dtc-last-read');
    
    // Update active codes display
    if (data.active && data.active.length > 0) {
        activeContainer.innerHTML = data.active.map(code => `
            <div class="dtc-code active">
                <span class="code-num">${code.code}</span>
                <span class="code-desc">${code.description || code.name}</span>
            </div>
        `).join('');
    } else {
        activeContainer.innerHTML = '<span class="dtc-empty">No active codes</span>';
    }
    
    // Update stored codes display
    if (data.stored && data.stored.length > 0) {
        storedContainer.innerHTML = data.stored.map(code => `
            <div class="dtc-code stored">
                <span class="code-num">${code.code}</span>
                <span class="code-desc">${code.description || code.name}</span>
            </div>
        `).join('');
    } else {
        storedContainer.innerHTML = '<span class="dtc-empty">No stored codes</span>';
    }
    
    // Update indicator
    const hasActive = data.active && data.active.length > 0;
    const hasStored = data.stored && data.stored.length > 0;
    
    if (hasActive) {
        indicator.className = 'dtc-indicator error';
        indicator.textContent = `${data.active.length} ACTIVE`;
    } else if (hasStored) {
        indicator.className = 'dtc-indicator warning';
        indicator.textContent = `${data.stored.length} STORED`;
    } else {
        indicator.className = 'dtc-indicator ok';
        indicator.textContent = 'OK';
    }
    
    // Update last read time
    if (data.lastRead) {
        const date = new Date(data.lastRead * 1000);
        lastReadEl.textContent = `Last read: ${date.toLocaleTimeString()}`;
    }
}

function requestDtcRead() {
    console.log('Requesting DTC read...');
    socket.emit('request_dtc_read');
}

function requestDtcClear() {
    if (confirm('Are you sure you want to clear all trouble codes?\n\nNote: Turn off ignition after clearing to finalize.')) {
        console.log('Requesting DTC clear...');
        socket.emit('request_dtc_clear');
    }
}

// DTC Socket events
socket.on('dtc_update', (data) => {
    console.log('DTC Update:', data);
    updateDtcDisplay(data);
});

socket.on('dtc_status', (status) => {
    console.log('DTC Status:', status);
    const lastReadEl = document.getElementById('dtc-last-read');
    if (status.status === 'reading') {
        lastReadEl.textContent = 'Reading codes...';
    } else if (status.status === 'clearing') {
        lastReadEl.textContent = 'Clearing codes...';
    } else if (status.status === 'error') {
        lastReadEl.textContent = `Error: ${status.message}`;
    }
});

// Make toggleDtcPanel available globally
window.toggleDtcPanel = toggleDtcPanel;

// Initialize on load
document.addEventListener('DOMContentLoaded', () => {
    console.log('DOM loaded, initializing...');
    initCharts();
    chartsInitialized = true;
    console.log('Charts initialized:', Object.keys(charts));
    
    // CSV download button handlers
    const downloadBtn = document.getElementById('download-csv');
    if (downloadBtn) {
        downloadBtn.addEventListener('click', downloadCSV);
    }
    
    const downloadBtnGraphs = document.getElementById('download-csv-graphs');
    if (downloadBtnGraphs) {
        downloadBtnGraphs.addEventListener('click', downloadCSV);
    }
    
    // DTC button handlers
    const dtcReadBtn = document.getElementById('dtc-read-btn');
    if (dtcReadBtn) {
        dtcReadBtn.addEventListener('click', requestDtcRead);
    }
    
    const dtcClearBtn = document.getElementById('dtc-clear-btn');
    if (dtcClearBtn) {
        dtcClearBtn.addEventListener('click', requestDtcClear);
    }
});
