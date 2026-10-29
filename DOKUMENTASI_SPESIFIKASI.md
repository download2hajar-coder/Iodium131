# Spesifikasi Sistem HMI dan Firmware Dispensing Iodium I-131

## 1. Informasi Dokumen

| Item | Keterangan |
|---|---|
| Judul | Spesifikasi Sistem HMI dan Firmware Auto Dispensing Iodium |
| Komponen yang dispesifikasikan | `HMI_FINAL.py` dan `I_131_SingleFillSingleDispense_v6.ino` |
| Jenis dokumen | Spesifikasi *as-built* (mengikuti implementasi saat ini) |
| Bahasa | Indonesia |
| Basis kode | `D:\Iodium131` |

Berkas sumber:

| Berkas | Baris | Peran |
|---|---:|---|
| `HMI_FINAL.py` | 496 | Antarmuka operator berbasis Python/Tkinter. |
| `I_131_SingleFillSingleDispense_v6/I_131_SingleFillSingleDispense_v6.ino` | 494 | Firmware Arduino untuk homing, persiapan, dispensing, dan penutupan kapsul. |

Dokumen ini menggabungkan spesifikasi antarmuka, perilaku, data, protokol, batasan, dan temuan implementasi dari kedua berkas. Dokumen pendukung yang sudah ada: `HMI_FINAL.md` dan `I_131_SingleFillSingleDispense_v6/README.md`.

### 1.1 Tujuan

1. Menjadi acuan perilaku sistem yang benar-benar diterapkan oleh kode saat ini.
2. Mendefinisikan kontrak komunikasi antara HMI dan firmware.
3. Mendokumentasikan parameter, state, urutan gerak, dan aturan validasi.
4. Mencatat deviasi, risiko, dan batasan yang perlu diperbaiki atau divalidasi.

### 1.2 Di luar cakupan

Dokumen ini **bukan** validasi dosis, kalibrasi radiologis, analisis keselamatan formal, atau persetujuan penggunaan klinis. Validasi mekanik, limit switch, *emergency stop*, interlock, dan prosedur operator harus dilakukan oleh personel berwenang.

## 2. Ringkasan Sistem

Sistem terdiri dari dua komponen perangkat lunak:

- **HMI** menerima input operator, memvalidasi nilai, mengonversi volume ke jumlah step, mengirim perintah serial, menampilkan trafik serial, dan mencatat log.
- **Firmware Arduino** menerima perintah, mengendalikan empat motor stepper melalui driver, menjalankan homing, persiapan iodium, pengisian kapsul, penutupan, dan homing akhir.

```text
Operator
   │ input volume, faktor kalibrasi, jumlah kapsul
   ▼
HMI_FINAL.py (Python/Tkinter)
   │ validasi + konversi µL -> step
   │ <INIT:stepsPerCapsule;jumlahCapsule>\n
   ▼
Serial 9600 baud (8N1)
   ▼
I_131_SingleFillSingleDispense_v6.ino (Arduino)
   │ perintah gerak
   ▼
4 driver motor stepper + 4 limit switch
```

### 2.1 Tanggung jawab

| Komponen | Bertanggung jawab atas | Tidak bertanggung jawab atas |
|---|---|---|
| HMI | Validasi input, konversi µL ke step, pengiriman perintah, monitoring serial, logging, koneksi ulang. | Gerakan motor, homing, priming, urutan dispensing, keselamatan mekanis. |
| Firmware | Parsing perintah, homing, gerakan motor, urutan persiapan/dispensing/penutupan, pelaporan teks serial. | Konversi volume ke step, validasi batas volume, antarmuka operator. |

## 3. Kontrak Integrasi Utama

| Aspek | Nilai |
|---|---|
| Media | USB-serial / UART Arduino |
| Baud rate | 9600 |
| Format karakter | ASCII/teks, default 8 data bit, tanpa parity, 1 stop bit |
| Pembungkus perintah | `<` dan `>` |
| Terminator | HMI menambahkan `\n`; firmware mengakhiri perintah saat `\n` atau `>` diterima |
| Perintah utama | `<INIT:stepsPerCapsule;jumlahCapsule>` |
| Satuan nilai INIT | Jumlah step motor, **bukan** mikroliter |
| Mode eksekusi firmware | *Blocking*; perintah berikutnya tidak diproses sampai siklus selesai |
| Umpan balik status terstruktur | Tidak ada; firmware hanya mencetak teks bebas |

## 4. Spesifikasi Antarmuka Serial

### 4.1 Format frame

```text
<PERINTAH>
```

- HMI mengirim `<INIT:20330;4>` diikuti newline.
- Firmware juga dapat menerima beberapa perintah dalam satu buffer, misalnya `<M2;1000;0><READPOS>`.
- Teks di luar pasangan `<...>` diabaikan oleh `processQueue()`.
- Perintah tanpa pembungkus `<...>` tidak akan dieksekusi.

### 4.2 Daftar perintah

| Perintah | Pengirim | Sintaks | Validasi firmware | Efek |
|---|---|---|---|---|
| INIT | HMI atau manual | `<INIT:steps;caps>` | `caps` 1–5, `steps` > 0 | Menjalankan `prepareIodium()` lalu `runAutoDispense()`. |
| Gerak motor | Manual | `<M<n>;steps;arah>` | `n` 1–4 | Menggerakkan motor `n` sebanyak `steps` dengan arah `0`/`1`. |
| Homing semua | Manual | `<HOME:ALL>` | Tidak ada | Homing M4 → M3 → M2 → M1, lalu mencetak rekap. |
| Homing satu motor | Manual | `<HOME:n>` | `n` 1–4 | Homing motor `n`, lalu mencetak jumlah langkah. |
| Baca posisi | Manual | `<READPOS>` | Harus sama persis | Mencetak `posAbs[1..4]`. |

