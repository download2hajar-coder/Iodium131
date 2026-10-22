# I-131 Nextion HMI — Dokumentasi Firmware

Dokumentasi lengkap untuk sketch `I_131_Nextion_HMI.ino`. Sketch ini merupakan
pengembangan dari `I_131_SingleFillSingleDispense_v6_working.ino` dengan
penambahan HMI Nextion, kalkulasi volume ke step di Arduino, dan antarmuka
perintah baru.

> **Peringatan keselamatan:** dokumen ini menjelaskan implementasi perangkat
> lunak, bukan validasi dosis, proses farmasi, atau kelayakan klinis. Firmware
> menjalankan gerakan secara blocking, belum memiliki `STOP` yang aktif, dan
> belum memiliki emergency stop perangkat keras. Validasi mekanik, limit switch,
> emergency stop, batas volume, dan prosedur operasi harus dilakukan oleh
> personel berwenang sebelum penggunaan nyata.

Referensi seperti `:38` merujuk ke nomor baris di `I_131_Nextion_HMI.ino`.

---

## 1. Ringkasan

Firmware ini mengendalikan empat motor stepper untuk:

- homing empat motor;
- persiapan iodium (`prepareIodium()`);
- pengisian kapsul otomatis (`runAutoDispense()`);
- pemindahan kapsul dan penutupan;
- komunikasi dengan HMI Nextion;
- komunikasi serial USB untuk perintah manual dan debugging.

Perubahan utama dibanding firmware `v6_working`:

| Perubahan | Keterangan |
|---|---|
| HMI Nextion | Ditambahkan `SoftwareSerial` pada A0/A1, 9600 baud. |
| Kalkulasi di Arduino | Volume, faktor kalibrasi, dan jumlah kapsul dikirim ke Arduino; Arduino menghitung step. |
| Protokol baru | `CFG:volume;factor;caps` dan `START` untuk Nextion. |
| Protokol USB baru | `<INIT:volume;factor;caps>`. |
| Kompatibilitas lama | `<INIT:steps;caps>` masih diterima sebagai jalur legacy. |
| Status HMI | `tStatus`, `jProg`, `tStep`, dan `tLog` diperbarui oleh firmware. |
| Perlindungan timing | `SoftwareSerial` dinonaktifkan sementara selama gerakan motor. |

Status implementasi:

- Kalkulasi dan validasi volume sudah berada di Arduino.
- HMI Nextion belum menyertakan file proyek UI; objek Nextion harus dibuat
  terpisah di Nextion Editor.
- `STOP` belum menghentikan motor; perintah hanya dicatat.
- Siklus otomatis masih blocking dan belum dapat dibatalkan dari HMI.

---

## 2. Perangkat Keras dan Wiring

### 2.1 Pin Arduino

| Fungsi | Pin | Keterangan |
|---|---:|---|
| STEP bersama | 7 | Pulsa step untuk driver yang sedang aktif. |
| DIR bersama | 6 | Arah putaran untuk driver yang sedang aktif. |
| ENABLE M1 | 8 | Aktif-LOW; LOW mengaktifkan driver M1. |
| ENABLE M2 | 9 | Aktif-LOW; M2 adalah syringe pump. |
| ENABLE M3 | 10 | Aktif-LOW. |
| ENABLE M4 | 11 | Aktif-LOW. |
| Limit M1 | 3 | `INPUT_PULLUP`; LOW berarti limit aktif. |
| Limit M2 | 4 | `INPUT_PULLUP`; LOW berarti limit aktif. |
| Limit M3 | 5 | `INPUT_PULLUP`; LOW berarti limit aktif. |
| Limit M4 | 2 | `INPUT_PULLUP`; LOW berarti limit aktif. |
| Nextion TX -> Arduino A0 | A0 | RX SoftwareSerial; menerima data dari Nextion. |
| Arduino A1 -> Nextion RX | A1 | TX SoftwareSerial; mengirim perintah ke Nextion. |
| USB Serial | D0/D1 | Serial monitor, upload, dan perintah USB 9600 baud. |

Referensi kode: pin `:10-14`, Nextion `:35-44`.

### 2.2 Wiring Nextion

- Nextion TX -> Arduino A0.
- Nextion RX -> Arduino A1.
- GND Nextion dan GND Arduino harus tersambung.
- Gunakan catu daya Nextion terpisah **5 V / 2 A** yang sesuai spesifikasi
  display. Jangan memberi daya Nextion dari pin 5 V Nano.
- Periksa level logika. TX Nextion umumnya 3,3 V; TX Arduino 5 V dapat
  memerlukan pembagi tegangan atau level shifter sebelum masuk RX Nextion.
  Jangan menganggap semua pin Nextion 5 V tolerant.
