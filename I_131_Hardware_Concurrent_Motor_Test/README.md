# Dokumentasi Tes Empat Motor Stepper Serentak

Dokumen ini menjelaskan sketch `I_131_Hardware_Concurrent_Motor_Test.ino`, meliputi fungsi program, konfigurasi Arduino, pemetaan pin, parameter timing, alur kerja, kontrol serial, dan contoh simulasi saat perangkat dinyalakan. Sketch ini adalah uji bangku (bench test) untuk menggerakkan empat motor stepper secara serentak melalui sinyal STEP/DIR yang dipakai bersama.

> **Peringatan:** ini adalah sketch uji bangku. Empat motor bergerak terus menerus tanpa homing atau batas jarak; limit switch hanya membalik arah dan bukan penghentian darurat. Siapkan mekanik agar gerak dua arah aman, awasi pengujian, dan sediakan pemutus daya darurat. Setelan 80 us bukan jaminan kecepatan maksimum yang aman untuk motor, driver, atau beban.

## Tujuan dan perilaku

Sketch ini mengaktifkan empat driver stepper sekaligus. Semua driver berbagi sinyal STEP (pin 7) dan DIR (pin 6), sehingga semua motor menerima pulsa step dan arah yang sama. Saat startup, motor langsung berjalan dengan DIR awal LOW.

Jika salah satu limit switch ditekan dan stabil setidaknya 25 ms, sketch membalik DIR bersama untuk keempat motor satu kali. Switch harus dilepas dan ditekan kembali untuk memicu pembalikan berikutnya. Sketch tidak menghentikan motor saat switch ditekan.

Sketch menyediakan kontrol Serial Monitor:

- `S` atau `s`: hentikan pulsa, set STEP LOW, dan nonaktifkan semua driver.
- `R` atau `r`: aktifkan kembali semua driver dan lanjutkan gerak dengan arah terakhir.

Port serial menggunakan **9600 baud**. Karakter lain diabaikan. Tidak ada kebutuhan mengirim newline.

## Konfigurasi Arduino

Konfigurasi diambil dari `.vscode/arduino.yaml` dan `.vscode/c_cpp_properties.json`. Sketch di-upload dari VSCode memakai ekstensi Arduino.

| Item | Nilai | Sumber |
|---|---|---|
| Board | Arduino Nano (`arduino:avr:nano`) | `arduino.yaml` |
| Prosesor / bootloader | ATmega328P (Old Bootloader, `cpu=atmega328old`) | `arduino.yaml` |
| Chip target | `atmega328p` | `c_cpp_properties.json` |
| Clock CPU | `F_CPU = 16000000L` (16 MHz) | `c_cpp_properties.json` |
| Port upload | `COM7` | `arduino.yaml` |
| Output build | folder `.build/` | `arduino.yaml` |
| Sketch | `I_131_Hardware_Concurrent_Motor_Test.ino` | `arduino.yaml` |
| Toolchain | `avr-gcc 7.3.0-atmel3.6.1-arduino7` (core AVR 1.8.6) | `c_cpp_properties.json` |
| Serial Monitor | 9600 baud | `.ino` |

Catatan:

- Board Nano dengan chip lama sering memerlukan pilihan **Old Bootloader**. Bila upload gagal pada `COM7`, periksa nomor port dan pilihan prosesor.
- Port `COM7` bersifat spesifik mesin; sesuaikan bila perangkat dihubungkan ke port lain.
- Folder `.build/` berisi artefak hasil kompilasi (`.elf`, `.hex`, objek), bukan sumber; boleh dibersihkan dan dibangun ulang.

## Pemetaan pin

| Fungsi | Pin Arduino | Perilaku |
|---|---:|---|
| STEP bersama | 7 | Pulsa bersama untuk keempat driver. |
| DIR bersama | 6 | Arah bersama; awal LOW dan dibalik antara LOW/HIGH. |
| ENABLE M1 | 8 | Aktif LOW, dinonaktifkan HIGH. |
| ENABLE M2 | 9 | Aktif LOW, dinonaktifkan HIGH. |
| ENABLE M3 | 10 | Aktif LOW, dinonaktifkan HIGH. |
| ENABLE M4 | 11 | Aktif LOW, dinonaktifkan HIGH. |
| Limit M1 | 3 | Input `INPUT_PULLUP`; LOW berarti ditekan. |
| Limit M2 | 4 | Input `INPUT_PULLUP`; LOW berarti ditekan. |
| Limit M3 | 5 | Input `INPUT_PULLUP`; LOW berarti ditekan. |
| Limit M4 | 2 | Input `INPUT_PULLUP`; LOW berarti ditekan. |
| Serial | TX/RX bawaan board | Status dan kontrol pada 9600 baud. |

