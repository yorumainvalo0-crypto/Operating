/*
 * Deploy agent widget: renders an "Update" button that pulls the latest
 * GitHub version onto the server.
 *
 * Usage:
 *   <script src="/deploy/widget.js" data-project="my-site" defer></script>
 * Optional attributes:
 *   data-target="#some-element"  render inside this element instead of floating
 *   data-endpoint="/deploy"       agent base URL (default: derived from src)
 */
(function () {
  "use strict";

  var script = document.currentScript;
  if (!script) return;

  var project = script.dataset.project;
  var endpoint = (script.dataset.endpoint || script.src.replace(/\/widget\.js(\?.*)?$/, ""))
    .replace(/\/$/, "");
  var tokenKey = "deploy-agent-token";
  var pollTimer = null;

  if (!project) {
    console.error("[deploy-agent] data-project attribute is missing");
    return;
  }

  // --- token storage -------------------------------------------------------

  function getToken() {
    try { return localStorage.getItem(tokenKey); } catch (e) { return null; }
  }

  function setToken(value) {
    try {
      if (value) localStorage.setItem(tokenKey, value);
      else localStorage.removeItem(tokenKey);
    } catch (e) { /* storage unavailable: token is asked again next time */ }
  }

  function askToken() {
    var value = window.prompt("Deploy admin token:");
    if (value) setToken(value.trim());
    return value ? value.trim() : null;
  }

  // --- UI ------------------------------------------------------------------

  var host = document.createElement("div");
  var root = host.attachShadow({ mode: "open" });
  root.innerHTML =
    "<style>" +
    ":host{all:initial}" +
    ".box{font:13px/1.4 system-ui,sans-serif;background:#fff;color:#1b1f24;border:1px solid #d0d7de;" +
    "border-radius:8px;padding:10px 12px;box-shadow:0 4px 16px rgba(0,0,0,.12);width:280px;box-sizing:border-box}" +
    ".floating{position:fixed;right:16px;bottom:16px;z-index:2147483647}" +
    ".row{display:flex;gap:6px;align-items:center}" +
    ".title{font-weight:600;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}" +
    "button{font:inherit;cursor:pointer;border:1px solid #d0d7de;border-radius:6px;padding:4px 10px;background:#f6f8fa;color:inherit}" +
    "button.primary{background:#1f883d;border-color:#1f883d;color:#fff}" +
    "button:disabled{opacity:.6;cursor:default}" +
    ".commit,.state{margin-top:6px;color:#57606a;word-break:break-word}" +
    ".state.ok{color:#1a7f37}.state.err{color:#cf222e}" +
    "pre{max-height:200px;overflow:auto;background:#f6f8fa;padding:6px;border-radius:6px;font-size:11px;white-space:pre-wrap;margin:6px 0 0}" +
    "details summary{cursor:pointer;margin-top:6px;color:#57606a}" +
    "@media (prefers-color-scheme:dark){.box{background:#161b22;color:#e6edf3;border-color:#30363d}" +
    "button{background:#21262d;border-color:#30363d}pre{background:#0d1117}.commit,.state,details summary{color:#8d96a0}}" +
    "</style>" +
    "<div class='box'>" +
    "<div class='row'><span class='title'></span>" +
    "<button class='refresh' title='Refresh status'>&#8635;</button>" +
    "<button class='primary update'>Update</button></div>" +
    "<div class='commit'></div><div class='state'></div>" +
    "<details><summary>Log</summary><pre></pre></details>" +
    "</div>";

  var box = root.querySelector(".box");
  var titleEl = root.querySelector(".title");
  var commitEl = root.querySelector(".commit");
  var stateEl = root.querySelector(".state");
  var logEl = root.querySelector("pre");
  var updateBtn = root.querySelector(".update");
  var refreshBtn = root.querySelector(".refresh");

  titleEl.textContent = project;
  commitEl.textContent = "Click ↻ to load status.";

  function mount() {
    var target = script.dataset.target && document.querySelector(script.dataset.target);
    if (target) {
      target.appendChild(host);
    } else {
      box.classList.add("floating");
      document.body.appendChild(host);
    }
  }

  function setState(text, kind) {
    stateEl.textContent = text;
    stateEl.className = "state" + (kind ? " " + kind : "");
  }

  function render(status) {
    var c = status.commit;
    commitEl.textContent = c
      ? c.sha.slice(0, 7) + " – " + c.message + " (" + new Date(c.date).toLocaleString() + ")"
      : "Not deployed yet.";

    var run = status.last_run;
    if (status.running) {
      setState("Updating…" + (status.pending ? " (another update queued)" : ""));
    } else if (run) {
      var labels = { success: "Updated", "up-to-date": "Already up to date", failed: "Update failed" };
      var when = run.finished_at ? new Date(run.finished_at).toLocaleString() : "";
      setState((labels[run.state] || run.state) + " – " + when + " (" + run.trigger + ")",
        run.state === "failed" ? "err" : "ok");
    } else {
      setState(status.auto_update ? "Auto-update on push is enabled." : "");
    }
    logEl.textContent = run ? run.log : "";
    updateBtn.disabled = status.running;
  }

  // --- API -----------------------------------------------------------------

  function request(method, path, retried) {
    var token = getToken() || askToken();
    if (!token) return Promise.reject(new Error("No token entered."));
    return fetch(endpoint + "/" + encodeURIComponent(project) + path, {
      method: method,
      headers: { Authorization: "Bearer " + token },
      cache: "no-store"
    }).then(function (res) {
      if (res.status === 401 && !retried) {
        setToken(null);
        return request(method, path, true);
      }
      return res.json().then(function (body) {
        if (!res.ok) throw new Error(body.error || "HTTP " + res.status);
        return body;
      });
    });
  }

  function loadStatus() {
    return request("GET", "/status").then(function (status) {
      render(status);
      clearTimeout(pollTimer);
      if (status.running) pollTimer = setTimeout(loadStatus, 2000);
      return status;
    }).catch(function (err) {
      setState(err.message, "err");
    });
  }

  updateBtn.addEventListener("click", function () {
    updateBtn.disabled = true;
    setState("Starting update…");
    request("POST", "/update").then(function () {
      return loadStatus();
    }).catch(function (err) {
      setState(err.message, "err");
      updateBtn.disabled = false;
    });
  });

  refreshBtn.addEventListener("click", loadStatus);

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", mount);
  } else {
    mount();
  }
  if (getToken()) {
    // Already logged in: show the current status right away.
    setTimeout(loadStatus, 0);
  }
})();
