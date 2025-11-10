<h1>HVAC Filter Clogging Detection System</h1>
<h2>ESP32 + Sensirion SDP810-500 Differential Pressure Sensor<br/>
Modbus TCP Slave + MQTT Publisher for Home Assistant</h2><br/>

<h2>📌 Overview</h2>
This project provides a practical solution for detecting <b>HVAC air filter clogging</b> using an <b>ESP32</b> microcontroller and a <b>Sensirion SDP810-500</b> differential pressure sensor.
By measuring the pressure <b>before</b> and <b>after</b> the air filter, the system determines the pressure drop (ΔP). As the filter becomes dirty, air flow decreases → pressure drop increases → the system detects clogging.

This project exposes measured data via <b>two simultaneous interfaces:</b>

✅ <b>Modbus TCP Slave</b> — allowing any BAS/BMS controller to poll the device<br/>
✅ <b>MQTT Publisher</b> — reporting data to <b>Home Assistant</b> or any MQTT broker

The ESP32 works as:
<ul>
  <li><b>I²C master</b> for the SDP810-500 sensor</li>
  <li><b>Modbus slave device</b> on port 502</li>
  <li><b>MQTT sensor publisher</b></li>
  <li><b>Wi-Fi client</b></li>
</ul>

You can test Modbus functionality using <b>Modpoll</b> or any Modbus TCP client.
<hr><br/>

<h2>🛠 Features</h2>
<h3>✅ Differential Pressure Monitoring</h3>
<ul>
  <li>Uses <b>SDP810-500</b> for accurate low-range differential pressure readings</li>
  <li>Calculates pressure difference and outputs values in Pascals</li>
  <li>Includes CRC validation according to Sensirion protocol</li>
</ul>

<h3>✅ Modbus TCP Slave</h3>
<ul>
  <li>Slave address: 1</li>
  <li>Default port: 502</li>
  <li>Exposes holding registers containing sensor data</li>
  <li>Pollable from software like Modpoll, PLCs, BAS controllers, etc.</li>
</ul><br/>

<b>Registers map:</b>
<table>
  <tr>
    <th>Register</th>
    <th>Meaning</th>
    <th>Notes</th>
  </tr>
  <tr>
    <td>40001 (index 0)</td>
    <td>Differential Pressure (Pa)</td>
    <td>raw_dp/60 scaling</td>
  </tr>
  <tr>
    <td>40002 (index 1)</td>
    <td>Temperature (°C)</td>
    <td>raw_temp/200 scaling</td>
  </tr>
</table><br/>

<h3>✅ MQTT Publishing</h3>

Publishes real-time pressure values to a Home Assistant MQTT broker:<br/>
<b>Topic:</b>
<code>homeassistant/sensor/pressure</code>

<h3>✅ Wi-Fi Support</h3>
<ul>
  <li>Connects to local network</li>
  <li>Automatically reconnects if connection drops</li>
  <li>MQTT starts only after Wi-Fi IP assignment</li>
</ul>
<hr/><br/>

<h2>🧰 Hardware Requirements</h2>
<ul>
  <li><b>ESP32</b> development board</li>
  <li><b>Sensirion SDP810-500PA</b> differential pressure sensor</li>
  <li>3.3V power supply</li>
  <li>HVAC duct with two pressure sampling points</li>
  <li>Tubing for connecting sensor ports</li>
</ul><br/>

<h3>Wiring (ESP32 → SDP810-500)</h3>
<table>
<tr>
  <th>ESP32 Pin</th>	<th>SDP810 Pin</th>	<th>Description</th>
</tr>
<tr>
  <td>3.3V</td>
  <td>VDD</td>
  <td>Power</td>
</tr>
<tr>
  <td>GND</td>
  <td>GND</td>
  <td>Ground</td>
</tr>
<tr>
  <td>GPIO21</td>
  <td>SDA</td>
  <td>I²C Data</td>
</tr>
<tr>
  <td>GPIO22</td>
  <td>SCL</td>
  <td>I²C Clock</td>
</tr>
</table><hr/>