Pastikan driver benar-benar menggunakan logika ENABLE aktif-low dan tiap limit switch terhubung sehingga input LOW saat switch ditekan. Ikuti spesifikasi board dan driver untuk wiring, ground bersama, tegangan, serta arus motor.

## Parameter timing

- `STEP_HALF_PERIOD_US = 80`: interval nominal antara perubahan level STEP. Satu siklus HIGH+LOW memerlukan dua interval; secara ideal frekuensi step sekitar $1/(2 \times 80\,\mu s) = 6{,}25\,kHz$. Overhead `digitalWrite()`, loop, dan pemeriksaan serial/switch dapat menurunkan frekuensi aktual.
- `LIMIT_DEBOUNCE_MS = 25`: perubahan input limit harus stabil selama 25 ms sebelum dianggap valid.
- Tidak ada ramp percepatan/perlambatan. Pulsa dimulai pada laju yang ditentukan oleh interval tersebut.

Perhitungan frekuensi ini hanya teoretis. Jangan menganggapnya sebagai kecepatan gerak mekanis tertentu tanpa mengetahui konfigurasi microstep, steps/rev, lead screw, dan beban.

## Ringkasan fungsi program

| Fungsi | Lokasi | Peran |
|---|---|---|
| `setup()` | `I_131_Hardware_Concurrent_Motor_Test.ino:29` | Membuka Serial 9600, menyiapkan pin STEP/DIR/ENABLE/limit, menyimpan state awal limit, mencetak pesan startup. |
| `loop()` | `...ino:54` | Dispatcher utama: panggil `processSerialControl()`, `scanLimitSwitches()`, lalu `updateStepperPulse()` setiap iterasi. |
| `processSerialControl()` | `...ino:60` | Membaca perintah Serial; `S`/`s` memanggil `stopMotors()`, `R`/`r` memanggil `startMotors()`. |
| `scanLimitSwitches()` | `...ino:74` | Memindai 4 limit switch dengan debounce 25 ms; transisi ke LOW menandai `directionChangePending`. |
| `updateStepperPulse()` | `...ino:107` | Membangkitkan pulsa STEP tiap 80 us dan mengeksekusi pembalikan DIR (hanya saat STEP LOW), lalu mereset pewaktu. |
| `stopMotors()` | `...ino:130` | Menyetop pulsa (STEP LOW), membatalkan pembalikan, menonaktifkan keempat driver (ENABLE HIGH). |
| `startMotors()` | `...ino:141` | Mengaktifkan keempat driver (ENABLE LOW), mereset pewaktu STEP, dan melanjutkan gerak. |

## Variabel dan konstanta global

| Nama | Lokasi | Fungsi |
|---|---|---|
| `enablePins[4]` | `...ino:11` | Pin ENABLE keempat driver `{8, 9, 10, 11}`. |
| `stepPin` | `...ino:12` | Pin pulsa STEP bersama (7). |
| `dirPin` | `...ino:13` | Pin arah DIR bersama (6). |
| `limitPins[4]` | `...ino:14` | Pin limit switch `{3, 4, 5, 2}`. |
| `STEP_HALF_PERIOD_US` | `...ino:16` | Setengah periode pulsa STEP (80 us). |
| `LIMIT_DEBOUNCE_MS` | `...ino:17` | Waktu debounce limit switch (25 ms). |
| `directionState` | `...ino:19` | Level DIR saat ini (LOW/HIGH). |
| `stepState` | `...ino:20` | Level STEP saat ini. |
| `motorsRunning` | `...ino:21` | Status run/stop seluruh motor. |
| `directionChangePending` | `...ino:22` | Penanda permintaan pembalikan arah. |
| `lastStepEdgeUs` | `...ino:23` | Stempel waktu tepi STEP terakhir (us). |
| `lastRawLimitState[4]` | `...ino:25` | Nilai mentah terakhir tiap limit. |
| `stableLimitState[4]` | `...ino:26` | Nilai limit setelah debounce. |
| `lastLimitChangeMs[4]` | `...ino:27` | Waktu perubahan mentah terakhir tiap limit (ms). |

Catatan: nomor baris mengacu pada `I_131_Hardware_Concurrent_Motor_Test.ino`; folder sketch disingkat `...ino`.

## Alur kerja