- Baud kedua sisi harus sama: **9600**.

### 2.3 Asumsi perangkat

- Board target saat ini: Arduino Nano, FQBN
  `arduino:avr:nano:cpu=atmega328old`.
- Driver stepper aktif-LOW.
- Limit switch aktif-LOW dengan pull-up internal.
- Nextion menggunakan protokol serial TTL standar dan dapat menerima perintah
  teks yang diakhiri `FF FF FF`.
- Model Nextion tidak di-hardcode; firmware hanya membutuhkan link serial TTL
  9600 dan objek dengan nama yang sesuai.
- Nextion belum tentu memiliki RTC, SD card, atau kemampuan logging runtime.
- Jika board diganti ke Mega 2560, sketch ini tetap memakai SoftwareSerial di
  A0/A1. Untuk memakai `Serial1`, kode link Nextion perlu diubah.

---

## 3. Build, Upload, dan Memori

Perintah kompilasi yang digunakan:

```powershell
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old `
  --build-path "C:\Users\User\AppData\Local\Temp\opencode\nextion_build" `
  "I_131_Nextion_HMI"
```

Hasil build saat dokumentasi ini dibuat:

| Metrik | Nilai |
|---|---:|
| Flash | 16.930 bytes (55% dari 30.720 bytes) |
| RAM global | 1.141 bytes (55% dari 2.048 bytes) |
| Sisa RAM untuk stack/local | 907 bytes |

Catatan:

- Sketch menggunakan `String` untuk parser USB dan buffer Nextion berukuran
  tetap 48 byte. Sisa RAM 907 byte masih dapat digunakan, tetapi tidak besar.
- Library yang dipakai hanya `SoftwareSerial` dari Arduino core dan
  `stdlib.h`.
- Warning build yang tersisa adalah variabel `POS_BOTOL` yang tidak dipakai di
  `runAutoDispense()`; ini berasal dari alur lama.
- Jangan mengunggah sketch ini ke board lain tanpa memeriksa pin, level logika,
  dan kapasitas RAM.

---

## 4. Konfigurasi Utama

### 4.1 Nextion

| Konstanta | Nilai | Lokasi | Keterangan |
|---|---:|---|---|
| `NEXTION_RX_PIN` | `A0` | `:38` | RX Arduino dari TX Nextion. |
| `NEXTION_TX_PIN` | `A1` | `:39` | TX Arduino ke RX Nextion. |
| `NEXTION_BAUD` | `9600` | `:40` | Baud link Nextion. |
| `NEXTION_LINE_MAX` | `48` | `:41` | Buffer baris; maksimum 47 karakter + null. |
| `NEXTION_SUSPEND_DURING_MOVE` | `1` | `:42` | Nonaktifkan SoftwareSerial saat motor bergerak. |

### 4.2 Batas kalkulasi

| Konstanta | Nilai | Lokasi | Keterangan |
|---|---:|---|---|
| `MAX_VOLUME_UL` | `10.0` | `:52` | Batas atas volume per kapsul. |
| `MAX_STEP_PER_CAPSULE` | `2000000` | `:53` | Penjaga overflow, bukan batas mekanis. |
| `DEFAULT_CALIBRATION_FACTOR` | `2033.0` | `:54` | Nilai awal faktor, bukan pengganti kalibrasi. |

### 4.3 Timing motor

| Motor | Delay minimum | Delay awal/akhir | Lokasi |
|---|---:|---:|---|
| M1 | 80 µs | 800 µs | `:24-25` |
| M2 | 300 µs | 800 µs | `:24-25` |
| M3 | 80 µs | 800 µs | `:24-25` |
| M4 | 80 µs | 800 µs | `:24-25` |
| Homing | 80 µs | tanpa ramp | `:26` |

Nilai tersebut adalah setelan awal sketch, bukan jaminan batas aman mekanis.

### 4.4 Data mekanik yang belum dipakai

`leadMotor1_mmPerRev`, `leadMotor2_mmPerRev`, `leadMotor3_mmPerRev`,
`leadMotor4_mmPerRev`, dan `stepsPerRev` dideklarasikan di `:28-33`, tetapi
tidak digunakan untuk perhitungan gerak saat ini. Perhitungan volume memakai
faktor kalibrasi step/µL dari HMI.

---

## 5. Arsitektur Firmware

### 5.1 Alur data

```text
Nextion
  PING / CFG / START / STOP / CMD
        |
        v
  SoftwareSerial A0/A1
        |
        v
  nextionPoll() -> nextionHandleLine()
        |
        +--> calculateConfiguration()
        +--> startDispenseCycle()
        +--> executeCommand()

USB Serial 9600
  <INIT:...>, <START>, <M...>, <HOME:...>, <READPOS>
        |
        v
  serialEvent() -> processQueue() -> executeCommand()
        |
        v
  kalkulasi / homing / prepareIodium() / runAutoDispense()
        |
        v
  stepPulse() -> driver motor
```