### 4.3 Semantik arah

| Nilai `arah` | Sinyal DIR | Efek pada `posAbs` |
|---:|---|---|
| `0` | LOW | `posAbs += langkah` |
| `1` | HIGH | `posAbs -= langkah` |

Arti fisik arah bergantung pada wiring motor/driver. Firmware tidak memvalidasi bahwa `arah` hanya `0` atau `1`.

### 4.4 Respons firmware yang benar-benar dikirim

| Kondisi | Teks serial |
|---|---|
| Startup per motor | `Homing motor n`, `Motor n langkah menuju HOME = X` |
| Startup selesai | `Sistem siap. Masukkan perintah...` |
| INIT diterima | `Volume per kapsul (uL): 0.00`, `Step per kapsul: X`, `Total steps: Y` |
| Awal prepare | `=== FASE PREPARE IODIUM ===` |
| Prepare selesai | `=== PREPARE SELESAI, SIAP DISPENSE ===` |
| Awal dispensing | `=== AUTO DISPENSE PIPELINE SYRINGE ===` |
| Per kapsul | `=== KAPSUL 1 (hasil prepare) ===`, `=== PROSES KAPSUL n ===` |
| Bypass slot | `=== BYPASS KAPSUL n ===` |
| Penutup | `=== FASE PENUTUP ===` |
| Homing akhir | `HOME:ALL otomatis...` |
| Selesai | `=== AUTO SELESAI ===`, `OK: INIT Accepted` |
| Format INIT salah | `ERR: INIT Format Wrong` |
| Nilai INIT di luar rentang | `ERR: INIT Value Out of Range` |
| Format M salah | `Format M invalid` |
| Nomor motor salah | `Motor invalid` |
| HOME tidak valid | `Perintah HOME invalid.` |
| Perintah tidak dikenali | `Silakan masukkan perintah lagi : ` |

Firmware **tidak** mengirim `READY`, `BUSY`, `FILLING`, `DONE`, atau `ERROR` sebagai status terstruktur.

## 5. Spesifikasi HMI (`HMI_FINAL.py`)

### 5.1 Kebutuhan fungsional

| ID | Kebutuhan | Implementasi |
|---|---|---|
| HMI-FR-01 | Menyediakan input volume per kapsul dalam µL. | `entry_volume` |
| HMI-FR-02 | Menyediakan input faktor kalibrasi step/µL. | `entry_kal`, default `2033` |
| HMI-FR-03 | Menyediakan pilihan jumlah kapsul 1–5. | Combobox `jumlah_combo`, default `1` |
| HMI-FR-04 | Memvalidasi volume dan faktor > 0. | `send_init_command()` |
| HMI-FR-05 | Memvalidasi jumlah kapsul 1–5. | `send_init_command()` |
| HMI-FR-06 | Membatasi volume maksimum 10,0 µL pada alur INIT. | `MAX_VOLUME_UL` |
| HMI-FR-07 | Mengonversi volume ke step. | `step = int(round(volume × faktor))` |
| HMI-FR-08 | Mengirim perintah INIT ke Arduino. | `safe_write()` |
| HMI-FR-09 | Menampilkan trafik TX/RX. | `monitor`, `append_monitor()` |
| HMI-FR-10 | Menampilkan status proses. | `status_label`, `status_var` |
| HMI-FR-11 | Menyediakan progress bar selama pengiriman. | `progress` mode indeterminate |
| HMI-FR-12 | Melakukan auto-reconnect setiap 2 detik. | `auto_reconnect_loop()` |
| HMI-FR-13 | Menyediakan koneksi manual. | Tombol `Refresh COM` |
| HMI-FR-14 | Menyediakan perintah manual bebas. | `entry_command`, `send_manual_command()` |
| HMI-FR-15 | Mencatat aktivitas ke CSV. | `log_to_csv()` |
| HMI-FR-16 | Menyediakan tema gelap/terang. | `toggle_theme()` |
| HMI-FR-17 | Menutup serial saat aplikasi keluar. | `on_closing()` |

### 5.2 Konfigurasi

| Konstanta | Nilai | Lokasi | Keterangan |
|---|---:|---|---|
| `BAUDRATE` | 9600 | `HMI_FINAL.py:18` | Baud rate serial. |
| `RECONNECT_INTERVAL_MS` | 2000 | `HMI_FINAL.py:19` | Interval auto-reconnect. |
| `SERIAL_READ_SLEEP` | 0.1 | `HMI_FINAL.py:20` | Jeda saat tidak ada data (detik). |
| `MAX_VOLUME_UL` | 10.0 | `HMI_FINAL.py:21` | Batas keras volume per kapsul. |
| `LOG_DIR` | `logs` | `HMI_FINAL.py:22` | Direktori log relatif terhadap direktori kerja. |
| `LOG_FILE` | `logs/dispensing_log.csv` | `HMI_FINAL.py:23` | Berkas log. |
| Faktor default | 2033 | `HMI_FINAL.py:373` | Nilai awal input faktor. |
| Pilihan kapsul | 1–5 | `HMI_FINAL.py:387` | Nilai Combobox. |