1. `setup()` (`...ino:29`) membuka Serial (`...ino:30`), mengatur STEP dan DIR sebagai output, menetapkan STEP LOW dan DIR LOW (`...ino:32`-`...ino:35`).
2. Setiap pin ENABLE diatur OUTPUT lalu LOW sehingga driver aktif (`...ino:38`-`...ino:39`).
3. Limit switch disiapkan sebagai `INPUT_PULLUP`; kondisi awal disimpan agar switch yang sudah ditekan saat startup tidak langsung dianggap sebagai transisi baru (`...ino:41`-`...ino:45`).
4. Pewaktu STEP diinisialisasi (`...ino:48`) dan sketch mencetak tiga pesan status startup (`...ino:49`-`...ino:51`).
5. `loop()` (`...ino:54`) berulang kali menjalankan tiga tahap berikut.
6. `processSerialControl()` (`...ino:60`) membaca perintah Serial dan memanggil `stopMotors()`/`startMotors()`.
7. `scanLimitSwitches()` (`...ino:74`) memindai tiap limit dengan debounce 25 ms. Ketika limit beralih menjadi LOW dan stabil, nomor limit dilaporkan (`...ino:92`-`...ino:95`). Jika motor sedang berjalan, `directionChangePending` diminta (`...ino:102`-`...ino:104`).
8. `updateStepperPulse()` (`...ino:107`) membalik DIR saat STEP LOW (`...ino:113`-`...ino:121`), lalu menghasilkan tepi STEP bila selang 80 us terlewati (`...ino:123`-`...ino:127`).
9. Perintah `S` menghentikan pulsa dan menonaktifkan driver (`...ino:64`-`...ino:66`, `stopMotors()` pada `...ino:130`); `R` mengaktifkan kembali driver dan melanjutkan pulsa (`...ino:67`-`...ino:69`, `startMotors()` pada `...ino:141`).

Diagram alur `loop()`:

```text
loop()
  |-- processSerialControl()   : baca 'S'/'R' dari Serial
  |-- scanLimitSwitches()      : debounce 25 ms -> directionChangePending
  |-- updateStepperPulse()     : balik DIR bila STEP LOW, bangkitkan pulsa 80 us
  '-- ulangi
```

## Simulasi saat Arduino pertama kali dinyalakan

Asumsi: semua limit switch tidak ditekan saat startup, Serial Monitor disetel ke 9600 baud, driver/wiring tersedia, dan tidak ada karakter kontrol masuk. Output teks awalnya:

```text
Tes serentak dimulai: M1-M4 berjalan pada jeda 80 us.
Tekan limit mana pun untuk membalik arah semua motor.
Kirim S=stop, R=run.
```

Setelah itu, tidak ada teks status berulang. M1-M4 bergerak serentak dengan DIR LOW dan sinyal STEP bersama. Jika M3 kemudian ditekan dan bertahan selama debounce, contoh tampilan berikut muncul:

```text
Limit M3 ditekan; membalik arah semua motor.
Arah bersama sekarang: DIR HIGH
```

Semua motor kini menerima DIR HIGH. Setelah switch M3 dilepas, tidak ada perubahan arah tambahan. Bila switch lain ditekan setelahnya, arah kembali dibalik bersama. Jika dua switch terdeteksi ditekan pada iterasi pemindaian yang sama, masing-masing dapat dicetak sebagai switch ditekan, tetapi pembalikan dijadwalkan satu kali untuk iterasi itu.

Contoh menghentikan lalu menjalankan kembali dari Serial Monitor:

```text
S
STOP: semua driver dinonaktifkan.
R
RUN: semua motor berjalan.
```

Baris `S` dan `R` di atas adalah input yang diketik pengguna; sketch hanya mencetak respons STOP/RUN. Baris `Limit ...` dan `Arah bersama ...` hanya muncul saat transisi limit yang memenuhi debounce dan pembalikan diproses.

## Catatan uji dan keterbatasan

- Keempat motor tidak bisa memakai profil laju berbeda karena sinyal STEP dan DIR dipakai bersama. Satu laju pulsa yang sama berlaku untuk semuanya. Pastikan syringe pump aman bergerak dengan laju uji ini sebelum menjalankan sketch.
- Tidak ada homing otomatis, penghitung langkah, batas gerak, timeout, deteksi stall, atau penghentian berdasarkan limit. Limit hanya membalik arah.
- Bila satu motor mencapai limit lebih dulu, motor tersebut tetap bergerak ke arah berlawanan setelah pembalikan; motor lain juga ikut berbalik.
- Kecepatan maksimum aman ditentukan oleh hardware dan beban, bukan hanya nilai delay. Mulai dari pengujian tanpa beban/kecepatan lebih rendah bila belum tervalidasi.
- Logika scan limit dan serial berjalan di `loop()`, tetapi pulsa STEP tidak dibangkitkan dengan timer hardware. Frekuensi aktual dapat bervariasi akibat waktu eksekusi loop dan pencetakan Serial.
- Driver dinonaktifkan pada perintah STOP, sehingga motor dapat kehilangan torsi penahan. Pastikan mekanisme tidak jatuh atau bergerak akibat gravitasi/beban saat driver nonaktif.