### 5.2 Komponen utama

| Komponen | Lokasi | Tanggung jawab |
|---|---|---|
| `setup()` | `:130` | Serial, pin, homing awal, status READY. |
| `loop()` | `:162` | Poll Nextion dan proses perintah USB. |
| `serialEvent()` | `:172` | Mengumpulkan perintah USB sampai newline atau `>`. |
| `processQueue()` | `:684` | Menjalankan setiap perintah di dalam `<...>`. |
| `executeCommand()` | `:702` | Parser perintah USB dan dispatcher. |
| `calculateConfiguration()` | `:283` | Validasi dan kalkulasi volume -> step. |
| `startDispenseCycle()` | `:329` | Menjalankan prepare dan auto dispense. |
| `prepareIodium()` | `:579` | Posisi awal sebelum pengisian. |
| `runAutoDispense()` | `:603` | Pipeline pengisian kapsul 1–5. |
| `homingMotor()` | `:490` | Homing satu motor. |
| `moveMotorAccel()` | `:535` | Gerak motor dengan accel/cruise/decel. |

### 5.3 State machine Nextion

| State | Arti | Kapan terjadi |
|---|---|---|
| `NEX_BOOT` | Firmware baru mulai. | `nextionBegin()` (`:186`). |
| `NEX_HOMING` | Homing awal berjalan. | Sebelum homing di `setup()` (`:143`). |
| `NEX_READY` | Siap menerima konfigurasi/START. | Setelah homing, CFG valid, atau siklus selesai. |
| `NEX_RUNNING` | Siklus dispense berjalan. | Awal `startDispenseCycle()` (`:349`). |
| `NEX_ERROR` | Konfigurasi/perintah tidak valid. | `nextionConfigError()` (`:276`). |

`tStatus` pada Nextion berisi salah satu dari `BOOT`, `HOMING`, `READY`,
`RUNNING`, atau `ERROR`.

---

## 6. Protokol Nextion

### 6.1 Framing

- Nextion mengirim **teks ASCII** ke Arduino.
- Setiap perintah harus diakhiri newline `\n` (0x0A).
- Karakter `\r` diabaikan.
- Baris tanpa newline tidak diproses sampai newline diterima.
- Panjang maksimum baris 47 karakter; lebih dari itu menghasilkan pesan
  `ERR: baris terlalu panjang`.
- Nama perintah bersifat case-sensitive.
- Nextion **tidak** mengirim pembungkus `<...>`; `<...>` hanya untuk USB.
- Arduino mengirim perintah ke Nextion dengan terminator `FF FF FF`.

 Contoh pengiriman dari Nextion Editor:

```text
prints "PING",0
printh 0A
```

atau untuk memulai:

```text
prints "START",0
printh 0A
```

Sesuaikan sintaks `prints` dengan versi Nextion Editor yang digunakan; yang
penting adalah string dikirim tanpa newline tambahan dan diakhiri satu `0A`.

### 6.2 Perintah Nextion yang didukung

| Perintah | Format | Efek |
|---|---|---|
| `PING` | `PING\n` | Kirim ulang `tStatus` dan `jProg`. Tidak menggerakkan motor. |
| `CFG` | `CFG:volume;factor;caps\n` | Validasi, hitung step, simpan konfigurasi, set READY. Tidak menggerakkan motor. |
| `START` | `START\n` | Hitung ulang konfigurasi lalu jalankan siklus. |
| `STOP` | `STOP\n` | Hanya menulis log; motor belum dihentikan. |
| `CMD` | `CMD:<perintah>\n` | Meneruskan perintah ke parser USB. |

Contoh:

```text
CFG:5.0;2033;4
START
```

Artinya volume 5,0 µL, faktor 2033 step/µL, 4 kapsul. Hasil kalkulasi:
`10165` step per kapsul.

`CMD:` meneruskan isi setelah `CMD:` ke `executeCommand()`. Perintah
`CMD:INIT:...` sengaja diblokir dengan pesan `ERR: pakai CFG lalu START`.
Perintah manual seperti `CMD:M2;1600;0`, `CMD:HOME:ALL`, `CMD:READPOS`, dan
`CMD:START` dapat digunakan, tetapi berisiko dan hanya untuk pengujian.

### 6.3 Objek Nextion yang diperbarui

