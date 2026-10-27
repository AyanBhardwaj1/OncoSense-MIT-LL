// Talks to a real OncoSense Band over USB using the Web Serial API
// (Chrome / Edge on desktop). The band prints one JSON object per line in
// JSON mode, and `#` comment lines for human-readable messages.

export const BAUD_RATE = 115200;
const MAX_READ_ERRORS = 3;
const SHUTDOWN_STEP_MS = 1000;

// Resolve after `promise` settles or `ms` passes, whichever is first, so a
// stuck USB write can never hang a disconnect.
function settleWithin(promise, ms) {
  return Promise.race([
    Promise.resolve(promise).catch(() => {}),
    new Promise((resolve) => setTimeout(resolve, ms)),
  ]);
}

export class SerialDevice {
  static supported() {
    return typeof navigator !== "undefined" && "serial" in navigator;
  }

  constructor() {
    this.kind = "serial";
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.lineHandlers = [];
    this.closeHandlers = [];
    this.closed = false;
    this.onDisconnect = (e) => {
      if (e.target === this.port) this.shutdown("The band was unplugged.");
    };
  }

  onLine(fn) {
    this.lineHandlers.push(fn);
  }

  onClose(fn) {
    this.closeHandlers.push(fn);
  }

  // Must be called from a user gesture (a click): the browser shows its port picker.
  async connect() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: BAUD_RATE });
    navigator.serial.addEventListener("disconnect", this.onDisconnect);
    const encoder = new TextEncoderStream();
    this.writableClosed = encoder.readable.pipeTo(this.port.writable).catch(() => {});
    this.writer = encoder.writable.getWriter();
    this.readLoop();
    // JSON lines, then a status report so the app knows the device state.
    await this.send("j");
    await this.send("s");
  }

  // Hand each line to every handler. A bug in one handler (e.g. rendering an
  // unexpected message) must never take the USB connection down with it.
  deliver(line) {
    for (const fn of this.lineHandlers) {
      try {
        fn(line);
      } catch (err) {
        console.error("OncoSense: line handler failed", err, line);
      }
    }
  }

  async readLoop() {
    let buffer = "";
    let consecutiveErrors = 0;
    // Per the Web Serial spec, non-fatal errors (framing, buffer overrun)
    // end one reader; the port stays readable and we keep going. A port
    // that keeps failing is given up on rather than retried forever.
    while (this.port?.readable && !this.closed) {
      let gotData = false;
      const decoder = new TextDecoderStream();
      this.readableClosed = this.port.readable.pipeTo(decoder.writable).catch(() => {});
      this.reader = decoder.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          gotData = true;
          consecutiveErrors = 0;
          buffer += value;
          let nl;
          while ((nl = buffer.indexOf("\n")) >= 0) {
            const line = buffer.slice(0, nl).replace(/\r$/, "");
            buffer = buffer.slice(nl + 1);
            if (line) this.deliver(line);
          }
        }
      } catch (err) {
        if (this.closed) return;
        consecutiveErrors += 1;
        if (!this.port?.readable || consecutiveErrors >= MAX_READ_ERRORS) {
          await this.shutdown(`Connection lost: ${err.message}.`);
          return;
        }
      } finally {
        this.reader.releaseLock();
      }
      if (this.closed) return;
      if (!this.port?.readable) break;
      // A stream that ends without delivering anything counts as a failure,
      // so a port that keeps closing is given up on instead of spinning.
      if (!gotData && ++consecutiveErrors >= MAX_READ_ERRORS) break;
    }
    if (!this.closed) await this.shutdown("The band closed the connection.");
  }

  async send(text) {
    if (!this.writer || this.closed) throw new Error("Not connected");
    await this.writer.write(text);
  }

  // User-initiated disconnect.
  async close() {
    await this.shutdown(null);
  }

  // Release everything so the same port can be opened again, then notify once.
  async shutdown(reason) {
    if (this.closed) return;
    this.closed = true;
    navigator.serial.removeEventListener("disconnect", this.onDisconnect);
    // Each step is bounded: an errored reader or a writer the device never
    // drains (e.g. stuck in its fault loop) must not block the disconnect.
    await settleWithin(this.reader?.cancel(), SHUTDOWN_STEP_MS);
    await settleWithin(this.readableClosed, SHUTDOWN_STEP_MS);
    await settleWithin(this.writer?.close(), SHUTDOWN_STEP_MS);
    await settleWithin(this.writableClosed, SHUTDOWN_STEP_MS);
    try {
      await this.port?.close();
    } catch (err) {
      console.warn("OncoSense: could not close the port", err);
    }
    const handlers = this.closeHandlers;
    this.closeHandlers = [];
    handlers.forEach((fn) => fn(reason));
  }
}
