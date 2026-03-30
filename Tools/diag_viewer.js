/**
 * @file diag_viewer.js
 * @brief Lightweight viewer for client and server diagnostics JSON reports.
 *
 * The viewer intentionally compares a curated subset of high-signal metrics
 * instead of trying to expose every raw field in the report schema.
 */
"use strict";

// ----- State and bootstrapping -----

const state = { a: null, b: null };
const REPORT_CLIENT = "client_diagnostics_report";
const REPORT_SERVER = "net_diagnostics_report";

document.getElementById("fileA").addEventListener("change", (event) => {
  loadFile(event.target.files[0], "a");
});

document.getElementById("fileB").addEventListener("change", (event) => {
  loadFile(event.target.files[0], "b");
});

document.addEventListener("click", (event) => {
  const button = event.target.closest("[data-clear-slot]");
  if (!button) {
    return;
  }

  clearSlot(button.dataset.clearSlot);
});

render();

async function loadFile(file, slot) {
  if (!file) {
    state[slot] = null;
    render();
    return;
  }

  try {
    const parsed = JSON.parse(await file.text());
    state[slot] = { fileName: file.name, data: parsed };
    render();
  } catch (error) {
    state[slot] = null;
    render();
    renderError(slot, `Failed to parse ${file.name}: ${error.message}`);
  }
}

function clearSlot(slot) {
  state[slot] = null;
  document.getElementById(slot === "a" ? "fileA" : "fileB").value = "";
  render();
}

function renderError(slot, message) {
  const target = document.getElementById(slot === "a" ? "reportA" : "reportB");
  target.innerHTML = `<h2>Report ${slot.toUpperCase()}</h2><div class="empty">${escapeHtml(message)}</div>`;
  renderComparison();
}

function render() {
  const reportAElement = document.getElementById("reportA");
  const reportBElement = document.getElementById("reportB");
  const hasA = Boolean(state.a);
  const hasB = Boolean(state.b);

  renderReport("a", reportAElement);
  renderReport("b", reportBElement);

  reportAElement.hidden = !hasA && hasB;
  reportBElement.hidden = !hasB && hasA;
  document.getElementById("clearTopA").disabled = !hasA;
  document.getElementById("clearTopB").disabled = !hasB;

  renderComparison();
}

// ----- Rendering -----

function renderReport(slot, target) {
  const payload = state[slot];
  if (!payload) {
    target.innerHTML = `<h2>Report ${slot.toUpperCase()}</h2><div class="empty">No report loaded.</div>`;
    return;
  }

  const report = payload.data;
  const metrics = extractKeyMetrics(report);
  const summaryMetrics = extractReportSummary(report);
  const configRows = toRows(report.config || {});
  const eventItems = (report.recent_events || []).slice(-10).reverse();

  target.innerHTML = `
    <div class="report-header">
      <h2>Report ${slot.toUpperCase()}</h2>
      <button class="clear-button" type="button" aria-label="Clear report ${slot.toUpperCase()}" data-clear-slot="${slot}">×</button>
    </div>
    <div class="report-file">${escapeHtml(payload.fileName)}</div>
    <div class="summary">
      ${summaryMetrics.map((metric) => renderMetric(metric.label, metric.display, metric.description)).join("")}
    </div>
    <h3>Key Metrics</h3>
    <div class="summary">
      ${metrics.map((metric) => renderMetric(metric.label, metric.display, metric.description)).join("")}
    </div>
    <h3 class="section-gap">Config</h3>
    ${renderTable(configRows)}
    <h3 class="section-gap">Recent Events</h3>
    ${renderEvents(eventItems)}
  `;
}

function renderComparison() {
  const title = document.getElementById("comparisonTitle");
  const target = document.getElementById("comparisonContent");
  const reportA = state.a?.data;
  const reportB = state.b?.data;

  if (!reportA && !reportB) {
    title.textContent = "Overview";
    target.innerHTML = "Load one or two reports to compare key metrics.";
    return;
  }

  if (reportA && !reportB) {
    title.textContent = "Overview";
    target.innerHTML = renderSingleComparison(extractKeyMetrics(reportA), "A");
    return;
  }

  if (!reportA && reportB) {
    title.textContent = "Overview";
    target.innerHTML = renderSingleComparison(extractKeyMetrics(reportB), "B");
    return;
  }

  if (reportA.report !== reportB.report) {
    title.textContent = "Overview";
    target.innerHTML = "Loaded reports are different kinds and are shown side by side only.";
    return;
  }

  title.textContent = "Comparison";

  const metricsA = extractKeyMetrics(reportA);
  const metricsB = extractKeyMetrics(reportB);
  const bars = [];
  const rows = [];

  for (let index = 0; index < Math.min(metricsA.length, metricsB.length); index += 1) {
    const a = metricsA[index];
    const b = metricsB[index];
    const max = Math.max(a.value, b.value, 1);
    const delta = b.value - a.value;
    const deltaClass = deltaClassForMetric(a, delta);
    const tones = comparisonBarTones(a, delta);

    rows.push(`
      <tr>
        <td>${escapeHtml(a.label)}</td>
        <td>${escapeHtml(a.display)}</td>
        <td>${escapeHtml(b.display)}</td>
        <td class="${deltaClass}">${formatDelta(delta)}</td>
      </tr>
    `);

    bars.push(`
      <div class="bar-row">
        <div class="bar-title">${escapeHtml(a.label)}</div>
        <div class="bar-track">
          ${renderBar("A", a.value, max, tones.a, a.display)}
          ${renderBar("B", b.value, max, tones.b, b.display)}
        </div>
      </div>
    `);
  }

  target.innerHTML = `
    <table>
      <thead>
        <tr>
          <th>Metric</th>
          <th>Report A</th>
          <th>Report B</th>
          <th>Delta (B - A)</th>
        </tr>
      </thead>
      <tbody>${rows.join("")}</tbody>
    </table>
    <div class="bars">${bars.join("")}</div>
  `;
}

