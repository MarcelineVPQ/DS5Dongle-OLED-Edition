# Pico2W DualSense 5 Bridge — OLED Edition

[English](README.md) · [中文](README.CN.md) · **Deutsch** · [Español](README.ES.md) · [Français](README.FR.md) · [日本語](README.JA.md) · [Português (Brasil)](README.PT-BR.md)

> Mach aus einem Raspberry Pi Pico2W einen kabellosen Adapter für den DualSense-Controller (DS5) — mit optionalem Statusdisplay direkt auf dem Board.

> **OLED Edition** ist ein Fork von **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** (Upstream). Er ergänzt ein optionales Pico-OLED-1.3-Display (128×64) mit 11 Bildschirmen (Status, Kopplung von bis zu 4 Controllern, Farbwähler für die Leuchtleiste mit Favoriten und Effekt-Vorlagen, Trigger-Test, Gyro-Neigung, Touchpad, Diagnose, CPU/Takt, Bluetooth-Signalstärke, Audio-VU-Meter und ein dauerhaft gespeichertes Einstellungsmenü) sowie einen Soft-Reboot per Tastenkombination am DS5. Für die Kern-Firmware der Bridge ist Upstream maßgeblich; dieser Fork folgt Upstream und setzt seine Zusatzfunktionen obendrauf.

> **🧪 Du willst die neuesten Funktionen? Probier den Entwicklungskanal.** In der App: **App-Einstellungen → Updates → Entwicklung** (die App fragt vorher nach; mit **Zurück zu stabil** kehrst du jederzeit zurück). Entwicklungs-Builds bestehen bei jeder Änderung dieselben automatischen Tests; stabile Versionen bekommen zusätzlich die vollständigen Release-Prüfungen auf echter Hardware und unter Windows sowie Zeit im Alltagseinsatz. Der aktuelle, **0.9.5-dev.15**, bringt gegenüber der stabilen 0.9.4:
>
> - **DualSense Edge:** Fn-Tasten und Rücktasten neu zuweisen, Edge-Profile in PlayStation Accessories, eine eigene Zeichnung und eine eigene OLED-Ansicht
> - **Sticks:** Empfindlichkeitskurven und Stick-Tests
> - **Controller-Mikrofon:** Mikrofonpegel, Echounterdrückung, und die Leuchte der Mute-Taste folgt der Stummschaltung; Stummschaltung und Mikrofonlautstärke des PCs kommen beim Controller an
> - **Ruhezustand und Aufwecken:** Der Controller schaltet sich aus, wenn der PC schläft, und die PS-Taste kann den PC aufwecken
> - **Spiele (Linux):** beliebige Proton-Version pro Spiel, HDR-Einrichtung, und reine XInput-Spiele finden den Controller
> - **Das OLED aus der App bedienen:** KEY0/KEY1 und die Controller-Tasten neben dem Live-Bild, dazu **OLED teilen** (PNG, Zwischenablage oder GIF)
> - **Prüfung der Controller-Firmware** und Infos zum Zustand des Controllers
> - **Headset (Linux):** Stereo, sobald ein Headset im Controller steckt, und zurück zum Lautsprecher, wenn es herauskommt
> - **Koppeln am Board:** das OLED sagt *Press PS to connect*; KEY0 2 s halten zum Koppeln, mit Countdown; kein Dauer-Scannen im Leerlauf
> - **Jede Meldung in 7 Sprachen**

---

## 🛠️ Erste Schritte: die App herunterladen

**[→ DS5 Dongle OLED Edition Configurator herunterladen](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest)** (Linux-AppImage und ein Windows-Installer ohne Administratorrechte, beide getestet; der macOS-Build ist von der Community getestet: [läuft laut Bericht unter macOS 27.0.1](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/17))

Mehr als die App brauchst du nicht. Jedes Release enthält die passende Firmware: Die App installiert sie auf deinem Pico, hält sie aktuell und richtet alles ein:

