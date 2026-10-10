# KNX–DALI Gateway

DIY gateway between a wired KNX TP line and one DALI line (up to 64 control gear).
The first milestone switches and dims 64 channels mapped to DALI short addresses;
provisioning is done with ETS. No KNX or DALI certification is claimed or implied.

| Component    | Contents                                         | Licence          |
| ------------ | ------------------------------------------------ | ---------------- |
| `gateway/`   | ESP32-C6 firmware (links the GPL-3.0 knx stack)  | GPL-3.0-or-later |
| `knxprod/`   | ETS product definition and generation tooling    | MIT              |
| `simulation/`| Host-side bus models and test services           | MIT              |
| `hardware/`  | Schematics and PCB                               | CERN-OHL-P-2.0   |
| `mechanical/`| Enclosure                                        | CERN-OHL-P-2.0   |
| repo tooling | README/AGENTS docs, .vscode, REUSE metadata      | CC-BY-4.0 / CC0-1.0 |

Licences follow the REUSE specification (see `REUSE.toml`); per-file licence
headers are intentionally omitted. Firmware links Espressif's binary-only radio
libraries, as ESP32 KNX projects commonly do. See
`gateway/THIRD_PARTY_NOTICES.md` for the third-party software inside the
firmware. Engineering rules, requirements, and current limitations are
documented in `AGENTS.md`.
