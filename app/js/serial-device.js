// Talks to a real OncoSense Band over USB using the Web Serial API
// (Chrome / Edge on desktop). The band prints one JSON object per line in
// JSON mode, and `#` comment lines for human-readable messages.

export const BAUD_RATE = 115200;
const MAX_READ_ERRORS = 3;

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
      const decoder = new TextDecoderStream();
      this.readableClosed = this.port.readable.pipeTo(decoder.writable).catch(() => {});
      this.reader = decoder.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
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
    try {
      await this.reader?.cancel();
    } catch {
      // Reader already errored or released; nothing to cancel.
    }
    await this.readableClosed;
    try {
      await this.writer?.close();
    } catch {
      // Writable already errored (unplugged); the port close below still runs.
    }
    await this.writableClosed;
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
