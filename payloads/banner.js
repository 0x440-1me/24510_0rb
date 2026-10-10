(() => {
  const id = "tsuki-juri-test-banner";
  document.getElementById(id)?.remove();

  const host = document.createElement("div");
  host.id = id;
  host.style.cssText = "position:fixed;top:14px;left:50%;transform:translateX(-50%);z-index:2147483647;pointer-events:none";
  const shadow = host.attachShadow({ mode: "open" });
  const banner = document.createElement("div");
  banner.style.cssText = "pointer-events:auto;display:flex;align-items:center;gap:14px;padding:12px 16px 12px 20px;border-radius:14px;background:#ffd6e7;color:#342331;font:600 15px/1.35 system-ui,-apple-system,Segoe UI,sans-serif;box-shadow:0 8px 28px rgba(45,20,35,.28);border:1px solid rgba(80,30,60,.18)";
  const label = document.createElement("span");
  label.textContent = "JURI WAS HERE ♡";
  const close = document.createElement("button");
  close.type = "button";
  close.textContent = "×";
  close.setAttribute("aria-label", "Close Juri test banner");
  close.style.cssText = "border:0;border-radius:8px;background:rgba(80,30,60,.1);color:inherit;font:700 20px/1 system-ui,sans-serif;width:30px;height:30px;cursor:pointer";
  close.addEventListener("click", () => host.remove());
  banner.append(label, close);
  shadow.append(banner);
  document.documentElement.append(host);
  return "TSUKI_BANNER_SHOWN";
})()
