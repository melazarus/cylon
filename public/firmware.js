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

/** List published releases that carry a .bin firmware asset, newest first. */
export async function listReleases() {
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
      asset,
    });
  }
  return out;
}

/** Download a release asset into a Uint8Array. */
export async function downloadFirmware(asset) {
  const res = await fetch(asset.browser_download_url, { redirect: "follow" });
  if (!res.ok) throw new Error(`Firmware download failed (${res.status})`);
  return new Uint8Array(await res.arrayBuffer());
}

/**
 * Connect to the bootloader. This is the gesture-sensitive step, so it is done
 * before any firmware download. Call it directly from a click handler.
 *
 * @returns {Promise<object>} a wchisp-web transport
 */
export async function connectBootloader({ enterBootloader, midi, onLog }) {
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

  // Reuse a previously authorised bootloader without a chooser; otherwise ask.
  const paired = await WchIsp.WebUsbTransport.openPaired();
  if (paired.length) {
    onLog("Using the previously authorised bootloader.");
    return paired[0];
  }
  onLog("Select the Cylon bootloader (WCH ISP) in the browser dialog — it may take a moment to appear.");
  return WchIsp.WebUsbTransport.request();
}

/** Erase, program, verify and reset. `transport` comes from connectBootloader. */
export async function programBootloader(transport, firmware, { onProgress, onLog }) {
  const WchIsp = window.WchIsp;
  try {
    const isp = new WchIsp.WchIspFlasher(transport);
    const info = await isp.connect(onProgress);
    onLog(`Connected: ${info.name}`);
    await isp.flash(firmware, { erase: true, verify: true, reset: true, progress: onProgress });
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
