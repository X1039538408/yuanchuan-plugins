import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/panel.html', import.meta.url), 'utf8');
const code = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const session = () => ({
  sessionId: 'session-1', version: '1.0.0',
  config: { direction: 'landscape', templateId: 'landscape', template: null, detection: 'auto', fit: 'cover', zoom: 100, rect: null, resolution: '1080p', quality: 'high', backend: 'auto', outputDir: '' },
  videos: [{ path: 'D:\\素材\\测试.mp4', name: '测试.mp4', info: { width: 480, height: 720, duration: 2 } }],
  templates: [{ id: 'landscape', name: '横版' }, { id: 'portrait', name: '竖版' }],
  presets: [{ name: '横版共享', config: { direction: 'landscape' }, scope: 'shared' }], outputSize: [1920, 1080], rect: [100, 0, 720, 1080],
});
function harness(handle = () => session()) {
  const classes = () => { const set = new Set(); return { add: v => set.add(v), remove: v => set.delete(v), toggle: (v, force) => force ? set.add(v) : set.delete(v), contains: v => set.has(v) }; };
  const element = () => ({ value: '', textContent: '', disabled: false, children: [], classList: classes(), setAttribute() {}, append(...xs) { this.children.push(...xs); }, prepend(...xs) { this.children.unshift(...xs); }, replaceChildren(...xs) { this.children = xs; }, get options() { return this.children; } });
  const elements = Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m => [m[1], element()]));
  elements.seconds.value = '0'; elements.videoIndex.value = '0';
  const listeners = {}; const calls = []; let context;
  const parent = { postMessage(message) {
    calls.push(message);
    if (message.id === undefined) return;
    queueMicrotask(async () => {
      try {
        const result = message.method === 'tools/call' ? { structuredContent: await handle(message.params.name, message.params.arguments) } : { protocolVersion: '2026-01-26', hostContext: {} };
        listeners.message({ source: parent, data: { jsonrpc: '2.0', id: message.id, result } });
      } catch (error) { listeners.message({ source: parent, data: { jsonrpc: '2.0', id: message.id, error: { message: error.message } } }); }
    });
  } };
  context = vm.createContext({ window: { parent, addEventListener: (name, fn) => listeners[name] = fn }, document: { getElementById: id => elements[id], createElement: element, documentElement: { dataset: {} } }, setTimeout, clearTimeout, console, crypto: { randomUUID: () => 'request-1' } });
  vm.runInContext(code, context);
  return { context, elements, calls, notify: result => listeners.message({ source: parent, data: { jsonrpc: '2.0', method: 'ui/notifications/tool-result', params: { structuredContent: result } } }), spoof: result => listeners.message({ source: {}, data: { jsonrpc: '2.0', method: 'ui/notifications/tool-result', params: { structuredContent: result } } }), run: expression => vm.runInContext(expression, context) };
}
const tick = () => new Promise(resolve => setImmediate(resolve));

test('initializes MCP Apps bridge, accepts host session and ignores other frames', async () => {
  const app = harness(); await tick();
  assert.equal(app.calls[0].method, 'ui/initialize');
  assert.equal(app.calls[1].method, 'ui/notifications/initialized');
  app.notify(session());
  assert.equal(app.elements.exportBtn.disabled, false);
  assert.equal(app.elements.count.textContent, '01');
  assert.equal(app.elements.presets.children[1].textContent, '横版共享 · 共享');
  app.spoof({ ...session(), version: 'spoofed' });
  assert.equal(app.elements.version.textContent, 'v1.0.0');
});

test('preview saves controls first, calls video frame tool and displays returned data URI', async () => {
  const app = harness((name, args) => name === 'preview_frame' ? { sessionId: args.sessionId, previewDataUrl: 'data:image/jpeg;base64,YQ==' } : { ...session(), config: { ...session().config, ...args.config } });
  await tick(); app.notify(session()); app.elements.fit.value = 'zoom'; app.elements.zoom.value = '145'; app.elements.seconds.value = '1.2';
  await app.elements.previewBtn.onclick();
  const tools = app.calls.filter(c => c.method === 'tools/call');
  assert.deepEqual(tools.map(c => c.params.name), ['update_config', 'preview_frame']);
  assert.equal(tools[0].params.arguments.config.zoom, 145);
  assert.equal(tools[1].params.arguments.seconds, 1.2);
  assert.equal(app.elements.previewImage.src, 'data:image/jpeg;base64,YQ==');
  assert.equal(app.elements.placeholder.classList.contains('hidden'), true);
});

test('resolved builtin paths and detected rectangle retain automatic controls', async () => {
  const app = harness(); await tick();
  const data = session();
  data.config.template = 'D:/runtime/assets/default.png';
  data.templates[0].path = data.config.template;
  data.config.rect = [200, 0, 800, 1080];
  data.rectSource = 'auto';
  app.notify(data);
  assert.equal(app.elements.templateId.value, 'landscape');
  assert.equal(app.elements.manualRect.checked, false);
  assert.equal(app.run('readConfig().rect'), null);
  assert.equal(app.run('readConfig().template'), null);
  data.rectSource = 'manual'; app.notify(data);
  assert.equal(app.elements.manualRect.checked, true);
  assert.deepEqual(Array.from(app.run('readConfig().rect')), [200, 0, 800, 1080]);
});

test('export request deduplication survives response error and uses same request ID on retry', async () => {
  let attempts = 0;
  const app = harness((name, args) => {
    if (name !== 'export_videos') return { ...session(), config: { ...session().config, ...args.config } };
    if (++attempts === 1) throw new Error('temporary connection error');
    return { taskId: 'task-1', sessionId: 'session-1', config: session().config, outputSize: [1920, 1080], status: 'completed', progress: 1, version: '1.0.0', items: [{ path: session().videos[0].path, status: 'completed', progress: 1, output: 'D:\\output.mp4' }] };
  });
  await tick(); app.notify(session());
  await app.elements.exportBtn.onclick(); assert.match(app.elements.notice.textContent, /temporary/);
  await app.elements.exportBtn.onclick();
  const exports = app.calls.filter(c => c.params?.name === 'export_videos');
  assert.equal(exports.length, 2); assert.equal(exports[0].params.arguments.requestId, exports[1].params.arguments.requestId);
  assert.equal(app.elements.progress.value, 100);
  assert.equal(app.run('state.session.videos.length'), 1);
  assert.equal(app.elements.openBtn.classList.contains('hidden'), false);
});

test('validates template coordinates and zoom before sending tools, preserves plain text errors', async () => {
  const app = harness(); await tick(); app.notify(session());
  app.elements.zoom.value = '501'; await app.elements.previewBtn.onclick();
  assert.equal(app.calls.filter(c => c.method === 'tools/call').length, 0);
  assert.match(app.elements.notice.textContent, /10%～500%/);
  app.elements.zoom.value = '100'; app.elements.manualRect.checked = true;
  app.elements.rectW.value = '0'; await app.elements.previewBtn.onclick();
  assert.match(app.elements.notice.textContent, /正整数/);
  app.notify({ taskId: 'task-1', status: 'failed', progress: .42, version: '1.0.0', items: [{ path: session().videos[0].path, status: 'failed', progress: .42, error: '<img src=x onerror=alert(1)>' }] });
  assert.equal(app.elements.progress.value, 42);
  assert.equal(app.elements.queue.children[0].children[1].children[1].textContent, '<img src=x onerror=alert(1)>');
});