function renderSingleComparison(metrics, label) {
  return `
    <div class="bars">
      ${metrics.map((metric) => `
        <div class="bar-row">
          <div class="bar-title">${escapeHtml(metric.label)}</div>
          ${renderBar(label, metric.value, Math.max(metric.value, 1), "neutral", metric.display)}
        </div>
      `).join("")}
    </div>
  `;
}

function renderBar(label, value, max, tone, displayOverride = null) {
  const width = Math.max(2, Math.min(100, (value / max) * 100));
  const display = displayOverride ?? formatNumber(value);
  return `
    <div class="bar-line">
      <div class="bar-value">${escapeHtml(label)}: ${escapeHtml(display)}</div>
      <div class="bar ${tone}">
        <span style="width:${width}%;"></span>
      </div>
    </div>
  `;
}

function renderMetric(label, display, description = "") {
  return `
    <div class="metric" title="${escapeHtml(description)}">
      <span class="label" title="${escapeHtml(description)}">${escapeHtml(label)}</span>
      <span class="value">${escapeHtml(String(display))}</span>
    </div>
  `;
}

function renderTable(rows) {
  if (!rows.length) {
    return `<div class="empty">No config data present.</div>`;
  }

  return `
    <table>
      <thead><tr><th>Key</th><th>Value</th></tr></thead>
      <tbody>${rows.map((row) => `<tr><td>${escapeHtml(row.key)}</td><td>${escapeHtml(row.value)}</td></tr>`).join("")}</tbody>
    </table>
  `;
}

function renderEvents(events) {
  if (!events.length) {
    return `<div class="empty">No recent events recorded.</div>`;
  }

  return `
    <ol class="events">
      ${events.map((event) => `<li>${escapeHtml(describeEvent(event))}</li>`).join("")}
    </ol>
  `;
}

// ----- Report extraction -----

