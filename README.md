IoT Agricultural Monitoring System
A smart monitoring and control system designed for high-sensitivity crops, integrating an ESP32 wireless sensor network with a secured hierarchical network infrastructure.
Key Features
•	Real-time Monitoring: Tracks temperature, air humidity, and light intensity.
•	Automated Control: Irrigation (pump) and lighting (LED) based on environmental thresholds.
•	Secured Network: Hierarchical Network design with MikroTik routers and WireGuard VPN for remote management.
Technical Stack
Microcontroller: ESP32-S3 DevKitC1
•	Sensors: DHT11 (Temperature/Humidity), BH1750 (Light)
•	Networking: MikroTik hEX Refresh, Mercusys MW305R Access Point
•	Protocols: MQTT (Mosquitto), TCP/IP, WireGuard VPN
•	Security: VLAN Segmentation, NAT, Firewall Filter Rules
Network Planning
•	LAN Subnet: 10.10.0.0/24
•	Gateway: 10.10.0.1 (MikroTik Router)
•	DMZ Zone: Isolated MQTT Broker at 172.168.1.0/29 for data security.
Project Structure
•	/hardware: Schematic and PCB designs (Altium).
•	/firmware: C++/Arduino source code for ESP32 nodes.
•	/network: MikroTik configuration scripts (.rsc) and system backups.
Student Information
Student: Nguyen Van Quan (ID: 2022602268)
Supervisor: Dr. Tran Dinh Thong
Institution: Hanoi University of Industry (HaUI)
Hanoi - 2026
