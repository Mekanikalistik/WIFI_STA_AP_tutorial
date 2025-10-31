// ESP32 WiFi Manager JavaScript

document.addEventListener('DOMContentLoaded', function () {
    const scanBtn = document.getElementById('scanBtn');
    const networksDiv = document.getElementById('networks');
    const ledOnBtn = document.getElementById('ledOnBtn');
    const ledOffBtn = document.getElementById('ledOffBtn');
    const ledStatus = document.getElementById('ledStatus');
    const statusDiv = document.getElementById('status');

    // Initialize status
    updateStatus('Device ready. Click "Scan Networks" to find available WiFi networks.');

    // Scan networks button
    scanBtn.addEventListener('click', function () {
        scanBtn.disabled = true;
        scanBtn.innerHTML = '<span class="loading"></span>Scanning...';
        updateStatus('Scanning for WiFi networks...');

        fetch('/api/wifi/scan')
            .then(response => response.json())
            .then(data => {
                displayNetworks(data.networks);
                updateStatus(`Found ${data.networks.length} WiFi networks.`);
            })
            .catch(error => {
                console.error('Scan error:', error);
                updateStatus('Error scanning networks. Please try again.');
            })
            .finally(() => {
                scanBtn.disabled = false;
                scanBtn.innerHTML = 'Scan Networks';
            });
    });

    // LED control buttons
    ledOnBtn.addEventListener('click', function () {
        controlLED(true);
    });

    ledOffBtn.addEventListener('click', function () {
        controlLED(false);
    });

    function controlLED(state) {
        ledOnBtn.disabled = true;
        ledOffBtn.disabled = true;

        fetch('/api/led/control', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ state: state ? "on" : "off" })
        })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    ledStatus.textContent = state ? 'ON' : 'OFF';
                    ledStatus.style.color = state ? '#28a745' : '#dc3545';
                    updateStatus(`LED turned ${state ? 'ON' : 'OFF'}`);
                } else {
                    updateStatus('Error controlling LED');
                }
            })
            .catch(error => {
                console.error('LED control error:', error);
                updateStatus('Error controlling LED. Please try again.');
            })
            .finally(() => {
                ledOnBtn.disabled = false;
                ledOffBtn.disabled = false;
            });
    }

    function displayNetworks(networks) {
        networksDiv.innerHTML = '';

        if (networks.length === 0) {
            networksDiv.innerHTML = '<p>No networks found.</p>';
            return;
        }

        networks.forEach(network => {
            const networkDiv = document.createElement('div');
            networkDiv.className = 'network-item';

            const signalStrength = getSignalStrength(network.rssi);
            const authIcon = getAuthIcon(network.authmode);

            networkDiv.innerHTML = `
                <div class="network-info">
                    <div class="network-ssid">${network.ssid}</div>
                    <div class="network-details">
                        Signal: ${signalStrength} | Channel: ${network.channel} | Security: ${network.authmode.toUpperCase()} ${authIcon}
                    </div>
                </div>
                <div class="network-connect">
                    <div class="connect-form">
                        <input type="password" class="connect-input" placeholder="Password" id="pwd-${network.ssid.replace(/[^a-zA-Z0-9]/g, '_')}">
                        <button class="connect-btn" onclick="connectToNetwork('${network.ssid}', this)">Connect</button>
                    </div>
                </div>
            `;

            networksDiv.appendChild(networkDiv);
        });
    }

    // Global function for connect button
    window.connectToNetwork = function (ssid, button) {
        const passwordInput = document.getElementById(`pwd-${ssid.replace(/[^a-zA-Z0-9]/g, '_')}`);
        const password = passwordInput.value;

        if (!password && !['open', 'none'].includes(passwordInput.placeholder.toLowerCase())) {
            updateStatus('Password is required for this network');
            return;
        }

        button.disabled = true;
        button.textContent = 'Connecting...';
        updateStatus(`Connecting to ${ssid}...`);

        fetch('/api/wifi/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                ssid: ssid,
                password: password
            })
        })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    updateStatus(`Attempting to connect to ${ssid}. Please wait...`);
                } else {
                    updateStatus('Connection failed. Please check credentials and try again.');
                }
            })
            .catch(error => {
                console.error('Connection error:', error);
                updateStatus('Error connecting to network. Please try again.');
            })
            .finally(() => {
                button.disabled = false;
                button.textContent = 'Connect';
            });
    };

    function getSignalStrength(rssi) {
        if (rssi >= -50) return 'Excellent';
        if (rssi >= -60) return 'Good';
        if (rssi >= -70) return 'Fair';
        return 'Weak';
    }

    function getAuthIcon(authmode) {
        switch (authmode) {
            case 'open': return '🔓';
            case 'wpa': return '🔒';
            case 'wpa2': return '🔐';
            case 'wpa3': return '🛡️';
            default: return '❓';
        }
    }

    function updateStatus(message) {
        statusDiv.textContent = message;
        console.log('Status:', message);
    }

    // Auto-refresh LED status and WiFi status every 5 seconds
    setInterval(() => {
        fetch('/api/led/status')
            .then(response => response.json())
            .then(data => {
                const isOn = data.state === true || data.state === "true";
                ledStatus.textContent = isOn ? 'ON' : 'OFF';
                ledStatus.style.color = isOn ? '#28a745' : '#dc3545';
            })
            .catch(error => {
                console.error('LED status error:', error);
            });

        fetch('/api/wifi/status')
            .then(response => response.json())
            .then(data => {
                if (data.connected) {
                    updateStatus(`Connected to ${data.ssid} (RSSI: ${data.rssi}dBm, Channel: ${data.channel})`);
                } else {
                    updateStatus('Not connected to any WiFi network');
                }
            })
            .catch(error => {
                console.error('WiFi status error:', error);
            });
    }, 5000);
});