function extractKeyMetrics(report) {
  if (report.report === REPORT_CLIENT) {
    const corrections = report.prediction?.corrections_applied || 0;
    const mismatches = report.prediction?.corrections_mismatched || 0;
    const mismatchRate = corrections > 0 ? (mismatches / corrections) * 100 : 0;
    const avgRtt = Number(report.transport?.avg_rtt_ms) || Number(report.transport?.rtt_ms) || 0;
    const avgJitter = Number(report.transport?.avg_rtt_var_ms) || Number(report.transport?.rtt_var_ms) || 0;
    const avgLoss = Number(report.transport?.avg_loss_permille) || Number(report.transport?.loss_permille) || 0;

    return [
      makeMetric("Avg RTT", avgRtt, `${avgRtt.toFixed(1)} ms`,
        "Average sampled round-trip time during this client session.", "lower_better"),
      makeMetric("Avg Jitter", avgJitter, `${avgJitter.toFixed(1)} ms`,
        "Average sampled RTT variance during this client session.", "lower_better"),
      makeMetric("Avg Loss", avgLoss, `${(avgLoss / 10).toFixed(1)} %`,
        "Average sampled packet loss during this client session.", "lower_better"),
      makeMetric("Corrections Processed", corrections, String(corrections),
        "Authoritative correction messages processed locally. These provide context for how often prediction was checked, not how often it was wrong.", "neutral"),
      makeMetric("Prediction Mismatches", mismatches, String(mismatches),
        "Subset of correction messages where the authoritative state differed from the predicted local state.", "lower_better"),
      makeMetric("Prediction Mismatch Rate", mismatchRate, `${mismatchRate.toFixed(2)} %`,
        "Prediction mismatches divided by correction messages applied.", "lower_better"),
      makeMetric("Recovery Activations", report.prediction?.recovery_activations || 0, String(report.prediction?.recovery_activations || 0),
        "How often client-side prediction entered recovery because correction replay could not continue cleanly.", "lower_better"),
      makeMetric("Max Correction Delta (Q8)", report.prediction?.max_correction_delta_q || 0, `${report.prediction?.max_correction_delta_q || 0} Q8`,
        "Largest position correction magnitude observed in tile-Q8 units.", "lower_better")
    ];
  }

  const direct = report.simulation_continuity?.direct_deadline_consumes || 0;
  const gaps = report.simulation_continuity?.simulation_gaps || 0;
  const buffered = report.simulation_continuity?.buffered_deadline_recoveries || 0;
  const roundsEnded = report.simulation_continuity?.rounds_ended || 0;
  const continuityTotal = direct + gaps + buffered;
  const gapRate = continuityTotal > 0 ? (gaps / continuityTotal) * 100 : 0;
  const worstRttSample = maxTransportSample(report.transport_samples, "rtt_ms");
  const worstJitterSample = maxTransportSample(report.transport_samples, "rtt_var_ms");
  const worstLossSample = maxTransportSample(report.transport_samples, "loss_permille");
  const worstRtt = Number(worstRttSample?.rtt_ms) || 0;
  const worstJitter = Number(worstJitterSample?.rtt_var_ms) || 0;
  const worstLoss = Number(worstLossSample?.loss_permille) || 0;
  const worstRttPlayer = formatPlayerLabel(worstRttSample?.player_id);
  const worstJitterPlayer = formatPlayerLabel(worstJitterSample?.player_id);
  const worstLossPlayer = formatPlayerLabel(worstLossSample?.player_id);

  return [
    makeMetric(worstRttSample ? `Worst RTT (${worstRttPlayer})` : "Worst RTT", worstRtt, `${worstRtt.toFixed(1)} ms`,
      "Highest sampled round-trip time among connected gameplay players.", "lower_better"),
    makeMetric(worstJitterSample ? `Worst Jitter (${worstJitterPlayer})` : "Worst Jitter", worstJitter, `${worstJitter.toFixed(1)} ms`,
      "Highest sampled RTT variance among connected gameplay players.", "lower_better"),
    makeMetric(worstLossSample ? `Worst Loss (${worstLossPlayer})` : "Worst Loss", worstLoss, `${(worstLoss / 10).toFixed(1)} %`,
      "Highest sampled packet loss among connected gameplay players.", "lower_better"),
    makeMetric("Input Gap Rate", gapRate, `${gapRate.toFixed(2)} %`,
      "Share of authoritative input consume deadlines that had no fresh exact input and had to gap.", "lower_better"),
    makeMetric("Actual Simulation Gaps", gaps, String(gaps),
      "Server ticks where no fresh input was available by the consume deadline, so prior held buttons were reused.", "lower_better"),
    makeMetric("Buffered Input Recoveries", buffered, String(buffered),
      "Server ticks recovered from later buffered input history instead of producing an actual simulation gap.", "neutral"),
    makeMetric("Rounds Ended", roundsEnded, String(roundsEnded),
      "Rounds that reached a server-authoritative end state during this diagnostics session.", "neutral"),
    makeMetric("Bombs Placed", report.simulation_continuity?.bombs_placed || 0, String(report.simulation_continuity?.bombs_placed || 0),
      "Authoritative bombs placed this session.", "neutral"),
    makeMetric("Bricks Destroyed", report.simulation_continuity?.bricks_destroyed || 0, String(report.simulation_continuity?.bricks_destroyed || 0),
      "Authoritative bricks destroyed this session.", "neutral")
  ];
}

function extractReportSummary(report) {
  const metrics = [
    makeMetric("Owner", 0, report.session_owner || "unknown"),
    makeMetric("Duration", Number(report.session?.duration_ms) || 0, formatMs(report.session?.duration_ms)),
    makeMetric("Schema", 0, formatReportVersion(report))
  ];

  if (report.report === REPORT_CLIENT) {
    metrics.push(makeMetric("Player", 0, formatPlayerLabel(report.config?.assigned_player_id)));
  } else if (report.report === REPORT_SERVER) {
    metrics.push(makeMetric("Ticks", Number(report.session?.ticks) || 0, String(report.session?.ticks ?? 0)));
    metrics.push(makeMetric("Rounds", Number(report.simulation_continuity?.rounds_ended) || 0, String(report.simulation_continuity?.rounds_ended ?? 0)));
  }

  return metrics;
}

function maxTransportSample(samples, key) {
  if (!Array.isArray(samples) || samples.length === 0) {
    return null;
  }

  return samples.reduce((best, sample) => {
    const value = Number(sample?.[key]) || 0;
    const bestValue = Number(best?.[key]) || -1;
    return value > bestValue ? sample : best;
  }, null);
}

