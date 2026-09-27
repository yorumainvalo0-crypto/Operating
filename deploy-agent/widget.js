/*
 * Deploy agent widget: update button for any website.
 *
 * Usage (same line for every website):
 *   <script src="https://deploy.example.com/widget.js" defer></script>
 *
 * Visitors see nothing. Open the website with "#deploy" at the end of the URL
 * once and enter the admin token; from then on the box is shown in that browser.
 * The website is matched by its domain (see "domains" in the agent config).
 *
 * Optional attributes:
 *   data-project="name"   show this project instead of matching by domain
 *   data-target="#el"     render inside this element instead of floating
 *   data-all              show all projects and always show the box (dashboard)
 */
(function () {
  "use strict";

  var script = document.currentScript;
  if (!script) return;

  var endpoint = script.src.replace(/\/widget\.js(\?.*)?$/, "");
  var tokenKey = "deploy-agent-token";
  var showAll = script.hasAttribute("data-all");
  var forcedProject = script.dataset.project || null;
  var rows = {};

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

  function wantsLogin() {
    return showAll || /(^#|[#&?])deploy\b/.test(location.hash + location.search);
  }

  // --- API -----------------------------------------------------------------

  function api(method, path) {
    return fetch(endpoint + path, {
      method: method,
      headers: { Authorization: "Bearer " + getToken() },
      cache: "no-store"
    }).then(function (res) {
      return res.json().catch(function () { return {}; }).then(function (body) {
        if (res.status === 401) {
          setToken(null);
          throw new Error("Token invalid");
        }
        if (!res.ok) throw new Error(body.error || "HTTP " + res.status);
        return body;
      });
    });
  }

  // --- UI ------------------------------------------------------------------

  var host = document.createElement("div");
  var root = host.attachShadow({ mode: "open" });
  root.innerHTML =
    "<style>" +
    ":host{all:initial}" +
    ".box{font:13px/1.4 system-ui,sans-serif;background:#fff;color:#1b1f24;border:1px solid #d0d7de;" +
    "border-radius:8px;padding:10px 12px;box-shadow:0 4px 16px rgba(0,0,0,.12);box-sizing:border-box}" +
    ".floating{position:fixed;right:16px;bottom:16px;z-index:2147483647;width:300px;max-width:calc(100vw - 32px)}" +
    ".head{display:flex;gap:6px;align-items:center;font-weight:600;margin-bottom:4px}" +
    ".head span{flex:1}" +
    ".proj{border-top:1px solid #d0d7de;padding-top:8px;margin-top:8px}" +
    ".row{display:flex;gap:6px;align-items:center}" +
    ".name{font-weight:600;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}" +
    "button{font:inherit;cursor:pointer;border:1px solid #d0d7de;border-radius:6px;padding:3px 9px;background:#f6f8fa;color:inherit}" +
    "button.primary{background:#1f883d;border-color:#1f883d;color:#fff}" +
    "button:disabled{opacity:.6;cursor:default}" +
    "input{font:inherit;flex:1;min-width:0;padding:4px 6px;border:1px solid #d0d7de;border-radius:6px;background:inherit;color:inherit}" +
    ".muted{margin-top:4px;color:#57606a;word-break:break-word}" +
    ".ok{color:#1a7f37}.err{color:#cf222e}" +
    "pre{max-height:180px;overflow:auto;background:#f6f8fa;padding:6px;border-radius:6px;font-size:11px;white-space:pre-wrap;margin:4px 0 0}" +
    "summary{cursor:pointer;margin-top:4px;color:#57606a}" +
    "@media (prefers-color-scheme:dark){.box{background:#161b22;color:#e6edf3;border-color:#30363d}" +
    ".proj{border-color:#30363d}button,input{background:#21262d;border-color:#30363d}pre{background:#0d1117}" +
    ".muted,summary{color:#8d96a0}}" +
    "</style>" +
    "<div class='box'><div class='head'><span>Website update</span>" +
    "<button class='logout' title='Log out'>Log out</button>" +
    "<button class='close' title='Hide'>&#x2715;</button></div>" +
    "<div class='body'></div></div>";

  var box = root.querySelector(".box");
  var body = root.querySelector(".body");
  var logoutBtn = root.querySelector(".logout");
  var closeBtn = root.querySelector(".close");

  function el(tag, cls, text) {
    var node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text != null) node.textContent = text;
    return node;
  }

  function mount() {
    var target = script.dataset.target && document.querySelector(script.dataset.target);
    if (target) {
      target.appendChild(host);
      closeBtn.hidden = true;
    } else {
      box.classList.add("floating");
      document.body.appendChild(host);
    }
  }

  function showLogin(message) {
    body.textContent = "";
    logoutBtn.hidden = true;
    var row = el("div", "row");
    var input = el("input");
    input.type = "password";
    input.placeholder = "Admin token";
    var btn = el("button", "primary", "Log in");
    row.appendChild(input);
    row.appendChild(btn);
    body.appendChild(row);
    if (message) body.appendChild(el("div", "muted err", message));

    function submit() {
      if (!input.value.trim()) return;
      setToken(input.value.trim());
      loadProjects();
    }
    btn.addEventListener("click", submit);
    input.addEventListener("keydown", function (e) { if (e.key === "Enter") submit(); });
    input.focus();
  }

  function pickProjects(all) {
    if (forcedProject) {
      return all.filter(function (p) { return p.name === forcedProject; });
    }
    if (showAll) return all;
    var hostname = location.hostname.toLowerCase().replace(/^www\./, "");
    var matching = all.filter(function (p) {
      return p.domains.some(function (d) { return d.replace(/^www\./, "") === hostname; });
    });
    return matching.length ? matching : all;
  }

  function loadProjects() {
    body.textContent = "";
    body.appendChild(el("div", "muted", "Loading…"));
    api("GET", "/projects").then(function (data) {
      logoutBtn.hidden = false;
      body.textContent = "";
      rows = {};
      var list = pickProjects(data.projects);
      if (!list.length) {
        body.appendChild(el("div", "muted", "No projects configured."));
        return;
      }
      list.forEach(function (p) {
        rows[p.name] = createRow(p.name);
        loadStatus(p.name);
      });
    }).catch(function (err) {
      showLogin(err.message);
    });
  }

  function createRow(name) {
    var wrap = el("div", "proj");
    var row = el("div", "row");
    var btn = el("button", "primary", "Update");
    row.appendChild(el("span", "name", name));
    row.appendChild(btn);
    var commit = el("div", "muted");
    var state = el("div", "muted");
    var details = el("details");
    details.appendChild(el("summary", null, "Log"));
    var log = el("pre");
    details.appendChild(log);
    wrap.appendChild(row);
    wrap.appendChild(commit);
    wrap.appendChild(state);
    wrap.appendChild(details);
    body.appendChild(wrap);

    var rowObj = { btn: btn, commit: commit, state: state, log: log, timer: null };
    btn.addEventListener("click", function () {
      btn.disabled = true;
      setState(rowObj, "Starting update…");
      api("POST", "/" + encodeURIComponent(name) + "/update")
        .then(function () { loadStatus(name); })
        .catch(function (err) {
          setState(rowObj, err.message, "err");
          btn.disabled = false;
        });
    });

    return rowObj;
  }

  function setState(row, text, kind) {
    row.state.textContent = text;
    row.state.className = "muted" + (kind ? " " + kind : "");
  }

  function render(row, status) {
    var c = status.commit;
    row.commit.textContent = c
      ? c.sha.slice(0, 7) + " – " + c.message + " (" + new Date(c.date).toLocaleString() + ")"
      : "Not deployed yet.";
    var run = status.last_run;
    if (status.running) {
      setState(row, "Updating…" + (status.pending ? " (another update queued)" : ""));
    } else if (run) {
      var labels = { success: "Updated", "up-to-date": "Already up to date", failed: "Update failed" };
      setState(row, (labels[run.state] || run.state) + " – " +
        new Date(run.finished_at).toLocaleString() + " (" + run.trigger + ")",
        run.state === "failed" ? "err" : "ok");
    } else {
      setState(row, status.auto_update ? "Auto update on push: on" : "Auto update on push: off");
    }
    row.log.textContent = run ? run.log : "";
    row.btn.disabled = status.running;
  }

  function loadStatus(name) {
    var row = rows[name];
    if (!row) return;
    api("GET", "/" + encodeURIComponent(name) + "/status").then(function (status) {
      render(row, status);
      clearTimeout(row.timer);
      if (status.running) row.timer = setTimeout(function () { loadStatus(name); }, 2000);
    }).catch(function (err) {
      setState(row, err.message, "err");
    });
  }

  logoutBtn.addEventListener("click", function () {
    setToken(null);
    if (showAll || forcedProject || script.dataset.target) showLogin();
    else host.remove();
  });

  closeBtn.addEventListener("click", function () { host.remove(); });

  function start() {
    if (!getToken() && !wantsLogin()) return; // normal visitor: show nothing
    mount();
    if (getToken()) loadProjects();
    else showLogin();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start);
  } else {
    start();
  }
})();
