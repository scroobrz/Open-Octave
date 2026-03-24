import { useEffect, useMemo, useRef, useState } from 'react';
import COLORS from '../../shared/colors.json';
import './App.css';

function pretty(obj) {
  return JSON.stringify(obj, null, 2);
}

async function readErrorPayload(res) {
  try {
    const data = await res.json();
    if (data && typeof data === 'object') return data;
    return { error: String(data) };
  } catch {
    try {
      const text = await res.text();
      return { error: text || `HTTP ${res.status}` };
    } catch {
      return { error: `HTTP ${res.status}` };
    }
  }
}

async function apiGet(path) {
  const res = await fetch(path);
  if (!res.ok) {
    const payload = await readErrorPayload(res);
    const msg =
      typeof payload?.error === 'string' && payload.error.trim()
        ? payload.error
        : `GET ${path} failed: ${res.status} ${res.statusText}`;
    throw new Error(msg);
  }
  return res.json();
}

async function apiPost(path, body) {
  const opts = { method: 'POST' };
  if (body !== undefined) {
    opts.headers = { 'Content-Type': 'application/json' };
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  if (!res.ok) {
    const payload = await readErrorPayload(res);
    const msg =
      typeof payload?.error === 'string' && payload.error.trim()
        ? payload.error
        : `POST ${path} failed: ${res.status} ${res.statusText}`;
    throw new Error(msg);
  }
  return res.json();
}

async function apiDelete(path) {
  const res = await fetch(path, { method: 'DELETE' });
  if (!res.ok) {
    const payload = await readErrorPayload(res);
    throw new Error(payload?.error || `DELETE ${path} failed: ${res.status}`);
  }
  return res.json();
}

// ============ CONSTANTS ============

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const IS_BLACK = [false, true, false, true, false, false, true, false, true, false, true, false];

function keyToNote(globalKeyIndex) {
  const octave = 4 + Math.floor(globalKeyIndex / 12);
  const noteIndex = globalKeyIndex % 12;
  return `${NOTE_NAMES[noteIndex]}${octave}`;
}

const FINGER_OPTIONS = COLORS.fingerOrder.map(f => ({
  finger: f,
  label: f.charAt(0).toUpperCase() + f.slice(1),
  color: COLORS.fingerColors[f]
}));

function Badge({ connected }) {
  const cls = connected ? 'badge badge-online' : 'badge badge-offline';
  const text = connected ? 'Connected' : 'Disconnected';
  return (
    <div className={cls}>
      <span className="badge-dot" />
      {text}
    </div>
  );
}


export default function App() {
  const [tab, setTab] = useState('connect');
  const [uiMode, setUiMode] = useState(() => localStorage.getItem('oo-ui-mode') || 'user');
  const [colorMode, setColorMode] = useState(() => localStorage.getItem('oo-color-mode') || 'default');
  // Persist developer mode toggle across page reloads.
  useEffect(() => {
    localStorage.setItem('oo-ui-mode', uiMode);
    document.documentElement.dataset.theme = uiMode;
  }, [uiMode]);

  useEffect(() => {
    localStorage.setItem('oo-color-mode', colorMode);
  }, [colorMode]);

  // Resolve active finger colours based on colour mode.
  const activeFingerColors = useMemo(() => {
    if (colorMode === 'colorblind' && COLORS.alternativePalettes?.colorblind) {
      return COLORS.alternativePalettes.colorblind.fingerColors;
    }
    return COLORS.fingerColors;
  }, [colorMode]);

  // Map canonical hex -> display hex for rendering swatches/LEDs.
  const colorDisplayMap = useMemo(() => {
    const map = {};
    for (const finger of COLORS.fingerOrder) {
      const original = COLORS.fingerColors[finger].toUpperCase();
      const display = activeFingerColors[finger].toUpperCase();
      map[original] = display;
    }
    return map;
  }, [activeFingerColors]);

  function displayColor(hex) {
    const clean = String(hex || '').trim().toUpperCase().replace('#', '');
    return colorDisplayMap[clean] || clean;
  }

  // Force safe tabs when switching UI modes
  useEffect(() => {
    if (uiMode === 'user' && tab === 'logs') {
      setTab('connect');
    }
  }, [uiMode, tab]);

  // ============ HEALTH / CONNECTION STATE ============
  const [health, setHealth] = useState(null);
  const [healthError, setHealthError] = useState('');

  // ============ MODULES STATE ============
  const [modulesList, setModulesList] = useState([]);

  // ============ CONNECT TAB STATE ============
  const [serialPortPath, setSerialPortPath] = useState('');

  // ============ LOGS STATE ============
  const [logs, setLogs] = useState([]);
  const [tail, setTail] = useState(200);
  const [autoLogs, setAutoLogs] = useState(true);
  const [logsPrefixedOnly, setLogsPrefixedOnly] = useState(false);
  const [logsSearch, setLogsSearch] = useState('');
  const [logsCopyMsg, setLogsCopyMsg] = useState('');

  const [lastResult, setLastResult] = useState(null);

  // ============ CONTROLLER STATE MIRROR ============
  const [ctrlState, setCtrlState] = useState(null);
  const [ctrlStateError, setCtrlStateError] = useState('');

  // ============ SYNC STATE (developer) ============
  const [syncState, setSyncState] = useState({
    running: false,
    phase: 'idle',
    statusOk: null,
    seqListOk: null,
    statusResp: null,
    seqListResp: null,
    error: ''
  });

  // ============ SEQUENCES STATE ============
  const [dbSeqItems, setDbSeqItems] = useState([]);
  const [dbSeqError, setDbSeqError] = useState('');
  const [dbActionBusy, setDbActionBusy] = useState(false);

  // Per-chain selected sequence (moduleIp -> sequenceId)
  const [chainSequences, setChainSequences] = useState({});
  // Per-chain cached full sequence data (moduleIp -> { steps: [...] })
  const [chainSeqData, setChainSeqData] = useState({});

  // ============ SEQUENCE EDITOR STATE ============
  const [editorOpen, setEditorOpen] = useState(false);
  const [editorSeqId, setEditorSeqId] = useState(null); // null = new sequence
  const [editorName, setEditorName] = useState('');
  const [editorDesc, setEditorDesc] = useState('');
  const [editorSteps, setEditorSteps] = useState([]);
  const [editorErrors, setEditorErrors] = useState({});
  const [editorSaving, setEditorSaving] = useState(false);
  const [editorEditingStep, setEditorEditingStep] = useState(null); // index or null
  const dragStepRef = useRef(null);
  const dragOverStepRef = useRef(null);
  const [editStepKeys, setEditStepKeys] = useState([0]);
  const [editStepColors, setEditStepColors] = useState([COLORS.fingerColors.thumb]);
  const [editStepDuration, setEditStepDuration] = useState(300);

  // ============ MIDI IMPORT STATE ============
  const [midiFile, setMidiFile] = useState(null);
  const [midiImportBusy, setMidiImportBusy] = useState(false);
  const [midiImportError, setMidiImportError] = useState('');
  const [midiImportResult, setMidiImportResult] = useState(null);

  // ============ SEQUENCE DETAILS MODAL (user mode) ============
  const [seqModalOpen, setSeqModalOpen] = useState(false);
  const [seqModalLoading, setSeqModalLoading] = useState(false);
  const [seqModalError, setSeqModalError] = useState('');
  const [seqModalSeq, setSeqModalSeq] = useState(null);

  // ============ DB CREATE (developer JSON editor) ============
  const defaultTemplate = {
    name: 'My Sequence',
    description: 'Optional description',
    steps: [
      { keys: [0], colors: [COLORS.fingerColors.thumb], duration: 300 },
      { keys: [4], colors: [COLORS.fingerColors.index], duration: 300 }
    ]
  };
  const [dbCreateJson, setDbCreateJson] = useState(pretty(defaultTemplate));
  const [dbCreateMsg, setDbCreateMsg] = useState('');
  const [dbCreateErr, setDbCreateErr] = useState('');
  const [dbCreateBusy, setDbCreateBusy] = useState(false);

  // Derive hex->friendly-name and hex->finger-name maps from colors.json
  const { HEX_TO_NAME, HEX_TO_FINGER } = useMemo(() => {
    const nameMap = {};
    const fingerMap = {};
    const allPalettes = [
      COLORS.fingerColors,
      ...(COLORS.alternativePalettes
        ? Object.values(COLORS.alternativePalettes).map(p => p.fingerColors)
        : [])
    ];
    for (const finger of COLORS.fingerOrder) {
      const displayName = COLORS.fingerDisplayNames[finger];
      const fingerLabel = finger.charAt(0).toUpperCase() + finger.slice(1);
      for (const palette of allPalettes) {
        const hex = palette[finger]?.toUpperCase();
        if (hex) {
          nameMap[hex] = displayName;
          fingerMap[hex] = fingerLabel;
        }
      }
    }
    return { HEX_TO_NAME: nameMap, HEX_TO_FINGER: fingerMap };
  }, []);

  function hexColorName(hex) {
    const clean = String(hex || '').trim().toUpperCase().replace('#', '');
    return HEX_TO_NAME[clean] || hex;
  }

  function renderColorSwatch(hex) {
    const clean = String(hex || '').trim();
    const mapped = displayColor(clean);
    const css = mapped ? `#${mapped.replace('#', '')}` : '#000000';
    return (
      <span
        className="swatch"
        title={mapped || 'n/a'}
        style={{ backgroundColor: css }}
      />
    );
  }

  // ============ COMPUTED ============

  const isConnected = useMemo(() => {
    return modulesList.some(m => m.connected);
  }, [modulesList]);

  const connectedModuleCount = useMemo(() => {
    return modulesList.filter(m => m.connected).length;
  }, [modulesList]);

  const connectedPhysicalModuleCount = useMemo(() => {
    return modulesList
      .filter(m => m.connected)
      .reduce((sum, m) => sum + (Number.isFinite(m.chainLength) ? m.chainLength : 1), 0);
  }, [modulesList]);

  const smallestChainKeys = useMemo(() => {
    const connected = modulesList.filter(m => m.connected);
    if (connected.length === 0) return 0;
    return Math.min(...connected.map(m => m.totalKeys));
  }, [modulesList]);

  const displayedLogs = useMemo(() => {
    let items = Array.isArray(logs) ? logs : [];
    if (logsPrefixedOnly) {
      items = items.filter((x) => {
        const m = String(x.message || '');
        return m.startsWith('ACK ') || m.startsWith('STATUS ') || m.startsWith('EVT ') || m.startsWith('ERR ');
      });
    }
    const q = String(logsSearch || '').trim().toLowerCase();
    if (q) {
      items = items.filter((x) => {
        const line = `[${x.ts}] ${x.source}: ${x.message}`.toLowerCase();
        return line.includes(q);
      });
    }
    return items;
  }, [logs, logsPrefixedOnly, logsSearch]);

  // ============ API FUNCTIONS ============

  async function refreshHealth() {
    try {
      setHealthError('');
      const data = await apiGet('/api/health');
      setHealth(data);
    } catch (e) {
      setHealth(null);
      setHealthError(`Failed to load /api/health: ${e.message}`);
    }
  }

  async function refreshModules() {
    try {
      const data = await apiGet('/api/modules');
      const nextModules = Array.isArray(data?.modules) ? data.modules : [];
      setModulesList(nextModules);

      // module visualisation cache should only keep connected modules that still exist
      // in the current controller snapshot. This prevents stale mock/local state from
      // reusing cached sequence data for an old connection.
      const activeIps = new Set(nextModules.filter(m => m.connected).map(m => m.ip));

      setChainSequences(prev => {
        const next = {};
        for (const [ip, sequenceId] of Object.entries(prev)) {
          if (activeIps.has(ip)) next[ip] = sequenceId;
        }
        return next;
      });

      setChainSeqData(prev => {
        const next = {};
        for (const [ip, value] of Object.entries(prev)) {
          if (activeIps.has(ip)) next[ip] = value;
        }
        return next;
      });
    } catch {
      // Demo stability patch: keep the last known module state on brief API hiccups
      // so the UI does not flicker or collapse controls during short disconnects.
    }
  }

  async function refreshLogs() {
    const safeTail = Number.isFinite(tail) ? Math.max(1, Math.min(500, Math.floor(tail))) : 200;
    try {
      const data = await apiGet(`/api/logs?tail=${safeTail}`);
      setLogs(data.items || []);
    } catch {
      setLogs([]);
    }
  }

  async function refreshControllerState() {
    try {
      setCtrlStateError('');
      const data = await apiGet('/api/state');
      setCtrlState(data?.state || null);
    } catch (e) {
      setCtrlState(null);
      setCtrlStateError(`Failed to load /api/state: ${e.message}`);
    }
  }

  async function refreshDbSequences() {
    try {
      setDbSeqError('');
      const data = await apiGet('/api/db/sequences');
      setDbSeqItems(Array.isArray(data?.items) ? data.items : []);
    } catch (e) {
      setDbSeqItems([]);
      setDbSeqError(`Failed to load /api/db/sequences: ${e.message}`);
    }
  }

  async function seedDemoSequences() {
    try {
      setDbActionBusy(true);
      setDbSeqError('');
      await apiPost('/api/db/sequences/seed');
      await refreshDbSequences();
    } catch (e) {
      setDbSeqError(`Failed to seed demo sequences: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  // ============ MODULE ACTIONS ============

  async function fetchAndCacheSeqData(moduleIp, sequenceId) {
    try {
      const data = await apiGet(`/api/db/sequences/${encodeURIComponent(sequenceId)}`);
      const steps = data?.item?.data?.steps || [];
      setChainSeqData(prev => ({
        ...prev,
        [moduleIp]: {
          sequenceId: String(sequenceId),
          steps
        }
      }));
    } catch {
      // Non-critical — keyboard just won't highlight
    }
  }

  async function uploadSequenceToModule(moduleIp, sequenceId) {
    try {
      setDbActionBusy(true);
      await apiPost(`/api/modules/${encodeURIComponent(moduleIp)}/upload`, {
        sequenceId,
        colorMode
      });
      setChainSequences(prev => ({ ...prev, [moduleIp]: sequenceId }));
      await fetchAndCacheSeqData(moduleIp, sequenceId);
      await refreshModules();
    } catch (e) {
      setDbSeqError(`Upload failed: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  async function uploadSequenceToAll(sequenceId) {
    try {
      setDbActionBusy(true);
      await apiPost('/api/modules/all/upload', {
        sequenceId,
        colorMode
      });
      const newChains = {};
      for (const m of modulesList) {
        if (m.connected) newChains[m.ip] = sequenceId;
      }
      setChainSequences(prev => ({ ...prev, ...newChains }));
      // Fetch full sequence data once, cache for all modules
      try {
        const data = await apiGet(`/api/db/sequences/${encodeURIComponent(sequenceId)}`);
        const steps = data?.item?.data?.steps || [];
        const newSeqData = {};
        for (const m of modulesList) {
          if (m.connected) {
            newSeqData[m.ip] = {
              sequenceId: String(sequenceId),
              steps
            };
          }
        }
        setChainSeqData(prev => ({ ...prev, ...newSeqData }));
      } catch { /* Non-critical */ }
      await refreshModules();
    } catch (e) {
      setDbSeqError(`Upload to all failed: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  async function sendModuleControl(moduleIp, cmd, mode) {
    try {
      await apiPost(`/api/modules/${encodeURIComponent(moduleIp)}/control`, { cmd, mode });
      await refreshModules();
    } catch (e) {
      setDbSeqError(`Control failed: ${e.message}`);
    }
  }

  // async function setModuleOctaveOffset(moduleIp, octaveOffset) {
  //   try {
  //     await apiPost(`/api/modules/${encodeURIComponent(moduleIp)}/octave-offset`, {
  //       octaveOffset
  //     });
  //     await refreshModules();
  //   } catch (e) {
  //     setDbSeqError(`Octave offset update failed: ${e.message}`);
  //   }
  // }

  async function sendAllControl(cmd, mode) {
    try {
      await apiPost('/api/modules/all/control', { cmd, mode });
      await refreshModules();
    } catch (e) {
      setDbSeqError(`Control all failed: ${e.message}`);
    }
  }

  // ============ CONNECT TAB ACTIONS ============

  async function connectSerial() {
    try {
      const data = await apiPost('/api/connect', {
        transport: 'SERIAL',
        serialPort: serialPortPath
      });
      setLastResult(data);
      await refreshHealth();
      await refreshModules();
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  async function disconnectAll() {
    try {
      const data = await apiPost('/api/disconnect');
      setLastResult(data);
      await refreshHealth();
      await refreshModules();
    } catch (e) {
      setLastResult({ error: e.message });
    }
  }

  // ============ DEVELOPER: SYNC ============

  async function runSync() {
    setSyncState(s => ({ ...s, running: true, phase: 'status', statusOk: null, seqListOk: null, statusResp: null, seqListResp: null, error: '' }));
    try {
      let statusResp = null;
      let statusOk = false;
      try {
        statusResp = await apiGet('/api/status');
        statusOk = !statusResp?.error;
      } catch (e) {
        statusResp = { error: e.message };
      }

      setSyncState(s => ({ ...s, phase: 'seq_list' }));
      let seqListResp = null;
      let seqListOk = false;
      try {
        seqListResp = await apiGet('/api/seq/list');
        seqListOk = !seqListResp?.error;
      } catch (e) {
        seqListResp = { error: e.message };
      }

      setSyncState(s => ({ ...s, running: false, statusOk, seqListOk, statusResp, seqListResp, phase: 'complete' }));
      await refreshHealth();
      await refreshLogs();
      await refreshControllerState();
    } catch (e) {
      setSyncState(s => ({ ...s, running: false, phase: 'idle', error: e.message }));
    }
  }

  // ============ DEVELOPER: JSON EDITOR ============

  async function saveDbSequenceFromJson() {
    try {
      setDbCreateBusy(true);
      setDbCreateMsg('');
      setDbCreateErr('');
      let payload;
      try { payload = JSON.parse(dbCreateJson); } catch {
        setDbCreateErr('Invalid JSON.');
        return;
      }
      if (!payload || typeof payload !== 'object') { setDbCreateErr('JSON must be an object.'); return; }
      if (!payload.name) { setDbCreateErr('Missing "name".'); return; }
      if (!Array.isArray(payload.steps)) { setDbCreateErr('Missing "steps" array.'); return; }

      const data = await apiPost('/api/db/sequences', payload);
      setLastResult(data);
      if (!data?.ok) { setDbCreateErr(data?.error || 'Failed to save.'); return; }
      setDbCreateMsg(`Saved sequence: ${data.item?.id}`);
      await refreshDbSequences();
    } catch (e) {
      setDbCreateErr(e.message);
    } finally {
      setDbCreateBusy(false);
    }
  }

  // ============ MIDI IMPORT ============
  async function importMidiFile() {
    if (!midiFile) {
      setMidiImportError('Please choose a MIDI file first.');
      return;
    }

    try {
      setMidiImportBusy(true);
      setMidiImportError('');
      setMidiImportResult(null);

      const formData = new FormData();
      formData.append('file', midiFile);

      const res = await fetch('/api/midi/import', {
        method: 'POST',
        body: formData
      });

      const data = await res.json();

      if (!res.ok || !data?.ok) {
        throw new Error(data?.error || `MIDI import failed: ${res.status}`);
      }

      setMidiImportResult(data);
      setMidiFile(null);
      await refreshDbSequences();
    } catch (e) {
      setMidiImportError(e.message);
    } finally {
      setMidiImportBusy(false);
    }
  }

  // ============ SEQUENCE EDITOR ============

  function openEditor(seqId) {
    if (seqId) {
      // Edit existing
      const seq = dbSeqItems.find(s => s.id === seqId);
      if (!seq) return;
      // Need to fetch full sequence data
      apiGet(`/api/db/sequences/${seqId}`).then(data => {
        const item = data?.item;
        if (!item) return;
        setEditorSeqId(item.id);
        setEditorName(item.name || '');
        setEditorDesc(item.description || '');
        const steps = item.data?.steps || [];
        setEditorSteps(steps.map(s => ({
          keys: Array.isArray(s.keys) ? [...s.keys] : [s.k ?? 0],
          colors: Array.isArray(s.colors) ? [...s.colors] : [s.c ?? COLORS.fingerColors.thumb],
          duration: s.duration ?? s.d ?? 300
        })));
        setEditorErrors({});
        setEditorEditingStep(null);
        setEditorOpen(true);
      });
    } else {
      // New sequence
      setEditorSeqId(null);
      setEditorName('');
      setEditorDesc('');
      setEditorSteps([]);
      setEditorErrors({});
      setEditorEditingStep(null);
      setEditorOpen(true);
    }
  }

  function closeEditor() {
    setEditorOpen(false);
    setEditorEditingStep(null);
  }

  function startEditStep(idx) {
    const step = editorSteps[idx];
    if (!step) return;
    setEditorEditingStep(idx);
    setEditStepKeys([...step.keys]);
    setEditStepColors([...step.colors]);
    setEditStepDuration(step.duration);
  }

  function startAddStep() {
    setEditorEditingStep('new');
    setEditStepKeys([0]);
    setEditStepColors([COLORS.fingerColors.thumb]);
    setEditStepDuration(300);
  }

  function confirmEditStep() {
    const newStep = {
      keys: editStepKeys.map(Number),
      colors: [...editStepColors],
      duration: Number(editStepDuration)
    };

    if (editorEditingStep === 'new') {
      setEditorSteps(prev => [...prev, newStep]);
    } else if (typeof editorEditingStep === 'number') {
      setEditorSteps(prev => prev.map((s, i) => i === editorEditingStep ? newStep : s));
    }
    setEditorEditingStep(null);
  }

  function cancelEditStep() {
    setEditorEditingStep(null);
  }

  function deleteEditorStep(idx) {
    setEditorSteps(prev => prev.filter((_, i) => i !== idx));
  }


  async function saveEditorSequence() {
    // Validate
    const errors = {};
    if (!editorName.trim()) errors.name = 'Name is required';
    if (editorName.length > 31) errors.name = 'Max 31 characters';
    if (editorName.includes(',')) errors.name = 'Commas not allowed';
    if (editorSteps.length === 0) errors.steps = 'At least 1 step required';
    if (editorSteps.length > 128) errors.steps = 'Max 128 steps';

    for (let i = 0; i < editorSteps.length; i++) {
      const s = editorSteps[i];
      if (!s.keys || s.keys.length === 0) { errors[`step_${i}`] = 'No keys'; continue; }
      if (s.keys.length > 4) { errors[`step_${i}`] = 'Max 4 keys per step'; continue; }
      if (s.keys.some(k => k < 0 || k > 47)) { errors[`step_${i}`] = 'Key index must be 0-47'; continue; }
      if (new Set(s.keys).size !== s.keys.length) { errors[`step_${i}`] = 'Duplicate keys'; continue; }
      if (s.keys.length !== s.colors.length) { errors[`step_${i}`] = 'Keys and colors must match'; continue; }
      if (s.duration < 100 || s.duration > 10000) { errors[`step_${i}`] = 'Duration must be 100-10000ms'; continue; }
    }

    if (Object.keys(errors).length > 0) {
      setEditorErrors(errors);
      return;
    }

    setEditorErrors({});
    setEditorSaving(true);

    try {
      const payload = {
        name: editorName.trim(),
        description: editorDesc.trim(),
        steps: editorSteps
      };
      if (editorSeqId !== null) payload.id = String(editorSeqId);

      const data = await apiPost('/api/db/sequences', payload);
      if (!data?.ok) {
        setEditorErrors({ save: data?.error || 'Failed to save' });
        return;
      }
      await refreshDbSequences();
      closeEditor();
    } catch (e) {
      setEditorErrors({ save: e.message });
    } finally {
      setEditorSaving(false);
    }
  }

  async function deleteSequence(id) {
    if (!confirm(`Delete sequence #${id}?`)) return;
    try {
      setDbActionBusy(true);
      await apiDelete(`/api/db/sequences/${id}`);
      await refreshDbSequences();
    } catch (e) {
      setDbSeqError(`Delete failed: ${e.message}`);
    } finally {
      setDbActionBusy(false);
    }
  }

  // ============ SEQUENCE MODAL (user mode) ============

  function normalizeSequenceForModal(item) {
    if (!item || typeof item !== 'object') return null;
    const out = { ...item };
    let steps = out.steps;
    if (!steps && out.data && Array.isArray(out.data.steps)) steps = out.data.steps;
    if (typeof steps === 'string') { try { steps = JSON.parse(steps); } catch { /* invalid JSON, keep as-is */ } }
    out.steps = Array.isArray(steps) ? steps : [];
    return out;
  }

  async function openSequenceModal(id) {
    if (!id) return;
    setSeqModalOpen(true);
    setSeqModalLoading(true);
    setSeqModalError('');
    setSeqModalSeq(null);
    try {
      const data = await apiGet(`/api/db/sequences/${encodeURIComponent(id)}`);
      const item = data?.item || null;
      if (!item) { setSeqModalError('Not found.'); return; }
      setSeqModalSeq(normalizeSequenceForModal(item));
    } catch (e) {
      setSeqModalError(`Failed to load: ${e.message}`);
    } finally {
      setSeqModalLoading(false);
    }
  }

  function closeSequenceModal() {
    setSeqModalOpen(false);
    setSeqModalSeq(null);
  }

  // ============ TIMERS ============

  const timerRef = useRef(null);
  const stateTimerRef = useRef(null);
  const modulesTimerRef = useRef(null);

  function startAutoLogs() {
    if (timerRef.current) clearInterval(timerRef.current);
    timerRef.current = setInterval(refreshLogs, 2000);
  }
  function stopAutoLogs() {
    if (timerRef.current) { clearInterval(timerRef.current); timerRef.current = null; }
  }
  function startAutoState() {
    if (stateTimerRef.current) clearInterval(stateTimerRef.current);
    timerRef.current;
    stateTimerRef.current = setInterval(refreshControllerState, 3000);
  }
  function stopAutoState() {
    if (stateTimerRef.current) { clearInterval(stateTimerRef.current); stateTimerRef.current = null; }
  }
  function startAutoModules() {
    if (modulesTimerRef.current) clearInterval(modulesTimerRef.current);
    modulesTimerRef.current = setInterval(refreshModules, 5000);
  }
  function stopAutoModules() {
    if (modulesTimerRef.current) { clearInterval(modulesTimerRef.current); modulesTimerRef.current = null; }
  }

  async function copyDisplayedLogs() {
    try {
      const text = displayedLogs.map(x => `[${x.ts}] ${x.source}: ${x.message}`).join('\n');
      if (!text) { setLogsCopyMsg('Nothing to copy.'); return; }
      await navigator.clipboard.writeText(text);
      setLogsCopyMsg(`Copied ${displayedLogs.length} lines.`);
      setTimeout(() => setLogsCopyMsg(''), 2000);
    } catch (e) { setLogsCopyMsg(`Copy failed: ${e.message}`); }
  }

  useEffect(() => {
    refreshHealth();
    refreshModules();
    refreshDbSequences();
    startAutoModules();
    return () => { stopAutoModules(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Only poll logs and controller state in developer mode
  useEffect(() => {
    if (uiMode === 'developer') {
      refreshLogs();
      refreshControllerState();
      if (autoLogs) startAutoLogs();
      startAutoState();
    } else {
      stopAutoLogs();
      stopAutoState();
    }
    return () => { stopAutoLogs(); stopAutoState(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [uiMode, autoLogs, tail]);

  useEffect(() => {
    // When module state refreshes, make sure the keyboard visualisation cache follows
    // the controller's current selected sequence for each connected module.
    for (const mod of modulesList) {
      if (!mod.connected || !mod.currentSequenceId) continue;
      const cached = chainSeqData[mod.ip];
      if (!cached || cached.sequenceId !== String(mod.currentSequenceId)) {
        fetchAndCacheSeqData(mod.ip, mod.currentSequenceId);
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [modulesList]);

  // ============ RENDER HELPERS ============

  // Render a chain of piano keys for module visualization
  function renderChainKeyboard(mod) {
    const totalKeys = mod.totalKeys || 12;
    const modules12 = Math.ceil(totalKeys / 12);

    // Build per-key hit counts and first-seen colors from cached sequence data
    const cachedSeq = chainSeqData[mod.ip];
    const steps = cachedSeq?.steps || [];
    const keyHits = {};
    const keyColors = {};
    for (const s of steps) {
      const keys = Array.isArray(s.keys) ? s.keys : (s.k !== undefined ? [s.k] : []);
      const colors = Array.isArray(s.colors) ? s.colors : (s.c !== undefined ? [s.c] : []);
      for (let ki = 0; ki < keys.length; ki++) {
        const k = keys[ki];
        if (k >= 0 && k < totalKeys) {
          keyHits[k] = (keyHits[k] || 0) + 1;
          if (!keyColors[k] && colors[ki]) keyColors[k] = colors[ki];
        }
      }
    }
    const maxHits = Math.max(...Object.values(keyHits), 1);
    const activeKeyCount = Object.keys(keyHits).length;

    const octaveGroups = [];
    for (let m = 0; m < modules12; m++) {
      const startKey = m * 12;
      const keys = [];
      for (let i = 0; i < 12; i++) {
        const globalIdx = startKey + i;
        if (globalIdx >= totalKeys) break;
        keys.push({
          globalIdx,
          note: keyToNote(globalIdx),
          isBlack: IS_BLACK[i],
          localIdx: i
        });
      }
      octaveGroups.push({ moduleNum: m + 1, keys });
    }

    return (
      <div className="chain-card card" key={mod.ip}>
        <div className="chain-header">
          <div className="chain-header-left">
            <span className={`badge-dot ${mod.connected ? 'dot-green' : 'dot-red'}`} />
            <strong>{mod.label}</strong>
            <span className="chain-info">
              ({mod.chainLength} module{mod.chainLength !== 1 ? 's' : ''}, {totalKeys} keys)
            </span>
          </div>
          <div className="chain-header-right">
            {/* laptop hosting / module power control */}
            <span className={mod.connected ? 'pill pill-green' : 'pill pill-coral'}>
              {mod.connected ? 'Powered' : 'Off'}
            </span>
            {mod.currentSequenceName && (
              <span className="pill pill-teal">{mod.currentSequenceName}</span>
            )}
            {mod.lastStatus?.running && (
              <span className="pill pill-green">{mod.lastStatus.mode}</span>
            )}
          </div>
        </div>

        <div className="chain-keyboard-row" style={{ overflowX: 'auto'}}>
          {octaveGroups.map((group, gi) => (
            <div className="octave-group" key={gi} style={{ minWidth: 560, flex: '0 0 auto' }}>
              <div className="octave-label">Module {group.moduleNum}</div>
              <div className="keyboard-vis" style={{ width: '100%', minWidth: 560 }}>
                {group.keys.filter(k => !k.isBlack).map(k => {
                  const active = keyHits[k.globalIdx] > 0;
                  const color = keyColors[k.globalIdx] ? `#${displayColor(keyColors[k.globalIdx])}` : null;
                  const intensity = (keyHits[k.globalIdx] || 0) / maxHits;
                  return (
                    <div
                      key={k.globalIdx}
                      className={`kb-key kb-white${active ? ' kb-active' : ''}`}
                      style={active ? { '--kb-glow': color, '--kb-intensity': intensity } : undefined}
                    >
                      {active && <span className="kb-led" style={{ backgroundColor: color }} />}
                      <span className="kb-note">{k.note}</span>
                    </div>
                  );
                })}
                {group.keys.filter(k => k.isBlack).map(k => {
                  const active = keyHits[k.globalIdx] > 0;
                  const color = keyColors[k.globalIdx] ? `#${displayColor(keyColors[k.globalIdx])}` : null;
                  const intensity = (keyHits[k.globalIdx] || 0) / maxHits;
                  const BLACK_OFFSETS = { 1: 0.5, 3: 1.5, 6: 3.5, 8: 4.5, 10: 5.5 };
                  const offset = BLACK_OFFSETS[k.localIdx];
                  const leftPercent = ((offset + 0.5) / 7) * 100;
                  return (
                    <div
                      key={k.globalIdx}
                      className={`kb-key kb-black${active ? ' kb-active' : ''}`}
                      style={{
                        left: `${leftPercent}%`,
                        ...(active ? { '--kb-glow': color, '--kb-intensity': intensity } : {})
                      }}
                    >
                      {active && <span className="kb-led" style={{ backgroundColor: color }} />}
                      <span className="kb-note">{k.note}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          ))}
        </div>

        {steps.length > 0 ? (
          <div className="hint">
            <b>{steps.length}</b> steps loaded for this chain.
          </div>
        ) : (
          <div className="hint">Select a sequence to preview the notes used.</div>
        )}

        {/* Per-chain controls */}
        <div className="chain-controls">
        <div className="chain-controls-left" style={{ display: 'flex', flexDirection: 'column', gap: 10, alignItems: 'flex-start', textAlign: 'left' }}>

            <div className="row" style={{ gap: 10, alignItems: 'center', flexWrap: 'wrap', justifyContent: 'flex-start', width: '100%' }}>
              <label className="label" style={{ marginBottom: 0 }}>Sequence</label>
              <select
                className="input chain-seq-select"
                style={{ maxWidth: 300 }}
                value={chainSequences[mod.ip] || mod.currentSequenceId || ''}
                onChange={(e) => {
                  const val = e.target.value;
                  if (val) uploadSequenceToModule(mod.ip, val);
                }}
                disabled={dbActionBusy || !mod.connected}
              >
                <option value="">Select sequence...</option>
                {dbSeqItems
                  .filter(s => s.maxKey < totalKeys)
                  .map(s => (
                    <option key={s.id} value={s.id}>
                      {s.name} ({s.stepCount} steps)
                    </option>
                  ))}
              </select>
            </div>
          </div>
          <div className="chain-controls-right">
            <button className="btn btn-sm btn-green" disabled={!mod.connected} onClick={() => sendModuleControl(mod.ip, 'start', 'guided')}>
              Practice
            </button>
            <button className="btn btn-sm" disabled={!mod.connected} onClick={() => sendModuleControl(mod.ip, 'start', 'teaching')}>
              Watch & Learn
            </button>
            <button className="btn btn-sm btn-coral" disabled={!mod.connected} onClick={() => sendModuleControl(mod.ip, 'stop')}>
              Stop
            </button>
            <button className="btn btn-sm btn-secondary" disabled={!mod.connected} onClick={() => sendModuleControl(mod.ip, 'power_toggle')}>
              Power
            </button>
          </div>
        </div>
      </div>
    );
  }

  // Render the step editor row (inline)
  function renderStepEditRow() {
    return (
      <tr className="step-edit-row">
        <td colSpan={6}>
          <div className="step-edit-form">
            <div className="step-edit-keys">
              <label className="label">Keys:</label>
              {editStepKeys.map((k, ki) => (
                <div key={ki} className="step-key-input-group">
                  <input
                    type="number"
                    className="input input-small"
                    min={0}
                    max={47}
                    value={k}
                    onChange={(e) => {
                      const newKeys = [...editStepKeys];
                      newKeys[ki] = Number(e.target.value);
                      setEditStepKeys(newKeys);
                    }}
                  />
                  <span className="step-key-note">{keyToNote(k)}</span>
                  <select
                    className="input step-color-select"
                    value={editStepColors[ki] || COLORS.fingerColors.thumb}
                    onChange={(e) => {
                      const newColors = [...editStepColors];
                      newColors[ki] = e.target.value;
                      setEditStepColors(newColors);
                    }}
                  >
                    {FINGER_OPTIONS.map(f => (
                      <option key={f.finger} value={activeFingerColors[f.finger]}>
                        {f.label}
                      </option>
                    ))}
                  </select>
                  {renderColorSwatch(editStepColors[ki])}
                  {editStepKeys.length > 1 && (
                    <button
                      className="btn btn-sm btn-coral"
                      type="button"
                      onClick={() => {
                        setEditStepKeys(prev => prev.filter((_, i) => i !== ki));
                        setEditStepColors(prev => prev.filter((_, i) => i !== ki));
                      }}
                    >
                      X
                    </button>
                  )}
                </div>
              ))}
              {editStepKeys.length < 4 && (
                <button
                  className="btn btn-sm btn-secondary"
                  type="button"
                  onClick={() => {
                    setEditStepKeys(prev => [...prev, 0]);
                    setEditStepColors(prev => [...prev, COLORS.fingerColors.thumb]);
                  }}
                >
                  + Key
                </button>
              )}
            </div>
            <div className="step-edit-duration">
              <label className="label">Duration (ms):</label>
              <input
                type="number"
                className="input input-small"
                min={100}
                max={10000}
                value={editStepDuration}
                onChange={(e) => setEditStepDuration(Number(e.target.value))}
              />
            </div>
            <div className="step-edit-actions">
              <button className="btn btn-sm btn-green" type="button" onClick={confirmEditStep}>Confirm</button>
              <button className="btn btn-sm btn-secondary" type="button" onClick={cancelEditStep}>Cancel</button>
            </div>
          </div>
        </td>
      </tr>
    );
  }

  // ============ RENDER ============

  // Determine tabs based on UI mode
  const userTabs = [
    { id: 'connect', label: 'Connect' },
    { id: 'modules', label: 'Modules' },
    { id: 'sequences', label: 'Sequences' }
  ];

  const devTabs = [
    { id: 'connect', label: 'Connect' },
    { id: 'modules', label: 'Modules' },
    { id: 'sequences', label: 'Sequences' },
    { id: 'logs', label: 'Logs' }
  ];

  const tabs = uiMode === 'developer' ? devTabs : userTabs;

  return (
    <div className="app">
      <aside className="sidebar">
        <div className="brand">
          <img src="/open-octave-logo.png" alt="Open Octave" className="brand-logo" />
          <div className="brand-text">
            <div className="brand-title">Open Octave</div>
            <div className="brand-subtitle">
              {uiMode === 'user' ? 'User Interface' : 'Developer Interface'}
            </div>
          </div>

          <div className="btn-row" style={{ justifyContent: 'center', marginTop: 10 }}>
            <button className={uiMode === 'user' ? 'btn' : 'btn btn-secondary'} type="button" onClick={() => setUiMode('user')}>User</button>
            <button className={uiMode === 'developer' ? 'btn' : 'btn btn-secondary'} type="button" onClick={() => setUiMode('developer')}>Developer</button>
          </div>
        </div>

        <nav className="nav">
          {tabs.map(t => (
            <button
              key={t.id}
              className={tab === t.id ? 'nav-btn is-active' : 'nav-btn'}
              onClick={() => setTab(t.id)}
            >
              {t.label}
            </button>
          ))}
        </nav>

        <div className="sidebar-footer">
          <Badge connected={isConnected} />
          {connectedModuleCount > 0 && (
            <div className="sidebar-module-count">
              {connectedModuleCount} controller{connectedModuleCount !== 1 ? 's' : ''} connected
              <br />
              {connectedPhysicalModuleCount} physical module{connectedPhysicalModuleCount !== 1 ? 's' : ''}
            </div>
          )}
        </div>
      </aside>

      <main className="main">

        {/* ============ CONNECT TAB ============ */}
        {tab === 'connect' && (
          <section className="panel">
            <h1>Connect</h1>

            <div className="card card-accent-teal">
              <h2>{uiMode === 'developer' ? 'WebSocket Server' : 'Connected Modules'}</h2>
              <div className="row">
                <div className="pill-row">
                  {uiMode === 'developer' && (
                    <span className={isConnected ? 'pill pill-green' : 'pill pill-muted'}>
                      WS Server: Port {health?.wsServerPort || 81}
                    </span>
                  )}
                  <span className={connectedModuleCount > 0 ? 'pill pill-green' : 'pill pill-coral'}>
                    {connectedModuleCount} controller{connectedModuleCount !== 1 ? 's' : ''} connected
                  </span>
                  <span className={connectedPhysicalModuleCount > 0 ? 'pill pill-green' : 'pill pill-muted'}>
                    {connectedPhysicalModuleCount} physical module{connectedPhysicalModuleCount !== 1 ? 's' : ''}
                  </span>
                </div>
                <button className="btn btn-secondary" onClick={() => { refreshHealth(); refreshModules(); }} type="button">
                  Refresh
                </button>
              </div>

              {connectedModuleCount > 0 && (
                <div className="mt">
                  <table className="seq-table">
                    <thead>
                      <tr>
                        <th>Controller</th>
                        {uiMode === 'developer' && <th>IP</th>}
                        <th>Physical chain</th>
                        <th>Status</th>
                      </tr>
                    </thead>
                    <tbody>
                      {modulesList.filter(m => m.connected).map(m => (
                        <tr key={m.ip}>
                          <td>{m.label}</td>
                          {uiMode === 'developer' && <td><code>{m.ip}</code></td>}
                          <td>{m.chainLength} module{m.chainLength !== 1 ? 's' : ''} ({m.totalKeys} keys)</td>
                          <td><span className="pill pill-green">Connected</span></td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}

              <div className="hint">
                Controllers connect to the server automatically via WiFi. A chained pair should appear as 1 controller with 2 physical modules.
              </div>
            </div>

            {/* Serial fallback */}
            {uiMode === 'developer' && (
              <details className="diagnostics">
                <summary>Serial Fallback (debugging)</summary>
                <div className="card">
                  <h2>Serial Connection</h2>
                  <div className="row">
                    <input
                      className="input"
                      value={serialPortPath}
                      onChange={(e) => setSerialPortPath(e.target.value)}
                      placeholder="Serial port path (e.g. /dev/cu.usbmodemXXXX)"
                    />
                    <div className="btn-row">
                      <button className="btn btn-green" onClick={connectSerial} type="button">Connect Serial</button>
                      <button className="btn btn-coral" onClick={disconnectAll} type="button">Disconnect</button>
                    </div>
                  </div>
                  <div className="hint">
                    Serial is for single-module debugging only. In production, modules connect via WiFi.
                  </div>
                </div>
              </details>
            )}

            {healthError && <pre className="pre">{healthError}</pre>}

            {/* Developer-only diagnostics */}
            {uiMode === 'developer' && (
              <details className="diagnostics">
                <summary>Diagnostics</summary>

                <div className="card">
                  <h2>Sync</h2>
                  <div className="row">
                    <div className="pill-row">
                      <span className={syncState.statusOk === null ? 'pill pill-muted' : syncState.statusOk ? 'pill pill-green' : 'pill pill-coral'}>
                        Status: {syncState.statusOk === null ? '--' : syncState.statusOk ? 'OK' : 'FAILED'}
                      </span>
                      <span className={syncState.seqListOk === null ? 'pill pill-muted' : syncState.seqListOk ? 'pill pill-green' : 'pill pill-coral'}>
                        Seq: {syncState.seqListOk === null ? '--' : syncState.seqListOk ? 'OK' : 'FAILED'}
                      </span>
                    </div>
                    <button className="btn" onClick={runSync} disabled={!isConnected || syncState.running} type="button">
                      {syncState.running ? 'Syncing...' : 'Run Sync'}
                    </button>
                  </div>
                </div>

                <div className="card">
                  <h2>Controller State</h2>
                  {ctrlStateError ? <pre className="pre">{ctrlStateError}</pre> : (
                    <pre className="pre">{ctrlState ? pretty(ctrlState) : '(not loaded)'}</pre>
                  )}
                </div>

                <div className="card">
                  <h2>Raw Health</h2>
                  <pre className="pre">{health ? pretty(health) : '(not loaded)'}</pre>
                </div>

                <div className="card">
                  <h2>Last API Result</h2>
                  <pre className="pre">{lastResult ? pretty(lastResult) : '(none yet)'}</pre>
                </div>
              </details>
            )}
          </section>
        )}

        {/* ============ MODULES TAB ============ */}
        {tab === 'modules' && (
          <section className="panel">
            <h1>Modules</h1>

            {/* Global Actions Bar */}
            <div className="card card-accent-teal">
              <h2>Global Controls</h2>
              <div className="row">
                <div className="btn-row">
                  <button className="btn btn-green" disabled={!isConnected} onClick={() => sendAllControl('start', 'guided')}>
                    Start All -- Guided
                  </button>
                  <button className="btn" disabled={!isConnected} onClick={() => sendAllControl('start', 'teaching')}>
                    Start All -- Teaching
                  </button>
                  <button className="btn btn-coral" disabled={!isConnected} onClick={() => sendAllControl('stop')}>
                    Stop All
                  </button>
                  <button className="btn btn-secondary" disabled={!isConnected} onClick={() => sendAllControl('power_toggle')}>
                    On/Off All
                  </button>
                </div>
              </div>

              {isConnected && dbSeqItems.length > 0 && (
                <div className="row mt">
                  <div className="label">Upload to All:</div>
                  <select
                    className="input chain-seq-select"
                    onChange={(e) => {
                      if (e.target.value) uploadSequenceToAll(e.target.value);
                    }}
                    value=""
                    disabled={dbActionBusy}
                  >
                    <option value="">Select sequence...</option>
                    {dbSeqItems
                      .filter(s => smallestChainKeys > 0 && s.maxKey < smallestChainKeys)
                      .map(s => (
                        <option key={s.id} value={s.id}>
                          {s.name} ({s.stepCount} steps)
                        </option>
                      ))}
                  </select>
                </div>
              )}
            </div>

            {dbSeqError && <pre className="pre">{dbSeqError}</pre>}

            {/* Module visualization */}
            {modulesList.filter(m => m.connected).length === 0 ? (
              <div className="card">
                <div className="hint">No modules connected. Modules connect automatically via WiFi.</div>
              </div>
            ) : (
              modulesList.filter(m => m.connected).map(mod => renderChainKeyboard(mod))
            )}

            {uiMode === 'developer' && (
              <details className="diagnostics">
                <summary>Test Controls</summary>
                <div className="card">
                  <h2>Hardware Tests</h2>
                  <div className="btn-row">
                    {modulesList.filter(m => m.connected).map(m => (
                      <div key={m.ip} className="btn-row">
                        <span className="label">{m.label}:</span>
                        <button className="btn btn-sm" disabled={!m.connected} onClick={() => sendModuleControl(m.ip, 'led_test')}>Test LEDs</button>
                        <button className="btn btn-sm" disabled={!m.connected} onClick={() => sendModuleControl(m.ip, 'servo_test')}>Test Servos</button>
                      </div>
                    ))}
                  </div>
                </div>
              </details>
            )}
          </section>
        )}

        {/* ============ SEQUENCES TAB ============ */}
        {tab === 'sequences' && (
          <section className="panel">
            <div className="panel-top-row">
              <h1>Sequences</h1>
              <div className="btn-row">
                <button className="btn btn-green" onClick={() => openEditor(null)} type="button">
                  New Sequence
                </button>
                {uiMode === 'developer' && (
                  <>
                    <button className="btn btn-secondary" onClick={refreshDbSequences} type="button" disabled={dbActionBusy}>
                      Refresh
                    </button>
                    <button className="btn btn-coral" onClick={seedDemoSequences} type="button" disabled={dbActionBusy}>
                      Seed demos
                    </button>
                  </>
                )}
              </div>
            </div>

            {/* midi import */}
            <div className="card card-accent-teal">
              <h2>Import MIDI</h2>
              <div className="row" style={{ alignItems: 'flex-end', gap: 12, flexWrap: 'wrap' }}>
                <div style={{ flex: 1, minWidth: 260 }}>
                  <label className="label">Choose a .mid or .midi file</label>
                  <input
                    className="input"
                    type="file"
                    accept=".mid,.midi"
                    onChange={(e) => {
                      const file = e.target.files && e.target.files[0] ? e.target.files[0] : null;
                      setMidiFile(file);
                      setMidiImportError('');
                      setMidiImportResult(null);
                    }}
                  />
                  <div className="hint">
                    Best with melody-only MIDI. Supports up to 2 modules and auto-transposes by octave when possible. Dense full arrangements may fail.
                  </div>
                </div>

                <div className="btn-row">
                  <button
                    className="btn btn-green"
                    type="button"
                    onClick={importMidiFile}
                    disabled={midiImportBusy || !midiFile}
                  >
                    {midiImportBusy ? 'Importing...' : 'Import MIDI'}
                  </button>
                </div>
              </div>

              {midiFile && (
                <div className="hint">
                  Selected file: <b>{midiFile.name}</b>
                </div>
              )}

              {midiImportError && <pre className="pre">{midiImportError}</pre>}

              {midiImportResult?.ok && (
                <div className="card" style={{ marginTop: 12, marginBottom: 0 }}>
                  <h2>Import Result</h2>
                  <div className="hint" style={{ marginTop: 0 }}>
                    <b>{midiImportResult.item?.name}</b> imported successfully.
                  </div>

                  <table className="seq-table" style={{ marginTop: 10 }}>
                    <tbody>
                      <tr>
                        <td><b>Sequence ID</b></td>
                        <td>{midiImportResult.item?.id ?? '--'}</td>
                      </tr>
                      <tr>
                        <td><b>Required modules</b></td>
                        <td>{midiImportResult.meta?.requiredModules ?? '--'}</td>
                      </tr>
                      <tr>
                        <td><b>Steps</b></td>
                        <td>{midiImportResult.meta?.stepCount ?? '--'}</td>
                      </tr>
                      <tr>
                        <td><b>Notes imported</b></td>
                        <td>{midiImportResult.meta?.noteCount ?? '--'}</td>
                      </tr>
                      <tr>
                        <td><b>Key range</b></td>
                        <td>
                          {typeof midiImportResult.meta?.minKey === 'number' &&
                           typeof midiImportResult.meta?.maxKey === 'number'
                            ? `${keyToNote(midiImportResult.meta.minKey)} - ${keyToNote(midiImportResult.meta.maxKey)}`
                            : '--'}
                        </td>
                      </tr>
                      <tr>
                        <td><b>Transpose</b></td>
                        <td>
                          {typeof midiImportResult.meta?.transposeSemitones === 'number'
                            ? midiImportResult.meta.transposeSemitones === 0
                              ? 'None'
                              : `${midiImportResult.meta.transposeSemitones > 0 ? '+' : ''}${midiImportResult.meta.transposeSemitones} semitones`
                            : '--'}
                        </td>
                      </tr>
                    </tbody>
                  </table>

                  {Array.isArray(midiImportResult.warnings) && midiImportResult.warnings.length > 0 && (
                    <div style={{ marginTop: 12 }}>
                      <div className="label" style={{ marginBottom: 6 }}>Import warnings</div>
                      <pre className="pre" style={{ marginTop: 0 }}>
{midiImportResult.warnings.join('\n')}
                      </pre>
                    </div>
                  )}

                  <div className="btn-row" style={{ marginTop: 12 }}>
                    <button
                      className="btn btn-secondary"
                      type="button"
                      onClick={() => openEditor(midiImportResult.item?.id)}
                    >
                      Open in Editor
                    </button>
                  </div>
                </div>
              )}
            </div>

            {dbSeqError && <pre className="pre">{dbSeqError}</pre>}

            {/* Sequence Library Table */}
            <div className="card card-accent-gold">
              <h2>Sequence Library</h2>

              {dbSeqItems.length > 0 ? (
                <table className="seq-table">
                  <thead>
                    <tr>
                      <th>Name</th>
                      <th>Steps</th>
                      <th>Key Range</th>
                      <th>Min Modules</th>
                      {uiMode === 'developer' && <th>ID</th>}
                      <th>Actions</th>
                    </tr>
                  </thead>
                  <tbody>
                    {dbSeqItems.map((it) => {
                      const minModules = it.maxKey >= 0 ? Math.ceil((it.maxKey + 1) / 12) : 1;
                      const minNote = it.maxKey >= 0 ? keyToNote(0) : '--';
                      const maxNote = it.maxKey >= 0 ? keyToNote(it.maxKey) : '--';

                      return (
                        <tr
                          key={it.id}
                          className="seq-row-clickable"
                          onClick={() => uiMode === 'user' ? openSequenceModal(it.id) : openEditor(it.id)}
                        >
                          <td><strong>{it.name}</strong></td>
                          <td>{typeof it.stepCount === 'number' ? it.stepCount : '--'}</td>
                          <td>{minNote} - {maxNote}</td>
                          <td>{minModules}</td>
                          {uiMode === 'developer' && <td>{it.id}</td>}
                          <td onClick={(e) => e.stopPropagation()}>
                            <div className="btn-row">
                              <button className="btn btn-sm btn-secondary" onClick={() => openEditor(it.id)}>Edit</button>
                              <button className="btn btn-sm btn-coral" onClick={() => deleteSequence(it.id)}>Delete</button>
                            </div>
                          </td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              ) : (
                <div className="hint">
                  No sequences yet. Use <b>New Sequence</b> to create one{uiMode === 'developer' ? ' or Seed demos to populate' : ''}.
                </div>
              )}
            </div>

            {/* Developer JSON Editor */}
            {uiMode === 'developer' && (
              <div className="card card-accent-magenta">
                <h2>JSON Sequence Editor</h2>
                <div className="row">
                  <div className="label">Paste JSON and save to the sequence library.</div>
                  <div className="btn-row">
                    <button className="btn" type="button" disabled={dbCreateBusy} onClick={saveDbSequenceFromJson}>
                      {dbCreateBusy ? 'Saving...' : 'Save to DB'}
                    </button>
                    <button className="btn btn-secondary" type="button" disabled={dbCreateBusy} onClick={() => { setDbCreateMsg(''); setDbCreateErr(''); setDbCreateJson(pretty(defaultTemplate)); }}>
                      Reset template
                    </button>
                  </div>
                </div>
                {dbCreateErr && <pre className="pre">{dbCreateErr}</pre>}
                {dbCreateMsg && <pre className="pre">{dbCreateMsg}</pre>}
                <textarea
                  className="input"
                  style={{ minHeight: 200, fontFamily: 'var(--font-mono)' }}
                  value={dbCreateJson}
                  onChange={(e) => setDbCreateJson(e.target.value)}
                />
                <div className="hint">
                  Format: <code>{`{ name, description, steps: [{ keys: [0], colors: ["00B4D8"], duration: 300 }] }`}</code>
                </div>
              </div>
            )}

            {/* Finger colour reference */}
            <div className="card">
              <div className="row" style={{ marginBottom: 12 }}>
                <h2 style={{ margin: 0 }}>Finger Colours</h2>
                <label className="cb-toggle">
                  <span className="cb-toggle-label">Colourblind</span>
                  <input
                    type="checkbox"
                    checked={colorMode === 'colorblind'}
                    onChange={(e) => setColorMode(e.target.checked ? 'colorblind' : 'default')}
                  />
                  <span className="cb-toggle-track">
                    <span className="cb-toggle-knob" />
                  </span>
                </label>
              </div>
              <div className="btn-row" style={{ gap: 12 }}>
                {FINGER_OPTIONS.map(f => (
                  <div key={f.finger} className="step-color" style={{ gap: 6 }}>
                    {renderColorSwatch(activeFingerColors[f.finger])}
                    <span>{f.label}</span>
                  </div>
                ))}
              </div>
            </div>
          </section>
        )}

        {/* ============ LOGS TAB (developer only) ============ */}
        {uiMode === 'developer' && tab === 'logs' && (
          <section className="panel">
            <h1>Logs</h1>
            <div className="card">
              <div className="row">
                <div className="row-left">
                  <button className="btn" onClick={refreshLogs}>Refresh</button>
                  <button className="btn btn-secondary" onClick={() => setAutoLogs(v => !v)}>Auto: {autoLogs ? 'ON' : 'OFF'}</button>
                  <button className="btn btn-secondary" onClick={copyDisplayedLogs}>Copy</button>
                  <button className="btn btn-secondary" onClick={() => setLogs([])}>Clear UI</button>
                </div>
                <div className="row-right">
                  <div className="label">Tail</div>
                  <input className="input input-small" type="number" min="1" max="500" value={tail} onChange={(e) => setTail(Number(e.target.value))} />
                  <div className="label">Search</div>
                  <input className="input" value={logsSearch} onChange={(e) => setLogsSearch(e.target.value)} placeholder="Find text in logs" />
                  <button className={logsPrefixedOnly ? 'btn' : 'btn btn-secondary'} onClick={() => setLogsPrefixedOnly(v => !v)} type="button">Prefixed only</button>
                </div>
              </div>
              <div className="hint">
                Showing <b>{displayedLogs.length}</b> lines.
                {logsCopyMsg ? `  ${logsCopyMsg}` : ''}
              </div>
              <pre className="pre pre-logs">
                {displayedLogs.length
                  ? displayedLogs.map(x => `[${x.ts}] ${x.source}: ${x.message}`).join('\n')
                  : '(no logs yet)'}
              </pre>
            </div>
          </section>
        )}

      </main>

      {/* ============ SEQUENCE EDITOR MODAL ============ */}
      {editorOpen && (
        <div className="modal-overlay" onClick={closeEditor}>
          <div className="modal" onClick={(e) => e.stopPropagation()} style={{ maxWidth: 1000 }}>
            <div className="modal-header">
              <div>
                <div className="modal-title">{editorSeqId !== null ? `Edit Sequence #${editorSeqId}` : 'New Sequence'}</div>
              </div>
              <div className="btn-row">
                <button className="btn btn-green" onClick={saveEditorSequence} disabled={editorSaving}>
                  {editorSaving ? 'Saving...' : 'Save'}
                </button>
                <button className="btn btn-secondary" onClick={closeEditor}>Cancel</button>
              </div>
            </div>

            {editorErrors.save && <pre className="pre">{editorErrors.save}</pre>}

            <div className="card" style={{ marginBottom: 12 }}>
              <div className="row">
                <div style={{ flex: 1 }}>
                  <label className="label">Name (max 31 chars, no commas)</label>
                  <input
                    className={`input ${editorErrors.name ? 'input-error' : ''}`}
                    value={editorName}
                    onChange={(e) => setEditorName(e.target.value)}
                    maxLength={31}
                    placeholder="Sequence name"
                  />
                  {editorErrors.name && <div className="error-text">{editorErrors.name}</div>}
                </div>
                <div style={{ flex: 1 }}>
                  <label className="label">Description (optional)</label>
                  <input
                    className="input"
                    value={editorDesc}
                    onChange={(e) => setEditorDesc(e.target.value)}
                    placeholder="Optional description"
                  />
                </div>
              </div>
            </div>

            <div className="card" style={{ marginBottom: 12 }}>
              <div className="row">
                <h2 style={{ margin: 0 }}>Steps</h2>
                <button
                  className="btn btn-sm btn-green"
                  disabled={editorSteps.length >= 128 || editorEditingStep !== null}
                  onClick={startAddStep}
                >
                  Add Step
                </button>
              </div>
              {editorErrors.steps && <div className="error-text">{editorErrors.steps}</div>}

              <table className="steps-table" style={{ marginTop: 8 }}>
                <thead>
                  <tr>
                    <th></th>
                    <th>#</th>
                    <th>Key</th>
                    <th>Note</th>
                    <th>Finger</th>
                    <th>Duration</th>
                    <th>Actions</th>
                  </tr>
                </thead>
                <tbody>
                  {editorSteps.map((step, idx) => {
                    if (editorEditingStep === idx) {
                      return renderStepEditRow();
                    }

                    return (
                      <tr
                        key={idx}
                        draggable={editorEditingStep === null}
                        onDragStart={(e) => {
                          dragStepRef.current = idx;
                          e.dataTransfer.effectAllowed = 'move';
                          e.currentTarget.classList.add('dragging');
                        }}
                        onDragEnd={(e) => {
                          dragStepRef.current = null;
                          dragOverStepRef.current = null;
                          e.currentTarget.classList.remove('dragging');
                          document.querySelectorAll('.steps-table tr.drag-insert-before, .steps-table tr.drag-insert-after').forEach(el => {
                            el.classList.remove('drag-insert-before', 'drag-insert-after');
                          });
                        }}
                        onDragOver={(e) => {
                          e.preventDefault();
                          e.dataTransfer.dropEffect = 'move';
                          const rect = e.currentTarget.getBoundingClientRect();
                          const midY = rect.top + rect.height / 2;
                          const isAbove = e.clientY < midY;
                          const key = isAbove ? `${idx}-before` : `${idx}-after`;
                          if (dragOverStepRef.current !== key) {
                            document.querySelectorAll('.steps-table tr.drag-insert-before, .steps-table tr.drag-insert-after').forEach(el => {
                              el.classList.remove('drag-insert-before', 'drag-insert-after');
                            });
                            dragOverStepRef.current = key;
                            e.currentTarget.classList.add(isAbove ? 'drag-insert-before' : 'drag-insert-after');
                          }
                        }}
                        onDrop={(e) => {
                          e.preventDefault();
                          const rect = e.currentTarget.getBoundingClientRect();
                          const isAbove = e.clientY < rect.top + rect.height / 2;
                          e.currentTarget.classList.remove('drag-insert-before', 'drag-insert-after');
                          const from = dragStepRef.current;
                          if (from === null) return;
                          let to = isAbove ? idx : idx + 1;
                          if (from < to) to--;
                          if (from !== to) {
                            setEditorSteps(prev => {
                              const arr = [...prev];
                              const [moved] = arr.splice(from, 1);
                              arr.splice(to, 0, moved);
                              return arr;
                            });
                          }
                        }}
                      >
                        <td className="drag-handle" style={{ cursor: 'grab', textAlign: 'center', color: 'var(--muted-foreground)', userSelect: 'none' }}>⠿</td>
                        <td>{idx + 1}</td>
                        <td>{step.keys.join(', ')}</td>
                        <td>{step.keys.map(k => keyToNote(k)).join(', ')}</td>
                        <td>
                          <div className="pill-row">
                            {step.colors.map((c, ci) => (
                              <span key={ci} className="step-color" style={{ gap: 4 }}>
                                {renderColorSwatch(c)}
                                <span>{HEX_TO_FINGER[String(c).toUpperCase()] || hexColorName(c)}</span>
                              </span>
                            ))}
                          </div>
                        </td>
                        <td>{step.duration}ms</td>
                        <td>
                          <div className="btn-row">
                            <button className="btn btn-sm btn-secondary" onClick={() => startEditStep(idx)} disabled={editorEditingStep !== null}>Edit</button>
                            <button className="btn btn-sm btn-coral" onClick={() => deleteEditorStep(idx)} disabled={editorEditingStep !== null}>Delete</button>
                          </div>
                          {editorErrors[`step_${idx}`] && <div className="error-text">{editorErrors[`step_${idx}`]}</div>}
                        </td>
                      </tr>
                    );
                  })}
                  {editorEditingStep === 'new' && renderStepEditRow()}
                </tbody>
              </table>
            </div>

          </div>
        </div>
      )}

      {/* ============ SEQUENCE DETAILS MODAL (user mode) ============ */}
      {seqModalOpen && (
        <div className="modal-overlay" onClick={closeSequenceModal}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <div>
                <div className="modal-title">
                  {seqModalSeq?.name || (seqModalLoading ? 'Loading...' : 'Song details')}
                </div>
                <div className="modal-subtitle">
                  ID: <b>{seqModalSeq?.id || '--'}</b>
                  {seqModalSeq?.description && <>{' | '}<span>{seqModalSeq.description}</span></>}
                </div>
              </div>
              <button className="btn btn-secondary" type="button" onClick={closeSequenceModal}>Close</button>
            </div>

            {seqModalError && <pre className="pre">{seqModalError}</pre>}

            {seqModalLoading ? (
              <div className="hint mt">Loading...</div>
            ) : seqModalSeq && Array.isArray(seqModalSeq.steps) ? (
              <div className="card" style={{ marginBottom: 0 }}>
                <h2>Steps</h2>
                {seqModalSeq.steps.length === 0 ? (
                  <div className="hint">No steps.</div>
                ) : (
                  <table className="steps-table">
                    <thead>
                      <tr>
                        <th>#</th>
                        <th>Keys</th>
                        <th>Notes</th>
                        <th>Finger</th>
                        <th>Duration</th>
                      </tr>
                    </thead>
                    <tbody>
                      {seqModalSeq.steps.map((s, idx) => {
                        const keys = Array.isArray(s.keys) ? s.keys : (s.k !== undefined ? [s.k] : []);
                        const colors = Array.isArray(s.colors) ? s.colors : (s.c !== undefined ? [s.c] : []);
                        const duration = s.duration ?? s.d ?? '--';

                        return (
                          <tr key={idx}>
                            <td>{idx + 1}</td>
                            <td>{keys.join(', ')}</td>
                            <td>{keys.map(k => keyToNote(k)).join(', ')}</td>
                            <td>
                              <div className="pill-row">
                                {colors.map((c, ci) => (
                                  <span key={ci} className="step-color" style={{ gap: 4 }}>
                                    {renderColorSwatch(c)}
                                    <span>{HEX_TO_FINGER[String(c).toUpperCase()] || hexColorName(c)}</span>
                                  </span>
                                ))}
                              </div>
                            </td>
                            <td>{duration}ms</td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                )}
              </div>
            ) : (
              <div className="hint mt">No steps found.</div>
            )}
          </div>
        </div>
      )}

    </div>
  );
}