function toRows(obj, prefix = "") {
  const rows = [];
  for (const [key, value] of Object.entries(obj)) {
    const fullKey = prefix ? `${prefix}.${key}` : key;
    if (value && typeof value === "object" && !Array.isArray(value)) {
      rows.push(...toRows(value, fullKey));
    } else {
      rows.push({ key: fullKey, value: formatConfigValue(fullKey, value) });
    }
  }
  return rows;
}

// ----- Labels and formatting -----

function describeEvent(event) {
  const parts = [`ts=${event.ts_ms}`];
  if (event.type != null) parts.push(`event=${eventTypeLabel(event.type)}`);
  if (event.lifecycle_type != null && event.type === 3) parts.push(`lifecycle=${lifecycleTypeLabel(event.lifecycle_type)}`);
  if (event.simulation_type != null && event.type === 6) parts.push(`sim=${simulationTypeLabel(event.simulation_type)}`);
  if (event.player_id != null) parts.push(`player=${formatPlayerLabel(event.player_id)}`);
  if (event.msg_type != null) parts.push(`msg=${msgTypeLabel(event.msg_type)}`);
  if (event.seq) parts.push(`seq=${event.seq}`);
  if (event.note) parts.push(String(event.note));
  return parts.join(" ");
}

function eventTypeLabel(type) {
  switch (Number(type)) {
    case 1: return "SessionBegin";
    case 2: return "SessionEnd";
    case 3: return "PeerLifecycle";
    case 4: return "PacketSent";
    case 5: return "PacketRecv";
    case 6: return "Simulation";
    case 7: return "Flow";
    default: return `Unknown(${type})`;
  }
}

function lifecycleTypeLabel(type) {
  switch (Number(type)) {
    case 0: return "TransportConnected";
    case 1: return "PlayerAccepted";
    case 2: return "PeerRejected";
    case 3: return "PeerDisconnected";
    case 4: return "TransportDisconnectedBeforeHandshake";
    default: return `Unknown(${type})`;
  }
}

function simulationTypeLabel(type) {
  switch (Number(type)) {
    case 0: return "Gap";
    case 1: return "BufferedDeadlineRecovery";
    case 2: return "RoundEnded";
    default: return `Unknown(${type})`;
  }
}

function msgTypeLabel(type) {
  switch (Number(type)) {
    case 1: return "Hello";
    case 2: return "Welcome";
    case 3: return "Reject";
    case 4: return "LevelInfo";
    case 5: return "LobbyState";
    case 6: return "LobbyReady";
    case 7: return "MatchLoaded";
    case 8: return "MatchStart";
    case 9: return "MatchCancelled";
    case 10: return "MatchResult";
    case 11: return "Input";
    case 12: return "Snapshot";
    case 13: return "Correction";
    case 14: return "BombPlaced";
    case 15: return "ExplosionResolved";
    default: return `Unknown(${type})`;
  }
}

function makeMetric(label, value, display, description = "", polarity = "neutral") {
  return { label, value: Number(value) || 0, display, description, polarity };
}

function deltaClassForMetric(metric, delta) {
  if (delta === 0 || metric.polarity === "neutral") {
    return "delta-flat";
  }

  if (metric.polarity === "higher_better") {
    return delta > 0 ? "delta-good" : "delta-bad";
  }

  return delta < 0 ? "delta-good" : "delta-bad";
}

function comparisonBarTones(metric, delta) {
  if (delta === 0 || metric.polarity === "neutral") {
    return { a: "neutral", b: "neutral" };
  }

  if (metric.polarity === "higher_better") {
    return delta > 0 ? { a: "bad", b: "good" } : { a: "good", b: "bad" };
  }

  return delta < 0 ? { a: "bad", b: "good" } : { a: "good", b: "bad" };
}

function formatPlayerLabel(value) {
  if (value == null || value === "" || Number.isNaN(Number(value))) {
    return "unknown";
  }
  return `P${Number(value) + 1}`;
}

function formatReportVersion(report) {
  const version = report?.report_version;
  return Number.isFinite(Number(version)) ? `v${Number(version)}` : "legacy";
}

function formatConfigValue(key, value) {
  if (key.endsWith("assigned_player_id")) {
    return formatPlayerLabel(value);
  }
  return Array.isArray(value) ? JSON.stringify(value) : String(value);
}

function formatMs(value) {
  const numeric = Number(value) || 0;
  return `${numeric} ms`;
}

function formatNumber(value) {
  return Number.isInteger(value) ? String(value) : value.toFixed(2);
}

function formatDelta(value) {
  if (value === 0) {
    return "0";
  }
  const prefix = value > 0 ? "+" : "";
  return `${prefix}${formatNumber(value)}`;
}

function escapeHtml(text) {
  return String(text)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll("\"", "&quot;");
}
