[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/3E8ca2dkcC)

```
 ██████╗██╗     ██╗   ██╗███████╗████████╗███████╗██████╗
██╔════╝██║     ██║   ██║██╔════╝╚══██╔══╝██╔════╝██╔══██╗
██║     ██║     ██║   ██║███████╗   ██║   █████╗  ██████╔╝
██║     ██║     ██║   ██║╚════██║   ██║   ██╔══╝  ██╔══██╗
╚██████╗███████╗╚██████╔╝███████║   ██║   ███████╗██║  ██║
 ╚═════╝╚══════╝ ╚═════╝ ╚══════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝

███╗   ██╗███████╗██████╗ ██████╗  █████╗ ██╗  ██╗███████╗
████╗  ██║██╔════╝██╔══██╗██╔══██╗██╔══██╗╚██╗██╔╝██╔════╝
██╔██╗ ██║█████╗  ██████╔╝██║  ██║███████║ ╚███╔╝ █████╗
██║╚██╗██║██╔══╝  ██╔══██╗██║  ██║██╔══██║ ██╔██╗ ██╔══╝
██║ ╚████║███████╗██║  ██║██████╔╝██║  ██║██╔╝ ██╗███████╗
╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝
```

> *"War never changes. But mining rigs? Those get better."*
> — **Overseer's Field Manual, Appendix IX: Computational Assets**

---

## ROBCO INDUSTRIES UNIFIED OPERATING SYSTEM
### NerdAxe Cluster Firmware — Classified: OPEN SOURCE

**VAULT-TEC MINING DIVISION** proudly presents the **NerdAxe_Cluster** firmware —
a hardened, multi-unit ESP32-S3 Bitcoin mining control system designed to coordinate
an entire cluster of NerdAxe family devices from a single Overseer node.

