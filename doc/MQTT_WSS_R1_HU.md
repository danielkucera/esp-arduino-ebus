# MQTT WSS r1 – saját esp-arduino-ebus firmware

> Történeti r1 dokumentáció. Az aktuális upstream-összeolvasztáshoz és WSS r2
> teszteléshez lásd: [MQTT_WSS_R2_HU.md](MQTT_WSS_R2_HU.md).

## Alap és ellenőrzési állapot

Ez a javítás a feltöltött `esp-arduino-ebus-mqtt-wss.zip` forrására készült.
A GitHub-archívum megjegyzésében szereplő commit:
`91e8bccea78fab11a8d88730cec1af60e97c3ced`.

Nem a v7.2 tag teljes fájljaival írja felül a projektet. A feltöltött ágban már
kisbetűs `src/mqtt.cpp` és `include/mqtt.hpp` található. A cél továbbra is
`esp32-c3-internal`; a `platformio.ini`, `dependencies.lock`, a partíciótábla,
a parancskezelés, a HA discovery és az OTA feltöltés kódja nem változott.
Az ágon már meglévő, v7.2 óta történt upstream módosításokat ez a csomag nem vonja vissza.

A javítócsomag készítésekor elvégzett ellenőrzések:

- 48 konkrét URI/címteszt és 5000 véletlen bemeneti eset sikeresen lefutott.
- A címfeldolgozó teszt GCC-vel, Clanggal, valamint AddressSanitizer és
  UndefinedBehaviorSanitizer mellett is lefutott.
- A build-ellenőrző script 19 mesterséges tesztesete lefutott, beleértve a
  hiányzó biztonsági opciók és túlméretes OTA-kép elutasítását.
- A workflow YAML és a konfigurációs oldal JavaScript-szintaxisa ellenőrizve.

**Teljes ESP32/ESP-IDF-fordítás és valódi adapteres WSS-teszt itt nem történt.**
Ebben a környezetben nem volt PlatformIO/ESP-IDF toolchain, a hálózati függőségek
letöltése sem volt elérhető. A mellékelt GitHub Actions végzi el a teljes buildet.
A natív tesztek nem vizsgálnak TLS-kézfogást, eBUS-kommunikációt vagy eszközstabilitást.

## Mit módosít?

Az MQTT Server mező fogadhat teljes URI-t. Nincs beégetett szervernév, felhasználónév
vagy jelszó a firmware kapcsolati beállításaiban. A dokumentációban és a tesztben
szereplő cím csak példa; a tényleges célt az adapter webes konfigurációjában adod meg.

| Megadott érték | Működés |
|---|---|
| `vicktor-ha.duckdns.org` | Régi mód: `mqtt://vicktor-ha.duckdns.org:1883` |
| `broker.example.org:1884` | MQTT/TCP a megadott porton |
| `mqtt://broker.example.org:1883` | Teljes MQTT/TCP URI |
| `mqtts://broker.example.org:8883` | Közvetlen MQTT/TLS, CA-ellenőrzéssel |
| `ws://broker.example.org:8083/mqtt` | Titkosítatlan MQTT/WebSocket |
| `wss://mqtt.vicktor-ha.duckdns.org:443/mqtt` | MQTT/WebSocket TLS-en keresztül |

A teljes URI-k nem kapják meg a régi, fix 1883-as portot. A port nélküli teljes
URI a kiválasztott protokoll szokásos alapportját használja. WebSocket esetén
hiányzó útvonalhoz `/mqtt` kerül. Saját útvonalat a kód megőriz.

Az elején/végén lévő szóközök levágásra kerülnek, a belső szóközök, vezérlőjelek,
hibás portok és nem támogatott protokollok hibát eredményeznek. URI-ba ágyazott
felhasználónév/jelszó, query és fragment ebben a változatban nincs támogatva.
Használd a külön MQTT User / MQTT Password mezőket. IPv6-cím alakilag megadható
szögletes zárójelben, de tényleges IPv6-kapcsolathoz megfelelő hálózati/SDK-támogatás
is szükséges; ez a javítás azt külön nem kapcsolja be.

TLS esetén a firmware az ESP-IDF beépített, teljes CA-tanúsítványcsomagját használja.
A szervernév- és tanúsítvány-érvényességi ellenőrzés bekapcsolva marad.
Hiba esetén nincs automatikus visszaváltás titkosítatlan kapcsolatra, és nincs
"insecure" beállítás. A tanúsítványcsomag a fordításkor beépül; későbbi CA-változásokhoz
új firmware fordítására lehet szükség. NPMplus tanúsítványát/privát kulcsát nem kell
feltölteni a GitHubra vagy az adapterre.

A `sdkconfig.defaults` a dinamikus TLS-adatpuffereket is engedélyezi. Ez az állandó
memóriaigényt csökkentheti, de a TLS-kézfogás csúcsterhelését nem szünteti meg.
A hardveren a szabad és a minimum szabad memóriát külön ellenőrizni kell.

## Feltöltés a GitHubra – csak a változott fájlok