Contoh: volume `10 µL` dengan faktor `2033` menghasilkan `20330` step, sesuai contoh `<INIT:20330;4>`.

### 5.3 State global

| Variabel | Nilai awal | Fungsi |
|---|---|---|
| `ser` | `None` | Objek `serial.Serial` aktif. |
| `serial_lock` | `threading.Lock()` | Sinkronisasi akses serial. |
| `running` | `True` | Penanda thread reader masih berjalan. |
| `theme_dark` | `True` | Status tema. |

### 5.4 Peta widget

| Widget | Lokasi | Fungsi |
|---|---|---|
| `root` | `HMI_FINAL.py:350` | Jendela utama 720×640, minimum 720×640. |
| `title` | `HMI_FINAL.py:359` | Judul aplikasi. |
| `entry_volume` | `HMI_FINAL.py:380` | Input volume per kapsul (µL). |
| `entry_kal` | `HMI_FINAL.py:374` | Input faktor kalibrasi. |
| `jumlah_combo` | `HMI_FINAL.py:387` | Pilihan jumlah kapsul. |
| `btn_send` | `HMI_FINAL.py:395` | Tombol `KIRIM KE ARDUINO`. |
| `progress` | `HMI_FINAL.py:398` | Progress bar indeterminate, 220 px. |
| `status_label` | `HMI_FINAL.py:402` | Label status yang terlihat operator. |
| `status_var` | `HMI_FINAL.py:404` | Status internal untuk watcher timeout. |
| `monitor` | `HMI_FINAL.py:410` | Serial monitor read-only. |
| `entry_command` | `HMI_FINAL.py:420` | Input perintah manual. |
| `btn_send_cmd` | `HMI_FINAL.py:422` | Tombol kirim perintah manual. |
| `theme_btn` | `HMI_FINAL.py:426` | Toggle tema. |
| `btn_refresh` | `HMI_FINAL.py:456` | Tombol `Refresh COM`. |
| `status_bar` | `HMI_FINAL.py:460` | Label bawah; dibuat `"Ready."` dan tidak pernah diperbarui. |

### 5.5 Alur validasi dan pengiriman INIT

```text
Klik "KIRIM KE ARDUINO"
  ├─ baca volume, faktor, jumlah
  ├─ volume/faktor harus float > 0
  ├─ jumlah harus integer 1..5
  ├─ volume > 10.0 µL?
  │    └─ ya → tulis log LOCK + tampilkan error → berhenti
  ├─ step = int(round(volume × faktor))
  ├─ step <= 0?
  │    └─ ya → tampilkan error → berhenti
  ├─ tulis log SENT
  ├─ bentuk "<INIT:step;jumlah>"
  ├─ tampilkan perhitungan di monitor
  ├─ progress.start(10)
  ├─ status_var = "Sending INIT..."
  ├─ kirim perintah + "\n"
  └─ thread watch_timeout(6 detik)  [selalu dijalankan]
       ├─ status mengandung DONE/ERROR → stop progress
       └─ timeout → stop progress
```

Aturan validasi:

| Parameter | Aturan | Perilaku bila gagal |
|---|---|---|
| Volume | Float > 0 dan ≤ 10,0 µL | Warning/error; INIT tidak dikirim. |
| Faktor | Float > 0 | Warning; INIT tidak dikirim. |
| Jumlah kapsul | Integer 1–5 | Warning; INIT tidak dikirim. |
| Hasil step | > 0 | Error; INIT tidak dikirim. |

Catatan: batas 10,0 µL hanya berlaku pada jalur `send_init_command()`. Perintah manual tidak melewati validasi ini.

### 5.6 Perilaku status masuk

`handle_incoming_status()` memetakan kata kunci berikut pada setiap baris RX:

| Kata kunci | Status label | Warna |
|---|---|---|
| `READY` | `Status: READY` | lime |
| `BUSY` | `Status: BUSY` | orange |
| `FILLING` atau `FILL` | `Status: FILLING` | yellow |
| `DONE`, `FINISHED`, `COMPLETE` | `Status: DONE` | cyan |
| `ERROR`, `FAIL` | `Status: ERROR` | red |

Setelah pemetaan, fungsi selalu memanggil:

```python
log_to_csv(status="DONE", note="Dispensing complete")
```

Akibatnya setiap baris RX dicatat sebagai `DONE`, termasuk baris `READY`, pesan tahapan, dan error.

### 5.7 Koneksi serial

- Auto-reconnect berjalan melalui `root.after()` setiap 2000 ms.
- Port dicari dengan heuristik: deskripsi mengandung `arduino`, `ch340`, `usb-serial`, `ft232`, atau `usb serial`; fallback `usb` + (`serial` atau `com`).
- Port dibuka dengan `serial.Serial(port, 9600, timeout=1)`.
- Setelah membuka port, HMI menunggu 1,5 detik.
- `Refresh COM` melakukan hal serupa dengan jeda 1,2 detik dan hanya jika belum terhubung.
- Tidak ada pemilihan port manual, pencocokan VID/PID, atau verifikasi firmware.

### 5.8 Thread dan konkurensi

| Thread | Sifat | Pekerjaan |
|---|---|---|
| Serial reader | Daemon | Loop `readline()` selama `running = True`. |
| Timeout watcher | Daemon | Berhenti setelah 6 detik atau status DONE/ERROR. |
| Auto-reconnect | Callback `root.after` | Berjalan di thread utama setiap 2 detik. |

