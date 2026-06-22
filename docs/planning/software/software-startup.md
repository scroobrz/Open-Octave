# Open Octave — Startup Guide

This guide explains how to start the Open Octave system on a **Windows 11 laptop**.

---

# 1. Normal Startup (every time)

Before starting the system:

1. Turn on **Windows Mobile Hotspot**

```
Settings → Network & Internet → Mobile hotspot
```

Use these settings:

```
Network name: Open Octave
Password: oop321321
Network band: 2.4 GHz
```

---

### Start the system

1. Open the project folder in **File Explorer**
2. Find the file:

```
start.bat
```

3. **Double-click `start.bat`**

Windows will open two command windows:

- Controller server
- Web interface

After a few seconds, the browser will open automatically.

If it does not open, go to:

```
http://localhost:5173
```

---

# 2. Alternative: Start using terminal commands

If needed, you can start the system manually.

Open **Command Prompt** and run:

```
cd SDP-Open-Octave/software/controller
npm run start:laptop
```

Open a **second Command Prompt** and run:

```
cd SDP-Open-Octave/software/web
npm run dev
```

Then open:

```
http://localhost:5173
```

---

# 3. Connect the piano modules

After the system is running:

1. Power the ESP32 module
2. Wait a few seconds
3. The module should appear in the UI automatically

---

# 4. First Time Setup (only once)

You only need to do this the **first time on a new laptop**.

### Step 1 — Install Node.js
Download and install Node.js:

https://nodejs.org

After installing, open **Command Prompt** and check:

```
node -v
npm -v
```

If both show version numbers, Node.js is installed correctly.

---

### Step 2 — Install project dependencies

1. Open **File Explorer**
2. Navigate to the project folder:

```
SDP-Open-Octave/software
```

3. Click the folder path bar and type:

```
cmd
```

Then press **Enter** to open Command Prompt in that folder.

Run:

```
cd controller
npm install
```

Then run:

```
cd ../web
npm install
```

This installs all required packages.

You only need to do this **once**.