1. Csomagold ki az `ebus-mqtt-wss-r1-javitas.zip` fájlt a gépeden.
2. A `vicktor1979/esp-arduino-ebus` repóban válaszd a `mqtt-wss` ágat.
3. Maradj a repó gyökerében, ahol a `platformio.ini` is látszik.
4. `Add file` → `Upload files`.
5. A kicsomagolt ZIP **tartalmát** húzd be: a `.github`, `doc`, `include`,
   `scripts`, `src`, `static`, `tests` mappákat és a `sdkconfig.defaults` fájlt.
   Ne a ZIP-et és ne egy ezeket körülvevő, új nevű szülőmappát tölts fel.
6. Ellenőrizd a feltöltési listát. Például `src/mqtt.cpp` és
   `.github/workflows/build-custom-firmware.yml` legyenek benne, ne
   `javitas/src/mqtt.cpp`. A meglévő mappák egyéb fájljait ne töröld.
7. Commit üzenet: `Add verified MQTT WSS support (r1)`.
8. `Commit directly to the mqtt-wss branch` → `Commit changes`.

Minden fájlt egy commitban célszerű feltölteni, különben a félkész állapotból induló
build hiányzó fejlécet vagy tesztet jelezhet. A `.github` mappát se hagyd ki.
A csomagban 10 feltöltendő fájl van:

```text
.github/workflows/build-custom-firmware.yml
doc/MQTT_WSS_R1_HU.md
include/mqtt.hpp
include/mqtt_endpoint.hpp
scripts/check_wss_build.py
sdkconfig.defaults
src/main.cpp
src/mqtt.cpp
static/config.html
tests/mqtt_wss/endpoint_test.cpp
```

A teljes forrás-ZIP csak kényelmi másolat/archiválás. A webes GitHub-feltöltéshez
inkább a fenti, kizárólag változásokat tartalmazó ZIP-et használd.

## Fordítás

A meglévő nevű workflow (`Build Custom eBUS Firmware`) a `mqtt-wss` commitra
elindul. Ugyanazt az `esp32-c3-internal` environmentet használja.

A workflow sorrendje:

1. Címfeldolgozó natív teszt.
2. PlatformIO 6.1.19 telepítése.
3. A generált, környezetspecifikus sdkconfig eltávolítása; teljes INTERNAL build.
4. A ténylegesen generált `sdkconfig.h` ellenőrzése: WSS/SSL, CA-bundle,
   dátumellenőrzés bekapcsolva; bizonytalan TLS-opciók kikapcsolva.
5. OTA-képméret ellenőrzése a projekt változatlan partíciótáblája alapján.
6. Firmware és buildjelentés feltöltése artifactként.

Az `Artifacts` alatt továbbra is `ebus-internal-custom` lesz. Tartalma:

```text
firmware-HW_v5.x-internal-wss-r1.bin
WSS_BUILD_REPORT.json
SHA256SUMS.txt
```

A sikeres ellenőrző lépés végén `WSS_BUILD_CHECKS_OK` jelenik meg.
A JSON a commitot, a képméretet, az SHA-256 hash-t és a fontos SDK-beállításokat
rögzíti. A `hardware_tested: false` szándékos: a CI nem teszteli az adaptert.
Nincs automatikus telepítés, release-törlés vagy feltöltés az eszközre.

Ha piros a futás, az első hibás lépés érdemi hibaüzenetét kell megvizsgálni.
Az esetleg korábbról meglévő `.bin` fájlt ne tekintsd az új build eredményének.

## OTA-frissítés előtt

Mentsd le az adapter konfigurációját a saját gépedre (`Save to File`), valamint
az esetleges parancs-/ütemezésbeállításokat. Legyen meg a most működő hivatalos
INTERNAL OTA-firmware is. A konfigurációs JSON jelszavakat tartalmazhat:
**ne töltsd fel a nyilvános GitHub-repóba.**

Az első saját firmware kipróbálásakor legyen helyi hozzáférés az adapterhez,
és szükség esetére USB-s helyreállítási lehetőség. Csak stabil tápellátás mellett
frissíts. A sikeres fordítás önmagában nem bizonyítja, hogy a készülék stabil lesz.
A javítás nem változtat partíciótáblát; a készülék tényleges flash-elrendezését
viszont a forrás-ZIP-ből nem lehet ellenőrizni.

## Tesztelési sorrend

1. **1883 maradjon nyitva**, az adapter MQTT Server mezője egyelőre maradjon
   a régi `vicktor-ha.duckdns.org` értéken.
2. A zöld Actions-futásból kapott `firmware-HW_v5.x-internal-wss-r1.bin`
   fájlt használd az adapter webes Firmware Upgrade → Upload funkciójában.
   Nem fullflash képet készítettünk.
3. Újraindulás után a státuszban a `firmware.mqtt_patch` értéke legyen `wss-r1`.
   Ellenőrizd, hogy a régi kapcsolaton az MQTT és a Home Assistant továbbra is működik.
