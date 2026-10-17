# Pico2W DualSense 5 Bridge — OLED Edition

[English](README.md) · [中文](README.CN.md) · [Deutsch](README.DE.md) · **Español** · [Français](README.FR.md) · [日本語](README.JA.md) · [Português (Brasil)](README.PT-BR.md)

> Convierte una Raspberry Pi Pico2W en un adaptador inalámbrico para el mando DualSense (DS5), con una pantalla de estado opcional sobre la placa.

> **OLED Edition** es un fork de **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** (upstream) que añade un módulo de pantalla opcional Pico-OLED-1.3 de 128×64 con 11 pantallas (estado, emparejamiento de varios mandos en 4 ranuras, selector de color de la barra luminosa con favoritos y efectos predefinidos, prueba de gatillos, inclinación del giroscopio, panel táctil, diagnóstico, CPU/reloj, intensidad de la señal BT, vúmetros de audio y un menú de ajustes persistente), además de un reinicio por software con una combinación de botones del DS5. Upstream es la fuente de referencia del firmware del puente; este fork lo sigue y añade funciones encima.

> **🧪 ¿Quieres las funciones más nuevas? Prueba el canal de desarrollo.** En la app: **Ajustes de la app → Actualizaciones → Desarrollo** (antes te pregunta; **Volver a estable** te devuelve cuando quieras). Las versiones de desarrollo pasan las mismas pruebas automáticas en cada cambio; las estables además pasan todas las comprobaciones de lanzamiento en hardware real y en Windows, y un tiempo de uso diario. La actual, **0.9.5-dev.15**, añade respecto a la estable 0.9.4:
>
> - **DualSense Edge:** reasigna el botón Fn y los botones posteriores, perfiles personalizados del Edge en PlayStation Accessories, y su propio dibujo y vista en la OLED
> - **Joysticks:** curvas de respuesta y pruebas de joystick
> - **Micrófono del mando:** control de nivel, cancelación de eco, y la luz del botón de silenciado sigue al silencio; el silencio y el volumen del micrófono del PC llegan al mando
> - **Suspensión y reactivación:** el mando se apaga cuando el PC entra en suspensión, y el botón PS puede reactivar el PC
> - **Juegos (Linux):** elige cualquier versión de Proton por juego, configuración de HDR, y los juegos solo XInput encuentran el mando
> - **Maneja la OLED desde la app:** KEY0/KEY1 y los botones del mando junto a la pantalla en directo, además de **Compartir la OLED** (PNG, portapapeles o GIF)
> - **Comprobación del firmware del mando** e información sobre su estado
> - **Auriculares (Linux):** estéreo al conectar unos auriculares al mando, y de vuelta al altavoz al quitarlos
> - **Vincular desde la placa:** la OLED dice *Press PS to connect*; mantén KEY0 2 s para vincular, con cuenta atrás; sin búsquedas sin fin en reposo
> - **Todos los mensajes en 7 idiomas**

---

## 🛠️ Primeros pasos: descarga la app

**[→ Descargar DS5 Dongle OLED Edition Configurator](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest)** (AppImage para Linux y un instalador para Windows que no necesita permisos de administrador, ambos probados; la versión para macOS la ha probado la comunidad: [funciona en macOS 27.0.1](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/17))

La app es todo lo que necesitas. Cada versión incluye el firmware correspondiente, así que la app lo instala en tu Pico, lo mantiene al día y lo configura todo:

