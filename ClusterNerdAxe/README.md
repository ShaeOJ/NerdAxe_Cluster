# CLUSTER NERDAXE — TECHNICAL DOSSIER
### *Vault-Tec Division of Distributed Mining Operations*

> *"Alone, a single unit mines. Together, a cluster conquers the blockchain."*

---

## OVERVIEW

**ClusterNerdAxe** is the cluster coordination layer built into the NerdAxe_Cluster firmware.
It enables a group of NerdAxe family ESP32-S3 mining units to operate as a single
coordinated mining entity using **ESP-NOW** — a peer-to-peer radio protocol that requires
no WiFi router or external infrastructure.

One unit becomes the **Master (Overseer)**. The rest become **Slaves (Field Units)**.
The Master connects to the stratum pool, distributes unique work to each slave,
and collects all shares back for submission.

---

## ARCHITECTURE

```
                    ┌─────────────────────┐
                    │   STRATUM POOL       │
                    │  (vault101.firepool) │
                    └──────────┬──────────┘
                               │ WiFi / TCP
                    ┌──────────▼──────────┐
                    │   CLUSTER MASTER    │  ← Overseer Unit
                    │   (NerdAxe Unit 0)  │
                    └──┬──────┬──────┬───┘
               ESP-NOW │      │      │ ESP-NOW
            ┌──────────▼┐  ┌──▼──────┐  ┌▼──────────┐
            │  SLAVE 1  │  │ SLAVE 2 │  │  SLAVE N  │
            │ (Unit 1)  │  │(Unit 2) │  │ (Unit N)  │
            └───────────┘  └─────────┘  └───────────┘
                        Max 8 Slaves
```

---

## MASTER RESPONSIBILITIES

- Connects to stratum pool (primary + failover)
- Subscribes and authenticates to the pool
- Mines locally on its own ASIC(s)
- Broadcasts unique work packets to each registered slave
- Accepts shares from slaves and submits them to the pool
- Tracks slave health via heartbeat timeouts
- Rebroadcasts current work every 5 seconds for lost slaves
- Exposes the web dashboard and API for the whole cluster

---

## SLAVE RESPONSIBILITIES

- Listens for Master beacon (`CLAXE` magic identifier)
- Registers with Master upon discovery
- Receives work packets and mines locally
- Reports found shares back to Master via ESP-NOW
- Sends heartbeat every 2 seconds to signal liveness
- Self-heals: re-registers if no work received within 20 seconds

---

## CLUSTER PROTOCOL MESSAGES

| Message | Direction | Purpose |
|---|---|---|
| `CLAXE` beacon | Master → Broadcast | Discovery beacon |
| `$REGISTER` | Slave → Master | Slave registration |
| `$CLWRK` | Master → Slave | Work packet distribution |
| `$CLSHR` | Slave → Master | Found share report |
| `$CLHBT` | Slave → Master | Heartbeat / keepalive |
| `$NACK` | Master → Slave | Rejection / re-register trigger |

---

## TIMING & LIMITS

| Parameter | Value |
|---|---|
| Max slaves | 8 |
| Beacon interval | 1000 ms |
| Heartbeat interval | 2000 ms |
| Slave timeout | 15000 ms (~7 missed heartbeats) |
| Work resend interval | 5000 ms (every 5 beacon cycles) |
| Slave self-heal timeout | 20000 ms |

---

## SUPPORTED BOARD DRIVERS

The following hardware drivers are included in this component:

| Driver | Board |
|---|---|
| `nerdaxe` | NerdAxe |
| `nerdaxegamma` | NerdAxe Gamma |
| `nerdeko` | NerdEko |
| `nerdhaxegamma` | NerdHAxe Gamma |
| `nerdoctaxegamma` | NerdOctAxe Gamma |
| `nerdoctaxeplus` | NerdOctAxe Plus |
| `nerdqaxeplus` | NerdQAxe Plus |
| `nerdqaxeplus2` | NerdQAxe Plus+ |
| `nerdqx` | NerdQX |

---

> *"The Overseer sees all. The cluster mines all."*
> — NerdAxe_Cluster Field Manual
