# MQTT WSS r2 – upstream frissítés

A `mqtt-wss` ág WSS r1 módosításait a 2026-10-02-án ellenőrzött upstream
`master` állapotával egyesíti: `22523a71d789feee51d6c504a1575064a22a6cf2`.
A fork kiinduló commitja: `819f7e2ad7ba491aa4be45cdeb972b16628d24b0`.

## Mi változik?

- Bekerül az upstream #370 parancskezelési, HTTP-, Wi-Fi- és buszkönyvtár-frissítése,
  valamint a #377 és #389 változtatása.
- A meglévő `mqtt://`, `mqtts://`, `ws://`, `wss://` támogatás átkerül az új
  `src/app/mqtt.cpp` és `include/app/` szerkezetbe.
- A WSS/TLS továbbra is kötelező CA-, szervernév- és dátumellenőrzést használ;
  nincs automatikus visszaváltás titkosítatlan kapcsolatra.
- Megmarad a saját WebSocket-útvonal és a hiányzó útvonalhoz tartozó `/mqtt` alapérték.
- Az upstream 64 karakteres MQTT-szervermezője helyett a konfiguráció megőrzi az
  r1-ben támogatott, legfeljebb 500 karakteres teljes URI-t.
- Visszakerülnek a WSS állapotmezői az új státusz-végpontra. A patch azonosítója
  `wss-r2`.
- Javítva az upstream 96 tárolható parancs / 64 elemű rendezési tömb eltérése:
  a parancs- és értéklista is a tényleges `command_capacity` méretet használja.
- A saját GitHub Actions valóban futtatja a hostteszteket, az URI-teszteket és
  a generált TLS-konfiguráció/OTA-képméret ellenőrzését.

Az MQTT WSS az adapter saját MQTT-kapcsolata. Az ebusd addon TCP-kapcsolata
(`192.168.6.100:3333`) és az addon saját MQTT-kapcsolata külön beállítás marad.
Az upstream ág előzetes fejlesztői kiadás; ez a csomag sem hivatalos stabil release.

## Fordítás és eredmény

A `Build Custom eBUS Firmware` workflow a `mqtt-wss` ágra történő push után,
vagy kézzel az Actions oldalon indítható. A cél `esp32-c3-internal`.

Az artifact neve: `ebus-internal-wss-r2`. Tartalma:

- `firmware-HW_v5.x-internal-wss-r2.bin` – alkalmazáskép webes OTA-frissítéshez.
- `WSS_BUILD_REPORT.json` – pontos commit, méret, SHA-256 és tényleges SDK-opciók.
- `SHA256SUMS.txt` – firmware ellenőrzőösszeg.

A `HW_v5.x` a projekt ESP32-C3 firmware-családjának meglévő fájlneve;
a 7.0 hardverhez is ezt a családot használja a projekt. Ez az artifact nem
USB-s, 0x0 címre írható fullflash kép. A partíciótábla változatlan.

A CI és a számítógépes tesztek nem tesztelik a valódi TLS-kézfogást, a kazánt
vagy a tartós adapterstabilitást. A jelentés ezért `hardware_tested: false`.

## Kipróbálás az adapteren

1. Mentsd le helyileg az aktuális konfigurációt és a parancsfájlt. A mentések
   jelszavakat tartalmazhatnak; ne tedd őket a nyilvános repóba.
2. A működő webes frissítővel töltsd fel a fenti OTA `.bin` képet. A firmware
   nevéből önmagában ne következtess a készülék tényleges partícióelrendezésére.
3. Újraindulás után a **Status / System** nézetben, illetve a
   `GET /api/v1/system` JSON-ban ellenőrizd: `firmware.mqtt_patch = wss-r2`.
4. A Configuration oldalon az MQTT Server / URI mezőbe kerüljön a teljes cím:

   ```text
   wss://mqtt.vicktor-ha.duckdns.org:443/mqtt
   ```

   A meglévő MQTT User / Password külön mezőkben maradjon. A fenti cím nincs
   beégetve a firmware-be. A SNTP legyen engedélyezve, hogy a tanúsítványok
   érvényességéhez megfelelő idő álljon rendelkezésre.
5. **Save**, majd **Restart to Apply Changes**.
6. Várt állapot:

   ```text
   mqtt.connected: true
   mqtt.server_valid: true
   mqtt.transport: wss
   mqtt.tls_enabled: true
   mqtt.tls_verification: ca+hostname+expiry
   mqtt.clock_initialized: true
   ```

   A `clock_initialized` csak az alaphelyzetű óra kiszűrése; nem SNTP-bizonyíték.
7. Ellenőrizd az élő MQTT-adatokat és az újracsatlakozást. Hibakereséshez a
   `GET /api/v1/system`, a `GET /api/v1/system/heap` és a
   `GET /api/v1/app/logs` kimenete hasznos. A szabad memória és az Uptime
   alakulását is figyeld. Megosztás előtt takard ki a személyes hálózati adatokat.

Ha a készülék jelenleg még v7.2 parancsformátumot használ, azt az új `fields` /
profil formátumra kell átalakítani. A fork r1 forrása már ezt az újabb szerkezetet
használta, ezért az r1-es forráság frissítése nem ugyanaz, mint egy v7.2-es
parancsfájl automatikus migrációja. Ilyen migrációt ez a merge nem végez.