- `serial_lock` melindungi pembacaan, penulisan, dan penutupan serial.
- `readline()` menahan lock hingga timeout 1 detik, sehingga pengiriman dari UI dapat tertunda.
- `append_monitor()` dan `handle_incoming_status()` mengubah widget Tkinter langsung dari thread serial.
- `auto_reconnect_loop()` dan `manual_refresh()` memanggil `time.sleep()` di thread utama, sehingga UI dapat membeku sesaat saat koneksi.

### 5.9 Log CSV

Lokasi: `logs/dispensing_log.csv` (relatif terhadap direktori kerja).

Header:

```csv
timestamp,volume_ul,faktor_step_per_ul,step_per_capsule,jumlah_kapsul,status,keterangan
```

Status yang dapat ditulis:

| Status | Kapan | Catatan |
|---|---|---|
| `SENT` | Sebelum `safe_write()` | Tetap ditulis meskipun pengiriman gagal. |
| `LOCK` | Volume > 10,0 µL | Disertai keterangan batas. |
| `DONE` | Setiap baris RX | Ditulis tanpa syarat. |
| `ERROR` | Tidak pernah | Pemanggilan berada di cabang duplikat yang tidak dapat dicapai. |

Log dibuka dengan mode append, header ditulis hanya jika file belum ada, dan tidak ada rotasi atau pembatasan ukuran.

### 5.10 Fitur yang tidak tersedia

- Pemilihan port dan baud dari UI.
- Tombol *emergency stop* atau pembatalan siklus.
- Pembacaan status terstruktur dari firmware.
- Penyimpanan pengaturan.
- Rotasi log.
- Pengujian otomatis dan `requirements.txt`.
- Indikator selesai yang sinkron dengan siklus firmware.

## 6. Spesifikasi Firmware (`I_131_SingleFillSingleDispense_v6.ino`)

### 6.1 Pin dan konfigurasi listrik

| Fungsi | Pin | Konfigurasi |
|---|---:|---|
| STEP bersama | 7 | Output |
| DIR bersama | 6 | Output |
| ENABLE M1 | 8 | Output, aktif `LOW`, nonaktif `HIGH` |
| ENABLE M2 | 9 | Output, aktif `LOW` |
| ENABLE M3 | 10 | Output, aktif `LOW` |
| ENABLE M4 | 11 | Output, aktif `LOW` |
| Limit M1 | 3 | `INPUT_PULLUP` |
| Limit M2 | 4 | `INPUT_PULLUP` |
| Limit M3 | 5 | `INPUT_PULLUP` |
| Limit M4 | 2 | `INPUT_PULLUP` |
| Serial | TX/RX board | 9600 baud |

Semua motor berbagi pin STEP dan DIR. Motor dipilih dengan mengaktifkan pin ENABLE masing-masing. Limit dianggap belum aktif saat terbaca `HIGH`; homing berhenti saat input menjadi `LOW`.

### 6.2 Pemetaan fungsi motor

| Motor | Peran yang tersirat dari urutan program |
|---|---|
| M1 | Indexer posisi kapsul dan posisi penutup. |
| M2 | Plunger/syringe untuk menyedot dan mengeluarkan cairan. |
| M3 | Pemosisi syringe/nozzle antara botol dan kapsul. |
| M4 | Mekanisme penutup kapsul. |

### 6.3 Model gerak

**`stepPulse(delayUs)`**

```text
STEP HIGH selama delayUs
STEP LOW  selama delayUs
periode = 2 × delayUs
```

**`moveMotorAccel(id, langkah, arah)`**

| Bagian | Jumlah pulsa | Delay |
|---|---|---|
| Akselerasi | `accelSteps = max(1, langkah / 8)` | `800 → 80 µs` |
| Cruise | `langkah / 2` | `80 µs` |
| Deselerasi | `accelSteps` | `80 → 800 µs` |

Jumlah pulsa aktual kira-kira:

```text
2 × (langkah / 8) + (langkah / 2) = 0,75 × langkah
```

Untuk `langkah` besar, motor hanya menerima sekitar **75%** pulsa dari nilai yang diminta. `posAbs` tetap diperbarui sebesar `langkah` penuh, bukan jumlah pulsa aktual.

**`homingMotor(id)`**

1. Aktifkan driver (`ENABLE` LOW).
2. Set arah sesuai `homingDir[id-1]`.
3. Bergerak terus sampai limit terbaca `LOW` dengan `stepPulse(80)`.
4. Mundur 50 pulsa dengan `stepPulse(250)`.
5. Maju kembali sampai limit terbaca `LOW` dengan `stepPulse(300)`.
6. Nonaktifkan driver (`ENABLE` HIGH).
7. Kembalikan jumlah pulsa yang dihitung.

Tidak ada timeout, debounce, atau verifikasi tambahan pada homing.

### 6.4 Parameter mekanis