Built upon the shoulders of the brave engineers at [ESP-Miner](https://github.com/skot/ESP-Miner) and [NerdQAxePlus](https://github.com/shufps/ESP-Miner-NerdQAxePlus).

| FIELD | DATA |
|---|---|
| Supported Targets | ESP32-S3 |
| Required Platform | >= ESP-IDF v5.3.X |
| Cluster Protocol | ESP-NOW (peer-to-peer, no router required) |
| Max Cluster Size | 1 Master + 8 Slaves |
| Release | v1.0.0_beta1 |

---

## SUPPORTED HARDWARE — APPROVED VAULT UNITS

The following NerdAxe family units have been approved by Vault-Tec Engineering:

| Unit Designation | Board Target |
|---|---|
| NerdAxe | `NERDAXE` |
| NerdAxe Gamma | `NERDAXEGAMMA` |
| NerdEko | `NERDEKO` |
| NerdHAxe Gamma | `NERDHAXEGAMMA` |
| NerdOctAxe Gamma | `NERDOCTAXEGAMMA` |
| NerdOctAxe Plus | `NERDOCTAXEPLUS` |
| NerdQAxe Plus | `NERDQAXEPLUS` |
| NerdQAxe Plus+ | `NERDQAXEPLUS2` |
| NerdQX | `NERDQX` |

---

## SYSTEM CAPABILITIES — CLASSIFIED DOSSIER

### CLUSTER OPERATIONS — *ESP-NOW Mesh Network*
The flagship feature of this firmware. One unit assumes the role of **Cluster Master (Overseer)**
and up to **8 subordinate units** operate as **Cluster Slaves** — all communicating over
**ESP-NOW**, Vault-Tec's preferred peer-to-peer radio protocol that requires no WiFi router.

- Master connects to the stratum pool and distributes unique work packets to each slave
- Slaves are auto-discovered via beacon broadcast (`CLAXE` magic beacon)
- Slaves self-heal: if no work is received within the watchdog window, they re-register automatically
- Master rebroadcasts current work every 5 seconds to recover slaves that missed initial delivery
- Dead slaves are timed out and purged after 15 seconds of missed heartbeats
- All shares from slaves are routed back through the master to the pool

### AUTO-TIMING — *Adaptive Frequency Control*
The firmware automatically tunes ASIC operating frequency to maximize hashrate
while staying within safe thermal and power limits. No manual tuning required.

### POWER MANAGEMENT — *PID Control Loop*
A PID (Proportional-Integral-Derivative) controller actively manages power draw,
keeping your unit stable under varying load conditions. Fan speed is managed
to balance thermal performance with noise.

### DISPLAY SYSTEM — *LVGL UI*
Full graphical interface rendered via LVGL on supported displays.
Shows real-time hashrate, pool status, cluster slave status, temperature, and voltage.

### STRATUM PROTOCOL — *Dual-Pool Support*
Connects to any Stratum v1 mining pool. Dual-pool configuration supported —
configure a primary and a failover pool for uninterrupted operation.

### INFLUXDB + GRAFANA MONITORING — *Vault Command Dashboard*
Push real-time telemetry (hashrate, temperature, power, frequency) to an InfluxDB instance
and visualize it in a Grafana dashboard. Setup scripts are included in the `monitoring/` directory.

### DISCORD NOTIFICATIONS — *Overseer Alerts*
Receive mining alerts, share confirmations, and fault notifications directly
to your Discord server via webhook integration.

### OTA FIRMWARE UPDATES — *Remote Re-programming*
Update firmware over-the-air through the built-in web interface.
No serial cable required after initial flash.

### WIFI HEALTH MONITOR — *Connectivity Watchdog*
Monitors WiFi connection quality and automatically reconnects if the signal is lost,
ensuring continuous mining operation.

### SELF-TEST MODE — *Pre-Deployment Diagnostics*
Run a built-in hardware self-test to verify ASIC communication, display, power rails,
and sensor readings before deploying a unit to the field.

### HASHRATE MONITOR — *Performance Tracking*
Continuous hashrate monitoring with ring-buffer history.
Detects performance degradation and logs anomalies.

---

## DEPLOYMENT PROTOCOLS

### METHOD ALPHA: Flash Pre-Built Binary *(Recommended)*

Download the latest release binary from the [Releases](../../releases) page and flash using `bitaxetool`:

```bash
# Install bitaxetool
pip install --upgrade bitaxetool

# Copy and edit your config
cp config.cvs.example config.cvs

# Flash the factory binary (hold BOOT button during reset to enter bootload mode)
bitaxetool --config ./config.cvs --firmware esp-miner-NerdAxe.bin
```

### METHOD BRAVO: Build from Source — Windows (ESP-IDF Native)

Pre-built `.bat` scripts are provided for each board variant:

```bat
:: Build NerdAxe
build_nerdaxe.bat

:: Build NerdQAxe+
build_nerdqaxeplus.bat

:: Build NerdQAxe++
build_nerdqaxeplusplus.bat
```

Requires ESP-IDF v5.3+ installed at `C:\Users\<you>\esp\esp-idf`.

### METHOD CHARLIE: Build from Source — Docker *(Cross-platform)*

```bash
# Build the Docker container (once)
cd docker
./build_docker.sh
cd ..

# Set your board target
export BOARD="NERDQAXEPLUS2"
./docker/idf.sh set-target esp32-s3

# Build
./docker/idf.sh build
```

Output: `build/esp-miner.bin` and `build/www.bin`

#### Full Manual Flash via Docker

```bash
# Enter the build shell
./docker/idf-shell.sh

export BOARD="NERDQAXEPLUS2"
idf.py set-target esp32s3
idf.py build

# Generate config partition
nvs_partition_gen.py generate config.cvs config.bin 12288

# Merge all partitions
./merge_bin_with_config.sh nerdqaxe+.bin

# Flash
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 nerdqaxe+.bin
```

---

## GRAFANA MONITORING — *Vault Command Center*

<img src="https://github.com/user-attachments/assets/3c485428-5e48-4761-9717-bd88579a747d" width="600px">

Deploy the included monitoring stack (InfluxDB + Grafana) with a few commands.
See [`monitoring/`](./monitoring/) for setup instructions.

---

## CLUSTER CONFIGURATION — *Overseer Deployment Guide*

1. Flash one unit with this firmware — it will auto-discover the network role.
2. In the web UI, set one unit as **Master** and the rest as **Slaves**.
3. Slaves will auto-register with the Master via ESP-NOW beacon discovery.
4. The Master handles pool authentication and job distribution. Slaves mine and report shares.
5. All cluster telemetry is visible on the Master's display and web dashboard.

See [`ClusterNerdAxe/`](./ClusterNerdAxe/) for detailed cluster architecture documentation.

---

## CREDITS — *The Engineers Who Built the Vaults*

- **@skot** and the BitAxe team at OSMU — original ESP-Miner firmware
- **@ben** and **@jhonny** — BitAxe core contributors
- **@BitMaker** — NerdAxe hardware and firmware
- **@shufps** — NerdQAxePlus firmware fork

---

> *"Safety, Security, and Bitcoin — that's the Vault-Tec promise."*