| Objek | Tipe yang diharapkan | Format yang dikirim firmware | Keterangan |
|---|---|---|---|
| `tStatus` | Text | `tStatus.txt="READY"` | Status state machine. |
| `jProg` | Number/Progress Bar | `jProg.val=0` | 0 saat mulai, 100 setelah selesai; tidak ada progres antara. |
| `tStep` | Text | `tStep.txt="10165"` | Hasil kalkulasi step per kapsul. Awalnya `-`. |
| `tLog` | Text | `tLog.txt+="pesan\r"` | Append log. `\r` diubah Nextion menjadi CRLF. |

Objek harus dibuat dengan nama yang sama di Nextion Editor. Jika objek tidak
ada, Nextion dapat mengembalikan kode error. Disarankan mengatur `bkcmd=0`
pada `Program.s` Nextion agar kode balasan tidak masuk ke parser Arduino.

### 6.4 PING dan watchdog

Firmware tidak memiliki watchdog Nextion. `PING` hanya mengirim status terbaru.
Disarankan UI mengirim `PING` setiap 1–2 detik saat idle. PING yang dikirim
saat motor bergerak dapat hilang karena link Nextion dinonaktifkan sementara.

Jika Nextion baru menyala setelah Arduino, status awal dan log `Arduino READY`
dapat hilang. Kirim `PING` setelah Nextion siap untuk meminta status terbaru.

---

## 7. Protokol USB Serial

### 7.1 Framing

- USB Serial berjalan pada 9600 baud.
- `serialEvent()` mengumpulkan karakter sampai newline atau `>`.
- `processQueue()` hanya mengeksekusi perintah yang berada di dalam `<...>`.
- Beberapa perintah dalam satu baris dapat diproses, misalnya
  `<HOME:ALL><INIT:5;2033;2>`.
- Perintah tanpa `<...>` diabaikan oleh parser USB. Ini berbeda dari Nextion
  yang mengirim baris langsung.

### 7.2 Perintah USB

| Perintah | Format | Efek |
|---|---|---|
| `INIT` baru | `<INIT:volume;factor;caps>` | Hitung step di Arduino lalu jalankan siklus. |
| `INIT` legacy | `<INIT:steps;caps>` | Gunakan step yang sudah dihitung HMI lalu jalankan siklus. |
| `START` | `<START>` | Jalankan konfigurasi CFG yang tersimpan. |
| Motor | `<M2;1600;0>` | Gerakkan motor 2 sebanyak 1600 step arah 0. |
| Homing semua | `<HOME:ALL>` | Homing M4 -> M3 -> M2 -> M1. |
| Homing satu | `<HOME:3>` | Homing motor 3. |
| Posisi | `<READPOS>` | Cetak estimasi `posAbs` M1–M4. |

Contoh:

```text
<INIT:5.0;2033;4>
```

menghitung `10165` step/kapsul lalu menjalankan siklus. Format lama:

```text
<INIT:20330;4>
```

tetap diterima, tetapi Arduino mencetak `WARN: LEGACY INIT`, mengisi volume dan
faktor dengan 0, dan memakai 20330 step langsung tanpa validasi volume.

### 7.3 Perbedaan jalur USB dan Nextion

| Aspek | Nextion | USB |
|---|---|---|
| Pembungkus | Tanpa `<...>` | Wajib `<...>` |
| Akhir baris | `\n` | `\n` atau `>` |
| Kalkulasi | `CFG` + `START` | `INIT` 3-field |
| Status | `tStatus`, `jProg`, `tLog` | Teks Serial Monitor |
| Manual | `CMD:<...>` | `<...>` langsung |

---

## 8. Logika Kalkulasi

### 8.1 Fungsi

Kalkulasi utama berada di `calculateConfiguration()` (`:283`).

Rumus:

```text
step_per_capsule = round(volume_uL × factor_step_per_uL)
```

Pembulatan mengikuti perilaku `round()` Python: jika pecahan tepat 0,5,
hasil dibulatkan ke bilangan genap terdekat.

### 8.2 Validasi

| Parameter | Aturan | Status error |
|---|---|---|
| Volume | `> 0` dan `<= 10.0 µL` | `CALC_VOLUME_RANGE` |
| Faktor | `> 0` | `CALC_FACTOR_RANGE` |
| Jumlah kapsul | `1–5` | `CALC_CAPSULE_RANGE` |
| Hasil step | `>= 1` dan `<= 2.000.000` | `CALC_STEP_RANGE` |

Jika validasi gagal:

- `configurationReady` dan `configurationHasVolume` direset;
- state Nextion menjadi `ERROR`;
- pesan error dikirim ke `tLog`;
- siklus tidak dijalankan.

### 8.3 Contoh

