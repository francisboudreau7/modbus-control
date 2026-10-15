# Simulateur de boucle de niveau 101

Lire en ligne : [https://francisboudreau7.github.io/modbus-control/fr/simulateur-de-boucle/](https://francisboudreau7.github.io/modbus-control/fr/simulateur-de-boucle/)

English version: [Level loop 101 simulator](../LOOP_SIMULATOR.md)

`modbus_loop_sim.exe` simule une petite boucle de régulation de niveau, pour les cours et pour essayer
l'analyse de la réponse à l'échelon de Modbus Control. Une pompe remplit un réservoir par une vanne de
régulation, et le réservoir se vide par une vanne manuelle. Le transmetteur de niveau et le régulateur
forment un serveur Modbus TCP : Modbus Control les lit et y écrit comme avec un vrai régulateur. Le
programme a une fenêtre avec le schéma TI, la face avant du régulateur et quelques commandes.

Le régulateur n'a pas de mode automatique. La commande de la vanne (OUT) change seulement quand un
client Modbus l'écrit ou quand vous la changez sur la face avant. C'est voulu : la boucle est ouverte,
ce qui permet de faire des essais à l'échelon et de mesurer le procédé.

Contenu : [Démarrage](#démarrage), [Le schéma TI](#le-schéma-ti), [La physique](#la-physique),
[Registres Modbus](#registres-modbus), [La fenêtre](#la-fenêtre), [Fichiers de scénario](#fichiers-de-scénario),
[Exercice : essai à l'échelon](#exercice--essai-à-léchelon-avec-modbus-control).

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
| Débit dans FCV-101 | Qfcv = 1.0×10⁻³ m³/s × y × s (60 L/min à pleine ouverture et pleine vitesse) |
| Mouvement de la tige | va vers OUT d'au plus 100 % par 5 s (temps de course complète) |
| Débit entrant dans TK-101 | Qin(t) = Qfcv(t − 2 s) |
| Débit sortant par HV-102 | Qhv = 1.4142×10⁻³ m^2.5/s × v × √h |
| Niveau | A × dh/dt = Qin − Qhv, avec A = π × 0.15² = 0.070686 m² |
| Trop-plein | quand h dépasserait 0.99 m, h reste à 0.99 m et l'excédent sort par le trop-plein |
| LT-101 | 100 × h / 1 m, passé dans un filtre de premier ordre de 2 s, plus une valeur aléatoire entre −0.05 et +0.05 %, une nouvelle valeur toutes les 0.1 s de temps simulé |

La simulation avance par pas de 10 ms de temps simulé.

**Niveau stabilisé.** Quand Qin = Qhv, le niveau s'arrête :

h = ( 1.0×10⁻³ × y × s / ( 1.4142×10⁻³ × v ) )²

Avec la pompe à 100 % et HV-102 à 50 %, la tige y égale OUT :

| OUT | Débit entrant | Niveau stabilisé (LT-101) |
|---|---|---|
| 30 % | 18 L/min | 18 % |
| 50 % | 30 L/min | 50 % |
| 60 % | 36 L/min | 72 % |
| 70 % | 42 L/min | 98 % |
| plus d'environ 70 % | | 99 % et l'eau sort par le trop-plein |

**Gain et constante de temps.** Près d'un niveau h, le procédé se comporte comme un système du premier
ordre. Son gain est la pente du tableau ci-dessus : environ 1.6 %/% entre 30 et 50 %, 2.2 %/% entre 50
et 60 %, et 2.4 %/% entre 50 et 70 %. Sa constante de temps est

T = 2 × A × h / Qhv

Elle est d'environ 141 s à un niveau de 50 % (0.50 m et 30 L/min), d'environ 85 s à un niveau de 18 % et
d'environ 170 s à 72 %. Plus le niveau est haut, plus la boucle est lente.

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
- SP est seulement mémorisée. Elle est affichée sur la face avant et sur le schéma, et rien ne
  l'utilise.

Une valeur OUT écrite agit tout de suite sur FCV-101 (la tige se déplace ensuite à la vitesse de la
course).

## La fenêtre

- **Schematic** (au centre) : le schéma TI. Le niveau d'eau de TK-101 est dessiné au niveau réel. Les
  étiquettes affichent les valeurs : LT-101 en % et en mA, OUT de FCV-101 en % et en mA avec la position
  de la tige, l'ouverture de HV-102, la vitesse de la pompe, et PV, SP et OUT de LIC-101. La ligne du
  trop-plein est en couleur pendant que l'eau s'écoule.
- **Show process data** (case dans la barre du bas) : ajoute le niveau en m, les débits Qin, Qhv et du
  trop-plein en L/min, la pression de la pompe en bar et le temps mort.
- **Face avant de LIC-101** (à droite) : comme un régulateur de panneau en **MAN**. Elle a une barre
  verticale de PV avec le repère de SP, et les valeurs PV, SP et OUT avec les mA de PV et de OUT. Les
  boutons **▲** et **▼** changent OUT de 1 % (5 % avec Maj enfoncée). Tapez une valeur dans la case OUT ou
  SP et appuyez sur Entrée. Les valeurs tapées hors de 0 à 100 sont ramenées dans cette plage. Un client
  qui écrit par Modbus change les mêmes valeurs.
- **Glissières** (barre du bas) : ouverture de **HV-102** (0 à 100 %) et vitesse de **P-101 speed**
  (0 à 100 %). Déplacer HV-102 est la perturbation de la boucle.
- **Speed** : ×1, ×2 ou ×5. Même à ×1, le procédé va **5 fois plus vite que le temps réel**, pour qu'un
  échelon se stabilise en quelques minutes plutôt qu'en un quart d'heure : une seconde d'horloge vaut
  5 s de temps du procédé à ×1, 10 s à ×2 et 25 s à ×5. Le temps **t** sous le schéma est le temps du
  procédé. Les temps affichés par Modbus Control sont des temps d'horloge : multipliez-les par 5 × la
  vitesse (5 à ×1) pour obtenir les temps du procédé.
- **Pause** / **Run** : arrête la simulation. Le serveur Modbus répond toujours, avec des valeurs
  figées, et les écritures changent encore SP et OUT.
- **t = ...** : le temps simulé depuis le démarrage ou la dernière réinitialisation.
- Menu **File** : **Open scenario...**, **Save scenario...**, **Reset** (retour à l'état stable avec
  OUT = SP = 50 %) et **Quit**. **Help > About** affiche la version.
- **Barre d'état** : l'adresse du serveur et le nombre de clients connectés.

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
5. Attendez que PV soit stable, environ 3 minutes à ×1 (cinq constantes de temps du procédé, divisées
   par 5). PV monte à environ 72 %.
6. Ouvrez le panneau **Analysis**. Le projet a déjà **LT-101 PV** comme PV et **LIC-101 OUT** comme CO.
   Cliquez sur **Find step**, ou placez C1 juste avant l'échelon et C2 après la stabilisation de PV avec
   **Place cursors**. Vérifiez les curseurs sur Trend.
7. Lisez K, L et T. Placez le pointeur dessus pour voir les formules.

**À quoi s'attendre.**

- **K** (environ 2.2 %/%) : ΔPV/ΔCO = (72 − 50) / (60 − 50). Il correspond au tableau des niveaux
  stabilisés.
- **L** : le temps mort de 2 s, plus les retards de la course de FCV-101 (un déplacement de 10 % prend
  0.5 s) et de l'amortissement de LT-101 (2 s). Cela fait environ 4 à 5 s de temps du procédé, donc
  environ 1 s sur Trend à ×1. La période de scrutation du projet (100 ms) vaut 0.5 s de temps du
  procédé : L n'est qu'une valeur approximative, c'est le prix de l'horloge accélérée.
- **T** : environ 150 s de temps du procédé, donc environ 30 s sur Trend à ×1. Multipliez les temps de
  Trend par 5 (à ×1) pour obtenir les temps du procédé. La théorie donne 2 × A × h / Qhv = 141 s à 50 % et 170 s à 72 %, et l'échelon couvre les
  deux. Un modèle ajusté sur le point à 63.2 % donne une valeur entre les deux.

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
