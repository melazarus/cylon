/*
 * Cylon control site — UI wiring.
 *
 * LED / animation / pin control goes over USB-MIDI (CylonMidi). Firmware
 * updates go over WebUSB via the externally-loaded wchisp-web library.
 */

import { CylonMidi } from "./cylon-midi.js";
import { listReleases, downloadFirmware, connectBootloader, programBootloader, loadFlasher, webusbSupported } from "./firmware.js";

const cylon = new CylonMidi();

const $ = (id) => document.getElementById(id);
const els = {
  support: $("support"),
  port: $("port"),
  connect: $("connect"),
  connStatus: $("conn-status"),
  fwVersion: $("fw-version"),
  readVersion: $("read-version"),
  leds: $("leds"),
  animColor: $("anim-color"),
  anims: $("anims"),
  pins: $("pins"),
  fwSelect: $("fw-select"),
  fwRefresh: $("fw-refresh"),
  fwFile: $("fw-file"),
  fwBoot: $("fw-boot"),
  fwAllUsb: $("fw-all-usb"),
  fwFlash: $("fw-flash"),
  fwProgress: $("fw-progress"),
  fwPhase: $("fw-phase"),
  fwLog: $("fw-log"),
  fwStatus: $("fw-status"),
  servoMin: $("servo-min"),
  servoMax: $("servo-max"),
  servoApply: $("servo-apply"),
};

const state = {
  connected: false,
  releases: [],
  localFirmware: null,
  selectedRelease: null,
  busy: false,
  midiControls: [],
  fwControls: [],
};

/* ------------------------------------------------------------- utilities */

const hexToRgb = (hex) => {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
};
const rgbToHex = (r, g, b) => "#" + [r, g, b].map((v) => v.toString(16).padStart(2, "0")).join("");

function setConnStatus(text, kind = "") {
  els.connStatus.textContent = text;
  els.connStatus.className = `status ${kind}`.trim();
}

function logFirmware(message) {
  const time = new Date().toLocaleTimeString();
  els.fwLog.textContent += `[${time}] ${message}\n`;
  els.fwLog.scrollTop = els.fwLog.scrollHeight;
}

/** Run now, then at most once per `ms`, and once more at the end. */
function throttle(fn, ms) {
  let last = 0;
  let timer = null;
  return () => {
    const wait = ms - (Date.now() - last);
    if (wait <= 0) {
      if (timer) {
        clearTimeout(timer);
        timer = null;
      }
      last = Date.now();
      fn();
    } else if (!timer) {
      timer = setTimeout(() => {
        timer = null;
        last = Date.now();
        fn();
      }, wait);
    }
  };
}

function guardSend(fn) {
  try {
    fn();
  } catch (err) {
    setConnStatus(err.message, "err");
  }
}

/* ------------------------------------------------------------------ LEDs */

/** Colours the firmware shows at boot, used to pre-fill the pickers. */
const LED_DEFAULTS = ["#ff0000", "#00ff00", "#0000ff"];

function buildLeds() {
  const cards = [
    ...CylonMidi.LED_ADDRESS.map((_, i) => ({
      title: `LED ${i}`,
      value: LED_DEFAULTS[i],
      apply: (r, g, b) => cylon.setLed(i, r, g, b),
    })),
    { title: "All LEDs", value: "#ffffff", apply: (r, g, b) => cylon.setAll(r, g, b) },
  ];

  cards.forEach(({ title, value, apply }) => {
    const card = document.createElement("div");
    card.className = "led-card";
    card.innerHTML = `
      <div class="row" style="justify-content:space-between">
        <strong>${title}</strong>
      </div>
      <input type="color" value="${value}" aria-label="${title} colour">
      <div class="swatch"></div>
      <div class="meta"><span>requested <b class="req"></b></span><span>LED <b class="eff"></b></span></div>`;

    const picker = card.querySelector("input[type=color]");
    const swatch = card.querySelector(".swatch");
    const req = card.querySelector(".req");
    const eff = card.querySelector(".eff");

    const render = (send) => {
      const [r, g, b] = hexToRgb(picker.value);
      // The 7-bit transport can only reach even 8-bit values.
      const led = [r & 0xfe, g & 0xfe, b & 0xfe];
      req.textContent = picker.value;
      eff.textContent = rgbToHex(...led);
      swatch.style.background = rgbToHex(...led);
      swatch.style.boxShadow = `0 0 16px ${rgbToHex(...led)}`;
      if (send && state.connected) guardSend(() => apply(...led));
    };

    const sendThrottled = throttle(() => render(true), 40);
    picker.addEventListener("input", () => {
      render(false);
      sendThrottled();
    });
    picker.addEventListener("change", () => render(true));

    render(false);
    els.leds.appendChild(card);
    state.midiControls.push(picker);
  });
}

