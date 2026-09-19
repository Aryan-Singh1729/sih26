"use strict";

const COLORS = Object.freeze({
  critical: "#ff6f79",
  close: "#ff9d57",
  intermediate: "#aaa8ff",
  clear: "#67e8cf",
  invalid: "#303a49"
});

const canvas = document.querySelector("#radar");
const context = canvas.getContext("2d");
const cone = document.querySelector("#depth-cone");
const statusNodes = [...document.querySelectorAll("[data-status]")];
const nearestNode = document.querySelector("#nearest");
const frameNode = document.querySelector("#frame");
const ageNode = document.querySelector("#age");
const rayCountNode = document.querySelector("#ray-count");
const cameraStatusNode = document.querySelector("#camera-status");
const cameraFeed = document.querySelector("#camera-feed");
const cameraViewport = document.querySelector(".camera-viewport");
const cameraPlaceholder = document.querySelector("#camera-placeholder p");

let telemetry = null;
let lastMessageAt = 0;
let connectionState = "starting";

const cells = Array.from({ length: 96 }, (_, index) => {
  const cell = document.createElement("div");
  cell.className = "depth-cell";
  cell.dataset.index = String(index);
  cone.append(cell);
  return cell;
});

function setStatus(state) {
  connectionState = state;
  const normalized = state === "live" ? "live" : state === "stale" ? "stale" :
    state === "starting" ? "starting" : "disconnected";
  statusNodes.forEach((node) => {
    node.className = `status ${normalized}`;
    node.textContent = normalized.toUpperCase();
  });
}

function updateCone(readings) {
  const byPosition = new Map(readings.map((cell) => [`${cell.column}:${cell.row}`, cell]));
  for (let row = 0; row < 6; row += 1) {
    for (let column = 0; column < 16; column += 1) {
      const element = cells[row * 16 + column];
      const reading = byPosition.get(`${column}:${row}`);
      const valid = reading?.valid === true && Number.isFinite(reading.distance_m);
      const risk = valid && COLORS[reading.risk] ? reading.risk : "invalid";
      element.style.backgroundColor = COLORS[risk];
      element.style.opacity = valid ? String(0.58 + 0.42 * reading.confidence) : "0.72";
      element.title = valid
        ? `Column ${column + 1}, row ${row + 1}: ${reading.distance_m.toFixed(2)} m (${risk})`
        : `Column ${column + 1}, row ${row + 1}: unknown`;
    }
  }
}

function acceptTelemetry(event) {
  let next;
  try { next = JSON.parse(event.data); } catch { setStatus("disconnected"); return; }
  if (next.schema_version !== 1) { setStatus("disconnected"); return; }
  telemetry = next;
  lastMessageAt = performance.now();
  setStatus(next.stale ? "stale" : next.sensor_status);
  updateCone(Array.isArray(next.cells) ? next.cells : []);

  const validRays = (next.rays || []).filter((ray) => ray.valid && Number.isFinite(ray.range_m));
  const nearest = validRays.reduce((value, ray) => Math.min(value, ray.range_m), Infinity);
  nearestNode.textContent = Number.isFinite(nearest) ? `${nearest.toFixed(2)} m` : "UNKNOWN";
  frameNode.textContent = String(next.frame_number ?? "—");
  rayCountNode.textContent = `${validRays.length} / ${(next.rays || []).length || 48}`;
}

const stream = new EventSource("/events");
stream.addEventListener("telemetry", acceptTelemetry);
stream.onopen = () => { if (!telemetry) setStatus("starting"); };
stream.onerror = () => setStatus("disconnected");

function setCameraStatus(state) {
  const normalized = state === "live" ? "live" : state === "stale" ? "stale" :
    state === "starting" ? "starting" : "disconnected";
  cameraStatusNode.className = `status ${normalized}`;
  cameraStatusNode.textContent = normalized.toUpperCase();
  cameraViewport.classList.toggle("live", normalized === "live");
  if (normalized !== "live") cameraPlaceholder.textContent = `CAMERA ${normalized.toUpperCase()}`;
}

cameraFeed.addEventListener("load", () => cameraViewport.classList.add("live"));
cameraFeed.addEventListener("error", () => setCameraStatus("disconnected"));

async function refreshHealth() {
  try {
    const response = await fetch("/health", { cache: "no-store" });
    if (!response.ok) throw new Error("health unavailable");
    const health = await response.json();
    setCameraStatus(health.emeet_state || "disconnected");
  } catch {
    setCameraStatus("disconnected");
  }
}

refreshHealth();
setInterval(refreshHealth, 1000);

