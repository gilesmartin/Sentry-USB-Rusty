# Bluetooth pairing

Run on the SentryUSB Pi:

```bash
sudo teslamic-pair
```

The helper:

1. registers a temporary BlueZ `NoInputNoOutput` agent;
2. powers the controller;
3. enables pairability/discoverability for 180 seconds by default;
4. authorizes only A2DP/AVRCP audio services;
5. trusts only a newly bonded device on that adapter, and only after an allowlisted audio service is authorized;
6. rejects weak fixed-PIN legacy pairing;
7. disables both discoverability and pairability on success, timeout, interruption, or error.

Override the bounded interval with `--timeout` (30-900 seconds) or `TESLAMIC_PAIRING_TIMEOUT` in `/etc/teslamic-gadget.conf`.

Manage devices:

```bash
teslamic-devices list
sudo teslamic-devices info DEVICE_ADDRESS
sudo teslamic-devices connect DEVICE_ADDRESS
sudo teslamic-devices disconnect DEVICE_ADDRESS
sudo teslamic-devices remove DEVICE_ADDRESS
```

Do not copy `/var/lib/bluetooth` between Pis. Pair each source independently. Devices may briefly use a random LE identity while their bonded public/classic identity differs; trust the identity BlueZ reports as paired and bonded.