| Volume (µL) | Faktor | Volume × faktor | Step hasil |
|---:|---:|---:|---:|
| 0,5 | 2033 | 1016,5 | 1016 |
| 1,5 | 2033 | 3049,5 | 3050 |
| 2,5 | 2033 | 5082,5 | 5082 |
| 5,0 | 2033 | 10165,0 | 10165 |
| 10,0 | 2033 | 20330,0 | 20330 |

### 8.4 Catatan numerik

- Pada AVR, `double` umumnya sama dengan `float` 32-bit. Presisi cukup untuk
  rentang volume/faktor yang divalidasi, tetapi bukan presisi 64-bit.
- `MAX_STEP_PER_CAPSULE` adalah penjaga overflow, bukan bukti bahwa 2.000.000
  step aman secara mekanis.
- `totalVolumeSteps = stepsPerCapsule × jumlahCapsule` masih berada dalam
  `long` 32-bit untuk batas yang divalidasi (maksimum 10.000.000).
- Jalur legacy `<INIT:steps;caps>` **tidak** memakai kalkulasi ini dan tidak
  memvalidasi volume/faktor.

### 8.5 Penggunaan hasil

`calculateConfiguration()` dipanggil oleh:

- `nextionHandleConfig()` untuk `CFG`;
- `startDispenseCycle()` untuk menghitung ulang sebelum motor bergerak;
- `executeCommand()` untuk `<INIT:volume;factor;caps>`.

Dengan demikian, nilai yang menggerakkan syringe selalu dihitung ulang oleh
Arduino sebelum siklus dimulai.

---

## 9. Alur Siklus

### 9.1 Startup dan homing awal

Urutan `setup()` (`:130-157`):

1. `Serial.begin(9600)`.
2. `nextionBegin()`: SoftwareSerial A0/A1, 9600, reset state.
3. Pin ENABLE diset OUTPUT dan HIGH (driver nonaktif).
4. Pin limit diset `INPUT_PULLUP`.
5. STEP dan DIR diset OUTPUT.
6. State Nextion menjadi `HOMING`.
7. Homing berurutan M4 -> M3 -> M2 -> M1.
8. Input Nextion dibersihkan.
9. State menjadi `READY`, `tStep` diisi `-`, log `Arduino READY`.
10. USB mencetak `Sistem siap. Masukkan perintah...`.

Homing satu motor (`homingMotor()`, `:490-520`):

1. Aktifkan driver.
2. Set arah homing.
3. Cari limit sampai LOW.
4. Mundur 50 step.
5. Cari limit kembali sampai LOW.
6. Nonaktifkan driver.
7. Aktifkan kembali SoftwareSerial dan flush input.

Tidak ada timeout homing. Jika limit tidak aktif, loop dapat berjalan terus.

### 9.2 Alur CFG dan START

1. Nextion mengirim `CFG:volume;factor;caps`.
2. `nextionHandleConfig()` mem-parsing tiga nilai dengan `strtod`/`strtol`.
3. `calculateConfiguration()` memvalidasi dan menghitung step.
4. Nilai disimpan:
   - `volumePerCapsule_uL`
   - `calibrationFactor`
   - `stepsPerCapsule`
   - `jumlahCapsule`
   - `totalVolumeSteps`
   - `configurationReady = true`
   - `configurationHasVolume = true`
5. Firmware mengirim `tStep`, state `READY`, dan log
   `CFG OK; motor belum jalan`.
6. Saat `START`, `startDispenseCycle()` menghitung ulang, state menjadi
   `RUNNING`, lalu menjalankan `prepareIodium()` dan `runAutoDispense()`.
7. Setelah selesai, `jProg=100`, state `READY`, log `Siklus selesai`.

### 9.3 `prepareIodium()`

Lokasi: `:579-596`.

| Urutan | Motor | Langkah | Arah | Keterangan |
|---:|---:|---:|---:|---|
| 1 | M2 | 113000 | 0 | Plunger turun/posisi kosong. |
| 2 | M3 | 135000 | 1 | Masuk ke botol iodium. |
| 3 | M2 | `stepsPerCapsule` | 1 | Priming/ambil cairan. |
| 4 | M3 | 110000 | 0 | Keluar botol dan standby. |

### 9.4 `runAutoDispense()`

Lokasi: `:603-679`.

Konstanta posisi:

| Konstanta | Nilai | Keterangan |
|---|---:|---|
| `POS_BOTOL` | 0 | Dideklarasikan tetapi tidak dipakai. |
| `POS_KAPSUL1` | 31500 | Posisi kapsul pertama. |
| `JARAK_KAPSUL` | 16000 | Jarak antar-kapsul. |
| `TOTAL_SLOT` | 5 | Total slot kapsul. |
| `POS_KAPSUL5` | 95500 | Kapsul kelima. |
| `POS_PENUTUP` | 227500 | Posisi penutup. |

Urutan:

1. **Kapsul 1 dari hasil prepare**
   - M1 ke posisi 31500.
   - M3 55000 arah 1.
   - M2 `stepsPerCapsule` arah 0 (dispense).
   - M3 55000 arah 0.
2. **Kapsul 2 sampai `jumlahCapsule`**
   - M1 kembali ke botol (`posAbs[1]` arah 1).
   - M3 110000 arah 1.
   - M2 `stepsPerCapsule` arah 1 (sedot).
   - M3 110000 arah 0.
   - M1 ke posisi kapsul ke-i.
   - M3 55000 arah 1.
   - M2 `stepsPerCapsule` arah 0 (dispense).
   - M3 55000 arah 0.
3. **Bypass slot sisa**
   - M1 bergerak ke posisi setiap slot yang tidak dipakai sampai slot 5.
4. **Penutupan**
   - M1 ke 227500.
   - M4 16000 arah 1.
5. **Homing akhir**
   - M4, M3, dan M2 dihoming.
   - Homing M1 **dinonaktifkan** (baris `:676` dikomentari).

### 9.5 Catatan penting siklus berulang

Karena M1 tidak dihoming pada akhir siklus, `posAbs[1]` tetap berada di posisi
penutup setelah siklus pertama. Siklus berikutnya dapat menghitung jarak M1
yang negatif dan tidak kembali ke posisi awal yang benar. Untuk penggunaan
berulang, homing M1 di akhir siklus atau strategi posisi absolut yang konsisten
harus ditambahkan dan diuji.

---

## 10. Kontrol Motor

### 10.1 `stepPulse()`

Lokasi: `:525-530`.

```text
STEP HIGH
delayMicroseconds(delayUs)
STEP LOW
delayMicroseconds(delayUs)
```

Satu panggilan menghasilkan satu pulsa step.

### 10.2 `moveMotorAccel()`

Lokasi: `:535-572`.

- Validasi hanya `id` 1–4.
- `arah` 0 menghasilkan DIR LOW dan `posAbs += langkah`.
- `arah` selain 0 menghasilkan DIR HIGH dan `posAbs -= langkah`.
- Tidak ada validasi `langkah` negatif atau `arah` selain 0/1.
- Fase:
  - accel: `langkah / 8` pulsa, delay dari `maxDelay` ke `minDelay`;
  - cruise: `langkah / 2` pulsa pada `minDelay`;
  - decel: `langkah / 8` pulsa, delay dari `minDelay` ke `maxDelay`.
- Total pulsa aktual sekitar 3/4 dari `langkah` (tergantung pembulatan),
  tetapi `posAbs` diperbarui sebesar seluruh `langkah`. Ini adalah
  ketidaksesuaian penting untuk posisi open-loop.
- `SoftwareSerial` dinonaktifkan sebelum gerakan dan diaktifkan kembali
  setelah gerakan.

### 10.3 Homing

- Arah homing: `{1, 1, 0, 0}` untuk M1–M4 (`:17`).
- Homing memakai delay 80 µs tanpa ramp.
- Limit harus LOW saat aktif.
- Tidak ada timeout.
- Homing M1 hanya dilakukan saat startup, bukan di akhir siklus.

### 10.4 SoftwareSerial dan timing

- `nextionSuspend()` memanggil `nextionSerial.end()` (`:205-209`).
- `nextionResume()` memanggil `begin()`, `listen()`, dan flush (`:211-217`).
- Tujuannya mencegah interrupt SoftwareSerial mengganggu pulsa step.
- Konsekuensi: perintah Nextion, termasuk `PING` dan `STOP`, yang dikirim
  selama motor bergerak akan hilang.
- Setelah setiap gerakan, input Nextion dibersihkan sehingga data parsial
  tidak diproses.

---

## 11. Kontrak UI Nextion

Firmware tidak menyertakan file `.HMI` atau desain halaman. UI harus dibuat
terpisah di Nextion Editor.

### 11.1 Objek minimum

| Objek | Fungsi |
|---|---|
| `tStatus` | Menampilkan `BOOT/HOMING/READY/RUNNING/ERROR`. |
| `jProg` | Progress bar atau number 0–100. |
| `tStep` | Menampilkan step/kapsul hasil kalkulasi. |
| `tLog` | Menampilkan log teks dari firmware. |

### 11.2 Tombol yang disarankan

| Tombol | Kirim ke Arduino |
|---|---|
| PING | `PING` + newline |
| Hitung/Set | `CFG:<volume>;<factor>;<caps>` + newline |
| Mulai | `START` + newline |
| Stop | `STOP` + newline (belum berfungsi) |
| Manual | `CMD:<perintah>` + newline |

Contoh event Nextion:

```text
// Tombol Hitung
prints "CFG:5.0;2033;4",0
printh 0A

// Tombol Mulai
prints "START",0
printh 0A

// Tombol PING
prints "PING",0
printh 0A
```

### 11.3 Pengaturan Nextion yang disarankan

- Baud `9600`.
- `bkcmd=0` di `Program.s` untuk mengurangi kode balasan yang tidak diparse.
- Pastikan `tLog` memiliki panjang teks yang cukup; append yang melebihi batas
  dapat terpotong.
- Jangan mengirim perintah saat status `RUNNING`; firmware tidak memproses
  input selama gerakan blocking.
- Firmware tidak mengirim perintah `page`; navigasi halaman sepenuhnya diatur
  oleh event Nextion.

---

## 12. Pengujian

Lakukan pengujian dengan mekanik aman, driver mendapat daya yang benar, dan
area gerak bebas.

### 12.1 Checklist dasar

1. Kompilasi berhasil dan upload ke Nano.
2. Buka Serial Monitor 9600; amati homing M4 -> M3 -> M2 -> M1.
3. Pastikan muncul `Sistem siap. Masukkan perintah...`.
4. Hubungkan Nextion pada A0/A1 dan pastikan baud 9600.
5. Kirim `PING`; `tStatus` harus berubah dan `jProg` terkirim.
6. Kirim `CFG:5.0;2033;4`; `tStep` harus `10165`, status `READY`, motor
   **tidak** bergerak.
7. Kirim `START`; status menjadi `RUNNING`, siklus berjalan, lalu `READY`.
8. Kirim `CFG:11;2033;4`; status harus `ERROR`, tidak ada gerakan.
9. Kirim `START` tanpa CFG valid; harus ditolak.
10. Uji USB `<INIT:5.0;2033;4>` dan legacy `<INIT:20330;4>`.
11. Uji `<HOME:ALL>` dan `<READPOS>`.
12. Uji perintah yang dikirim saat motor bergerak; perintah tersebut dapat
    hilang dan tidak boleh diandalkan.

### 12.2 Hasil yang diharapkan

| Aksi | Hasil |
|---|---|
| `PING` | `tStatus` dan `jProg` dikirim ulang. |
| `CFG` valid | `tStep` terisi, `READY`, tidak ada gerakan. |
| `CFG` invalid | `ERROR`, konfigurasi dibatalkan. |
| `START` valid | `RUNNING` -> gerakan -> `READY`, `jProg=100`. |
| `STOP` | Hanya log; motor tetap berjalan. |
| `<INIT:...>` baru | Kalkulasi Arduino lalu siklus. |
| `<INIT:steps;caps>` lama | Step dipakai langsung, warning legacy. |

---

## 13. Batasan dan Known Issues

1. **`STOP` belum aktif.** Firmware blocking dan tidak memeriksa perintah
   `STOP` di tengah gerakan.
2. **Tidak ada emergency stop perangkat keras** dalam firmware ini.
3. **Homing tanpa timeout.** Limit yang tidak aktif menyebabkan loop tak
   berujung.
4. **M1 tidak dihoming di akhir siklus.** Hal ini dapat membuat siklus
   berikutnya salah posisi; lihat bagian 9.5.
5. **Jumlah pulsa `moveMotorAccel()` tidak sama dengan `posAbs`.** Posisi
   open-loop dapat melenceng.
6. **`POS_BOTOL` tidak dipakai.**
7. **Data lead screw dan `stepsPerRev` tidak dipakai.**
8. **Tidak ada validasi manual `M`.** Langkah negatif dan arah selain 0/1
   tidak ditolak.
9. **Jalur legacy `<INIT:steps;caps>` melewati kalkulasi dan validasi volume.**
10. **`CMD:INIT` diblokir**; gunakan `CFG` lalu `START`.
11. **Perintah Nextion hilang saat motor bergerak** karena SoftwareSerial
    dinonaktifkan.
12. **`jProg` tidak memiliki progres antara.** Nilai 0 saat mulai dan 100
    setelah selesai.
13. **`jProg` tidak direset oleh CFG.** Setelah siklus selesai, nilai 100 dapat
    tetap tampil sampai `START` berikutnya mengeset 0.
14. **Tidak ada logging CSV/RTC/SD.** Hanya `tLog` di Nextion dan Serial USB.
15. **Tidak ada autentikasi/admin.** Siapa pun dengan akses serial dapat
    menjalankan perintah.
16. **Tidak ada validasi mekanis** untuk batas step, arus, torsi, atau
    kecepatan.
17. **String dan RAM terbatas.** Penggunaan `String` dapat memicu fragmentasi
    heap pada siklus panjang.