/* ------------------------------------------------------------ animations */

const ANIMATIONS = [
  { label: "Blink 1 Hz", address: CylonMidi.ANIM.BLINK_1HZ },
  { label: "Blink 2 Hz", address: CylonMidi.ANIM.BLINK_2HZ },
  { label: "Breathe 1 Hz", address: CylonMidi.ANIM.BREATHE_1HZ },
  { label: "Breathe 2 Hz", address: CylonMidi.ANIM.BREATHE_2HZ },
  { label: "Larson", address: CylonMidi.ANIM.LARSON },
  { label: "Wheel offset", address: CylonMidi.ANIM.WHEEL_OFFSET, color: false },
  { label: "Wheel uniform", address: CylonMidi.ANIM.WHEEL_UNIFORM, color: false },
];

function buildAnimations() {
  ANIMATIONS.forEach(({ label, address, color = true }) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "btn small";
    button.textContent = label;
    button.addEventListener("click", () => {
      if (!state.connected) return;
      if (color) {
        const [r, g, b] = hexToRgb(els.animColor.value);
        guardSend(() => cylon.animate(address, r & 0xfe, g & 0xfe, b & 0xfe));
      } else {
        guardSend(() => cylon.animate(address, 0, 0, 0));
      }
    });
    els.anims.appendChild(button);
    state.midiControls.push(button);
  });
}

/* ------------------------------------------------------------------ pins */

function buildPins() {
  const names = ["PA0", "PA1", "PA2", "PA3"];

  names.forEach((name, index) => {
    const row = document.createElement("div");
    row.className = "pin mode-digital";
    row.innerHTML = `
      <span class="pin-name">${name}</span>
      <select class="pin-mode" aria-label="${name} mode">
        <option value="digital">Digital</option>
        <option value="pwm">PWM</option>
        <option value="servo">Servo</option>
      </select>
      <div class="pin-body">
        <div class="pin-field pin-digital">
          <button type="button" class="btn small" aria-pressed="false">Low</button>
        </div>
        <div class="pin-field pin-pwm">
          <input type="range" min="0" max="127" value="0" aria-label="${name} PWM duty">
          <span class="pin-val">0%</span>
        </div>
        <div class="pin-field pin-servo">
          <input type="range" min="0" max="127" value="0" aria-label="${name} servo position">
          <span class="pin-val">0%</span>
          <label class="pin-move" title="Move time: 0 = instant, otherwise ×100 ms">move
            <input type="number" min="0" max="127" value="0" aria-label="${name} servo move time">
            <span class="pin-speed-label">instant</span>
          </label>
        </div>
      </div>`;

    const mode = row.querySelector(".pin-mode");
    const digitalBtn = row.querySelector(".pin-digital button");
    const pwmSlider = row.querySelector(".pin-pwm input[type=range]");
    const pwmVal = row.querySelector(".pin-pwm .pin-val");
    const servoSlider = row.querySelector(".pin-servo input[type=range]");
    const servoVal = row.querySelector(".pin-servo .pin-val");
    const speedInput = row.querySelector(".pin-move input");
    const speedLabel = row.querySelector(".pin-speed-label");

    const percent = (value) => `${Math.round((value / 127) * 100)}%`;

    const applyCurrent = () => {
      if (!state.connected) return;
      if (mode.value === "digital") {
        guardSend(() => cylon.setPin(index, digitalBtn.getAttribute("aria-pressed") === "true"));
      } else if (mode.value === "pwm") {
        guardSend(() => cylon.setPinPwm(index, Number(pwmSlider.value)));
      } else {
        guardSend(() => cylon.setServo(index, Number(servoSlider.value), Number(speedInput.value)));
      }
    };

    mode.addEventListener("change", () => {
      row.className = `pin mode-${mode.value}`;
      applyCurrent();
    });

    digitalBtn.addEventListener("click", () => {
      const on = digitalBtn.getAttribute("aria-pressed") !== "true";
      digitalBtn.setAttribute("aria-pressed", String(on));
      digitalBtn.textContent = on ? "High" : "Low";
      if (state.connected) guardSend(() => cylon.setPin(index, on));
    });

    const sendPwm = throttle(() => {
      if (!state.connected) return;
      guardSend(() => cylon.setPinPwm(index, Number(pwmSlider.value)));
    }, 40);
    pwmSlider.addEventListener("input", () => {
      pwmVal.textContent = percent(Number(pwmSlider.value));
      sendPwm();
    });

    const sendServo = throttle(() => {
      if (!state.connected) return;
      guardSend(() => cylon.setServo(index, Number(servoSlider.value), Number(speedInput.value)));
    }, 40);
    servoSlider.addEventListener("input", () => {
      servoVal.textContent = percent(Number(servoSlider.value));
      sendServo();
    });
    speedInput.addEventListener("input", () => {
      const units = Math.max(0, Math.min(127, Number(speedInput.value) || 0));
      speedLabel.textContent = units === 0 ? "instant" : `${(units * 0.1).toFixed(1)} s`;
      sendServo();
    });

    els.pins.appendChild(row);
    state.midiControls.push(mode, digitalBtn, pwmSlider, servoSlider, speedInput);
  });
}

