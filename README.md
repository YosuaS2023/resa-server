# resa-multiplayer (resam)

> Modern, Modular, Resource-Based Multiplayer Server for GTA: San Andreas, Powered by Lua.

---

## 📌 Tentang Proyek

**resa-multiplayer (`resam`)** adalah platform server multiplayer generasi baru untuk Grand Theft Auto: San Andreas yang mengadopsi filosofi arsitektur **modular layaknya FiveM (CitizenFX)**, dibangun di atas fondasi performa tinggi dan netcode stabil dari **open.mp**.

Tujuan utama proyek ini adalah mengalihkan paradigma scripting GTA SA lama yang monolitik (Pawn `.pwn` / `.amx`) menuju ekosistem modern yang **berorientasi pada Resource dan modularitas penuh menggunakan bahasa pemrograman Lua**.

---

## 🎯 Tujuan Utama (Goals)

1. **Modular Resource Architecture (FiveM-Style)**:
   - Server tidak lagi dijalankan sebagai satu gamemode raksasa monolitik.
   - Semua fitur (spawn, kendaraan, sistem login/auth, ekonomi, pekerjaan, inventory) dipisah ke dalam folder independen di direktori `resources/`.
   - Menggunakan manifest `fxmanifest.lua` (atau `resmanifest.lua`) untuk konfigurasi metadata, dependency, dan daftar file script.

2. **Full Lua Scripting Engine**:
   - Menjadikan **Lua** (LuaJIT / Lua 5.4 via Sol2) sebagai bahasa scripting tingkat satu (*first-class language*).
   - Menyediakan API native yang bersih, idiomatik, dan mudah dipelajari baik secara prosedural (`SetPlayerPos`) maupun Object-Oriented (`player:setPosition(...)`).

3. **Isolated Sandboxing & Safety**:
   - Setiap resource berjalan di dalam lingkungan Lua State (`lua_State`) terisolasi masing-masing. Variabel global dari satu resource tidak akan mencemari resource lainnya.

4. **Inter-Resource Communication**:
   - Sistem **`exports`**: Resource dapat mengekspor fungsi untuk dipanggil oleh resource lain (contoh: `exports['resam_core']:getPlayerMoney(playerId)`).
   - Sistem **Event Bus**: Komunikasi decoupled antar modul menggunakan event handler (`AddEventHandler`, `TriggerEvent`).

5. **Hot-Reloading Tanpa Restart Server**:
   - Perintah konsol dinamis seperti FiveM: `start <resource>`, `stop <resource>`, `restart <resource>`, dan `ensure <resource>` yang dapat me-reload resource secara instan tanpa perlu memutus koneksi pemain atau merestart proses server.

---

## 📚 Sumber Referensi & Inspirasi

Proyek ini dibangun dengan menggabungkan keunggulan dari proyek-proyek open-source terkemuka:

1. **[open.mp (Open Multiplayer)](https://github.com/openmultiplayer/open.mp)**
   - Sebagai fondasi inti (*underlying core*): Netcode sinkronisasi pemain & kendaraan, pool sistem, entity management, dan sistem komponen C++ yang kokoh serta CAPI.
2. **[CitizenFX / FiveM](https://github.com/citizenfx/fivem)**
   - Inspirasi utama sistem resource modular: Siklus hidup resource (`Resource`, `ResourceManager`), struktur manifest (`fxmanifest.lua`), isolasi environment, dan mekanisme event bus.
3. **[Multi Theft Auto: San Andreas (MTA:SA)](https://github.com/multitheftauto/mtasa-blue)**
   - Referensi desain API Lua terbaik untuk dunia GTA: San Andreas, sinkronisasi entity dunia San Andreas, dan implementasi scripting sandbox.
4. **[Sol2](https://github.com/ThePhD/sol2)**
   - Binding library modern C++17/20 untuk Lua yang cepat, aman, dan efisien untuk menghubungkan Core C++ ke runtime Lua.

---

## 🗂 Struktur Direktori Server (Target Resa)

```text
resam/
├── components/                     # Komponen internal C++ server
│   ├── Lua/                        # Component runtime Lua & Resource Manager
│   └── ...                         # Core open.mp components (Vehicles, Objects, etc.)
├── resources/                      # Direktori semua modular resource
│   ├── [core]/
│   │   ├── resam_core/             # Resource inti (player session, shared state)
│   │   │   ├── fxmanifest.lua
│   │   │   └── server.lua
│   │   └── resam_database/         # Konektor database (MySQL/SQLite)
│   │       ├── fxmanifest.lua
│   │       └── server.lua
│   └── [gameplay]/
│       ├── spawn_manager/          # Manajemen spawn pemain
│       └── vehicle_shop/           # Sistem dealer kendaraan
└── config.json / server.cfg        # Konfigurasi server & auto-start resources
```

---

## 🚀 Panduan Membangun (Building)

Proyek ini menggunakan CMake dan compiler Clang/Clang-cl. Untuk detail instalasi dependensi dan instruksi build pada Windows, Linux, dan macOS, silakan baca [BUILD.md](BUILD.md).

---

## 📄 Lisensi

Proyek ini mewarisi lisensi Mozilla Public License 2.0 (MPL 2.0) dari open.mp, dan komponen baru dikembangkan di bawah lisensi yang kompatibel.
"# resa-server" 