18. **Nextion UI tidak disertakan.** Objek dan halaman harus dibuat terpisah.
19. **Tidak ada watchdog** untuk Nextion, limit, atau komunikasi.
20. **Level logika Nextion perlu diperiksa.** TX 5 V Arduino dapat merusak RX
    Nextion yang hanya 3,3 V.
21. **`START` langsung menjalankan siklus** tanpa dialog konfirmasi tambahan.
    Pastikan area aman dan konfigurasi sudah diperiksa sebelum menekan START.

---

## 14. Perbedaan dengan `v6_working`

| Aspek | `v6_working` | `I_131_Nextion_HMI` |
|---|---|---|
| HMI | USB/PC | Nextion + USB |
| Kalkulasi volume | Di HMI/Python | Di Arduino |
| Perintah utama | `<INIT:steps;caps>` | `CFG` + `START`, atau `<INIT:volume;factor;caps>` |
| Legacy INIT | Ya | Masih diterima |
| Status HMI | Tidak ada | `tStatus`, `jProg`, `tStep`, `tLog` |
| Serial kedua | Tidak ada | SoftwareSerial A0/A1 |
| Suspend serial saat gerak | Tidak ada | Ada |
| State machine | Tidak ada | BOOT/HOMING/READY/RUNNING/ERROR |
| Memori | Lebih kecil | Lebih besar karena fitur HMI |

File asli `I_131_SingleFillSingleDispense_v6_working.ino` tidak diubah.

---

## 15. Peta File dan Referensi Kode

| Bagian | Lokasi |
|---|---|
| Header dan include | `:1-8` |
| Pin dan timing | `:10-26` |
| Data mekanik | `:28-33` |
| Konfigurasi Nextion | `:35-54` |
| Enum state dan kalkulasi | `:56-73` |
| Variabel operasional | `:75-92` |
| Prototipe | `:94-125` |
| `setup()` | `:130-157` |
| `loop()` | `:162-170` |
| `serialEvent()` | `:172-181` |
| `nextionBegin()` | `:186-195` |
| `nextionFlushInput()` | `:197-203` |
| `nextionSuspend()` / `nextionResume()` | `:205-217` |
| `nextionEndCommand()` | `:219-223` |
| `nextionSetText()` | `:225-239` |
| `nextionSetNumber()` | `:241-246` |
| `nextionLog()` | `:248-254` |
| `nextionSendCurrentStatus()` | `:256-269` |
| `nextionSetState()` | `:271-274` |
| `nextionConfigError()` | `:276-281` |
| `calculateConfiguration()` | `:283-317` |
| `calcErrorMessage()` | `:319-327` |
| `startDispenseCycle()` | `:329-371` |
| `nextionHandleConfig()` | `:373-417` |
| `nextionHandleLine()` | `:419-455` |
| `nextionPoll()` | `:457-485` |
| `homingMotor()` | `:490-520` |
| `stepPulse()` | `:525-530` |
| `moveMotorAccel()` | `:535-572` |
| `prepareIodium()` | `:579-596` |
| `runAutoDispense()` | `:603-679` |
| `processQueue()` | `:684-693` |
| `executeCommand()` | `:702-837` |
| `getValue()` / `getValueLong()` | `:842-858` |
| Blok komentar perintah manual | `:862-938` |

Referensi baris mengikuti versi file saat dokumentasi ini dibuat.

---

## 16. Glosarium

| Istilah | Arti |
|---|---|
| µL | Mikroliter, satuan volume cairan. |
| Step | Satu pulsa pergerakan motor stepper. |
| Faktor kalibrasi | Jumlah step per mikroliter (step/µL). |
| Homing | Proses mencari posisi referensi menggunakan limit switch. |
| `posAbs` | Estimasi posisi motor dalam step dari home; bukan encoder. |
| Open-loop | Tidak ada umpan balik posisi fisik. |
| Blocking | Fungsi berjalan sampai selesai sebelum loop dapat memproses perintah lain. |
| Legacy INIT | Format lama `<INIT:steps;caps>` yang mengirim step langsung. |

---

## 17. Ringkasan Cepat untuk Operator

1. Pastikan area aman dan daya driver benar.
2. Nyalakan Arduino dan Nextion; tunggu status `READY`.
3. Isi volume (maksimum 10 µL), faktor kalibrasi, dan jumlah kapsul 1–5.
4. Kirim `CFG`; periksa `tStep` dan pastikan status `READY`.
5. Tekan `START` hanya setelah konfigurasi benar.
6. Jangan mengandalkan `STOP`; hentikan melalui emergency stop perangkat
   keras bila diperlukan.
7. Setelah selesai, periksa posisi mekanik sebelum memulai siklus berikutnya,
   terutama karena M1 tidak dihoming di akhir siklus.