| Nama | Nilai | Dipakai? | Keterangan |
|---|---:|---|---|
| `stepsPerRev` | 1600 | Tidak | Data step per putaran. |
| `leadMotor1_mmPerRev` | 2.0 | Tidak | Lead screw M1. |
| `leadMotor2_mmPerRev` | 1.0 | Tidak | Lead screw M2. |
| `leadMotor3_mmPerRev` | 1.0 | Tidak | Lead screw M3. |
| `leadMotor4_mmPerRev` | 8.0 | Tidak | Lead screw M4. |
| `homingDir[4]` | `{1, 1, 0, 0}` | Ya | Arah homing M1–M4. |
| `jumlahCapsule` | 0 | Ya | Ditetapkan oleh INIT. |
| `stepsPerCapsule` | 0 | Ya | Ditetapkan oleh INIT. |
| `totalVolumeSteps` | 0 | Tidak | Hanya dicetak. |
| `volumePerCapsule_uL` | 0 | Tidak | Tidak pernah diisi; selalu tercetak 0. |
| `posAbs[1..4]` | 0 | Ya | Estimasi posisi internal relatif terhadap home. |
| `homingOffset[1..4]` | 0 | Tidak | Hanya dicatat/dicetak. |

### 6.5 Startup

`setup()` melakukan:

1. `Serial.begin(9600)`.
2. Set pin ENABLE sebagai output dan nonaktif (`HIGH`).
3. Set pin limit sebagai `INPUT_PULLUP`.
4. Set pin STEP dan DIR sebagai output.
5. Homing berurutan: **M4 → M3 → M2 → M1**.
6. Setiap `posAbs` motor yang di-home menjadi 0.
7. Cetak `Sistem siap. Masukkan perintah...`.

Homing awal berjalan otomatis setiap board melakukan reset. Membuka port serial dari HMI dapat memicu reset dan homing ulang pada board yang menghubungkan DTR ke reset.

### 6.6 Pemrosesan perintah

```text
serialEvent()
  └─ kumpulkan karakter sampai '\n' atau '>'
       └─ stringComplete = true
            └─ loop() → processQueue(inputString)
                 └─ cari setiap pasangan <...>
                      └─ executeCommand(isi perintah)
```

`executeCommand()` menangani:

1. Perintah `M...` untuk gerak motor langsung.
2. Perintah `INIT:...` untuk siklus otomatis.
3. Perintah `HOME:ALL` dan `HOME:n`.
4. Perintah `READPOS`.
5. Perintah lain → cetak prompt.

Catatan: setelah blok INIT selesai, eksekusi tidak langsung `return`, sehingga perintah INIT juga berakhir di cabang perintah tidak dikenali dan mencetak prompt tambahan.

### 6.7 Urutan otomatis

#### 6.7.1 `prepareIodium()`

| Langkah | Motor | Jumlah | Arah | Tujuan menurut komentar |
|---:|---|---:|---:|---|
| 1 | M2 | 113000 | 0 | Plunger ke posisi kosong. |
| 2 | M3 | 135000 | 1 | Masuk ke botol iodium. |
| 3 | M2 | `stepsPerCapsule` | 1 | Priming awal. |
| 4 | M3 | 110000 | 0 | Keluar botol dan standby. |

#### 6.7.2 `runAutoDispense()`

Konstanta posisi:

| Konstanta | Nilai | Keterangan |
|---|---:|---|
| `POS_BOTOL` | 0 | Dideklarasikan, tidak dipakai dalam target. |
| `POS_KAPSUL1` | 31500 | Posisi kapsul pertama. |
| `JARAK_KAPSUL` | 16000 | Jarak antarkapsul. |
| `TOTAL_SLOT` | 5 | Jumlah slot kapsul. |
| `POS_KAPSUL5` | 95500 | Kapsul kelima. |
| `POS_PENUTUP` | 227500 | Posisi mekanisme penutup. |

Urutan:

1. **Kapsul 1 (hasil prepare)**
   - M1 bergerak ke `31500` berdasarkan selisih `posAbs[1]`.
   - M3 bergerak `55000` arah `1`.
   - M2 mengeluarkan `stepsPerCapsule` arah `0`.
   - M3 bergerak `55000` arah `0`.

2. **Kapsul 2 sampai `jumlahCapsule`**
   - M1 kembali ke posisi botol/home dengan jarak `posAbs[1]`, arah `1`.
   - M3 bergerak `110000` arah `1` menuju botol.
   - M2 menyedot `stepsPerCapsule` arah `1`.
   - M3 bergerak `110000` arah `0`.
   - M1 bergerak ke target kapsul: `31500 + (i - 1) × 16000`.
   - M3 bergerak `55000` arah `1`.
   - M2 mengeluarkan `stepsPerCapsule` arah `0`.
   - M3 bergerak `55000` arah `0`.

3. **Bypass slot tersisa**
   - Untuk `i = jumlahCapsule + 1` sampai `5`, M1 bergerak ke target slot tanpa dispensing.

4. **Penutupan**
   - M1 bergerak ke `227500`.
   - M4 bergerak `16000` arah `1`.

5. **Homing akhir**
   - Homing M4, M3, dan M2.
   - Homing M1 **tidak dijalankan** (baris dikomentari).
   - `posAbs[1]` tetap berada di nilai posisi penutup setelah siklus selesai.

### 6.8 State setelah satu siklus

| State | Nilai setelah siklus normal |
|---|---|
| `posAbs[4]`, `posAbs[3]`, `posAbs[2]` | 0 (hasil homing akhir). |
| `posAbs[1]` | Tidak di-reset; tetap sekitar `227500` relatif terhadap home awal. |
| `jumlahCapsule`, `stepsPerCapsule` | Tetap menyimpan nilai INIT terakhir. |
| `homingOffset` | Diperbarui untuk M2–M4. |
| Driver | Nonaktif setelah setiap gerakan/homing. |

