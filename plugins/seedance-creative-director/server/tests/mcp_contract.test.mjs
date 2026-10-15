import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { spawn } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const serverPath = path.resolve(here, "..", "server.mjs");
const widgetPath = path.resolve(here, "..", "widget.html");

function frame(message) {
  return `${JSON.stringify(message)}\n`;
}

function nextMessage(buffer) {
  const lineEnd = buffer.indexOf("\n");
  if (lineEnd < 0) return null;
  return { message: JSON.parse(buffer.slice(0, lineEnd)), rest: buffer.slice(lineEnd + 1) };
}

const widget = await readFile(widgetPath, "utf8");
assert.match(widget, /确认并生成/);
assert.match(widget, /prepare_video_config/);
assert.match(widget, /submit_video_config/);

if (process.platform === "win32") {
  const { execFileSync } = await import("node:child_process");
  execFileSync("pwsh.exe", ["-NoProfile", "-Command", "$PSVersionTable.PSVersion.Major"], { encoding: "utf8" });
}

const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "pipe"] });
let buffer = "";
const messages = [];
child.stdout.on("data", (chunk) => { buffer += chunk.toString(); while (true) { const parsed = nextMessage(buffer); if (!parsed) break; messages.push(parsed.message); buffer = parsed.rest; } });

child.stdin.write(frame({ jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-06-18", capabilities: {}, clientInfo: { name: "contract-test", version: "1" } } }));
await new Promise((resolve, reject) => { const started = Date.now(); const timer = setInterval(() => { if (messages.some(m => m.id === 1)) { clearInterval(timer); resolve(); } else if (Date.now() - started > 5000) { clearInterval(timer); reject(new Error("initialize timeout")); } }, 20); });
child.stdin.write(frame({ jsonrpc: "2.0", method: "notifications/initialized", params: {} }));
child.stdin.write(frame({ jsonrpc: "2.0", id: 2, method: "tools/list", params: {} }));
await new Promise((resolve, reject) => { const started = Date.now(); const timer = setInterval(() => { if (messages.some(m => m.id === 2)) { clearInterval(timer); resolve(); } else if (Date.now() - started > 5000) { clearInterval(timer); reject(new Error("tools/list timeout")); } }, 20); });
const listed = messages.find(m => m.id === 2)?.result?.tools ?? [];
for (const name of ["prepare_video_config", "refresh_video_config", "validate_video_config", "submit_video_config", "resume_video_config"]) assert.ok(listed.some(t => t.name === name), `missing ${name}`);
child.kill();
console.log("MCP panel contract passed");
