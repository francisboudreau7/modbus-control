# French glossary (Québec technical French)

Reference terms for the French interface (`modbus_control_fr.ts`) and the French documentation
(`docs/fr`). Use the same term everywhere; the documentation already follows most of them.

General rules:
- Québec usage: *courriel*, *logiciel*, *fichier*, *appareil*; no anglicisms when a common
  French term exists, but keep the industry symbols (PV, SP, CO, FC03, HR, IR, DI, int16,
  float32, ABCD, Kp, Ti, Td) and the tag names (LIC-101, LT-101, FV-101) as they are. *CO* is
  both the coil prefix ("CO 12") and the controller output; the context tells them apart, as in
  English.
- Sentence case for menus, buttons and titles, as in English ("Enregistrer sous...", not
  "Enregistrer Sous...").
- Non-breaking space before `:` `;` `?` `!` and inside « » only in long texts (help, messages);
  short labels ending with `:` keep a normal space (`Valeur :`).
- Numbers keep the point as decimal mark (`12.5`), as in the English interface.
- *ON* / *OFF* stay as they are for coil and discrete input states (same as on most HMIs and in
  the write log).

## Modbus

| English | Français | Alternatives / note |
|---|---|---|
| Holding register | registre de maintien | Area column: *Maintien* |
| Input register | registre d'entrée | Area column: *Entrée* |
| Coil | bobine | Area column: *Bobine* |
| Discrete input | entrée discrète | Area column: *Discrète*; *entrée TOR* is the France term |
| Register (number) | registre (numéro de registre) | |
| Address | adresse | |
| 1-based register numbers | numéros de registre à partir de 1 | menu: *Numéroter les registres à partir de 1* |
| Function code | code de fonction | FC03 stays |
| Unit ID | numéro d'unité | keep "(Unit ID)" in tooltips |
| Device | appareil | *automate* only for a PLC |
| Server / client | serveur / client | |
| Test server | serveur de test | |
| Connect / Disconnect | Se connecter / Se déconnecter | buttons: *Connecter* / *Déconnecter* |
| Host | hôte | |
| Poll interval | période de scrutation | |
| Polling | scrutation | |
| Timeout | délai d'attente | quality: *Délai dépassé* |
| Exception (Modbus) | exception | *Illegal data address* → *adresse de données illégale* (as in the French Modbus literature) |
| Byte order | ordre des octets | ABCD, CDAB... stay |
| Raw value | valeur brute | |
| Off by one register | décalé d'un registre | |
| Half of a 32-bit value | moitié d'une valeur 32 bits | |
| Encoding (detect encoding) | codage (*Détecter le codage*) | not *encodage* |
| Floating point | virgule flottante | |

## Tags and table

| English | Français | Alternatives / note |
|---|---|---|
| Tag | étiquette | (*point*, *variable*); the guide uses *étiquette* |
| Add tag | Ajouter une étiquette | |
| Name / Area / Type / Unit | Nom / Zone / Type / Unité | *Area* → *Zone* (alt. *Table*) |
| Gain / Offset | Gain / Décalage | |
| Scaling | mise à l'échelle | |
| Writable | inscriptible | (*accessible en écriture*) |
| Quality: Good / Unknown / Error / Disconnected | Qualité : Bonne / Inconnue / Erreur / Déconnectée | |
| Axis: Left / Right | Axe : Gauche / Droite | |
| Lock tag configuration | Verrouiller la configuration des étiquettes | status bar: *Étiquettes verrouillées* |
| Write / quick write | écriture / écriture rapide | |
| Write log | journal des écritures | |
| Confirm quick writes | Confirmer les écritures rapides | |

## Trend, cursors, exports

| English | Français | Alternatives / note |
|---|---|---|
| Trend (plot) | tendance (graphique) | |
| Cursor | curseur | |
| Y axis range | plage de l'axe Y | |
| Step curve | courbe en escalier | |
| Recording | enregistrement | |
| Session | session | |
| Project | projet | |
| Example project | projet d'exemple | |
| Export graph image | Exporter l'image du graphique | |
| Data (CSV) | Données (CSV) | |
| Clipboard / Copy | presse-papiers / Copier | |

## Control loop and analysis

| English | Français | Alternatives / note |
|---|---|---|
| Process variable (PV) | variable mesurée (PV) | (*mesure*) |
| Setpoint (SP) | consigne (SP) | |
| Controller output (CO) | sortie du régulateur (CO) | |
| Controller | régulateur | |
| Auto / Manual | Auto / Manuel | |
| Bumpless | sans à-coup | |
| Step test / step response | essai à l'échelon / réponse à l'échelon | (*réponse indicielle*, used in lab titles) |
| Process gain | gain du procédé | |
| Dead time | temps mort | |
| Time constant | constante de temps | |
| Overshoot | dépassement | |
| Rise time / settling time | temps de montée / temps de stabilisation | |
| Tuning | réglage | not *syntonisation* |
| Proportional / integral / derivative | proportionnelle / intégrale / dérivée | Kp, Ti, Td stay |
| Disturbance | perturbation | |
| Level / tank / valve | niveau / réservoir / vanne | |
| Faceplate | panneau de commande | (*façade*) |
| Scenario | scénario | |

## Menus

| English | Français |
|---|---|
| File / Export / View / Options / Help | Fichier / Exporter / Affichage / Options / Aide |
| New / Open... / Save / Save As... / Quit | Nouveau / Ouvrir... / Enregistrer / Enregistrer sous... / Quitter |
| Open example... | Ouvrir un exemple... |
| Save session... / Open session... | Enregistrer la session... / Ouvrir une session... |
| Language / System language | Langue / Langue du système |
| Quick Start / User Guide / About | Démarrage rapide / Guide de l'utilisateur / À propos |
