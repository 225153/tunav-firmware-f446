## 📡 Real-Time Telemetry via MQTT

This branch contains the implementation for streaming live telemetry data using the **EC200U Cellular Module** over an unencrypted raw TCP socket, paired with a public WebSockets-based dashboard for live monitoring.

### 🌐 Public Broker Details
We utilize the free, public sandbox broker provided by HiveMQ. This setup **does not require a username or password**, making it ideal for rapid prototyping and testing.

* **Public Broker Host:** `broker.hivemq.com`
* **Telemetry Topic:** `tunav/telemetry`

---

### 🖥️ How to Set Up the Live Web Dashboard

To monitor the data packets streaming from the STM32 board in real time, follow these exact configurations on the public MQTT web client:

1. Open the free online client in your web browser:  
   👉 **[HiveMQ Web MQTT Client](https://www.hivemq.com/demos/websocket-client/)**

2. Configure the **Connection** panel with the following parameters:
   
   | Field | Setting | Reason |
   | :--- | :--- | :--- |
   | **Host** | `broker.hivemq.com` | Public cluster broker endpoint |
   | **Port** | `8884` | **Required** secure WebSocket port for browsers |
   | **SSL** | **[X] Checked** | Web browsers (`https`) strictly block unencrypted sockets |
   | **Clean Session** | **[X] Checked** | Wipes old lingering sessions instantly |
   | **ClientID** | *Leave Default* | Automatically generates a unique browser identifier |

3. Click **Connect**. The status indicator dot will turn **Green** once established.

4. Go to the **Subscriptions** panel on the right side:
   * Click **Add New Topic Subscription**
   * **Topic:** `tunav/telemetry`
   * **QoS:** `0`
   * Click **Subscribe**

Incoming payloads from the hardware will stream into the **Messages** log panel instantly.

---

### 🔌 Microcontroller Configuration Reference (EC200U)

> ⚠️ **Important Architecture Note:** While the web browser must use port **`8884`** with **SSL enabled** due to modern browser security restrictions, the embedded EC200U cellular engine communicates over raw unencrypted TCP. 
> 
> Ensure your STM32 AT command sequence targets the raw port:
> * **AT Command Port:** `1883`
> * **AT Command SSL Profile:** Disabled (`AT+QMTCFG="ssl",0,0`)
