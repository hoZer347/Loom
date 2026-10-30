// Checks Player.html's GPU notice in headless Chrome and exits with the result.
//
//   node player.mjs <folder for the browser's profile> <chrome or edge exe>
//
// Serves the page with its placeholders filled as play-web.ps1 fills them,
// but no engine, and calls Module.loomCheckGpu as a ComputeShader would, with
// the browser's WebGL answer and its user agent set for each case.

import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const folder = resolve(process.argv[2]);
const browserPath = process.argv[3];

const PLAYER = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "Loom Web", "Player.html");
const TIMEOUT_MS = 60000;
const COPY_SETTLE_MS = 500;
const FIXED_RATE = "120";

const AGENTS = {
  chrome: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  edge: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0",
  firefox: "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:131.0) Gecko/20100101 Firefox/131.0",
  other: "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15",
};

const page = (await readFile(PLAYER, "utf8"))
  .replace("{TITLE}", "Player")
  .replace("{BASE}", "/")
  .replaceAll("{SCRIPT}", "player.js")
  .replace("{SCENE}", "scene.loomscene")
  .replace("{FILES}", "[]")
  .replace("{INPUT}", "")
  .replace("{FIXED_RATE}", FIXED_RATE);

const server = createServer((request, response) => {
  if (new URL(request.url, "http://x").pathname !== "/index.html") {
    response.writeHead(404);
    response.end();
    return;
  }

  response.writeHead(200, { "Content-Type": "text/html" });
  response.end(page);
});

await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

const browser = spawn(browserPath, [
  "--headless=new",
  `--user-data-dir=${join(folder, "player-profile")}`,
  "--remote-debugging-port=0",
  "--no-first-run",
  "about:blank",
]);

function finish(code) {
  browser.kill();
  server.close();
  process.exit(code);
}

setTimeout(() => {
  console.error("The player checks did not finish in time");
  finish(1);
}, TIMEOUT_MS);

const devtools = await new Promise((resolve) => {
  let said = "";

  browser.stderr.on("data", (chunk) => {
    said += chunk;
    const found = said.match(/DevTools listening on (ws:\/\/[^\s]+)/);
    if (found) resolve(new URL(found[1]));
  });
});

const targets = await (await fetch(`http://${devtools.host}/json/list`)).json();
const socket = new WebSocket(targets.find((target) => target.type === "page").webSocketDebuggerUrl);
await new Promise((resolve) => (socket.onopen = resolve));

let nextId = 0;
const pending = new Map();

function send(method, params = {}) {
  return new Promise((resolve) => {
    pending.set(++nextId, resolve);
    socket.send(JSON.stringify({ id: nextId, method, params }));
  });
}

socket.onmessage = (event) => {
  const message = JSON.parse(event.data);

  if (pending.has(message.id)) {
    pending.get(message.id)(message.result);
    pending.delete(message.id);
  }
};

async function evaluate(expression) {
  return (await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true, userGesture: true })).result.value;
}

// The notice after the page loads afresh as agent, with a context that fails
// failIfMajorPerformanceCaveat when software is true, and drawable as given.
async function check(agent, software, drawable) {
  await send("Emulation.setUserAgentOverride", { userAgent: AGENTS[agent] });
  await send("Page.navigate", { url: `${origin}/index.html` });

  while (!(await evaluate("document.readyState === 'complete' && typeof Module === 'object'")))
    await new Promise((resolve) => setTimeout(resolve, 50));

  return evaluate(`(() => {
    const get = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (type, options) {
      return options && options.failIfMajorPerformanceCaveat ? (${software} ? null : {}) : get.call(this, type, options);
    };
    Module.loomCheckGpu(${drawable});
    return {
      hidden: document.getElementById("gpu-notice").hidden,
      title: document.getElementById("gpu-title").textContent,
      address: document.getElementById("gpu-address").hidden ? "" : document.querySelector("#gpu-address code").textContent,
      steps: document.querySelectorAll("#gpu-steps li").length,
    };
  })()`);
}

const failures = [];

function expect(name, actual, expected) {
  for (const [key, value] of Object.entries(expected))
    if (actual[key] !== value) failures.push(`${name}: ${key} was ${JSON.stringify(actual[key])}, not ${JSON.stringify(value)}`);
}

await send("Page.enable");
await send("Emulation.setFocusEmulationEnabled", { enabled: true });
await send("Browser.grantPermissions", { origin, permissions: ["clipboardReadWrite", "clipboardSanitizedWrite"] });

expect("a GPU that runs it", await check("chrome", false, true), { hidden: true });
expect("no float targets", await check("chrome", false, false), { hidden: false, title: "This GPU cannot run compute shaders", address: "", steps: 0 });
expect("Edge", await check("edge", true, true), { hidden: false, title: "Hardware acceleration is off", address: "edge://settings/system", steps: 3 });
expect("Firefox", await check("firefox", true, true), { hidden: false, address: "about:preferences#general", steps: 4 });
expect("another browser", await check("other", true, true), { hidden: false, address: "", steps: 2 });
expect("Chrome", await check("chrome", true, true), { hidden: false, address: "chrome://settings/system", steps: 3 });

expect("the fixed rate", await evaluate("({ rate: Module.arguments[1] })"), { rate: FIXED_RATE });

await evaluate("document.getElementById('gpu-copy').click()");
await new Promise((resolve) => setTimeout(resolve, COPY_SETTLE_MS));
expect("Copy", await evaluate("navigator.clipboard.readText().then((text) => ({ text, button: document.getElementById('gpu-copy').textContent }))"), { text: "chrome://settings/system", button: "Copied" });

for (const failure of failures) console.error(failure);
console.log(`Player notice: ${failures.length ? `${failures.length} failed` : "all passed"}`);
finish(failures.length ? 1 : 0);
