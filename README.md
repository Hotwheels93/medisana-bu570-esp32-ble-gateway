# medisana-bu570-esp32-ble-gateway
ESP32 implementation of a BLE Gateway for Medisana BU570 blood pressure monitor 

# BLE Health Gateway
An ESP32-based Bluetooth Low Energy (BLE) gateway for medical devices, specifically designed for blood pressure monitors. This project creates a bridge between BLE health devices and cloud services via WiFi.

# Features

Auto-Discovery: Automatically scans and connects to BLE blood pressure devices
Standard Compliance: Implements BLE Health Device Profile (HDP) for blood pressure monitors
Data Processing: Parses blood pressure measurements and intermediate cuff readings
WiFi Management: Built-in web interface for WiFi configuration
Cloud Integration: Forwards health data to a custom API endpoint
Signal Monitoring: Includes RSSI tracking for connection quality assessment
Time Synchronization: Sets the current time on connected devices
Coexistence Management: Careful handling of WiFi and BLE to prevent interference


# Author

Martin Hocquel-Hans (mh2-deveolopment)

# Disclaimer

This project is for development purposes only and NOT for use in medical diagnostics or treatment. I am in no way affiliated with Medisana or any of its subsidiaries. This is an independent project that interacts with Medisana BU570 blood pressure monitors using standard Bluetooth Low Energy protocols. Medisana and BU570 are trademarks of Medisana AG. Use this software at your own risk.


This software comes with no warranty of any kind. I take no responsibility for any damages that may occur from using this software.
The data should not be used for medical diagnosis without consulting healthcare professional.