1. Bau dein Board zusammen (siehe [Hardware](#hardware) unten) und starte die App.
2. Steck den Pico ein und **halte dabei seine BOOTSEL-Taste gedrückt** (ein fabrikneuer Pico braucht die Taste nicht). Die App findet ihn und installiert die Firmware mit einem Klick; nichts muss von Hand kopiert werden.
3. Kopple deinen Controller: Halte **PS-Taste + Create-Taste**, bis die Leuchtleiste blinkt. Die App zeigt an, wenn er verbunden ist.

![Die App: dein Controller live, in seinen eigenen Farben](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/live-controller.png)

Dein Controller wird in seinen eigenen Farben gezeichnet und bewegt sich mit, während du ihn benutzt; klick auf ein Teil, um es einzurichten. Die **[Schritt-für-Schritt-Anleitung](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/GUIDE.md)** (auf Englisch) erklärt alles, vom Zusammenbau des Boards bis zu jeder Einstellung:

- **Der Controller:** Modi und Favoriten für die Leuchtleiste, Effekte der adaptiven Trigger, **haptisches Feedback mit Vibrationsintensität pro Seite** (haptisches Feedback und klassische Vibration, mit 3-Sekunden-Test), Audio Auto Haptics, Lautsprecher und integriertes Mikrofon (mit Tests), Tastenzuweisung (alle Tasten in einer Liste, auch die Fn-Tasten und Rücktasten des DualSense Edge, und **gespeicherte Tastenbelegungen**, die ein Spiel während des Spielens einwechseln kann, ohne dass etwas in den Flash geschrieben wird: Firmware v0.9.4), die vier Kopplungsplätze und **Controller-Info** (die eigene Firmware des Controllers, mit Sonys neuester verglichen, seine Akkuspannung und Lebensdauer-Zähler).
- **Kalibrierung:** Stick-Drift, Sticks oder Trigger, die nicht bis zum Anschlag kommen, ein wanderndes Gyroskop — im Pico korrigiert (pro Kopplungsplatz), für jedes Spiel; in den Controller wird nichts geschrieben. Dazu eine **Empfindlichkeitskurve** pro Stick und Stick-Tests (Rundheit und Drift).
- **Das Board:** sein OLED-Bildschirm live gespiegelt, ein Diagnose-Dashboard (Temperatur, Takt, Bluetooth- und USB-Verkehr, die Audio-Pipeline und wie lange jeder Teil der Firmware braucht), Einstellungen für Energie, Display und USB sowie **Firmware-Updates mit einem Klick** (keine BOOTSEL-Taste, kein Kopieren auf ein Laufwerk).
- **Spiele:** deine Bibliothek aus Steam, Heroic und Lutris (Windows: Steam, GOG Galaxy und Epic) als Liste und als Cover-Raster, mit Controller-Profilen pro Spiel (jede Controller-Einstellung, die das Spiel ändert, einschließlich Tastenbelegung und Stick-Kurven, live in der Controller-Ansicht bearbeitet), DualSense-Unterstützung pro Spiel und unter Linux Einrichtungskorrekturen plus der DualSense-Proton-Build.
- **Ganz deins:** ein Thema in den Farben deines Controllers, Glas, Regler für das Aussehen, Farbsehen-Modi, Bewegung reduzieren, eine Sprachausgabe über den Lautsprecher des Controllers, 7 Sprachen, ein Symbol im Infobereich.

Die App spricht mit Firmware **v0.8 oder neuer** über eine kleine USB-Schnittstelle für die Konfiguration (Kalibrierung und die Haptik pro Griff brauchen **v0.9**; die App bietet das Update an). Spiele sehen diese Schnittstelle nie, der Pico sieht für sie also weiterhin genau wie ein echter DualSense aus. Quellcode der App: **[MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web)**.

> **Das Konfigurationswerkzeug im Browser wurde eingestellt**: Die Desktop-App kann alles, was es konnte, und mehr. Seine alte Adresse, [marcelinevpq.github.io/DS5Dongle-OLED-Config-Web](https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/), ist jetzt die Download-Seite der App. Um einen Pico ohne die App zu flashen, halte beim Einstecken BOOTSEL gedrückt und kopiere die `.uf2` aus dem Release auf das Laufwerk, das erscheint.

## 🔄 Updates und Firmware-Installation, für dich erledigt

**Die App hält sich selbst aktuell.**

- **Signiert und geprüft.** Jedes Update kommt aus einem Feed, der mit unserem Ed25519-Schlüssel signiert ist. Die App ignoriert einen Feed ohne gültige Signatur, prüft Größe und SHA-256 jedes Downloads vor der Installation und installiert nie von selbst eine ältere Version.
- **Zwei Kanäle:** **Stabil** für alle, **Entwicklung** für die neuesten Funktionen. **Zurück zu stabil** bringt App und Firmware auf die neueste stabile Version zurück.
- **Deine Wahl:** Mit **Updates automatisch installieren** (die App fragt einmal) werden Updates im Hintergrund geladen und geprüft, und die neue Version startet beim nächsten Öffnen der App. Ist **Automatisch nach Updates suchen** aus, geht die App nur online, wenn du eine Prüfen-Schaltfläche drückst.
- **Je nach System:** Unter Linux legt das AppImage die neue Version neben sich ab, und der Menüeintrag folgt ihr. Unter Windows läuft der Installer pro Benutzer, ohne Administratorrechte. Unter macOS zeigt die App das Update mit einem Link zum Release.

**Firmware mit einem Klick installieren.**

- **Mitgeliefert:** Jedes App-Release enthält die Firmware, mit der es gebaut und getestet wurde, also lässt sich ein neues Board auch offline einrichten. Neuere Firmware kommt aus demselben signierten Feed.
- **Ersteinrichtung:** Steck den Pico mit gedrückter BOOTSEL-Taste ein (ein fabrikneuer Pico braucht die Taste nicht). Die App findet ihn und installiert die Firmware.
- **Updates:** keine Taste, kein Kopieren auf ein Laufwerk. Die App startet das Board selbst in BOOTSEL neu, schreibt die Firmware über USB und verbindet sich wieder.
- **Vor dem Schreiben geprüft:** Der SHA-256 der Datei muss zum Feed passen, und es muss Firmware für diesen Chip sein. Eine Datei, die durchfällt, schreibt nichts.
- **Nach dem Schreiben geprüft:** Die App wartet, bis die neue Firmware startet. Kommt das Board stattdessen in BOOTSEL zurück, sagt die App das und bietet **Erneut installieren** an. Während die Firmware geschrieben wird, startet die App nicht neu, beendet sich nicht und aktualisiert sich nicht.
- **Deine Einrichtung bleibt:** Einstellungen, Kopplungen, Kalibrierung und Tastenbelegungen liegen in einem eigenen Bereich des Flash-Speichers. Jeder Firmware-Build wird geprüft, dass er diesen Bereich nicht berührt, und ein App-Test prüft, dass ein Update nur die Sektoren der Firmware-Datei schreibt.

## 🔬 Wie es gebaut und getestet wird

Jede Änderung durchläuft alle automatischen Prüfungen. Jedes Release bekommt weitere dazu. Die vollständige Liste mit den Dateien hinter jeder Prüfung: [docs/QUALITY.md](docs/QUALITY.md) (auf Englisch).

**Jede Änderung**

- **App:** über 1.600 Tests (Unit-, Integrations- und Sicherheitstests) unter Linux, Windows und macOS, dazu Typprüfung, ESLint und Prettier.
- **Firmware:** 185 Host-Tests der Firmware-Logik mit AddressSanitizer und UndefinedBehaviorSanitizer und fünf Firmware-Builds (Pico 2 W, Pico W, Debug und zwei Varianten). Der Build schlägt fehl, wenn der zeitkritische Code den RAM verlässt oder sich die Flash-Einrichtung ändert.
- **Regel-Gates der App:** die Electron-Sicherheitseinstellungen, kein unsicheres HTML, ein Theme-System ohne verirrte Farben, kein doppelter Code und keine doppelten Dateien ohne Urteil.
- **Schriftliche Urteile:** Jede Schleife, die wartet oder auf einem heißen Pfad läuft, jeder Fehlerpfad, jeder Cache und jede Ressource, die der Code belegt (ein Timer, ein Listener, ein Gerät), trägt einen Kommentar mit ihrem Urteil. Eine neue ohne Urteil lässt die Prüfung fehlschlagen.
- **Sicherheits-Scans:** Semgrep, Gitleaks und npm audit, auch wöchentlich.

**Jeder Entwicklungs-Build**

- **UI-Prüfer:** Eine eigene Kopie der App mit einem simulierten Board öffnet jede Seite in allen 7 Sprachen in zwei Fenstergrößen. Er schlägt fehl bei abgeschnittenem Text, Kontrast unter WCAG AA oder einem Bedienelement ohne Namen.
- **Ressourcen-Budgets** gemessen unter Linux und in einer Windows-11-VM.
- **Windows-Test** der installierten App in einer Windows-11-VM und ein **macOS**-Smoke-Test auf einem echten Mac in GitHub Actions (so installiert wie von einem Nutzer, gestartet und bedient).

**Vor einem stabilen Release**

- **Die vollständige UI-Prüfung:** jeder Bildschirm und jedes Panel, alle 7 Sprachen, beide Größen, jedes Theme und jeder Stil, jede Farbvariante beider Controller-Modelle.
- **Der lange Budget-Lauf** mit einem 30-minütigen Dauertest auf Speicherwachstum, unter Linux und Windows.
- **Hardware-Prüfungen** an einem echten Board mit einem DualSense: Live-Ansicht, haptisches Feedback, Lichtleiste, Tastenbelegungen, Slots, Trigger, Audio-Zähler, Neustart und BOOTSEL, Ersteinrichtung, der Updater, Spiele.
- **Alltagseinsatz:** die eigene Freigabe der Entwicklerin, nachdem sie den Build im Alltag benutzt hat.

**Effizienz**

- **Budgets, die das Release stoppen:** höchstens 10 Bilder pro Sekunde auf einer ruhenden Seite, unter 2 % eines CPU-Kerns sichtbar und im Leerlauf, unter 0,5 % versteckt im Infobereich, höchstens 275 MB Speicher und unter 5 MB Zuwachs in 30 Minuten. Ein gerissenes Budget wird behoben, nicht angehoben. Die neuesten Zahlen: [BUDGETS.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md).
- **GPU-Rendering:** Effekte zeichnet die Grafikkarte, und bei verstecktem Fenster wird nichts gezeichnet.
- **Firmware:** Der Audio-Pfad läuft aus dem RAM, und die App zeigt, wie lange jeder Teil der Firmware braucht.

**Sicherheit und Vertrauen**

- **Signierter Feed** (Ed25519) und per SHA-256 geprüfte Downloads, wie oben.
- **Überprüfbare Downloads:** Jede Release-Datei hat eine Build-Attestierung. `gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition` zeigt, dass sie vom Release-Workflow dieses Repositorys aus ihrem Tag gebaut wurde.
- **Abgesicherte App:** Fenster in der Sandbox, strenge Content Security Policy, jede Nachricht aus dem Fenster wird auf ihren Absender geprüft und validiert, USB-Zugriff nur auf das Board und den Pico-Bootloader. Exakte Versionen der Abhängigkeiten, frühestens 2 Wochen nach ihrem Erscheinen gewählt. Das Audit: [SECURITY_AUDIT.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md).
- **Ein echter DualSense für Spiele:** Der USB-Deskriptor des Boards ist Byte für Byte der von Sony (289 Bytes), und die Konfigurator-Schnittstelle ist getrennt, sodass Spiele sie nie sehen.

**Barrierefreiheit und Sprachen**

- **7 Sprachen, vollständig:** Ein Test schlägt fehl, wenn einer Sprache ein Text fehlt, und Controller-Funktionen tragen in jeder Sprache Sonys offizielle Namen.
- **Lesbar und benannt:** Der UI-Prüfer prüft Kontrast und Namen der Bedienelemente in jeder Sprache; Text muss in jeder Größe passen.
- **Wie du es brauchst:** Farbsehen-Modi, Bewegung reduzieren und Tastaturbedienung in jedem Aufklappmenü.

---

## Überblick

Mit diesem Projekt arbeitet der Raspberry Pi Pico2W als Bluetooth-Bridge für den DualSense-Controller: kabellose Verbindung mit erweiterter Haptik-Unterstützung.

## Funktionen

**Kern-Bridge (von Upstream):**

- Volle DualSense-Anbindung über den Pico2W
- HD-Haptik (erweitertes Vibrations-Feedback)
- Kabellose Bluetooth-Bridge
- Haptik-Verstärkung über die Mikrofonlautstärke einstellbar
- Konfigurierbares Verhalten von LED und Verbindungstrennung

**Ergänzungen der OLED Edition:**

- **Native adaptive Trigger des DualSense in PC-Spielen unter Linux/Proton — über den Pico.** Spiele mit nativer DualSense-Unterstützung steuern die adaptiven Trigger des Controllers kabellos über den Pico an, **1:1 wie bei einem per Kabel angeschlossenen DualSense**. Voraussetzung ist ein Proton mit dem Wine-Fix `winebus.sys` #9034 (Wine 11 / aktuelles Proton-GE) bei deaktiviertem Steam Input — **keine Startoption nötig** (Wine 11 aktiviert den nativen hidraw-Pfad standardmäßig). Funktioniert mit **Steam und Heroic** (Epic/GOG). Die Firmware macht den Pico Byte für Byte ununterscheidbar von einem echten DS5, damit das Spiel ihn akzeptiert (ausführlich in [CHANGELOG.md](./CHANGELOG.md)). Kompromiss: Die Tabs Config/Remap des Browser-Werkzeugs sind mit dieser Firmware deaktiviert (siehe den Hinweis oben).
- Optionales Pico-OLED-1.3-Statusdisplay mit **11 Bildschirmen** (Status, Plätze, Leuchtleiste, Trigger-Test, Gyro-Neigung, Touchpad, Diagnose, CPU/Takt, RSSI, VU-Meter, Einstellungen)
- **Kalibrierung im Pico (v0.9)** — Stick-Mittelpunkt, Deadzone und Radius bei vollem Ausschlag, Trigger-Weg und Gyro-Nullpunkt, pro Kopplungsplatz, angewendet auf das, was Spiele empfangen (nie in den Controller geschrieben). Eingestellt über die geführte Kalibrierung der App.
- **Vibrationsintensität pro Seite (v0.9)** — links und rechts getrennt, für haptisches Feedback und klassische Vibration gleichermaßen.
- **Empfindlichkeitskurven (v0.9.5)** — eine Kurve pro Stick (Linear, Präzise, Stetig, Schnell, Digital oder vier eigene Punkte), im Pico nach der Kalibrierung angewendet, also in jedem Spiel wirksam; das Profil eines Spiels kann während des Spielens seine eigene einwechseln.
- **Unterstützung für den DualSense Edge** — der Edge wird am Firmware-Report, den er beim Verbinden sendet, vom DualSense unterschieden; seine Fn-Tasten und Rücktasten lassen sich wie jede andere Taste zuweisen, und der OLED-Bildschirm Status zeigt sie an. Seine benutzerdefinierten Profile werden in Sonys App PlayStation Accessories angezeigt und gespeichert (Upstream awalol 8a2f576; die App braucht nach dem Verbinden des Controllers eventuell ein paar Sekunden). Im DS5-Modus bietet die App kein Firmware-Update mehr an, das fehlschlägt (Upstream awalol ae83907).
- **Controller-Info (v0.9.5)** — die App kann über den Pico die Firmware-Versionen, die Akkuspannung und die Lebensdauer-Zähler des Controllers auslesen (nur lesend; die Seriennummer des Controllers wird geschwärzt, bevor die App sie sieht).
- **Laufzeiten pro Stufe (v0.9.5)** — der Pico misst die Dauer jedes Teils seiner eigenen Arbeit (Bluetooth, USB, Audio, Eingabe, OLED, Lautsprecher- und Mikrofon-Frames von core1) für die Karte Laufzeiten in der App.
- **OLED-Spiegelung (v0.9)** — die App zeigt den Bildschirm des Picos pixelgenau, mit oder ohne OLED-Add-on.
- **Tastenzuweisung** — jedes der 16 digitalen Bedienelemente (Aktionstasten, Richtungstasten, L1/R1/L2/R2, Stick-Klicks, Create-Taste und Optionstaste) sowie die Fn-Tasten und Rücktasten des DualSense Edge lassen sich jeder anderen Taste zuweisen. Gespeichert auf dem Pico und angewendet, bevor der Host den Report sieht, also wirksam in **jedem Spiel und Betriebssystem ohne Software auf dem Host**; Standard ist die Identität (keine Umbelegung). Bearbeite sie grafisch in der [Konfigurations-App](#️-erste-schritte-die-app-herunterladen) unter **Tastenzuweisung** oder ohne Oberfläche mit `scripts/remap_test.py`. In einem eigenen Flash-Sektor gespeichert, übersteht einen Neustart.
- **PC-Ruhezustand und Aufwecken (Upstream awalol)** — der Controller schaltet sich aus, sobald der PC 3 s geschlafen hat (standardmäßig an), und optional weckt die PS-Taste den PC (standardmäßig aus: ohne verbundenen Controller meldet sich der Pico dann als kleine Tastatur nur zum Aufwecken, „DS5 Dongle Wake Keyboard“, die der PC als Wecker zulassen muss). Mit verbundenem Controller ist der Pico genau wie vorher der DualSense. Beides findest du in der App und im OLED-Menü Settings. Zum Aufwecken muss der PC USB im Ruhezustand mit Strom versorgen.
- **Intensität des Trigger-Effekts, Lautsprecher-Verstärkung, Lautstärke sperren, Mikrofon- und Lautsprecherwahl, Status-Pin (Upstreams Einstellungen, Protokoll 1.13)** — schwächere adaptive Trigger in 10 Stufen (standardmäßig die Stärke des Spiels), die Vorverstärkung des Controller-Verstärkers, Spiele dürfen die Lautstärke des Controllers nicht ändern, das Mikrofon als Auto / Integriert / Headset, das Audio des PCs auf Auto / Lautsprecher / Headset / Aus, und ein GPIO, der die Controller-Verbindung anzeigt. Die Leuchte der Mute-Taste folgt der Stummschaltung des Mikrofons, und Stummschaltung und Mikrofonpegel des PCs kommen beim Controller an. Eine frische Konfiguration fragt mit 500 Hz ab.
- **Dauerhafte Kopplung von 4 Controllern** — bis zu vier DualSense koppeln, am OLED zwischen ihnen wechseln, Platz 0 verbindet sich beim Start automatisch
- **Farbwähler für die Leuchtleiste** mit 4 eigenen Favoritenplätzen + Effekt-Vorlagen Atmen / Regenbogen / Überblenden
- **Dauerhaft gespeichertes Einstellungsmenü** für die 8 Konfigurationsfelder der Firmware (Haptik-Verstärkung, Lautsprecherlautstärke, Abfragerate usw.) mit den Aktionen Zurücksetzen und Alle Plätze löschen, die durch Halten bestätigt werden
- **Energiestufen bei Leerlauf des OLEDs** — Helligkeit manuell durchschalten (KEY1 lang drücken), nach 2 min Leerlauf automatisch stark gedimmt mit einem kleinen atmenden Punkt, nach 15 min Leerlauf ganz aus. Wacht sofort auf bei Tastendruck, Kopplung eines Controllers oder Eingabe. Echter Schutz vor Einbrennen, nicht nur eine Kontrastanpassung.
- **Soft-Reboot** ohne USB-Abziehen: `PS + Mute` am DS5 halten (geht auch ohne Display) oder **KEY0 + KEY1 1 s gemeinsam halten** am OLED-Add-on (ersetzt die ältere Doppelklick-Geste auf KEY0, die beim schnellen Blättern leicht versehentlich ausgelöst wurde)
- **Sauberer Controller-Lautsprecher und sauberes Mikrofon** — der Audiopfad von core1 (Opus, Resampler, Speicheroperationen) läuft aus dem RAM, Audio geht in Upstreams gepackten `0x39`-Reports raus, und das Mikrofon wird auf core1 dekodiert und vom USB-Takt des Hosts getaktet. Damit sind das lange bestehende Knistern des Lautsprechers und das Stottern bei aktivem Mikrofon weg (siehe [CHANGELOG.md](./CHANGELOG.md)). **Dank an [awalol](https://github.com/awalol/DS5Dongle)**: Das ist Upstreams Audio-Arbeit, hierher portiert.
- **Prüfdurchgang an der Kern-Bridge** — kritischer Fix für einen Stack-Überlauf im Audiopfad (behebt das lange bestehende „Audio-Stottern“), Sicherheitshärtung, Watchdog, Längenprüfung an den Grenzen zwischen HID und L2CAP (siehe [CHANGELOG.md](./CHANGELOG.md))

## 🐧 Linux: native adaptive Trigger (Proton)

Damit ein Spiel unter Linux die **adaptiven Trigger des Controllers über den Pico** ansteuern kann, brauchst du ein Proton mit dem Wine-Fix `winebus.sys` **#9034**. Offizielle **GE-Proton 11**-Releases enthalten ihn inzwischen. Der hier bereitgestellte Build, **`GE-Proton11-7-DualSense`** (siehe [sein Release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense)), ist das offizielle `GE-Proton11-7` plus ein weiterer Fix in `winebus.sys`: Der DualSense meldet seine Seriennummer als Bluetooth-MAC mit Doppelpunkten, wodurch sich sein Gerätepfad unter Windows nicht öffnen lässt; Spiele, die Sonys libScePad nutzen, erreichen dann die Trigger nicht. Dieser Build ersetzt diese Zeichen, sodass sich der Pfad öffnen lässt. Auf diesem Build wurden die Trigger-Ergebnisse unten überprüft.

> Warum: Der Fehler #9034 unterdrückt das SDL-Gamepad, wenn es für dieselbe VID/PID ein hidraw-Gerät gibt; das Pad ist also *überall außer im Spiel* sichtbar. Der Fix lässt den nativen (triggerfähigen) Pfad und ein funktionierendes Gamepad nebeneinander bestehen.

**Einmalige Einrichtung**

1. **Installiere den Proton-Build.** Lade `GE-Proton11-7-DualSense.tar.gz` aus [dem Release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense) herunter und entpacke es nach:
   - **Steam:** `~/.steam/root/compatibilitytools.d/`
   - **Heroic:** `~/.config/heroic/tools/proton/`

   Dann **starte Steam vollständig neu**, damit es das neue Kompatibilitätstool erkennt.
2. **Deaktiviere Steam Input**, damit das Spiel nativ mit dem Controller spricht — Steam → Spiel **Eigenschaften → Controller → Steam Input deaktivieren** (oder global: Einstellungen → Controller → PlayStation-Controller-Unterstützung ausschalten).

**Pro Spiel**

3. **Erzwinge das Proton:** Steam → Rechtsklick auf das Spiel → **Eigenschaften → Kompatibilität → Die Verwendung eines bestimmten Steam-Play-Kompatibilitätstools erzwingen → `GE-Proton11-7-DualSense`**. (Heroic: setze die *Wine-Version* des Spiels darauf.)

**Keine Startoption nötig** — Wine 11 aktiviert den nativen hidraw-Pfad standardmäßig, der Pico wird also automatisch an das Spiel übergeben. Einfach starten — native adaptive Trigger lösen über den Pico aus, 1:1 wie bei einem per Kabel angeschlossenen DualSense (ohne Flags überprüft mit Cyberpunk 2077, Uncharted, Spider-Man Remastered, The Last of Us Part I, Avatar, Indiana Jones, Star Wars: Galactic Racer — und mit **Steam und Heroic**). Das Spiel muss **native DualSense-Unterstützung** haben (reine XInput-Spiele liefern Rumble, aber keine adaptiven Trigger — das ist eine Grenze der Engine, nicht des Picos). Der erste Start unter einem neuen Proton richtet Gebietsschema und Prefix ein und kann eine Minute lang „Keine Rückmeldung“ zeigen — das ist die Einrichtung, kein Absturz; lass ihn fertig werden.

**Lass das Audio des Controllers auf seinem Standardprofil.** GE-Proton 11 leitet die Haptik des DualSense selbst und erwartet die Karte im Standardprofil **HiFi**. Eine WirePlumber-/PulseAudio-Regel, die den DualSense auf **pro-audio** zwingt (ein verbreiteter Tipp für ältere Proton-Builds), führte dazu, dass Avatar Trigger ohne Kraft sendete und die HD-Haptik verlor (überprüft; andere von Sony portierte Spiele verhalten sich vermutlich genauso). Entferne eine solche Regel oder prüfe mit `pactl list cards`, dass der DualSense `Active Profile: HiFi (...)` anzeigt.

### Heroic (Epic / GOG) — gleiches Rezept, zwei zusätzliche Voraussetzungen

Native Trigger funktionieren unter Heroic genauso, aber Launcher außerhalb von Steam reagieren empfindlich auf zwei Dinge auf dem Host, die den Controller stillschweigend wegnehmen:

1. **Beende Steam vollständig** — ein Steam im Hintergrund schnappt sich den DualSense vor Spielen außerhalb von Steam (oder deaktiviere Einstellungen → Controller → PlayStation-Controller-Unterstützung).
2. **Kein globales `PROTON_PREFER_SDL`** — diese Umgebungsvariable erzwingt den SDL/Xbox-Pfad und *unterdrückt* native Trigger. Nutze sie nur pro Spiel, als bewussten Rückfall auf ein generisches Pad.

**Übersicht der Startoptionen**

| Variable | Wert | Zweck |
|---|---|---|
| `PROTON_ENABLE_HIDRAW` | `0x054c/0x0ce6` | *Veraltet / optional — unter Wine 11 **nicht nötig***, das den nativen hidraw-Pfad standardmäßig aktiviert. Schadet nicht, wenn gesetzt. Nur relevant bei älterem Proton, wo hidraw erst eingeschaltet werden musste (dem fehlt aber auch der Fix #9034, die Trigger funktionieren dort also ohnehin nicht). |
| `PROTON_PREFER_SDL` | `1` | *Alternative, nur Eingabe.* Erzwingt den SDL-Gamepad-Pfad — funktionierende Eingabe + Rumble, aber als generisches/Xbox-Pad (**keine Trigger**). Nur verwenden, wenn ein Spiel keine native DualSense-Unterstützung hat und `ENABLE_HIDRAW` nicht hilft. **Nicht** mit `ENABLE_HIDRAW` für Trigger-Spiele kombinieren. Braucht ein Proton, das sie beachtet: Der veröffentlichte **GE-Proton11-7-DualSense** ignoriert sie, also spiele reine XInput-Spiele mit dem normalen GE-Proton, bis der nächste DualSense-Build erscheint. |
| `PROTON_LOG` | `+hid` | *Nur zur Diagnose* (nicht zum normalen Spielen). Schreibt einen Trace der HID-Erkennung — bei Steam landet er unter `~/steam-<appid>.log`; Heroic setzt `PROTON_LOG_DIR` auf dein Home-Verzeichnis, er landet also unter `~/steam-0.log`. Nützlich, um zu bestätigen, dass das Spiel den nativen HID-Pfad geöffnet hat. |

> **Heroic vs. Steam:** Native Trigger funktionieren mit **beiden** — und keiner braucht unter Wine 11 eine Startoption. Steam: Runner setzen + Steam Input deaktivieren. Heroic: Runner setzen, **Steam vollständig beenden** (ein Steam im Hintergrund nimmt Spielen außerhalb von Steam das Pad weg) und `PROTON_PREFER_SDL` ausgeschaltet lassen. In jedem Fall muss das Spiel DualSense nativ unterstützen — reine XInput-Spiele (z. B. Ghostrunner, Control) liefern Rumble, aber keine adaptiven Trigger, auf jedem Betriebssystem. Diese Spiele brauchen `PROTON_PREFER_SDL=1` und einen Proton-Build, der das beachtet: Der veröffentlichte **GE-Proton11-7-DualSense** tut das nicht, nutze für sie also das normale GE-Proton, bis der nächste DualSense-Build erscheint.

## Hardware

### Erforderlich

| Teil | Hinweise | Ca. Preis |
|---|---|---|
| **Raspberry Pi Pico 2 W** | RP2350-MCU mit integriertem CYW43 für Bluetooth/WLAN. [Offizielle Produktseite](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) | ~7 USD |
| **Sony DualSense Controller** | Jeder PS5-DualSense (VID `054C:0CE6`) oder DualSense Edge (`054C:0DF2`). | — |
| **USB-C-Kabel** | Verbindet den Pico 2 W mit dem Host-PC. | — |

### Optional (dringend empfohlen)

| Teil | Hinweise | Ca. Preis |
|---|---|---|
| **Waveshare Pico-OLED-1.3** | 128×64-OLED-Aufsteckplatine mit SH1107 (SKU HIPI1798). Wird direkt auf die Stiftleisten des Pico 2 W gesteckt. Die Firmware steuert sie automatisch an, wenn sie vorhanden ist, und tut ohne sie einfach nichts. [Produktseite](https://www.waveshare.com/pico-oled-1.3.htm) · [Wiki](https://www.waveshare.com/wiki/Pico-OLED-1.3) | ~6 USD |
| **Kleiner Kühlkörper** für den RP2350 | Nicht nötig: Die Firmware betreibt den RP2350 mit seinem Nenntakt von 150 MHz und Standardspannung (siehe [Leistung / Takt](#leistung--takt)). Schadet nicht, wenn du schon einen hast. | 1–3 USD |

### Bezugsquellen

Der Pico 2 W und das Waveshare Pico-OLED-1.3 sind weltweit gut erhältlich:

- **Adafruit**, **Pimoroni**, **The Pi Hut**, **DigiKey**, **Mouser** — große Elektronik-Distributoren (USA / EU)
- **Waveshares eigener Shop** für das OLED-Add-on
- Regionale **Amazon**-Shops — suche nach `Raspberry Pi Pico 2 W` und `Waveshare Pico-OLED-1.3` (oder der SKU `HIPI1798`)
- **AliExpress** — originale Waveshare- und Pico-Ware sowie Klone; achte auf die Verkäuferbewertungen

## Einrichtung

### Firmware flashen

Die [App](#️-erste-schritte-die-app-herunterladen) erledigt das für dich. Wenn du stattdessen von Hand flashen willst (oder für den Pico-W-Build, der ein CI-Artefakt ist):

1. Halte die BOOTSEL-Taste am Pico2W gedrückt
2. Verbinde den Pico2W per USB mit deinem Computer
3. Das Gerät erscheint als USB-Speicher
4. Zieh die .uf2-Firmwaredatei (aus den [Releases](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases)) auf das Gerät

### Controller koppeln

1. Versetze den DualSense-Controller in den Bluetooth-Kopplungsmodus
2. Warte, bis der Pico2W ihn erkennt und sich verbindet
3. Sobald er verbunden ist, erscheint das Gerät am Host-System

## Konfiguration

Es gibt vier Wege, die Firmware zu konfigurieren:

**Konfigurations-App (empfohlen):** der [DS5 Dongle OLED Edition Configurator](#️-erste-schritte-die-app-herunterladen) für Linux und Windows (ein macOS-Build liegt bei und läuft laut Bericht unter macOS 27.0.1; die Seite Spiele listet unter Linux Spiele aus Steam, Heroic und Lutris und unter Windows aus Steam, GOG Galaxy und Epic, und die Proton-Einrichtung gibt es nur unter Linux). Sie findet den Pico von selbst, übernimmt Änderungen sofort und speichert sie mit einem Klick in seinen Flash. Braucht Firmware v0.8+.

**Am Gerät (mit OLED-Add-on):** nutze das Menü **Settings** auf dem Bildschirm (11. Bildschirm). Richtungstasten ▲▼ bewegen die Auswahl, ▶◀ ändern Werte, △ speichert in den Flash.

**Terminal-CLI (jedes Betriebssystem, jeder Browser):** installiere hidapi und nutze dann `scripts/set_ds5.py`:

```bash
pip install hidapi
scripts/set_ds5.py                            # aktuelle Konfiguration anzeigen
scripts/set_ds5.py --auto-haptics fallback    # ein Feld ändern, in den Flash speichern
scripts/set_ds5.py --speaker-volume -10 --haptics-gain 1.5
scripts/set_ds5.py --slot 2                   # aktiven Kopplungsplatz wechseln
scripts/set_ds5.py --version                  # Firmware-Version
scripts/set_ds5.py --rssi                     # Bluetooth-Signal live, dB relativ zum idealen Bereich (0 = ideal)
scripts/set_ds5.py --help                     # alle Optionen
```

Das Skript spricht mit der Firmware über die USB-HID-Feature-Reports `0xF6`/`0xF7`/`0xF8`/`0xF9`. Seit v0.6.12 sind diese Reports nicht im HID-Deskriptor deklariert (Spiele brauchen ihn Byte für Byte identisch mit einem echten DualSense), daher funktioniert das Skript nur unter Linux; nutze anderswo die Konfigurations-App. Portiert von [loteran/DS5Dongle](https://github.com/loteran/DS5Dongle) und für das Feld `current_slot` dieses Forks erweitert.

**Tasten am DualSense-Controller (alter Rückfallweg, ohne OLED, ohne CLI):**

### Mikrofonlautstärke

Steuert den Faktor der Haptik-Verstärkung. Bereich: `[1.0 – 2.0]`.

### Lautsprecher stumm

Deaktiviert die LED-Anzeige der Verbindung. Wirkt, nachdem sich der Controller neu verbunden hat.

### Mikrofon stumm

Deaktiviert das stille Trennen der Verbindung.

## Hinweise

Der Pico ist für das System erst sichtbar, nachdem der Controller verbunden ist

Manche Einstellungen wirken erst nach einem erneuten Verbinden

### LED-Anzeige bei schwachem Akku

Meldet der verbundene DualSense einen Akkustand von 10 % oder weniger (und lädt er nicht), wechselt die LED auf dem Pico von Dauerlicht zu Blinken mit 1 Hz, sodass du die Warnung auf einen Blick siehst. Die LED leuchtet wieder dauerhaft, sobald der Controller angeschlossen wird oder sein gemeldeter Stand wieder steigt. Das Blinken kommt auch, wenn `disable_pico_led` gesetzt ist — die Warnung gilt als kritisch und hat Vorrang vor der ausgeschalteten LED; die LED kehrt in ihren deaktivierten Zustand (aus) zurück, sobald sich der Akku erholt oder der Controller lädt.

Abschalten lässt es sich beim Build mit `-DENABLE_BATT_LED=OFF`. Standard ist ON.

## DualSense-Mikrofon über Bluetooth

**Das integrierte Mikrofon des DualSense funktioniert über die Bluetooth-Kopplung mit dem Pico** (seit v0.6.8). Der Controller streamt sein Mikrofon als Opus-Audio; der Pico dekodiert es und stellt es dem Host als das übliche USB-Aufnahmegerät des DualSense bereit, sodass jede App (Discord, OBS, Sprachchat im Spiel) es wie ein normales Mikrofon nutzen kann.

Das galt lange als unmöglich — frühere Versionen dieses Forks beschrieben es als harte Grenze der Sony-Firmware (und der Linux-Kerneltreiber `hid-playstation` unterstützt es bis heute nicht). Am Ende hing es an einem einzigen Freigabe-Bit im ausgehenden Audio-Report des Picos. **Der Dank dafür gebührt ganz [awalol](https://github.com/awalol/DS5Dongle) (Upstream), der es gefunden hat.** Das korrigierte Untersuchungsprotokoll steht in [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md).

**So nutzt du es:**

- **Standardmäßig an.** Kopple den Controller, und das Mikrofon beginnt innerhalb von ein, zwei Sekunden zu streamen — kein Spiel-Audio nötig (der Pico hält den Stream selbst am Leben).
- **Erhöhe die Aufnahmelautstärke am Host** — sie ist standardmäßig niedrig. Unter Linux findest du die Karte mit `arecord -l`, dann z. B. `amixer -c <DualSense card> sset 'Headset' 90%`. Prüfe die Aufnahme mit `scripts/mic_diag.sh capture`.
- **Schalte es aus, um den Akku des Controllers zu schonen** — OLED **Settings → BT Mic** oder der Schalter **Integriertes Mikrofon** in der [Konfigurations-App](#️-erste-schritte-die-app-herunterladen). Ein immer aktives Mikrofon hält das Audiosystem des DS5 wach und leert seinen Akku spürbar schneller; schalte es also ab, wenn du keinen Sprachchat nutzt.

**Einschränkungen:**

- Mikrofon-Audio ist **mono** (mono dekodiert, auf beide Kanäle des Stereo-Aufnahme-Endpunkts dupliziert).
- Ausschalten während einer Sitzung stoppt die Übertragung zum Host sofort, aber der Controller streamt weiter, bis er sich das nächste Mal neu verbindet (ein „Stopp“-Befehl ist nicht bekannt); eine neue Verbindung mit ausgeschaltetem Schalter aktiviert es nie.
- Der Zähler `Mic in:` auf dem OLED-Bildschirm **Diagnose** zeigt ~100/s, während das Mikrofon streamt — eine schnelle Kontrolle, ob es läuft.
- **Verschleierung von Paketverlusten:** Verlorene Mikrofon-Frames (schwache Bluetooth-Verbindung, Entfernung, Störungen) werden mit Opus PLC verschleiert, sodass die Stimme durchgehend bleibt statt zu knacken oder abzubrechen, um den Preis einer kleinen Latenz im Jitter-Puffer (~30 ms). Der Zähler `Mic PLC:` auf dem Diagnose-Bildschirm steigt nur, wenn Frames verschleiert werden — praktisch eine Live-Anzeige der Verbindungsqualität.

## Bekannte Probleme

- HD-Haptik löst unter Linux + Steam nicht in jedem Spiel aus; das liegt am Spiel (manche Titel senden HD-Haptik-Audio nur über Windows-spezifische APIs). Getestet funktionierend in Spider-Man Remastered; nicht geliefert in Ghost of Tsushima — gleiche Firmware, gleicher Controller.
- **USB-3.0-Ports können die Kopplung stören** — der Controller bleibt eventuell bei dauerhaft gelb/orange leuchtender Leuchtleiste hängen und verbindet sich nie, während derselbe Pico an einem USB-2.0-Port problemlos funktioniert. Das sind Funkstörungen, kein Firmware-Fehler; siehe [USB-3.0-Ports & Bluetooth-Störungen](#usb-30-ports--bluetooth-störungen) unten. (Seit v0.6.8 versucht die Firmware eine hängende Verbindung automatisch erneut, statt stehen zu bleiben; das rettet viele — aber nicht alle — Grenzfälle.)
- **Windows: Die App kann den Pico nicht öffnen** auf PCs, an denen vorher ein echter DualSense per USB angeschlossen war (der Pico teilt dessen USB-IDs, und Windows hat sich für sie „kein Treiber nötig“ gemerkt). Ab App 0.9.2 bietet der Einrichtungsbildschirm **USB-Treiber reparieren** an: eine Administrator-Abfrage, dann neu einstecken. Das vollständige PowerShell-Skript, das dabei läuft, steht Zeile für Zeile in der [README der App](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web#repair-usb-driver-exactly-what-it-runs).

## USB-3.0-Ports & Bluetooth-Störungen

Wenn der Pico an einem Port funktioniert und an einem anderen nicht, **probier zuerst einen USB-2.0-Port.** USB-3.0-Ports und (vor allem) USB-3.0-Verlängerungskabel strahlen breitbandiges Funkrauschen um 2,4 GHz ab — genau das Band, in dem das Bluetooth-Funkmodul des Picos mit dem Controller spricht. Das ist ein gut dokumentiertes Problem der Branche (Intel, *"USB 3.0 Radio Frequency Interference Impact on 2.4 GHz Wireless Devices"*), nicht speziell dieser Firmware. Das Rauschen macht den Bluetooth-Empfänger des Picos unempfindlicher, sodass der Controller zwar zu verbinden beginnt (gelb/orange Leuchtleiste), die Verbindung aber zu verrauscht ist, um zustande zu kommen — er bleibt auf Gelb hängen.

Abhilfen, ungefähr nach Wirksamkeit sortiert:

1. **Steck den Pico in einen USB-2.0-Port** (oft die einfachste Lösung — viele Mainboards/Gehäuse haben beides).
2. **Nimm ein kurzes USB-2.0-Verlängerungskabel**, um den Pico ein paar Zentimeter von den USB-3.0-Ports / dem Metallgehäuse wegzubringen und die Sichtverbindung zum Controller zu verbessern. Meide ausdrücklich USB-3.0-Verlängerungskabel.
3. **Nutze einen USB-2.0-Hub mit eigener Stromversorgung** am USB-3.0-Port — der Hub schaltet die Verbindung herunter und schafft Abstand.
4. **Klemm einen Ferritkern** in der Nähe des Picos ans Kabel.
5. **Halte den Controller beim Koppeln näher am Pico / in Sichtlinie.**

Die Firmware versucht eine hängende Verbindung von selbst immer wieder; lässt du den Pico nach dem Gelbwerden der Leuchtleiste ~10–20 s eingesteckt, erholt er sich vielleicht ohne neues Einstecken.

## Leistung / Takt

**Dafür musst du nichts tun.** Der Pico 2 W läuft mit denselben Einstellungen wie Upstream awalol/DS5Dongle v0.7.0:

- **Takt: 150 MHz**, der Nenntakt des RP2350 (mit dem er startet)
- **Kernspannung: 1,10 V**, der Standard. Die Firmware setzt sie beim Start, weil der RP2350 die Spannung einer älteren Firmware behält, bis er vom Strom getrennt wird.
- **Bus des Bluetooth-Chips (CYW43 PIO SPI): Taktteiler 2**
- **Flash: 75 MHz** (Teiler 2), von der Firmware selbst beim Start eingerichtet (`PICO_EMBED_XIP_SETUP`). Die Einstellung des Bootroms beizubehalten ließ core1 auf manchen Boards abstürzen ([Issue #15](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/15)).

Ältere Releases übertakteten auf 320 MHz bei 1,20 V, eine von Upstream geerbte Einstellung. Upstream ging auf 150 MHz zurück, sobald der Audiopfad aus dem RAM lief, und dieser Fork tut das jetzt auch, da der Audiopfad und der heiße Bluetooth/USB-Pfad von core0 beide aus dem RAM laufen. 1,20 V liegt über der maximalen Betriebsspannung des RP2350 von 1,16 V, und bei 150 MHz ist kein Kühlkörper nötig.

Für ein Release kann ein selbst kompilierter Build die alte Übertaktung noch als Rückfall nutzen: `-DDS5_CLOCK_KHZ=320000` (1,20 V, Bluetooth-Busteiler 5, Flash-Teiler 4). Der Build verweigert eine Pico-2-W-Konfiguration ohne das eingebettete Flash-Setup oder mit einem Flash über 100 MHz. Der Pico-W-Build bleibt unverändert (200 MHz).

## Build-Anleitung

So baust du das Projekt aus dem Quellcode:

1. Installiere Pico SDK **2.3.0**. Der Build nutzt `pico_sdk_import.cmake`. CI und Releases bauen mit ARMs offizieller GNU-Toolchain **15.2.Rel1** (`arm-none-eabi`); das `gcc-arm-none-eabi` einer Distribution funktioniert meist, aber 15.2.Rel1 ergibt dieselbe Binärdatei wie das Release. Die genauen Versionen (SDK, TinyUSB-Commit, Toolchain-Download) stehen in `tools/toolchain.env`. Setze `PICO_TOOLCHAIN_PATH` auf den entpackten Toolchain-Ordner, falls er nicht in deinem `PATH` liegt.
2. **Setze TinyUSB im Pico SDK (`$PICO_SDK_PATH/lib/tinyusb`) auf den Commit aus `tools/toolchain.env`**: TinyUSB 0.21 plus spätere Fixes, wie Upstream es nutzt. Pico SDK 2.3.0 bringt noch 0.18.0 mit, dem die 4-Argument-Form von `TUD_AUDIO_EP_SIZE` fehlt, die `tusb_config.h` dieses Projekts nutzt, und der Build verweigert jedes TinyUSB außer 0.21:
   ```bash
   cd "$PICO_SDK_PATH/lib/tinyusb"
   git fetch --depth 1 origin 2d56dc533e45e4e91b15e93fdab5e22e964f328d
   git checkout --detach FETCH_HEAD
   ```
3. Konfiguriere und baue mit CMake + Ninja (oder Make):
   ```bash
   cd /path/to/DS5Dongle
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
   cmake --build build --target ds5-bridge
   ```
4. Die UF2 landet unter `build/ds5-bridge-oled.uf2`. Flashe wie gewohnt mit BOOTSEL.

Wissenswerte Build-Flags:

- `-DENABLE_BATT_LED=ON` (Standard) — Pico-LED blinkt bei schwachem DS5-Akku.
- `-DENABLE_SERIAL=ON` — printf zum Debuggen über USB CDC ausgeben (Standard OFF; Produktions-Builds geben den UART frei).
- `-DPICO_W_BUILD=ON` — für den ursprünglichen Pico W bauen (ohne Audio, niedrigerer Takt). Standardziel ist der Pico 2 W.

## Diagnose- & Debug-Werkzeuge

Zwei Wege, Probleme der Bridge einzugrenzen — am Gerät über den OLED-Bildschirm Diagnose und am Host über `scripts/mic_diag.sh` (Linux). Der Weg über den Host ist schneller: kein Umschalten der Bildschirme, kein Flash-Zyklus, läuft, während der Controller in Benutzung ist.

```
# Einmalige Momentaufnahme — ist der Pico am USB? Hat ALSA ihn erkannt? Läuft
# der Aufnahme-Stream? Ist gerade ein Controller gekoppelt?
scripts/mic_diag.sh status

# 3 Sekunden arecord am Mikrofon-IN-Endpunkt — meldet Spitze / RMS / Anzahl
# der Nicht-Null-Werte, um "Stream ist still" von "Stream liefert Audio" zu unterscheiden.
scripts/mic_diag.sh capture 3

# Wie `status`, aber in einer Schleife; gibt nur bei Zustandswechsel aus. Nützlich,
# um genau die Sekunde zu erwischen, in der die Kopplung fertig wird oder Audio-Streams öffnen / schließen.
scripts/mic_diag.sh watch

# Live-Auslesen des Hersteller-Feature-Reports 0xFD der Firmware (über /dev/hidraw):
# Bluetooth-Eingangszähler + Raten, zuletzt gesehene IDs außer 0x31, Byte-Präfixe UND die
# Zähler des Trigger-Flusses (Host-0x02 empfangen / mit gesetztem AllowTriggerFFB /
# an Bluetooth weitergeleitet). bt-trace gibt ein Urteil aus — "Host-Treiber sendet die
# Trigger-Allow-Bits nicht" oder "weitergeleitet, aber der Controller hat nicht reagiert" —
# wofür man sonst einen USB-Protokollanalysator bräuchte.
scripts/mic_diag.sh bt-trace
```

Ursprünglich geschrieben, um die zurückgestellte Untersuchung des DS5-Bluetooth-Mikrofons einzugrenzen (siehe [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)). Der Feature-Report `0xFD` und der Decoder `bt-trace` enthalten jetzt auch die Zähler des Trigger-Flusses, die für [Issue #3](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/3) (fehlender Widerstand der adaptiven Trigger in manchen Spielen) hinzugekommen sind.

## OLED-Display-Add-on (optional)

Steckst du ein [Waveshare Pico-OLED-1.3](#hardware) auf die Stiftleisten des Pico2W, steuert die Firmware es automatisch als Live-Statusdisplay an. Keine Konfiguration nötig — ohne OLED tut die Firmware einfach nichts.

### Startbildschirm (1,5 s nach dem Einschalten)

Die Firmware-Version mittig auf leerem Bildschirm für 1,5 Sekunden, dann geht es zum Bildschirm Status.

### Elf Bildschirme, durchgeschaltet mit KEY0 am Add-on

Reihenfolge: **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → CPU/Clock → BT Signal → VU Meters → Settings →** und wieder von vorn. **KEY0 kurz drücken schaltet vorwärts, KEY1 kurz drücken rückwärts** — auf *jedem* Bildschirm. Aktionen auf einzelnen Bildschirmen (Trigger-Vorlage wechseln, Modus der Leuchtleiste wechseln, Cursor in Settings bewegen, Plätze wechseln) liegen auf den **Tasten des DualSense-Controllers**, nie auf KEY0/KEY1; die zwei physischen Tasten des OLED-Add-ons bedeuten also immer dasselbe.

Jeder Bildschirm zeichnet außerdem **`>`** an den linken oberen Rand (neben KEY0) und **`<`** an den linken unteren Rand (neben KEY1), sodass die Beschriftungen auf dem Bildschirm räumlich zu den Tasten passen.

#### 1. Status

Verbindungsstatus, BD-Adresse des gekoppelten DualSense, Akku in % mit Balken (`+` lädt / `*` voll / `!` Fehler), Live-Positionen der analogen Sticks (jedes Stick-Feld blinkt invertiert — ein schwarzer Punkt auf weißem Feld —, solange **L3 / R3** gedrückt ist), Richtungstasten, Aktionstasten (△ ◯ ✕ □), L1/R1 und Füllbalken der analogen Trigger L2/R2. Verbindungsanzeige und Akku nutzen kleine Pixel-Symbole. Beim Laden erscheint neben dem Akku eine Schätzung `~Nm` der Zeit bis 100 %.

<img src="./assets/oled/oled_sc01.jpg" alt="Bildschirm Status auf dem OLED" width="420">

#### 2. Plätze

Dauerhafte Kopplung von bis zu 4 Controllern. Gespeicherte Controller durchsehen, aktiven Platz wechseln oder einen einzelnen Platz löschen. `>` ist der Cursor, `*` markiert den gerade aktiven Platz.

<img src="./assets/oled/oled_sc02.jpg" alt="Bildschirm Slots auf dem OLED" width="420">

- **Richtungstasten ▲▼** — Cursor über die Plätze 0–3 bewegen
- **△** — zum Platz unter dem Cursor wechseln (aktuellen trennen, mit dem gespeicherten Controller des Platzes neu verbinden)
- **□ 1,5 s halten** — Platz unter dem Cursor löschen (verwirft bd_addr + BTstack-Link-Key)
- Der aktive Platz wird gespeichert; der Pico verbindet sich beim nächsten Start wieder damit

#### 3. Farbwähler für die Leuchtleiste

Neig den Controller entlang jeder Achse, um R / G / B einzustellen; die Firmware sendet die resultierende Farbe mit 10 Hz an die echte Leuchtleiste des DualSense — die Leuchtleiste IST also die Vorschau (das OLED ist einfarbig).

<img src="./assets/oled/oled_sc03.jpg" alt="Farbwähler für die Leuchtleiste auf dem OLED" width="420">

- Drück **△ ◯ ✕ □** am Controller, um die aktuelle Farbe im Favoritenplatz 0 / 1 / 2 / 3 zu **speichern**
- Drück **R1** am Controller, um die Modus-Markierung durchzuschalten: `[LIVE]` → `[FAV0]` → `[FAV1]` → `[FAV2]` → `[FAV3]` → Effekte (Breathing / Rainbow / Fade) → `[HOST]` → zurück zu `[LIVE]`
- `[HOST]` (Standard) überlässt die Leuchtleiste dem PC / Spiel; der Pico setzt keine Farbe. Jeder andere Modus behält die Farbe des Picos, egal was das Spiel sendet
- Standard-Favoriten: Rot, Grün, Blau, Weiß

#### 4. Trigger-Test

Drück **△** am Controller, um sieben Effekte der adaptiven Trigger durchzuschalten, die auf L2 und R2 wirken. Zieh jeden Trigger, um den Effekt zu spüren.

<img src="./assets/oled/oled_sc04.jpg" alt="Bildschirm Trigger Test auf dem OLED" width="420">

Reihenfolge: **Off → Feedback → Weapon → Vibration → Bow → Gallop → Machine Gun → Off …** Die Effektparameter sind nach dem Reverse Engineering von [dualsensectl](https://github.com/nowrep/dualsensectl) bitweise gepackt, alle in maximaler Stärke.

#### 5. Gyro-Neigung

Live-Werte des Beschleunigungsmessers für X/Y/Z mit einem 40×40-Fadenkreuzfeld. Der Punkt folgt der Neigung des Controllers in Echtzeit und **sitzt mittig, wenn der Controller flach liegt** — er nutzt die werkseitige IMU-Kalibrierung des einzelnen Controllers (aus dem Feature-Report `0x05` gelesen), sodass Ruhelage und Verstärkung bei jedem Controller stimmen. Neigen nach links/rechts und vorn/hinten bewegt den Punkt in die passende Richtung.

<img src="./assets/oled/oled_sc05.jpg" alt="Bildschirm Gyro Tilt auf dem OLED" width="420">

#### 6. Touchpad

Live-Darstellung der Touchpad-Fläche. Punkte erscheinen an den aktuellen Fingerpositionen; die Anzahl aktualisiert sich beim Berühren / Loslassen.

<img src="./assets/oled/oled_sc06.jpg" alt="Bildschirm Touchpad auf dem OLED" width="420">

#### 7. Diagnose

Scrollbare Liste von Live-Zählern — Laufzeit, Bluetooth-Status, Trigger-Fluss Host → Bluetooth (`host02` / `trig` / `tx`), Rate der Bluetooth-Eingänge 0x31, USB-Audio-Frames/s, Bluetooth-0x32-Pakete/s und unten die Zähler der zurückgestellten Mikrofon-Untersuchung. Richtungstasten ▲/▼ am Controller scrollen; kleine Zeichen `^` / `v` am rechten Rand zeigen „mehr oberhalb/unterhalb“. Nur lesend, daher ohne Cursor. Nützlich, um ohne UART-Kabel zu prüfen, dass die Bridge Bytes bewegt.

Dieselben Zähler werden auch über den HID-Feature-Report `0xFD` für Werkzeuge am Host bereitgestellt — siehe `scripts/mic_diag.sh bt-trace` unten.

<img src="./assets/oled/oled_sc07.jpg" alt="Bildschirm Diagnostics auf dem OLED" width="420">

#### 8. CPU / Takt

Live-Werte des RP2350: eingestellter (`Set`) und tatsächlich laufender (`Real`) Systemtakt, gegen den Quarz gemessen, Kernspannung aus dem Spannungsregler zurückgelesen und Chiptemperatur (Mittel aus 256 Messungen + langsamer EMA, damit der Wert der echten Chiptemperatur folgt statt dem ADC-Rauschen).

<img src="./assets/oled/oled_sc08.jpg" alt="Diagnosebildschirm CPU / Clock auf dem OLED" width="420">

Dieselbe Telemetrie wird auch über den HID-Feature-Report `0xFC` für Werkzeuge bereitgestellt.

#### 9. Bluetooth-Signal

Live-Signalstärke der aktiven Bluetooth-Verbindung, in dB relativ zum idealen Bereich (0 = ideal, angezeigt als *in range*; negativer ist schwächer), mit einem Balken von −30 dB (leer) bis 0 dB (voll) und der Einstufung der App (Excellent / Good / Fair / Weak, also Sehr gut / Gut / Mittel / Schwach). Vor dem ersten Messwert oder wenn eine Messung fehlschlägt, steht dort `Signal: --` und `Link: unknown`.

<img src="./assets/oled/oled_sc09.jpg" alt="Bildschirm BT Signal auf dem OLED" width="420">

#### 10. VU-Meter

Live-Spitzenwertanzeigen für den Lautsprecher- und den Haptik-Audiopfad. Nützlich, um das Audio-Routing zu prüfen, ohne dass der Controller an einem Host steckt.

<img src="./assets/oled/oled_sc10.jpg" alt="Bildschirm VU Meters auf dem OLED" width="420">

#### 11. Einstellungen

Editor für die dauerhafte Konfiguration. Richtungstasten ▲▼ bewegen die Auswahl, ▶◀ ändern Werte, △ speichert in den Flash. Enthält die Konfigurationsfelder der Firmware (Haptik-Verstärkung, Lautsprecherlautstärke, Inaktivitäts-Timeout, Abfragerate), die Regler für Audio Auto Haptics und zwei Aktionen, die durch Halten bestätigt werden:

`Hap Left` und `Hap Right` stellen jeden Griff einzeln ein. Drücke △, um die Änderungen zu übernehmen und zu speichern.

<img src="./assets/oled/oled_sc11.jpg" alt="Bildschirm Settings auf dem OLED" width="420">

- **AutoHap Off / Fallback / Mix / Replace** — wählt den Modus von Audio Auto Haptics. Standard `Off`: Haptik kommt nur vom Spiel. `Fallback` erzeugt abgeleitetes Rumble nur, wenn das Spiel keine nativen Haptikdaten sendet (z. B. Ghost of Tsushima unter Linux) — es vibriert dann allerdings auch bei Musik und Desktop-Audio mit; Spiele, die *doch* native Haptik senden (Spider-Man Remastered), laufen unverändert durch. `Mix` legt das abgeleitete Signal über das native, `Replace` ignoriert das native ganz.
- **AH Gain N%** — Verstärkung des abgeleiteten Signals, 0–200 % in Schritten von 10 %. Standard 100 %.
- **AH LP 80/160/250/400 Hz** — Grenzfrequenz des Tiefpasses auf dem Lautsprecher-Audio vor der Hüllkurvenverfolgung. Niedriger = mehr Tiefbass, höher = mehr Präsenz. Standard 160 Hz.
- **TrigEff game / -1 … -10** — Intensität des Trigger-Effekts: schwächt den Widerstand der adaptiven Trigger in Spielen in 10 Stufen ab. Standard `game` (0): die Stärke des Spiels, sein Report wird unverändert durchgereicht.
- **SpkBoost game / 1 … 7** — Lautsprecher-Verstärkung: die Vorverstärkung des Controller-Verstärkers. Standard `game` (0): dem Spiel überlassen. Getrennt von `Spk Vol`.
- **VolLock on / off** — Lautstärke sperren: Spiele können Lautstärke, Stummschaltung und Mute-Leuchte von Lautsprecher, Headset und Mikrofon des Controllers nicht ändern; Lautstärke und Stummschaltung des PCs wirken weiterhin. Standard off.
- **SPK Out Auto / SPK / Hset / off** — Audioausgabe: wo das Audio des PCs spielt (Auto = das Headset, wenn eins am Controller steckt). Standard Auto.
- **Mic In Auto / Int / Hset** — Verwendetes Mikrofon: Auto, das integrierte Mikrofon oder das eines Headsets. Standard Auto. `BT Mic` bleibt der Schalter, der das Mikrofon ausschaltet.
- **Sleep CtrlOff on / off** — den Controller ausschalten, sobald der PC 3 s geschlafen hat. Standard on.
- **PS Wakes PC on / off** — ohne verbundenen Controller meldet sich der Pico als Tastatur nur zum Aufwecken („DS5 Dongle Wake Keyboard“), damit die PS-Taste den PC aufwecken kann. Standard off.
- **Reset to defaults** — △ 2 s halten, um alle Konfigurationsfelder zurückzusetzen
- **Wipe all slots** — △ 2 s halten, um alle 4 gekoppelten Controller + alle BTstack-Link-Keys zu verwerfen

### Tastenübersicht

Die zwei physischen Tasten des OLED-Add-ons dienen **ausschließlich der Navigation**:

| Taste | Aktion |
|---|---|
| **KEY0** kurz drücken | Nächster Bildschirm (vorwärts) |
| **KEY1** kurz drücken | Vorheriger Bildschirm (rückwärts) |
| **KEY1** länger als 1,5 s gedrückt | Helligkeit wechselt einmal, noch während gedrückt; Loslassen geht nicht zurück |
| **KEY0 + KEY1** gemeinsam ≥ 1 s halten | `watchdog_reboot` — Soft-Reboot ohne USB-Abziehen |

Zustandsänderungen auf einzelnen Bildschirmen (Trigger-Vorlage wechseln, Modus der Leuchtleiste wechseln, in Settings navigieren, Plätze wechseln, Farben in Favoritenplätzen speichern) passieren alle über die **Tasten des DualSense-Controllers** — nie über KEY0 / KEY1 —, sodass die zwei physischen Tasten auf jedem Bildschirm immer dasselbe bedeuten. Welche Controller-Taste was tut, steht oben im Abschnitt des jeweiligen Bildschirms.

### Pinbelegung (Standard-Layout des Waveshare Pico HAT)

| Funktion | GPIO |
|---|---|
| MOSI | 11 |
| SCK  | 10 |
| CS   | 9  |
| DC   | 8  |
| RST  | 12 |
| KEY0 | 15 |
| KEY1 | 17 |

### Wiederherstellung per Soft-Reboot

Zwei Wege, den Pico ohne USB-Abziehen neu zu starten — praktisch, wenn die Kopplung hängt oder du einen sauberen Zustand willst:

- **KEY0 + KEY1 am OLED gleichzeitig ≥ 1 s halten** → `watchdog_reboot`. Ersetzt die ältere Geste „KEY0 doppelt klicken“ früherer Versionen, weil schnelles Vorwärtsblättern den Doppelklick-Timer immer wieder versehentlich auslöste.
- **`PS + Mute` am DualSense 2 Sekunden halten** → `watchdog_reboot` (funktioniert mit und ohne OLED — Reserve ohne Display).

## Danksagungen

Einige Funktionen und Designideen dieses Forks stammen aus anderen Forks von Upstream, mit Dank:

- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)** — Pixel-Art-Symbole in der Statuszeile des OLEDs (visueller Ansatz), das Bedienmuster „Halten zum Zurücksetzen auf Werkseinstellungen“, das der Eintrag „Reset to defaults“ im Bildschirm Settings nutzt (△ 2 s halten zum Bestätigen), und das dauerhafte Bluetooth-Kopplungssystem mit mehreren Plätzen auf dem neuen Bildschirm Slots (4 gekoppelte Controller, Richtungstasten zum Navigieren, △ zum Wechseln, □ halten zum Löschen eines Platzes, dazu „Wipe all slots“ im Menü Settings).
- **[loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)** — unabhängige Wiederentdeckung der Regression in Upstream `3a31bd7`, die die Ausgabe von Lautsprecher / HD-Haptik kaputt machte (Commit `c7a8d3c`); der Fix in unserem `src/audio.cpp` stellt denselben SetStateData-Unterreport wieder her. Außerdem die Quelle der DSP für Audio Auto Haptics (1-Pol-Tiefpass + Hüllkurvenfolger, siehe Settings → Auto Haptics) und des Fixes „die UAC1-Lautstärke der USB-Seite nicht in die dauerhafte Konfiguration übernehmen“.
- **[SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge)** — eine verwandte DualSense-Bridge auf dem Pico. Die Logik zum Anwenden der Tastenumbelegung und die Menge der digitalen Bedienelemente in `src/remap.{h,cpp}` sind von dort portiert. Ihre Audio-Architektur (Opus-Kodierung auf dem Gerät vs. optionale Begleit-App, die auf dem Host kodiert) war auch die Referenz bei der Diagnose unserer Arbeit an der Lautsprecher-Kadenz („Warble“). Danke für die Vorarbeit.
- **[awalol/ds5dongle-config-web](https://github.com/awalol/ds5dongle-config-web)** — Basis unserer geforkten Web-Konfigurations-App unter [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web). Der Fork passt Upstreams 13-Byte-Layout von Config_body an unser 19-Byte-Layout an und ergänzt die Oberfläche für unsere Zusätze (Kopplung mit mehreren Plätzen, Auto Haptics).
- **[PS5 Button Icons and Controls](https://zacksly.itch.io/ps5-button-icons-and-controls)** von **Zacksly**: der DualSense-Controller-Umriss und die Tastensymbole auf der Seite „Tastenzuweisung“ des Konfigurators (Desktop-App und Web-Tool), lizenziert unter [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) (auf `currentColor` umgefärbt und für die Themes zugeschnitten).

## Roadmap
- Schau dir den [DS5Dongle plan](https://github.com/users/awalol/projects/5) an

## Community
- Tritt dem Discord-Server bei: [Discord Server](https://discord.gg/hM4ntchGCa)
- Wenn du einen Fehler gefunden hast, eröffne bitte stattdessen ein Issue.

## Quellen

- [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense) — Inspiration für das Projekt
- [egormanga/SAxense](https://github.com/egormanga/SAxense) — Proof of Concept für Bluetooth-Haptik
- [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense) - Dokumentation der Struktur der DualSense-Datenreports
- [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX) — Paket des Lautsprecher-Reports

---

*Diese Übersetzung folgt der englischen [README.md](README.md); die englische Fassung ist maßgeblich, die Übersetzung kann hinterherhinken.*
