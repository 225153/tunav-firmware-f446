# How to Connect with TCP (Ngrok & SSH Bridge Guide)

This guide explains how to set up a public TCP tunnel to connect an external hardware module (like an STM32 GSM gateway) to a local PC server. It documents the transition from Ngrok to a secure, free SSH-based tunnel due to protocol restrictions.

---

## 1. Prerequisites (What You Need at the Beginning)

To get started with local TCP testing, you originally need:

- **Python**: Installed on your local machine to run the backend server script.
- **Ngrok Account**: An active account registered on `ngrok.com`.

### The Ngrok TCP Limitation

While running `ngrok tcp 8080` is the standard approach, Ngrok now restricts raw TCP traffic on free tier accounts by forcing users to link a credit card (`ERR_NGROK_8013`). To bypass this security/payment wall without a credit card, an **OpenSSH Client** bridge was added to the setup.

---

## 2. The SSH Component (The Fix Added Later)

If Windows does not recognize the native `ssh` command, it must be enabled manually via PowerShell (Opened as **Administrator**):

```powershell
# Install the native Windows OpenSSH Client
Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0

# Generate a local host key to bypass remote password authentication
C:\Windows\System32\OpenSSH\ssh-keygen.exe
```

> ⚠️ **Note:** When generating the host key, press **Enter** for all prompts to leave the passphrases blank.

### Starting the Free Tunnel Bridge

Instead of Ngrok, execute this command in a normal PowerShell window to establish a free TCP tunnel bound to your local port `8080`:

```powershell
C:\Windows\System32\OpenSSH\ssh.exe -p 443 -R0:127.0.0.1:8080 tcp@free.pinggy.io
```

> Type `yes` if prompted to trust the host connection fingerprint during the first launch.

---

## 3. The Python Server (`server.py`)

This script runs locally and listens for incoming hardware data packets routed through the public tunnel interface.

```python
import socket

# Configure a TCP/IP socket
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('0.0.0.0', 8080))
server.listen(1)

print("[INFO] Server is online. Waiting for STM32 hardware data on port 8080...")

while True:
    connection, client_address = server.accept()
    try:
        print(f"[CONNECTED] Incoming connection from: [...]")
        data = connection.recv(1024)
        if data:
            # Print the raw string message received from the GSM module
            print(f"[DATA RECEIVED] : {data.decode('utf-8')}")
    finally:
        connection.close()
```

---

## 4. Where to Receive and Visualize the Messages

To monitor live transmissions from your hardware, keep **two separate terminal windows** active on your workstation:

| Terminal | Role | Command |
|----------|------|---------|
| **Terminal 1** — Tunnel Interface | Runs the active `ssh.exe` process. Displays your public URL under the `TCP URL` row. | `ssh.exe -p 443 -R0:127.0.0.1:8080 tcp@free.pinggy.io` |
| **Terminal 2** — Data Receiver | Runs the Python server and prints incoming hardware payloads. | `python server.py` |

The public URL generated in Terminal 1 will look like:

```
tcp://[YOUR_UNIQUE_LINK].run.pinggy-free.link:[YOUR_PORT]
```

---

## 5. Executing the Hardware Handshake

Extract the unique domain string and the 5-digit port from Terminal 1, and map them directly into your microcontroller's AT command framework:

```
AT+QIOPEN=1,0,"TCP","[YOUR_UNIQUE_LINK].run.pinggy-free.link",[YOUR_PORT],0,0
```

Terminal 2 (the Python console) is exactly where you will visualize incoming data payloads. When the hardware initializes a `AT+QISEND` pipeline and pushes bytes, the console log will update instantly:

```
[CONNECTED] Incoming connection from: [...]
[DATA RECEIVED] : hello
```

---
