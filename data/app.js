/* T-Print setup UI - icons via Iconify https://icon-sets.iconify.design/ */

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];

const typeIcon = {
  klipper: "mdi:printer-3d-nozzle-heat",
  bambu: "mdi:printer-3d",
  prusa: "mdi:printer-outline",
};

async function api(path, opts = {}) {
  const res = await fetch(path, {
    headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
    ...opts,
  });
  const text = await res.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch { data = { raw: text }; }
  if (!res.ok) throw new Error((data && (data.error || data.result)) || res.statusText);
  return data;
}

function setToast(el, text, ok) {
  if (!el) return;
  if (!text) {
    el.hidden = true;
    el.innerHTML = "";
    el.className = el.classList.contains("sticky-toast") ? "toast sticky-toast" : "toast";
    return;
  }
  const sticky = el.classList.contains("sticky-toast");
  el.hidden = false;
  el.className = (sticky ? "toast sticky-toast " : "toast ") + (ok === true ? "ok" : ok === false ? "err" : "");
  if (sticky) {
    el.innerHTML = `<span class="toast-body">${escapeHtml(text)}</span><button type="button" class="toast-dismiss" aria-label="Dismiss">Dismiss</button>`;
    el.querySelector(".toast-dismiss").onclick = () => setToast(el, "");
  } else {
    el.textContent = text;
  }
}

function escapeHtml(s) {
  return String(s || "").replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[c]);
}

function showPostWifiBanner(ip) {
  const banner = $("#postWifiBanner");
  const text = $("#postWifiText");
  if (!banner || !text) return;
  banner.hidden = false;
  const url = `http://${ip}`;
  text.innerHTML = `Reconnect this phone to your <strong>home Wi‑Fi</strong>, then open <a href="${url}">${url}</a> (also shown on the device screen).`;
}

async function refreshStatus() {
  const s = await api("/api/status");
  const chip = $("#statusChip");
  const title = $(".live-title", chip);
  const ipEl = $(".live-ip", chip);
  const icon = $("iconify-icon", chip);
  if (s.wifiConnected) {
    chip.dataset.state = "ok";
    icon.setAttribute("icon", "mdi:wifi-check");
    title.textContent = s.ssid || "Wi‑Fi";
    ipEl.textContent = s.ip || "";
    // Already on LAN — skip SoftAP reconnect banner
    $("#postWifiBanner").hidden = true;
  } else if (s.softAp) {
    chip.dataset.state = "ap";
    icon.setAttribute("icon", "mdi:access-point");
    title.textContent = "T-Print";
    ipEl.textContent = s.ip || "192.168.4.1";
    $("#postWifiBanner").hidden = true;
  } else {
    chip.dataset.state = "boot";
    icon.setAttribute("icon", "mdi:wifi-off");
    title.textContent = "Offline";
    ipEl.textContent = "";
    $("#postWifiBanner").hidden = true;
  }
  const form = $("#deviceForm");
  form.deviceName.value = s.deviceName || "";
  form.brightness.value = s.brightness || 80;
  $("#briVal").textContent = `${form.brightness.value}%`;
  form.lcdTimeout.value = String(s.lcdTimeoutSec ?? 300);
  form.alarmOnError.checked = !!s.alarmOnError;
  $("#deviceMeta").textContent = `v${s.version} · http://${s.mdns} · ${s.ip}`;
  return s;
}

function showTab(tab) {
  $$(".rail-btn").forEach((b) => {
    const on = b.dataset.tab === tab;
    b.classList.toggle("active", on);
    b.setAttribute("aria-selected", on ? "true" : "false");
  });
  $$(".stage").forEach((panel) => {
    const on = panel.id === `tab-${tab}`;
    panel.classList.toggle("active", on);
    panel.hidden = !on;
  });
  if (tab === "printers") loadPrinters().catch(() => {});
}

