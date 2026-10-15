import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import readline from "node:readline";

const serverDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(serverDir, "..");
const widgetUri = "ui://widget/seedance-video-panel-v1.html";
// PowerShell 5.1 misreads the UTF-8 skill scripts without a BOM and reports
// misleading parser errors. Prefer PowerShell 7, which is the runtime used by
// the installed CLI skills; keep a non-Windows fallback for local development.
const ps = process.platform === "win32" ? "pwsh.exe" : "pwsh";

const scripts = {
  dreamina: path.join(root, "tools", "dreamina_video_flow.ps1"),
  libtv: path.join(root, "tools", "libtv_video_flow.ps1"),
};

function json(value) { return JSON.stringify(value ?? {}); }

function parseControllerOutput(raw) {
  const text = String(raw ?? "").trim();
  if (!text) throw new Error("控制器没有返回结果");
  try { return JSON.parse(text); } catch {}
  const lines = text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  for (let i = lines.length - 1; i >= 0; i -= 1) {
    try { return JSON.parse(lines[i]); } catch {}
  }
  throw new Error(`控制器返回不是 JSON：${text.slice(-500)}`);
}

function runPowerShell(script, args) {
  return new Promise((resolve, reject) => {
    if (!existsSync(script)) return reject(new Error(`找不到控制器：${script}`));
    const child = spawn(ps, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, ...args], {
      cwd: root, windowsHide: true, env: { ...process.env },
    });
    let stdout = ""; let stderr = "";
    child.stdout.on("data", (chunk) => { stdout += chunk; });
    child.stderr.on("data", (chunk) => { stderr += chunk; });
    child.on("error", reject);
    child.on("close", (code) => {
      try { resolve({ code, value: parseControllerOutput(stdout), stderr }); }
      catch (error) { reject(new Error(`${error.message}${stderr ? `；${stderr.trim()}` : ""}`)); }
    });
  });
}

function arg(name, value) { return value === undefined || value === null || value === "" ? [] : [name, String(value)]; }

async function callController(provider, action, input = {}) {
  const script = scripts[provider];
  const args = ["-Action", action];
  if (provider === "dreamina") args.push("-Adapter", String(input.adapter ?? "canvas"));
  if (input.profile) args.push(...arg("-Profile", input.profile));
  if (input.projectId) args.push(...arg(provider === "dreamina" ? "-ProjectId" : "-Project", input.projectId));
  if (input.sessionId) args.push(...arg("-SessionId", input.sessionId));
  if (input.model) args.push(...arg("-Model", input.model));
  if (input.mode) args.push(...arg(provider === "dreamina" ? "-Mode" : "-ModeType", input.mode));
  if (input.prompt) args.push(...arg("-Prompt", input.prompt));
  if (input.parameters) args.push(...arg("-ParametersJson", json(input.parameters)));
  if (input.assets && provider === "dreamina") args.push(...arg("-AssetsJson", json(input.assets)));
  if (input.assetManifest) args.push(...arg("-AssetManifestJson", json(input.assetManifest)));
  if (input.assetRefs) args.push(...arg(provider === "dreamina" ? "-AssetsJson" : "-AssetRefsJson", json(input.assetRefs)));
  if (input.runId) args.push(...arg("-RunId", input.runId));
  if (input.taskId) args.push(...arg("-TaskId", input.taskId));
  if (input.submitId) args.push(...arg("-SubmitId", input.submitId));
  if (input.fresh) args.push("-Fresh");
  if (input.confirmed) args.push("-Confirmed");
  const result = await runPowerShell(script, args);
  return { ...result.value, controllerExitCode: result.code };
}

function toolResult(data, narration = "") {
  return {
    content: [{ type: "text", text: narration || `视频配置状态：${data?.status ?? "ready"}` }],
    structuredContent: data,
    _meta: {
      "ui/resourceUri": widgetUri,
      "openai/outputTemplate": widgetUri,
      "openai/widgetDescription": "Seedance 视频生成配置面板。所有选择完成后只提交一次。",
      widgetState: data,
    },
  };
}

const inputSchema = {
  type: "object",
  properties: {
    provider: { type: "string", enum: ["libtv", "dreamina"] },
    adapter: { type: "string", enum: ["canvas", "conversation"] },
    profile: { type: "string" }, projectId: { type: "string" }, sessionId: { type: "string" },
    prompt: { type: "string" }, model: { type: "string" }, mode: { type: "string" },
    parameters: { type: "object", additionalProperties: true },
    assets: { type: "array", items: { type: "object", additionalProperties: true } },
    assetManifest: { type: "array", items: { type: "object", additionalProperties: true } },
    assetRefs: { type: "array", items: { type: "object", additionalProperties: true } },
    runId: { type: "string" }, submitId: { type: "string" }, taskId: { type: "string" },
    fresh: { type: "boolean" }, confirmed: { type: "boolean" },
  },
  required: ["provider"], additionalProperties: false,
};