/* --------------------------------------------------------- servo range */

function applyServoRange() {
  if (!state.connected) return;
  const min = Number(els.servoMin.value);
  const max = Number(els.servoMax.value);
  if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) {
    setConnStatus("Servo range: MAX must be greater than MIN", "err");
    return;
  }
  guardSend(() => cylon.setServoRange(min, max));
  const units = (us) => Math.round(us / 20);
  setConnStatus(`Servo range ${min}–${max} µs (${units(min)}–${units(max)})`, "ok");
}

/* ------------------------------------------------------------ connection */

function refreshPorts() {
  const outputs = cylon.outputs();
  const current = els.port.value;
  els.port.innerHTML = "";

  if (!outputs.length) {
    els.port.innerHTML = `<option value="">No MIDI outputs</option>`;
    cylon.use(null);
    cylon.useInput(null);
    return;
  }

  outputs.forEach((port, i) => {
    const option = document.createElement("option");
    option.value = String(i);
    option.textContent = port.name;
    els.port.appendChild(option);
  });

  const preferred = cylon.pick("cylon");
  const index = preferred ? outputs.indexOf(preferred) : 0;
  els.port.value = String(index >= 0 ? index : 0);
  cylon.use(outputs[Number(els.port.value)]);
  cylon.useInput(cylon.pickInput("cylon"));

  if (current !== els.port.value) {
    setConnStatus(`Selected ${cylon.output.name}`, "ok");
  }
}

function setMidiControlsEnabled(enabled) {
  state.midiControls.forEach((control) => {
    control.disabled = !enabled;
  });
  els.readVersion.disabled = !enabled;
}

function handleStateChange() {
  refreshPorts();
  state.connected = cylon.connected && cylon.outputs().includes(cylon.output);
  setMidiControlsEnabled(state.connected);
  if (!state.connected) setConnStatus("Device disconnected", "err");
}

async function connect() {
  els.connect.disabled = true;
  setConnStatus("Requesting MIDI access…");
  try {
    await cylon.connect();
    cylon.access.addEventListener("statechange", handleStateChange);
    refreshPorts();
    state.connected = cylon.connected;
    setMidiControlsEnabled(state.connected);
    setConnStatus(state.connected ? `Connected to ${cylon.output.name}` : "No MIDI output found", state.connected ? "ok" : "err");
    if (state.connected) readVersion();
  } catch (err) {
    setConnStatus(err.message, "err");
  } finally {
    els.connect.disabled = false;
  }
}

/** Ask for the firmware version, retrying once if the first reply is missed. */
function readVersion(attempt = 0) {
  if (!state.connected) return;
  let answered = false;
  cylon.requestVersion((version) => {
    answered = true;
    els.fwVersion.textContent = version.join(".");
  });
  setTimeout(() => {
    if (!answered && attempt === 0) readVersion(1);
    else if (!answered) els.fwVersion.textContent = "no reply";
  }, 1200);
}

/* -------------------------------------------------------------- firmware */

const PHASE_LABELS = {
  identify: "Identifying chip",
  "read-config": "Reading config",
  unprotect: "Removing read protection",
  erase: "Erasing flash",
  write: "Writing",
  verify: "Verifying",
  reset: "Resetting",
  "eeprom-read": "Reading EEPROM",
  "eeprom-write": "Writing EEPROM",
  "eeprom-erase": "Erasing EEPROM",
};

function setFirmwareStatus(text, kind = "") {
  els.fwStatus.textContent = text;
  els.fwStatus.className = `status ${kind}`.trim();
}

function refreshFirmwareButtons() {
  const ready = !state.busy && (!!state.localFirmware || !!state.selectedRelease);
  els.fwFlash.disabled = !ready;
}

async function loadReleases() {
  els.fwSelect.disabled = true;
  els.fwSelect.innerHTML = `<option value="">Loading releases…</option>`;
  try {
    state.releases = await listReleases();
    if (!state.releases.length) {
      els.fwSelect.innerHTML = `<option value="">No releases found</option>`;
      setFirmwareStatus("No released firmware — use a local file", "");
      return;
    }
    els.fwSelect.innerHTML = "";
    state.releases.forEach((release, i) => {
      const option = document.createElement("option");
      option.value = String(i);
      option.textContent = `${release.tag}${release.size ? ` — ${(release.size / 1024).toFixed(1)} KB` : ""}`;
      els.fwSelect.appendChild(option);
    });
    els.fwSelect.disabled = false;
    els.fwSelect.selectedIndex = 0;
    if (!state.localFirmware) {
      state.selectedRelease = state.releases[0];
      setFirmwareStatus("");
    }
  } catch (err) {
    els.fwSelect.innerHTML = `<option value="">Releases unavailable</option>`;
    setFirmwareStatus(err.message, "err");
  } finally {
    refreshFirmwareButtons();
  }
}

