# Pico2W DualSense 5 Bridge — OLED Edition

[English](README.md) · [中文](README.CN.md) · [Deutsch](README.DE.md) · [Español](README.ES.md) · **Français** · [日本語](README.JA.md) · [Português (Brasil)](README.PT-BR.md)

> Transforme un Raspberry Pi Pico2W en adaptateur sans fil pour la manette DualSense (DS5), avec en option un écran d'état embarqué.

> **OLED Edition** est un fork de **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** (upstream) qui ajoute un module d'affichage optionnel Pico-OLED-1.3 128×64 avec 11 écrans (état, association de 4 manettes dans des emplacements, sélecteur de couleur de la barre lumineuse avec favoris et préréglages d'effets, test des gâchettes, inclinaison gyroscope, pavé tactile, diagnostic, CPU/horloge, puissance du signal BT, vumètres audio et un menu de réglages persistant), plus un redémarrage logiciel par combinaison de touches de la DS5. Upstream reste la source de référence pour le firmware du pont ; ce fork le suit et ajoute ses fonctions par-dessus.

> **🧪 Tu veux les toutes dernières fonctions ? Essaie le canal de développement.** Dans l'app : **Réglages de l'app → Mises à jour → Développement** (elle te demande d'abord ; **Revenir au stable** te ramène à tout moment). Les versions de développement passent les mêmes tests automatiques à chaque modification ; les versions stables passent en plus toutes les vérifications de sortie sur du vrai matériel et sous Windows, et un temps d'usage au quotidien. L'actuelle, **0.9.5-dev.19**, ajoute par rapport à la version stable 0.9.4 :
>
> - **DualSense Edge :** réattribution de la touche Fn et des touches arrière, profils Edge dans PlayStation Accessories, et son propre dessin et sa propre vue OLED
> - **Joysticks :** courbes de réponse et tests des joysticks
> - **Micro de la manette :** réglage du niveau, annulation de l'écho, et le voyant de la touche mode silencieux suit la coupure du micro ; la coupure du son et le volume du micro du PC arrivent jusqu'à la manette
> - **Veille et réveil :** la manette s'éteint quand le PC se met en veille, et la touche PS peut réveiller le PC
> - **Jeux (Linux) :** n'importe quelle version de Proton par jeu, configuration HDR, et les jeux uniquement XInput trouvent la manette
> - **Piloter l'OLED depuis l'app :** KEY0/KEY1 et les touches de la manette à côté de l'écran en direct, plus **Partager l'OLED** (PNG, presse-papiers ou GIF)
> - **Vérification du firmware de la manette** et infos sur l'état de la manette
> - **Casque (Linux) :** stéréo dès qu'un casque est branché sur la manette, et retour au haut-parleur quand il est retiré
> - **Association depuis la carte :** l'OLED affiche *Press PS to connect* ; maintiens KEY0 2 s pour associer, avec un compte à rebours ; plus de recherche sans fin au repos
> - **Tous les messages en 7 langues**

---

## 🛠️ Pour commencer : télécharger l'app

**[→ Télécharger DS5 Dongle OLED Edition Configurator](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest)** (AppImage Linux et installateur Windows sans droits administrateur, tous deux testés ; la version macOS est testée par la communauté : [signalée fonctionnelle sous macOS 27.0.1](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/17))

L'app suffit. Chaque version contient le firmware correspondant : l'app l'installe sur ton Pico, le tient à jour et configure tout :

1. Assemble ta carte (voir [Matériel](#matériel) plus bas) et lance l'app.
2. Branche le Pico **en maintenant son bouton BOOTSEL** (un Pico tout neuf n'a pas besoin du bouton). L'app le trouve et installe le firmware en un clic ; rien à copier à la main.
3. Associe ta manette : maintiens **PS + Create** jusqu'à ce que la barre lumineuse clignote. L'app indique quand elle est connectée.

![L'app : ta manette en direct, dans ses propres couleurs](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/live-controller.png)

Ta manette est dessinée dans ses propres couleurs et bouge quand tu l'utilises ; clique sur n'importe quelle partie pour la régler. Le **[guide pas à pas](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/GUIDE.md)** (en anglais) explique tout, de l'assemblage de la carte à chaque réglage :

- **La manette :** modes et favoris de la barre lumineuse, effets des gâchettes adaptatives, **retour haptique avec intensité des vibrations par côté** (retour haptique et vibration classique, avec un test de 3 secondes), Audio Auto Haptics, haut-parleur et microphone intégré (avec tests), attribution des touches (toutes les touches dans une seule liste, y compris la touche Fn et les touches arrière de la DualSense Edge, et des **dispositions des boutons enregistrées** qu'un jeu peut activer pendant qu'il tourne, jamais écrites en flash : firmware v0.9.4), les quatre emplacements d'association, et **Infos de la manette** (le firmware propre de la manette, comparé au dernier de Sony, la tension de sa batterie et ses compteurs à vie).
- **Calibrage :** dérive des joysticks, joysticks ou gâchettes qui n'atteignent pas le maximum, gyroscope qui dérive, corrigés dans la carte (par emplacement d'association) pour tous les jeux ; rien n'est écrit dans la manette. En plus, une **courbe de sensibilité** par joystick et des tests des joysticks (circularité et dérive).
- **La carte :** son écran OLED en miroir en direct, un tableau de bord de diagnostic (température, horloge, trafic Bluetooth et USB, la chaîne audio, et le temps que prend chaque partie du firmware), les réglages d'alimentation, d'affichage et USB, et des **mises à jour du firmware en un clic** (sans bouton BOOTSEL, sans copie sur un lecteur).
- **Jeux :** ta bibliothèque Steam, Heroic et Lutris (Windows : Steam, GOG Galaxy et Epic) en liste et en grille de jaquettes, avec des profils de manette par jeu (chaque réglage de manette que le jeu change, y compris une disposition des boutons et des courbes de joystick, modifiés en direct dans la vue de la manette), la prise en charge de la DualSense par jeu, et sous Linux des corrections de configuration plus la version DualSense de Proton.
- **À ton image :** un thème aux couleurs de ta manette, Verre, des curseurs d'apparence, des modes de vision des couleurs, Réduire les animations, un narrateur dans le haut-parleur de la manette, 7 langues, une icône dans la zone de notification.

L'app communique avec le firmware **v0.8 ou plus récent** via une petite interface USB de configuration (le calibrage et l'intensité des vibrations par côté nécessitent la **v0.9** ; l'app propose la mise à jour). Les jeux ne la voient jamais, la carte ressemble donc toujours exactement à une vraie DualSense. Code source de l'app : **[MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web)**.

> **L'outil de configuration dans le navigateur a été retiré** : l'app de bureau fait tout ce qu'il faisait, et plus encore. Son ancienne adresse, [marcelinevpq.github.io/DS5Dongle-OLED-Config-Web](https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/), est maintenant la page de téléchargement de l'app. Pour flasher un Pico sans l'app, maintiens BOOTSEL en le branchant et copie le `.uf2` de la version sur le lecteur qui apparaît.

## 🔄 Mises à jour et installation du firmware, sans effort

**L'app se tient à jour toute seule.**

- **Signées et vérifiées.** Chaque mise à jour vient d'un seul flux signé avec notre clé Ed25519. L'app ignore un flux sans signature valide, vérifie la taille et le SHA-256 de chaque téléchargement avant de l'installer, et n'installe jamais d'elle-même une version plus ancienne.
- **Deux canaux :** **Stable** pour tout le monde, **Développement** pour les toutes dernières fonctions. **Revenir au stable** ramène l'app et le firmware à la dernière version stable.
- **À toi de choisir :** avec **Installer les mises à jour automatiquement** (l'app te le demande une fois), les mises à jour se téléchargent et se vérifient en arrière-plan, et la nouvelle version démarre la prochaine fois que tu ouvres l'app. Avec **Rechercher les mises à jour automatiquement** désactivé, l'app ne se connecte que quand tu appuies sur un bouton de vérification.
- **Selon le système :** sous Linux, l'AppImage pose la nouvelle version à côté d'elle et l'entrée du menu la suit. Sous Windows, l'installeur par utilisateur fonctionne sans droits d'administrateur. Sous macOS, l'app affiche la mise à jour avec un lien vers la version publiée.

**Le firmware s'installe en un clic.**

- **Inclus :** chaque version de l'app contient le firmware avec lequel elle a été compilée et testée, donc une nouvelle carte peut être préparée hors ligne. Un firmware plus récent arrive par le même flux signé.
- **Première installation :** branche le Pico en maintenant BOOTSEL (un Pico neuf n'a pas besoin du bouton). L'app le trouve et installe le firmware.
- **Mises à jour :** pas de bouton, pas de copie sur un lecteur. L'app redémarre elle-même la carte en BOOTSEL, écrit le firmware par USB et se reconnecte.
- **Vérifié avant l'écriture :** le SHA-256 du fichier doit correspondre au flux, et ce doit être un firmware pour cette puce. Un fichier qui échoue n'écrit rien.
- **Vérifié après l'écriture :** l'app attend que le nouveau firmware démarre. Si la carte revient en BOOTSEL à la place, l'app le dit et propose **Réinstaller**. Pendant l'écriture du firmware, l'app ne redémarre pas, ne se ferme pas et ne se met pas à jour.
- **Ta configuration reste :** réglages, appairages, calibrage et attribution des touches sont dans leur propre partie de la mémoire flash. Chaque compilation du firmware est vérifiée pour ne jamais atteindre cette partie, et un test de l'app vérifie qu'une mise à jour n'écrit que les secteurs du fichier de firmware.

## 🔬 Comment c'est construit et testé

Chaque modification passe toutes les vérifications automatiques. Chaque version en ajoute d'autres. La liste complète, avec les fichiers derrière chaque vérification : [docs/QUALITY.md](docs/QUALITY.md) (en anglais).

**Chaque modification**

- **App :** plus de 1 600 tests (unitaires, d'intégration et de sécurité) sous Linux, Windows et macOS, plus la vérification des types, ESLint et Prettier.
- **Firmware :** 185 tests sur PC de la logique du firmware avec AddressSanitizer et UndefinedBehaviorSanitizer, et cinq compilations du firmware (Pico 2 W, Pico W, débogage et deux variantes). La compilation échoue si le code critique en temps quitte la RAM ou si la configuration de la flash change.
- **Règles de l'app :** les réglages de sécurité d'Electron, pas de HTML dangereux, un seul système de thèmes sans couleurs isolées, pas de code ni de fichiers en double sans verdict.
- **Verdicts écrits :** chaque boucle qui attend ou tourne sur un chemin critique, chaque chemin d'erreur, chaque cache et chaque ressource que le code prend (un minuteur, un écouteur, un appareil) porte un commentaire avec son verdict. Un nouveau sans verdict fait échouer la vérification.
- **Analyses de sécurité :** Semgrep, Gitleaks et npm audit, aussi chaque semaine.

**Chaque version de développement**

- **Vérificateur d'interface :** sa propre copie de l'app avec une carte simulée ouvre chaque page dans les 7 langues, à deux tailles de fenêtre. Il échoue sur un texte coupé, un contraste sous WCAG AA ou une commande sans nom.
- **Budgets de ressources** mesurés sous Linux et dans une machine virtuelle Windows 11.
- **Test Windows** de l'app installée dans une machine virtuelle Windows 11, et un **test rapide macOS** sur un vrai Mac de GitHub Actions (installée comme le fait un utilisateur, lancée et pilotée).

**Avant une version stable**

- **La vérification complète de l'interface :** chaque écran et chaque panneau, les 7 langues, les deux tailles, chaque thème et style, chaque coloris des deux modèles de manette.
- **La longue mesure des budgets,** avec un test de 30 minutes sur la croissance de la mémoire, sous Linux et Windows.
- **Vérifications sur le matériel** avec une vraie carte et une DualSense : vue en direct, retour haptique, barre lumineuse, attribution des touches, emplacements, gâchettes, compteurs audio, redémarrage et BOOTSEL, première installation, la mise à jour, des jeux.
- **Usage au quotidien :** le feu vert de la développeuse elle-même, après avoir utilisé la version au quotidien.

**Efficacité**

- **Des budgets qui bloquent la version :** au plus 10 images par seconde sur une page au repos, moins de 2 % d'un cœur de processeur visible et au repos, moins de 0,5 % cachée dans la zone de notification, au plus 275 Mo de mémoire et moins de 5 Mo de croissance en 30 minutes. Un budget dépassé se corrige, il ne se relève pas. Les derniers chiffres : [BUDGETS.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md).
- **Rendu sur le GPU :** les effets sont dessinés par la carte graphique, et rien n'est dessiné quand la fenêtre est cachée.
- **Firmware :** le chemin audio tourne depuis la RAM, et l'app montre combien de temps prend chaque partie du firmware.

**Sécurité et confiance**

- **Flux signé** (Ed25519) et téléchargements vérifiés par SHA-256, comme plus haut.
- **Téléchargements vérifiables :** chaque fichier d'une version a une attestation de compilation. `gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition` montre qu'il a été compilé par le workflow de publication de ce dépôt à partir de son tag.
- **App verrouillée :** fenêtre isolée (sandbox), Content Security Policy stricte, chaque message de la fenêtre vérifié pour son expéditeur et validé, accès USB limité à la carte et au chargeur d'amorçage du Pico. Versions exactes des dépendances, choisies au moins 2 semaines après leur sortie. L'audit : [SECURITY_AUDIT.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md).
- **Une vraie DualSense pour les jeux :** le descripteur USB de la carte est identique octet par octet à celui de Sony (289 octets), et l'interface du configurateur est à part, donc les jeux ne la voient jamais.

**Accessibilité et langues**

- **7 langues, complètes :** un test échoue s'il manque un texte dans une langue, et les fonctions de la manette portent les noms officiels de Sony dans chaque langue.
- **Lisible et nommé :** le vérificateur d'interface contrôle le contraste et le nom des commandes dans chaque langue ; le texte doit tenir à chaque taille.
- **À ton image :** modes de vision des couleurs, Réduire les animations et usage au clavier dans chaque menu déroulant.

---

## Présentation

Ce projet permet au Raspberry Pi Pico2W de servir de pont Bluetooth pour la manette DualSense, avec une connexion sans fil et une meilleure prise en charge de l'haptique.

## Fonctionnalités

**Pont de base (d'upstream) :**

- Connectivité DualSense complète via le Pico2W
- Haptique HD (retour de vibration avancé)
- Pont Bluetooth sans fil
- Gain haptique réglable via le volume du microphone
- LED et comportement de déconnexion configurables

**Ajouts de l'OLED Edition :**

- **Gâchettes adaptatives DualSense natives dans les jeux PC sous Linux/Proton, à travers la carte.** Les jeux qui prennent en charge la DualSense en natif pilotent les gâchettes adaptatives de la manette sans fil via la carte, **à l'identique d'une DualSense branchée par câble**. Nécessite un Proton qui contient le correctif Wine `winebus.sys` #9034 (Wine 11 / Proton-GE actuel) avec Steam Input désactivé ; **aucune option de lancement nécessaire** (Wine 11 active par défaut le chemin natif hidraw). Fonctionne **sur Steam et sur Heroic** (Epic/GOG). Le firmware rend la carte indiscernable, octet pour octet, d'une vraie DS5, pour que le jeu l'accepte (description complète dans [CHANGELOG.md](./CHANGELOG.md)). Contrepartie : les onglets Config/Remap de l'outil dans le navigateur sont désactivés avec ce firmware (voir la note ci-dessus).
- Écran d'état optionnel Pico-OLED-1.3 avec **11 écrans** (état, emplacements, barre lumineuse, test des gâchettes, inclinaison gyroscope, pavé tactile, diagnostic, CPU/horloge, RSSI, vumètres, réglages)
- **Calibrage dans la carte (v0.9)** : décalage du centre du joystick, zone inerte et rayon d'inclinaison maximale, course des gâchettes et zéro du gyroscope, par emplacement d'association, appliqués à ce que reçoivent les jeux (jamais écrits dans la manette). Se règle depuis le calibrage guidé de l'app.
- **Intensité des vibrations par côté (v0.9)** : gauche et droite séparément, pour le retour haptique comme pour la vibration classique.
- **Courbes de sensibilité (v0.9.5)** : une courbe par joystick (Linéaire, Précis, Régulier, Rapide, Numérique ou quatre points personnalisés), appliquée dans la carte après le calibrage, donc dans tous les jeux ; le profil d'un jeu peut activer la sienne pendant qu'il tourne.
- **Prise en charge de la DualSense Edge** : le firmware distingue l'Edge d'une DualSense grâce au rapport de firmware qu'elle envoie à la connexion ; sa touche Fn et ses touches arrière s'attribuent comme n'importe quelle autre touche, et l'écran État de l'OLED les affiche. Ses profils personnalisés s'affichent et s'enregistrent dans l'app PlayStation Accessories de Sony (upstream awalol 8a2f576 ; l'app peut avoir besoin de quelques secondes après la connexion de la manette). En mode DS5, l'app ne propose plus une mise à jour du firmware qui échoue (upstream awalol ae83907).
- **Infos de la manette (v0.9.5)** : l'app peut lire via la carte les versions du firmware propre de la manette, la tension de la batterie et les compteurs à vie (en lecture seule ; le numéro de série de la manette est effacé avant que l'app ne le voie).
- **Temps par étape (v0.9.5)** : la carte chronomètre chaque partie de son propre travail (Bluetooth, USB, audio, entrées, OLED, trames haut-parleur et micro de core1) pour la carte Temps d'exécution de l'app.
- **Miroir de l'écran OLED (v0.9)** : l'app affiche l'écran de la carte pixel pour pixel, avec ou sans le module OLED.
- **Attribution des touches** : réattribue n'importe laquelle des 16 commandes numériques (touches d'action, touches directionnelles, L1/R1/L2/R2, clics des joysticks, touche de création et touche d'options), ainsi que la touche Fn et les touches arrière de la DualSense Edge, à n'importe quelle autre. Stockée sur la carte et appliquée avant que l'hôte ne voie le rapport, elle fonctionne donc dans **tous les jeux et tous les systèmes, sans logiciel côté hôte** ; l'identité (aucune réattribution) est la valeur par défaut. Modifie-la visuellement sur la page **Attribution des touches** de l'[app de configuration](#️-pour-commencer--télécharger-lapp), ou sans interface via `scripts/remap_test.py`. Enregistrée dans son propre secteur de flash, elle survit au redémarrage.
- **Veille et réveil du PC (upstream awalol)** : la manette s'éteint quand le PC est en veille depuis 3 s (activé par défaut), et en option la touche PS réveille le PC (désactivé par défaut : sans manette connectée, la carte apparaît alors comme un petit clavier qui sert uniquement au réveil, « DS5 Dongle Wake Keyboard », que le PC doit autoriser à le réveiller). Avec une manette connectée, la carte est la DualSense exactement comme avant. Les deux se trouvent dans l'app et dans le menu Settings de l'OLED. Le réveil nécessite que le PC garde l'USB alimenté pendant la veille.
- **Intensité de l'effet gâchette, amplification du haut-parleur, verrouillage du volume, choix du micro et du haut-parleur, broche d'état (réglages d'upstream, protocole 1.13)** : des gâchettes adaptatives plus faibles en 10 crans (la force propre du jeu par défaut), le pré-gain de l'ampli de la manette, des jeux empêchés de changer le volume de la manette, le micro en Auto / Intégré / Casque, l'audio du PC en Auto / Haut-parleur / Casque / Désactivé, et une GPIO qui indique la connexion de la manette. Le voyant de la touche mode silencieux suit la coupure du micro, et la coupure du son et le niveau du micro du PC arrivent jusqu'à la manette. Une configuration neuve interroge à 500 Hz.
- **Association persistante de 4 manettes dans des emplacements** : associe jusqu'à quatre DualSense, passe de l'une à l'autre depuis l'OLED ; l'emplacement 0 se reconnecte automatiquement au démarrage
- **Sélecteur de couleur de la barre lumineuse** avec 4 emplacements favoris + préréglages d'effets respiration / arc-en-ciel / fondu
- **Menu de réglages persistant** pour les 8 champs de configuration du firmware (gain haptique, volume du haut-parleur, fréquence d'interrogation, etc.) avec des actions Reset et Wipe all slots à confirmer par appui long
- **Paliers d'économie de l'OLED au repos** : cycle de luminosité manuel (appui long sur KEY1), assombrissement profond automatique avec un petit point qui respire après 2 min d'inactivité, écran complètement éteint après 15 min. Se réveille instantanément sur une touche, une association de manette ou une entrée. Une vraie protection contre le marquage, pas un simple réglage de contraste.
- **Redémarrage logiciel** sans débrancher l'USB : maintien de `PS + Mute` sur la DS5 (fonctionne sans écran) ou **KEY0 + KEY1 maintenus ensemble 1 s** sur le module OLED (remplace l'ancien double-clic sur KEY0, trop facile à déclencher par accident en feuilletant vite)
- **Haut-parleur et micro de la manette propres** : la chaîne audio de core1 (Opus, rééchantillonneur, opérations mémoire) tourne depuis la RAM, l'audio part dans les rapports `0x39` groupés d'upstream, et le micro est décodé sur core1 et cadencé par l'horloge USB de l'hôte. Cela a supprimé le grésillement du haut-parleur de longue date et les saccades quand le micro est actif (voir [CHANGELOG.md](./CHANGELOG.md)). **Merci à [awalol](https://github.com/awalol/DS5Dongle)** : c'est le travail audio d'upstream, porté ici.
- **Audit du pont de base** : correctif critique d'un dépassement de pile dans la chaîne audio (résout les « saccades audio » de longue date), renforcement de la sécurité, watchdog, validation des longueurs aux frontières HID/L2CAP (voir [CHANGELOG.md](./CHANGELOG.md))

## 🐧 Linux : gâchettes adaptatives natives (Proton)

Pour qu'un jeu pilote les **gâchettes adaptatives de la manette à travers la carte** sous Linux, il faut un Proton qui contient le correctif Wine `winebus.sys` **#9034**. Les versions officielles de **GE-Proton 11** l'incluent désormais. La version fournie ici, **`GE-Proton11-7-DualSense`** (voir [sa version](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense)), est le `GE-Proton11-7` officiel plus un correctif `winebus.sys` supplémentaire : la DualSense annonce son numéro de série sous forme d'adresse MAC Bluetooth séparée par des deux-points, ce qui rend son chemin de périphérique impossible à ouvrir sous Windows, et les jeux qui utilisent la libScePad de Sony n'atteignent pas les gâchettes. Cette version remplace ces caractères pour que le chemin s'ouvre. C'est sur elle qu'ont été vérifiés les résultats ci-dessous.

> Pourquoi : le bug #9034 supprime le périphérique manette SDL quand un périphérique hidraw existe pour le même VID/PID, si bien que la manette est vue *partout sauf en jeu*. Le correctif laisse coexister le chemin natif (capable de piloter les gâchettes) et une manette qui fonctionne.

**Configuration unique**

1. **Installe la version de Proton.** Télécharge `GE-Proton11-7-DualSense.tar.gz` depuis [la version](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7-DualSense) et extrais-la dans :
   - **Steam :** `~/.steam/root/compatibilitytools.d/`
   - **Heroic :** `~/.config/heroic/tools/proton/`

   Puis **redémarre complètement Steam** pour qu'il détecte le nouvel outil de compatibilité.
2. **Désactive Steam Input** pour que le jeu parle directement à la manette : Steam → **Propriétés** du jeu **→ Manette → Désactiver Steam Input** (ou globalement : Paramètres → Manette → désactive la prise en charge des manettes PlayStation).

**Pour chaque jeu**

3. **Force le Proton :** Steam → clic droit sur le jeu → **Propriétés → Compatibilité → Forcer l'utilisation d'un outil de compatibilité Steam Play spécifique → `GE-Proton11-7-DualSense`**. (Heroic : règle la *version de Wine* du jeu sur celui-ci.)

**Aucune option de lancement n'est nécessaire** : Wine 11 active par défaut le chemin natif hidraw, la carte est donc remise au jeu automatiquement. Lance simplement le jeu : les gâchettes adaptatives natives fonctionnent à travers la carte, à l'identique d'une DualSense filaire (vérifié sans option sur Cyberpunk 2077, Uncharted, Spider-Man Remastered, The Last of Us Part I, Avatar, Indiana Jones, Star Wars: Galactic Racer, et **sur Steam comme sur Heroic**). Le jeu doit avoir une **prise en charge native de la DualSense** (les jeux uniquement XInput donnent des vibrations mais pas de gâchettes adaptatives : c'est une limite du moteur, pas de la carte). Le premier lancement avec un nouveau Proton prépare la langue et le préfixe et peut rester « Ne répond pas » pendant une minute : c'est de la préparation, pas un plantage ; laisse-le finir.

**Laisse l'audio de la manette sur son profil par défaut.** GE-Proton 11 gère lui-même l'haptique de la DualSense et attend la carte son sur le profil standard **HiFi**. Une règle WirePlumber/PulseAudio qui force la DualSense sur **pro-audio** (une astuce courante pour d'anciennes versions de Proton) a fait envoyer à Avatar des gâchettes sans force et perdre l'haptique HD (vérifié ; les autres jeux portés par Sony se comportent probablement pareil). Supprime une telle règle, ou vérifie avec `pactl list cards` que la DualSense affiche `Active Profile: HiFi (...)`.

### Heroic (Epic / GOG) : même recette, deux exigences en plus

Les gâchettes natives fonctionnent de la même façon sur Heroic, mais les lanceurs hors Steam sont sensibles à deux choses côté hôte qui volent la manette sans rien dire :

1. **Quitte complètement Steam** : un Steam en arrière-plan accapare la DualSense au détriment des jeux hors Steam (ou désactive Paramètres → Manette → prise en charge des manettes PlayStation).
2. **Pas de `PROTON_PREFER_SDL` global** : cette variable d'environnement force le chemin SDL/Xbox et *supprime* les gâchettes natives. Utilise-la seulement par jeu, comme solution de repli volontaire vers une manette générique.

**Référence des options de lancement**

| Variable | Valeur | Rôle |
|---|---|---|
| `PROTON_ENABLE_HIDRAW` | `0x054c/0x0ce6` | *Ancienne / facultative : **inutile** sous Wine 11*, qui active par défaut le chemin natif hidraw. Sans effet nuisible si elle est définie. Utile seulement sur d'anciens Proton où hidraw était à activer (mais ceux-ci n'ont pas non plus le correctif #9034, donc les gâchettes n'y fonctionnent de toute façon pas). |
| `PROTON_PREFER_SDL` | `1` | *Alternative, entrées seulement.* Force le chemin manette SDL : entrées et vibrations fonctionnent, mais comme une manette générique/Xbox (**pas de gâchettes**). À utiliser seulement quand un jeu n'a pas de prise en charge native de la DualSense et que `ENABLE_HIDRAW` n'aide pas. Ne la **combine pas** avec `ENABLE_HIDRAW` pour les jeux à gâchettes. Nécessite un Proton qui la respecte : le **GE-Proton11-7-DualSense** publié l'ignore, donc lance les jeux uniquement XInput avec un GE-Proton standard jusqu'à la publication de la prochaine version DualSense. |
| `PROTON_LOG` | `+hid` | *Diagnostic uniquement* (pas pour jouer normalement). Écrit une trace de l'énumération HID : Steam la place dans `~/steam-<appid>.log` ; Heroic règle `PROTON_LOG_DIR` sur ton dossier personnel, elle arrive donc dans `~/steam-0.log`. Utile pour confirmer que le jeu a ouvert le chemin HID natif. |

> **Heroic ou Steam :** les gâchettes natives fonctionnent sur **les deux**, et aucun n'a besoin d'option de lancement sous Wine 11. Steam : choisis le runner + désactive Steam Input. Heroic : choisis le runner, **quitte complètement Steam** (un Steam en arrière-plan vole la manette aux jeux hors Steam) et laisse `PROTON_PREFER_SDL` désactivé. Dans tous les cas, le jeu doit prendre en charge la DualSense en natif : les jeux uniquement XInput (par ex. Ghostrunner, Control) donnent des vibrations mais pas de gâchettes adaptatives, sur n'importe quel système. Ces jeux ont besoin de `PROTON_PREFER_SDL=1` et d'un Proton qui la respecte : le **GE-Proton11-7-DualSense** publié ne la respecte pas, utilise donc un GE-Proton standard pour eux jusqu'à la publication de la prochaine version DualSense.

## Matériel

### Indispensable

| Élément | Remarques | Prix approx. |
|---|---|---|
| **Raspberry Pi Pico 2 W** | MCU RP2350 avec Bluetooth/WiFi CYW43 intégré. [Page officielle du produit](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) | ~7 USD |
| **Manette Sony DualSense** | N'importe quelle DualSense PS5 (VID `054C:0CE6`) ou DualSense Edge (`054C:0DF2`). | — |
| **Câble USB-C** | Relie le Pico 2 W au PC hôte. | — |

### Facultatif (fortement recommandé)

| Élément | Remarques | Prix approx. |
|---|---|---|
| **Waveshare Pico-OLED-1.3** | Module OLED SH1107 128×64 (SKU HIPI1798). Se branche directement sur les broches du Pico 2 W. Le firmware le pilote automatiquement s'il est présent et ne fait simplement rien s'il est absent. [Page du produit](https://www.waveshare.com/pico-oled-1.3.htm) · [Wiki](https://www.waveshare.com/wiki/Pico-OLED-1.3) | ~6 USD |
| **Petit dissipateur** pour le RP2350 | Inutile : le firmware fait tourner le RP2350 à sa fréquence nominale de 150 MHz et à sa tension par défaut (voir [Performances / Horloge](#performances--horloge)). Sans inconvénient si tu en as déjà un. | 1–3 USD |

### Où acheter

Le Pico 2 W et le Waveshare Pico-OLED-1.3 sont tous deux faciles à trouver dans le monde entier :

- **Adafruit**, **Pimoroni**, **The Pi Hut**, **DigiKey**, **Mouser** : grands distributeurs d'électronique (US / UE)
- **La boutique de Waveshare** pour le module OLED
- Les boutiques **Amazon** régionales : cherche `Raspberry Pi Pico 2 W` et `Waveshare Pico-OLED-1.3` (ou le SKU `HIPI1798`)
- **AliExpress** : stock Waveshare et Pico d'origine plus des clones ; vérifie les notes des vendeurs

## Prise en main

### Flasher le firmware

L'[app](#️-pour-commencer--télécharger-lapp) le fait pour toi. Pour flasher à la main (ou pour la version Pico W, qui est un artefact de CI) :

1. Maintiens le bouton BOOTSEL du Pico2W
2. Branche le Pico2W à ton ordinateur en USB
3. L'appareil apparaît comme un périphérique de stockage USB
4. Glisse-dépose le fichier firmware .uf2 (depuis les [versions](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases)) sur le périphérique

### Associer la manette

1. Mets la manette DualSense en mode d'association Bluetooth
2. Attends que le Pico2W la détecte et se connecte
3. Une fois connecté, l'appareil apparaît sur le système hôte

## Configuration

Il y a quatre façons de configurer le firmware :

**App de configuration (recommandée) :** le [DS5 Dongle OLED Edition Configurator](#️-pour-commencer--télécharger-lapp) pour Linux et Windows (une version macOS est incluse, signalée fonctionnelle sous macOS 27.0.1 ; la page Jeux liste les jeux Steam, Heroic et Lutris sous Linux et les jeux Steam, GOG Galaxy et Epic sous Windows, et sa configuration de Proton est réservée à Linux). Elle trouve la carte toute seule, applique les changements en direct et enregistre dans la flash de la carte en un clic. Nécessite le firmware v0.8+.

**Sur l'appareil (module OLED présent) :** utilise le menu **Settings** à l'écran (11e écran). ▲▼ des touches directionnelles déplacent la sélection, ▶◀ ajustent les valeurs, △ enregistre en flash.

**CLI en terminal (tout système, tout navigateur) :** installe hidapi, puis utilise `scripts/set_ds5.py` :

```bash
pip install hidapi
scripts/set_ds5.py                            # afficher la configuration actuelle
scripts/set_ds5.py --auto-haptics fallback    # changer un champ, l'enregistrer en flash
scripts/set_ds5.py --speaker-volume -10 --haptics-gain 1.5
scripts/set_ds5.py --slot 2                   # changer d'emplacement d'association actif
scripts/set_ds5.py --version                  # version du firmware
scripts/set_ds5.py --rssi                     # signal BT en direct, en dB par rapport à la plage idéale (0 = idéal)
scripts/set_ds5.py --help                     # liste complète des options
```

Le script parle au firmware via les rapports de fonctionnalité USB HID `0xF6`/`0xF7`/`0xF8`/`0xF9`. Depuis la v0.6.12, ces rapports ne sont plus déclarés dans le descripteur HID (les jeux exigent qu'il soit identique octet pour octet à celui d'une vraie DualSense), le script ne fonctionne donc que sous Linux ; ailleurs, utilise l'app de configuration. Porté depuis [loteran/DS5Dongle](https://github.com/loteran/DS5Dongle) et étendu pour le champ `current_slot` de ce fork.

**Touches de la manette DualSense (ancienne solution de repli, sans OLED ni CLI) :**

### Volume du microphone

Règle le multiplicateur de gain haptique. Plage : `[1.0 – 2.0]`.

### Coupure du haut-parleur

Désactive le voyant LED de connexion. Prend effet après la reconnexion de la manette.

### Coupure du microphone

Désactive la déconnexion silencieuse.

## Remarques

Le Pico n'est visible pour le système qu'une fois la manette connectée

Certains comportements ne prennent effet qu'après un cycle de reconnexion

### Voyant LED de batterie faible

Quand la DualSense connectée annonce une batterie à 10 % ou moins (et qu'elle ne charge pas), la LED intégrée du Pico passe d'allumée en continu à un clignotement à 1 Hz, pour que tu voies l'avertissement d'un coup d'œil. La LED redevient allumée en continu dès que la manette est branchée ou que son niveau annoncé remonte. Le clignotement se déclenche aussi quand `disable_pico_led` est défini : l'avertissement est considéré comme critique et passe outre la préférence LED éteinte ; la LED revient à son état désactivé (éteint) une fois la batterie rétablie ou la manette en charge.

Pour le désactiver à la compilation, configure avec `-DENABLE_BATT_LED=OFF`. Activé (ON) par défaut.

## Microphone de la DualSense en Bluetooth

**Le microphone intégré de la DualSense fonctionne via l'association Bluetooth de la carte** (depuis la v0.6.8). La manette envoie son micro en audio Opus ; la carte le décode et le présente à l'hôte comme le périphérique de capture USB standard de la DualSense, donc n'importe quelle app (Discord, OBS, chat vocal en jeu) peut l'utiliser comme un micro normal.

On l'a longtemps cru impossible : les premières versions de ce fork le décrivaient comme une limite stricte du firmware de Sony (et le pilote noyau Linux `hid-playstation` ne le prend toujours pas en charge). Tout tenait en fait à un seul bit d'activation dans le rapport audio sortant de la carte. **Tout le mérite revient à [awalol](https://github.com/awalol/DS5Dongle) (upstream), qui l'a identifié.** Le journal d'enquête corrigé se trouve dans [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md).

**Utilisation :**

- **Activé par défaut.** Associe la manette et le micro commence à émettre en une seconde ou deux, sans audio de jeu nécessaire (la carte maintient le flux elle-même).
- **Monte le volume de capture sur l'hôte** : il est bas par défaut. Sous Linux, trouve la carte son avec `arecord -l`, puis par ex. `amixer -c <DualSense card> sset 'Headset' 90%`. Vérifie la capture avec `scripts/mic_diag.sh capture`.
- **Désactive-le pour économiser la batterie de la manette** : sur l'OLED **Settings → BT Mic**, ou l'interrupteur **Microphone intégré** dans l'[app de configuration](#️-pour-commencer--télécharger-lapp). Un micro toujours actif garde le sous-système audio de la DS5 éveillé, ce qui vide sa batterie nettement plus vite ; désactive-le si tu n'utilises pas la voix.

**Limites :**

- L'audio du micro est **mono** (décodé en mono, dupliqué sur le point de capture stéréo).
- Le désactiver en cours de session coupe immédiatement le flux vers l'hôte, mais la manette continue d'émettre jusqu'à sa prochaine reconnexion (aucune commande « stop » connue) ; une nouvelle connexion avec l'interrupteur désactivé ne l'active jamais.
- Le compteur `Mic in:` de l'écran **Diagnostics** de l'OLED affiche ~100/s pendant que le micro émet : un moyen rapide de confirmer qu'il est actif.
- **Masquage des pertes de paquets :** les trames de micro perdues (lien BT faible, distance, interférences) sont masquées avec le PLC d'Opus pour que la voix reste continue au lieu de cliquer ou de se couper, au prix d'une petite latence de tampon de gigue (~30 ms). Le compteur `Mic PLC:` de l'écran Diag ne monte que quand des trames sont masquées : en pratique, une jauge de qualité du lien en direct.

## Problèmes connus

- L'haptique HD peut ne pas fonctionner dans tous les jeux sous Linux + Steam ; cela vient du jeu (certains titres n'envoient l'audio haptique HD que via des API propres à Windows). Testée et fonctionnelle dans Spider-Man Remastered ; non transmise dans Ghost of Tsushima, avec le même firmware et la même manette.
- **Les ports USB 3.0 peuvent perturber l'association** : la manette peut rester bloquée sur une barre lumineuse ambre/jaune fixe sans jamais se connecter, alors que la même carte fonctionne bien sur un port USB 2.0. Ce sont des interférences radio, pas un bug du firmware ; voir [Ports USB 3.0 et interférences Bluetooth](#ports-usb-30-et-interférences-bluetooth) plus bas. (Depuis la v0.6.8, le firmware relance automatiquement une connexion bloquée au lieu de rester figé, ce qui récupère beaucoup de cas limites, mais pas tous.)
- **Windows : l'app n'arrive pas à ouvrir la carte** sur les PC où une vraie DualSense a déjà été branchée en USB (la carte partage ses identifiants USB, et Windows a retenu « aucun pilote nécessaire » pour eux). Depuis l'app 0.9.2, l'écran de configuration propose **Réparer le pilote USB** : une demande administrateur, puis on rebranche. Le PowerShell complet qu'il exécute, ligne par ligne, se trouve dans le [README de l'app](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web#repair-usb-driver-exactly-what-it-runs).

## Ports USB 3.0 et interférences Bluetooth

Si la carte fonctionne sur un port mais pas sur un autre, **essaie d'abord un port USB 2.0.** Les ports USB 3.0 et (surtout) les rallonges USB 3.0 émettent un bruit radio large bande centré près de 2,4 GHz, la même bande que la radio Bluetooth de la carte utilise pour parler à la manette. C'est un problème bien documenté dans l'industrie (Intel, *"USB 3.0 Radio Frequency Interference Impact on 2.4 GHz Wireless Devices"*), pas propre à ce firmware. Le bruit désensibilise le récepteur BT de la carte : la manette peut commencer à se connecter (barre lumineuse ambre) mais le lien est trop bruité pour aboutir, et elle reste bloquée sur le jaune.

Solutions, à peu près par ordre d'efficacité :

1. **Branche la carte sur un port USB 2.0** (souvent la solution la plus simple : beaucoup de cartes mères/boîtiers ont les deux).
2. **Utilise une courte rallonge USB 2.0** pour éloigner la carte de quelques centimètres des ports USB 3.0 / du châssis métallique et améliorer la ligne de vue vers la manette. Évite précisément les rallonges USB 3.0.
3. **Utilise un hub USB 2.0 alimenté** branché sur le port USB 3.0 : le hub fait redescendre le lien en USB 2.0 et ajoute de la distance.
4. **Pose une ferrite** sur le câble près de la carte.
5. **Garde la manette plus près / en ligne de vue** de la carte pendant l'association.

Le firmware relance de lui-même une connexion bloquée ; laisser la carte branchée ~10–20 s après le passage de la barre lumineuse à l'ambre peut lui permettre de récupérer sans débrancher.

## Performances / Horloge

**Tu n'as rien à faire pour ça.** Le Pico 2 W tourne avec les mêmes réglages qu'upstream awalol/DS5Dongle v0.7.0 :

- **Horloge : 150 MHz**, la fréquence nominale du RP2350 (celle avec laquelle il démarre)
- **Tension du cœur : 1,10 V**, la valeur par défaut. Le firmware la règle au démarrage, car le RP2350 garde la tension d'un ancien firmware jusqu'à ce qu'il soit débranché.
- **Bus de la puce Bluetooth (CYW43 PIO SPI) : diviseur d'horloge 2**
- **Flash : 75 MHz** (diviseur 2), configurée par le firmware lui-même au démarrage (`PICO_EMBED_XIP_SETUP`). La laisser au réglage du bootrom faisait planter core1 sur certaines cartes ([issue #15](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/15)).

Les anciennes versions overclockaient à 320 MHz sous 1,20 V, un réglage hérité d'upstream. Upstream est passé à 150 MHz une fois la chaîne audio exécutée depuis la RAM, et ce fork aussi, maintenant que la chaîne audio et le chemin critique Bluetooth/USB de core0 tournent tous deux depuis la RAM. 1,20 V dépasse le maximum de fonctionnement de 1,16 V du RP2350, et aucun dissipateur n'est nécessaire à 150 MHz.

Pendant une version encore, une compilation depuis les sources peut utiliser l'ancien overclock en solution de repli : `-DDS5_CLOCK_KHZ=320000` (1,20 V, diviseur du bus Bluetooth 5, diviseur de flash 4). La compilation refuse une configuration Pico 2 W sans la configuration flash embarquée ou avec la flash au-dessus de 100 MHz. La version Pico W ne change pas (200 MHz).

## Instructions de compilation

Pour compiler le projet depuis les sources :

1. Installe le Pico SDK **2.3.0**. La compilation utilise `pico_sdk_import.cmake`. La CI et les versions publiées compilent avec la chaîne d'outils GNU officielle d'ARM **15.2.Rel1** (`arm-none-eabi`) ; le `gcc-arm-none-eabi` d'une distribution fonctionne généralement, mais 15.2.Rel1 donne le même binaire que la version publiée. Les versions exactes (SDK, commit TinyUSB, téléchargement de la chaîne d'outils) sont dans `tools/toolchain.env`. Définis `PICO_TOOLCHAIN_PATH` sur le dossier de la chaîne d'outils décompressée si elle n'est pas dans ton `PATH`.
2. **Fixe TinyUSB au commit indiqué dans `tools/toolchain.env`** dans le Pico SDK (`$PICO_SDK_PATH/lib/tinyusb`) : TinyUSB 0.21 plus des correctifs ultérieurs, comme upstream. Le Pico SDK 2.3.0 embarque encore la 0.18.0, qui n'a pas la forme à 4 arguments de `TUD_AUDIO_EP_SIZE` utilisée par le `tusb_config.h` de ce projet, et la compilation refuse tout TinyUSB autre que 0.21 :
   ```bash
   cd "$PICO_SDK_PATH/lib/tinyusb"
   git fetch --depth 1 origin 2d56dc533e45e4e91b15e93fdab5e22e964f328d
   git checkout --detach FETCH_HEAD
   ```
3. Configure et compile avec CMake + Ninja (ou Make) :
   ```bash
   cd /path/to/DS5Dongle
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
   cmake --build build --target ds5-bridge
   ```
4. Le UF2 se trouve dans `build/ds5-bridge-oled.uf2`. Flashe-le avec BOOTSEL comme d'habitude.

Options de compilation utiles :

- `-DENABLE_BATT_LED=ON` (par défaut) : fait clignoter la LED du Pico quand la batterie de la DS5 est faible.
- `-DENABLE_SERIAL=ON` : envoie printf vers l'USB CDC pour le débogage (OFF par défaut ; libère l'UART pour les versions de production).
- `-DPICO_W_BUILD=ON` : compile pour le Pico W d'origine (sans audio, horloge plus basse). Par défaut, la cible est le Pico 2 W.

## Diagnostic et outils de débogage

Deux façons d'analyser les problèmes du pont : sur l'appareil via l'écran Diagnostics de l'OLED, et côté hôte via `scripts/mic_diag.sh` (Linux). Le côté hôte est plus rapide : pas de changement d'écran, pas de cycle de flash, et il tourne pendant que la manette est utilisée.

```
# Instantané unique : la carte est-elle sur l'USB ? ALSA l'a-t-il énumérée ? Le
# flux de capture est-il actif ? Une manette est-elle associée en ce moment ?
scripts/mic_diag.sh status

# arecord de 3 secondes sur le point IN du micro : indique crête / RMS / nombre
# d'échantillons non nuls, pour distinguer « flux silencieux » de « flux qui produit de l'audio ».
scripts/mic_diag.sh capture 3

# Comme `status` mais en boucle, n'affiche que les changements d'état. Utile pour
# saisir la seconde exacte où l'association aboutit ou où les flux audio s'ouvrent / se ferment.
scripts/mic_diag.sh watch

# Lecture en direct du rapport de fonctionnalité fournisseur 0xFD du firmware (via /dev/hidraw) :
# compteurs et débits d'entrée BT, derniers ID non-0x31 vus, préfixes d'octets, ET les
# compteurs du flux des gâchettes (0x02 de l'hôte reçus / avec AllowTriggerFFB défini /
# transmis en BT). bt-trace donne un verdict, « le pilote hôte n'envoie pas les bits
# Allow des gâchettes » ou « transmis mais la manette n'a pas réagi », ce qui
# demanderait sinon un analyseur de protocole USB.
scripts/mic_diag.sh bt-trace
```

Écrit à l'origine pour analyser l'enquête mise en pause sur le microphone BT de la DS5 (voir [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)). Le rapport de fonctionnalité `0xFD` et le décodeur `bt-trace` portent désormais aussi les compteurs du flux des gâchettes ajoutés pour l'[issue #3](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/3) (tension des gâchettes adaptatives absente dans certains jeux).

## Module d'affichage OLED (optionnel)

Si tu branches un [Waveshare Pico-OLED-1.3](#matériel) sur les broches du Pico2W, le firmware le pilote automatiquement comme écran d'état en direct. Aucune configuration : le firmware ne fait simplement rien quand aucun OLED n'est présent.

<img src="./assets/oled/oled-status-live.gif" alt="L'écran État en direct sur l'OLED" width="512">

L'écran État en direct sur une vraie carte, capturé pixel par pixel via le miroir de l'OLED. Toutes les images d'écran ci-dessous sont elles aussi de vraies captures de la carte.

### Écran de démarrage (1,5 s à la mise sous tension)

Version du firmware centrée sur un écran vide pendant 1,5 seconde, puis passage à l'écran État.

### Onze écrans, parcourus avec KEY0 sur le module

Ordre du cycle : **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → CPU/Clock → BT Signal → VU Meters → Settings →** retour au début. **Un appui court sur KEY0 avance ; un appui court sur KEY1 recule**, sur *chaque* écran. Les interactions propres à chaque écran (changer le préréglage des gâchettes, changer le mode de la barre lumineuse, déplacer le curseur de Settings, changer d'emplacement) se font avec les **touches de la manette DualSense**, jamais avec KEY0/KEY1 ; les deux boutons physiques du module OLED veulent donc toujours dire la même chose.

Chaque écran affiche aussi **`>`** sur le bord en haut à gauche (à côté de KEY0) et **`<`** sur le bord en bas à gauche (à côté de KEY1), pour que les repères à l'écran correspondent physiquement aux boutons.

#### 1. État

État de la connexion, adresse BD de la DualSense associée, batterie en % avec une barre (`+` en charge / `*` charge terminée / `!` erreur), position des joysticks en direct (chaque cadre de joystick clignote en inversé, un point noir sur un cadre blanc, tant que son **L3 / R3** est enfoncé), touches directionnelles, touches d'action (△ ◯ ✕ □), L1/R1, et barres de remplissage des gâchettes analogiques L2/R2. L'indicateur de lien et la batterie utilisent de petites icônes en pixels. Pendant la charge, une estimation `~Nm` du temps restant jusqu'à 100 % apparaît à côté de la batterie.

<img src="./assets/oled/oled_sc01.png" alt="Écran État sur l'OLED" width="512">

#### 2. Emplacements

Association persistante de 4 manettes dans des emplacements. Parcours les manettes enregistrées, change d'emplacement actif ou efface un seul emplacement. `>` est le curseur, `*` marque l'emplacement actif.

<img src="./assets/oled/oled_sc02.png" alt="Écran Emplacements sur l'OLED" width="512">

- **Touches directionnelles ▲▼** : déplacent le curseur sur les emplacements 0–3
- **△** : passe à l'emplacement sous le curseur (déconnecte la manette actuelle, reconnecte celle enregistrée dans l'emplacement)
- **□ maintenu 1,5 s** : efface l'emplacement sous le curseur (supprime bd_addr + la clé de lien BTstack)
- L'emplacement actif est enregistré ; la carte s'y reconnecte au démarrage suivant

#### 3. Sélecteur de couleur de la barre lumineuse

Incline la manette sur chaque axe pour régler R / G / B ; le firmware envoie la couleur obtenue à la vraie barre lumineuse de la DualSense à 10 Hz, la barre lumineuse EST donc l'aperçu (l'OLED est monochrome).

<img src="./assets/oled/oled_sc03.png" alt="Sélecteur de couleur de la barre lumineuse sur l'OLED" width="512">

- Appuie sur **△ ◯ ✕ □** sur la manette pour **enregistrer** la couleur actuelle dans l'emplacement favori 0 / 1 / 2 / 3
- Appuie sur **R1** sur la manette pour changer d'étiquette de mode : `[LIVE]` → `[FAV0]` → `[FAV1]` → `[FAV2]` → `[FAV3]` → effets (Breathing / Rainbow / Fade) → `[HOST]` → retour à `[LIVE]`
- `[HOST]` (par défaut) laisse la barre lumineuse au PC / au jeu ; la carte ne définit aucune couleur. Tout autre mode garde la couleur de la carte, quoi que le jeu envoie
- Favoris par défaut : rouge, vert, bleu, blanc

#### 4. Test des gâchettes

Appuie sur **△** sur la manette pour parcourir sept effets de gâchettes adaptatives appliqués à L2 et R2. Presse chaque gâchette pour sentir l'effet.

<img src="./assets/oled/oled_sc04.png" alt="Écran Test des gâchettes sur l'OLED" width="512">

Ordre du cycle : **Off → Feedback → Weapon → Vibration → Bow → Gallop → Machine Gun → Off …** Paramètres des effets empaquetés en bits d'après la rétro-ingénierie de [dualsensectl](https://github.com/nowrep/dualsensectl), tous à la force maximale.

#### 5. Inclinaison gyroscope

Valeurs X/Y/Z de l'accéléromètre en direct avec un cadre à réticule de 40×40. Le point suit l'inclinaison de la manette en temps réel et **reste centré quand la manette est posée à plat** : il s'appuie sur le calibrage IMU d'usine propre à chaque manette (lu dans le rapport de fonctionnalité `0x05`), donc la position de repos et le gain sont justes sur chaque manette. Incliner à gauche/droite et en avant/arrière déplace le point dans le sens correspondant.

<img src="./assets/oled/oled_sc05.png" alt="Écran Inclinaison gyroscope sur l'OLED" width="512">

#### 6. Pavé tactile

Rendu en direct de la surface du pavé tactile. Des points apparaissent aux positions actuelles des doigts ; le compteur se met à jour quand les doigts touchent / quittent le pavé.

<img src="./assets/oled/oled_sc06.png" alt="Écran Pavé tactile sur l'OLED" width="512">

#### 7. Diagnostic

Liste défilante de compteurs en direct : temps de fonctionnement, état BT, flux des gâchettes hôte → BT (`host02` / `trig` / `tx`), débit d'entrée BT 0x31, trames audio USB/s, paquets BT 0x32/s, et en bas les compteurs de l'enquête micro mise en pause. ▲/▼ des touches directionnelles de la manette font défiler ; de petits `^` / `v` sur le bord droit signalent « encore au-dessus/en dessous ». En lecture seule, donc sans curseur. Utile pour vérifier que le pont transfère bien des octets sans câble UART.

Les mêmes compteurs sont aussi exportés dans le rapport de fonctionnalité HID `0xFD` pour les outils côté hôte : voir `scripts/mic_diag.sh bt-trace` plus bas.

<img src="./assets/oled/oled_sc07.png" alt="Écran Diagnostic sur l'OLED" width="512">

#### 8. CPU / Horloge

Constantes vitales du RP2350 en direct : horloge système configurée (`Set`) et réellement utilisée (`Real`), mesurée par rapport au quartz de référence, tension du cœur relue au régulateur, et température de la puce (moyenne sur 256 échantillons + EMA lente, pour que la valeur suive la vraie température de la puce et non le bruit de l'ADC).

<img src="./assets/oled/oled_sc08.png" alt="Écran de diagnostic CPU / Horloge sur l'OLED" width="512">

La même télémétrie est aussi exportée dans le rapport de fonctionnalité HID `0xFC` pour les outils.

#### 9. Signal BT

Puissance du signal Bluetooth du lien actif en direct, en dB par rapport à la plage idéale (0 = idéal, affiché *in range* ; plus c'est négatif, plus c'est faible), avec une barre de −30 dB (vide) à 0 dB (pleine) et les mots de l'app (Excellent / Good / Fair / Weak, soit Excellent / Bon / Moyen / Faible). Avant la première mesure, ou si une mesure échoue, il affiche `Signal: --` et `Link: unknown`.

<img src="./assets/oled/oled_sc09.png" alt="Écran Signal BT sur l'OLED" width="512">

#### 10. Vumètres

Crête-mètres en direct pour les chaînes audio du haut-parleur et de l'haptique. Utile pour vérifier le routage audio sans que la manette soit branchée à un hôte.

<img src="./assets/oled/oled_sc10.png" alt="Écran Vumètres sur l'OLED" width="512">

#### 11. Réglages

Éditeur de configuration persistante. ▲▼ des touches directionnelles déplacent la sélection, ▶◀ ajustent les valeurs, △ enregistre en flash. Contient les champs de configuration du firmware (gain haptique, volume du haut-parleur, délai d'inactivité, fréquence d'interrogation), les réglages Audio Auto Haptics, et deux actions à confirmer par appui long :

`Hap Left` et `Hap Right` règlent chaque poignée séparément. Appuie sur △ pour appliquer et enregistrer les modifications.

<img src="./assets/oled/oled_sc11.png" alt="Écran Réglages sur l'OLED" width="512">

- **AutoHap Off / Fallback / Mix / Replace** : choisit le mode Audio Auto Haptics. Par défaut `Off` : l'haptique vient uniquement du jeu. `Fallback` ne déclenche des vibrations dérivées que quand le jeu n'envoie aucune donnée haptique native (par ex. Ghost of Tsushima sous Linux) ; note qu'il vibre aussi avec la musique et l'audio du bureau ; les jeux qui *envoient* de l'haptique native (Spider-Man Remastered) passent sans changement. `Mix` ajoute le signal dérivé par-dessus le natif, `Replace` ignore entièrement le natif.
- **AH Gain N%** : gain du signal dérivé, 0–200 % par pas de 10 %. Par défaut 100 %.
- **AH LP 80/160/250/400 Hz** : fréquence de coupure du passe-bas appliqué à l'audio du haut-parleur avant le suivi d'enveloppe. Plus bas = plus d'infra-basses, plus haut = plus de présence. Par défaut 160 Hz.
- **TrigEff game / -1 … -10** : Intensité de l'effet gâchette ; affaiblit la résistance des gâchettes adaptatives des jeux en 10 crans. Par défaut `game` (0) : la force propre du jeu, son rapport transmis tel quel.
- **SpkBoost game / 1 … 7** : Amplification du haut-parleur ; le pré-gain de l'amplificateur de la manette. Par défaut `game` (0) : laissé au jeu. Distinct de `Spk Vol`.
- **VolLock on / off** : Verrouiller le volume ; les jeux ne peuvent pas changer le volume du haut-parleur, du casque ou du micro de la manette, ni la coupure du son ou son voyant ; le volume et la coupure du son du PC lui-même s'appliquent toujours. Désactivé par défaut.
- **SPK Out Auto / SPK / Hset / off** : Sortie audio ; où l'audio du PC est joué (Auto = le casque quand il y en a un branché sur la manette). Par défaut Auto.
- **Mic In Auto / Int / Hset** : Microphone utilisé ; Auto, le micro intégré ou celui d'un casque. Par défaut Auto. `BT Mic` reste l'interrupteur qui coupe le micro.
- **Sleep CtrlOff on / off** : éteint la manette quand le PC est en veille depuis 3 s. Activé par défaut.
- **PS Wakes PC on / off** : sans manette connectée, la carte apparaît comme un clavier qui sert uniquement au réveil (« DS5 Dongle Wake Keyboard ») pour que la touche PS puisse réveiller le PC. Désactivé par défaut.
- **Reset to defaults** : maintiens △ pendant 2 s pour rétablir tous les champs de configuration
- **Wipe all slots** : maintiens △ pendant 2 s pour supprimer les 4 manettes associées + toutes les clés de lien BTstack

### Référence des boutons

Les deux boutons physiques du module OLED servent **uniquement à la navigation** :

| Bouton | Action |
|---|---|
| **KEY0** appui court | Écran suivant (avancer) |
| **KEY1** appui court | Écran précédent (reculer) |
| **KEY1** maintenu plus de 1,5 s | Change la luminosité une fois pendant l'appui ; relâcher ne revient pas en arrière |
| **KEY0 + KEY1** maintenus ensemble ≥ 1 s | `watchdog_reboot` : redémarrage logiciel sans débrancher l'USB |

Les changements d'état propres à chaque écran (changer le préréglage des gâchettes, changer le mode de la barre lumineuse, naviguer dans Settings, changer d'emplacement, enregistrer des couleurs dans les emplacements favoris) se font tous avec les **touches de la manette DualSense**, jamais avec KEY0 / KEY1 ; les deux boutons physiques veulent donc toujours dire la même chose sur chaque écran. Voir la section de chaque écran plus haut pour savoir quelle touche de la manette fait quoi.

### Brochage (disposition standard des HAT Pico de Waveshare)

| Fonction | GPIO |
|---|---|
| MOSI | 11 |
| SCK  | 10 |
| CS   | 9  |
| DC   | 8  |
| RST  | 12 |
| KEY0 | 15 |
| KEY1 | 17 |

### Récupération par redémarrage logiciel

Deux façons de redémarrer la carte sans débrancher l'USB, pratiques si l'association reste bloquée ou si tu veux repartir d'un état propre :

- **KEY0 + KEY1 de l'OLED maintenus ensemble ≥ 1 s** → `watchdog_reboot`. Remplace l'ancien geste « double-clic sur KEY0 » des premières versions, car la navigation rapide vers l'avant déclenchait sans cesse le minuteur du double-clic par accident.
- **`PS + Mute` de la DualSense maintenus 2 secondes** → `watchdog_reboot` (fonctionne avec ou sans OLED : solution de secours sans écran).

## Remerciements

Certaines fonctions et idées de conception de ce fork viennent d'autres forks d'upstream, avec nos remerciements :

- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)** : les icônes en pixel art de l'en-tête d'état de l'OLED (approche visuelle), le principe « maintenir pour réinitialiser » utilisé par l'élément « Reset to defaults » de l'écran Réglages (maintenir △ 2 s pour confirmer), et le système d'association BT persistante dans plusieurs emplacements du nouvel écran Emplacements (4 manettes associées, touches directionnelles pour naviguer, △ pour changer d'emplacement, □ maintenu pour effacer un emplacement, plus « Wipe all slots » dans le menu Settings).
- **[loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)** : redécouverte indépendante de la régression d'upstream `3a31bd7` qui cassait la sortie haut-parleur / haptique HD (commit `c7a8d3c`) ; le correctif dans notre `src/audio.cpp` rétablit le même sous-rapport SetStateData. C'est aussi la source du DSP d'Audio Auto Haptics (passe-bas à 1 pôle + suiveur d'enveloppe, voir Settings → Auto Haptics) et du correctif « ne pas synchroniser le volume UAC1 côté USB avec la configuration persistante ».
- **[SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge)** : un pont DualSense Pico frère. La logique d'application de la réattribution des touches et l'ensemble des commandes numériques dans `src/remap.{h,cpp}` en sont portés. Son architecture audio (encodage Opus sur l'appareil ou app compagnon optionnelle qui encode sur l'hôte) a aussi servi de référence pour diagnostiquer notre travail sur la cadence du haut-parleur (« warble »). Merci pour ces bases.
- **[awalol/ds5dongle-config-web](https://github.com/awalol/ds5dongle-config-web)** : base de notre app de configuration web forkée, [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web). Le fork adapte la structure Config_body de 13 octets d'upstream à notre structure de 19 octets et ajoute l'interface pour nos ajouts (association dans plusieurs emplacements, Auto Haptics).
- **[PS5 Button Icons and Controls](https://zacksly.itch.io/ps5-button-icons-and-controls)** par **Zacksly** : le contour de la manette DualSense et les icônes des touches de la page « Attribution des touches » du configurateur (app de bureau et outil web), sous licence [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) (recolorés en `currentColor` et recadrés pour les thèmes).

## Feuille de route
- Consulte le [DS5Dongle plan](https://github.com/users/awalol/projects/5)

## Communauté
- Rejoins le serveur Discord : [Discord Server](https://discord.gg/hM4ntchGCa)
- Si tu rencontres un bug, ouvre plutôt une issue.

## Références

- [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense) : inspiration du projet
- [egormanga/SAxense](https://github.com/egormanga/SAxense) : preuve de concept de l'haptique en Bluetooth
- [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense) : documentation de la structure des rapports de données de la DualSense
- [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX) : paquet du rapport haut-parleur

---

*Cette traduction suit le [README.md](README.md) anglais, qui fait référence ; elle peut avoir du retard sur lui.*