4. Az adapter Configuration oldalán írd át az MQTT Server / URI mezőt:

   ```text
   wss://mqtt.vicktor-ha.duckdns.org:443/mqtt
   ```

   MQTT User: `ebus01`; MQTT Password: az eddigi jelszó.
   MQTT Enabled és Home Assistant Enabled maradjon bekapcsolva.
   SNTP Enabled legyen bekapcsolva, a jelenlegi `pool.ntp.org` szerver maradhat.
5. `Save`, majd **Restart to Apply Changes**. A feltöltött forrásban a Save
   az NVS-be ment, a kapcsolat új beállítása újrainduláskor lép életbe.
6. A státuszban ellenőrizd:

   ```text
   mqtt.connected: true
   mqtt.server_valid: true
   mqtt.transport: wss
   mqtt.tls_enabled: true
   mqtt.tls_verification: ca+hostname+expiry
   mqtt.clock_initialized: true
   ```

   A `clock_initialized` csak az alaphelyzetbe állt, 1970 körüli óra kiszűrése;
   nem bizonyítja a pontos időt vagy az SNTP-szinkron sikerét.
   A tanúsítvány dátumait ettől külön az mbedTLS ellenőrzi a kapcsolatfelépítéskor.
7. Ellenőrizd az EMQX-ben az `ebus-eb4198` klienst és az élő eBUS állapotüzeneteket
   Home Assistantban/MQTT Explorerben. A proxy mögött a broker sima WS kapcsolatot
   láthat: a TLS az adapter és az NPMplus között végződik, ez a célzott kialakítás.
8. Figyeld a `heap.minimum_free_bytes` és `heap.largest_free_block` értékeket,
   a Wi-Fi/MQTT újracsatlakozásokat és az Uptime-ot. Próbálj egy szabályos újraindítást
   és egy rövid hálózatmegszakítás utáni újracsatlakozást is.
9. Csak stabil WSS-működés után zárd le a router nyilvános TCP/1883 továbbítását.
   Ellenőrizd ezután is az új állapotüzeneteket. A belső EMQX 1883-as listenerét
   nem szükséges letiltani, ha helyi kliensek használják.

A tervezett útvonal:

```text
eBUS adapter
  → wss://mqtt.vicktor-ha.duckdns.org:443/mqtt
  → NPMplus, nyilvános 443/TLS
  → ws://192.168.1.28:8083/mqtt, belső hálózat
  → EMQX
```

## Hibakeresés és visszaállás

A Logs oldalon például ezek az új üzenetek jelenhetnek meg:

```text
[MQTT] Connecting: transport=wss, TLS=verify-required
[MQTT] Connected: transport=wss, TLS=verified
```

A `TLS=verify-required` azt jelzi, hogy kötelező az ellenőrzés;
a sikeres kapcsolatot a Connected sor igazolja.
Hibánál `esp`, `tls_stack`, `verify_flags`, `socket_errno`, illetve a broker
CONNECT-válaszkódja jelenik meg. A jelszót és a teljes URI-t az új logolás nem írja ki.
A TLS-részletes napló és a memóriaértékek nem jelentenek automatikus hibajavítást.

- `Clock not initialized`: SNTP-beállítás és internetes időszinkron ellenőrzése.
  Induláskor az első TLS-próbálkozás megelőzheti az SNTP-t; a kliens újrapróbálkozik.
  A tanúsítvány-ellenőrzés ilyenkor sem kapcsol ki.
- Nem nulla `verify_flags`: idő, szervernév, hitelesítési lánc ellenőrzése.
- `Broker refused CONNECT`: MQTT hitelesítési adatok / broker szabályok ellenőrzése.
- WebSocket/hálózati hiba: az NPMplus már működő WSS végpontjának, útvonalának,
  tanúsítványának és az adapter DNS/internetelérésének ellenőrzése.
- Újraindulás, memóriahiba: ne tekintsd stabilnak; először állj vissza a működő módra.

Visszaállás a titkosítatlan, korábbi kapcsolatra: a webes konfigurációban az MQTT
Server / URI mezőt állítsd vissza `vicktor-ha.duckdns.org` értékre, Save + Restart.
Ehhez a nyilvános 1883 továbbításnak működnie kell. Ez kézi, ideiglenes visszaállás,
nem automatikus titkosításcsökkentés. Régi, WSS-t nem támogató firmware-re való
visszaflash-elés **előtt** is állítsd vissza a régi szervermezőt.
Ne kapcsold ki a tanúsítvány-ellenőrzést hibakerülésként.

## Hivatkozások

- ESP-MQTT (ESP-IDF 5.5.3, ESP32-C3):
  https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/protocols/mqtt.html
- CA bundle:
  https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/protocols/esp_crt_bundle.html
- SDK-opciók, dátumellenőrzés és dinamikus TLS-pufferek:
  https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/kconfig-reference.html
- GitHub webes fájlfeltöltés:
  https://docs.github.com/en/repositories/working-with-files/managing-files/adding-a-file-to-a-repository
