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
