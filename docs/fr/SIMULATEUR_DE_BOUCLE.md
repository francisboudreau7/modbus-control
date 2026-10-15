# Simulateur de boucle de niveau 101

Lire en ligne : [https://francisboudreau7.github.io/modbus-control/fr/simulateur-de-boucle/](https://francisboudreau7.github.io/modbus-control/fr/simulateur-de-boucle/)

English version: [Level loop 101 simulator](../LOOP_SIMULATOR.md)

`modbus_loop_sim.exe` simule une petite boucle de régulation de niveau, pour les cours et pour essayer
l'analyse de la réponse à l'échelon de Modbus Control. Une pompe remplit un réservoir par une vanne de
régulation, et le réservoir se vide par une vanne manuelle. Le transmetteur de niveau et le régulateur
forment un serveur Modbus TCP : Modbus Control les lit et y écrit comme avec un vrai régulateur. Le
programme a une fenêtre avec le schéma TI, la face avant du régulateur et quelques commandes.

Le régulateur démarre en **Manual** (MAN). La commande de la vanne (OUT) change alors seulement quand
un client Modbus l'écrit ou quand vous la changez sur la face avant. La boucle est ouverte, ce qui
permet de faire des essais à l'échelon et de mesurer le procédé. Passez le régulateur en **Auto** et il
devient un régulateur PID qui maintient le niveau à SP : vous entrez alors le réglage calculé à partir de
l'essai à l'échelon et vous voyez la boucle fermée à l'œuvre.

Contenu : [Démarrage](#démarrage), [Le schéma TI](#le-schéma-ti), [La physique](#la-physique),
[Registres Modbus](#registres-modbus), [La fenêtre](#la-fenêtre), [Le régulateur PID](#le-régulateur-pid),
[Fichiers de scénario](#fichiers-de-scénario),
[Exercice : essai à l'échelon](#exercice--essai-à-léchelon-avec-modbus-control),
[Exercice : boucle fermée](#exercice--boucle-fermée).

## Démarrage

Dans le menu Démarrer, choisissez **Modbus Loop Simulator**. Ou démarrez-le dans une console :

```bat
modbus_loop_sim.exe --speed 2
```

Les programmes sont dans le dossier `bin` du dossier d'installation. Pour une installation par
utilisateur (par défaut), c'est `%LOCALAPPDATA%\Programs\Modbus Control` ; pour une installation pour
tous les utilisateurs, c'est `C:\Program Files\Modbus Control`. Les fichiers d'exemple, comme le
scénario ci-dessous, sont dans son dossier `examples` : avec `--scenario`, partez donc du dossier
d'installation :

```bat
cd "%LOCALAPPDATA%\Programs\Modbus Control"
bin\modbus_loop_sim.exe --scenario examples\loop101-step.mbloop
```

(Pour une installation pour tous les utilisateurs, utilisez `cd "C:\Program Files\Modbus Control"`.)

La version avec fenêtre est un programme graphique Windows : lancée depuis `cmd` ou PowerShell, elle
rend l'invite tout de suite, et Ctrl+C ou la fermeture de la console ne l'arrête pas (fermez plutôt sa
fenêtre). Son code de sortie n'est donc pas disponible pour l'interpréteur. Pour l'attendre et lire le
code de sortie, utilisez `start /wait bin\modbus_loop_sim.exe --headless` dans `cmd` ou
`Start-Process -Wait -NoNewWindow -PassThru` dans PowerShell (puis lisez `ExitCode` de l'objet
retourné). `--help`, `--version`, les erreurs de ligne de commande et `--headless` écrivent bien dans
la console.

Par défaut, le serveur écoute sur `127.0.0.1:1601` : seul ce PC peut s'y connecter. La barre d'état de
la fenêtre affiche l'adresse et le nombre de clients.

| Option | Signification |
|---|---|
| `-h`, `--help` | Afficher l'aide. |
| `--version` | Afficher la version et la version de libmodbus. |
| `--port <port>` | Port TCP. 1601 par défaut. `0` laisse le système choisir. |
| `--host <adresse>` | Adresse d'écoute. `127.0.0.1` par défaut. Utilisez `0.0.0.0` pour accepter les clients du réseau. Le serveur n'a pas d'authentification. |
| `--scenario <fichier>` | Charger un fichier de scénario (`.mbloop`) au démarrage (voir [Fichiers de scénario](#fichiers-de-scénario)). |
| `--speed <facteur>` | Vitesse de simulation : 1, 2 ou 5 (voir **Speed** plus bas). 1 par défaut. |
| `--headless` | Sans fenêtre. Exécute seulement la boucle et le serveur Modbus, et affiche `Listening on <hôte>:<port>`. Arrêtez-le avec Ctrl+C. |

Codes de sortie : 0 pour un arrêt normal, 1 quand le serveur ne peut pas démarrer (par exemple le port
est utilisé, seulement avec `--headless`), 2 pour une ligne de commande ou un fichier de scénario
invalide. Sans `--headless`, une erreur de démarrage est affichée dans la barre d'état et la fenêtre
reste ouverte.

## Le schéma TI

Le dessin suit les symboles ISA (ISA-5.1). Les conduites du procédé sont en trait plein. Les traits
pointillés sont des signaux électriques.

```
 TK-102 --> P-101 --> FCV-101 --> (conduite) --> TK-101 --> HV-102 --> TK-102
                         ^                          |  \--- trop-plein (99 %) ---> TK-102
                         |                          |
                      LIC-101 <-------------------- LT-101
```

| Étiquette | Description | Symbole ISA dans la fenêtre |
|---|---|---|
| TK-102 | Réservoir de base. Il est très grand : son niveau ne change pas. Il alimente la pompe et reçoit l'eau de HV-102 et du trop-plein. | Large réservoir, en bas à gauche. |
| P-101 | Pompe centrifuge. Sa vitesse se règle dans la fenêtre (100 % par défaut). La pression est de 1.00 bar à 100 % et diminue avec le carré de la vitesse. | Cercle avec un triangle pointant vers l'aval. |
| FCV-101 | Vanne de régulation. **Fermée en cas de défaillance** (air pour ouvrir) : elle se ferme quand le signal ou l'air est perdu. Linéaire : le débit est proportionnel à la position de la tige. La course complète prend 5 s. Elle est commandée par la sortie OUT du régulateur (4 à 20 mA = 0 à 100 %). | Vanne à soupape (deux triangles pointe à pointe) avec un actionneur à membrane et « FC » dessous. |
| Conduite | La conduite de FCV-101 à TK-101. Elle ajoute un temps mort pur de 2 s. | Trait plein avec des flèches. |
| TK-101 | Réservoir du procédé, diamètre 0.30 m, hauteur 1 m. Le niveau est la variable réglée. | Grand cylindre gradué à 0, 25, 50, 75 et 100 %. |
| HV-102 | Vanne manuelle à la sortie du bas de TK-101. C'est la perturbation : vous la changez à la main (ouverte à 50 % par défaut). | Vanne manuelle (deux triangles) avec une poignée en T. |
| Trop-plein | Une conduite à 99 % de la hauteur qui retourne l'excédent d'eau à TK-102. Le niveau ne peut pas dépasser 99 %. | Ligne du haut de TK-101 vers TK-102, en couleur pendant que l'eau s'écoule. |
| LT-101 | Transmetteur de niveau : 0 à 1 m = 0 à 100 % = 4 à 20 mA. Il a un amortissement de 2 s et un bruit de ±0.05 %. Sa sortie est l'entrée du régulateur (variable de procédé, PV). | Cercle (instrument de terrain). |
| LIC-101 | Régulateur indicateur de niveau. C'est le serveur Modbus. Il affiche PV, SP et OUT. | Cercle avec un trait horizontal (instrument de panneau). |

## La physique

En mots simples : la pompe pousse l'eau dans FCV-101. Plus la vanne est ouverte, plus d'eau arrive dans
TK-101, après un retard. L'eau sort par HV-102. Plus il y a d'eau dans le réservoir, plus la pression
au fond est grande, et plus la vidange est rapide. Le niveau monte donc jusqu'à ce que le débit de
sortie égale le débit d'entrée, puis il reste là. Ce procédé est stable (auto-régulant), mais il n'est
pas linéaire : le débit de sortie dépend de la racine carrée du niveau.

Avec la vitesse de la pompe s, la position de la tige de FCV-101 y, l'ouverture de HV-102 v (toutes en
fractions de 0 à 1) et le niveau h (en m) :

| Grandeur | Équation |
|---|---|
| Pression de la pompe | p = 1 bar × s² |
| Débit dans FCV-101 | Qfcv = 5.0×10⁻³ m³/s × y × s (300 L/min à pleine ouverture et pleine vitesse) |
| Mouvement de la tige | va vers OUT d'au plus 100 % par 5 s (temps de course complète) |
| Débit entrant dans TK-101 | Qin(t) = Qfcv(t − 2 s) |
| Débit sortant par HV-102 | Qhv = 7.0711×10⁻³ m^2.5/s × v × √h |
| Niveau | A × dh/dt = Qin − Qhv, avec A = π × 0.15² = 0.070686 m² |
| Trop-plein | quand h dépasserait 0.99 m, h reste à 0.99 m et l'excédent sort par le trop-plein |
| LT-101 | 100 × h / 1 m, passé dans un filtre de premier ordre de 2 s, plus une valeur aléatoire entre −0.05 et +0.05 %, une nouvelle valeur toutes les 0.1 s de temps simulé |

La simulation avance par pas de 10 ms de temps simulé.

**Niveau stabilisé.** Quand Qin = Qhv, le niveau s'arrête :

h = ( 5.0×10⁻³ × y × s / ( 7.0711×10⁻³ × v ) )²

Avec la pompe à 100 % et HV-102 à 50 %, la tige y égale OUT :

| OUT | Débit entrant | Niveau stabilisé (LT-101) |
|---|---|---|
| 30 % | 90 L/min | 18 % |
| 50 % | 150 L/min | 50 % |
| 60 % | 180 L/min | 72 % |
| 70 % | 210 L/min | 98 % |
| plus d'environ 70 % | | 99 % et l'eau sort par le trop-plein |

**Gain et constante de temps.** Près d'un niveau h, le procédé se comporte comme un système du premier
ordre. Son gain est la pente du tableau ci-dessus : environ 1.6 %/% entre 30 et 50 %, 2.2 %/% entre 50
et 60 %, et 2.4 %/% entre 50 et 70 %. Sa constante de temps est

T = 2 × A × h / Qhv

Elle est d'environ 28 s à un niveau de 50 % (0.50 m et 150 L/min), d'environ 17 s à un niveau de 18 % et
d'environ 34 s à 72 %. Les débits sont grands pour un si petit réservoir, exprès : un échelon se
stabilise en environ 2.5 minutes, en temps réel. Plus le niveau est haut, plus la boucle est lente.

## Registres Modbus

Le serveur répond à n'importe quel numéro d'unité. Les valeurs sont des `float32` dans l'ordre `ABCD`
(mot de poids fort en premier). Les numéros de registre ci-dessous commencent à 1, comme dans le manuel
d'un ABB CM50 et comme le projet d'exemple les affiche (**Address base 1**). Les adresses de protocole
dans les trames sont plus petites de un.

| Valeur | Zone | Registre (base 1) | Adresse de protocole | Plage |
|---|---|---|---|---|
| LT-101 PV (niveau) | Registre d'entrée (FC04) | 1-2 | 0-1 | lecture seule, 0 à 100 % |
| LIC-101 SP (consigne) | Registre de maintien (FC03, FC16) | 1-2 | 0-1 | 0 à 100 % |
| LIC-101 OUT | Registre de maintien (FC03, FC16) | 3-4 | 2-3 | 0 à 100 % |

Règles, comme le CM50 :

- Lisez un nombre à virgule flottante avec une requête de 2 registres. Lire les registres de maintien 1-4
  en une seule requête fonctionne aussi.
- L'écriture de SP ou de OUT doit être une requête FC16 sur une paire (début à 1 ou à 3, nombre pair).
  Une valeur hors de 0 à 100, ou qui n'est pas un nombre, est refusée avec l'exception 3 (valeur de
  donnée illégale).
- Tout le reste est refusé avec l'exception 2 (adresse de donnée illégale) : FC06, une requête qui
  commence au milieu d'un nombre, un nombre de registres qui coupe un nombre, toute autre adresse, ainsi
  que les bobines (coils) et les entrées discrètes.
- SP est mémorisée. En Manual, rien ne l'utilise : elle est seulement affichée sur la face avant et sur
  le schéma. En Auto, c'est la consigne du PID.
- Quand LIC-101 est en **Auto**, une écriture qui touche OUT (registres 3-4) est refusée avec
  l'exception 3 (valeur de donnée illégale) et rien n'est écrit, pas même la SP de la même requête.
  Écrire SP seule (registres 1-2) est accepté. La lecture n'est pas touchée : OUT est calculée par le
  PID et vous la lisez comme avant.

En Manual, une valeur OUT écrite agit tout de suite sur FCV-101 (la tige se déplace ensuite à la vitesse
de la course).

## La fenêtre

- **Schematic** (au centre) : le schéma TI. Le niveau d'eau de TK-101 est dessiné au niveau réel. Les
  étiquettes affichent les valeurs : LT-101 en % et en mA, OUT de FCV-101 en % et en mA avec la position
  de la tige, l'ouverture de HV-102, la vitesse de la pompe, et PV, SP et OUT de LIC-101. La ligne du
  trop-plein est en couleur pendant que l'eau s'écoule.
- **Show process data** (case dans la barre du bas) : ajoute le niveau en m, les débits Qin, Qhv et du
  trop-plein en L/min, la pression de la pompe en bar et le temps mort.
- **Face avant de LIC-101** (à droite) : comme un régulateur de panneau. Elle a une barre
  verticale de PV avec le repère de SP, et les valeurs PV, SP et OUT avec les mA de PV et de OUT. Les
  boutons **▲** et **▼** changent OUT de 1 % (5 % avec Maj enfoncée). Tapez une valeur dans la case OUT ou
  SP et appuyez sur Entrée. Les valeurs tapées hors de 0 à 100 sont ramenées dans cette plage. Un client
  qui écrit par Modbus change les mêmes valeurs. Le bouton de mode et les champs de réglage sont décrits
  dans [Le régulateur PID](#le-régulateur-pid).
- **Glissières** (barre du bas) : ouverture de **HV-102** (0 à 100 %) et vitesse de **P-101 speed**
  (0 à 100 %). Déplacer HV-102 est la perturbation de la boucle.
- **Speed** : ×1, ×2 ou ×5. ×1 est le temps réel : les temps affichés par Modbus Control sont les temps
  du procédé. À ×2 ou ×5, le procédé va plus vite que l'horloge : multipliez les temps de Modbus Control
  par la vitesse pour obtenir les temps du procédé. Le temps **t** sous le schéma est le temps du
  procédé.
- **Pause** / **Run** : arrête la simulation. Le serveur Modbus répond toujours, avec des valeurs
  figées, et les écritures changent encore SP et OUT.
- **t = ...** : le temps simulé depuis le démarrage ou la dernière réinitialisation.
- Menu **File** : **Open scenario...**, **Save scenario...**, **Reset** (retour à l'état stable avec
  OUT = SP = 50 %) et **Quit**. **Help > About** affiche la version.
- **Barre d'état** : l'adresse du serveur et le nombre de clients connectés.

## Le régulateur PID

Le bouton de mode de la face avant affiche **MAN** (ambre) ou **AUTO** (vert). Cliquez dessus pour
changer de mode. Le programme démarre en Manual.

- **Manual** : OUT est à vous, comme dans l'essai à l'échelon. Les champs de réglage peuvent être
  modifiés, mais ne servent pas.
- **Auto** : le régulateur calcule OUT toutes les 0.1 s de temps simulé à partir de SP et de PV. Les
  boutons OUT et la case OUT sont désactivés, et les écritures Modbus dans OUT sont refusées
  (exception 3). Vous pouvez toujours changer SP, sur la face avant ou par Modbus.
- Le passage de MAN à AUTO est **sans à-coup** : OUT ne saute pas, parce que le régulateur part de OUT
  actuelle. Cela vaut aussi avec Ti = 0 : le régulateur garde alors un biais constant
  (fixé au transfert, comme un « manual reset »), de sorte que OUT = biais + Kp × e. Le retour à MAN conserve OUT actuelle.

Les champs **Tuning** (tapez une valeur et appuyez sur Entrée ; les valeurs sont ramenées dans la
plage) :

| Champ | Signification | Plage | Par défaut |
|---|---|---|---|
| Kp | Gain proportionnel, en % de OUT par % d'erreur | 0 à 100 | 1 |
| Ti | Temps d'intégrale, en secondes. 0 signifie pas d'action intégrale. | 0 à 3600 | 30 |
| Td | Temps de dérivée, en secondes. 0 signifie pas d'action dérivée. | 0 à 600 | 0 |
| Action | **Reverse** ou **Direct** (voir ci-dessous) | | Reverse |

Le régulateur est de forme idéale (ISA), la même que celle du calculateur de réglage du panneau
Analysis :

u = Kp × (e + 1/Ti × ∫e dt + Td × de/dt)

- L'erreur e vaut SP − PV pour l'action **Reverse** (inverse) : quand PV monte, OUT descend. C'est la
  bonne action pour cette boucle : plus d'eau dans le réservoir doit fermer la vanne. L'action **Direct**
  utilise e = PV − SP. Avec elle, la boucle a une rétroaction positive et OUT s'emballe vers 0 ou 100 %.
- PV est la sortie du transmetteur, bruit compris. La dérivée agit sur PV et non sur l'erreur, et PV
  passe par un filtre du premier ordre de Td / 10 : un changement de SP ne donne pas de coup sur OUT, et
  le bruit est limité.
- Anti-emballement de l'intégrale : l'intégrale est gelée tant que OUT est à 0 ou à 100 % et que l'erreur
  la pousserait plus loin.
- OUT est limitée à 0 à 100 %. La tige de FCV-101 suit toujours OUT à la vitesse de la course.

## Fichiers de scénario

Un scénario (`.mbloop`) est un fichier JSON avec les réglages, et au besoin l'état de départ. Toutes les
clés sont facultatives. **Save scenario...** écrit les réglages et l'état actuels. Ouvrez-en un avec
**File > Open scenario...** ou avec `--scenario`. Un fichier invalide est refusé avec un message qui
nomme la clé.

| Clé (dans `settings`) | Plage | Par défaut |
|---|---|---|
| `pumpSpeedPct` | 0 à 100 | 100 |
| `hvOpeningPct` | 0 à 100 | 50 |
| `deadTimeS` | 0 à 30 | 2 |
| `ltNoisePct` | 0 à 5 | 0.2 |
| `ltDampingS` | 0 à 60 | 1 |
| `fcvStrokeS` | 0 à 120 | 5 |
| `speedFactor` | 1, 2 ou 5 | 1 |

L'objet facultatif `pid` contient le régulateur : `mode` (`"manual"` ou `"auto"`), `kp` (0 à 100), `tiS`
(0 à 3600), `tdS` (0 à 600) et `action` (`"reverse"` ou `"direct"`). Les valeurs par défaut sont celles
du tableau de [Le régulateur PID](#le-régulateur-pid). **Save scenario...** l'écrit toujours. Avec
`"mode": "auto"`, la boucle démarre en Auto, entré sans à-coup après l'application de `state`. Un scénario sans `pid` (enregistré par la version 1.3.0)
charge les réglages PID par défaut, en Manual.

L'objet facultatif `state` contient `levelPct` (0 à 99), `outPct`, `spPct` et `stemPct` (0 à 100). Sans
lui, la boucle démarre à l'état stable pour OUT = 50 %.

[examples/loop101-step.mbloop](../../examples/loop101-step.mbloop) contient les réglages par défaut à la
vitesse ×1, pour l'essai à l'échelon ci-dessous. Un enseignant peut enregistrer d'autres scénarios :
un temps mort plus long, plus de bruit, une vanne plus lente, une pompe plus petite.

## Exercice : essai à l'échelon avec Modbus Control

Vous mesurez le procédé à OUT = 50 % : le K, le L et le T d'un modèle du premier ordre avec temps mort.
Ensuite, vous les comparez à la théorie. Pour le panneau, voir [Analyse](GUIDE_UTILISATEUR.md#analyse-réponse-à-léchelon)
dans le Guide de l'utilisateur.

1. Démarrez le simulateur avec `bin\modbus_loop_sim.exe --scenario examples\loop101-step.mbloop`, depuis
   le dossier d'installation (voir [Démarrage](#démarrage))
   (ou depuis le menu Démarrer : la vitesse ×1 par défaut convient). Le niveau est stable à 50 %.
2. Démarrez Modbus Control et ouvrez [examples/loop101.mbproj](../../examples/loop101.mbproj). Le projet
   se connecte à `127.0.0.1:1601`, unité 1, avec trois étiquettes : **LT-101 PV** (registre d'entrée 1),
   **LIC-101 SP** (registre de maintien 1) et **LIC-101 OUT** (registre de maintien 3). Cliquez sur
   **Connect**.
3. Attendez 10 s. **LT-101 PV** est à environ 50 % et plat. Trend affiche PV et OUT.
4. Dans le panneau **Quick write**, cliquez sur **B: 60** pour faire passer OUT de 50 à 60 % (**A: 50**
   revient en arrière). La confirmation d'écriture s'affiche si elle est activée.
5. Attendez que PV soit stable, environ 2.5 minutes (cinq constantes de temps). PV monte à environ
   72 %.
6. Ouvrez le panneau **Analysis**. Le projet a déjà **LT-101 PV** comme PV et **LIC-101 OUT** comme CO.
   Cliquez sur **Find step**, ou placez C1 juste avant l'échelon et C2 après la stabilisation de PV avec
   **Place cursors**. Vérifiez les curseurs sur Trend.
7. Lisez K, L et T. Placez le pointeur dessus pour voir les formules.

**À quoi s'attendre.**

- **K** (environ 2.2 %/%) : ΔPV/ΔCO = (72 − 50) / (60 − 50). Il correspond au tableau des niveaux
  stabilisés.
- **L** : le temps mort de 2 s, plus les retards de la course de FCV-101 (un déplacement de 10 % prend
  0.5 s) et de l'amortissement de LT-101 (2 s). Cela fait environ 4 à 5 s. Le projet scrute toutes les
  100 ms : Trend le mesure bien à ×1.
- **T** : environ 32 s. La théorie donne 2 × A × h / Qhv = 28 s à 50 % et 34 s à 72 %, et l'échelon
  couvre les deux. Un modèle ajusté sur le point à 63.2 % donne une valeur entre les deux.

**Pourquoi un plus grand échelon donne-t-il un T plus grand ?** Le débit de sortie suit √h : le réservoir
est plus lent à haut niveau. Un échelon de 50 à 70 % donne un T plus grand que de 50 à 60 %, et un K plus
grand aussi (2.4 contre 2.2 %/%). Un échelon de 50 à 30 % donne des valeurs plus petites. Un procédé
linéaire donnerait chaque fois les mêmes K et T. Quand vous réglez un PID avec ces nombres, réglez-le pour
le point de fonctionnement que vous utiliserez.

**Perturbation de charge.** Remettez OUT à 50 % et attendez l'état stable. Déplacez ensuite le curseur
**HV-102** de 50 à 60 %. OUT ne change pas, et le niveau descend à environ 35 %, parce que la sortie est
plus grande. La boucle n'a pas d'action du régulateur : le niveau ne revient pas. Dans le panneau
Analysis, avec la CO réglée à **Typed ΔCO**, vous pouvez mesurer la réponse à une perturbation, mais il
n'y a pas de ΔCO pour donner un K.

**Trop-plein.** Écrivez 80 dans **LIC-101 OUT**. Le niveau monte à 99 % et y reste. La ligne du
trop-plein change de couleur, et avec **Show process data** vous voyez le débit du trop-plein en L/min.
PV reste à 99 % quel que soit OUT : le gain du procédé est nul à cet endroit. Un essai à l'échelon qui
entre dans le trop-plein donne un K et un T faux. Gardez OUT à 70 % ou moins avec les réglages par défaut.

**Autres essais.** Changez le temps mort, le bruit ou le temps de course dans un fichier de scénario et
refaites l'essai. Baissez la vitesse de la pompe à 70 % : le même OUT donne alors un niveau plus bas.
Fermez HV-102 (0 %) et le réservoir se remplit jusqu'au trop-plein.

## Exercice : boucle fermée

Vous mettez le régulateur en Auto avec le réglage obtenu de l'essai à l'échelon, puis vous comparez des
règles de réglage. Faites d'abord l'[essai à l'échelon](#exercice--essai-à-léchelon-avec-modbus-control),
pour que K, L et T soient dans le panneau Analysis.

**1. Lire le réglage.** Dans le panneau **Analysis**, le groupe Tuning transforme K, L et T en réglages.
Avec les nombres de l'essai à l'échelon (K environ 2.2 %/%, L environ 4 à 5 s, T environ 28 à 34 s),
vous obtenez à peu près les valeurs ci-dessous. Les vôtres seront un peu différentes. La PV et la OUT de
cette boucle sont toutes deux en %, de sorte que le Kp du tableau (en % par %) peut être tapé tel quel.

| Règle | Régulateur | Kp (environ) | Ti (environ) |
|---|---|---|---|
| IMC / lambda, λ = Auto (= L) | PI | 1.6 | 31 s |
| SIMC, λ = Auto (= L) | PI | 1.6 | 31 s |
| Ziegler-Nichols | PI | 2.8 | 15 s |

**2. Démarrer en Auto.** Réglez OUT à 50 % et attendez l'état stable (PV et SP valent 50 %). Sur la face
avant, tapez Kp et Ti (Td reste à 0, Action à Reverse), puis cliquez sur **MAN**. Le bouton devient
**AUTO**, et OUT ne bouge pas.

**3. Faire un échelon de consigne.** Dans Modbus Control, écrivez 60 dans **LIC-101 SP**. Ouvrez Trend et
réglez son axe de gauche de 0 à 100 : faites un clic droit sur l'axe Y de gauche et choisissez
**0 to 100** (voir [Plages des axes Y](GUIDE_UTILISATEUR.md#plages-des-axes-y) dans le Guide de
l'utilisateur). Trend ne change alors pas d'échelle pendant que la boucle bouge. Tracez PV et OUT sur
cet axe.

Avec les valeurs par défaut (Kp 1, Ti 30 s), PV monte à 60 % sans dépassement et reste à moins de 0.5 %
de SP après environ 30 s. OUT saute d'abord de Kp × 10 % = 10 %, à 60 %, puis redescend et se stabilise
à environ 55 %, la valeur de OUT qui maintient un niveau de 60 % avec HV-102 à 50 %.

**4. Comparer les règles.** Ramenez SP à 50 %, essayez chaque paire de valeurs à tour de rôle et écrivez
de nouveau 60 dans SP. Observez le dépassement (de combien PV dépasse 60 %) et le temps de stabilisation
(le temps jusqu'à ce que PV reste à ±0.5 % de 60 %). En général, le PI de Ziegler-Nichols est le plus
agressif : son Kp est plus grand et son Ti plus court, de sorte que PV dépasse et peut osciller avant de
se stabiliser, tandis qu'IMC et SIMC avec λ = L donnent une réponse plus douce. Un λ plus grand est plus
lent et plus doux. Les curseurs et le panneau Analysis peuvent mesurer les temps pour vous.

**5. Perturbation de charge.** La boucle en Auto et PV stable à SP (60 %), déplacez le curseur **HV-102**
de 50 à 60 %. La sortie est plus grande, donc PV baisse d'abord. Le régulateur ouvre la vanne, et l'action
intégrale ramène PV à SP : OUT finit à environ 66 %, contre environ 55 % avant. Comparez le creux de PV
et le mouvement de OUT pour différents réglages.

Tapez maintenant Ti = 0 (P seul) et recommencez à partir d'un état stable. PV ne revient plus à SP : il
reste un **écart permanent**, parce qu'un régulateur P a besoin d'une erreur pour donner une autre OUT.
Un Kp plus grand réduit l'écart. L'écart vaut (OUT au nouvel état stable - biais) / Kp, où le biais est la OUT au moment du passage
en AUTO. Le passage en AUTO avec Ti = 0 ne fait pas sauter OUT, comme l'explique [Le régulateur PID](#le-régulateur-pid).

**6. La mauvaise action.** En Auto avec Ti = 30 s, réglez **Action** à **Direct**. Le régulateur ouvre
maintenant la vanne quand PV est au-dessus de SP, ce qui ajoute de l'eau. PV s'éloigne de SP et OUT
s'emballe vers 100 % (PV au-dessus de SP) ou vers 0 % (PV en dessous de SP) et y reste. Remettez
Action à Reverse et cliquez sur **AUTO** pour revenir en **MAN**, puis réglez OUT à la main si le niveau
est allé au trop-plein ou à vide.

**Autres essais.** Ajoutez Td (par exemple Td = L / 2) et regardez le bruit sur OUT. Allongez le temps
mort dans un fichier de scénario et voyez comment le même réglage se comporte. Essayez une consigne de
80 % : à ce niveau le procédé est plus lent et son gain plus élevé, de sorte qu'un réglage fait à 50 %
est moins stable.
