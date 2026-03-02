# Open Octave (Demo 2) — UI Design Document 

This document defines the complete UI structure, user flow, layout, debugging model, component usage, and local hosting requirements for Demo 2.

This UI must:

- Match the PRD and firmware contract.
- Avoid any “vibey dashboard” styling.
- Be stable and predictable.
- Be easy for any teammate to run locally.
- Prioritize debugging and reliability over aesthetics.

---

# 0. Locked UI Principles

These principles are non-negotiable for Demo 2.

1. Dark mode only. No theme toggle.
2. System fonts only. No external font downloads.
3. Left vertical navigation tabs always visible.
4. State-driven UI. UI renders only from controller mirror state.
5. Debug-first design. Every major action is observable in logs.
6. Transport selector required (Serial + WebSocket).
7. Local hosting only (no cloud deployment).

---

# 1. Visual Theme (Dark Only)

## 1.1 CSS Tokens

Paste this into `software/web/src/index.css`.

```css
:root {
  --card: #f9fafb;
  --ring: #e50914;
  --input: #e5e7eb;
  --muted: #f9fafb;
  --accent: #fffbeb;
  --border: #e5e7eb;
  --radius: 0.25rem;
  --shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.1), 0 1px 2px 0 rgba(0, 0, 0, 0.06);
  --popover: #ffffff;
  --primary: #e50914;
  --spacing: 0.25rem;
  --font-mono: "Courier New", monospace;
  --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --secondary: #f3f4f6;
  --shadow-lg: 0 10px 15px -3px rgba(0, 0, 0, 0.1), 0 4px 6px -2px rgba(0, 0, 0, 0.05);
  --shadow-md: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
  --shadow-sm: 0 1px 2px 0 rgba(0, 0, 0, 0.05);
  --shadow-xl: 0 20px 25px -5px rgba(0, 0, 0, 0.1), 0 10px 10px -5px rgba(0, 0, 0, 0.04);
  --background: #ffffff;
  --font-serif: "Georgia", serif;
  --foreground: #000000;
  --shadow-2xl: 0 25px 50px -12px rgba(0, 0, 0, 0.25);
  --destructive: #ef4444;
  --card-foreground: #171717;
  --muted-foreground: #6b7280;
  --accent-foreground: #92400e;
  --popover-foreground: #000000;
  --primary-foreground: #ffffff;
  --secondary-foreground: #4b5563;
  --destructive-foreground: #ffffff;
}

.dark {
  --card: #1a1a1a;
  --ring: #e50914;
  --input: #404040;
  --muted: #2b2b2b;
  --accent: #e50914;
  --border: #404040;
  --radius: 0.25rem;
  --shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.1), 0 1px 2px 0 rgba(0, 0, 0, 0.06);
  --popover: #1a1a1a;
  --primary: #e50914;
  --spacing: 0.25rem;
  --font-mono: "Courier New", monospace;
  --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --secondary: #2b2b2b;
  --shadow-lg: 0 10px 15px -3px rgba(0, 0, 0, 0.1), 0 4px 6px -2px rgba(0, 0, 0, 0.05);
  --shadow-md: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
  --shadow-sm: 0 1px 2px 0 rgba(0, 0, 0, 0.05);
  --shadow-xl: 0 20px 25px -5px rgba(0, 0, 0, 0.1), 0 10px 10px -5px rgba(0, 0, 0, 0.04);
  --background: #000000;
  --foreground: #ffffff;
  --destructive: #e50914;
  --card-foreground: #ffffff;
  --muted-foreground: #a3a3a3;
  --accent-foreground: #ffffff;
}
```

App root must always include `class="dark"`.

---

# 2. Navigation Layout

Left vertical tabs are always visible.

Tab order:

1. Connect (default)
2. Control
3. Sequences
4. Logs
5. Settings / Debug

---

# 3. User Flow (Complete)

## 3.1 Entry

User opens localhost UI.

Connect tab is active.

User sees:
- Short intro
- Transport selector
- Connect button

## 3.2 Transport Selection

