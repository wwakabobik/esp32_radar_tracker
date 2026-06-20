async function loadSettings() {
  const res = await fetch('/api/settings/');
  const data = await res.json();
  document.getElementById('telegramStatus').textContent = data.telegram_configured
    ? 'configured'
    : 'not configured (edit .env)';
  document.getElementById('standupEnabled').checked = data.standup_enabled;
  document.getElementById('standupMin').value = data.standup_min;
  document.getElementById('standupMessage').value = data.telegram_standup_message || '';
  document.getElementById('morningEnabled').checked = data.morning_summary_enabled;
  document.getElementById('morningHour').value = data.morning_summary_hour;
  document.getElementById('workMin').value = data.work_presence_min_cm;
  document.getElementById('workMax').value = data.work_presence_max_cm;
  document.getElementById('workType').value = data.work_presence_type;
  document.getElementById('gateWork').value = data.radar_gate_work;
  document.getElementById('sleepMax').value = data.sleep_breath_max_cm;
  document.getElementById('sleepEnergy').value = data.sleep_energy_min;
  document.getElementById('gateSleep').value = data.radar_gate_sleep;
  document.getElementById('mediaBackend').value = data.media_backend;
  document.getElementById('aiEnabled').checked = data.ai_enabled;
  document.getElementById('aiRecordMode').checked = data.ai_record_mode;
  document.getElementById('aiConfidenceMin').value = data.ai_confidence_min;
  document.getElementById('aiFatigueMinutes').value = data.ai_fatigue_minutes;
  document.getElementById('aiFallback').checked = data.ai_fallback_heuristics;
}

document.getElementById('saveSettings').addEventListener('click', async () => {
  const body = {
    standup_enabled: document.getElementById('standupEnabled').checked,
    standup_min: Number(document.getElementById('standupMin').value),
    telegram_standup_message: document.getElementById('standupMessage').value,
    morning_summary_enabled: document.getElementById('morningEnabled').checked,
    morning_summary_hour: Number(document.getElementById('morningHour').value),
    work_presence_min_cm: Number(document.getElementById('workMin').value),
    work_presence_max_cm: Number(document.getElementById('workMax').value),
    work_presence_type: document.getElementById('workType').value,
    radar_gate_work: Number(document.getElementById('gateWork').value),
    sleep_breath_max_cm: Number(document.getElementById('sleepMax').value),
    sleep_energy_min: Number(document.getElementById('sleepEnergy').value),
    radar_gate_sleep: Number(document.getElementById('gateSleep').value),
    media_backend: document.getElementById('mediaBackend').value,
    ai_enabled: document.getElementById('aiEnabled').checked,
    ai_record_mode: document.getElementById('aiRecordMode').checked,
    ai_confidence_min: Number(document.getElementById('aiConfidenceMin').value),
    ai_fatigue_minutes: Number(document.getElementById('aiFatigueMinutes').value),
    ai_fallback_heuristics: document.getElementById('aiFallback').checked,
  };
  const res = await fetch('/api/settings/', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const data = await res.json();
  document.getElementById('settingsStatus').textContent = data.ok ? 'Saved' : 'Failed';
});

loadSettings();
loadGestureSummary('gestureSummary');