### 6.9 Batasan firmware

- Semua gerakan bersifat blocking.
- Tidak ada pembatalan atau *emergency stop* dari parser.
- Tidak ada timeout homing.
- Tidak ada encoder atau umpan balik posisi.
- Tidak ada validasi arah atau jumlah step negatif.
- `posAbs` adalah estimasi berdasarkan nilai yang diminta, bukan pulsa aktual.
- Perintah yang dikirim selama siklus dapat menumpuk di buffer serial dan diproses setelah siklus selesai, atau hilang jika buffer penuh.
- Input INIT adalah step, bukan volume; konversi volume hanya dilakukan HMI.
- Tidak ada batas volume maksimum di firmware.

## 7. Spesifikasi Integrasi End-to-End

### 7.1 Skenario normal

1. Firmware diunggah dan board melakukan homing awal M4 → M3 → M2 → M1.
2. HMI dijalankan; auto-reconnect membuka port dan menampilkan `READY`.
3. Operator mengisi volume, faktor, dan jumlah kapsul.
4. HMI memvalidasi input dan menghitung `step = round(volume × faktor)`.
5. HMI menulis log `SENT` dan mengirim `<INIT:step;jumlah>\n`.
6. Firmware memvalidasi jumlah kapsul dan step, lalu menjalankan prepare dan dispensing.
7. Selama siklus, HMI menampilkan setiap baris RX dan mencatat setiap baris sebagai `DONE`.
8. Progress bar HMI berhenti setelah 6 detik, tanpa menunggu selesainya siklus firmware.
9. Firmware mencetak `=== AUTO SELESAI ===` dan `OK: INIT Accepted`.
10. HMI tidak mengenali pesan tersebut sebagai `DONE`; `status_label` umumnya tetap `READY`.

### 7.2 Matriks kompatibilitas

| Aspek | HMI | Firmware | Status |
|---|---|---|---|
| Baud rate | 9600 | 9600 | Sesuai |
| Terminator | `\n` | `\n` atau `>` | Sesuai |
| Pembungkus | `<...>` | `<...>` | Sesuai |
| Satuan INIT | Step | Step | Sesuai |
| Rentang kapsul | 1–5 | 1–5 | Sesuai |
| Batas volume | 10,0 µL | Tidak ada | Hanya di HMI |
| Status selesai | `DONE`/`FINISHED`/`COMPLETE` | `AUTO SELESAI`/`OK: INIT Accepted` | Tidak sesuai |
| Status error | `ERROR`/`FAIL` | `ERR:` | Tidak sesuai |
| Status siap | `READY` | `Sistem siap...` | Tidak sesuai |
| Batas step | Tidak ada batas atas | `long` 32-bit | Berisiko overflow |
| Pembatalan | Tidak ada | Tidak ada | Tidak tersedia |

### 7.3 Konsekuensi ketidaksesuaian status

- `status_label` tidak mengikuti siklus firmware; nilainya berasal dari koneksi (`READY`) atau kata kunci yang tidak pernah dikirim firmware.
- Progress bar tidak merepresentasikan durasi siklus sebenarnya; selalu berhenti setelah 6 detik.
- Log RX tidak dapat dipercaya untuk membedakan sukses, selesai, atau error karena setiap baris dicatat `DONE`.
- Operator tidak memperoleh indikator bahwa siklus otomatis telah benar-benar selesai.

## 8. Keselamatan dan Non-Fungsional

### 8.1 Keselamatan

1. Tidak ada *emergency stop* perangkat lunak di HMI maupun firmware.
2. Firmware blocking tidak dapat memproses perintah stop selama siklus berlangsung.
3. Homing tanpa timeout dapat terus berjalan jika limit switch gagal.
4. Pembukaan port serial dapat mereset Arduino dan memicu homing otomatis.
5. Batas volume 10,0 µL hanya ditegakkan di HMI; perintah manual dan firmware dapat melewatinya.
6. Tidak ada verifikasi posisi fisik; kehilangan langkah tidak terdeteksi.
7. Tidak ada interlock antara status firmware dan pengiriman perintah baru.
8. Nilai step yang sangat besar dapat melebihi kapasitas `long` Arduino.

### 8.2 Non-fungsional

| Aspek | Kondisi saat ini |
|---|---|
| Keandalan | Tidak ada heartbeat, checksum, atau acknowledgement terstruktur. |
| Robustness | Tidak ada timeout homing; validasi perintah terbatas. |
| Responsivitas UI | Dapat membeku 1,2–1,5 detik saat membuka port. |
| Thread safety | Widget Tkinter diperbarui dari thread serial. |
| Logging | Tanpa lock, tanpa rotasi, status DONE tidak akurat. |
| Portabilitas | Path log relatif; deteksi port bergantung pada deskripsi OS. |
| Observability | Hanya teks serial bebas dan CSV. |
| Pengujian | Tidak ada unit test, integration test, atau simulasi. |

## 9. Deviasi dan Temuan Implementasi