function fitCanvas() {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.round(canvas.clientWidth * ratio));
  const height = Math.max(1, Math.round(canvas.clientHeight * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return { width: canvas.clientWidth, height: canvas.clientHeight };
}

function line(x1, y1, x2, y2, color, width = 1, dash = []) {
  context.beginPath(); context.moveTo(x1, y1); context.lineTo(x2, y2);
  context.strokeStyle = color; context.lineWidth = width; context.setLineDash(dash); context.stroke();
  context.setLineDash([]);
}

function drawRobot(x, y) {
  context.save(); context.translate(x, y);
  context.fillStyle = "#111c25"; context.strokeStyle = "#6fe7ef"; context.lineWidth = 1.5;
  context.beginPath(); context.roundRect(-24, -30, 48, 58, 9); context.fill(); context.stroke();
  context.fillStyle = "#eba83f"; context.beginPath(); context.roundRect(-17, -24, 34, 43, 6); context.fill();
  context.fillStyle = "#17232d";
  context.fillRect(-29, -20, 7, 17); context.fillRect(22, -20, 7, 17);
  context.fillRect(-29, 7, 7, 17); context.fillRect(22, 7, 7, 17);
  context.fillStyle = "#e9fcff"; context.fillRect(-10, -18, 20, 4);
  context.fillStyle = "#ffe16a";
  context.beginPath(); context.moveTo(0, -54); context.lineTo(-8, -38); context.lineTo(-3, -38);
  context.lineTo(-3, -27); context.lineTo(3, -27); context.lineTo(3, -38);
  context.lineTo(8, -38); context.closePath(); context.fill();
  context.restore();
}

function draw() {
  const { width, height } = fitCanvas();
  const origin = { x: width / 2, y: height - Math.max(95, height * 0.10) };
  const topPanelBottom = Math.min(282, 18 + Math.max(180, height * 0.27));
  const available = Math.max(140, origin.y - topPanelBottom - 18);
  const radius = Math.min(width * 0.47, available);
  const maxRange = telemetry?.display_range_m || 5;
  const fov = telemetry?.horizontal_fov_rad || 1.2;

  context.clearRect(0, 0, width, height);
  const gradient = context.createRadialGradient(origin.x, origin.y, 0, origin.x, origin.y, radius * 1.3);
  gradient.addColorStop(0, "#102733"); gradient.addColorStop(1, "#071018");
  context.fillStyle = gradient; context.fillRect(0, 0, width, height);

  context.strokeStyle = "rgba(106,191,208,.10)"; context.lineWidth = 1;
  for (let x = origin.x % 48; x < width; x += 48) line(x, 0, x, height, "rgba(106,191,208,.055)");
  for (let y = origin.y % 48; y < height; y += 48) line(0, y, width, y, "rgba(106,191,208,.055)");

  for (let step = 1; step <= 5; step += 1) {
    const ring = radius * step / 5;
    context.beginPath(); context.arc(origin.x, origin.y, ring, Math.PI + fov / 2, Math.PI * 2 - fov / 2);
    context.strokeStyle = "rgba(121,205,219,.15)"; context.setLineDash([3, 6]); context.stroke(); context.setLineDash([]);
    context.fillStyle = "rgba(134,190,200,.45)"; context.font = "9px ui-monospace, monospace";
    context.fillText(`${(maxRange * step / 5).toFixed(0)}m`, origin.x + 7, origin.y - ring + 11);
  }

  for (const side of [-1, 1]) {
    const angle = side * fov / 2;
    line(origin.x, origin.y, origin.x + Math.sin(angle) * radius,
      origin.y - Math.cos(angle) * radius, "rgba(105,217,229,.28)", 1, [5, 7]);
  }

  for (const ray of telemetry?.rays || []) {
    if (!ray.valid || !Number.isFinite(ray.range_m) || !Number.isFinite(ray.bearing_rad)) continue;
    const length = Math.min(ray.range_m / maxRange, 1) * radius;
    const endX = origin.x + Math.sin(ray.bearing_rad) * length;
    const endY = origin.y - Math.cos(ray.bearing_rad) * length;
    const color = COLORS[ray.risk] || COLORS.invalid;
    line(origin.x, origin.y, endX, endY, color, ray.risk === "critical" ? 2.2 : 1.25);
    context.fillStyle = color; context.beginPath(); context.arc(endX, endY, 2.2, 0, Math.PI * 2); context.fill();
  }

  for (const cluster of telemetry?.clusters || []) {
    if (!Number.isFinite(cluster.nearest_m) || !Number.isFinite(cluster.bearing_rad)) continue;
    const length = Math.min(cluster.nearest_m / maxRange, 1) * radius;
    const x = origin.x + Math.sin(cluster.bearing_rad) * length;
    const y = origin.y - Math.cos(cluster.bearing_rad) * length;
    context.fillStyle = "rgba(227,249,251,.82)"; context.font = "9px ui-monospace, monospace";
    context.fillText(`${cluster.nearest_m.toFixed(2)}m`, x + 6, y - 6);
  }

  drawRobot(origin.x, origin.y);
  const elapsed = lastMessageAt ? performance.now() - lastMessageAt : Infinity;
  ageNode.textContent = Number.isFinite(elapsed) ? `${Math.round(elapsed)} ms` : "—";
  if (elapsed > 1500 && connectionState === "live") setStatus("stale");
  requestAnimationFrame(draw);
}

requestAnimationFrame(draw);
