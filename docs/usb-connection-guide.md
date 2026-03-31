## Connect Open Octave to ESP32 through USB

Use this when you want the software to control **one ESP32 module** or **one chained pair** through a wired USB connection.

### Before you start
- Plug the ESP32 into your computer with a **data-capable USB cable**
- Open the Open Octave software
- Go to the **Connect** tab
- Switch **Connection Mode** to **USB**
- If you are in **user mode**, switch to **developer mode** so you can enter the serial port path

> Important: plugging in the cable alone is **not enough**.  
> You must still find the correct serial port, enter it, and click **Connect USB**.

---

## macOS

### 1. Find the serial port
Open **Terminal** and run:

`ls /dev/cu.*`

Look for a device that appears after plugging in the ESP32. It is usually something like:

`/dev/cu.usbmodem14101`

or

`/dev/cu.SLAB_USBtoUART`

### 2. Copy the serial port path
Copy the full path, for example:

`/dev/cu.usbmodem14101`

### 3. Enter it into Open Octave
- In the **Connect** tab under **USB**
- Paste the path into the **Serial port path** box
- Click **Connect USB**

### 4. Check it worked
You should see:
- **USB connected**
- the number of detected physical modules
- the total number of keys

---

## Windows

### 1. Find the serial port
Plug in the ESP32, then open **Device Manager**.

Go to:

`Ports (COM & LPT)`

Look for the ESP32 port. It is usually something like:

`USB Serial Device (COM3)`

or

`Silicon Labs CP210x USB to UART Bridge (COM4)`

### 2. Copy the COM port
Use the COM name only, for example:

`COM3`

### 3. Enter it into Open Octave
- In the **Connect** tab under **USB**
- Type the COM port into the **Serial port path** box
- Click **Connect USB**

### 4. Check it worked
You should see:
- **USB connected**
- the number of detected physical modules
- the total number of keys

---

## If it does not connect
Check these first:

- the USB cable is a **data cable**, not a charging-only cable
- the ESP32 is powered on
- you entered the correct port
- no other program is already using the port, such as:
  - Arduino Serial Monitor
  - another controller script
  - a terminal serial tool

Then try:
- unplugging and reconnecting the USB cable
- checking the port again
- clicking **Connect USB** again

---

## Summary
To connect successfully through USB:

1. plug in the ESP32  
2. find the serial port  
3. enter the port into the software  
4. click **Connect USB**

Only then will Open Octave start sending commands through USB.