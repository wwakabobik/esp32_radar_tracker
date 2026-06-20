function formatGestureSummary(data) {
  const on = (v) => (v ? 'ON' : 'off');
  const zone = `${data.gesture_zone_min_cm}–${data.gesture_zone_max_cm} cm`;
  const ml = [
    data.gesture_ml_next ? 'next' : null,
    data.gesture_ml_prev ? 'prev' : null,
    data.gesture_ml_vol ? 'vol' : null,
  ].filter(Boolean);
  const mlLine = ml.length ? ml.join(', ') : 'none (zone hold only)';
  return [
    `Near zone: ${zone}, hold ${data.gesture_hold_ms} ms`,
    `Zone hold → next: ${on(data.gesture_zone_hold)}`,
    `ML gestures: ${mlLine}`,
    `Debug MQTT: ${on(data.gesture_debug)}`,
  ].join(' · ');
}

async function loadGestureSummary(elementId) {
  const el = document.getElementById(elementId);
  if (!el) return;
  try {
    const data = await (await fetch('/api/gestures/settings')).json();
    el.textContent = formatGestureSummary(data);
  } catch (_) {
    el.textContent = 'Could not load — open Media gestures page';
  }
}
