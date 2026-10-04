/*
 * CylonMidi — small Web MIDI client for the Cylon USB-MIDI SysEx protocol.
 *
 *   const cylon = new CylonMidi();
 *   await cylon.connect();
 *   cylon.use(cylon.pick("cylon"));
 *   cylon.useInput(cylon.pickInput("cylon"));
 *   cylon.setLed(0, 255, 0, 0);
 *   cylon.requestVersion(([major, minor, patch]) => console.log(major, minor, patch));
 *
 * Protocol: F0 13 37 <record> ... F7, record = <address> <b1> <b2> <b3>.
 * All payload bytes are 7-bit. See docs/PROTOCOL.md.
 */

export class CylonMidi {
  static MANUFACTURER_ID = [0x13, 0x37];
  static ADDRESS_ALL = 0x00; // broadcast to every LED
  static LED_ADDRESS = [0x01, 0x02, 0x03]; // LED 0 / 1 / 2
  static VERSION_ADDRESS = 0x0b; // firmware version reply (device -> host)
  static CMD_ENTER_BOOTLOADER = [0x7f, 0x01, 0x00, 0x00];
  static CMD_GET_VERSION = [0x7f, 0x02, 0x00, 0x00];
  static ANIM = {
    BLINK_1HZ: 0x04,
    BLINK_2HZ: 0x05,
    BREATHE_1HZ: 0x06,
    BREATHE_2HZ: 0x07,
    LARSON: 0x08,
    WHEEL_OFFSET: 0x09,
    WHEEL_UNIFORM: 0x0a,
  };
  static PIN_ADDRESS = [0x10, 0x11, 0x12, 0x13]; // PA0..PA3
  static PIN_PUSH_PULL = 0x00;
  static PIN_PWM = 0x01;
  static PIN_SERVO = 0x02;
  static SYSEX_START = 0xf0;
  static SYSEX_END = 0xf7;

  constructor() {
    this.access = null;
    this.output = null;
    this.input = null;
    this._versionCallback = null;
  }

  /** 8-bit value -> 7-bit MIDI data byte (the firmware shifts it back up). */
  static scale7(value) {
    return (value & 0xff) >> 1;
  }

  /** Ask the browser for MIDI access. Must be called from a user gesture. */
  async connect() {
    if (!navigator.requestMIDIAccess) {
      throw new Error("Web MIDI is not available — use Chrome, Edge or another Chromium browser.");
    }
    this.access = await navigator.requestMIDIAccess({ sysex: true });
    return this.outputs();
  }

  /** All MIDI output ports the browser can see. */
  outputs() {
    return this.access ? Array.from(this.access.outputs.values()) : [];
  }

  /** All MIDI input ports the browser can see. */
  inputs() {
    return this.access ? Array.from(this.access.inputs.values()) : [];
  }

  /** Pick an output by name substring (default "cylon"), else the first one. */
  pick(hint = "cylon") {
    const ports = this.outputs();
    if (!ports.length) return null;
    const needle = hint.toLowerCase();
    return ports.find((p) => p.name.toLowerCase().includes(needle)) || ports[0];
  }

  /** Pick an input by name substring (default "cylon"), else the first one. */
  pickInput(hint = "cylon") {
    const ports = this.inputs();
    if (!ports.length) return null;
    const needle = hint.toLowerCase();
    return ports.find((p) => p.name.toLowerCase().includes(needle)) || ports[0];
  }

  /** Select the output to send to. Pass null to disconnect. */
  use(output) {
    this.output = output || null;
    return this.output;
  }

  /** Select the input used to receive device replies. Pass null to detach. */
  useInput(input) {
    if (this.input) this.input.onmidimessage = null;
    this.input = input || null;
    if (this.input) this.input.onmidimessage = (event) => this._handleMessage(event);
    return this.input;
  }

  get connected() {
    return !!this.output;
  }

  /** Set one LED (index 0..2) from 8-bit RGB components. */
  setLed(index, r, g, b) {
    if (index < 0 || index >= CylonMidi.LED_ADDRESS.length) {
      throw new Error(`Invalid LED index: ${index}`);
    }
    this.send([CylonMidi.LED_ADDRESS[index], CylonMidi.scale7(r), CylonMidi.scale7(g), CylonMidi.scale7(b)]);
  }

