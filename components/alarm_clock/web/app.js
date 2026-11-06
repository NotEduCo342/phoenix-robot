// PHOENIX SMART ROBOT REST Client & Controller
(function() {
  'use strict';

  var isSetupMode = true;
  var scanPollCount = 0;
  var statusPollTimer = null;
  var alarmPollTimer = null;

  // DOM Elements
  var viewSetup = document.getElementById('view-setup');
  var viewDashboard = document.getElementById('view-dashboard');
  var apListEl = document.getElementById('ap-list');
  var formWifi = document.getElementById('form-wifi');
  var inputSsid = document.getElementById('wifi-ssid');
  var inputPass = document.getElementById('wifi-pass');
  var btnRefresh = document.getElementById('btn-refresh-scan');
  var setupStatusBox = document.getElementById('setup-status-box');
  var setupSpinner = document.getElementById('setup-spinner');
  var setupTitle = document.getElementById('setup-status-title');
  var setupDesc = document.getElementById('setup-status-desc');
  var toastEl = document.getElementById('toast');

  // Dashboard elements
  var teleIp = document.getElementById('tele-ip');
  var teleRssi = document.getElementById('tele-rssi');
  var teleNtp = document.getElementById('tele-ntp');
  var teleSsid = document.getElementById('tele-ssid');
  var alarmBanner = document.getElementById('alarm-banner');
  var alarmBadge = document.getElementById('alarm-badge');
  var alarmCountdown = document.getElementById('alarm-countdown');
  var alarmSub = document.getElementById('alarm-sub');
  var btnSetAlarm = document.getElementById('btn-set-alarm');
  var btnCancelAlarm = document.getElementById('btn-cancel-alarm');
  var inputH = document.getElementById('alarm-h');
  var inputM = document.getElementById('alarm-m');

  function showToast(msg, durationMs) {
    if (!toastEl) return;
    toastEl.innerText = msg;
    toastEl.style.display = 'block';
    setTimeout(function() {
      toastEl.style.display = 'none';
    }, durationMs || 3000);
  }

  // ─── API Helpers ─────────────────────────────────────────────────────────────
  function apiGet(url, timeoutMs, onOk, onError) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.timeout = timeoutMs || 6000;
    xhr.onload = function() {
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          var data = JSON.parse(xhr.responseText);
          if (onOk) onOk(data);
        } catch(e) {
          if (onError) onError('Invalid JSON: ' + e);
        }
      } else {
        if (onError) onError('HTTP ' + xhr.status);
      }
    };
    xhr.ontimeout = function() { if (onError) onError('Timeout'); };
    xhr.onerror = function() { if (onError) onError('Network error'); };
    xhr.send();
  }

  function apiPost(url, payload, onOk, onError) {
    var xhr = new XMLHttpRequest();
    xhr.open('POST', url, true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.timeout = 8000;
    xhr.onload = function() {
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          var data = JSON.parse(xhr.responseText);
          if (onOk) onOk(data);
        } catch(e) {
          if (onOk) onOk({});
        }
      } else {
        if (onError) onError('HTTP ' + xhr.status);
      }
    };
    xhr.ontimeout = function() { if (onError) onError('Timeout'); };
    xhr.onerror = function() { if (onError) onError('Network error'); };
    xhr.send(JSON.stringify(payload));
  }

  // ─── Wi-Fi Scan & List Rendering ─────────────────────────────────────────────
  function fetchScan(isRefresh) {
    var url = '/api/scan' + (isRefresh ? '?refresh=1' : '');
    apListEl.innerHTML = '<div class="status-msg"><div class="mini-spinner"></div>Scanning 2.4GHz frequencies...</div>';

    apiGet(url, 6000, function(data) {
      var list = Array.isArray(data) ? data : (data.networks || []);
      var isScanning = data.scanning;

      if (isScanning && list.length === 0 && scanPollCount < 10) {
        scanPollCount++;
        setTimeout(function() { fetchScan(false); }, 1200);
        return;
      }
      scanPollCount = 0;

      if (!list || list.length === 0) {
        apListEl.innerHTML = '<div class="status-msg" style="color:#ff9900;">No networks found.<br><span style="font-size:10px;color:#8b949e;">You can type your SSID directly below.</span></div>';
        return;
      }

      apListEl.innerHTML = '';
      list.forEach(function(net) {
        var item = document.createElement('div');
        item.className = 'net-item';
        var icon = net.lock ? '🔒 ' : '📶 ';
        item.innerHTML = '<span class="net-name">' + icon + net.ssid + '</span><span class="net-pct">' + net.pct + '%</span>';
        item.onclick = function() {
          inputSsid.value = net.ssid;
          inputPass.focus();
        };
        apListEl.appendChild(item);
      });
    }, function(err) {
      console.warn('Scan error:', err);
      apListEl.innerHTML = '<div class="status-msg" style="color:#ff5100;">Scan unavailable.<br><span style="font-size:10px;color:#8b949e;">Type your Wi-Fi name directly in the box below.</span></div>';
    });
  }

  // ─── Wi-Fi Connect Form Submission ──────────────────────────────────────────
  formWifi.onsubmit = function(e) {
    e.preventDefault();
    var ssid = inputSsid.value.trim();
    var pass = inputPass.value;
    if (!ssid) return;

    setupStatusBox.style.display = 'block';
    formWifi.style.display = 'none';
    var nbox = document.querySelector('.network-box');
    if (nbox) nbox.style.display = 'none';

    setupSpinner.style.display = 'block';
    setupTitle.style.color = '#fff';
    setupTitle.innerText = 'Connecting robot to ' + ssid + '...';
    setupDesc.innerText = 'Verifying credentials and requesting IP address...';

    // Send connection command
    apiPost('/api/wifi', { ssid: ssid, password: pass }, function(resp) {
      console.log('Connect command sent:', resp);
    }, function(err) {
      console.warn('Error sending connect command:', err);
    });

    // Start polling status
    var pollCount = 0;
    var pollInterval = setInterval(function() {
      pollCount++;
      apiGet('/api/status', 3000, function(st) {
        if (st.status === 'connected') {
          clearInterval(pollInterval);
          setupSpinner.style.display = 'none';
          setupTitle.style.color = '#00ff88';
          setupTitle.innerText = '✓ Successfully Connected!';
          setupDesc.innerHTML = 'Assigned Router IP:<br><b style="color:#00f2fe;font-size:18px;display:inline-block;margin:8px 0;background:#141923;padding:6px 14px;border-radius:10px;">' + st.sta_ip + '</b><br><br>The robot setup AP has shut down.<br>Switch your device to your home Wi-Fi and open the IP above!';
        } else if (st.status === 'failed') {
          clearInterval(pollInterval);
          setupSpinner.style.display = 'none';
          setupTitle.style.color = '#ff4d4d';
          setupTitle.innerText = '✕ Connection Failed!';
          setupDesc.innerHTML = 'Incorrect password or router did not respond.<br><br><button type="button" class="btn-text" style="font-size:14px;color:#00f2fe;font-weight:bold;" onclick="location.reload()">Try Again</button>';
        }
      });
      if (pollCount > 40) {
        clearInterval(pollInterval);
        setupSpinner.style.display = 'none';
        setupTitle.style.color = '#ff9900';
        setupTitle.innerText = 'Connection Timeout';
        setupDesc.innerHTML = 'The robot is still attempting to connect.<br>Check your home router or <button type="button" class="btn-text" onclick="location.reload()">retry</button>.';
      }
    }, 1000);
  };

  btnRefresh.onclick = function() {
    fetchScan(true);
  };

  // ─── Dashboard & Alarm Controller ────────────────────────────────────────────
  function refreshStatus() {
    apiGet('/api/status', 4000, function(st) {
      if (st.setup_mode) {
        viewSetup.style.display = 'block';
        viewDashboard.style.display = 'none';
        isSetupMode = true;
      } else {
        viewSetup.style.display = 'none';
        viewDashboard.style.display = 'block';
        isSetupMode = false;

        teleIp.innerText = st.sta_ip || '---';
        teleRssi.innerText = (st.rssi_pct || 0) + '%';
        teleNtp.innerText = st.time_synced ? 'YES' : 'SYNCING';
        teleSsid.innerText = st.ssid || 'Home Router';

        refreshAlarm();
      }
    });
  }

  function refreshAlarm() {
    apiGet('/api/alarm', 3000, function(al) {
      if (al.is_active) {
        alarmBanner.className = 'alarm-banner banner-active';
        alarmBadge.className = 'banner-badge badge-on';
        alarmBadge.innerText = 'ALARM ARMED';
        alarmSub.innerText = 'Target: in ' + formatSeconds(al.remaining_sec);
        alarmCountdown.innerText = formatCountdown(al.remaining_sec);
        btnCancelAlarm.style.display = 'block';
      } else {
        alarmBanner.className = 'alarm-banner banner-idle';
        alarmBadge.className = 'banner-badge badge-off';
        alarmBadge.innerText = 'ALARM OFF';
        alarmSub.innerText = 'No wake-up timer active';
        alarmCountdown.innerText = '--:--:--';
        btnCancelAlarm.style.display = 'none';
      }
    });
  }

  function formatSeconds(totalSec) {
    if (!totalSec || totalSec <= 0) return '0s';
    var h = Math.floor(totalSec / 3600);
    var m = Math.floor((totalSec % 3600) / 60);
    var s = totalSec % 60;
    if (h > 0) return h + 'h ' + m + 'm';
    if (m > 0) return m + 'm ' + s + 's';
    return s + 's';
  }

  function formatCountdown(totalSec) {
    if (!totalSec || totalSec <= 0) return '00:00:00';
    var h = Math.floor(totalSec / 3600);
    var m = Math.floor((totalSec % 3600) / 60);
    var s = totalSec % 60;
    var pad = function(n) { return (n < 10 ? '0' : '') + n; };
    return pad(h) + ':' + pad(m) + ':' + pad(s);
  }

  window.setPreset = function(h, m) {
    inputH.value = h;
    inputM.value = m;
  };

  window.submitAlarm = function() {
    var h = parseInt(inputH.value, 10) || 0;
    var m = parseInt(inputM.value, 10) || 0;
    if (h === 0 && m === 0) {
      showToast('Please enter hours or minutes!');
      return;
    }
    apiPost('/api/alarm', { hours: h, minutes: m }, function(res) {
      showToast('Alarm set for ' + h + 'h ' + m + 'm!');
      refreshAlarm();
    }, function(err) {
      showToast('Failed to set alarm: ' + err);
    });
  };

  window.cancelAlarm = function() {
    apiPost('/api/alarm', { cancel: true }, function(res) {
      showToast('Alarm cancelled');
      refreshAlarm();
    }, function(err) {
      showToast('Failed to cancel alarm');
    });
  };

  // ─── Initialization ──────────────────────────────────────────────────────────
  apiGet('/api/status', 3000, function(st) {
    if (st.setup_mode) {
      viewSetup.style.display = 'block';
      viewDashboard.style.display = 'none';
      fetchScan(false);
    } else {
      viewSetup.style.display = 'none';
      viewDashboard.style.display = 'block';
      refreshStatus();
      alarmPollTimer = setInterval(refreshAlarm, 1000);
    }
  }, function(err) {
    // Default fallback to setup view
    viewSetup.style.display = 'block';
    fetchScan(false);
  });

})();

