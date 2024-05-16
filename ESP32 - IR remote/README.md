<!DOCTYPE html>
<html lang="en">
<body>
  <h1>ESP32 IR remote</h1>
  <p>IR remote implemented using the ESP32 RMT component, controlled by a webserver running on the ESP. </p>
  <br>
  <p align="center"> 
    <img width=300 height=235 src="https://github.com/tom-zv/FPGA-ESP32-Projects/assets/96687713/9c52fccc-1e1a-4177-b196-013618267c74">
  </p>
  <h2>Frontend</h2>
  <p>web-browser client implementation - gained an understanding of HTML, CSS, JavaScript as well as designing for frontend & backend interoperabilty</p>
  <table align="center">
  <tr>
    <td><img width="239" height="286" src="https://github.com/tom-zv/FPGA-ESP32-Projects/assets/96687713/6cbf0d7e-873d-4af8-9c47-810d24c74db7"></td>
    <td><img width="224" height="376" src="https://github.com/tom-zv/FPGA-ESP32-Projects/assets/96687713/c7ad3d25-0176-4fe8-b7a5-5eba6871e56c"></td>
  </tr>
  <tr>
     <td align="center">AC controller page</td>
     <td align="center">Upload page
    <br>
      <p> allows OTA webpage update</p>
  </td>
  </tr>
 </table>
 <h2>Backend</h2>
 <p> The backend uses the ESP-IDF framework for its https server implementation & Remote Control Transciever (RMT) component, running on an ESP-WROOM-32 Devkit.<br>
     This project provided valuable experience in working within an established framework and interfacing with its APIs. Additionally, it involved practical   application of operating system features, such as synchronization mechanisms with semaphores and mutexes, interprocess communication using FreeRTOS task notifications, and employing timers to coordinate task sleep states.
 </p>
</body>
</html>
