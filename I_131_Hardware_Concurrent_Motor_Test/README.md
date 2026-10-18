# Dokumentasi Tes Empat Motor Stepper Serentak

Dokumen ini menjelaskan sketch `I_131_Hardware_Concurrent_Motor_Test.ino`, termasuk pemetaan pin, cara kerja, kontrol serial, dan contoh simulasi saat Arduino dinyalakan.

> **Peringatan:** ini adalah sketch uji bangku. Empat motor bergerak terus menerus tanpa homing atau batas jarak; limit switch hanya membalik arah dan bukan penghentian darurat. Siapkan mekanik agar gerak dua arah aman, awasi pengujian, dan sediakan pemutus daya darurat. Setelan 80 µs bukan jaminan kecepatan maksimum yang aman untuk motor, driver, atau beban.

## Tujuan dan perilaku

Sketch ini mengaktifkan empat driver stepper sekaligus. Semua driver berbagi sinyal STEP (pin 7) dan DIR (pin 6), sehingga semua motor menerima pulsa step dan arah yang sama. Saat startup, motor langsung berjalan dengan DIR awal LOW.

Jika salah satu limit switch ditekan dan stabil setidaknya 25 ms, sketch membalik DIR bersama untuk keempat motor satu kali. Switch harus dilepas dan ditekan kembali untuk memicu pembalikan berikutnya. Sketch tidak menghentikan motor saat switch ditekan.

Sketch menyediakan kontrol Serial Monitor:

- `S` atau `s`: hentikan pulsa, set STEP LOW, dan nonaktifkan semua driver.
- `R` atau `r`: aktifkan kembali semua driver dan lanjutkan gerak dengan arah terakhir.

Port serial menggunakan **9600 baud**. Karakter lain diabaikan. Tidak ada kebutuhan mengirim newline.

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

## Alur kerja

1. `setup()` membuka Serial, mengatur STEP dan DIR sebagai output, menetapkan STEP LOW dan DIR LOW.
2. Setiap pin ENABLE diatur OUTPUT lalu LOW sehingga driver aktif.
3. Limit switch disiapkan sebagai `INPUT_PULLUP`; kondisi awal disimpan agar switch yang sudah ditekan saat startup tidak langsung dianggap sebagai transisi baru.
4. Sketch mencetak tiga pesan status startup.
5. `loop()` berulang kali membaca kontrol Serial, memindai limit switch dengan debounce, lalu memperbarui pulsa STEP.
6. Ketika limit beralih menjadi LOW dan stabil 25 ms, nomor limit dilaporkan. Jika motor sedang berjalan, `directionChangePending` meminta pembalikan DIR.
7. DIR dibalik saat STEP LOW, lalu pembaruan waktu pulsa di-reset agar ada jeda sebelum pulsa berikutnya.
8. Perintah `S` menghentikan pulsa dan menonaktifkan driver; `R` mengaktifkan kembali driver dan melanjutkan pulsa.

## Simulasi saat Arduino pertama kali dinyalakan

Asumsi: semua limit switch tidak ditekan saat startup, Serial Monitor disetel ke 9600 baud, driver/wiring tersedia, dan tidak ada karakter kontrol masuk. Output teks awalnya:

```text
Tes serentak dimulai: M1-M4 berjalan pada jeda 80 us.
Tekan limit mana pun untuk membalik arah semua motor.
Kirim S=stop, R=run.
```

Setelah itu, tidak ada teks status berulang. M1–M4 bergerak serentak dengan DIR LOW dan sinyal STEP bersama. Jika M3 kemudian ditekan dan bertahan selama debounce, contoh tampilan berikut muncul:

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