| ID | Severity | Temuan | Dampak |
|---|---|---|---|
| D-01 | Tinggi | `moveMotorAccel()` menghasilkan sekitar 75% pulsa dari `langkah`. | Volume dan posisi dapat meleset kecuali dikompensasi kalibrasi. |
| D-02 | Tinggi | Homing M1 tidak dijalankan di akhir siklus; `posAbs[1]` tetap di posisi penutup. | INIT berikutnya tanpa `<HOME:ALL>` dapat menggerakkan M1 ke posisi salah. |
| D-03 | Tinggi | HMI tidak mengenali `OK: INIT Accepted`, `AUTO SELESAI`, atau `ERR:`. | Status selesai/error tidak pernah tampil akurat. |
| D-04 | Tinggi | Perintah manual dapat mengirim INIT melewati batas 10,0 µL. | Batas keras HMI dapat dilewati. |
| D-05 | Tinggi | Tidak ada emergency stop; firmware blocking. | Tidak ada penghentian perangkat lunak saat siklus berjalan. |
| D-06 | Tinggi | Homing tanpa timeout. | Motor dapat terus berjalan jika limit gagal. |
| D-07 | Sedang | Setiap baris RX dicatat sebagai `DONE`. | Log tidak dapat dijadikan bukti status. |
| D-08 | Sedang | Progress bar berhenti setelah 6 detik. | Indikator selesai menyesatkan; siklus firmware lebih lama. |
| D-09 | Sedang | Widget Tkinter diubah dari thread serial. | Risiko crash atau perilaku UI tidak stabil. |
| D-10 | Sedang | `readline()` menahan `serial_lock` hingga 1 detik. | Pengiriman perintah tertunda. |
| D-11 | Sedang | `posAbs` bukan umpan balik fisik. | Kehilangan langkah tidak terdeteksi. |
| D-12 | Sedang | Tidak ada validasi arah/step negatif di firmware. | Perintah manual dapat menyebabkan gerak tak terduga. |
| D-13 | Sedang | Tidak ada batas atas step; `long` Arduino 32-bit. | Overflow untuk nilai besar. |
| D-14 | Sedang | Deteksi port hanya heuristik. | Dapat terhubung ke perangkat USB-serial yang salah. |
| D-15 | Rendah | Log `SENT` ditulis sebelum pengiriman. | Log menyatakan terkirim meski gagal. |
| D-16 | Rendah | Cabang DONE/ERROR duplikat tidak dapat dicapai. | Kode membingungkan; log ERROR tidak pernah ditulis. |
| D-17 | Rendah | `status_bar` tidak pernah diperbarui. | Selalu menampilkan `"Ready."`. |
| D-18 | Rendah | INIT tidak `return` setelah selesai. | Mencetak prompt perintah tambahan. |
| D-19 | Rendah | `volumePerCapsule_uL` selalu 0. | Pesan serial menyesatkan. |
| D-20 | Rendah | `manufacturer` dihitung tetapi tidak dipakai. | Deteksi port tidak memanfaatkan informasi vendor. |
| D-21 | Rendah | Auto-reconnect/Refresh memblokir UI dengan `sleep()`. | UI membeku sesaat. |
| D-22 | Rendah | Log tanpa lock dan tanpa rotasi. | Risiko baris rusak dan file membesar. |
| D-23 | Rendah | NaN/Inf pada input float tidak ditangani khusus. | `round()` dapat melempar exception. |

## 10. Checklist Uji Penerimaan

| ID | Uji | Langkah | Hasil yang diharapkan |
|---|---|---|---|
| T-01 | Konversi volume | Isi 10 µL, faktor 2033, 4 kapsul; kirim. | TX `<INIT:20330;4>`; log SENT. |
| T-02 | Batas volume | Isi 10,1 µL; kirim. | Ditolak; log LOCK; tidak ada TX. |
| T-03 | Validasi input | Isi volume 0, faktor 0, atau jumlah di luar 1–5. | Warning; tidak ada TX. |
| T-04 | Auto-connect | Jalankan HMI tanpa membuka port manual. | Port ditemukan dan status READY. |
| T-05 | Refresh COM | Klik tombol Refresh COM. | Port dibuka atau pesan sudah terhubung. |
| T-06 | Perintah manual | Kirim `<READPOS>`. | TX terlihat di monitor. |
| T-07 | Homing awal | Nyalakan Arduino. | Urutan M4 → M3 → M2 → M1; rekap tercetak. |
| T-08 | Validasi INIT firmware | Kirim `<INIT:0;4>` dan `<INIT:1600;6>`. | `ERR: INIT Value Out of Range`. |
| T-09 | Format INIT salah | Kirim `<INIT:1600>`. | `ERR: INIT Format Wrong`. |
| T-10 | Siklus 1 kapsul | Kirim `<INIT:1600;1>`. | Prepare, kapsul 1, bypass slot 2–5, penutup, homing M4–M2. |
| T-11 | Siklus 5 kapsul | Kirim `<INIT:1600;5>`. | Lima kapsul diisi tanpa bypass. |
| T-12 | Baca posisi | Kirim `<READPOS>` setelah gerakan. | `posAbs` M1–M4 tercetak. |
| T-13 | Status selesai HMI | Amati selama dan setelah siklus. | HMI tidak menampilkan DONE dari firmware. |
| T-14 | Log RX | Amati CSV setelah pesan READY/stage. | Setiap baris tercatat DONE. |
| T-15 | Durasi progress | Kirim INIT dengan gerakan panjang. | Progress berhenti setelah 6 detik. |
| T-16 | Jumlah pulsa | Ukur pulsa untuk `langkah` besar. | Sekitar 75% dari nilai yang diminta. |
| T-17 | Posisi M1 antar-siklus | Jalankan dua INIT berturut-turut tanpa HOME. | Gerakan M1 tidak sesuai posisi kapsul. |
| T-18 | Limit gagal | Lepas/block limit switch saat homing. | Homing tidak berhenti (tidak ada timeout). |
| T-19 | Emergency stop | Cari tombol stop saat siklus. | Tidak tersedia. |
| T-20 | Batas volume manual | Kirim `<INIT:999999;5>` via perintah manual. | Diterima firmware; batas HMI terlewati. |
| T-21 | Log error | Kirim pesan berisi ERROR dari firmware/simulator. | Status ERROR tampil, tetapi log hanya DONE. |
| T-22 | Responsivitas UI | Klik Refresh COM. | UI membeku sekitar 1,2 detik. |

