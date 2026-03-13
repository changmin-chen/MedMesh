(function () {
  function $(id) { return document.getElementById(id); }

  const state = {
    dragDepth: 0,
    busy: false,
    busyMessage: "",
    initialized: false,
    loadedLabel: "",
    hasVolumeLoaded: false,
    pendingIsoValue: 300,
    renderedIsoValue: 300,
    pendingKeepLargestComponent: true,
    renderedKeepLargestComponent: true,
    editingIsoInput: false,
    isoInputDraft: "",
  };

  function pathJoin() {
    const joined = Array.from(arguments)
      .filter((part) => part !== undefined && part !== null && part !== "")
      .map((part) => String(part))
      .map((part, index) => (
        index === 0 ? part.replace(/\/+$/, "") : part.replace(/^\/+|\/+$/g, "")
      ))
      .filter((part, index) => index === 0 || part.length)
      .join("/")
      .replace(/\/{2,}/g, "/");

    if (!joined) {
      return "/";
    }
    return joined.startsWith("/") ? joined : `/${joined}`;
  }

  function dirname(path) {
    const normalized = path.replace(/\/+$/, "");
    const index = normalized.lastIndexOf("/");
    return index <= 0 ? "/" : normalized.slice(0, index);
  }

  function ensureDirTree(path) {
    const parts = path.split("/").filter(Boolean);
    let current = "";
    for (const part of parts) {
      current += `/${part}`;
      try {
        Module.FS.mkdir(current);
      } catch (_) {
        // Ignore already-existing folders.
      }
    }
  }

  function removePathRecursive(path) {
    try {
      const stat = Module.FS.stat(path);
      if (Module.FS.isDir(stat.mode)) {
        for (const name of Module.FS.readdir(path)) {
          if (name === "." || name === "..") {
            continue;
          }
          removePathRecursive(pathJoin(path, name));
        }
        Module.FS.rmdir(path);
        return;
      }
      Module.FS.unlink(path);
    } catch (_) {
      // Ignore missing paths.
    }
  }

  async function writeFileToVfs(vpath, file) {
    ensureDirTree(dirname(vpath));
    try {
      Module.FS.unlink(vpath);
    } catch (_) {
      // Ignore files that do not exist yet.
    }
    Module.FS.writeFile(vpath, new Uint8Array(await file.arrayBuffer()));
  }

  async function writeFilesToDirectory(rootPath, files) {
    removePathRecursive(rootPath);
    ensureDirTree(rootPath);

    const sortedFiles = [...files].sort((left, right) =>
      left.relativePath.localeCompare(right.relativePath)
    );

    for (const item of sortedFiles) {
      const targetPath = pathJoin(rootPath, item.relativePath);
      await writeFileToVfs(targetPath, item.file);
    }
  }

  function formatNumber(value) {
    if (!Number.isFinite(value)) {
      return "-";
    }
    if (Math.abs(value) >= 1000) {
      return value.toFixed(0);
    }
    return value.toFixed(3).replace(/\.?0+$/, "");
  }

  function formatInputNumber(value) {
    if (!Number.isFinite(value)) {
      return "";
    }
    return Number(value.toFixed(6)).toString();
  }

  function stripExtension(name) {
    return name.replace(/(\.nii(\.gz)?|\.stl)$/i, "") || "mesh";
  }

  function setStatus(message, isError) {
    const status = $("status");
    if (!status) {
      return;
    }
    status.textContent = message;
    status.dataset.state = isError ? "error" : "ready";
  }

  function updateViewportStatus() {
    const overlay = $("viewportStatus");
    const text = $("viewportStatusText");
    if (!overlay || !text) {
      return;
    }

    text.textContent = state.busyMessage || "Working...";
    overlay.setAttribute("aria-hidden", state.busy ? "false" : "true");
  }

  function setBusy(isBusy, message) {
    state.busy = isBusy;
    state.busyMessage = isBusy ? (message || state.busyMessage || "Working...") : "";
    document.body.classList.toggle("is-busy", isBusy);
    if (message) {
      setStatus(message, false);
    }
    updateViewportStatus();
    updateMeshControls();
    updateDownloadState();
  }

  function setDragActive(isActive) {
    document.body.classList.toggle("is-drag-active", isActive);
  }

  function waitForNextPaint() {
    return new Promise((resolve) => {
      requestAnimationFrame(() => requestAnimationFrame(resolve));
    });
  }

  function isNearlyEqual(left, right) {
    if (!Number.isFinite(left) || !Number.isFinite(right)) {
      return false;
    }
    const tolerance = Math.max(1e-6, Math.abs(left) * 1e-6, Math.abs(right) * 1e-6);
    return Math.abs(left - right) <= tolerance;
  }

  function getCurrentScalarRange() {
    const min = Number(Module.getScalarMin());
    const max = Number(Module.getScalarMax());
    if (!Number.isFinite(min) || !Number.isFinite(max)) {
      return { min: 0, max: 1 };
    }
    return min <= max ? { min, max } : { min: max, max: min };
  }

  function clampIsoValue(value) {
    const { min, max } = getCurrentScalarRange();
    if (!Number.isFinite(value)) {
      return min;
    }
    return Math.min(max, Math.max(min, value));
  }

  function getNudgeStep() {
    const { min, max } = getCurrentScalarRange();
    const span = max - min;
    if (!(span > 0)) {
      return 1;
    }

    const rawStep = span / 100;
    const magnitude = 10 ** Math.floor(Math.log10(rawStep));
    const normalized = rawStep / magnitude;

    if (normalized <= 1) {
      return magnitude;
    }
    if (normalized <= 2) {
      return 2 * magnitude;
    }
    if (normalized <= 5) {
      return 5 * magnitude;
    }
    return 10 * magnitude;
  }

  function hasPendingMeshChanges() {
    if (!state.hasVolumeLoaded) {
      return false;
    }
    return !isNearlyEqual(state.pendingIsoValue, state.renderedIsoValue) ||
      state.pendingKeepLargestComponent !== state.renderedKeepLargestComponent;
  }

  function syncRenderedStateFromModule() {
    if (typeof Module.getIsoValue === "function") {
      state.renderedIsoValue = clampIsoValue(Number(Module.getIsoValue()));
    }
    if (typeof Module.getKeepLargestComponent === "function") {
      state.renderedKeepLargestComponent = !!Module.getKeepLargestComponent();
    }
  }

  function syncPendingStateFromRendered() {
    state.pendingIsoValue = state.renderedIsoValue;
    state.pendingKeepLargestComponent = state.renderedKeepLargestComponent;
    if (!state.editingIsoInput) {
      state.isoInputDraft = formatInputNumber(state.pendingIsoValue);
    }
  }

  function updateScalarControls() {
    const iso = $("iso");
    const isoInput = $("isoInput");
    const rangeMin = $("rangeMin");
    const rangeMid = $("rangeMid");
    const rangeMax = $("rangeMax");
    const stepDown = $("isoStepDown");
    const stepUp = $("isoStepUp");
    if (!iso || !isoInput || !rangeMin || !rangeMid || !rangeMax || !stepDown || !stepUp) {
      return;
    }

    const { min, max } = getCurrentScalarRange();
    const midpoint = min + ((max - min) / 2);
    const nudgeStep = getNudgeStep();

    iso.min = String(min);
    iso.max = String(max);
    iso.step = "any";

    isoInput.min = String(min);
    isoInput.max = String(max);
    isoInput.step = String(nudgeStep);

    rangeMin.textContent = formatNumber(min);
    rangeMid.textContent = formatNumber(midpoint);
    rangeMax.textContent = formatNumber(max);

    stepDown.textContent = `-${formatNumber(nudgeStep)}`;
    stepUp.textContent = `+${formatNumber(nudgeStep)}`;
    stepDown.title = `Decrease pending iso by ${formatNumber(nudgeStep)}`;
    stepUp.title = `Increase pending iso by ${formatNumber(nudgeStep)}`;
  }

  function updateMeshControls() {
    const iso = $("iso");
    const isoInput = $("isoInput");
    const largestComponent = $("largestComponent");
    const applyMesh = $("applyMesh");
    const stepDown = $("isoStepDown");
    const stepUp = $("isoStepUp");
    const resetBtn = $("reset");
    if (!iso || !isoInput || !largestComponent || !applyMesh || !stepDown || !stepUp || !resetBtn) {
      return;
    }

    const hasVolume = state.hasVolumeLoaded;
    const dirty = hasPendingMeshChanges();
    const disabled = state.busy || !hasVolume;

    iso.disabled = disabled;
    iso.value = hasVolume ? String(state.pendingIsoValue) : (iso.min || "0");

    isoInput.disabled = disabled;
    if (!state.editingIsoInput) {
      isoInput.value = hasVolume ? formatInputNumber(state.pendingIsoValue) : "";
    }

    largestComponent.disabled = disabled;
    largestComponent.checked = state.pendingKeepLargestComponent;

    stepDown.disabled = disabled;
    stepUp.disabled = disabled;
    resetBtn.disabled = state.busy || !hasVolume;

    applyMesh.disabled = state.busy || !hasVolume || !dirty;
    applyMesh.textContent = "Apply";

    document.body.classList.toggle("has-pending-mesh", dirty);
  }

  function updateDownloadState() {
    const downloadBtn = $("download");
    if (!downloadBtn) {
      return;
    }
    const hasMesh = typeof Module.hasMesh === "function" && Module.hasMesh();
    downloadBtn.disabled = state.busy || hasPendingMeshChanges() || !hasMesh;
  }

  function getModuleError(defaultMessage) {
    if (typeof Module.getLastError !== "function") {
      return defaultMessage;
    }
    return Module.getLastError() || defaultMessage;
  }

  function setPendingIsoValue(value, preserveDraft) {
    if (!state.hasVolumeLoaded) {
      return;
    }

    state.pendingIsoValue = clampIsoValue(value);
    if (!preserveDraft) {
      state.isoInputDraft = formatInputNumber(state.pendingIsoValue);
    }
    updateMeshControls();
    updateDownloadState();
  }

  function adjustPendingIso(delta) {
    if (!state.hasVolumeLoaded) {
      return;
    }
    setPendingIsoValue(state.pendingIsoValue + delta, false);
    setStatus(
      `Pending iso ${formatNumber(state.pendingIsoValue)}. Apply to rebuild the surface.`,
      false
    );
  }

  async function applyPendingMeshSettings() {
    if (state.busy || !state.hasVolumeLoaded || !hasPendingMeshChanges()) {
      return;
    }

    const targetIsoValue = clampIsoValue(state.pendingIsoValue);
    const keepLargestComponent = !!state.pendingKeepLargestComponent;
    state.pendingIsoValue = targetIsoValue;
    state.pendingKeepLargestComponent = keepLargestComponent;
    updateMeshControls();

    try {
      setBusy(true, "Updating surface...");
      await waitForNextPaint();

      let applied = true;
      if (typeof Module.applyMeshSettings === "function") {
        applied = Module.applyMeshSettings(targetIsoValue, keepLargestComponent);
      } else {
        if (typeof Module.setKeepLargestComponent === "function" &&
            keepLargestComponent !== state.renderedKeepLargestComponent) {
          Module.setKeepLargestComponent(keepLargestComponent);
        }
        if (typeof Module.setIsoValue === "function") {
          Module.setIsoValue(targetIsoValue);
        }
      }

      if (applied === false) {
        throw new Error(getModuleError("Failed to update the surface."));
      }

      syncRenderedStateFromModule();
      syncPendingStateFromRendered();
      updateMeshControls();
      updateDownloadState();

      if (typeof Module.hasMesh === "function" && !Module.hasMesh()) {
        setStatus(
          `Rendered iso ${formatNumber(state.renderedIsoValue)}. The surface is empty at this threshold.`,
          false
        );
      } else {
        setStatus(`Rendered iso ${formatNumber(state.renderedIsoValue)}.`, false);
      }
    } catch (error) {
      console.error("[UI] mesh apply failed:", error);
      setStatus(error.message || "Surface update failed.", true);
    } finally {
      setBusy(false);
    }
  }

  async function collectFilesFromHandle(handle, relativeRoot) {
    if (handle.kind === "file") {
      return [{
        file: await handle.getFile(),
        relativePath: relativeRoot || handle.name,
      }];
    }

    const files = [];
    for await (const child of handle.values()) {
      const childRoot = relativeRoot ? `${relativeRoot}/${child.name}` : child.name;
      files.push(...await collectFilesFromHandle(child, childRoot));
    }
    return files;
  }

  function readAllDirectoryEntries(directoryEntry) {
    return new Promise((resolve, reject) => {
      const reader = directoryEntry.createReader();
      const entries = [];

      function readChunk() {
        reader.readEntries((batch) => {
          if (!batch.length) {
            resolve(entries);
            return;
          }
          entries.push(...batch);
          readChunk();
        }, reject);
      }

      readChunk();
    });
  }

  function getFileFromEntry(entry) {
    return new Promise((resolve, reject) => entry.file(resolve, reject));
  }

  async function collectFilesFromEntry(entry, relativeRoot) {
    if (entry.isFile) {
      return [{
        file: await getFileFromEntry(entry),
        relativePath: relativeRoot || entry.name,
      }];
    }

    const children = await readAllDirectoryEntries(entry);
    children.sort((left, right) => left.name.localeCompare(right.name));

    const files = [];
    for (const child of children) {
      const childRoot = relativeRoot ? `${relativeRoot}/${child.name}` : child.name;
      files.push(...await collectFilesFromEntry(child, childRoot));
    }
    return files;
  }

  async function resolveDroppedDirectory(items) {
    for (const item of items) {
      if (typeof item.getAsFileSystemHandle === "function") {
        try {
          const handle = await item.getAsFileSystemHandle();
          if (handle && handle.kind === "directory") {
            return {
              rootName: handle.name || "dicom",
              files: await collectFilesFromHandle(handle, ""),
            };
          }
        } catch (_) {
          // Fall through to the legacy entry API.
        }
      }
    }

    for (const item of items) {
      if (typeof item.webkitGetAsEntry === "function") {
        const entry = item.webkitGetAsEntry();
        if (entry && entry.isDirectory) {
          return {
            rootName: entry.name || "dicom",
            files: await collectFilesFromEntry(entry, ""),
          };
        }
      }
    }

    return null;
  }

  function resolveDroppedFile(items, fileList) {
    for (const item of items) {
      const file = item.getAsFile && item.getAsFile();
      if (file) {
        return file;
      }
    }
    return fileList[0] || null;
  }

  async function resolveDropPayload(dataTransfer) {
    const items = Array.from(dataTransfer.items || []).filter((item) => item.kind === "file");
    const fileList = Array.from(dataTransfer.files || []);

    const directoryPayload = await resolveDroppedDirectory(items);
    if (directoryPayload && directoryPayload.files.length) {
      return { kind: "directory", ...directoryPayload };
    }

    const file = resolveDroppedFile(items, fileList);
    if (file) {
      return { kind: "file", file };
    }

    throw new Error("Drop a `.nii` file or a directory that contains DICOM slices.");
  }

  async function loadDroppedFile(file) {
    const inputRoot = "/data/nifti";
    const vpath = pathJoin(inputRoot, file.name);
    state.loadedLabel = stripExtension(file.name);

    setBusy(true, `Copying ${file.name}...`);
    await writeFilesToDirectory(inputRoot, [{
      file,
      relativePath: file.name,
    }]);

    setBusy(true, `Loading ${file.name}...`);
    await waitForNextPaint();

    if (typeof Module.loadNifti !== "function" || !Module.loadNifti(vpath)) {
      throw new Error(getModuleError("Failed to load the dropped file as NIFTI."));
    }

    state.hasVolumeLoaded = true;
    updateScalarControls();
    syncRenderedStateFromModule();
    syncPendingStateFromRendered();
    updateMeshControls();
    updateDownloadState();

    if (typeof Module.hasMesh === "function" && !Module.hasMesh()) {
      setStatus(`Loaded NIFTI: ${file.name}. The current iso-surface is empty.`, false);
      return;
    }
    setStatus(`Loaded NIFTI: ${file.name}`, false);
  }

  async function loadDroppedDirectory(payload) {
    const dicomRoot = pathJoin("/data/dicom", payload.rootName || "drop");
    state.loadedLabel = stripExtension(payload.rootName || "mesh");

    setBusy(true, `Copying ${payload.files.length} files from ${payload.rootName}...`);
    await writeFilesToDirectory(dicomRoot, payload.files);

    setBusy(true, `Loading ${payload.rootName}...`);
    await waitForNextPaint();

    if (typeof Module.loadDicom !== "function" || !Module.loadDicom(dicomRoot)) {
      throw new Error(getModuleError("Failed to load a DICOM series from the dropped directory."));
    }

    state.hasVolumeLoaded = true;
    updateScalarControls();
    syncRenderedStateFromModule();
    syncPendingStateFromRendered();
    updateMeshControls();
    updateDownloadState();

    if (typeof Module.hasMesh === "function" && !Module.hasMesh()) {
      setStatus(`Loaded DICOM directory: ${payload.rootName}. The current iso-surface is empty.`, false);
      return;
    }
    setStatus(`Loaded DICOM directory: ${payload.rootName}`, false);
  }

  async function handleDrop(dataTransfer) {
    if (state.busy) {
      return;
    }

    const payload = await resolveDropPayload(dataTransfer);
    try {
      if (payload.kind === "directory") {
        await loadDroppedDirectory(payload);
      } else {
        await loadDroppedFile(payload.file);
      }
    } finally {
      setBusy(false);
    }
  }

  function hasFiles(event) {
    return Array.from(event.dataTransfer?.types || []).includes("Files");
  }

  async function downloadStl() {
    if (state.busy || hasPendingMeshChanges()) {
      return;
    }

    const exportDir = "/data/export";
    const baseName = state.loadedLabel || "mesh";
    const exportPath = pathJoin(exportDir, `${baseName}.stl`);

    try {
      setBusy(true, "Preparing STL download...");
      removePathRecursive(exportDir);
      ensureDirTree(exportDir);
      await waitForNextPaint();

      if (typeof Module.exportStl !== "function" || !Module.exportStl(exportPath)) {
        throw new Error(getModuleError("Failed to export STL."));
      }

      const bytes = Module.FS.readFile(exportPath);
      const blob = new Blob([bytes], { type: "model/stl" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = `${baseName}.stl`;
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 0);
      setStatus(`Downloaded STL: ${baseName}.stl`, false);
    } catch (error) {
      console.error("[UI] STL export failed:", error);
      setStatus(error.message || "STL export failed.", true);
    } finally {
      setBusy(false);
    }
  }

  function initUI() {
    if (state.initialized) {
      return;
    }
    state.initialized = true;

    const iso = $("iso");
    const isoInput = $("isoInput");
    const isoStepDown = $("isoStepDown");
    const isoStepUp = $("isoStepUp");
    const largestComponent = $("largestComponent");
    const applyMesh = $("applyMesh");
    const resetBtn = $("reset");
    const downloadBtn = $("download");

    if (!iso || !isoInput || !isoStepDown || !isoStepUp || !largestComponent ||
        !applyMesh || !resetBtn || !downloadBtn) {
      console.error("[UI] Missing DOM elements.");
      return;
    }

    resetBtn.addEventListener("click", () => {
      if (typeof Module.resetCamera === "function") {
        Module.resetCamera();
      }
    });

    downloadBtn.addEventListener("click", () => {
      void downloadStl();
    });

    applyMesh.addEventListener("click", () => {
      void applyPendingMeshSettings();
    });

    if (typeof Module.getKeepLargestComponent === "function") {
      state.renderedKeepLargestComponent = !!Module.getKeepLargestComponent();
      state.pendingKeepLargestComponent = state.renderedKeepLargestComponent;
    }

    largestComponent.addEventListener("change", () => {
      state.pendingKeepLargestComponent = largestComponent.checked;
      updateMeshControls();
      updateDownloadState();
      setStatus("Mesh option staged. Apply to rebuild the surface.", false);
    });

    iso.addEventListener("input", () => {
      setPendingIsoValue(Number(iso.value), false);
      setStatus(
        `Pending iso ${formatNumber(state.pendingIsoValue)}. Apply to rebuild the surface.`,
        false
      );
    });

    iso.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        void applyPendingMeshSettings();
      }
    });

    isoStepDown.addEventListener("click", () => {
      adjustPendingIso(-getNudgeStep());
    });

    isoStepUp.addEventListener("click", () => {
      adjustPendingIso(getNudgeStep());
    });

    isoInput.addEventListener("focus", () => {
      state.editingIsoInput = true;
      state.isoInputDraft = isoInput.value;
    });

    isoInput.addEventListener("input", () => {
      state.isoInputDraft = isoInput.value;
      const parsedValue = Number(isoInput.value);
      if (!Number.isFinite(parsedValue)) {
        return;
      }
      setPendingIsoValue(parsedValue, true);
      setStatus(
        `Pending iso ${formatNumber(state.pendingIsoValue)}. Apply to rebuild the surface.`,
        false
      );
    });

    isoInput.addEventListener("blur", () => {
      state.editingIsoInput = false;
      const parsedValue = Number(isoInput.value);
      if (Number.isFinite(parsedValue)) {
        setPendingIsoValue(parsedValue, false);
      }
      updateMeshControls();
    });

    isoInput.addEventListener("keydown", (event) => {
      if (event.key !== "Enter") {
        return;
      }
      event.preventDefault();
      state.editingIsoInput = false;
      const parsedValue = Number(isoInput.value);
      if (Number.isFinite(parsedValue)) {
        setPendingIsoValue(parsedValue, false);
      }
      void applyPendingMeshSettings();
      isoInput.blur();
    });

    document.addEventListener("dragenter", (event) => {
      if (!hasFiles(event) || state.busy) {
        return;
      }
      event.preventDefault();
      state.dragDepth += 1;
      setDragActive(true);
    });

    document.addEventListener("dragover", (event) => {
      if (!hasFiles(event) || state.busy) {
        return;
      }
      event.preventDefault();
      event.dataTransfer.dropEffect = "copy";
      setDragActive(true);
    });

    document.addEventListener("dragleave", (event) => {
      if (!hasFiles(event) || state.busy) {
        return;
      }
      event.preventDefault();
      state.dragDepth = Math.max(0, state.dragDepth - 1);
      if (state.dragDepth === 0) {
        setDragActive(false);
      }
    });

    document.addEventListener("drop", (event) => {
      if (!hasFiles(event)) {
        return;
      }
      event.preventDefault();
      state.dragDepth = 0;
      setDragActive(false);

      void handleDrop(event.dataTransfer).catch((error) => {
        console.error("[UI] drop failed:", error);
        setBusy(false);
        setStatus(error.message || "Drop failed.", true);
      });
    });

    state.isoInputDraft = formatInputNumber(state.pendingIsoValue);
    setStatus("Drop a `.nii` file or a directory with DICOM slices.", false);
    updateScalarControls();
    updateMeshControls();
    updateDownloadState();
    updateViewportStatus();
  }

  Module.onRuntimeInitialized = () => {
    console.log("[Emscripten] onRuntimeInitialized");
    initUI();
  };

  if (Module.calledRun) {
    initUI();
  }
})();