1. Monta tu placa (consulta [Hardware](#hardware) más abajo) y abre la app.
2. Conecta la Pico **manteniendo pulsado su botón BOOTSEL** (una Pico nueva no necesita el botón). La app la encuentra e instala el firmware con un clic; no hay que copiar nada a mano.
3. Empareja tu mando: mantén pulsados **PS + Create** hasta que la barra luminosa parpadee. La app te muestra cuándo está conectado.

![La app: tu mando en directo, con sus propios colores](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/live-controller.png)

Tu mando se dibuja con sus propios colores y se mueve mientras lo usas; haz clic en cualquier parte para configurarla. La **[guía paso a paso](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/GUIDE.md)** (en inglés) lo explica todo, desde montar la placa hasta cada ajuste:

- **El mando:** modos y favoritos de la barra luminosa, efectos de gatillos adaptativos, **retroalimentación háptica con intensidad de la vibración por lado** (retroalimentación háptica y vibración clásica, con una prueba de 3 segundos), Audio Auto Haptics, altavoz y micrófono integrado (con pruebas), asignaciones de botones (todos los botones en una lista, también los botones Fn y los botones posteriores del DualSense Edge, y **distribuciones de botones guardadas** que un juego puede activar mientras se ejecuta, sin escribirse nunca en la flash: firmware v0.9.4), las cuatro ranuras de emparejamiento e **Info del mando** (el firmware del propio mando, comparado con el último de Sony, el voltaje de su batería y sus contadores de vida útil).
- **Calibración:** deriva de los joysticks, joysticks o gatillos que no llegan al máximo, un giroscopio que se desplaza poco a poco, corregidos en la placa (por ranura de emparejamiento) para todos los juegos; no se escribe nada en el mando. Además, una **curva de sensibilidad** por joystick y pruebas de joystick (circularidad y deriva).
- **La placa:** su pantalla OLED reflejada en directo, un panel de diagnóstico (temperatura, reloj, tráfico Bluetooth y USB, la cadena de audio y cuánto tarda cada parte del firmware), ajustes de energía, pantalla y USB, y **actualizaciones de firmware con un clic** (sin botón BOOTSEL ni copiar a ninguna unidad).
- **Juegos:** tu biblioteca de Steam, Heroic y Lutris (Windows: Steam, GOG Galaxy y Epic) como lista y como cuadrícula de portadas, con perfiles de mando por juego (cada ajuste del mando que cambia el juego, incluida una distribución de botones y curvas de joystick, editados en directo en la vista del mando), compatibilidad con DualSense por juego y, en Linux, correcciones de configuración y la versión de Proton para DualSense.
- **A tu manera:** un tema con los colores de tu mando, Cristal, controles deslizantes de aspecto, modos de visión del color, Reducir movimiento, un narrador por el altavoz del mando, 7 idiomas y un icono en la bandeja.

La app se comunica con el firmware **v0.8 o posterior** a través de una pequeña interfaz USB de configuración (la calibración y la háptica por lado necesitan **v0.9**; la app ofrece la actualización). Los juegos no la ven, así que la placa sigue pareciendo exactamente un DualSense real. Código de la app: **[MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web)**.

> **La herramienta de configuración en el navegador se ha retirado**: la app de escritorio hace todo lo que hacía y más. Su antigua dirección, [marcelinevpq.github.io/DS5Dongle-OLED-Config-Web](https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/), es ahora la página de descarga de la app. Para flashear una Pico sin la app, mantén pulsado BOOTSEL al conectarla y copia el `.uf2` de la versión en la unidad que aparece.

## 🔄 Actualizaciones e instalación del firmware, sin complicaciones

**La app se mantiene al día sola.**

- **Firmadas y comprobadas.** Cada actualización viene de un único feed firmado con nuestra clave Ed25519. La app ignora un feed sin una firma válida, comprueba el tamaño y el SHA-256 de cada descarga antes de instalarla y nunca instala por sí sola una versión más antigua.
- **Dos canales:** **Estable** para todos, **Desarrollo** para las funciones más nuevas. **Volver a estable** devuelve la app y el firmware a la última versión estable.
- **Tú decides:** con **Instalar actualizaciones automáticamente** (la app lo pregunta una vez), las actualizaciones se descargan y se comprueban en segundo plano, y la nueva versión arranca la próxima vez que abres la app. Con **Buscar actualizaciones automáticamente** desactivado, la app solo se conecta cuando pulsas un botón de comprobar.
- **Según el sistema:** en Linux, el AppImage deja la nueva versión a su lado y la entrada del menú la sigue. En Windows, el instalador por usuario funciona sin permisos de administrador. En macOS, la app muestra la actualización con un enlace a la versión publicada.

**El firmware se instala con un clic.**

- **Incluido:** cada versión de la app lleva el firmware con el que se compiló y probó, así que una placa nueva se puede preparar sin conexión. El firmware más nuevo llega por el mismo feed firmado.
- **Primera configuración:** conecta la Pico manteniendo pulsado BOOTSEL (una Pico nueva no necesita el botón). La app la encuentra e instala el firmware.
- **Actualizaciones:** sin botón y sin copiar a una unidad. La app reinicia la placa en BOOTSEL ella misma, escribe el firmware por USB y vuelve a conectar.
- **Comprobado antes de escribir:** el SHA-256 del archivo debe coincidir con el feed y debe ser firmware para este chip. Un archivo que falla no escribe nada.
- **Comprobado después de escribir:** la app espera a que arranque el nuevo firmware. Si la placa vuelve en BOOTSEL, la app lo dice y ofrece **Instalar de nuevo**. Mientras se escribe el firmware, la app no se reinicia, no se cierra y no se actualiza.
- **Tu configuración se queda:** los ajustes, los emparejamientos, la calibración y la asignación de botones están en su propia parte de la memoria flash. Cada compilación del firmware se comprueba para que no llegue a esa parte, y una prueba de la app comprueba que una actualización solo escribe los sectores del archivo de firmware.

## 🔬 Cómo se construye y se prueba

Cada cambio pasa todas las comprobaciones automáticas. Cada versión añade más. La lista completa, con los archivos detrás de cada comprobación: [docs/QUALITY.md](docs/QUALITY.md) (en inglés).

**Cada cambio**

- **App:** más de 1600 pruebas (unitarias, de integración y de seguridad) en Linux, Windows y macOS, además de comprobación de tipos, ESLint y Prettier.
- **Firmware:** 185 pruebas en el PC de la lógica del firmware con AddressSanitizer y UndefinedBehaviorSanitizer, y cinco compilaciones del firmware (Pico 2 W, Pico W, depuración y dos variantes). La compilación falla si el código crítico en tiempo sale de la RAM o cambia la configuración de la flash.
- **Reglas de la app:** los ajustes de seguridad de Electron, nada de HTML inseguro, un único sistema de temas sin colores sueltos, nada de código ni archivos duplicados sin veredicto.
- **Veredictos por escrito:** cada bucle que espera o corre en una ruta crítica, cada ruta de error, cada caché y cada recurso que toma el código (un temporizador, un oyente, un dispositivo) lleva un comentario con su veredicto. Uno nuevo sin veredicto hace fallar la comprobación.
- **Análisis de seguridad:** Semgrep, Gitleaks y npm audit, también cada semana.

**Cada versión de desarrollo**

- **Comprobador de la interfaz:** su propia copia de la app con una placa simulada abre cada página en los 7 idiomas en dos tamaños de ventana. Falla con texto recortado, contraste por debajo de WCAG AA o un control sin nombre.
- **Presupuestos de recursos** medidos en Linux y en una máquina virtual con Windows 11.
- **Prueba en Windows** de la app instalada en una máquina virtual con Windows 11, y una **prueba rápida en macOS** en un Mac real de GitHub Actions (instalada como lo hace un usuario, arrancada y manejada).

**Antes de una versión estable**

- **La comprobación completa de la interfaz:** cada pantalla y panel, los 7 idiomas, ambos tamaños, cada tema y estilo, cada combinación de colores de ambos modelos de mando.
- **La medición larga de presupuestos,** con una prueba de 30 minutos del crecimiento de la memoria, en Linux y Windows.
- **Comprobaciones con hardware** en una placa real con un DualSense: vista en vivo, vibración háptica, barra de luz, asignación de botones, ranuras, gatillos, contadores de audio, reinicio y BOOTSEL, primera configuración, el actualizador, juegos.
- **Uso diario:** la aprobación de la propia desarrolladora tras usar la versión en el día a día.

**Eficiencia**

- **Presupuestos que detienen la versión:** como mucho 10 imágenes por segundo en una página en reposo, menos del 2 % de un núcleo de CPU visible y en reposo, menos del 0,5 % oculta en la bandeja, como mucho 275 MB de memoria y menos de 5 MB de crecimiento en 30 minutos. Un presupuesto que falla se arregla, no se sube. Las últimas cifras: [BUDGETS.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md).
- **Dibujo en la GPU:** los efectos los dibuja la tarjeta gráfica, y no se dibuja nada con la ventana oculta.
- **Firmware:** la ruta de audio corre desde la RAM, y la app muestra cuánto tarda cada parte del firmware.

**Seguridad y confianza**

- **Feed firmado** (Ed25519) y descargas comprobadas con SHA-256, como arriba.
- **Descargas verificables:** cada archivo de una versión tiene una atestación de compilación. `gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition` muestra que lo compiló el flujo de publicación de este repositorio a partir de su etiqueta.
- **App blindada:** ventana aislada (sandbox), Content Security Policy estricta, cada mensaje de la ventana se comprueba por su remitente y se valida, acceso USB limitado a la placa y al cargador de arranque de la Pico. Versiones exactas de las dependencias, elegidas al menos 2 semanas después de publicarse. La auditoría: [SECURITY_AUDIT.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md).
- **Un DualSense real para los juegos:** el descriptor USB de la placa es idéntico byte a byte al de Sony (289 bytes), y la interfaz del configurador va aparte, así que los juegos nunca la ven.

**Accesibilidad e idiomas**

- **7 idiomas, completos:** una prueba falla si a un idioma le falta un texto, y las funciones del mando usan los nombres oficiales de Sony en cada idioma.
- **Legible y con nombre:** el comprobador de la interfaz revisa el contraste y los nombres de los controles en cada idioma; el texto debe caber en cada tamaño.
- **A tu manera:** modos de visión del color, Reducir movimiento y uso con teclado en cada menú desplegable.

---

## Descripción general

Este proyecto permite que la Raspberry Pi Pico2W funcione como puente Bluetooth para el mando DualSense, con conexión inalámbrica y compatibilidad mejorada con la háptica.

## Funciones

**Puente básico (de upstream):**

- Conectividad completa del DualSense a través de la Pico2W
- Háptica HD (retroalimentación de vibración avanzada)
- Puente Bluetooth inalámbrico
- Ganancia háptica ajustable mediante el volumen del micrófono
- LED y comportamiento de desconexión configurables

**Añadidos de OLED Edition:**

- **Gatillos adaptativos nativos del DualSense en juegos de PC con Linux/Proton, a través de la placa.** Los juegos con compatibilidad nativa con DualSense controlan los gatillos adaptativos del mando de forma inalámbrica a través de la placa, **1:1 como con un DualSense conectado por cable**. Necesita un Proton con la corrección de Wine `winebus.sys` #9034 (Wine 11 / Proton-GE actual) y Steam Input desactivado; **no hace falta ninguna opción de lanzamiento** (Wine 11 activa la ruta nativa hidraw de forma predeterminada). Funciona **tanto en Steam como en Heroic** (Epic/GOG). El firmware hace que la placa sea indistinguible byte a byte de un DS5 real para que el juego la acepte (explicación completa en [CHANGELOG.md](./CHANGELOG.md)). Contrapartida: las pestañas Config/Remap de la herramienta del navegador están desactivadas con este firmware (consulta la nota de arriba).
- Pantalla de estado opcional Pico-OLED-1.3 con **11 pantallas** (estado, ranuras, barra luminosa, prueba de gatillos, inclinación del giroscopio, panel táctil, diagnóstico, CPU/reloj, RSSI, vúmetros, ajustes)
- **Calibración en la placa (v0.9)**: desplazamiento del centro del joystick, zona muerta y radio de inclinación máxima, recorrido de los gatillos y cero del giroscopio, por ranura de emparejamiento, aplicados a lo que reciben los juegos (nunca se escriben en el mando). Se ajusta con la calibración guiada de la app.
- **Intensidad de la vibración por lado (v0.9)**: izquierda y derecha por separado, tanto para la retroalimentación háptica como para la vibración clásica.
- **Curvas de sensibilidad (v0.9.5)**: una curva por joystick (Lineal, Preciso, Constante, Rápido, Digital o cuatro puntos personalizados), aplicada en la placa después de la calibración, así que funciona en todos los juegos; el perfil de un juego puede activar la suya mientras se ejecuta.
- **Compatibilidad con DualSense Edge**: el Edge se distingue de un DualSense por el informe de firmware que envía al conectarse; su botón Fn y sus botones posteriores se pueden asignar como cualquier otro botón, y la pantalla Status de la OLED los muestra. Sus perfiles personalizados se muestran y se guardan en la app PlayStation Accessories de Sony (upstream awalol 8a2f576; la app puede necesitar unos segundos después de que el mando se conecte). En modo DS5, la app ya no ofrece una actualización de firmware que falla (upstream awalol ae83907).
- **Info del mando (v0.9.5)**: la app puede leer las versiones del firmware del propio mando, el voltaje de la batería y los contadores de vida útil a través de la placa (solo lectura; el número de serie del mando se borra antes de que la app lo vea).
- **Tiempos por etapa (v0.9.5)**: la placa mide cada parte de su propio trabajo (Bluetooth, USB, audio, entrada, OLED, las tramas de altavoz y micrófono de core1) para la tarjeta Tiempos de la app.
- **Réplica de la pantalla OLED (v0.9)**: la app muestra la pantalla de la placa píxel a píxel, con o sin el módulo OLED.
- **Asignaciones de botones**: reasigna cualquiera de los 16 controles digitales (botones de acción, botones de dirección, L1/R1/L2/R2, pulsación de los joysticks, el botón de crear y el botón de opciones), y el botón Fn y los botones posteriores del DualSense Edge, a cualquier otro. Se guarda en la placa y se aplica antes de que el host vea el informe, así que funciona en **todos los juegos y sistemas operativos sin software en el host**; la identidad (sin reasignación) es lo predeterminado. Edítalo visualmente en la página **Asignaciones de botones** de la [app de configuración](#️-primeros-pasos-descarga-la-app), o sin interfaz con `scripts/remap_test.py`. Se guarda en su propio sector de flash y se conserva tras reiniciar.
- **Suspensión y reactivación del PC (upstream awalol)**: el mando se apaga cuando el PC lleva 3 s en suspensión (activado de forma predeterminada) y, opcionalmente, el botón PS reactiva el PC (desactivado de forma predeterminada: sin ningún mando conectado, la placa aparece entonces como un pequeño teclado solo para reactivar, "DS5 Dongle Wake Keyboard", al que el PC debe permitir reactivarlo). Con un mando conectado, la placa es el DualSense exactamente igual que antes. Ambas opciones están en la app y en el menú Settings de la OLED. Para reactivar, el PC debe mantener la alimentación USB durante la suspensión.
- **Intensidad del efecto gatillo, amplificación del altavoz, bloqueo de volumen, elección de micrófono y altavoz, pin de estado (ajustes de upstream, protocolo 1.13)**: gatillos adaptativos más suaves en 10 pasos (la fuerza propia del juego de forma predeterminada), la preganancia del amplificador del mando, juegos que no pueden cambiar el volumen del mando, el micrófono como Auto / Integrado / Auriculares, el audio del PC en Auto / Altavoz / Auriculares / Desactivado, y un GPIO que muestra la conexión del mando. La luz del botón de silenciado sigue al silencio del micrófono, y los interruptores de silencio y el nivel del micrófono del PC llegan al mando. Una configuración nueva sondea a 500 Hz.
- **Emparejamiento persistente de varios mandos en 4 ranuras**: vincula hasta cuatro DualSense, cambia entre ellos desde la OLED; la ranura 0 se reconecta automáticamente al arrancar
- **Selector de color de la barra luminosa** con 4 ranuras de favoritos + efectos predefinidos de respiración / arcoíris / fundido
- **Menú de ajustes persistente** para los 8 campos de configuración del firmware (ganancia háptica, volumen del altavoz, frecuencia de sondeo, etc.) con las acciones Reset y Wipe-all-slots que se confirman manteniendo pulsado
- **Escalera de ahorro de energía de la OLED en reposo**: ciclo manual de brillo (pulsación larga de KEY1), atenuación profunda automática con un pequeño punto que respira a los 2 min de inactividad, pantalla totalmente apagada a los 15 min. Se reactiva al instante con un botón, al emparejar un mando o con cualquier entrada. Protección real contra el quemado de la pantalla, no solo un ajuste de contraste.
- **Reinicio por software** sin desconectar el USB manteniendo pulsado `PS + Mute` en el DS5 (funciona sin pantalla) o **KEY0 + KEY1 a la vez durante 1 s** en el módulo OLED (sustituye al antiguo gesto de doble clic en KEY0, que se disparaba fácilmente por accidente al pasar páginas rápido)
- **Altavoz y micrófono del mando limpios**: la ruta de audio de core1 (Opus, remuestreador, operaciones de memoria) se ejecuta desde la RAM, el audio sale en los informes empaquetados `0x39` de upstream, y el micrófono se decodifica en core1 y se sincroniza con el reloj USB del host. Esto eliminó el chisporroteo del altavoz y los cortes al activar el micrófono que llevaban tiempo ahí (consulta [CHANGELOG.md](./CHANGELOG.md)). **Mérito de [awalol](https://github.com/awalol/DS5Dongle)**: es el trabajo de audio de upstream, portado aquí.
- **Revisión del puente básico**: corrección crítica de desbordamiento de pila en la ruta de audio (resuelve los antiguos "cortes de audio"), refuerzo de seguridad, watchdog, validación de longitudes en los límites HID/L2CAP (consulta [CHANGELOG.md](./CHANGELOG.md))

## 🐧 Linux: gatillos adaptativos nativos (Proton)

Para que un juego controle los **gatillos adaptativos del mando a través de la placa** en Linux hace falta un Proton que incluya la corrección de Wine `winebus.sys` **#9034**. Las versiones oficiales de **GE-Proton 11** ya la incluyen. La versión que se ofrece aquí, **`GE-Proton11-7-DualSense`** (consulta [su versión publicada](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense)), es el `GE-Proton11-7` oficial más una corrección adicional de `winebus.sys`: el DualSense informa de su número de serie como una MAC Bluetooth separada por dos puntos, lo que hace que su ruta de dispositivo no se pueda abrir en Windows, así que los juegos que usan libScePad de Sony no pueden llegar a los gatillos. Esta versión sustituye esos caracteres para que la ruta se abra. Es la versión con la que se verificaron los resultados de gatillos de abajo.

> Por qué: el error #9034 suprime el dispositivo de mando SDL cuando existe un dispositivo hidraw con el mismo VID/PID, así que el mando se ve *en todas partes excepto en el juego*. La corrección permite que la ruta nativa (capaz de usar los gatillos) y un mando funcional convivan.

**Configuración única**

1. **Instala la versión de Proton.** Descarga `GE-Proton11-7-DualSense.tar.gz` de [la versión publicada](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense) y extráelo en:
   - **Steam:** `~/.steam/root/compatibilitytools.d/`
   - **Heroic:** `~/.config/heroic/tools/proton/`

   Después **reinicia Steam por completo** para que detecte la nueva herramienta de compatibilidad.
2. **Desactiva Steam Input** para que el juego hable con el mando de forma nativa: Steam → **Propiedades** del juego **→ Mando → Desactivar Steam Input** (o de forma global: Parámetros → Mando → desactiva la compatibilidad con mandos de PlayStation).

**Por juego**

3. **Fuerza el Proton:** Steam → clic derecho en el juego → **Propiedades → Compatibilidad → Forzar el uso de una herramienta de compatibilidad de Steam Play específica → `GE-Proton11-7-DualSense`**. (Heroic: establece la *versión de Wine* del juego en él.)

**No hace falta ninguna opción de lanzamiento**: Wine 11 activa la ruta nativa hidraw de forma predeterminada, así que la placa se entrega al juego automáticamente. Solo tienes que iniciarlo: los gatillos adaptativos nativos funcionan a través de la placa, 1:1 como con un DualSense por cable (verificado sin opciones en Cyberpunk 2077, Uncharted, Spider-Man Remastered, The Last of Us Part I, Avatar, Indiana Jones, Star Wars: Galactic Racer, y **tanto en Steam como en Heroic**). El juego debe tener **compatibilidad nativa con DualSense** (los juegos solo XInput dan vibración pero no gatillos adaptativos; es un límite del motor, no de la placa). El primer inicio con un Proton nuevo prepara el idioma y el prefijo y puede quedarse "No responde" durante un minuto; es la preparación, no un cuelgue; deja que termine.

**Deja el audio del mando en su perfil predeterminado.** GE-Proton 11 gestiona por sí mismo la háptica del DualSense y espera la tarjeta en el perfil estándar **HiFi**. Una regla de WirePlumber/PulseAudio que fuerce el DualSense a **pro-audio** (un consejo habitual para versiones antiguas de Proton) hizo que Avatar enviara gatillos con fuerza cero y perdiera la háptica HD (verificado; otros juegos portados por Sony probablemente se comporten igual). Elimina esa regla, o comprueba con `pactl list cards` que el DualSense muestra `Active Profile: HiFi (...)`.

### Heroic (Epic / GOG): la misma receta, dos requisitos más

Los gatillos nativos funcionan igual en Heroic, pero los lanzadores que no son Steam son sensibles a dos cosas del host que se quedan con el mando sin avisar:

1. **Cierra Steam por completo**: un Steam en segundo plano se queda con el DualSense en los juegos que no son de Steam (o desactiva Parámetros → Mando → compatibilidad con mandos de PlayStation).
2. **Nada de `PROTON_PREFER_SDL` global**: esa variable de entorno fuerza la ruta SDL/Xbox y *suprime* los gatillos nativos. Úsala solo por juego, como alternativa deliberada de mando genérico.

**Referencia de opciones de lanzamiento**

| Variable | Valor | Propósito |
|---|---|---|
| `PROTON_ENABLE_HIDRAW` | `0x054c/0x0ce6` | *Heredada / opcional: **no hace falta** en Wine 11*, que activa la ruta nativa hidraw de forma predeterminada. Inofensiva si se define. Solo es relevante en Protons antiguos donde hidraw había que activarlo (pero esos tampoco tienen la corrección #9034, así que ahí los gatillos no funcionan de todos modos). |
| `PROTON_PREFER_SDL` | `1` | *Alternativa, solo entrada.* Fuerza la ruta de mando SDL: da entrada y vibración que funcionan, pero como mando genérico/Xbox (**sin gatillos**). Úsala solo cuando un juego no tenga compatibilidad nativa con DualSense y `ENABLE_HIDRAW` no ayude. **No** la combines con `ENABLE_HIDRAW` en juegos con gatillos. Necesita un Proton que la respete: el **GE-Proton11-7-DualSense** publicado la ignora, así que ejecuta los juegos solo XInput con el GE-Proton estándar hasta que se publique la siguiente versión DualSense. |
| `PROTON_LOG` | `+hid` | *Solo para diagnóstico* (no para jugar con normalidad). Escribe un registro de la enumeración HID: Steam lo deja en `~/steam-<appid>.log`; Heroic pone `PROTON_LOG_DIR` en tu carpeta personal, así que queda en `~/steam-0.log`. Útil para confirmar que el juego abrió la ruta HID nativa. |

> **Heroic frente a Steam:** los gatillos nativos funcionan en **ambos**, y ninguno necesita una opción de lanzamiento en Wine 11. Steam: establece el runner + desactiva Steam Input. Heroic: establece el runner, **cierra Steam por completo** (un Steam en segundo plano se queda con el mando en los juegos que no son de Steam) y deja `PROTON_PREFER_SDL` desactivado. En ambos casos el juego debe ser compatible de forma nativa con DualSense: los juegos solo XInput (p. ej., Ghostrunner, Control) dan vibración pero no gatillos adaptativos, en cualquier sistema. Esos juegos necesitan `PROTON_PREFER_SDL=1` y un Proton que la respete: el **GE-Proton11-7-DualSense** publicado no lo hace, así que usa el GE-Proton estándar para ellos hasta que se publique la siguiente versión DualSense.

## Hardware

### Necesario

| Artículo | Notas | Precio aprox. |
|---|---|---|
| **Raspberry Pi Pico 2 W** | MCU RP2350 con Bluetooth/WiFi CYW43 integrado. [Página oficial del producto](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) | ~7 USD |
| **Mando Sony DualSense** | Cualquier DualSense de PS5 (VID `054C:0CE6`) o DualSense Edge (`054C:0DF2`). | — |
| **Cable USB-C** | Conecta la Pico 2 W al PC. | — |

### Opcional (muy recomendado)

| Artículo | Notas | Precio aprox. |
|---|---|---|
| **Waveshare Pico-OLED-1.3** | Módulo OLED SH1107 de 128×64 (SKU HIPI1798). Se conecta directamente a los pines de la Pico 2 W. El firmware lo controla automáticamente cuando está y no hace nada cuando no está. [Página del producto](https://www.waveshare.com/pico-oled-1.3.htm) · [Wiki](https://www.waveshare.com/wiki/Pico-OLED-1.3) | ~6 USD |
| **Disipador pequeño** para el RP2350 | No hace falta: el firmware ejecuta el RP2350 a sus 150 MHz nominales y con el voltaje predeterminado (consulta [Rendimiento / Reloj](#rendimiento--reloj)). No pasa nada si ya tienes uno. | 1–3 USD |

### Dónde comprar

Tanto la Pico 2 W como la Waveshare Pico-OLED-1.3 se encuentran fácilmente en todo el mundo:

- **Adafruit**, **Pimoroni**, **The Pi Hut**, **DigiKey**, **Mouser**: grandes distribuidores de electrónica (EE. UU. / UE)
- **La tienda de Waveshare** para el módulo OLED
- Las tiendas regionales de **Amazon**: busca `Raspberry Pi Pico 2 W` y `Waveshare Pico-OLED-1.3` (o el SKU `HIPI1798`)
- **AliExpress**: stock original de Waveshare y Pico además de clones; revisa la valoración del vendedor

## Cómo empezar

### Flashear el firmware

La [app](#️-primeros-pasos-descarga-la-app) lo hace por ti. Para flashear a mano (o para la versión para Pico W, que es un artefacto de CI):

1. Mantén pulsado el botón BOOTSEL de la Pico2W
2. Conecta la Pico2W al ordenador por USB
3. El dispositivo se montará como unidad de almacenamiento USB
4. Arrastra y suelta el archivo de firmware .uf2 (de las [versiones publicadas](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases)) en el dispositivo

### Emparejar el mando

1. Pon el mando DualSense en modo de emparejamiento Bluetooth
2. Espera a que la Pico2W lo detecte y se conecte
3. Una vez conectado, el dispositivo aparecerá en el sistema

## Configuración

Hay cuatro formas de configurar el firmware:

**App de configuración (recomendada):** [DS5 Dongle OLED Edition Configurator](#️-primeros-pasos-descarga-la-app) para Linux y Windows (incluye una versión para macOS que funciona en macOS 27.0.1; la página Juegos muestra los juegos de Steam, Heroic y Lutris en Linux y los de Steam, GOG Galaxy y Epic en Windows, y su configuración de Proton es solo para Linux). Encuentra la placa por sí sola, aplica los cambios en directo y los guarda en la flash de la placa con un clic. Necesita firmware v0.8 o posterior.

**En el propio dispositivo (con el módulo OLED):** usa el menú **Settings** en pantalla (la 11.ª pantalla). Los botones de dirección ▲▼ mueven la selección, ▶◀ ajustan los valores, △ guarda en la flash.

**CLI de terminal (cualquier sistema, cualquier navegador):** instala hidapi y usa `scripts/set_ds5.py`:

```bash
pip install hidapi
scripts/set_ds5.py                            # muestra la configuración actual
scripts/set_ds5.py --auto-haptics fallback    # cambia un campo y lo guarda en la flash
scripts/set_ds5.py --speaker-volume -10 --haptics-gain 1.5
scripts/set_ds5.py --slot 2                   # cambia la ranura de emparejamiento activa
scripts/set_ds5.py --version                  # versión del firmware
scripts/set_ds5.py --rssi                     # señal BT en directo, dB respecto al rango ideal (0 = ideal)
scripts/set_ds5.py --help                     # lista completa de opciones
```

El script se comunica con el firmware mediante los informes de características HID USB `0xF6`/`0xF7`/`0xF8`/`0xF9`. Desde la v0.6.12 esos informes no se declaran en el descriptor HID (los juegos lo necesitan idéntico byte a byte al de un DualSense real), así que el script solo funciona en Linux; en otros sistemas usa la app de configuración. Portado de [loteran/DS5Dongle](https://github.com/loteran/DS5Dongle) y ampliado para el campo `current_slot` de este fork.

**Botones del mando DualSense (alternativa heredada, sin OLED ni CLI):**

### Volumen del micrófono

Controla el multiplicador de ganancia háptica. Rango: `[1.0 – 2.0]`.

### Silenciar el altavoz

Desactiva el indicador LED de conexión. Surte efecto cuando el mando se vuelve a conectar.

### Silenciar el micrófono

Desactiva la desconexión silenciosa.

## Notas

El dispositivo Pico solo será visible para el sistema después de conectar el mando

Algunos comportamientos necesitan un ciclo de reconexión para surtir efecto

### Indicador LED de batería baja

Cuando el DualSense conectado informa de que su batería está al 10 % o menos (y no se está cargando), el LED integrado de la Pico pasa de encendido fijo a parpadear a 1 Hz para que veas el aviso de un vistazo. El LED vuelve a encendido fijo en cuanto el mando se conecta a la corriente o su nivel vuelve a subir. El parpadeo también se activa cuando `disable_pico_led` está activado: el aviso se trata como crítico y anula la preferencia de LED apagado; el LED vuelve a su estado desactivado (apagado) cuando la batería se recupera o el mando empieza a cargarse.

Para desactivarlo al compilar, configura con `-DENABLE_BATT_LED=OFF`. El valor predeterminado es ON.

## Micrófono del DualSense por Bluetooth

**El micrófono integrado del DualSense funciona a través del emparejamiento Bluetooth de la placa** (desde la v0.6.8). El mando transmite su micrófono como audio Opus; la placa lo decodifica y lo presenta al host como el dispositivo de captura USB estándar del DualSense, así que cualquier app (Discord, OBS, chat de voz de los juegos) puede usarlo como un micrófono normal.

Durante mucho tiempo se creyó imposible: versiones anteriores de este fork lo documentaban como una limitación fija del firmware de Sony (y el controlador del kernel de Linux `hid-playstation` sigue sin admitirlo). Resultó que dependía de un único bit de activación en el informe de audio saliente de la placa. **Todo el mérito para [awalol](https://github.com/awalol/DS5Dongle) (upstream) por descubrirlo.** El registro corregido de la investigación está en [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md).

**Uso:**

- **Activado de forma predeterminada.** Empareja el mando y el micrófono empieza a transmitir en uno o dos segundos; no hace falta audio del juego (la placa mantiene la transmisión por sí sola).
- **Sube el volumen de captura en el host**: de forma predeterminada está bajo. En Linux, busca la tarjeta con `arecord -l` y luego, por ejemplo, `amixer -c <DualSense card> sset 'Headset' 90%`. Comprueba la captura con `scripts/mic_diag.sh capture`.
- **Desactívalo para ahorrar batería del mando**: en la OLED, **Settings → BT Mic**, o el interruptor **Micrófono integrado** de la [app de configuración](#️-primeros-pasos-descarga-la-app). Un micrófono siempre activo mantiene despierto el subsistema de audio del DS5, lo que agota su batería bastante más rápido, así que desactívalo si no usas la voz.

**Advertencias:**

- El audio del micrófono es **mono** (decodificado en mono y duplicado en el punto de captura estéreo).
- Desactivarlo a mitad de sesión detiene de inmediato la señal al host, pero el mando sigue transmitiendo hasta la siguiente reconexión (no se conoce ningún comando de "parar"); al conectar de cero con el interruptor desactivado, nunca se activa.
- El contador `Mic in:` de la pantalla **Diagnostics** de la OLED marca ~100/s mientras el micrófono transmite: una forma rápida de confirmar que está activo.
- **Ocultación de pérdida de paquetes:** las tramas de micrófono perdidas (enlace BT débil, distancia, interferencias) se ocultan con Opus PLC para que la voz siga continua en lugar de chasquear o cortarse, a cambio de una pequeña latencia del búfer de jitter (~30 ms). El contador `Mic PLC:` de la pantalla Diag solo sube cuando se ocultan tramas: en la práctica, un medidor en directo de la calidad del enlace.

## Problemas conocidos

- Puede que la háptica HD no se active en todos los juegos en Linux + Steam; es cosa del juego (algunos títulos solo envían el audio háptico HD con APIs específicas de Windows). Probado y funcionando en Spider-Man Remastered; no llega en Ghost of Tsushima, con el mismo firmware y el mismo mando.
- **Los puertos USB 3.0 pueden interferir en el emparejamiento**: el mando puede quedarse con la barra luminosa ámbar/amarilla fija sin llegar a conectarse, mientras que la misma placa funciona bien en un puerto USB 2.0. Son interferencias de radiofrecuencia, no un fallo del firmware; consulta [Puertos USB 3.0 e interferencias Bluetooth](#puertos-usb-30-e-interferencias-bluetooth) más abajo. (Desde la v0.6.8 el firmware reintenta automáticamente una conexión atascada en lugar de quedarse colgado, lo que recupera muchos casos límite, aunque no todos.)
- **Windows: la app no puede abrir la placa** en PC donde antes se conectó un DualSense real por USB (la placa comparte sus ID USB, y Windows recordó "no necesita controlador" para ellos). Desde la app 0.9.2, la pantalla de configuración ofrece **Reparar controlador USB**: un aviso de administrador y luego vuelve a conectarla. El PowerShell completo que ejecuta, línea a línea, está en el [README de la app](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web#repair-usb-driver-exactly-what-it-runs).

## Puertos USB 3.0 e interferencias Bluetooth

Si la placa funciona en un puerto pero no en otro, **prueba primero un puerto USB 2.0.** Los puertos USB 3.0 y (sobre todo) los alargadores USB 3.0 emiten ruido de radiofrecuencia de banda ancha centrado cerca de 2,4 GHz, la misma banda que usa la radio Bluetooth de la placa para hablar con el mando. Es un problema bien documentado en la industria (Intel, *"USB 3.0 Radio Frequency Interference Impact on 2.4 GHz Wireless Devices"*), no algo propio de este firmware. El ruido reduce la sensibilidad del receptor BT de la placa, así que el mando puede empezar a conectarse (barra luminosa ámbar) pero el enlace tiene demasiado ruido para completarse: se queda en amarillo.

Soluciones, más o menos por orden de eficacia:

1. **Conecta la placa a un puerto USB 2.0** (suele ser lo más sencillo; muchas placas base y cajas tienen ambos).
2. **Usa un alargador USB 2.0 corto** para alejar la placa unos centímetros de los puertos USB 3.0 / del chasis metálico y mejorar la línea de visión con el mando. Evita en concreto los alargadores USB 3.0.
3. **Usa un hub USB 2.0 con alimentación** conectado al puerto USB 3.0: el hub reduce la velocidad del enlace y añade distancia.
4. **Pon una ferrita** en el cable cerca de la placa.
5. **Mantén el mando más cerca / con línea de visión** a la placa durante el emparejamiento.

El firmware sigue reintentando por sí solo una conexión atascada, así que dejarla conectada unos 10–20 s después de que la barra luminosa se ponga ámbar puede permitir que se recupere sin volver a conectarla.

## Rendimiento / Reloj

**No tienes que hacer nada para esto.** La Pico 2 W funciona con los mismos ajustes que upstream awalol/DS5Dongle v0.7.0:

- **Reloj: 150 MHz**, el reloj nominal del RP2350 (con el que arranca)
- **Voltaje del núcleo: 1,10 V**, el predeterminado. El firmware lo establece al arrancar, porque el RP2350 conserva el voltaje de un firmware anterior hasta que se desconecta.
- **Bus del chip Bluetooth (CYW43 PIO SPI): divisor de reloj 2**
- **Flash: 75 MHz** (divisor 2), configurada por el propio firmware al arrancar (`PICO_EMBED_XIP_SETUP`). Dejarla con el ajuste del bootrom hacía que core1 se colgara en algunas placas ([issue #15](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/15)).

Las versiones antiguas hacían overclock a 320 MHz a 1,20 V, un ajuste heredado de upstream. Upstream pasó a 150 MHz cuando la ruta de audio empezó a ejecutarse desde la RAM, y este fork también lo hace ahora que la ruta de audio y la ruta crítica Bluetooth/USB de core0 se ejecutan desde la RAM. 1,20 V está por encima del máximo de funcionamiento de 1,16 V del RP2350, y a 150 MHz no hace falta disipador.

Durante una versión, una compilación desde el código fuente aún puede usar el overclock antiguo como alternativa: `-DDS5_CLOCK_KHZ=320000` (1,20 V, divisor 5 del bus Bluetooth, divisor 4 de la flash). La compilación rechaza una configuración de Pico 2 W sin la configuración de flash integrada o con la flash por encima de 100 MHz. La versión para Pico W no cambia (200 MHz).

## Instrucciones de compilación

Para compilar el proyecto desde el código fuente:

1. Instala el Pico SDK **2.3.0**. La compilación usa `pico_sdk_import.cmake`. CI y las versiones publicadas compilan con la cadena de herramientas GNU oficial de ARM **15.2.Rel1** (`arm-none-eabi`); el `gcc-arm-none-eabi` de una distribución suele funcionar, pero 15.2.Rel1 da el mismo binario que la versión publicada. Las versiones exactas (SDK, commit de TinyUSB, descarga de la cadena de herramientas) están en `tools/toolchain.env`. Pon `PICO_TOOLCHAIN_PATH` en la carpeta descomprimida de la cadena de herramientas si no está en tu `PATH`.
2. **Fija TinyUSB en el commit de `tools/toolchain.env`** dentro del Pico SDK (`$PICO_SDK_PATH/lib/tinyusb`): TinyUSB 0.21 más correcciones posteriores, como usa upstream. El Pico SDK 2.3.0 sigue incluyendo la 0.18.0, que no tiene la forma de 4 argumentos de `TUD_AUDIO_EP_SIZE` que usa el `tusb_config.h` de este proyecto, y la compilación rechaza cualquier TinyUSB que no sea la 0.21:
   ```bash
   cd "$PICO_SDK_PATH/lib/tinyusb"
   git fetch --depth 1 origin 2d56dc533e45e4e91b15e93fdab5e22e964f328d
   git checkout --detach FETCH_HEAD
   ```
3. Configura y compila con CMake + Ninja (o Make):
   ```bash
   cd /path/to/DS5Dongle
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
   cmake --build build --target ds5-bridge
   ```
4. El UF2 queda en `build/ds5-bridge-oled.uf2`. Flashéalo con BOOTSEL como siempre.

Opciones de compilación útiles:

- `-DENABLE_BATT_LED=ON` (predeterminado): hace parpadear el LED de la Pico cuando la batería del DS5 está baja.
- `-DENABLE_SERIAL=ON`: envía printf a USB CDC para depurar (OFF de forma predeterminada; libera el UART en las compilaciones de producción).
- `-DPICO_W_BUILD=ON`: compila para la Pico W original (sin audio, reloj más bajo). De forma predeterminada se compila para la Pico 2 W.

## Diagnóstico y herramientas de depuración

Dos formas de investigar problemas del puente: en el dispositivo, con la pantalla Diagnostics de la OLED, y en el host, con `scripts/mic_diag.sh` (Linux). La del host es más rápida: sin cambiar de pantalla, sin ciclo de flasheo, y funciona mientras se usa el mando.

```
# Instantánea única: ¿está la placa en el USB? ¿La ha enumerado ALSA? ¿Está
# activa la transmisión de captura? ¿Hay un mando emparejado ahora mismo?
scripts/mic_diag.sh status

# arecord de 3 segundos en el endpoint IN del micrófono: informa de pico / RMS /
# recuento de muestras no nulas para distinguir "transmisión en silencio" de "transmisión con audio".
scripts/mic_diag.sh capture 3

# Igual que `status` pero en bucle; solo imprime cuando cambia el estado. Útil para
# captar el segundo exacto en que termina el emparejamiento o se abren / cierran las transmisiones de audio.
scripts/mic_diag.sh watch

# Lectura en directo del informe de características de fabricante 0xFD del firmware (vía /dev/hidraw):
# recuentos y tasas de entrada BT, últimos ID distintos de 0x31, prefijos de bytes, Y los
# contadores del flujo de gatillos (0x02 del host recibidos / con AllowTriggerFFB activado /
# reenviados a BT). bt-trace da un veredicto, "el controlador del host no envía los
# bits Allow de los gatillos" frente a "reenviado pero el mando no actuó", que es
# lo que de otro modo necesitaría un analizador de protocolo USB.
scripts/mic_diag.sh bt-trace
```

Se escribió originalmente para investigar el estudio aparcado del micrófono BT del DS5 (consulta [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)). El informe de características `0xFD` y el decodificador `bt-trace` incluyen ahora también los contadores del flujo de gatillos añadidos para el [issue #3](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/3) (falta de tensión de los gatillos adaptativos en algunos juegos).

## Módulo de pantalla OLED (opcional)

Si conectas una [Waveshare Pico-OLED-1.3](#hardware) a los pines de la Pico2W, el firmware la controla automáticamente como pantalla de estado en directo. No hace falta configurar nada: el firmware no hace nada cuando no hay OLED.

### Pantalla de arranque (1,5 s al encender)

La versión del firmware centrada en una pantalla en blanco durante 1,5 segundos, y luego salta a la pantalla Status.

### Once pantallas, que se recorren con KEY0 en el módulo

Orden del ciclo: **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → CPU/Clock → BT Signal → VU Meters → Settings →** vuelta al principio. **Una pulsación corta de KEY0 avanza; una pulsación corta de KEY1 retrocede**, en *todas* las pantallas. Las interacciones de cada pantalla (cambiar el efecto de gatillo, cambiar el modo de la barra luminosa, mover el cursor de Settings, cambiar de ranura) están en los **botones del mando DualSense**, nunca en KEY0/KEY1, así que los dos botones físicos del módulo OLED siempre significan lo mismo.

Cada pantalla dibuja además **`>`** en el borde superior izquierdo (junto a KEY0) y **`<`** en el borde inferior izquierdo (junto a KEY1) para que las marcas en pantalla queden junto a sus botones.

#### 1. Estado

Estado de la conexión, dirección BD del DualSense emparejado, % de batería con barra (`+` cargando / `*` completa / `!` error), posiciones de los joysticks analógicos en directo (el recuadro de cada joystick parpadea invertido, un punto negro sobre un recuadro blanco, mientras se pulsa su **L3 / R3**), botones de dirección, botones de acción (△ ◯ ✕ □), L1/R1 y las barras de recorrido de los gatillos analógicos L2/R2. El indicador de enlace y la batería usan pequeños iconos de píxeles. Mientras se carga, junto a la batería aparece una estimación `~Nm` del tiempo hasta el 100 %.

<img src="./assets/oled/oled_sc01.jpg" alt="Pantalla Status en la OLED" width="420">

#### 2. Ranuras

Emparejamiento persistente de varios mandos en 4 ranuras. Recorre los mandos guardados, cambia la ranura activa o borra una sola ranura. `>` es el cursor, `*` marca la ranura activa.

<img src="./assets/oled/oled_sc02.jpg" alt="Pantalla Slots en la OLED" width="420">

- **Botones de dirección ▲▼**: mueven el cursor por las ranuras 0–3
- **△**: cambia a la ranura del cursor (desconecta el mando actual y se reconecta al mando guardado en esa ranura)
- **□ mantenido 1,5 s**: borra la ranura del cursor (elimina bd_addr + la clave de enlace de BTstack)
- La ranura activa se guarda; la placa se reconecta a ella en el siguiente arranque

#### 3. Selector de color de la barra luminosa

Inclina el mando en cada eje para ajustar R / G / B; el firmware envía el color resultante a la barra luminosa real del DualSense a 10 Hz, así que la barra luminosa ES la vista previa (la OLED es monocroma).

<img src="./assets/oled/oled_sc03.jpg" alt="Selector de color de la barra luminosa en la OLED" width="420">

- Pulsa **△ ◯ ✕ □** en el mando para **guardar** el color actual en la ranura de favoritos 0 / 1 / 2 / 3
- Pulsa **R1** en el mando para cambiar la etiqueta de modo: `[LIVE]` → `[FAV0]` → `[FAV1]` → `[FAV2]` → `[FAV3]` → efectos (Breathing / Rainbow / Fade) → `[HOST]` → de nuevo `[LIVE]`
- `[HOST]` (el predeterminado) deja la barra luminosa al PC / juego; la placa no pone ningún color. Cualquier otro modo mantiene el color de la placa, envíe lo que envíe el juego
- Favoritos predeterminados: rojo, verde, azul, blanco

#### 4. Prueba de gatillos

Pulsa **△** en el mando para recorrer siete efectos de gatillos adaptativos aplicados a L2 y R2. Aprieta cada gatillo para notar el efecto.

<img src="./assets/oled/oled_sc04.jpg" alt="Pantalla Trigger Test en la OLED" width="420">

Orden del ciclo: **Off → Feedback → Weapon → Vibration → Bow → Gallop → Machine Gun → Off …** Parámetros de los efectos empaquetados en bits según la ingeniería inversa de [dualsensectl](https://github.com/nowrep/dualsensectl), todos a máxima fuerza.

#### 5. Inclinación del giroscopio

Valores X/Y/Z del acelerómetro en directo con un recuadro de 40×40 con cruz. El punto sigue la inclinación del mando en tiempo real y **queda centrado cuando el mando está plano**: usa la calibración IMU de fábrica propia de cada mando (leída del informe de características `0x05`), así que la posición de reposo y la ganancia son correctas en todos los mandos. Inclinarlo a izquierda/derecha y adelante/atrás mueve el punto en la dirección correspondiente.

<img src="./assets/oled/oled_sc05.jpg" alt="Pantalla Gyro Tilt en la OLED" width="420">

#### 6. Panel táctil

Representación en directo de la superficie del panel táctil. Aparecen puntos en las posiciones actuales de los dedos; el recuento se actualiza al tocar / levantar los dedos.

<img src="./assets/oled/oled_sc06.jpg" alt="Pantalla Touchpad en la OLED" width="420">

#### 7. Diagnóstico

Lista desplazable de contadores en directo: tiempo en marcha, estado BT, flujo de gatillos host → BT (`host02` / `trig` / `tx`), tasa de entrada BT 0x31, tramas de audio USB/s, paquetes BT 0x32/s y, al final, los contadores de la investigación aparcada del micrófono. Los botones de dirección ▲/▼ del mando desplazan la lista; pequeños glifos `^` / `v` en el borde derecho indican "hay más arriba/abajo". Solo lectura, así que sin cursor. Útil para comprobar que el puente mueve bytes sin necesitar un cable UART.

Los mismos contadores también se exportan en el informe de características HID `0xFD` para las herramientas del host; consulta `scripts/mic_diag.sh bt-trace` más abajo.

<img src="./assets/oled/oled_sc07.jpg" alt="Pantalla Diagnostics en la OLED" width="420">

#### 8. CPU / Reloj

Constantes vitales del RP2350 en directo: reloj del sistema configurado (`Set`) y el que funciona realmente (`Real`), medido con la referencia del cristal, voltaje del núcleo leído del regulador y temperatura del chip (media de 256 muestras + EMA lenta para que el valor siga la temperatura real del chip y no el ruido del ADC).

<img src="./assets/oled/oled_sc08.jpg" alt="Pantalla de diagnóstico CPU / Clock en la OLED" width="420">

La misma telemetría también se exporta en el informe de características HID `0xFC` para las herramientas.

#### 9. Señal BT

Intensidad de la señal Bluetooth del enlace activo en directo, en dB respecto al rango ideal (0 = ideal, se muestra como *in range*; más negativo es más débil), con una barra de −30 dB (vacía) a 0 dB (llena) y las palabras de la app (Excellent / Good / Fair / Weak, es decir Excelente / Buena / Regular / Débil). Antes de la primera lectura, o si una lectura falla, muestra `Signal: --` y `Link: unknown`.

<img src="./assets/oled/oled_sc09.jpg" alt="Pantalla BT Signal en la OLED" width="420">

#### 10. Vúmetros

Medidores de pico en directo de las rutas de audio del altavoz y de la háptica. Útiles para comprobar el enrutamiento de audio sin que el mando esté conectado a un host.

<img src="./assets/oled/oled_sc10.jpg" alt="Pantalla VU Meters en la OLED" width="420">

#### 11. Ajustes

Editor de configuración persistente. Los botones de dirección ▲▼ mueven la selección, ▶◀ ajustan los valores, △ guarda en la flash. Incluye los campos de configuración del firmware (ganancia háptica, volumen del altavoz, tiempo de inactividad, frecuencia de sondeo), los controles de Audio Auto Haptics y dos acciones que se confirman manteniendo pulsado:

`Hap Left` y `Hap Right` ajustan cada empuñadura por separado. Pulsa △ para aplicar y guardar los cambios.

<img src="./assets/oled/oled_sc11.jpg" alt="Pantalla Settings en la OLED" width="420">

- **AutoHap Off / Fallback / Mix / Replace**: elige el modo de Audio Auto Haptics. Predeterminado `Off`: la háptica solo viene del juego. `Fallback` genera vibración derivada solo cuando el juego no envía datos hápticos nativos (p. ej., Ghost of Tsushima en Linux); ten en cuenta que también vibra con la música y el audio del escritorio; los juegos que *sí* envían háptica nativa (Spider-Man Remastered) pasan sin cambios. `Mix` añade la derivada encima de la nativa, `Replace` ignora la nativa por completo.
- **AH Gain N%**: ganancia de la señal derivada, 0–200 % en pasos del 10 %. Predeterminado 100 %.
- **AH LP 80/160/250/400 Hz**: corte del filtro paso bajo aplicado al audio del altavoz antes del seguidor de envolvente. Más bajo = más subgraves, más alto = más presencia. Predeterminado 160 Hz.
- **TrigEff game / -1 … -10**: Intensidad del efecto gatillo: reduce la resistencia de los gatillos adaptativos de los juegos en 10 pasos. Predeterminado `game` (0): la fuerza propia del juego, con su informe sin tocar.
- **SpkBoost game / 1 … 7**: Amplificación del altavoz: la preganancia del amplificador del mando. Predeterminado `game` (0): lo decide el juego. Independiente de `Spk Vol`.
- **VolLock on / off**: Bloquear volumen: los juegos no pueden cambiar el volumen del altavoz, de los auriculares o del micrófono del mando, ni el silencio ni la luz del botón de silenciado; el volumen y el silencio propios del PC siguen funcionando. Predeterminado off.
- **SPK Out Auto / SPK / Hset / off**: Salida de audio: dónde suena el audio del PC (Auto = los auriculares cuando hay unos conectados al mando). Predeterminado Auto.
- **Mic In Auto / Int / Hset**: Micrófono en uso: Auto, el micrófono integrado o el de los auriculares. Predeterminado Auto. `BT Mic` sigue siendo el interruptor que apaga el micrófono.
- **Sleep CtrlOff on / off**: apaga el mando cuando el PC lleva 3 s en suspensión. Predeterminado on.
- **PS Wakes PC on / off**: sin ningún mando conectado, la placa aparece como un teclado solo para reactivar ("DS5 Dongle Wake Keyboard") para que el botón PS pueda reactivar el PC. Predeterminado off.
- **Reset to defaults**: mantén pulsado △ 2 s para restablecer todos los campos de configuración
- **Wipe all slots**: mantén pulsado △ 2 s para eliminar los 4 mandos emparejados + todas las claves de enlace de BTstack

### Referencia de botones

Los dos botones físicos del módulo OLED son **solo de navegación**:

| Botón | Acción |
|---|---|
| **KEY0** pulsación corta | Pantalla siguiente (avanzar) |
| **KEY1** pulsación corta | Pantalla anterior (retroceder) |
| **KEY1** mantenido más de 1,5 s | Cambia el brillo una vez mientras se mantiene; al soltar no vuelve atrás |
| **KEY0 + KEY1** mantenidos a la vez ≥ 1 s | `watchdog_reboot`: reinicio por software sin desconectar el USB |

Los cambios de estado de cada pantalla (cambiar el efecto de gatillo, cambiar el modo de la barra luminosa, navegar por Settings, cambiar de ranura, guardar colores en las ranuras de favoritos) se hacen todos con los **botones del mando DualSense**, nunca con KEY0 / KEY1, así que los dos botones físicos siempre significan lo mismo en todas las pantallas. Consulta la sección de cada pantalla más arriba para saber qué botón del mando hace qué.

### Pines (disposición estándar de los HAT para Pico de Waveshare)

| Función | GPIO |
|---|---|
| MOSI | 11 |
| SCK  | 10 |
| CS   | 9  |
| DC   | 8  |
| RST  | 12 |
| KEY0 | 15 |
| KEY1 | 17 |

### Recuperación con reinicio por software

Dos formas de reiniciar la placa sin desconectar el USB, útiles si el emparejamiento se atasca o quieres empezar de cero:

- **KEY0 + KEY1 de la OLED mantenidos a la vez ≥ 1 s** → `watchdog_reboot`. Sustituye al antiguo gesto de "doble clic en KEY0" de versiones anteriores, porque la navegación rápida hacia delante activaba sin querer el temporizador del doble clic.
- **`PS + Mute` del DualSense mantenidos 2 segundos** → `watchdog_reboot` (funciona tenga o no la OLED conectada: alternativa sin pantalla).

## Agradecimientos

Algunas funciones e ideas de diseño de este fork se han tomado de otros forks de upstream, con reconocimiento:

- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)**: iconos de píxeles en la cabecera de estado de la OLED (enfoque visual), el patrón de interacción "mantener pulsado para restablecer de fábrica" que usa el elemento "Reset to defaults" de la pantalla Settings (mantener △ 2 s para confirmar), y el sistema de emparejamiento BT persistente en varias ranuras de la nueva pantalla Slots (4 mandos vinculados, botones de dirección para navegar, △ para cambiar de ranura, □ mantenido para borrar una ranura, además de "Wipe all slots" en el menú Settings).
- **[loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)**: redescubrimiento independiente de la regresión de upstream `3a31bd7` que rompió la salida del altavoz / háptica HD (commit `c7a8d3c`); la corrección en nuestro `src/audio.cpp` restaura el mismo subinforme SetStateData. También es el origen del DSP de Audio Auto Haptics (paso bajo de 1 polo + seguidor de envolvente, consulta Settings → Auto Haptics) y de la corrección "no sincronizar el volumen UAC1 del lado USB con la configuración persistente".
- **[SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge)**: un puente DualSense para Pico hermano. La lógica de aplicación de la reasignación de botones y el conjunto de controles digitales de `src/remap.{h,cpp}` están portados de él. Su arquitectura de audio (codificación Opus en el dispositivo frente a una app complementaria opcional que codifica en el host) también fue la referencia para diagnosticar nuestro trabajo sobre la cadencia del altavoz ("warble"). Gracias por las bases.
- **[awalol/ds5dongle-config-web](https://github.com/awalol/ds5dongle-config-web)**: base de nuestra app web de configuración bifurcada en [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web). El fork adapta la disposición Config_body de 13 bytes de upstream a nuestra de 19 bytes y añade interfaz para nuestros añadidos (emparejamiento en varias ranuras, Auto Haptics).
- **[PS5 Button Icons and Controls](https://zacksly.itch.io/ps5-button-icons-and-controls)** de **Zacksly**: el contorno del mando DualSense y los iconos de botones de la página «Asignaciones de botones» del configurador (app de escritorio y herramienta web), con licencia [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) (recoloreados a `currentColor` y recortados para los temas).

## Hoja de ruta
- Consulta el [DS5Dongle plan](https://github.com/users/awalol/projects/5)

## Comunidad
- Únete al servidor de Discord: [Discord Server](https://discord.gg/hM4ntchGCa)
- Si encuentras un fallo, abre un issue en su lugar.

## Referencias

- [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense): inspiración del proyecto
- [egormanga/SAxense](https://github.com/egormanga/SAxense): prueba de concepto de háptica por Bluetooth
- [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense): documentación de la estructura de los informes de datos del DualSense
- [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX): paquete del informe del altavoz

---

*Esta traducción sigue el [README.md](README.md) en inglés, que es la referencia; puede ir por detrás.*