## 11. Traceability Kode

### 11.1 HMI

| Bagian | Lokasi |
|---|---|
| Import dan komentar | `HMI_FINAL.py:1-12` |
| Konfigurasi | `HMI_FINAL.py:15-23` |
| Global state | `HMI_FINAL.py:25-30` |
| Helper logging/serial | `HMI_FINAL.py:32-118` |
| Aksi INIT dan manual | `HMI_FINAL.py:120-222` |
| Serial reader | `HMI_FINAL.py:224-248` |
| Handler status | `HMI_FINAL.py:251-287` |
| Auto-reconnect | `HMI_FINAL.py:289-315` |
| Tema | `HMI_FINAL.py:317-345` |
| Pembangunan UI | `HMI_FINAL.py:347-464` |
| Start thread dan reconnect | `HMI_FINAL.py:466-472` |
| Cleanup | `HMI_FINAL.py:474-484` |
| Entry point | `HMI_FINAL.py:486-496` |

### 11.2 Firmware

| Bagian | Lokasi |
|---|---|
| Konfigurasi pin dan mekanik | `I_131_SingleFillSingleDispense_v6.ino:7-20` |
| Variabel operasional | `I_131_SingleFillSingleDispense_v6.ino:23-38` |
| Prototipe | `I_131_SingleFillSingleDispense_v6.ino:41-49` |
| `setup()` | `I_131_SingleFillSingleDispense_v6.ino:54-73` |
| `loop()` dan `serialEvent()` | `I_131_SingleFillSingleDispense_v6.ino:78-95` |
| `homingMotor()` | `I_131_SingleFillSingleDispense_v6.ino:100-126` |
| `stepPulse()` | `I_131_SingleFillSingleDispense_v6.ino:131-136` |
| `moveMotorAccel()` | `I_131_SingleFillSingleDispense_v6.ino:141-173` |
| `prepareIodium()` | `I_131_SingleFillSingleDispense_v6.ino:180-197` |
| `runAutoDispense()` | `I_131_SingleFillSingleDispense_v6.ino:204-280` |
| `processQueue()` | `I_131_SingleFillSingleDispense_v6.ino:285-294` |
| `executeCommand()` | `I_131_SingleFillSingleDispense_v6.ino:301-393` |
| Utilitas parser | `I_131_SingleFillSingleDispense_v6.ino:398-414` |
| Contoh perintah di komentar | `I_131_SingleFillSingleDispense_v6.ino:418-494` |

## 12. Rekomendasi Perbaikan

Prioritas tinggi:

1. Samakan protokol status HMI dan firmware, atau tambahkan acknowledgement terstruktur setelah siklus selesai.
2. Perbaiki perhitungan pulsa pada `moveMotorAccel()` agar sesuai dengan `langkah`.
3. Reset posisi M1 dengan homing atau sinkronisasi posisi setelah siklus.
4. Terapkan batas volume/step juga pada perintah manual dan firmware.
5. Tambahkan emergency stop perangkat keras dan pertimbangkan pemrosesan stop di firmware.
6. Tambahkan timeout dan penanganan fault pada homing.

Prioritas sedang:

7. Pindahkan pembaruan widget ke thread utama melalui `root.after()`.
8. Lepaskan `serial_lock` sebelum memproses data RX.
9. Gunakan timeout pembacaan yang lebih pendek dan mekanisme reopen.
10. Tambahkan validasi arah, step negatif, dan batas atas step.
11. Tambahkan pemilihan port manual dan pencocokan VID/PID.
12. Perbaiki logging: catat status sesuai kondisi, tambahkan lock, dan rotasi file.
13. Perpanjang atau ganti timeout progress dengan status firmware yang nyata.

## 13. Glosarium

| Istilah | Arti |
|---|---|
| HMI | *Human-Machine Interface*, antarmuka operator. |
| µL | Mikroliter. |
| Step | Satu pulsa pergerakan motor stepper. |
| Faktor kalibrasi | Jumlah step per mikroliter. |
| INIT | Perintah untuk memulai persiapan dan siklus dispensing. |
| Homing | Proses mencari posisi referensi menggunakan limit switch. |
| Priming | Pengambilan awal cairan untuk membuang udara. |
| `posAbs` | Estimasi posisi absolut internal relatif terhadap home. |
| Blocking | Fungsi yang menahan eksekusi sampai selesai. |
| RX/TX | Data diterima/dikirim melalui serial. |
| CSV | *Comma-Separated Values*, format log. |

---

Dokumen ini mengikuti implementasi `HMI_FINAL.py` dan `I_131_SingleFillSingleDispense_v6.ino` pada revisi saat ini. Perubahan kode, wiring, mekanik, atau parameter kalibrasi harus disertai pembaruan dokumen dan pengujian ulang.
