// Talks to a real OncoSense Band over USB using the Web Serial API
// (Chrome / Edge on desktop). The band prints one JSON object per line in
// JSON mode, and `#` comment lines for human-readable messages.

export const BAUD_RATE = 115200;

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
    this.closing = false;
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
    const encoder = new TextEncoderStream();
    this.writableClosed = encoder.readable.pipeTo(this.port.writable);
    this.writer = encoder.writable.getWriter();
    this.readLoop();
    navigator.serial.addEventListener("disconnect", (e) => {
      if (e.target === this.port) this.finish("The band was unplugged.");
    });
    // JSON lines, then a status report so the app knows the device state.
    await this.send("j");
    await this.send("s");
  }

  async readLoop() {
    const decoder = new TextDecoderStream();
    this.readableClosed = this.port.readable.pipeTo(decoder.writable).catch(() => {});
    this.reader = decoder.readable.getReader();
    let buffer = "";
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        buffer += value;
        let nl;
        while ((nl = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, nl).replace(/\r$/, "");
          buffer = buffer.slice(nl + 1);
          if (line) this.lineHandlers.forEach((fn) => fn(line));
        }
      }
    } catch (err) {
      if (!this.closing) this.finish(`Connection lost: ${err.message}`);
      return;
    }
    if (!this.closing) this.finish("The band closed the connection.");
  }

  async send(text) {
    if (!this.writer) throw new Error("Not connected");
    await this.writer.write(text);
  }

  async close() {
    this.closing = true;
    try {
      await this.reader?.cancel();
      await this.readableClosed;
      await this.writer?.close();
      await this.writableClosed;
      await this.port?.close();
    } catch {
      // Port already gone (unplugged); nothing left to release.
    }
    this.finish(null);
  }

  finish(reason) {
    const handlers = this.closeHandlers;
    this.closeHandlers = [];
    handlers.forEach((fn) => fn(reason));
  }
}
