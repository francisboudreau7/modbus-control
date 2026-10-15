# Guide de l'utilisateur de Modbus Control

Lire en ligne : [https://francisboudreau7.github.io/modbus-control/fr/guide/](https://francisboudreau7.github.io/modbus-control/fr/guide/)

English version: [User Guide](../USER_GUIDE.md) · Démarrage rapide : [Guide de démarrage rapide](GUIDE_DEMARRAGE_RAPIDE.md)

Ce guide décrit le logiciel tel qu'il est construit à partir de ce dépôt. Pour la compilation,
consultez le [README](../../README.md). Pour le serveur de test, consultez
[TEST_SERVER.md](../TEST_SERVER.md) (en anglais).

## Disposition de la fenêtre

- **Connection bar** (en haut) : les champs host, port, unit, timeout et period, un voyant d'état, et le bouton Connect / Disconnect.
- **Trend** (au centre) : le graphique en direct, avec ses commandes au-dessus.
- **Tags** (panneau du bas) : le tableau des étiquettes, avec les boutons Add, Duplicate, Remove et Write selected.
- **Cursors** (panneau de droite) : les temps des curseurs et le tableau de lecture de la Trend.
- **Quick write** (panneau de droite, regroupé en onglets avec Cursors et Analysis) : un bouton par valeur prédéfinie des
  étiquettes accessibles en écriture (voir [Écritures rapides](#écritures-rapides)).
- **Analysis** (panneau de droite, regroupé en onglets avec Cursors et Quick write) : l'analyse de la réponse à l'échelon de la
  Trend (voir [Analyse](#analyse-réponse-à-léchelon)).
- **Setpoints** (panneau de droite, onglet placé après Analysis) : les courbes qui affichent des repères, les
  pourcentages des repères, les références, le lissage et le tableau des repères (voir
  [Repères et options](#repères-et-options)).
- **Menus** : File, Export, View, Options et Help. **Help > Quick Start** et **Help > User Guide**
  ouvrent ces guides en ligne (en français quand Windows est en français), ou la copie installée
  avec le logiciel quand le navigateur ne peut pas être ouvert.
- **Barre d'état** : de courts messages (à gauche) et l'état de la connexion (à droite).

Les panneaux peuvent être fermés et rouverts à partir du menu View. La taille de la fenêtre et la disposition des panneaux sont mémorisées.

## Connexion

Remplissez les champs, puis cliquez sur **Connect** (ou appuyez sur Entrée dans le champ Host). Cliquez sur **Disconnect**
pour arrêter. Pendant la connexion et lorsque vous êtes connecté, les champs sont en lecture seule.

| Champ | Plage | Remarques |
|---|---|---|
| Host | Adresse IPv4, ou `localhost` | Les noms d'hôte autres que `localhost` sont refusés, et Connect reste désactivé. Utilisez `localhost` en minuscules. |
| Port | 1..65535 | 1502 par défaut. |
| Unit | 0..247 | Numéro d'unité Modbus. |
| Timeout | 100..60000 ms | Durée d'attente de chaque réponse. |
| Period | 100..60000 ms | Temps écoulé entre le début d'un cycle de scrutation et le début du suivant. Un cycle plus long que la période fait démarrer le suivant immédiatement. |

Les dernières valeurs avec lesquelles vous vous êtes connecté sont mémorisées et réutilisées au prochain démarrage du logiciel.

### Voyant d'état

| Couleur | État | Signification |
|---|---|---|
| Gris | Disconnected | Non connecté. Les champs sont modifiables. |
| Jaune | Connecting | Première tentative de connexion en cours. |
| Vert | Connected | Scrutation du serveur en cours. |
| Orange | Reconnecting | La connexion a été perdue, ou la tentative de connexion a échoué. Le logiciel réessaie. |

Placez le pointeur sur le voyant pour voir la raison du dernier échec et le temps restant avant la prochaine tentative.

Lorsque la connexion est perdue ou qu'une tentative de connexion échoue, l'état devient **Reconnecting**.
Les tentatives suivent un délai croissant de 0.5 s, 1 s, 2 s, 4 s et 8 s, puis une tentative toutes les 10 s. Chaque tentative échouée
est signalée dans la barre d'état. Appuyez sur **Disconnect** pour cesser les tentatives. Les étiquettes affichent la qualité
*Disconnected* tant que la connexion est interrompue, et la tendance montre une coupure.

## Étiquettes

Chaque ligne correspond à une valeur de registre que le logiciel lit. Colonnes :

| Colonne | Signification |
|---|---|
| Name | Nom affiché dans le tableau, dans la légende de la tendance et dans le panneau des curseurs. Ne peut pas être vide. |
| Area | `Holding` (lecture FC03, écriture FC06/FC16) ou `Input` (lecture FC04). |
| Address | Adresse Modbus à partir de zéro, 0..65535 (HR 100 correspond à l'adresse 100). Avec Options > Use 1-based register numbers, la colonne est intitulée **Register** et affiche et accepte le numéro tel qu'il est imprimé dans les manuels des appareils (1..65536 ; 1..65535 pour les étiquettes 32 bits). Voir Numéros de registre à partir de 1. |
| Type | `int16` (signé) ou `uint16` (non signé), un registre. `int32` (signé), `uint32` (non signé) et `float32` (virgule flottante simple précision IEEE-754), deux registres consécutifs. Voir Valeurs 32 bits. |
| Byte order | `ABCD`, `CDAB`, `BADC` ou `DCBA` : la façon dont les deux registres d'une valeur 32 bits sont arrangés. Affiche `—` pour les types 16 bits, où elle n'a aucun effet. Voir Valeurs 32 bits. |
| Gain | Multiplicateur appliqué à la valeur brute. Ne peut pas être 0. |
| Offset | Ajouté après le gain. |
| Unit | Affichée après le nom dans la légende et dans l'en-tête du CSV. Facultative. |
| Writable | Permet les écritures à partir de la colonne Value. Registres de maintien seulement, de n'importe quel type. La case est désactivée pour les registres d'entrée, et le passage de Area à Input la décoche. |
| Plot | Affiche l'étiquette dans la tendance. Les étiquettes non cochées sont quand même scrutées et enregistrées. |
| Axis | `Left` ou `Right` : l'axe Y utilisé par la courbe de l'étiquette dans la Trend. Voir [Tendance](#tendance). |
| Color | Couleur dans la tendance. Double-cliquez sur la cellule, ou appuyez sur F2 ou Entrée, pour ouvrir le sélecteur de couleur. |
| Raw | La valeur du registre telle qu'elle a été lue. Les types 16 bits affichent l'hexadécimal et le décimal (`0x00AB (171)`) ; les types 32 bits affichent la valeur 32 bits en hexadécimal (`0x42F6E979`). Lecture seule. |
| Value | Valeur mise à l'échelle : `raw * gain + offset`, avec 6 chiffres significatifs (7 pour `float32`). Lecture seule, sauf pour les étiquettes accessibles en écriture (voir Écrire des valeurs). |
| Min, Max | Limites d'écriture, en unités mises à l'échelle : la valeur la plus basse et la plus haute qui peuvent être écrites. Vide signifie aucune limite. Modifiables seulement pour les étiquettes accessibles en écriture ; Min ne peut pas être supérieur à Max. Toute écriture, y compris les écritures rapides, est refusée hors des limites. Voir [Sécurité des écritures](#sécurité-des-écritures). |
| Quick A, Quick B | Valeurs prédéfinies pour le panneau Quick write, en unités mises à l'échelle. Vide signifie aucun bouton. Seules les étiquettes accessibles en écriture peuvent en avoir, et une valeur prédéfinie doit respecter les limites Min et Max. Voir [Écritures rapides](#écritures-rapides). |
| Quality | État de la dernière lecture. Lecture seule. |

Valeurs de qualité :

| Quality | Signification |
|---|---|
| Unknown | Pas encore lue, ou la connexion a été fermée. |
| Good | La dernière lecture a réussi. |
| Timeout | Aucune réponse dans le délai d'attente du réglage Timeout. |
| Exception | Le serveur a renvoyé une exception Modbus, par exemple adresse de données illégale. |
| Disconnected | Aucune connexion au serveur. |
| Error | Toute autre défaillance. |

Placez le pointeur sur une cellule Quality pour voir la dernière écriture échouée de cette étiquette. L'infobulle est effacée par la
prochaine écriture réussie. Les erreurs de lecture sont signalées seulement dans la barre d'état.

### Afficher et masquer des colonnes

Faites un clic droit sur n'importe quel en-tête de colonne du tableau des étiquettes pour choisir les colonnes à afficher : cochez ou décochez une
colonne dans le menu. **Name** est toujours affichée. **Show all columns** affiche de nouveau toutes les colonnes, et
**Compact view** masque les colonnes qui ne sont nécessaires que pendant la configuration des étiquettes : Raw, Gain, Offset,
Byte order, Axis, Color, Min, Max, Quick A et Quick B. Les colonnes masquées conservent leurs valeurs et continuent de
s'appliquer (par exemple, les limites d'écriture protègent toujours les écritures lorsque Min et Max sont masquées).

Ce choix est enregistré dans le projet. Un nouveau projet, ainsi qu'un projet enregistré par une version plus ancienne, s'ouvre
en vue compacte. Les projets d'exemple gardent visibles les colonnes dont ils dépendent : Quick A/B dans la
démo, Byte order, Min et Max dans l'exemple du CM50.

### Valeurs 32 bits

Les étiquettes `int32`, `uint32` et `float32` lisent deux registres consécutifs, à partir de **Address**. Le
deuxième registre est Address + 1, donc une étiquette 32 bits à l'adresse 65535 aurait besoin d'un registre au-delà de la fin
de l'espace d'adressage et est refusée. Utilisez 65534 ou moins.

Les deux registres sont combinés selon **Byte order**. A, B, C et D sont les quatre octets de la
valeur, du plus significatif au moins significatif. Le tableau montre le contenu de chaque registre pour la valeur
`0x42F6E979` (`float32` 123.456) :

| Byte order | Registre à Address (n) | Registre à Address + 1 | Usage typique |
|---|---|---|---|
| `ABCD` | `0x42F6` (A B) | `0xE979` (C D) | Gros-boutiste (big endian). Valeur par défaut de Modbus. |
| `CDAB` | `0xE979` (C D) | `0x42F6` (A B) | Inversion des mots. Courant dans les automates programmables et les compteurs. |
| `BADC` | `0xF642` (B A) | `0x79E9` (D C) | Inversion des octets dans chaque registre. |
| `DCBA` | `0x79E9` (D C) | `0xF642` (B A) | Petit-boutiste (little endian). |

Si une valeur 32 bits semble incorrecte (un nombre à virgule flottante beaucoup trop grand ou trop petit, ou un entier qui saute
dans tous les sens), l'ordre des octets est généralement mauvais. Essayez `CDAB` en premier, puis les autres.

La colonne Raw affiche la valeur 32 bits une fois l'ordre des octets appliqué, sous la forme ABCD. Si l'ordre des octets
correspond à celui du serveur, Raw affiche la valeur voulue par le serveur, et Value est le résultat mis à l'échelle.

Les étiquettes 32 bits de type holding peuvent être écrites comme celles de 16 bits (voir Écrire des valeurs). Leurs cellules Raw et Value
sont mises à jour par la scrutation comme celles de toute autre étiquette.

### Numéros de registre à partir de 1

Modbus numérote lui-même les registres à partir de 0, mais de nombreux manuels d'appareils les numérotent à partir de 1 (le premier
registre de maintien est « 40001 » ou « registre 1 »). Cochez **Options > Use 1-based register numbers** pour
saisir et lire les numéros tels que le manuel les imprime. L'en-tête de la colonne Address devient **Register**,
et ses cellules, les confirmations d'écriture (« Write ... to HR 6639-6640 ») et la boîte de dialogue Write selected
utilisent tous les numéros à partir de 1. Rien d'autre ne change : les étiquettes sont stockées avec des adresses de protocole
à partir de zéro, de sorte que le registre 5009 est stocké et envoyé comme l'adresse 5008.

Ce réglage est enregistré dans le fichier du projet (`addressBase`) et rétabli à l'ouverture du projet.
Le basculer marque le projet comme modifié. File > New revient à la numérotation à partir de 0. Les messages
d'état sur les lectures échouées affichent toujours les adresses de protocole (à partir de zéro).

### Modifier le tableau

- Modifiez une cellule en double-cliquant dessus ou en appuyant sur F2.
- **Add** ouvre la boîte de dialogue Add tag (voir ci-dessous). Rien n'est deviné : vous choisissez vous-même la zone et
  l'adresse.
- **Duplicate** copie chaque ligne sélectionnée. Le nom de la copie reçoit « (copy) ».
- **Remove**, ou la touche Suppr, supprime les lignes sélectionnées. Une confirmation vous est demandée lorsque plus
  d'une ligne est sélectionnée.
- **Write selected...** est activé lorsque deux lignes ou plus sont sélectionnées et que toutes sont des
  registres de maintien accessibles en écriture. Voir Écrire des valeurs.

Les modifications des étiquettes marquent le projet comme modifié. Les valeurs en direct ne le font pas.

### Boîte de dialogue Add tag

Choisissez l'**Area** (registre de maintien ou registre d'entrée) et saisissez l'**Address**, dans la numérotation
qu'affiche le menu Options. La zone est vide et l'adresse est en blanc à l'ouverture de la boîte de dialogue. Le bouton **Add**
reste désactivé tant que les deux ne sont pas valides et que l'étiquette ne partage pas un registre avec une étiquette existante.
Une ligne rouge indique l'étiquette avec laquelle il y a chevauchement. Le nom, le type, l'ordre des octets, l'unité et Writable sont facultatifs.
Un nom laissé en blanc devient `Tag <id>`.

Lorsque vous êtes connecté, la boîte de dialogue lit le registre pour vous :

- **Read** affiche le registre brut et ses voisins (en hexadécimal), ainsi que leur signification comme valeur 16 bits
  et comme `float32` dans les deux sens. Elle s'exécute aussi d'elle-même peu après que vous avez changé la
  zone ou l'adresse, ou lorsque vous vous connectez. Les voisins qui appartiennent à une autre étiquette ne sont pas lus.
- **Detect encoding** tranche entre une valeur 16 bits et un `float32`, à l'adresse indiquée ou un registre
  plus tôt. S'il s'agit probablement d'un `float32`, la boîte de dialogue d'encodage s'ouvre (voir Détection de l'encodage). Lorsque vous
  l'appliquez, la boîte de dialogue définit le type, l'ordre des octets et, au besoin, l'adresse un registre
  plus tôt. Certains appareils refusent de lire un seul registre d'une valeur 32 bits. Detect lit alors
  la paire à la place et l'indique. Un registre qui vaut 0 ne permet pas de trancher : réessayez lorsque la valeur
  n'est pas 0. Si un voisin ne peut pas être lu, Detect ne peut pas exclure un `float32` et le dit : choisissez
  le type vous-même.
- Une étiquette ajoutée avec la réponse de Detect est confirmée, pourvu que vous n'ayez pas modifié ensuite la zone, l'adresse, le
  type ou l'ordre des octets. La boîte de dialogue d'encodage ne s'affiche pas une deuxième fois. Autrement, l'étiquette
  est vérifiée comme toute autre nouvelle étiquette.

Lorsque vous n'êtes pas connecté, la boîte de dialogue ne peut rien lire : elle ajoute l'étiquette telle que vous l'avez définie. L'étiquette
est vérifiée à la connexion, comme toute autre nouvelle étiquette. Cancel n'ajoute rien. La nouvelle ligne est sélectionnée.

## Écrire des valeurs

Les écritures sont envoyées au serveur entre les cycles de scrutation. Seules les étiquettes de registre de maintien dont **Writable** est coché
peuvent être écrites, de n'importe quel type (16 bits ou 32 bits).

### Un registre

1. Connectez-vous d'abord. Sans connexion, la barre d'état affiche « Not connected » et rien n'est envoyé.
2. Double-cliquez sur la cellule **Value** d'une étiquette accessible en écriture et saisissez une valeur en unités mises à l'échelle (et non le
   nombre brut).
3. Répondez à la question de confirmation. Son titre est « Write to 'Setpoint'? » et elle indique la **Current value**
   (ou « unknown »), la **New value** en gras et, lorsque l'étiquette a des limites, les **Limits**. Une petite
   note au bas donne le registre, le type et la valeur brute, par exemple « HR 100 · int16 ·
   raw 125 ». L'unité est aussi affichée, lorsque l'étiquette en a une.

La valeur est convertie en entier brut par l'inverse du gain et du décalage, puis arrondie à l'entier
le plus proche. Si la valeur brute est hors de la plage du type (-32768..32767 pour `int16`,
0..65535 pour `uint16` ; la plage finie de `float32`, et les plages de `int32` et `uint32`, pour les types
32 bits), un avertissement s'affiche et rien n'est envoyé. L'écriture d'un seul registre 16 bits utilise FC06 (Write Single Register).

Pour une étiquette 32 bits, la valeur est encodée dans deux registres selon l'ordre des octets de l'étiquette, et la
confirmation nomme les registres, le type et l'ordre des octets dans sa note, par exemple « HR 6639-6640 ·
float32 ABCD · raw 0x42480000 ». Les deux registres sont envoyés dans une seule requête FC16
(Write Multiple Registers), de sorte que le régulateur ne voit jamais une demi-valeur. Tout ce qui dépasse
un registre de 16 bits (toute valeur 32 bits, et Write selected avec plusieurs registres) est envoyé
de cette façon. Si un appareil rejette la requête avec l'exception Modbus 1 (« Illegal function »), c'est qu'il n'accepte
pas FC16. Le logiciel affiche alors un avertissement, « The device rejected the write of ... », qui le précise et indique
que rien n'a été écrit. Un tel appareil ne peut pas être utilisé pour les valeurs 32 bits ni pour les écritures de plusieurs registres.

La barre d'état affiche « Writing ... », puis « Write OK: <name> » ou « Write failed: <reason> ». Un échec
définit aussi la raison comme infobulle de la cellule Quality de l'étiquette. La colonne Value continue d'afficher la
valeur scrutée, de sorte qu'elle ne change que lorsque la prochaine lecture renvoie la nouvelle valeur.

### Plusieurs registres

1. Sélectionnez deux lignes ou plus. Utilisez Ctrl ou Maj pour sélectionner des lignes.
2. Cliquez sur **Write selected...**. Les lignes doivent toutes être des registres de maintien accessibles en écriture, d'adresses
   consécutives et sans doublons. Au plus 123 registres peuvent être écrits à la fois.
3. Modifiez la valeur de chaque registre. Chaque champ commence par la valeur actuelle. Utilisez `.` comme
   séparateur décimal.
4. Confirmez « Write N tags? ». Un tableau montre chaque étiquette avec sa valeur actuelle et sa nouvelle valeur, et une note donne
   les registres (par exemple « HR 100-103 · FC16 »). Tout le groupe est envoyé dans une seule requête FC16.
   Les étiquettes 16 bits et 32 bits peuvent être mélangées ; la limite de 123 registres compte des registres, pas des étiquettes.

### Écritures rapides

Les écritures rapides sont des écritures en un clic de valeurs prédéfinies, pour les consignes que vous changez souvent (par exemple
une valeur « marche » et une valeur « arrêt »).

1. Définissez les valeurs prédéfinies dans le tableau des étiquettes : saisissez une valeur dans la cellule **Quick A** et/ou **Quick B** d'une
   étiquette accessible en écriture, en unités mises à l'échelle. Videz la cellule pour supprimer la valeur prédéfinie. Quick A et Quick B sont
   en lecture seule pour les étiquettes qui ne sont pas accessibles en écriture. Une valeur prédéfinie hors des limites **Min**/**Max** de l'étiquette est
   refusée au moment où vous la saisissez.
2. Ouvrez le panneau **Quick write** (à droite, regroupé en onglets avec Cursors et Analysis ; rouvrez-le à partir du menu View).
   Il liste chaque étiquette accessible en écriture qui a une valeur prédéfinie, avec son nom, sa valeur en direct et un bouton par
   valeur prédéfinie : **A: <value>** et **B: <value>**. Les boutons sont désactivés lorsque vous n'êtes pas connecté.
3. Cliquez sur un bouton. Une question demande confirmation, par exemple « Write 1000 to HR 100
   'Setpoint'? ». L'écriture est ensuite envoyée comme l'écriture d'une seule étiquette (vérification de la plage ; une étiquette 32 bits est envoyée dans
   une seule requête FC16).

Pour les étiquettes 16 bits, la confirmation comporte une case à cocher **Don't ask again this session**. La cocher et
confirmer envoie l'écriture, et les écritures rapides suivantes vers des étiquettes 16 bits se font sans question jusqu'au
redémarrage du logiciel. Pour que la question soit posée de nouveau, cochez **Options > Confirm quick writes**, qui est cochée au démarrage
du logiciel. Décocher cette option envoie les écritures rapides 16 bits sans demander.

Les écritures rapides vers des étiquettes **32 bits** (`int32`, `uint32`, `float32`) demandent toujours une confirmation, quel que soit
le réglage de Options ; la case à cocher n'est pas affichée pour elles.

Les écritures à partir de la colonne Value et de **Write selected...** demandent toujours une confirmation. La case à cocher
**Don't ask again** n'a aucun effet sur elles.

### Sécurité des écritures

Avant l'envoi d'une écriture, le logiciel effectue les vérifications suivantes, dans cet ordre. Une vérification échouée affiche un
avertissement et rien n'est envoyé :

1. **Connecté.** Sans connexion, rien n'est envoyé.
2. **Limites.** Si l'étiquette a une limite **Min** et/ou **Max**, la valeur doit s'y trouver. L'avertissement
   nomme la valeur, l'étiquette et ses limites, par exemple « 150 °C is outside the limits of 'Setpoint'. »
   suivi de « 'Setpoint' accepts 0 to 100 °C. Nothing was written. »
3. **Lecture valide (étiquettes 32 bits seulement).** Une écriture 32 bits exige une lecture actuelle valide (Good) de cette étiquette. Si
   la dernière lecture n'était pas Good, ou s'il n'y en a pas encore, l'écriture est refusée (« No valid reading of
   'Setpoint' yet. »). L'avertissement liste ce qu'il faut vérifier : la connexion, le numéro de registre, ainsi que le
   type et l'ordre des octets. Un mauvais réglage fait généralement échouer les lectures ou les rend aberrantes.
4. **Plage.** La valeur doit tenir dans le type de l'étiquette (par exemple -32768..32767 pour `int16`).
5. **Plausibilité (`float32` seulement).** Si la valeur actuelle d'une étiquette `float32` est invraisemblable (pas un
   nombre fini, d'une grandeur supérieure à 1e9, ou inférieure à 1e-6 sans être nulle), un avertissement supplémentaire
   s'affiche. Il explique que l'ordre des octets ou le numéro de registre est probablement mauvais, et l'écriture
   n'a lieu que si vous cliquez sur **Write anyway**. Cancel est le bouton par défaut. Un mauvais ordre des octets
   ou un mauvais registre rend la lecture absurde, de sorte que cet avertissement permet souvent de repérer l'erreur avant
   une écriture.

Ensuite, la confirmation affiche la **Current value** (la dernière bonne lecture, ou « unknown ») et la
**New value**. Lorsque l'étiquette a des limites, elle affiche aussi les **Limits**, par exemple « 0 to 2000 ». Lisez la valeur actuelle avant de confirmer : si elle n'est pas celle que vous attendez, cliquez sur **No**.

**Vérifier les écritures avant d'utiliser un appareil réel.** Démarrez le serveur de test avec `--log-writes` (voir
le guide du serveur de test) : il affiche chaque écriture reçue, avec les registres en hexadécimal et la
valeur décodée dans chaque ordre des octets. Ajoutez une étiquette `float32` de type holding accessible en écriture à l'adresse 110, avec l'ordre des octets
que vous prévoyez d'utiliser, écrivez une valeur, et vérifiez que le journal montre cette valeur sous le même ordre
des octets, et que les écritures refusées (hors des limites, sans lecture) ne produisent aucune ligne.

Les limites sont un filet de sécurité, et non un substitut à la vérification des plages propres au régulateur. Réglez-les sur les
plages que votre régulateur accepte pour ce paramètre (voir la section sur le CM50 ci-dessous).

## Détection de l'encodage

Une valeur 32 bits peut être lue de 12 façons : `int32`, `uint32` ou `float32`, chacun avec l'un des
quatre ordres des octets. Si le type ou l'ordre des octets est incorrect, la valeur n'a aucun sens.
Un nombre à virgule flottante lu comme un entier donne un grand nombre aléatoire, et un nombre à
virgule flottante dont les mots sont inversés a généralement une grandeur erronée.

Une écriture pose le même problème en sens inverse. Une valeur écrite avec le mauvais encodage
arrive à l'appareil comme un autre nombre, et l'appareil l'accepte. La détection de l'encodage
compare les lectures de l'appareil avec les 12 encodages et détermine lequel convient. Elle décide
d'elle-même dans les cas clairs et vous pose la question quand la réponse n'est pas claire. Elle
vérifie aussi les étiquettes 16 bits qui pourraient être la moitié d'une valeur 32 bits (voir
[Une étiquette 16 bits sur la moitié d'une valeur 32 bits](#une-étiquette-16-bits-sur-la-moitié-dune-valeur-32-bits)).

La boîte de dialogue Add tag possède son propre bouton **Detect encoding**. Elle décide avant que
l'étiquette n'existe, de sorte que la nouvelle étiquette commence confirmée (voir Boîte de dialogue
Add tag).

### Détection automatique

Quand vous ajoutez une étiquette 32 bits, que vous faites passer une étiquette à 32 bits ou que vous
changez le numéro de registre d'une étiquette 32 bits, l'étiquette est **non confirmée** jusqu'à ce
que la détection donne un résultat : sa cellule Name affiche une icône d'avertissement, et les
écritures vers cette étiquette demandent d'abord une confirmation (voir Écritures vers une étiquette
non confirmée). Lorsque l'application est connectée, elle attend trois bonnes lectures de l'étiquette
puis classe les 12 encodages. Le résultat est l'un des suivants :

- **Clair, et c'est l'encodage que l'étiquette a déjà.** Les lectures le confirment : l'icône
  d'avertissement disparaît.
- **Clair, et un autre encodage convient alors que l'encodage propre à l'étiquette donne une valeur
  absurde.** L'étiquette est changée pour l'encodage qui convient, par exemple `float32 CDAB`, et la
  barre d'état l'indique. Le changement est appliqué, mais l'étiquette reste **non confirmée**.
  Vérifiez la valeur sur l'appareil avant d'écrire.
- **Clair, mais l'encodage propre à l'étiquette donne aussi une valeur plausible**, ou **ambigu.** La
  boîte de dialogue Detect encoding s'ouvre, et c'est vous qui décidez (voir ci-dessous). Par
  exemple, `int32` 17000 correspond aussi à `float32` 58.0 dans un autre ordre des octets : vous
  seul savez lequel l'appareil envoie.
- **Indéterminable.** L'application conserve les trois dernières lectures et attend un résultat clair
  ou ambigu. Entre-temps, l'étiquette reste non confirmée.

Changer seulement le type, ou seulement l'ordre des octets, d'une étiquette 32 bits ne déclenche pas
la détection. Choisir vous-même un autre ordre des octets confirme l'encodage ; changer le type
laisse la confirmation de l'étiquette telle qu'elle était.

### La boîte de dialogue Detect encoding

Faites un clic droit sur l'étiquette dans le tableau des étiquettes et choisissez **Detect
encoding...**. Cet élément est offert pour toutes les étiquettes. Pour une étiquette 32 bits, il
ouvre cette boîte de dialogue. Pour une étiquette 16 bits, il lance la vérification de la moitié
d'une valeur 32 bits (voir
[Une étiquette 16 bits sur la moitié d'une valeur 32 bits](#une-étiquette-16-bits-sur-la-moitié-dune-valeur-32-bits)),
ou, lorsque l'appareil refuse de lire le registre seul, la vérification décrite dans
[Un appareil qui refuse de lire la moitié d'une valeur 32 bits](#un-appareil-qui-refuse-de-lire-la-moitié-dune-valeur-32-bits).
La boîte de dialogue comprend les parties suivantes :

- **Les derniers registres**, en hexadécimal et avec leurs numéros de registre, par exemple
  `HR 110-111: 0x3333 0x4251`.
- **Value shown on the device (optional).** Saisissez le nombre affiché sur l'écran de l'appareil,
  par exemple `52.3`. Une unité après le nombre (`52.3 °C`), des espaces et un séparateur de milliers
  (`1,013.25`) sont acceptés. La boîte de dialogue marque alors les encodages qui donnent cette
  valeur. Une correspondance tolère le dernier chiffre saisi, ou 0.5 % de la valeur, selon la plus
  grande de ces deux marges. Saisir la valeur affichée est le moyen le plus sûr de décider. Si le
  texte n'est pas un nombre, la boîte de dialogue l'indique.
- **Le verdict**, une phrase, par exemple « float32 CDAB fits best. » ou « Several encodings are
  possible: type the value shown on the device to decide. »
- **Le tableau.** Une ligne par groupe d'encodages qui donnent les mêmes valeurs, du meilleur au
  moins bon. Les colonnes sont **Encoding** (l'encodage à utiliser ; l'infobulle énumère les autres
  qui donnent les mêmes valeurs), **Value** (la dernière lecture), **Plausibility** (Good, Possible,
  Unlikely ou Implausible) et **Match** (une coche ou No, une fois que vous avez saisi une valeur
  affichée).

Sélectionnez une ligne et cliquez sur **Apply**. Apply règle le type et l'ordre des octets de
l'étiquette et confirme l'encodage. Si vous avez saisi une valeur affichée, seule une ligne qui y
correspond peut être appliquée. Si d'autres étiquettes 32 bits ne sont pas encore confirmées et
utilisent un ordre des octets différent, la boîte de dialogue offre aussi **Also use <order> for the
other unconfirmed 32-bit tags**, qui est cochée par défaut. Cette case à cocher change seulement
leur ordre des octets, et non leur type, et ces étiquettes restent non confirmées. Les étiquettes
que vous avez réglées ou confirmées ne sont jamais modifiées par cette option.

### Quand le résultat ne peut pas être déterminé

Certaines lectures ne permettent pas de départager les encodages :

- **Registres à 0.** Tous les encodages lisent 0. Attendez une valeur, ou saisissez la valeur
  affichée sur l'appareil.
- **Petits entiers.** Les deux mêmes registres peuvent représenter deux nombres différents. Par
  exemple, le serveur de test contient `int32` 100 aux registres HR 120-121 dans l'ordre `ABCD`
  (registres `0x0000`, `0x0064`). Lus dans l'ordre `BADC`, les mêmes registres donnent 25600. Si
  l'application ne peut pas dire lequel est le bon, la boîte de dialogue énumère les deux et demande
  la valeur affichée. Saisissez 100 : seule la ligne `int32 ABCD` correspond (`uint32 ABCD` donne
  la même valeur et est énumérée avec elle).

### La vérification à la connexion

À la connexion, l'application vérifie les étiquettes 32 bits qui ont assez de bonnes lectures. Si
l'encodage actuel d'une étiquette donne des valeurs invraisemblables et qu'un autre encodage
convient beaucoup mieux, l'étiquette est marquée **non confirmée**. Un message énumère ces
étiquettes, avec deux boutons : **Review...** ouvre la boîte de dialogue pour chaque étiquette
énumérée, tour à tour, et **Close** les laisse telles quelles. Les étiquettes qui n'ont jamais eu de
bonne lecture sont ignorées.

### Une étiquette 16 bits sur la moitié d'une valeur 32 bits

Une étiquette 16 bits peut se trouver sur la moitié d'une valeur 32 bits, et elle lit alors des
nombres d'apparence ordinaire. Le serveur de test contient `float32` 52.3 dans l'ordre `CDAB` aux
registres HR 110-111 : le registre HR 110 vaut `0x3333` et HR 111 vaut `0x4251`. Une étiquette
laissée en `int16` sur HR 110 lit 13107, et une sur HR 111 lit 16977. Aucune des deux ne semble
erronée.

Une écriture vers une telle étiquette est le vrai problème. Une écriture 16 bits remplace la moitié
du nombre à virgule flottante, et l'appareil contient alors un autre nombre. Par exemple, écrire 50
dans HR 111 laisse `0x3333` dans HR 110 et `0x0032` dans HR 111. L'appareil lit ces deux registres
dans l'ordre `CDAB` comme un `float32` de 4.6e-39, ce qui est pratiquement zéro.

L'application vérifie une étiquette 16 bits dans les cas suivants :

- quand vous l'ajoutez, ou que vous changez son numéro de registre, à sa première bonne lecture
  pendant que l'application est connectée ;
- une fois pour chaque étiquette 16 bits à la connexion. Ce passage a lieu une seule fois par
  étiquette tant que la liste des étiquettes reste la même (jusqu'à ce que vous ouvriez ou démarriez
  un autre projet) : se reconnecter ne revérifie pas les mêmes étiquettes.

Chaque vérification lit une fois le registre situé en dessous et le registre situé au-dessus de
l'étiquette. Elle ne se répète pas à chaque cycle de scrutation. Un registre voisin qu'une autre
étiquette du projet utilise est ignoré : il est associé à autre chose, et une modification à cet
endroit ferait en sorte qu'une étiquette écrase une autre. Une étiquette dont le propre registre
lit 0 n'est jamais signalée. Si l'appareil ne répond pas à l'une de ces lectures (délai d'attente
dépassé, ou connexion perdue), l'application cesse de vérifier les étiquettes 16 bits jusqu'à ce que
vous ouvriez ou démarriez un autre projet, et elle l'indique dans la barre d'état. Certains appareils
coupent la connexion lors de lectures hors de leur carte de registres.

La vérification cherche un `float32` « propre » : deux registres voisins qui se lisent, dans l'ordre
`ABCD` ou `CDAB`, comme un nombre à virgule flottante de grandeur raisonnable, avec au plus 5 chiffres
significatifs, ou 6 avec au plus 2 décimales, comme 52.3, 0.5, 100 ou 1013.25.

Quand la vérification en trouve un, l'étiquette est signalée. Sa cellule Name affiche l'icône
d'avertissement, et son infobulle nomme la lecture 32 bits, par exemple « HR 110-111 read as float32
CDAB give 52.3 ». L'application demande alors quoi faire :

- **Change to float32 CDAB at HR 110** déplace l'étiquette vers HR 110 et en fait un `float32` dans
  l'ordre `CDAB`, en une seule étape. Le changement est confirmé. Le gain et le décalage reviennent à
  1 et à 0 et les valeurs rapides sont effacées, parce qu'ils étaient prévus pour la lecture 16 bits ;
  les limites d'écriture sont conservées. Le message l'indique quand l'étiquette a un gain, un
  décalage ou des valeurs rapides.
- **Keep int16** confirme que l'étiquette est bien 16 bits et retire le signalement. L'application
  retient la réponse dans le projet : l'étiquette n'est plus vérifiée automatiquement, à moins que
  vous ne changiez son numéro de registre, sa zone ou son type. **Detect encoding...** la vérifie
  quand même.
- **Decide later** laisse l'étiquette signalée.

À la connexion, les étiquettes 16 bits signalées sont énumérées dans un seul message, avec
**Review...** et **Close**. **Review...** ouvre le message pour chaque étiquette énumérée, tour à
tour. Si un autre message ou une boîte de dialogue Detect encoding est ouvert à ce moment, le
message attend et apparaît quand l'autre se ferme.

Les écritures vers une étiquette 16 bits signalée demandent d'abord une confirmation, comme celles
vers une étiquette 32 bits non confirmée (voir Écritures vers une étiquette non confirmée). Dans ce
message, **Confirm and continue** signifie que l'étiquette est bien 16 bits.

Pour revérifier une étiquette signalée, faites un clic droit sur elle et choisissez **Detect
encoding...**. Cela ne fonctionne que lorsque l'application est connectée. Si la nouvelle
vérification ne trouve aucun `float32`, l'étiquette reste signalée et un message l'indique, avec
**Keep int16** et **Decide later** : choisissez **Keep int16** lorsque l'étiquette est 16 bits. Une
vérification qui se lance d'elle-même (à la connexion, ou après le déplacement de l'étiquette) et ne
trouve rien retire un signalement qui n'a plus de lecture 32 bits pour l'appuyer, par exemple sur
une étiquette que vous avez éloignée de la paire.

La vérification a des limites :

- Deux registres 16 bits distincts peuvent former un nombre à virgule flottante propre par hasard.
  Par exemple, 16384 et 0 se lisent comme `float32` 2.0. Le point faible est une valeur placée à côté
  d'un registre vide : une valeur comprise approximativement entre 15000 et 20000 (ou un mot d'état
  comme `0x4000`) se lit souvent comme un nombre à virgule flottante propre, car 17000 à côté de 0
  se lit comme 58.0. Sur l'ensemble des valeurs 16 bits placées à côté d'un registre vide, environ
  7 % se lisent comme un nombre à virgule flottante propre (mesure faite sur les données de test).
  Les petites valeurs ordinaires et les valeurs brutes analogiques typiques ne sont presque jamais
  signalées (environ 0.5 % pour les valeurs brutes de 0 à 27648). Si cela se produit, choisissez
  **Keep int16** une fois : l'application s'en souvient dans le projet.
- Un nombre à virgule flottante qui est une mesure à pleine précision, comme 23.456789, n'est pas
  propre et n'est donc pas détecté.
- Les moitiés d'une valeur `int32` ne peuvent pas être distinguées de deux registres 16 bits
  distincts et ne sont donc pas détectées.
- Seuls les ordres de mots `ABCD` et `CDAB` sont vérifiés. Avec les ordres à octets inversés
  (`BADC`, `DCBA`), de petits nombres ordinaires à côté d'un registre vide se liraient comme des
  nombres à virgule flottante propres (63 et 0 se lisent comme 0.5), de sorte qu'un appareil qui
  inverse les octets de ses nombres à virgule flottante n'est pas vérifié.

Les étiquettes qui n'ont jamais été vérifiées s'écrivent normalement.

Pour l'essayer avec le serveur de test :

1. Démarrez le serveur de test et connectez l'application à celui-ci.
2. Ajoutez une étiquette **Holding** à l'adresse 110 avec le type `int16`. L'application la
   signale : la cellule Name affiche l'icône d'avertissement, et l'infobulle indique « HR 110-111
   read as float32 CDAB give 52.3 ». Un message s'ouvre.
3. Choisissez **Change to float32 CDAB at HR 110**. L'étiquette devient `float32` `CDAB` à HR 110 et
   lit 52.3.
4. Pour voir le déplacement, supprimez cette étiquette (sinon, ses registres HR 110-111 seraient
   utilisés par elle), puis ajoutez une étiquette `int16` à l'adresse 111, ou faites-le dans un
   nouveau projet. Choisissez **Change to float32 CDAB at HR 110** dans son message. L'étiquette est
   déplacée vers HR 110.

### Un appareil qui refuse de lire la moitié d'une valeur 32 bits

Certains régulateurs, comme l'ABB CM50, refusent de lire un seul registre d'une valeur 32 bits. Ils
répondent « Illegal data address » à une lecture de ce registre seul. Une étiquette 16 bits placée
sur un `float32` ne lit alors jamais rien, de sorte que la vérification ci-dessus ne peut pas voir
sa valeur. L'application essaie alors de lire le registre comme première et comme seconde moitié
d'une valeur 32 bits : pour une étiquette à HR 110, elle lit HR 110-111, puis HR 109-110.
Auparavant, elle lit encore une fois le registre de l'étiquette seul et ne continue que si l'appareil
répond « Illegal data address ». Quand une étiquette est refusée, les étiquettes voisines continuent
d'être lues : la scrutation se rabat sur la lecture une à une des étiquettes d'un groupe en échec.

- Si l'une de ces lectures fonctionne, l'étiquette est signalée (sa cellule Name affiche l'icône
  d'avertissement). La boîte de dialogue Detect encoding s'ouvre, avec une ligne qui explique
  pourquoi : « 'Setpoint' (HR 110, int16) can't be read alone. » et la réponse « Illegal data
  address » de l'appareil. Saisissez la valeur affichée sur l'appareil dans **Value shown on the
  device (optional)** pour choisir l'encodage, puis cliquez sur **Apply**. L'étiquette est déplacée
  au début de la valeur 32 bits, son type est changé et l'encodage est confirmé. Le gain, le
  décalage et les valeurs rapides sont réinitialisés, comme dans la section précédente.
- **Cancel** laisse l'étiquette signalée. Les écritures vers celle-ci demandent d'abord une
  confirmation.
- Si aucune des deux lectures ne fonctionne, le registre n'existe probablement pas. Rien ne change.
  Detect encoding... indique alors que le registre ne peut pas être lu seul ni comme partie d'une
  valeur 32 bits, et vous demande de vérifier le numéro de registre.

La vérification s'exécute quand vous ajoutez ou déplacez une étiquette 16 bits, une fois par session
à la connexion, et quand vous choisissez **Detect encoding...** dans le menu du clic droit, ce qui
fonctionne pour toutes les étiquettes.

Pour l'essayer avec le serveur de test, démarrez-le avec `--strict-32bit`, par exemple
`build\msvc-debug\bin\modbus_test_server.exe --port 1502 --strict-32bit`,
et connectez l'application à celui-ci. Ajoutez une étiquette **Holding** à l'adresse 110 avec le
type `int16`. La lecture de HR 110 seul est refusée, la lecture de HR 110-111 fonctionne, et la
boîte de dialogue Detect encoding s'ouvre avec l'explication. Saisissez `52.3` dans **Value shown on
the device**, sélectionnez la ligne `float32 CDAB` et cliquez sur **Apply**. L'étiquette devient
`float32` `CDAB` à HR 110 et lit 52.3.

### Écritures vers une étiquette non confirmée

Avant une écriture vers une étiquette 32 bits dont l'encodage n'est pas confirmé, l'application
demande d'abord une confirmation. Le message indique, par exemple, « The encoding of 'Setpoint' is
not confirmed. » et montre ce que l'étiquette lit (52.3 °C) et l'encodage avec lequel elle le lit
(float32 CDAB, HR 110-111), puis avertit qu'une écriture enverrait une valeur erronée si l'encodage
est incorrect. Il comporte trois boutons :

- **Confirm and continue** confirme l'encodage et poursuit avec les autres vérifications d'écriture
  et la confirmation habituelle.
- **Detect encoding...** ouvre la boîte de dialogue. Rien n'est écrit.
- **Cancel** est le choix par défaut. Rien n'est écrit.

Si vous écrivez plusieurs étiquettes à la fois et que certaines ne sont pas confirmées, un seul
message les énumère toutes, et **Confirm and continue** les confirme toutes.

### Ce qui confirme un encodage

Un encodage est confirmé lorsque :

- vous cliquez sur **Apply** dans la boîte de dialogue Detect encoding ;
- vous cliquez sur **Confirm and continue** dans le message d'écriture ;
- vous choisissez vous-même un autre **Byte order** pour l'étiquette ;
- la détection automatique constate que les lectures correspondent à l'encodage que l'étiquette a
  déjà ;
- pour une étiquette 16 bits signalée, vous cliquez sur **Change to float32 CDAB at HR 110** ou sur
  **Keep int16** dans son message (**Keep int16** est aussi enregistré dans le projet).

Une étiquette que la détection automatique a changée reste non confirmée, avec son icône
d'avertissement, jusqu'à ce que l'un de ces événements se produise.

### Essai avec le serveur de test

1. Démarrez le serveur de test et connectez l'application à celui-ci.
2. Ajoutez une étiquette **Holding** à l'adresse 110 avec le type `float32` et l'ordre des octets
   `ABCD`. Le serveur de test contient `float32` 52.3 dans l'ordre `CDAB` aux registres HR 110-111.
3. Après trois lectures, l'application détecte `float32 CDAB`, y change l'étiquette et affiche
   l'icône d'avertissement sur la cellule Name.
4. Faites un clic droit sur l'étiquette, choisissez **Detect encoding...**, saisissez `52.3` dans
   **Value shown on the device** et cliquez sur **Apply**. La ligne float32 CDAB correspond, et
   l'icône d'avertissement disparaît.

Si vous choisissez plutôt `CDAB` vous-même, votre choix confirme l'encodage et la détection s'arrête
pour cette étiquette. La boîte de dialogue reste accessible depuis le menu du clic droit, et saisir
52.3 montre la même correspondance.

## Tendance

La tendance montre chaque étiquette tracée sous forme d'une courbe, de la couleur de l'étiquette.
L'axe X représente le temps en secondes, depuis la première connexion de cette exécution (voir
Enregistrement des données). L'axe Y de gauche montre la valeur mise à l'échelle des étiquettes dont
**Axis** est réglé à `Left`.

### Axe Y de droite

Les étiquettes dont **Axis** est réglé à `Right` (tableau des étiquettes) sont tracées par rapport à
un second axe Y, à droite du graphique. Utilisez-le pour une étiquette dont l'unité ou la plage
diffère de celles des autres, par exemple une température à côté d'une position de vanne.

- Les deux axes Y sont mis à l'échelle séparément. Chacun s'ajuste seulement à ses propres courbes,
  de sorte qu'une étiquette à droite ne change pas l'axe de gauche, et inversement.
- Le titre de chaque axe énumère les unités de ses étiquettes. L'axe de droite n'est affiché que
  lorsqu'au moins une étiquette tracée l'utilise.
- **Fit Y** ajuste chaque axe à ses propres étiquettes.

Commandes au-dessus du graphique :

- **Window** : 60 s (par défaut), 5 min, 30 min ou All. Dans une fenêtre de temps, l'affichage suit
  les données les plus récentes. All montre tout ce qui est en mémoire.
- **Pause** / **Run** : Pause fige l'affichage pendant que l'enregistrement des données se poursuit.
  Run revient aux données les plus récentes et réactive la mise à l'échelle automatique.
- **Place cursors**, **Clear cursors** : voir Curseurs.
- **Fit Y** : règle la plage Y sur les données visibles, et continue de l'ajuster tant que
  l'affichage suit les données.
- **Clear data** : efface toutes les données enregistrées (après une confirmation s'il y en a),
  efface les curseurs et ramène l'axe du temps à 0. La connexion reste telle quelle.

Souris :

| Action | Effet |
|---|---|
| Molette de la souris | Zoom sur l'axe du temps (X). |
| Shift + molette de la souris | Zoom sur les deux axes Y. La mise à l'échelle automatique s'arrête jusqu'à Run ou Fit Y. |
| Glisser avec le bouton droit | Déplace la vue. |

Le zoom et le déplacement de la vue mettent la tendance en pause.

Cliquez sur le nom d'une étiquette dans la légende sous le graphique pour masquer ou afficher sa
courbe. La légende affiche les noms sous la forme `name [unit]`.

Quand des données manquent (déconnexion, lecture échouée ou mauvaise qualité), la ligne est
interrompue.

## Curseurs

Deux curseurs verticaux, **C1** (rouge) et **C2** (bleu), marquent des instants sur la tendance. Le
panneau [Analysis](#analyse-réponse-à-léchelon) utilise les deux mêmes curseurs.

- **Place cursors** (bascule) : cliquez une fois pour placer C1 à cet instant, cliquez de nouveau
  pour placer C2. Le placement s'arrête après C2. Appuyez sur Échap pour arrêter le placement sans
  placer C2.
- **Déplacer un curseur** : appuyez sur le bouton gauche à moins de 5 pixels de la ligne d'un curseur
  et faites glisser. Cela fonctionne pendant un zoom ou une pause.
- **Clear cursors** : supprime les deux curseurs.

Placer ou déplacer un curseur met la tendance en pause, de sorte que le curseur reste là où vous
l'avez mis.

### Panneau Cursors

- En-tête : `t1` et `t2` en secondes (3 décimales), `Δt = t2 - t1`, et `1/Δt` en Hz. Δt est négatif
  quand C2 précède C1, et 1/Δt est affiché comme une fréquence positive. Un tiret (—) signifie
  qu'un curseur est absent, ou que Δt vaut 0 pour 1/Δt.
- Tableau : une ligne par étiquette tracée, avec `Y@C1`, `Y@C2` et `ΔY = Y@C2 - Y@C1`. Chaque valeur
  Y est le dernier échantillon enregistré à l'instant du curseur ou avant. Un tiret signifie qu'il
  n'y a pas d'échantillon de ce genre, ou que l'échantillon était manquant.

## Exportation

### Image du graphique

Export > Graph image... (Ctrl+E).

- **File** : saisissez un nom se terminant par `.png`, `.svg` ou `.pdf`. Browse... ajoute
  l'extension pour vous. Les autres extensions sont refusées.
- **Width** et **Height** : 16..20000 px. La valeur par défaut est la taille actuelle du graphique.
- **Resolution** : 36..1200 dpi, 96 par défaut. Elle détermine les métadonnées du PNG et la
  résolution du SVG, et la taille de page du PDF est la taille en pixels divisée par la résolution.
- **Include cursors** : offerte lorsqu'un curseur est placé. Décochée, elle masque les lignes des
  curseurs.
- **Include cursor readout** : offerte lorsqu'un curseur est placé. Dessine le texte du panneau
  Cursors dans un encadré en haut à gauche de l'image. Lorsque le panneau Analysis a un résultat
  valide, l'encadré contient aussi le résumé de l'analyse (K, L, T et l'erreur d'ajustement ; voir
  [Analysis](#analyse-réponse-à-léchelon)).

L'image montre le graphique tel qu'il est à l'écran, y compris le zoom et le déplacement de la vue
actuels.

### Données (CSV)

Export > Data (CSV)... (Ctrl+Shift+E).

- **Scope** : All recorded data (par défaut), ou Between cursors. Between cursors est offert lorsque
  les deux curseurs sont placés. Cette option inclut les lignes dont l'instant se situe entre les
  deux instants des curseurs, bornes comprises.
- **Delimiter** : virgule ou point-virgule. La valeur par défaut est le point-virgule lorsque le
  séparateur décimal de Windows est la virgule, et la virgule dans le cas contraire. Votre choix est
  mémorisé.
- **File** : `trend.csv` par défaut, dans votre dossier Documents. `.csv` est ajouté s'il manque.

Le fichier comporte une colonne par étiquette qui a des données enregistrées, y compris les
étiquettes dont Plot est décochée. L'en-tête est `timestamp,elapsed_s,<name> [<unit>],...` (la
partie de l'unité est omise lorsqu'elle est vide).

- Encodage UTF-8, fins de ligne CRLF.
- `timestamp` est l'heure locale avec les millisecondes et le décalage UTC, par exemple
  `2026-10-09T14:03:12.250-04:00`.
- `elapsed_s` est le nombre de secondes depuis le début de la base de temps, avec 3 décimales.
- Les valeurs sont les valeurs mises à l'échelle, avec `.` comme séparateur décimal. Un échantillon
  manquant est une cellule vide.

## Analyse (réponse à l'échelon)

Le panneau Analysis mesure une réponse à l'échelon sur le graphique Trend, entre les deux curseurs C1
et C2. Il trouve les temps auxquels la réponse atteint les repères (0 % et 63.2 % par défaut). À
partir de ces temps, il calcule un modèle du premier ordre avec temps mort : le gain K, le temps mort
L et la constante de temps T. Il calcule aussi, à partir de ces valeurs, des réglages PID. Il est
conçu pour les essais en échelon, par exemple pour obtenir les valeurs du réglage en boucle ouverte
de Ziegler-Nichols.

Ouvrez le panneau avec **View > Analysis**. C'est le panneau **Analysis**, du côté droit, regroupé en
onglets avec **Cursors** et **Quick write**. Ses commandes sont réparties en quatre groupes numérotés,
un pour chaque étape :

1. **Signals** : la variable de procédé, la sortie du régulateur et les plages facultatives.
2. **Cursors** : l'emplacement de C1 et de C2, et **Find step**.
3. **Model** : la méthode, les résultats, la courbe du modèle et les vérifications.
4. **Tuning** : les réglages PID.

Les options des repères et le tableau des repères se trouvent dans leur propre onglet **Setpoints**, à
côté d'Analysis (voir [Repères et options](#repères-et-options)). Placez le curseur de la souris sur
une valeur pour voir comment elle a été calculée (voir [Infobulles](#infobulles)).

Les superpositions (marqueurs de repères, lignes de repère et courbe du modèle) sont dessinées sur le
graphique Trend, et l'exportation de l'image du graphique les inclut.

### 1. Signaux

- **Process value (PV)** : le signal mesuré qui réagit à l'échelon, par exemple une température.
  Choisissez-le dans la liste. Il affiche **Choose...** tant que vous n'avez pas choisi une étiquette.
  Le choix d'une PV affiche aussi ses repères (voir [Repères et options](#repères-et-options)).
- **Controller output (CO)** : le signal qui a subi l'échelon, par exemple une consigne ou la position
  d'une vanne. Sa variation entre les curseurs est **ΔCO**. Choisissez l'étiquette de la CO dans la
  liste. Pour saisir vous-même l'échelon, choisissez **Typed ΔCO** et entrez l'échelon, dans les
  unités de la sortie, dans la case à côté. Un ΔCO saisi n'a pas d'unité dans les résultats.

Sans ΔCO, le panneau ne peut pas calculer K. L et T sont tout de même calculés.

**Plages pour les gains en %/%** (facultatif). Saisissez les valeurs **PV min** et **PV max** de la
variable de procédé, ainsi que **CO min** et **CO max** de la sortie du régulateur (0 et 100 par
défaut). Le panneau affiche alors aussi les gains en pourcentage des plages, **K (%/%)** et
**Kp (%/%)** :

- K (%/%) = K × étendue de la CO / étendue de la PV, où l'étendue est max − min.
- La plage de la PV n'est utilisée que si les deux cases de la PV sont remplies. Une case vide signifie
  qu'il n'y a pas de plage.
- Une étendue de 0 équivaut à l'absence de plage. Cela inclut une plage dont le min et le max sont
  égaux : elle est ignorée, et un projet enregistré avec une telle plage s'ouvre quand même.

### 2. Curseurs

C1 et C2 sont les deux curseurs de Trend (voir [Curseurs](#curseurs)). La ligne **C1 = …, C2 = …** de
ce groupe indique leurs temps. Un tiret (—) signifie que ce curseur n'est pas placé.

- **C1** se place juste avant l'échelon, pendant que la PV a encore sa valeur de départ.
- **C2** se place après la stabilisation de la PV.

La ligne d'aide sous les boutons vous indique ce qui manque encore :

- « Choose the process value (PV) in step 1. » La PV n'est pas encore choisie.
- « Place C1 just before the step and C2 after the PV has settled, or click Find step. » Il manque un
  curseur.
- Sinon, la raison pour laquelle il n'y a pas de résultat, s'il y en a une (par exemple « No change
  between C1 and C2 »).

**Find step** place C1 et C2 à votre place. Il a besoin d'une PV et d'une CO à l'étape 1. Quand il est
désactivé, son infobulle indique ce qui manque. Il examine la plage de temps visible de Trend (tout
l'enregistrement si rien n'est visible) : agrandissez ou faites défiler l'affichage jusqu'à l'échelon
d'abord. Il procède ensuite ainsi :

- il trouve la plus grande variation de la sortie du régulateur dans cette plage. Cette variation est
  l'échelon ;
- il place C1 5 s avant l'échelon, ou à mi-chemin du changement précédent de la sortie quand celui-ci
  est plus proche. C1 n'est jamais après le dernier échantillon enregistré de la sortie avant
  l'échelon : avec une période de scrutation lente, ΔCO entre les curseurs correspond donc à
  l'échelon complet ;
- il place C2 après la stabilisation de la PV : au temps de stabilisation plus 20 % de la durée de
  cette stabilisation. Si la PV ne se stabilise pas avant la fin de la plage (par exemple à cause du
  bruit), C2 est placé à la fin de la plage, et la vérification **The PV is still moving at C2**
  apparaît. Elle disparaît quand vous déplacez vous-même un curseur ou que vous changez la PV ou la CO.

Si aucun échelon n'est trouvé, la ligne d'aide affiche « No step found in the controller output in
the visible range. » Find step est un point de départ. Vérifiez ensuite les curseurs sur Trend.

### 3. Modèle

**Method** choisit les deux points de la réponse qui donnent L et T. Soit ta et tb les temps auxquels
la PV atteint les deux points a et b (en fractions). Tous les temps sont mesurés à partir du temps de
l'échelon t_step, qui est le temps de l'échelon de la sortie du régulateur.

| Méthode | Points (a / b) | T (constante de temps) | L (temps mort) |
|---|---|---|---|
| 0 % / 63.2 % (two points), par défaut | 0 % / 63.2 % | T = t63.2 − t0 | L = t0 |
| Smith | 28.3 % / 63.2 % | T = 1.5 (t63.2 − t28.3) | L = t63.2 − T |
| Sundaresan-Krishnaswamy | 35.3 % / 85.3 % | T = 0.67 (t85.3 − t35.3) | L = 1.3 t35.3 − 0.29 t85.3 |
| Custom | **From %** (a) et **To %** (b) | voir ci-dessous | voir ci-dessous |

Pour **Custom** :

- T = (tb − ta) / (ln(1 − a) − ln(1 − b))
- L = ta + T · ln(1 − a)

**From %** et **To %** ne sont utilisés que pour Custom. Ils doivent respecter a < b, et les deux
doivent être inférieurs à 100. Les deux points n'ont pas à figurer dans la liste Setpoints : le
panneau en cherche les temps de toute façon.

**Results** (le tableau de ce groupe) :

| Ligne | Signification |
|---|---|
| y0 | Le niveau de la PV à C1 (0 %). |
| y100 | Le niveau de la PV à C2 (100 %). |
| ΔPV | y100 − y0 : de combien la PV a bougé. |
| ΔCO | La variation de la sortie du régulateur entre les curseurs, ou la valeur saisie. |
| K | Le gain du procédé, K = ΔPV / ΔCO, en unité de la PV par unité de la CO, par exemple `°C/%`. |
| K (%/%) | Le gain en pourcentage des plages. Affiché seulement quand les deux plages sont définies. |
| L (s) | Le temps mort : le délai avant que la PV commence à réagir. |
| T (s) | La constante de temps : la rapidité avec laquelle la PV suit après le temps mort. |
| L/T | Le rapport de contrôlabilité. Sous 0.3, le procédé est facile à régler. Au-dessus de 1, le temps mort domine. |
| Fit error | L'écart entre le modèle et la PV mesurée, sous forme de valeur RMS dans l'unité de la PV et en pourcentage de ΔPV, accompagné d'un mot : **Good**, **Acceptable** ou **Poor**. |

L'erreur d'ajustement est Good sous 3 %, Acceptable sous 8 %, et Poor autrement. Une petite erreur
d'ajustement signifie que le modèle du premier ordre avec temps mort décrit bien la réponse.

Une valeur qui ne peut pas être calculée affiche « — ».

**Show model curve** (cochée par défaut) dessine le modèle ajusté par-dessus la réponse : une ligne en
tirets, de la couleur de la PV, de C1 à C2. Elle reste au niveau 0 % jusqu'au temps de l'échelon plus
L, puis monte vers 100 % avec la constante de temps T. Comparez-la avec la courbe mesurée. Si les deux
se superposent, le modèle décrit bien le procédé. L'option est enregistrée dans le projet.

**Checks.** Sous les résultats, le panneau énumère les vérifications qui s'appliquent, chacune avec
une icône d'avertissement. La liste est masquée quand il n'y a aucune vérification. Voici chaque
message et ce qu'il faut faire :

| Message | Quoi faire |
|---|---|
| « K can't be computed: choose the controller output (CO) or type ΔCO in step 1. » | Choisissez l'étiquette de la CO, ou choisissez **Typed ΔCO** et saisissez l'échelon. |
| « The controller output (CO) does not change between the cursors: K can't be computed. Move the cursors around the step. » | Une étiquette de CO est choisie, mais elle a la même valeur à C1 et à C2. Placez C1 avant l'échelon et C2 après, ou utilisez **Find step**. |
| « L and T can't be computed: the PV does not reach both model points (a % and b %) between the cursors. » | La PV ne dépasse pas les deux niveaux avant C2. Déplacez C2 plus tard, après que la PV a dépassé le deuxième niveau, ou choisissez une autre méthode. |
| « The PV is still moving at C2: move C2 later. » | Déplacez C2 plus tard, jusqu'à ce que la PV soit stable. |
| « The step is small compared with the noise: the results are uncertain. » | Utilisez un échelon plus grand, ou réglez **Reference** à **Mean before cursor** pour un signal bruité. |
| « L is negative: C1 is after the start of the response. Move C1 before the step. » | Déplacez C1 plus tôt, avant l'échelon. Ou utilisez **Find step**. |
| « L/T > 1: dead time dominates. Ziegler-Nichols and Cohen-Coon will be aggressive; prefer IMC / lambda or SIMC. » | Choisissez **IMC / lambda** ou **SIMC** dans le groupe Tuning. |
| « The model does not fit the response well (fit error > 8 %). » | Vérifiez que les curseurs encadrent l'échelon et que la courbe du modèle suit la PV. La réponse n'est peut-être pas du premier ordre : K, L et T sont alors approximatifs. |

### 4. Réglage

Le calculateur de réglage transforme K, L et T en réglages P, PI et PID. Choisissez une règle dans
**Rule**. Le tableau affiche une ligne pour chaque type de régulateur. Avec r = L / T, les règles
sont :

| Règle | Régulateur | Kp | Ti (s) | Td (s) |
|---|---|---|---|---|
| Ziegler-Nichols (open loop) | P | T / (K · L) | — | — |
| | PI | 0.9 · T / (K · L) | L / 0.3 (environ 3.33 · L) | — |
| | PID | 1.2 · T / (K · L) | 2 · L | 0.5 · L |
| Cohen-Coon | P | (T / (K · L)) · (1 + r / 3) | — | — |
| | PI | (T / (K · L)) · (0.9 + r / 12) | L · (30 + 3r) / (9 + 20r) | — |
| | PID | (T / (K · L)) · (4/3 + r / 4) | L · (32 + 6r) / (13 + 8r) | 4 · L / (11 + 2r) |
| IMC / lambda | P | not defined | — | — |
| | PI | T / (K · (λ + L)) | T | — |
| | PID | (T + L/2) / (K · (λ + L/2)) | T + L/2 | T · L / (2T + L) |
| SIMC | P | T / (K · (λ + L)) | — | — |
| | PI | T / (K · (λ + L)) | min(T, 4 · (λ + L)) | — |
| | PID | not defined | not defined | not defined |

- **λ** (lambda) est la constante de temps en boucle fermée de IMC / lambda et de SIMC, en secondes.
  Un λ plus grand donne un régulateur plus lent et plus doux. Entrez 0, qui s'affiche
  **Auto (= L)**, pour utiliser λ = L. La case n'est activée que pour IMC / lambda et SIMC.
- Les valeurs de Ti et de Td sont en secondes. Cochez **Ti, Td in minutes** pour les afficher en
  minutes. Le calcul est le même.
- Les régulateurs utilisent la forme idéale (ISA), que la note sous le tableau donne :
  u = Kp (e + 1/Ti ∫e dt + Td de/dt).
- Kp est exprimé en unité de la CO par unité de la PV. Si K est négatif (la PV diminue quand la
  sortie augmente), Kp est négatif aussi. La note affiche alors « K is negative: the controller must
  be direct acting (Kp is shown negative). »
- **Kp (%/%)** n'est affiché que lorsque les deux plages sont définies.
- Le tableau est vide tant que K, L ou T est manquant, ou que L est nul ou négatif. Corrigez d'abord
  les résultats ci-dessus. « not defined » signifie que la règle ne donne aucun réglage pour ce
  régulateur.

Les règles ne sont qu'un point de départ. Essayez le régulateur sur le procédé avant de vous fier aux
valeurs.

### Repères et options

Ces options se trouvent dans le panneau **Setpoints**, un onglet à côté d'**Analysis**, du côté droit
(View > Setpoints).

- **Show setpoints on** : cochez la PV et les autres étiquettes tracées dont vous voulez voir les
  repères. Le panneau trouve aussi leurs temps de repère. Chaque étiquette cochée obtient des
  marqueurs sur Trend, des lignes de repère et des lignes dans le tableau des repères. La liste est
  enregistrée dans le projet. Une étiquette qui n'est pas tracée sur Trend n'affiche ni marqueurs, ni
  lignes de repère, ni courbe du modèle, ni lignes, même si elle est cochée. Une PV non tracée est
  tout de même analysée (résultats et réglage), et une étiquette non tracée autre que la PV n'est pas
  analysée du tout, de sorte que les résultats exportés correspondent à ce que le panneau affiche.
- **Reference** : la façon dont les niveaux 0 % et 100 % sont déterminés.
  - **Value at cursor** (par défaut) : la valeur enregistrée au curseur, interpolée entre les deux
    échantillons qui l'entourent.
  - **Mean before cursor** : la moyenne des valeurs enregistrées dans la fenêtre de **W** secondes
    avant le curseur (0.1 à 3600 s, 1 s par défaut). Utilisez-la quand le signal est bruité.
- **Smoothing** S (**Off** par défaut, jusqu'à 3600 s) : une moyenne mobile centrée de largeur S,
  appliquée aux échantillons entre les curseurs avant la recherche des temps de repère. Les niveaux ne
  sont pas lissés.

  Le lissage introduit un biais. Une moyenne centrée commence à monter jusqu'à S/2 avant la vraie
  réponse : le temps du repère 0 % sort donc trop tôt d'au plus S/2, et L est trop petit. Avec le
  lissage activé, utilisez un modèle avec **From %** supérieur à 0 (par exemple 10), ou gardez S
  petit par rapport à L.
- **Setpoints (%)** : les pourcentages de l'échelon dont les temps sont trouvés. Par défaut : 0 % et
  63.2 %. Saisissez une valeur de 0 à 100 (deux décimales ; la case commence à 28.3, la valeur dont la
  méthode de Smith a besoin) et cliquez sur **Add**. Sélectionnez une entrée et cliquez sur
  **Remove**. La liste reste triée, sans doublons, et contient au plus 20 valeurs.
- **Show time and value on plot labels** : ajoute deux lignes à chaque étiquette de repère sur le
  graphique : le temps (t = 15.512 s) et la valeur cible avec son unité (y = 56.32 °C).
- **Guide lines** (cochée par défaut) : pour chaque repère trouvé, une ligne en tirets de son marqueur
  jusqu'au bas du graphique, et une autre droit vers la gauche jusqu'à l'axe Y. Elles montrent le temps
  et le niveau de chaque repère. Elles suivent le zoom et le déplacement, et elles sont incluses dans
  l'image du graphique.
- **Tableau des repères** : une ligne pour chaque étiquette cochée et chaque repère, avec ces
  colonnes :

| Colonne | Signification |
|---|---|
| Tag | Pastille de couleur et nom [unité]. |
| % | Le repère. |
| Target | Le niveau du repère : y0 + % × ΔPV. |
| Measured | La valeur enregistrée (non lissée) au temps trouvé. |
| Time (s) | Le temps trouvé. « — » quand il n'est pas trouvé entre les curseurs. |
| t − C1 (s) | Le temps trouvé moins le temps du curseur le plus à gauche. |

Chaque repère trouvé est marqué sur Trend par une croix à son temps et à son niveau, de la couleur de
l'étiquette, avec une étiquette comme « 63.2 % ». L'étiquette est dessinée dans une boîte blanche
avec une bordure de la couleur de l'étiquette, ce qui la garde lisible là où elle croise une courbe.

#### Comment les temps sont trouvés

Pour chaque repère, les échantillons enregistrés entre les curseurs sont parcourus ainsi :

1. Le point à 50 % est trouvé en premier : le premier échantillon qui a atteint le niveau 50 %, à
   partir de la valeur de départ. C'est l'ancrage.
2. Pour un repère de 50 % ou plus, la recherche va vers l'avant à partir de l'ancrage, jusqu'au
   premier échantillon qui a atteint le niveau du repère.
3. Pour un repère inférieur à 50 %, la recherche va vers l'arrière à partir de l'ancrage, jusqu'au
   dernier échantillon qui est encore du côté de départ du niveau du repère.
4. Le temps est interpolé linéairement entre les deux échantillons de part et d'autre du niveau.

Les échantillons manquants sont ignorés. Quand le niveau n'est jamais atteint, le temps affiche « — ».

La recherche ne sort jamais des curseurs : elle commence à C1 avec le niveau 0 % et se termine à C2
avec le niveau 100 %. Ainsi, 0 % n'est jamais avant C1. Si le signal ne revient pas au niveau de
départ après C1 (bruit, ou C1 placé sur une partie qui monte déjà), 0 % est C1 lui-même. De la même
façon, 100 % n'est jamais après C2.

Les points du modèle sont recherchés de la même façon, même s'ils ne figurent pas dans la liste
Setpoints.

### Infobulles

Chaque valeur des tableaux de résultats, de réglage et de repères a une infobulle. Elle explique la
valeur en trois parties :

1. le nom de la valeur et sa signification ;
2. la formule générale ;
3. la même formule avec les nombres de ce résultat, et la valeur.

Par exemple, l'infobulle de K se lit : « K, process gain: change of the PV per unit of controller
output, once settled. K = ΔPV / ΔCO, K = 12.5 / 10 = 1.25 °C/%. » Les infobulles de L et de T
montrent la formule de la méthode choisie avec les temps de franchissement. Les infobulles des
valeurs de réglage montrent la formule de la règle avec K, L, T et λ remplacés par leurs valeurs. Les
nombres dans les infobulles ont 4 chiffres significatifs.

### Copier et exporter les résultats

- **Copy results** (le bouton en haut du panneau) copie le tableau des résultats, le tableau de
  réglage et le tableau des repères, avec une ligne vide entre eux. Les lignes de réglage commencent
  par le type de régulateur (P, PI ou PID).
- **Ctrl+C** sur un tableau copie les cellules sélectionnées. Sans sélection, il copie tout le
  tableau, avec sa ligne d'en-tête.

Le texte est séparé par des tabulations. Le coller dans Excel ou Word place chaque valeur dans sa
propre cellule.

**Export results (CSV)...** dans le panneau, ou **Export > Analysis results (CSV)...**, est activé
quand la PV a un résultat valide. Le nom par défaut est `analysis.csv`, dans le dernier dossier
d'exportation ou dans Documents.

- Le fichier est en UTF-8 avec des fins de ligne CRLF et des virgules comme séparateurs. Il comporte
  deux sections, séparées par une ligne vide. La première a une ligne par repère
  (`tag,percent,target,measured,time_s,time_from_c1_s`). La seconde a une ligne par étiquette
  (`tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note`).
- Une valeur manquante est une cellule vide : `fit_rms` et `fit_rms_percent` sont donc vides quand
  aucun modèle n'est valide. Pour un résultat invalide, `note` contient le texte de l'erreur.

**Export > Graph image...** exporte Trend avec les marqueurs de repères et la courbe du modèle. Avec
**Include cursor readout** coché, la boîte en haut à gauche de l'image contient le résumé de
l'analyse : les temps de C1 et de C2, les K, L, T et l'erreur d'ajustement de la PV, et les temps de
repère des autres étiquettes.

### Sauvegarde

Les paramètres d'Analysis sont enregistrés dans le projet : les choix de PV et de CO, les plages, la
méthode, les repères, la référence, les options du modèle et du réglage, les étiquettes du graphique,
la courbe du modèle et les lignes de repère. Les curseurs ne sont pas enregistrés dans un projet. Une
session les conserve (voir [Sessions](#sessions)).

### Essai avec le serveur de test

1. Démarrez le serveur de test dans une console : `build\msvc-debug\bin\modbus_test_server.exe --port 1502`.
2. Démarrez l'application avec `examples\demo.mbproj` (ou ouvrez-le avec File > Open), puis cliquez
   sur **Connect**.
3. Double-cliquez sur la cellule **Value** de **Setpoint** (HR 100), saisissez 0 et confirmez. Ou
   cliquez sur **A: 0** dans le panneau Quick write. Attendez quelques secondes.
4. Ouvrez le panneau **Analysis** (View > Analysis). Dans **1. Signals**, choisissez **Lag** comme PV
   et **Setpoint** comme CO.
5. Placez C1 maintenant, avant l'échelon : cliquez sur **Place cursors** sur Trend, puis cliquez une
   fois dans le graphique avant l'échelon.
6. Écrivez 1000 dans **Setpoint** de la même façon, ou cliquez sur **B: 1000** dans le panneau Quick
   write, et attendez environ 10 s. Le signal **Lag** (IR 10) commence à monter après son temps mort
   de 1 s, avec une constante de temps de 2 s. Quand il est stabilisé, placez C2 de la même façon, ou
   cliquez sur **Find step** dans **2. Cursors** et vérifiez les curseurs sur Trend.
7. Les résultats de **Lag** montrent K ≈ 1, L ≈ 1 s et T ≈ 2 s. L'erreur d'ajustement devrait être
   Good, la courbe du modèle devrait suivre la courbe mesurée, et la liste des vérifications devrait
   être vide.

## Projets

Les projets sont enregistrés sous forme de fichiers `.mbproj`. Un projet contient les paramètres de
connexion, la numérotation des registres (à partir de 1 ou non), la liste des étiquettes, y compris
leurs couleurs, l'axe Y et les préréglages d'écriture rapide, ainsi que les paramètres d'Analysis. Les
données enregistrées, les curseurs et la fenêtre de la tendance ne sont pas enregistrés dans un
projet. Pour les conserver, enregistrez une [session](#sessions).

Dans le fichier, chaque étiquette a une clé `axis` (`"left"` ou `"right"`) et des clés facultatives
`quickA` et `quickB` (les préréglages d'écriture rapide) et `writeMin` et `writeMax` (les limites
d'écriture ; une clé n'est écrite que lorsque la limite est définie). Les paramètres d'Analysis sont
dans un objet `analysis` : `pvTag` et `setpointTags` (la PV et les étiquettes dont les repères sont
affichés), `deltaCoTag` et `deltaCo`, `method` (`"0-63.2"`, `"smith"`, `"sundaresan"` ou `"custom"`),
`modelPercents`, `percents`, `reference` (`"cursor"` ou `"mean"`), `referenceWindowS`, `smoothingS`,
`pvRange`, `coRange`, `showModel`, `guideLines`, `detailedLabels`, `tuningRule` (`"zn"`,
`"cohen-coon"`, `"imc"` ou `"simc"`), `lambdaS` et `timeUnit` (`"s"` ou `"min"`). Un tableau
`hiddenColumns` de premier niveau énumère les colonnes du tableau des étiquettes masquées dans ce
projet (par exemple `"raw"`, `"gain"`, `"quickA"`). Toutes ces clés sont facultatives : un fichier
qui en est dépourvu se charge avec les valeurs par défaut (la vue compacte pour `hiddenColumns`).

| Élément de menu | Raccourci | Action |
|---|---|---|
| File > New | Ctrl+N | Liste d'étiquettes vide. Les champs de connexion reprennent les derniers paramètres de connexion. |
| File > Open... | Ctrl+O | Ouvre un fichier `.mbproj`. |
| File > Save | Ctrl+S | Enregistre dans le fichier courant. Demande un nom quand le projet n'en a pas. |
| File > Save As... | Ctrl+Shift+S | Enregistre sous un nouveau nom. `.mbproj` est ajouté s'il manque. |
| File > Quit | Ctrl+Q | Ferme l'application. |

Le titre de la fenêtre affiche `*` quand il y a des modifications non enregistrées. New, Open et Quit
demandent s'il faut enregistrer (Save, Discard ou Cancel).

Démarrage : l'application démarre toujours avec les étiquettes de démonstration intégrées (Sine,
Triangle, Counter, Square, Noise, Sawtooth, Lag, Setpoint, Float sine, Float sine (CDAB) et
Int32 saw), et non avec un fichier. Elle ne rouvre pas le dernier projet. Pour démarrer avec un
projet, donnez son chemin sur la ligne de commande :

```bat
build\msvc-debug\bin\modbus_control.exe examples\demo.mbproj
```

[`examples/demo.mbproj`](../../examples/demo.mbproj) correspond aux étiquettes de démonstration et se
connecte au serveur de test à 127.0.0.1:1502, unité 1, avec une période de 200 ms.
[`examples/cm50.mbproj`](../../examples/cm50.mbproj) est un point de départ pour un ABB CM50 (voir la
section suivante).

## Sessions

Un fichier de session (`.mbsession`) conserve un enregistrement des données pour que vous puissiez le
consulter plus tard, sans connexion. Il contient :

- le projet : les paramètres de connexion, les étiquettes et les paramètres d'Analysis,
- l'enregistrement : l'historique de la tendance avec son heure de départ,
- les curseurs (C1 et C2) de Trend.

| Élément de menu | Action |
|---|---|
| File > Save session... | Enregistre le projet, l'enregistrement et les curseurs dans un fichier `.mbsession`. `.mbsession` est ajouté s'il manque. |
| File > Open session... | Ouvre une session. La connexion est d'abord fermée, si elle est ouverte. |

### Consultation et analyse hors ligne

Après **Open session...**, Trend, le panneau Cursors et le panneau Analysis affichent les données
enregistrées et les curseurs sauvegardés. Rien n'est scruté. Analysis fonctionne comme d'habitude, tout
comme les exportations de l'image du graphique et du CSV. Les écritures sont impossibles tant que vous
ne vous connectez pas.

Le titre de la fenêtre affiche le nom du fichier de session. Le projet contenu dans une session n'est
pas encore un fichier `.mbproj` : utilisez File > Save As... pour le conserver sous cette forme.

C'est la façon d'analyser un enregistrement de laboratoire à la maison : enregistrez la session au
laboratoire, copiez le fichier, puis ouvrez-le avec File > Open session... à la maison.

### Se connecter après l'ouverture d'une session

Quand vous cliquez sur **Connect** avec une session chargée, une question demande quoi faire de
l'enregistrement :

- **Continue this recording** : les nouveaux échantillons sont ajoutés après les données chargées. Un
  trou est dessiné dans la tendance, et le temps continue à partir de l'heure de départ chargée.
- **Start a new recording** : les données chargées sont retirées de l'affichage et le temps repart à
  0. Le fichier de session sur le disque n'est pas modifié.
- **Cancel** : rien n'est connecté.

### Modifications non enregistrées

Une fois qu'une session est chargée, les nouveaux échantillons enregistrés pendant la connexion font
passer l'enregistrement à l'état « modifié ». New, Open, Open session et Quit demandent alors
« The recording has changed. Save the session? » avec **Save**, **Discard** et **Cancel**. Save ouvre
la boîte de dialogue Save session. Sans session chargée, l'invite du projet est utilisée, comme
auparavant.

### Taille des fichiers

Le fichier de session contient tout l'enregistrement en mémoire : sa taille suit donc la durée de
l'enregistrement et le nombre d'étiquettes. L'historique est limité à 72 000 lignes (environ 2 heures
avec une période de 100 ms). Pour 2 heures à 100 ms avec 20 étiquettes, le fichier fait environ 14 Mo.

## Connexion à un ABB ControlMaster CM50

Les faits ci-dessous proviennent du manuel ABB IM/CM/C-EN Rev. I. Vérifiez-les en fonction du
micrologiciel et de la configuration de votre régulateur.

**Configuration du régulateur**

- Modbus TCP nécessite l'option Ethernet du régulateur.
- Le port est fixé à 502. Entrez 502 dans le champ Port.
- Le régulateur accepte au plus 3 connexions simultanées : fermez donc les autres clients Modbus si
  Connect échoue.
- Le régulateur peut restreindre les clients par adresse IP (autorisation IP des clients). Si c'est le
  cas, ajoutez l'adresse de ce PC.
- **Unit** est l'adresse d'appareil (Device Address) du régulateur (1..247).

**Les numéros de registre dépendent de la révision du manuel.** Les registres eux-mêmes sont les
mêmes, mais ABB a changé la façon de les numéroter dans les tableaux :

- La Rev. A imprime des adresses à partir de 0 : le tableau des registres d'entrée commence à
  `0000` (Output 1), les bobines (coils) commencent à `0` et la variable de procédé de la boucle 1
  est `5008`.
- Les révisions suivantes (par exemple la Rev. I) impriment des numéros à partir de 1 : le même
  tableau commence à `0001`, les bobines commencent à `1` et la variable de procédé de la boucle 1
  est `5009`.

Regardez la première ligne du tableau des registres d'entrée de votre manuel. Si c'est `0001`,
cochez **Options > Use 1-based register numbers** et saisissez les numéros tels qu'ils sont
imprimés. Si c'est `0000`, laissez l'option décochée et saisissez-les aussi tels qu'ils sont
imprimés. Dans les deux cas, l'application envoie l'adresse 5008 pour la variable de procédé de la
boucle 1. Si une valeur semble encore fausse, comparez-la avec l'affichage du régulateur : un
registre décalé de un donne une valeur absurde (ou « Illegal data address »).

**Les valeurs à virgule flottante** sont en simple précision IEEE-754, mot de poids fort en premier :
ordre des octets `ABCD`. Si le paramètre « Reverse IEEE Data » du régulateur est à Yes, les mots sont
inversés : utilisez `CDAB`.

**Écritures.** Le manuel documente les registres de maintien pour FC03 (lecture) et FC06 (écriture
simple). Les consignes float32 de `examples/cm50.mbproj` sont écrites avec FC16 (Write Multiple
Registers) : une seule requête transporte les deux registres, de sorte que le régulateur ne contient
jamais une moitié d'ancienne valeur et une moitié de nouvelle. Si votre CM50 n'accepte pas FC16, il
rejette la requête (rien n'est écrit) et l'application le signale ; un tel régulateur ne peut pas
recevoir d'écritures float32 depuis cette application.

**Premières écritures vers un vrai régulateur**

1. Connectez-vous avec toutes les étiquettes en lecture seule (Writable décoché). Comparez chaque
   valeur avec l'afficheur du régulateur. L'ordre des octets et les numéros de registre ne sont
   corrects que si les valeurs concordent. Une étiquette avec l'icône d'avertissement a un encodage
   non confirmé : confirmez-le avec **Detect encoding...** (voir Détection de l'encodage) avant d'y
   écrire. Une étiquette 16 bits sur la moitié d'une consigne float32 est signalée de la même façon
   (voir [Une étiquette 16 bits sur la moitié d'une valeur 32 bits](#une-étiquette-16-bits-sur-la-moitié-dune-valeur-32-bits)).
2. Sur chaque étiquette inscriptible, réglez **Min** et **Max** à la plage permise par le régulateur
   pour ce paramètre.
3. Cochez **Writable** sur l'étiquette.
4. Réécrivez la valeur actuelle de la consigne sans la modifier, et vérifiez qu'elle est relue à
   l'identique.
5. Faites ensuite un petit changement, et vérifiez l'afficheur du régulateur et la tendance.
6. Gardez la confirmation des écritures rapides activée (**Options > Confirm quick writes**) tant que
   vous ne connaissez pas bien les étiquettes.

**Exemples d'étiquettes** (numéros à partir de 1, comme dans la Rev. I, avec l'option à partir de 1
activée; soustrayez 1 pour un manuel Rev. A) :

| Nom | Zone | Registre | Type | Remarques |
|---|---|---|---|---|
| Loop 1 Process Variable | Input | 5009 | float32, `ABCD` | Lecture seule. |
| Loop 1 Control Output | Input | 5013 | float32, `ABCD` | Lecture seule. |
| Loop 1 Local Setpoint 1 | Holding | 6639 | float32, `ABCD` | Inscriptible. Limites données en exemple : Min 0, Max 100 (valeurs provisoires). |
| Alarm 1 Trip | Holding | 6591 | float32, `ABCD` | Inscriptible. Limites données en exemple : Min 0, Max 100 (valeurs provisoires). |
| Totalizer 1 Batch Total | Input | 4001 | uint32, `ABCD` | Le manuel n'indique pas si la valeur est signée ; essayez `int32` si la valeur semble erronée. |
| Instrument Type | Input | 3 | uint16 | Lecture seule. |

`examples/cm50.mbproj` contient ces étiquettes, avec l'option à partir de 1 activée, l'hôte
192.168.1.50, le port 502, l'unité 1 et une période de 500 ms. Ouvrez-le avec File > Open, ou donnez
son chemin sur la ligne de commande, puis changez l'hôte et l'unité pour qu'ils correspondent à votre
régulateur. Les valeurs de consigne et de déclenchement (trip) sont écrites avec confirmation, mais
vérifiez-les d'abord par rapport aux limites propres du régulateur.

Le fichier d'exemple a des limites **Min** et **Max** de 0 et 100 sur **Loop 1 Local Setpoint 1** et
**Alarm 1 Trip**. Ce sont des valeurs provisoires, et non les vraies plages du régulateur.
Remplacez-les par la plage permise de chaque paramètre, d'après votre régulateur, avant d'y écrire.

**Non pris en charge par cette application :** les bobines (coils) et les entrées discrètes (états
d'alarme, mode automatique/manuel) ainsi que Modbus RTU (série). Utilisez la face avant du régulateur
ou un autre outil pour ceux-ci.

## Enregistrement des données

- Chaque cycle de scrutation est enregistré sous la forme d'une ligne, qui couvre toutes les
  étiquettes, et pas seulement celles qui sont tracées.
- L'historique est conservé en mémoire : 72 000 lignes, environ 2 heures avec une période de 100 ms.
  Les lignes les plus anciennes sont supprimées en premier.
- La base de temps commence à la première connexion de cette exécution. Se déconnecter puis se
  reconnecter continue la même ligne de temps, et les données enregistrées ne sont pas effacées.
  Utilisez **Clear data** au-dessus de la tendance (ou redémarrez l'application) pour commencer un
  nouvel enregistrement.

## Limites

- Un seul serveur à la fois.
- Modbus TCP seulement. Aucune connexion série (RTU).
- Registres de maintien et registres d'entrée seulement, avec les types `int16` et `uint16` (un
  registre) ou `int32`, `uint32` et `float32` (deux registres). Il n'y a ni bobines (coils) ni
  entrées discrètes.
- Certains serveurs TCP ignorent le numéro d'unité. Il est tout de même envoyé avec chaque requête.
- L'historique est en mémoire seulement. Un fichier de projet ne le conserve pas ; un fichier de
  session, oui (voir [Sessions](#sessions)).
