// Enhanced ESP32 WiFi Manager JavaScript with Shadcn Components

document.addEventListener('DOMContentLoaded', function () {
    const scanBtn = document.getElementById('scanBtn');
    const networksDiv = document.getElementById('networks');
    const ledOnBtn = document.getElementById('ledOnBtn');
    const ledOffBtn = document.getElementById('ledOffBtn');
    const ledStatus = document.getElementById('ledStatus');
    const statusDiv = document.getElementById('status');
    const retryBtn = document.getElementById('retryBtn');

    // Enhanced WiFi status elements
    const connectionState = document.getElementById('connectionState');
    const retryCount = document.getElementById('retryCount');
    const apStatus = document.getElementById('apStatus');

    // Initialize with welcome message
    updateStatus('Device ready. Click "Scan Networks" to find available WiFi networks.');

    // Enhanced scan networks button with loading state
    scanBtn.addEventListener('click', function () {
        scanBtn.disabled = true;
        scanBtn.innerHTML = `
            <div class="loading"></div>
            Scanning...
        `;
        updateStatus('Scanning for WiFi networks...');

        fetch('/api/wifi/scan')
            .then(response => response.json())
            .then(data => {
                displayNetworks(data.networks);
                updateStatus(`Found ${data.networks.length} WiFi networks.`);
                animateElement(networksDiv, 'fadeIn');
            })
            .catch(error => {
                console.error('Scan error:', error);
                updateStatus('Error scanning networks. Please try again.');
                showToast('Network scan failed', 'error');
            })
            .finally(() => {
                scanBtn.disabled = false;
                scanBtn.innerHTML = `
                    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" style="width: 1rem; height: 1rem;">
                        <path stroke-linecap="round" stroke-linejoin="round" d="m21 21-5.197-5.197m0 0A7.5 7.5 0 1 0 5.196 5.196a7.5 7.5 0 0 0 10.607 10.607Z" />
                    </svg>
                    Scan Networks
                `;
            });
    });

    // Enhanced LED control buttons
    ledOnBtn.addEventListener('click', function () {
        controlLED(true);
    });

    ledOffBtn.addEventListener('click', function () {
        controlLED(false);
    });

    // Enhanced WiFi retry button
    retryBtn.addEventListener('click', function () {
        retrySTAConnection();
    });

    function controlLED(state) {
        const button = state ? ledOnBtn : ledOffBtn;
        const otherButton = state ? ledOffBtn : ledOnBtn;

        // Disable both buttons temporarily
        button.disabled = true;
        otherButton.disabled = true;

        const originalHtml = button.innerHTML;
        button.innerHTML = `
            <div class="loading"></div>
            ${state ? 'Turning ON...' : 'Turning OFF...'}
        `;

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
                    ledStatus.className = state ? 'font-medium' : 'font-medium text-muted';
                    ledStatus.style.color = state ? 'hsl(142.1 76.2% 36.3%)' : 'hsl(var(--muted-foreground))';
                    updateStatus(`LED turned ${state ? 'ON' : 'OFF'}`);
                    showToast(`LED ${state ? 'activated' : 'deactivated'} successfully`, 'success');
                } else {
                    updateStatus('Error controlling LED');
                    showToast('LED control failed', 'error');
                }
            })
            .catch(error => {
                console.error('LED control error:', error);
                updateStatus('Error controlling LED. Please try again.');
                showToast('Connection error', 'error');
            })
            .finally(() => {
                button.innerHTML = originalHtml;
                button.disabled = false;
                otherButton.disabled = false;
            });
    }

    function displayNetworks(networks) {
        networksDiv.innerHTML = '';

        if (networks.length === 0) {
            networksDiv.innerHTML = `
                <div class="text-center text-muted" style="padding: 2rem;">
                    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" style="width: 3rem; height: 3rem; margin: 0 auto 1rem; opacity: 0.5;">
                        <path stroke-linecap="round" stroke-linejoin="round" d="m9 13.5 3 3m0 0 3-3m-3 3V8.25m3-9.375 9-9m0 0 9 9m0 0 3-3m-3 3V8.25" />
                    </svg>
                    <div class="font-medium">No networks found</div>
                    <div class="text-sm">Try scanning again</div>
                </div>
            `;
            return;
        }

        networks.forEach((network, index) => {
            const networkDiv = document.createElement('div');
            networkDiv.className = 'network-item fade-in';
            networkDiv.style.animationDelay = `${index * 0.1}s`;

            const signalStrength = getSignalStrength(network.rssi);
            const authIcon = getAuthIcon(network.authmode);
            const signalIcon = getSignalIcon(network.rssi);

            networkDiv.innerHTML = `
                <div class="network-info">
                    <div class="network-ssid">
                        ${network.ssid}
                        ${signalIcon}
                    </div>
                    <div class="network-details">
                        ${signalStrength} | Channel: ${network.channel} | Security: ${network.authmode.toUpperCase()} ${authIcon}
                    </div>
                </div>
                <div class="network-connect">
                    <input 
                        type="password" 
                        class="connect-input" 
                        placeholder="Password" 
                        id="pwd-${network.ssid.replace(/[^a-zA-Z0-9]/g, '_')}"
                        autocomplete="off"
                    >
                    <button class="btn btn-sm btn-default" onclick="connectToNetwork('${network.ssid}', this)">
                        Connect
                    </button>
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
            showToast('Password required', 'warning');
            passwordInput.focus();
            return;
        }

        button.disabled = true;
        const originalText = button.textContent;
        button.innerHTML = '<div class="loading"></div> Connecting...';
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
                    showToast('Connection initiated', 'success');
                } else {
                    updateStatus('Connection failed. Please check credentials and try again.');
                    showToast('Connection failed', 'error');
                }
            })
            .catch(error => {
                console.error('Connection error:', error);
                updateStatus('Error connecting to network. Please try again.');
                showToast('Connection error', 'error');
            })
            .finally(() => {
                button.disabled = false;
                button.textContent = originalText;
            });
    };

    function getSignalStrength(rssi) {
        if (rssi >= -50) return 'Excellent';
        if (rssi >= -60) return 'Good';
        if (rssi >= -70) return 'Fair';
        return 'Weak';
    }

    function getSignalIcon(rssi) {
        if (rssi >= -50) return '📶';
        if (rssi >= -60) return '📱';
        if (rssi >= -70) return '📴';
        return '📵';
    }

    function getAuthIcon(authmode) {
        switch (authmode.toLowerCase()) {
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

    function retrySTAConnection() {
        retryBtn.disabled = true;
        retryBtn.innerHTML = `
            <div class="loading"></div>
            Retrying...
        `;
        updateStatus('Manual STA retry initiated...');
        showToast('Retrying connection...', 'info');

        fetch('/api/wifi/retry', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ action: 'retry' })
        })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    updateStatus('Starting STA retry attempt...');
                } else {
                    updateStatus(data.message || 'Retry failed');
                    showToast('Retry failed', 'error');
                }
            })
            .catch(error => {
                console.error('Retry error:', error);
                updateStatus('Error retrying connection. Please try again.');
                showToast('Retry error', 'error');
            })
            .finally(() => {
                setTimeout(() => {
                    retryBtn.disabled = false;
                    retryBtn.innerHTML = `
                        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" style="width: 1rem; height: 1rem;">
                            <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0 3.181 3.183a8.25 8.25 0 0 0 13.803-3.7M4.031 9.865a8.25 8.25 0 0 1 13.803-3.7l3.181 3.182m0-4.991v4.99" />
                        </svg>
                        Retry STA Connection
                    `;
                }, 3000);
            });
    }

    // Enhanced WiFi status display
    window.updateWiFiStatusDisplay = function (data) {
        // Update connection state with enhanced styling
        let stateText = '';
        let stateClass = '';
        let badgeClass = '';
        let badgeText = '';
        let iconHtml = '';

        switch (data.state) {
            case 'connecting':
                stateText = 'Connecting...';
                stateClass = 'connecting';
                badgeClass = 'badge badge-warning';
                badgeText = 'Connecting';
                iconHtml = '<div class="loading"></div>';
                break;
            case 'connected':
                stateText = 'Connected';
                stateClass = 'connected';
                badgeClass = 'badge badge-success';
                badgeText = 'Online';
                iconHtml = '✅';
                break;
            case 'failed_ap_active':
                stateText = 'Failed - AP Active';
                stateClass = 'failed';
                badgeClass = 'badge badge-destructive';
                badgeText = 'AP Mode';
                iconHtml = '🔄';
                break;
            case 'ap_active':
                stateText = 'AP Mode';
                stateClass = 'failed';
                badgeClass = 'badge badge-destructive';
                badgeText = 'AP Mode';
                iconHtml = '📡';
                break;
            default:
                stateText = data.state || 'Unknown';
                stateClass = '';
                badgeClass = 'badge';
                badgeText = 'Unknown';
                iconHtml = '❓';
        }

        connectionState.textContent = stateText;
        connectionState.className = 'font-semibold ' + stateClass;

        const statusBadge = document.getElementById('statusBadge');
        statusBadge.className = badgeClass;
        statusBadge.innerHTML = iconHtml + ' ' + badgeText;

        // Update retry count with enhanced display
        retryCount.textContent = `${data.retry_count || 0}/3`;
        retryCount.className = `font-medium ${data.retry_count >= 2 ? 'text-destructive' : ''}`;

        // Update AP status
        if (data.ap_enabled) {
            apStatus.textContent = 'Enabled';
            apStatus.className = 'font-medium';
            apStatus.style.color = 'hsl(142.1 76.2% 36.3%)';
        } else {
            apStatus.textContent = 'Disabled';
            apStatus.className = 'font-medium text-muted';
        }

        // Show/hide retry button with animation
        if (data.state === 'failed_ap_active' || data.state === 'ap_active') {
            if (retryBtn.classList.contains('hidden')) {
                retryBtn.classList.remove('hidden');
                animateElement(retryBtn, 'fadeIn');
            }
        } else {
            if (!retryBtn.classList.contains('hidden')) {
                retryBtn.classList.add('hidden');
            }
        }
    };

    // Enhanced auto-refresh with improved error handling
    setInterval(() => {
        // Fetch LED status
        fetch('/api/led/status')
            .then(response => response.json())
            .then(data => {
                updateLEDStatusDisplay(data);
            })
            .catch(error => {
                console.error('LED status error:', error);
            });

        // Fetch WiFi status
        fetch('/api/wifi/status')
            .then(response => response.json())
            .then(data => {
                // Update WiFi status display
                window.updateWiFiStatusDisplay(data);

                // Enhanced status message
                if (data.connected) {
                    updateStatus(`Connected to ${data.ssid} (RSSI: ${data.rssi}dBm, Channel: ${data.channel})`);
                } else {
                    if (data.state === 'connecting') {
                        updateStatus('Attempting to connect to WiFi...');
                    } else if (data.state === 'failed_ap_active') {
                        updateStatus(`All STA attempts failed (${data.retry_count}/3). AP is now active for configuration.`);
                    } else {
                        updateStatus('Not connected to any WiFi network');
                    }
                }
            })
            .catch(error => {
                console.error('WiFi status error:', error);
                updateStatus('Unable to fetch device status');
            });
    }, 5000);

    // Utility Functions
    function updateLEDStatusDisplay(data) {
        const ledStatus = document.getElementById('ledStatus');
        const isOn = data.state === true || data.state === "true";
        ledStatus.textContent = isOn ? 'ON' : 'OFF';
        ledStatus.className = isOn ? 'font-medium' : 'font-medium text-muted';
        ledStatus.style.color = isOn ? 'hsl(142.1 76.2% 36.3%)' : 'hsl(var(--muted-foreground))';
    }

    function animateElement(element, animation) {
        element.classList.add(animation);
        setTimeout(() => {
            element.classList.remove(animation);
        }, 300);
    }

    // Enhanced toast notification system
    function showToast(message, type = 'info') {
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        toast.innerHTML = `
            <div class="toast-content">
                <span class="toast-message">${message}</span>
                <button class="toast-close" onclick="this.parentElement.parentElement.remove()">×</button>
            </div>
        `;

        // Add toast styles if not already present
        if (!document.querySelector('#toast-styles')) {
            const style = document.createElement('style');
            style.id = 'toast-styles';
            style.textContent = `
                .toast {
                    position: fixed;
                    top: 80px;
                    right: 20px;
                    z-index: 1000;
                    padding: 1rem;
                    border-radius: calc(var(--radius) - 0.125rem);
                    box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.1);
                    animation: slideIn 0.3s ease-out;
                    max-width: 300px;
                }
                .toast-success {
                    background-color: hsl(142.1 76.2% 36.3% / 0.1);
                    color: hsl(142.1 76.2% 36.3%);
                    border: 1px solid hsl(142.1 76.2% 36.3% / 0.2);
                }
                .toast-error {
                    background-color: hsl(var(--destructive) / 0.1);
                    color: hsl(var(--destructive));
                    border: 1px solid hsl(var(--destructive) / 0.2);
                }
                .toast-warning {
                    background-color: hsl(38 92% 50% / 0.1);
                    color: hsl(38 92% 50%);
                    border: 1px solid hsl(38 92% 50% / 0.2);
                }
                .toast-info {
                    background-color: hsl(var(--primary) / 0.1);
                    color: hsl(var(--primary));
                    border: 1px solid hsl(var(--primary) / 0.2);
                }
                .toast-content {
                    display: flex;
                    align-items: center;
                    justify-content: space-between;
                    gap: 1rem;
                }
                .toast-close {
                    background: none;
                    border: none;
                    font-size: 1.25rem;
                    cursor: pointer;
                    opacity: 0.7;
                    padding: 0;
                    width: 1.5rem;
                    height: 1.5rem;
                    display: flex;
                    align-items: center;
                    justify-content: center;
                }
                .toast-close:hover {
                    opacity: 1;
                }
                @keyframes slideIn {
                    from {
                        transform: translateX(100%);
                        opacity: 0;
                    }
                    to {
                        transform: translateX(0);
                        opacity: 1;
                    }
                }
            `;
            document.head.appendChild(style);
        }

        document.body.appendChild(toast);

        // Auto remove after 5 seconds
        setTimeout(() => {
            if (toast.parentNode) {
                toast.remove();
            }
        }, 5000);
    }

    // Add initial card animations
    const cards = document.querySelectorAll('.card');
    cards.forEach((card, index) => {
        card.style.animationDelay = `${index * 0.1}s`;
    });

    // Keyboard shortcuts
    document.addEventListener('keydown', function (e) {
        // Ctrl/Cmd + R to retry
        if ((e.ctrlKey || e.metaKey) && e.key === 'r') {
            e.preventDefault();
            if (!retryBtn.classList.contains('hidden')) {
                retrySTAConnection();
            }
        }

        // Ctrl/Cmd + S to scan
        if ((e.ctrlKey || e.metaKey) && e.key === 's') {
            e.preventDefault();
            if (!scanBtn.disabled) {
                scanBtn.click();
            }
        }
    });

    // Add visual feedback for loading states
    function addLoadingState(element, text = 'Loading...') {
        const originalContent = element.innerHTML;
        element.innerHTML = `<div class="loading"></div> ${text}`;
        element.disabled = true;
        return function () {
            element.innerHTML = originalContent;
            element.disabled = false;
        };
    }

    // Add connection attempt animation
    function showConnectionAttempt(networkName) {
        updateStatus(`Attempting to connect to ${networkName}...`);
        const statusElement = document.getElementById('status');
        statusElement.style.border = '2px solid hsl(var(--primary))';
        statusElement.style.borderRadius = 'calc(var(--radius) - 0.125rem)';

        setTimeout(() => {
            statusElement.style.border = '';
        }, 2000);
    }

    console.log('ESP32 WiFi Manager with Shadcn UI initialized successfully');
});