const BAR_MAX_CM = 50;

let zoneMin = 12;
let zoneMax = 28;

function fmtTs(ts) {
  if (ts == null || ts === '') return '—';
  const d = new Date(Number(ts) * 1000);
  return d.toLocaleString('en-US', { hour12: false, fractionalSecondDigits: 3 });
}

function applySettings(data) {
  zoneMin = data.gesture_zone_min_cm;
  zoneMax = data.gesture_zone_max_cm;
  document.getElementById('zoneMin').value = zoneMin;
  document.getElementById('zoneMax').value = zoneMax;
  document.getElementById('holdMs').value = data.gesture_hold_ms;
  document.getElementById('debounceMs').value = data.gesture_debounce_ms;
  document.getElementById('gestureDebug').checked = data.gesture_debug;
  updateBarLabels();
  updateZoneOverlay();
}

function updateBarLabels() {
  document.getElementById('barZoneMin').textContent = zoneMin;
  document.getElementById('barZoneMax').textContent = zoneMax;
}

function pct(cm) {
  return Math.min(100, Math.max(0, (cm / BAR_MAX_CM) * 100));
}

function updateZoneOverlay() {
  const zone = document.getElementById('radarZone');
  zone.style.left = `${pct(zoneMin)}%`;
  zone.style.width = `${pct(zoneMax) - pct(zoneMin)}%`;
}

function updateLive(data) {
  document.getElementById('liveMode').textContent = data.mode || '—';
  document.getElementById('liveOnline').textContent = data.online ? 'yes' : 'no';
  document.getElementById('liveInZone').textContent =
    data.in_zone != null ? (data.in_zone ? 'yes' : 'no') : '—';
  document.getElementById('liveHold').textContent =
    data.hold_left_ms != null ? `${data.hold_left_ms} ms` : '—';
  document.getElementById('liveArmed').textContent =
    data.zone_armed != null ? (data.zone_armed ? 'yes (ready)' : 'no — remove hand') : '—';
  document.getElementById('liveDist').textContent =
    data.dist != null ? `${data.dist} cm` : '—';
  document.getElementById('liveTs').textContent = fmtTs(data.ts ?? data.radar_ts);
  document.getElementById('liveGesture').textContent = data.last_gesture || '—';
  document.getElementById('liveGestureTs').textContent = fmtTs(data.last_gesture_ts);

  const marker = document.getElementById('radarMarker');
  if (data.dist != null && data.dist > 0 && data.presence) {
    marker.hidden = false;
    marker.style.left = `${pct(data.dist)}%`;
    marker.classList.toggle('in-zone', data.in_zone);
  } else {
    marker.hidden = true;
  }
}

async function loadSettings() {
  const res = await fetch('/api/gestures/settings');
  applySettings(await res.json());
}

async function pollLive() {
  try {
    updateLive(await (await fetch('/api/gestures/live')).json());
  } catch (_) {
    /* ignore */
  }
}

document.getElementById('saveGestures').addEventListener('click', async () => {
  const body = {
    gesture_zone_min_cm: Number(document.getElementById('zoneMin').value),
    gesture_zone_max_cm: Number(document.getElementById('zoneMax').value),
    gesture_hold_ms: Number(document.getElementById('holdMs').value),
    gesture_debounce_ms: Number(document.getElementById('debounceMs').value),
    gesture_debug: document.getElementById('gestureDebug').checked,
  };
  const res = await fetch('/api/gestures/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const data = await res.json();
  const status = document.getElementById('gestureStatus');
  if (res.ok && data.ok) {
    status.textContent = 'Saved — config sent to device';
    zoneMin = body.gesture_zone_min_cm;
    zoneMax = body.gesture_zone_max_cm;
    updateBarLabels();
    updateZoneOverlay();
  } else {
    status.textContent = data.error || data.detail?.[0]?.msg || 'Failed';
  }
});

['zoneMin', 'zoneMax'].forEach((id) => {
  document.getElementById(id).addEventListener('change', () => {
    zoneMin = Number(document.getElementById('zoneMin').value);
    zoneMax = Number(document.getElementById('zoneMax').value);
    updateBarLabels();
    updateZoneOverlay();
  });
});

loadSettings();
setInterval(pollLive, 400);
pollLive();
