# Add a vehicle Wi-Fi profile

On NetworkManager-based SentryUSB images, create the profile without placing its password in shell history:

```bash
read -r -p "Car Wi-Fi SSID: " WIFI_SSID
read -r -s -p "Car Wi-Fi password: " WIFI_PSK
echo
sudo nmcli connection add type wifi ifname wlan0 con-name "Tesla Car WiFi" ssid "$WIFI_SSID"
sudo nmcli connection modify "Tesla Car WiFi" \
  wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$WIFI_PSK" \
  connection.autoconnect yes connection.autoconnect-priority 100 \
  ipv4.method auto ipv6.method auto
unset WIFI_PSK WIFI_SSID
```

Verify without exposing the password:

```bash
nmcli -f connection.id,connection.autoconnect,connection.autoconnect-priority,802-11-wireless.ssid connection show "Tesla Car WiFi"
```

This profile belongs to runtime state and must not be committed to GitHub.
