(function () {
  function $(id) { return document.getElementById(id); }

  const state = {
    dragDepth: 0,
    busy: false,
    initialized: false,
    loadedLabel: "",
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

  function setBusy(isBusy, message) {
    state.busy = isBusy;
    document.body.classList.toggle("is-busy", isBusy);
    if (message) {
      setStatus(message, false);
    }
    updateDownloadState();
  }

  function setDragActive(isActive) {
    document.body.classList.toggle("is-drag-active", isActive);
  }

  function updateScalarControls() {
    const iso = $("iso");
    const isoValue = $("isoValue");
    const rangeEl = $("range");
    if (!iso || !isoValue || !rangeEl) {
      return;
    }

    const min = Number(Module.getScalarMin());
    const max = Number(Module.getScalarMax());
    const current = Number(Module.getIsoValue());

    iso.min = String(min);
    iso.max = String(max);
    iso.value = String(current);
    isoValue.textContent = formatNumber(current);
    rangeEl.textContent = `Range: [${formatNumber(min)}, ${formatNumber(max)}]`;
  }

  function updateDownloadState() {
    const downloadBtn = $("download");
    if (!downloadBtn) {
      return;
    }
    const hasMesh = typeof Module.hasMesh === "function" && Module.hasMesh();
    downloadBtn.disabled = state.busy || !hasMesh;
  }

  function getModuleError(defaultMessage) {
    if (typeof Module.getLastError !== "function") {
      return defaultMessage;
    }
    return Module.getLastError() || defaultMessage;
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

    await new Promise((resolve) => setTimeout(resolve, 0));

    if (typeof Module.loadNifti !== "function" || !Module.loadNifti(vpath)) {
      throw new Error(getModuleError("Failed to load the dropped file as NIFTI."));
    }

    updateScalarControls();
    updateDownloadState();
    setStatus(`Loaded NIFTI: ${file.name}`, false);
  }

  async function loadDroppedDirectory(payload) {
    const dicomRoot = pathJoin("/data/dicom", payload.rootName || "drop");
    state.loadedLabel = stripExtension(payload.rootName || "mesh");

    setBusy(true, `Copying ${payload.files.length} files from ${payload.rootName}...`);
    await writeFilesToDirectory(dicomRoot, payload.files);

    await new Promise((resolve) => setTimeout(resolve, 0));

    if (typeof Module.loadDicom !== "function" || !Module.loadDicom(dicomRoot)) {
      throw new Error(getModuleError("Failed to load a DICOM series from the dropped directory."));
    }

    updateScalarControls();
    updateDownloadState();
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
    if (state.busy) {
      return;
    }

    const exportDir = "/data/export";
    const baseName = state.loadedLabel || "mesh";
    const exportPath = pathJoin(exportDir, `${baseName}.stl`);

    try {
      setBusy(true, "Preparing STL download...");
      removePathRecursive(exportDir);
      ensureDirTree(exportDir);

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
    const isoValue = $("isoValue");
    const largestComponent = $("largestComponent");
    const resetBtn = $("reset");
    const downloadBtn = $("download");

    if (!iso || !isoValue || !largestComponent || !resetBtn || !downloadBtn) {
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

    if (typeof Module.getKeepLargestComponent === "function") {
      largestComponent.checked = Module.getKeepLargestComponent();
    }

    largestComponent.addEventListener("change", () => {
      if (typeof Module.setKeepLargestComponent === "function") {
        Module.setKeepLargestComponent(largestComponent.checked);
      }
      updateDownloadState();
    });

    iso.addEventListener("input", () => {
      const value = Number(iso.value);
      isoValue.textContent = formatNumber(value);
      if (typeof Module.setIsoValue === "function") {
        Module.setIsoValue(value);
      }
      updateDownloadState();
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

    setStatus("Drop a `.nii` file or a directory with DICOM slices.", false);
    updateScalarControls();
    updateDownloadState();
  }

  Module.onRuntimeInitialized = () => {
    console.log("[Emscripten] onRuntimeInitialized");
    initUI();
  };

  if (Module.calledRun) {
    initUI();
  }
})();
