async function fetchToday() {
  const res = await fetch('/api/dashboard/today');
  const data = await res.json();
  document.getElementById('mode').textContent = `Mode: ${data.mode}`;
  document.getElementById('online').textContent = `Online: ${data.online ? 'yes' : 'no'}`;
  document.getElementById('present').textContent = `Present: ${data.present ? 'yes' : 'no'}`;
  const hours = Math.floor(data.today_seconds / 3600);
  const minutes = Math.floor((data.today_seconds % 3600) / 60);
  document.getElementById('today').textContent = `${hours}h ${String(minutes).padStart(2, '0')}m`;
  const sMin = Math.floor(data.session_seconds / 60);
  document.getElementById('session').textContent = `Session: ${sMin}m`;
}

fetchToday();
setInterval(fetchToday, 5000);