const tools = [
  { name: "prepare_video_config", title: "打开视频生成配置面板", description: "Use this when a Seedance prompt is ready and the user wants to configure LibTV or Dreamina video generation. Batch-load accounts, containers, assets, models, schemas and cached defaults before showing the side panel.", annotations: { readOnlyHint: true, destructiveHint: false, openWorldHint: false } },
  { name: "refresh_video_config", title: "刷新视频生成配置", description: "Use this when the user changes provider, account, project, session or model, or explicitly asks to refresh available choices.", annotations: { readOnlyHint: true, destructiveHint: false, openWorldHint: false } },
  { name: "validate_video_config", title: "预检视频生成配置", description: "Use this before showing final confirmation. Validate model parameters, asset bindings and capability compatibility without creating a paid generation task.", annotations: { readOnlyHint: true, destructiveHint: false, openWorldHint: false } },
  { name: "submit_video_config", title: "确认并生成视频", description: "Use this only after the user has reviewed the complete side-panel summary and explicitly confirmed. Submit exactly once and persist the original operationRef or submit_id.", annotations: { readOnlyHint: false, destructiveHint: false, openWorldHint: true, idempotentHint: true } },
  { name: "resume_video_config", title: "恢复视频任务", description: "Use this when the user says continue or asks for status. Query the original task identity and never create a new submission.", annotations: { readOnlyHint: true, destructiveHint: false, openWorldHint: false, idempotentHint: true } },
].map((item) => ({ ...item, inputSchema, _meta: { "ui/resourceUri": widgetUri, "openai/outputTemplate": widgetUri } }));

function send(message) { process.stdout.write(`${JSON.stringify(message)}\n`); }

async function handle(message) {
  if (message.method === "notifications/initialized") return;
  if (message.method === "initialize") {
        return send({ jsonrpc: "2.0", id: message.id, result: { protocolVersion: message.params?.protocolVersion ?? "2025-06-18", capabilities: { tools: {}, resources: {} }, serverInfo: { name: "seedance-video-panel", version: "0.1.25" } } });
  }
  if (message.method === "tools/list") return send({ jsonrpc: "2.0", id: message.id, result: { tools } });
  if (message.method === "resources/list") return send({ jsonrpc: "2.0", id: message.id, result: { resources: [{ uri: widgetUri, name: "Seedance 视频生成控制面板", mimeType: "text/html;profile=mcp-app" }] } });
  if (message.method === "resources/read") {
    if (message.params?.uri !== widgetUri) throw new Error("未知面板资源");
    const html = await readFile(path.join(serverDir, "widget.html"), "utf8");
    return send({ jsonrpc: "2.0", id: message.id, result: { contents: [{ uri: widgetUri, mimeType: "text/html;profile=mcp-app", text: html }] } });
  }
  if (message.method === "tools/call") {
    const name = message.params?.name; const input = message.params?.arguments ?? {}; const provider = input.provider ?? "dreamina";
    try {
      let data; let narration = "";
      if (name === "prepare_video_config" || name === "refresh_video_config") {
        data = await callController(provider, "prepare-video-config", { ...input, fresh: name === "refresh_video_config" || input.fresh });
      } else if (name === "validate_video_config") {
        data = await callController(provider, "validate-plan", input); narration = data.status === "valid" ? "配置预检通过，请检查摘要后确认。" : "配置预检未通过，请修正标红字段。";
      } else if (name === "submit_video_config") {
        if (input.confirmed !== true) throw new Error("提交前必须由用户明确确认");
        data = await callController(provider, "submit", input); narration = data.status === "submitted" ? "任务已提交，后续可在面板中查询原任务。" : "任务未提交，请查看错误并恢复。";
      } else if (name === "resume_video_config") {
        data = await callController(provider, "resume", input); narration = "已查询原任务身份，没有创建新任务。";
      } else throw new Error(`未知工具：${name}`);
      return send({ jsonrpc: "2.0", id: message.id, result: toolResult(data, narration) });
    } catch (error) {
      return send({ jsonrpc: "2.0", id: message.id, result: toolResult({ status: "error", message: error.message }, `面板控制器错误：${error.message}`) });
    }
  }
  if (message.id !== undefined) send({ jsonrpc: "2.0", id: message.id, error: { code: -32601, message: `未知 MCP 方法：${message.method}` } });
}

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on("line", (line) => { if (!line.trim()) return; Promise.resolve(handle(JSON.parse(line))).catch((error) => send({ jsonrpc: "2.0", id: null, error: { code: -32603, message: error.message } })); });