  /** Set all three LEDs to the same 8-bit RGB colour (one broadcast message). */
  setAll(r, g, b) {
    this.send([CylonMidi.ADDRESS_ALL, CylonMidi.scale7(r), CylonMidi.scale7(g), CylonMidi.scale7(b)]);
  }

  /** Start an animation (address 0x04..0x0A) with a base colour. */
  animate(address, r, g, b) {
    this.send([address, CylonMidi.scale7(r), CylonMidi.scale7(g), CylonMidi.scale7(b)]);
  }

  /** Drive an auxiliary pin (index 0..3 = PA0..PA3) high or low. */
  setPin(index, high) {
    if (index < 0 || index >= CylonMidi.PIN_ADDRESS.length) {
      throw new Error(`Invalid pin index: ${index}`);
    }
    this.send([CylonMidi.PIN_ADDRESS[index], CylonMidi.PIN_PUSH_PULL, high ? 0x01 : 0x00, 0x00]);
  }

  /** Set an auxiliary pin's PWM duty; level is 0..127 (0..100%). */
  setPinPwm(index, level) {
    if (index < 0 || index >= CylonMidi.PIN_ADDRESS.length) {
      throw new Error(`Invalid pin index: ${index}`);
    }
    const duty = Math.max(0, Math.min(0x7f, Math.round(level)));
    this.send([CylonMidi.PIN_ADDRESS[index], CylonMidi.PIN_PWM, duty, 0x00]);
  }

  /**
   * Drive an auxiliary pin as a servo.
   * @param {number} index 0..3 (PA0..PA3)
   * @param {number} position 0..127 = 0..100%
   * @param {number} speed move time in 100 ms units; 0 = instant
   */
  setServo(index, position, speed = 0) {
    if (index < 0 || index >= CylonMidi.PIN_ADDRESS.length) {
      throw new Error(`Invalid pin index: ${index}`);
    }
    const pos = Math.max(0, Math.min(0x7f, Math.round(position)));
    const move = Math.max(0, Math.min(0x7f, Math.round(speed)));
    this.send([CylonMidi.PIN_ADDRESS[index], CylonMidi.PIN_SERVO, pos, move]);
  }

  /**
   * Set the global servo pulse range, in microseconds. Narrow it if a servo
   * reaches its mechanical limit before 100 % (it buzzes against the end stop).
   */
  setServoRange(minUs, maxUs) {
    const units = (us) => Math.max(0, Math.min(0x7f, Math.round(us / 20)));
    this.send([0x7f, 0x10, units(minUs), units(maxUs)]);
  }

  /** Reboot the board into the WCH ROM USB bootloader (for reflashing). */
  rebootToBootloader() {
    this.send(CylonMidi.CMD_ENTER_BOOTLOADER);
  }

  /** Ask for the firmware version; `callback([major, minor, patch])` runs on reply. */
  requestVersion(callback) {
    this._versionCallback = callback || null;
    this.send(CylonMidi.CMD_GET_VERSION);
  }

  /** Handle a device -> host message; recognises the version reply record. */
  _handleMessage(event) {
    const data = Array.from(event.data);
    for (let i = 0; i + 5 < data.length; i++) {
      if (
        data[i] === CylonMidi.MANUFACTURER_ID[0] &&
        data[i + 1] === CylonMidi.MANUFACTURER_ID[1] &&
        data[i + 2] === CylonMidi.VERSION_ADDRESS
      ) {
        const version = data.slice(i + 3, i + 6);
        if (this._versionCallback) this._versionCallback(version);
        return;
      }
    }
  }

  /** Wrap a payload in the F0 … F7 SysEx frame and send it. */
  send(payload) {
    if (!this.output) throw new Error("No MIDI output selected.");
    const message = Uint8Array.from([
      CylonMidi.SYSEX_START,
      ...CylonMidi.MANUFACTURER_ID,
      ...payload,
      CylonMidi.SYSEX_END,
    ]);
    this.output.send(message);
    return message;
  }
}

export default CylonMidi;
