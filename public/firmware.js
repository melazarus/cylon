/*
 * Firmware update support: fetch release assets from GitHub and flash them over
 * WebUSB using the ROM bootloader (WCH ISP).
 *
 * The flasher itself is NOT bundled with this site. It is GPL-2.0 code and is
 * loaded on demand, at runtime, from the npm CDN:
 *
 *   wchisp-web — https://github.com/DrSkunk/wchisp-web (GPL-2.0-only)
 *
 * Keeping it out of the repository avoids mixing its licence with Cylon's MIT
 * licence; the page only calls its published JavaScript API.
 */

export const REPO = "melazarus/cylon";

// Pin the CDN version so a future upstream release cannot change the API under us.
const FLASHER_VERSION = "0.2.1";
const FLASHER_URL = `https://unpkg.com/wchisp-web@${FLASHER_VERSION}/dist/index.umd.min.js`;

export function webusbSupported() {
  return typeof navigator !== "undefined" && "usb" in navigator;
}

export function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

let flasherPromise = null;

/** Load the (external, GPL) flasher library once. Call it early so the CDN
 *  fetch does not eat the user gesture needed by requestDevice(). */
export function loadFlasher() {
  if (typeof window !== "undefined" && window.WchIsp) return Promise.resolve(window.WchIsp);
  if (!flasherPromise) {
    flasherPromise = new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = FLASHER_URL;
      script.crossOrigin = "anonymous";
      script.onload = () => {
        if (window.WchIsp) resolve(window.WchIsp);
        else reject(new Error("wchisp-web loaded but did not initialise"));
      };
      script.onerror = () => reject(new Error("Could not load the WebUSB flasher from the CDN (check your connection)"));
      document.head.appendChild(script);
    });
  }
  return flasherPromise;
}

/**
 * List available firmware versions.
 *
 * Published release binaries are bundled into `firmware/` at deploy time (see
 * .github/scripts/bundle_firmware.py) so they can be fetched same-origin —
 * GitHub's release asset host does not send CORS headers. Falls back to the
 * GitHub API when the manifest is absent (e.g. local development).
 */
export async function listReleases() {
  try {
    const res = await fetch("./firmware/releases.json", { cache: "no-cache" });
    if (res.ok) {
      const manifest = await res.json();
      if (Array.isArray(manifest)) return manifest;
    }
  } catch {
    /* no bundled manifest — fall through to the API */
  }

  const res = await fetch(`https://api.github.com/repos/${REPO}/releases?per_page=100`, {
    headers: { Accept: "application/vnd.github+json" },
  });
  if (res.status === 403) {
    throw new Error("GitHub API rate limit reached — try again later, or choose a local file");
  }
  if (!res.ok) throw new Error(`GitHub API error ${res.status}`);

  const releases = await res.json();
  const out = [];
  for (const release of releases) {
    if (release.draft) continue;
    const assets = (release.assets || []).filter((a) => a.name.toLowerCase().endsWith(".bin"));
    const asset = assets.find((a) => a.name === "firmware.bin") || assets[0];
    if (!asset) continue;
    out.push({
      tag: release.tag_name,
      name: release.name || release.tag_name,
      publishedAt: release.published_at,
      size: asset.size,
      url: asset.browser_download_url,
    });
  }
  return out;
}

/** Download a firmware release into a Uint8Array. */
export async function downloadFirmware(release) {
  const url = release.file ? `./${release.file}` : release.url;
  if (!url) throw new Error("No firmware download URL for this release");
  const res = await fetch(url, { redirect: "follow" });
  if (!res.ok) throw new Error(`Firmware download failed (${res.status})`);
  return new Uint8Array(await res.arrayBuffer());
}

/**
 * Connect to the bootloader. This is the gesture-sensitive step, so it is done
 * before any firmware download. Call it directly from a click handler.
 *
 * @param {object} opts
 * @param {boolean} opts.enterBootloader  send the MIDI reboot command first
 * @param {object} opts.midi
 * @param {boolean} [opts.acceptAllDevices]  show every USB device, not just WCH
 * @param {(msg: string) => void} [opts.onLog]
 * @returns {Promise<object>} a wchisp-web transport
 */
export async function connectBootloader({ enterBootloader, midi, acceptAllDevices = false, onLog }) {
  const WchIsp = await loadFlasher();

  if (enterBootloader) {
    if (!midi || !midi.connected) {
      throw new Error("Connect to the board over MIDI first so it can be rebooted");
    }
    onLog("Sending bootloader command…");
    midi.rebootToBootloader();
    // Give the board time to leave USB-MIDI and re-enumerate as WCH ISP.
    await sleep(1800);
  }

  // "Show all" bypasses the VID/PID filter, which is useful when the device
  // enumerates with an unexpected ID or a driver keeps it out of the filter.
  if (acceptAllDevices) {
    onLog("Listing all USB devices — pick the WCH/Cylon bootloader.");
    const device = await navigator.usb.requestDevice({ acceptAllDevices: true });
    onLog(`Selected USB ${hex4(device.vendorId)}:${hex4(device.productId)} ${device.productName || ""}`.trim());
    return WchIsp.WebUsbTransport.openDevice(device);
  }

  // Reuse a previously authorised bootloader without a chooser; otherwise ask.
  const paired = await WchIsp.WebUsbTransport.openPaired();
  if (paired.length) {
    onLog("Using the previously authorised bootloader.");
    return paired[0];
  }
  onLog("Select the Cylon bootloader (WCH ISP) in the browser dialog — it may take a moment to appear.");
  return WchIsp.WebUsbTransport.request();
}

function hex4(value) {
  return `0x${(value ?? 0).toString(16).padStart(4, "0")}`;
}

/** Erase, program, verify and reset. `transport` comes from connectBootloader. */
export async function programBootloader(transport, firmware, { onProgress, onLog }) {
  const WchIsp = window.WchIsp;
  try {
    const isp = new WchIsp.WchIspFlasher(transport);
    const info = await isp.connect(onProgress);
    onLog(`Connected: ${info.name} (chipId 0x${info.chipId.toString(16)}, type 0x${info.deviceType.toString(16)})`);
    if (info.codeFlashProtected) onLog("Note: the code flash reports as read-protected.");

    // The reference wchisp CLI erases `binary.len()/1024 + 1` sectors. Doing
    // that here (and telling wchisp-web not to erase) matches it exactly;
    // wchisp-web's own erase is one sector short, which leaves the final sector
    // unerased on the CH32X035 and makes verification fail near the end.
    const sectors = Math.ceil(firmware.length / 1024) + 1;
    onLog(`Erasing ${sectors} sector(s)…`);
    await isp.eraseCode(sectors, "sectors", onProgress);

    await isp.flash(firmware, {
      erase: false,
      verify: true,
      reset: true,
      // Write/verify only the real firmware bytes; the padding adds nothing and
      // was where verification failed.
      pad: false,
      progress: onProgress,
    });
    onLog("Flashed, verified and reset.");
    return info;
  } finally {
    try {
      await transport.close?.();
    } catch {
      /* ignore */
    }
  }
}