function bindTabs() {
  $$(".rail-btn").forEach((btn) => {
    btn.addEventListener("click", () => showTab(btn.dataset.tab));
  });
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function scanWifi() {
  const list = $("#wifiList");
  const btn = $("#btnScan");
  if (btn) btn.disabled = true;
  list.innerHTML = `<div class="empty">Scanning nearby networks… stay on T-Print Wi‑Fi.</div>`;
  setToast($("#wifiMsg"), "Scanning…");

  try {
    await api("/api/wifi/scan", { method: "POST" });
    let nets = [];
    for (let i = 0; i < 40; i++) {
      await sleep(400);
      const r = await api("/api/wifi/scan");
      if (r.status === "scanning") {
        list.innerHTML = `<div class="empty">Scanning… ${i + 1}</div>`;
        continue;
      }
      if (r.status === "error") throw new Error(r.error || "Scan failed");
      nets = r.networks || r || [];
      break;
    }
    if (!Array.isArray(nets)) nets = [];
    if (!nets.length) {
      list.innerHTML = `<div class="empty">No networks found. Try again.</div>`;
      setToast($("#wifiMsg"), "No networks found", false);
      return;
    }
    list.innerHTML = "";
    nets.forEach((n) => {
      const el = document.createElement("button");
      el.type = "button";
      el.className = "tile";
      el.innerHTML = `
        <div>
          <strong>${escapeHtml(n.ssid)}</strong>
          <small>${n.rssi} dBm · ${n.open ? "open" : "secured"}</small>
        </div>
        <iconify-icon icon="${n.open ? "mdi:wifi" : "mdi:wifi-lock"}" width="22"></iconify-icon>`;
      el.addEventListener("click", () => {
        $("#wifiForm").ssid.value = n.ssid;
        $("#wifiForm").password.focus();
      });
      list.appendChild(el);
    });
    setToast($("#wifiMsg"), `Found ${nets.length} network${nets.length === 1 ? "" : "s"}`, true);
  } finally {
    if (btn) btn.disabled = false;
  }
}

async function loadPrinters() {
  const list = $("#printerList");
  const printers = await api("/api/printers");
  if (!printers.length) {
    list.innerHTML = `
      <div class="empty">
        <iconify-icon icon="mdi:printer-3d" width="28" style="color:var(--accent);margin-bottom:.4rem"></iconify-icon><br/>
        No printers yet. Add Klipper, Bambu, or Prusa.
      </div>`;
    return;
  }
  list.innerHTML = "";
  printers.forEach((p) => {
    const el = document.createElement("div");
    el.className = "tile";
    const camHint =
      p.snapshotUrl || p.type === "prusa" || p.type === "bambu" || p.type === "klipper"
        ? ` · <iconify-icon icon="mdi:cctv" width="14"></iconify-icon> cam`
        : "";
    el.innerHTML = `
      <div>
        <strong><iconify-icon icon="${typeIcon[p.type] || "mdi:printer-3d"}" width="18"></iconify-icon> ${escapeHtml(p.name)}</strong>
        <small>${escapeHtml(p.type)} · ${escapeHtml(p.host)}${p.port ? ":" + p.port : ""}${camHint}</small>
      </div>
      <div class="tile-actions">
        <button class="icon-btn" title="Test" data-act="test" type="button"><iconify-icon icon="mdi:connection" width="18"></iconify-icon></button>
        <button class="icon-btn" title="Edit" data-act="edit" type="button"><iconify-icon icon="mdi:pencil" width="18"></iconify-icon></button>
        <button class="icon-btn" title="Delete" data-act="del" type="button"><iconify-icon icon="mdi:trash-can-outline" width="18"></iconify-icon></button>
      </div>`;
    el.querySelector('[data-act="edit"]').onclick = () => openPrinterDialog(p);
    el.querySelector('[data-act="del"]').onclick = async () => {
      if (!confirm(`Delete ${p.name}?`)) return;
      await api(`/api/printers/${p.id}`, { method: "DELETE" });
      loadPrinters();
    };
    el.querySelector('[data-act="test"]').onclick = async () => {
      setToast($("#printerTestBanner"), `Testing ${p.name}…`);
      try {
        const r = await api(`/api/printers/${p.id}/test`, { method: "POST" });
        setToast($("#printerTestBanner"), `${p.name}: ${r.result || (r.ok ? "ok" : "failed")}`, !!r.ok);
      } catch (e) {
        setToast($("#printerTestBanner"), `${p.name}: ${e.message}`, false);
      }
    };
    list.appendChild(el);
  });
}

function syncTypeFields() {
  const type = $("#printerForm").type.value;
  document.body.dataset.ptype = type;
  $$(".type-card").forEach((c) => c.classList.toggle("active", c.dataset.type === type));
  const port = $("#printerForm").port;
  if (!port.value) {
    port.placeholder = type === "klipper" ? "7125" : type === "bambu" ? "8883" : "80";
  }
  syncCamPlaceholders();
  hideCamPreview();
}

function syncCamPlaceholders() {
  const form = $("#printerForm");
  if (!form || !form.snapshotUrl) return;
  const host = (form.host.value || "").trim() || "192.168.1.50";
  const type = form.type.value;
  const hint = $("#snapHint");
  if (type === "prusa") {
    form.snapshotUrl.placeholder = `http://${host}/api/v1/cameras/snap`;
    if (hint) {
      hint.textContent =
        "Optional. Leave blank to use PrusaLink /api/v1/cameras/snap (needs API key). Buddy3D RTSP isn’t supported on-device.";
    }
  } else if (type === "bambu") {
    form.snapshotUrl.placeholder = "(auto A1/P1 LAN cam — or paste a JPEG proxy URL)";
    if (hint) {
      hint.textContent =
        "Optional. A1/P1 use the LAN JPEG cam automatically. X1 RTSPS isn’t decodeable here — use a go2rtc/JPEG proxy URL if needed.";
    }
  } else {
    form.snapshotUrl.placeholder = `http://${host}/webcam/snapshot.jpg`;
    if (hint) {
      hint.textContent =
        "JPEG from crowsnest / webcamd — shown on the device camera page. Click Preview to check the URL.";
    }
  }
}

function hideCamPreview() {
  const box = $("#camPreview");
  const img = $("#camPreviewImg");
  if (box) {
    box.hidden = true;
    box.classList.remove("is-visible");
  }
  if (img) {
    img.removeAttribute("src");
    img.alt = "Printer camera";
  }
}

function showCamPreview() {
  const box = $("#camPreview");
  const img = $("#camPreviewImg");
  if (!box || !img) return;
  const snap = ($("#printerForm")?.snapshotUrl?.value || "").trim();
  if (!snap) {
    hideCamPreview();
    setToast($("#testMsg"), "Enter a Snapshot URL first", false);
    return;
  }
  box.hidden = false;
  box.classList.add("is-visible");
  img.alt = "Printer camera";
  img.src = `${snap}${snap.includes("?") ? "&" : "?"}_=${Date.now()}`;
}

function openPrinterDialog(p) {
  const form = $("#printerForm");
  $("#dialogTitle span").textContent = p ? "Edit printer" : "Add printer";
  form.id.value = p?.id || "";
  form.name.value = p?.name || "";
  form.type.value = p?.type || "klipper";
  form.host.value = p?.host || "";
  form.port.value = p?.port || "";
  form.apiKey.value = p?.apiKey || "";
  form.serial.value = p?.serial || "";
  form.accessCode.value = p?.accessCode || "";
  form.snapshotUrl.value = p?.snapshotUrl || "";
  setToast($("#testMsg"), "");
  hideCamPreview();
  syncTypeFields();
  $("#printerDialog").showModal();
}

function formPrinter() {
  const f = $("#printerForm");
  return {
    id: f.id.value || undefined,
    name: f.name.value,
    type: f.type.value,
    host: f.host.value,
    port: Number(f.port.value || 0),
    apiKey: f.apiKey.value,
    serial: f.serial.value,
    accessCode: f.accessCode.value,
    snapshotUrl: (f.snapshotUrl.value || "").trim(),
    enabled: true,
  };
}

function bindForms() {
  $("#btnScan").onclick = () => scanWifi().catch((e) => setToast($("#wifiMsg"), e.message, false));

  $("#wifiForm").onsubmit = async (e) => {
    e.preventDefault();
    const fd = new FormData(e.target);
    const connectBtn = e.target.querySelector('button[type="submit"]');
    if (connectBtn) connectBtn.disabled = true;
    setToast($("#wifiMsg"), "Connecting… keep your phone on T-Print Wi‑Fi.");
    try {
      await api("/api/wifi", {
        method: "PUT",
        body: JSON.stringify({ ssid: fd.get("ssid"), password: fd.get("password") }),
      });

      // Poll while still on T-Print AP — device joins home Wi‑Fi in the background.
      let ip = "";
      const deadline = Date.now() + 30000;
      while (Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 800));
        let st;
        try {
          st = await api("/api/wifi");
        } catch (_) {
          setToast(
            $("#wifiMsg"),
            "Joined Wi‑Fi. Reconnect your phone to home Wi‑Fi and open the IP shown on the device.",
            true,
          );
          return;
        }
        if (st.status === "connected" || st.wifiConnected) {
          ip = st.ip || "";
          break;
        }
        if (st.status === "failed") {
          throw new Error(st.error || "Failed to join network");
        }
        setToast($("#wifiMsg"), "Connecting…");
      }

      if (!ip) {
        setToast(
          $("#wifiMsg"),
          "Still connecting — check the device screen for an IP, then rejoin home Wi‑Fi.",
          false,
        );
        return;
      }

      setToast(
        $("#wifiMsg"),
        `Connected. Rejoin your home Wi‑Fi, then open http://${ip}`,
        true,
      );
      showPostWifiBanner(ip);
      try { await refreshStatus(); } catch (_) {}
    } catch (err) {
      const msg = (err && err.message) || "";
      if (/failed to fetch|networkerror|load failed/i.test(msg)) {
        setToast(
          $("#wifiMsg"),
          "Left T-Print Wi‑Fi (normal after join). Reconnect to home Wi‑Fi and open the IP on the device screen.",
          true,
        );
      } else {
        setToast($("#wifiMsg"), msg || "Failed to join network", false);
      }
    } finally {
      if (connectBtn) connectBtn.disabled = false;
    }
  };

  $("#deviceForm").brightness.oninput = (e) => {
    $("#briVal").textContent = `${e.target.value}%`;
  };

  $("#deviceForm").onsubmit = async (e) => {
    e.preventDefault();
    const fd = new FormData(e.target);
    await api("/api/device", {
      method: "PUT",
      body: JSON.stringify({
        deviceName: fd.get("deviceName"),
        brightness: Number(fd.get("brightness")),
        lcdTimeoutSec: Number(fd.get("lcdTimeout")),
        alarmOnError: !!e.target.alarmOnError.checked,
      }),
    });
    await refreshStatus();
    setToast($("#wifiMsg"), "");
  };

  $("#btnScreenshot").onclick = async () => {
    setToast($("#screenshotMsg"), "Capturing display…");
    try {
      const res = await fetch("/api/screenshot");
      if (!res.ok) throw new Error("Capture failed");
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      const stamp = new Date().toISOString().replace(/[:.]/g, "-");
      a.href = url;
      a.download = `t-print-${stamp}.bmp`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
      setToast($("#screenshotMsg"), "Screenshot downloaded (BMP of the device display).", true);
    } catch (e) {
      setToast($("#screenshotMsg"), e.message || "Screenshot failed", false);
    }
  };

  $("#btnReset").onclick = async () => {
    if (!confirm("Erase Wi-Fi and printers, then reboot?")) return;
    await api("/api/factory-reset", { method: "POST" });
  };

  $("#btnAdd").onclick = () => openPrinterDialog(null);
  $("#btnClosePrinter").onclick = () => $("#printerDialog").close();

  const discoverBtn = $("#btnDiscover");
  if (discoverBtn) {
    discoverBtn.onclick = async () => {
      const box = $("#discoverList");
      discoverBtn.disabled = true;
      box.innerHTML = `<div class="empty">Scanning LAN for printers…</div>`;
      setToast($("#printerTestBanner"), "Scanning network for printers…");
      try {
        await api("/api/discover", { method: "POST" });
        let st = null;
        const deadline = Date.now() + 90000;
        while (Date.now() < deadline) {
          await new Promise((r) => setTimeout(r, 700));
          st = await api("/api/discover");
          const pct = st.progress || 0;
          const n = st.count || 0;
          box.innerHTML = `<div class="empty">Scanning… ${pct}%${n ? ` · ${n} found` : ""}</div>`;
          setToast($("#printerTestBanner"), `Scanning… ${pct}%`);
          if (st.status === "done" || st.status === "failed") break;
        }
        if (!st || st.status === "failed") {
          throw new Error((st && st.error) || "Scan failed");
        }
        const found = st.printers || [];
        if (!found.length) {
          box.innerHTML = `<div class="empty">No printers found. Check they're online on this Wi‑Fi, then try Add.</div>`;
          setToast($("#printerTestBanner"), "No printers found on the LAN", false);
          return;
        }
        box.innerHTML = `<div class="empty" style="border-style:solid">Found ${found.length} — tap Add to save</div>`;
        found.forEach((p) => {
          const el = document.createElement("div");
          el.className = "tile";
          el.innerHTML = `
            <div>
              <strong><iconify-icon icon="${typeIcon[p.type] || "mdi:printer-3d"}" width="18"></iconify-icon> ${escapeHtml(p.name)}</strong>
              <small>${escapeHtml(p.type)} · ${escapeHtml(p.host)}:${p.port} · ${escapeHtml(p.source)}</small>
            </div>
            <button class="btn btn-primary" type="button">Add</button>`;
          el.querySelector("button").onclick = () => openPrinterDialog({
            name: p.name,
            type: p.type,
            host: p.host,
            port: p.port,
          });
          box.appendChild(el);
        });
        setToast($("#printerTestBanner"), `Found ${found.length} printer${found.length === 1 ? "" : "s"}`, true);
      } catch (e) {
        box.innerHTML = `<div class="empty">${escapeHtml(e.message)}</div>`;
        setToast($("#printerTestBanner"), e.message || "Scan failed", false);
      } finally {
        discoverBtn.disabled = false;
      }
    };
  }

  $$(".type-card").forEach((card) => {
    card.onclick = () => {
      $("#printerForm").type.value = card.dataset.type;
      $("#printerForm").port.value = "";
      syncTypeFields();
    };
  });

  const hostInput = $("#printerForm")?.host;
  if (hostInput) {
    hostInput.addEventListener("input", () => {
      syncCamPlaceholders();
    });
  }
  const snapInput = $("#printerForm")?.snapshotUrl;
  if (snapInput) {
    snapInput.addEventListener("input", () => {
      // Don't auto-preview — only after Preview is clicked.
      if (!($("#printerForm")?.snapshotUrl?.value || "").trim()) hideCamPreview();
    });
  }
  const camBtn = $("#btnCamPreview");
  if (camBtn) camBtn.onclick = () => showCamPreview();

  $("#btnTest").onclick = async (e) => {
    e.preventDefault();
    e.stopPropagation();
    const body = formPrinter();
    if (!body.host) {
      setToast($("#testMsg"), "Enter a host / IP first", false);
      return;
    }
    setToast($("#testMsg"), "Testing connection…");
    try {
      // Separate path — never POST /api/printers (that creates a profile).
      const r = await api("/api/test-printer", {
        method: "POST",
        body: JSON.stringify(body),
      });
      setToast($("#testMsg"), r.result || (r.ok ? "online" : "unreachable"), !!r.ok);
    } catch (err) {
      setToast($("#testMsg"), err.message || "Test failed", false);
    }
  };

  async function savePrinterFromDialog() {
    const body = formPrinter();
    const saveBtn = $("#btnSavePrinter");
    if (saveBtn) {
      saveBtn.disabled = true;
      saveBtn.textContent = "Saving…";
    }
    try {
      if (body.id) {
        await api(`/api/printers/${body.id}`, { method: "PUT", body: JSON.stringify(body) });
      } else {
        await api("/api/printers", { method: "POST", body: JSON.stringify(body) });
      }
      $("#printerDialog").close();
      setToast($("#printerTestBanner"), `Saved ${body.name}`, true);
      await loadPrinters();
    } catch (err) {
      setToast($("#testMsg"), err.message || "Save failed", false);
    } finally {
      if (saveBtn) {
        saveBtn.disabled = false;
        saveBtn.innerHTML = `<iconify-icon icon="mdi:content-save" width="18"></iconify-icon> Save printer`;
      }
    }
  }

  $("#btnSavePrinter").onclick = (e) => {
    e.preventDefault();
    e.stopPropagation();
    const form = $("#printerForm");
    if (form && !form.reportValidity()) return;
    savePrinterFromDialog();
  };

  // Enter in a field still saves; never treat Test as submit.
  $("#printerForm").addEventListener("submit", (e) => {
    e.preventDefault();
    savePrinterFromDialog();
  });
}

bindTabs();
bindForms();

(async () => {
  try {
    const s = await refreshStatus();
    if (s.wifiConnected) {
      showTab("printers");
      loadPrinters().catch(() => {});
    } else {
      showTab("wifi");
      loadPrinters().catch(() => {});
    }
  } catch (_) {
    showTab("wifi");
    loadPrinters().catch(() => {});
  }
})();