let lastPhase = null;

function progressHandler(event) {
  const label = PHASE_LABELS[event.phase] || event.phase;
  const percent = event.total ? Math.min(100, Math.round((event.done / event.total) * 100)) : 0;
  els.fwProgress.style.width = `${percent}%`;
  els.fwPhase.textContent = event.message || `${label}${event.total ? ` ${percent}%` : ""}`;
  if (event.phase !== lastPhase) {
    lastPhase = event.phase;
    logFirmware(label);
  }
}

async function flash() {
  if (state.busy) return;
  state.busy = true;
  refreshFirmwareButtons();
  els.fwProgress.style.width = "0%";
  setFirmwareStatus("Connecting to bootloader…");
  lastPhase = null;

  let transport = null;
  const onLog = (message) => logFirmware(message);

  try {
    // 1. Connect to the bootloader first — requestDevice() needs the click's
    //    user gesture, so nothing slow may run before it.
    transport = await connectBootloader({
      enterBootloader: els.fwBoot.checked,
      acceptAllDevices: els.fwAllUsb.checked,
      midi: cylon,
      onLog,
    });

    // 2. Fetch or read the image (safe to be slow now).
    setFirmwareStatus("Preparing firmware…");
    let firmware;
    if (state.localFirmware) {
      onLog(`Reading local file ${state.localFirmware.name}…`);
      firmware = state.localFirmware; // the flasher reads .bin/.hex/.elf by name
    } else {
      onLog(`Downloading ${state.selectedRelease.name}…`);
      firmware = await downloadFirmware(state.selectedRelease);
    }
    onLog(`Image size: ${firmware.length} bytes`);

    // 3. Erase, program, verify and reset.
    setFirmwareStatus("Flashing…");
    await programBootloader(transport, firmware, { onProgress: progressHandler, onLog });
    transport = null; // programBootloader closes it

    els.fwProgress.style.width = "100%";
    setFirmwareStatus("Flashed — board resetting", "ok");
    els.fwVersion.textContent = "—";
  } catch (err) {
    setFirmwareStatus(err.message, "err");
    logFirmware(`Error: ${err.message}`);
    try {
      await transport?.close?.();
    } catch {
      /* ignore */
    }
  } finally {
    state.busy = false;
    refreshFirmwareButtons();
  }
}

/* ------------------------------------------------------------------ init */

function updateSupportPill() {
  const midi = !!navigator.requestMIDIAccess;
  const usb = webusbSupported();
  if (midi && usb) {
    els.support.textContent = "Ready";
    els.support.className = "pill ok";
  } else if (midi) {
    els.support.textContent = "MIDI only — no WebUSB";
    els.support.className = "pill";
  } else {
    els.support.textContent = "Unsupported browser";
    els.support.className = "pill err";
  }
}

function bindEvents() {
  els.connect.addEventListener("click", connect);

  els.port.addEventListener("change", () => {
    cylon.use(cylon.outputs()[Number(els.port.value)] || null);
    cylon.useInput(cylon.pickInput("cylon"));
    state.connected = cylon.connected;
    setMidiControlsEnabled(state.connected);
    if (cylon.connected) setConnStatus(`Connected to ${cylon.output.name}`, "ok");
  });

  els.readVersion.addEventListener("click", () => readVersion());

  els.servoApply.addEventListener("click", applyServoRange);

  els.fwRefresh.addEventListener("click", loadReleases);

  els.fwSelect.addEventListener("change", () => {
    state.selectedRelease = state.releases[Number(els.fwSelect.value)] || null;
    state.localFirmware = null;
    els.fwFile.value = "";
    refreshFirmwareButtons();
  });

  els.fwFile.addEventListener("change", () => {
    state.localFirmware = els.fwFile.files[0] || null;
    if (state.localFirmware) {
      state.selectedRelease = null;
      setFirmwareStatus(`Local file: ${state.localFirmware.name}`, "");
    }
    refreshFirmwareButtons();
  });

  els.fwFlash.addEventListener("click", flash);
}

buildLeds();
buildAnimations();
buildPins();
bindEvents();
updateSupportPill();
setMidiControlsEnabled(false);
state.midiControls.push(els.servoMin, els.servoMax, els.servoApply);
loadReleases();

// Warm up the (external) flasher so the CDN fetch does not consume the user
// gesture needed by requestDevice() when the Flash button is clicked.
loadFlasher().catch(() => {});
