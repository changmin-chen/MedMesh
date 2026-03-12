(function () {
  function $(id) { return document.getElementById(id); }

  async function writeFileToVfs(file) {
    if (!Module || !Module.FS) {
      throw new Error("Module.FS not available (runtime not initialized?)");
    }

    const buf = new Uint8Array(await file.arrayBuffer());

    try { Module.FS.mkdir("/data"); } catch (_) { /* exists */ }

    const vpath = `/data/${file.name}`;
    try { Module.FS.unlink(vpath); } catch (_) { /* ignore */ }

    Module.FS.writeFile(vpath, buf);
    return vpath;
  }

  function initUI() {
    console.log("[UI] initUI()");

    const fileInput = $("file");
    const iso = $("iso");
    const isoValue = $("isoValue");
    const resetBtn = $("reset");
    const rangeEl = $("range");

    if (!fileInput || !iso || !isoValue || !resetBtn) {
      console.error("[UI] Missing DOM elements.");
      return;
    }

    console.log("[UI] exports:",
        "resetCamera=", typeof Module.resetCamera,
        "setIsoValue=", typeof Module.setIsoValue,
        "loadNifti=", typeof Module.loadNifti
    );

    resetBtn.addEventListener("click", () => {
      if (typeof Module.resetCamera !== "function") {
        console.error("[UI] Module.resetCamera is not a function");
        return;
      }
      Module.resetCamera();
    });

    iso.addEventListener("input", () => {
      isoValue.textContent = iso.value;
      if (typeof Module.setIsoValue !== "function") {
        console.error("[UI] Module.setIsoValue is not a function");
        return;
      }
      Module.setIsoValue(Number(iso.value));
    });

    fileInput.addEventListener("change", async () => {
      const file = fileInput.files && fileInput.files[0];
      if (!file) return;

      if (!file.name.toLowerCase().endsWith(".nii")) {
        alert("MVP only supports .nii (uncompressed).");
        return;
      }

      try {
        // Let the browser update UI before heavy wasm work
        await new Promise(r => setTimeout(r, 0));

        const vpath = await writeFileToVfs(file);

        if (typeof Module.loadNifti !== "function") {
          console.error("[UI] Module.loadNifti is not a function");
          return;
        }

        console.log("[UI] loadNifti:", vpath);
        Module.loadNifti(vpath);

        // Update UI range if exported
        if (typeof Module.getScalarMin === "function" && typeof Module.getScalarMax === "function") {
          const min = Module.getScalarMin();
          const max = Module.getScalarMax();
          if (rangeEl) rangeEl.textContent = `Range: [${min.toFixed(3)}, ${max.toFixed(3)}]`;

          // Adjust slider bounds
          iso.min = String(Math.floor(min));
          iso.max = String(Math.ceil(max));

          // Optionally move iso to mid-range
          if (Number.isFinite(min) && Number.isFinite(max) && max > min) {
            const mid = Math.round((min + max) * 0.5);
            iso.value = String(mid);
            isoValue.textContent = String(mid);
            Module.setIsoValue(mid);
          }
        }

        if (typeof Module.render === "function") {
          Module.render();
        }
      } catch (e) {
        console.error("[UI] upload/load failed:", e);
      }
    });
  }

  // Register hook BEFORE nifti_demo.js executes (because app.js loads first)
  Module.onRuntimeInitialized = () => {
    console.log("[Emscripten] onRuntimeInitialized");
    initUI();
  };

  // Defensive: if runtime was already initialized (shouldn't happen with this load order)
  if (Module.calledRun) {
    console.log("[Emscripten] calledRun=true (late init)");
    initUI();
  }
})();
