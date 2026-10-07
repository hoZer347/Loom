// Runs the browser suite in headless Chrome and exits with its result.
//
//   node run.mjs <folder holding index.html and tests.js> <chrome or edge exe>
//
// Serves the folder with the cross-origin isolation headers a pthreads build
// needs, drives Chrome over the DevTools protocol, and answers the input a
// case asks for (Module.loomRequest) with real events through Chrome's input
// pipeline, the same path a person's mouse and keyboard take. The browser is
// headless with a profile of its own, so nothing appears on screen.

import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { extname, join, normalize, resolve } from "node:path";

const folder = resolve(process.argv[2]);
const browserPath = process.argv[3];

const POLL_MS = 50;
const TIMEOUT_MS = 180000;

const CONTENT_TYPES = {
  ".html": "text/html",
  ".js": "text/javascript",
  ".wasm": "application/wasm",
};

const BUTTONS = { left: "left", right: "right", middle: "middle" };

const KEYS = {
  Space: { key: " ", code: "Space", keyCode: 32, text: " " },
};

const server = createServer(async (request, response) => {
  const path = normalize(join(folder, decodeURIComponent(new URL(request.url, "http://x").pathname)));

  try {
    if (!path.startsWith(folder)) throw new Error("outside the folder");

    const body = await readFile(path);

    response.writeHead(200, {
      "Content-Type": CONTENT_TYPES[extname(path)] ?? "application/octet-stream",
      "Cross-Origin-Opener-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
    });
    response.end(body);
  } catch {
    response.writeHead(404);
    response.end();
  }
});

await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));

const browser = spawn(browserPath, [
  "--headless=new",
  `--user-data-dir=${join(folder, "profile")}`,
  "--remote-debugging-port=0",
  "--enable-unsafe-swiftshader",
  "--use-angle=swiftshader",
  "--no-first-run",
  "--window-size=800,600",
  "about:blank",
]);

function finish(code) {
  browser.kill();
  server.close();
  process.exit(code);
}

setTimeout(() => {
  console.error("The browser suite did not finish in time");
  finish(1);
}, TIMEOUT_MS);

// Chrome names the port it took on stderr.
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
  } else if (message.method === "Runtime.consoleAPICalled") {
    console.log(message.params.args.map((arg) => arg.value ?? arg.description).join(" "));
  } else if (message.method === "Runtime.exceptionThrown") {
    const details = message.params.exceptionDetails;
    console.error(details.exception?.description ?? details.text);
  }
};

async function evaluate(expression) {
  return (await send("Runtime.evaluate", { expression, returnByValue: true })).result.value;
}

let pointer = { x: 0, y: 0 };

async function perform(action) {
  const [verb, argument] = action.split(":");

  if (verb === "move") {
    const [x, y] = argument.split(",").map(Number);
    pointer = { x, y };
    await send("Input.dispatchMouseEvent", { type: "mouseMoved", ...pointer });
  } else if (verb === "press" || verb === "release") {
    await send("Input.dispatchMouseEvent", {
      type: verb === "press" ? "mousePressed" : "mouseReleased",
      button: BUTTONS[argument],
      clickCount: 1,
      ...pointer,
    });
  } else if (verb === "keydown" || verb === "keyup") {
    const key = KEYS[argument];
    await send("Input.dispatchKeyEvent", {
      type: verb === "keydown" ? "keyDown" : "keyUp",
      key: key.key,
      code: key.code,
      windowsVirtualKeyCode: key.keyCode,
      nativeVirtualKeyCode: key.keyCode,
      text: verb === "keydown" ? key.text : undefined,
    });
  } else {
    console.error(`run.mjs: no such input as '${action}'`);
  }
}

await send("Runtime.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${server.address().port}/index.html` });

for (;;) {
  await new Promise((resolve) => setTimeout(resolve, POLL_MS));

  const [request, exit] = (await evaluate(
    "typeof Module === 'undefined' ? [null, null] : [Module.loomRequest, window.loomExit ?? null]"
  )) ?? [null, null];

  if (exit !== null) finish(typeof exit === "number" ? exit : 1);

  // Cleared first: a case asks for the next input as soon as it sees this one land.
  if (request) {
    await evaluate("Module.loomRequest = null");
    await perform(request);
  }
}