Dropdown:
- Serial (USB)
- WebSocket (WiFi)

If transport changed while connected:
- Force disconnect first.

## 3.3 Connect

On Connect:
- Open transport.
- Enter Sync state.
- Show live logs during sync.
- Perform:
  - Open transport
  - Request STATUS (q)
  - Request SEQ_LIST (L)
- Populate local mirror state.

If sync fails:
- Show which step failed.
- Show last log line.
- Show Retry button.

## 3.4 After Sync

User can:
- Switch modes.
- Start / Stop.
- Select or upload sequence.
- View logs.
- Use debug tools.

---

# 4. Tab Specifications

---

## 4.1 Connect Tab

User can:

- Select transport.
- Choose serial port OR enter websocket host/port.
- Connect.
- Disconnect.
- Switch transport.
- Reset local state.
- Resync.
- Send raw command.

### Sync Panel

Displays:
- Opening transport
- Waiting firmware
- Requesting STATUS
- Loading SEQ_LIST
- Sync complete

Includes compact live logs (last 50 lines).

---

## 4.2 Control Tab

Displays:

- Transport
- Connected status
- Mode
- Running
- Current sequence
- Step index

User can:

- Switch mode (Manual, Guided, Teaching)
- Start
- Stop
- Next
- Prev
- Query Status
- Query Sequence List

Buttons disabled if not connected.

UI updates only after firmware ACK/STATUS.

---

## 4.3 Sequences Tab

Stage 1:
- Hardcoded firmware sequences only.

Stage 2:
- SQLite library shown.
- Table:
  - id
  - name
  - step count
  - updated_at

User can:
- Select
- Upload to device
- Preview steps
- Delete (optional confirm)

Upload shows progress counter.

Only one uploaded sequence at a time.

---

## 4.4 Logs Tab

Features:

- Filter:
  - Only ACK/STATUS/EVT/ERR
  - Show all
- Search
- Pause autoscroll
- Clear UI logs
- Download logs
- Copy last 200 lines

Highlight:
- ACK = primary
- ERR = destructive
- STATUS/EVT = neutral

---

## 4.5 Settings / Debug Tab

User can:

- Reset local mirror
- Disconnect + resync
- Hard reconnect
- Send raw command
- View transport diagnostics
- View firmware contract expectation

All dangerous actions use confirmation dialog.

---

# 5. Component Policy

Allowed:
- Button
- Tabs
- Select
- Label
- Badge
- Tooltip
- AlertDialog

Avoid:
- Navigation menu
- Fancy dashboards
- Charts
- Animation libraries
- Heavy layout systems

---

# 6. Vertical Layout Skeleton

```tsx
<Tabs defaultValue="connect" orientation="vertical">
  <TabsList>
    <TabsTrigger value="connect">Connect</TabsTrigger>
    <TabsTrigger value="control">Control</TabsTrigger>
    <TabsTrigger value="sequences">Sequences</TabsTrigger>
    <TabsTrigger value="logs">Logs</TabsTrigger>
    <TabsTrigger value="debug">Settings / Debug</TabsTrigger>
  </TabsList>

  <TabsContent value="connect" />
  <TabsContent value="control" />
  <TabsContent value="sequences" />
  <TabsContent value="logs" />
  <TabsContent value="debug" />
</Tabs>
```

---

# 7. Debug Requirements (Mandatory)

1. Sync shows logs.
2. UI state updates only from parsed firmware logs.
3. Raw command tool exists.
4. Reset local mirror exists.
5. Disconnect + resync exists.
6. Transport status visible.
7. Upload progress visible.

---

# 8. Local Hosting Setup

Frontend:

```
cd software/web
npm install
npm run dev
```

Backend:

```
cd software/server
npm install
npm run dev
```

Dependencies must be installed on each host laptop.

Only use shadcn/ui primitives to reduce dependency risk.

---

# 9. Open Decisions To Lock Before Implementation

1. Default WebSocket host and port.
2. Serial port filtering rules.
3. What defines Sync Complete.
4. Maximum log history retained in memory.

---

End of UI Design Document.
