# Modbus Control — Guide de démarrage rapide

English version: [Quick Start](../QUICK_START.md) · Guide complet : [Guide de l'utilisateur](GUIDE_UTILISATEUR.md)

Ce guide présente le déroulement de base avec un régulateur ABB ControlMaster CM50 :

1. [Se connecter au régulateur](#1-se-connecter-au-régulateur)
2. [Ajouter une étiquette](#2-ajouter-une-étiquette)
3. [Placer les curseurs sur la tendance](#3-placer-les-curseurs-sur-la-tendance)
4. [Afficher les repères sur une courbe](#4-afficher-les-repères-sur-une-courbe)

Chaque étape prend quelques minutes. Le [Guide de l'utilisateur](GUIDE_UTILISATEUR.md) complet
explique toutes les options. L'interface du logiciel est en anglais : les noms des boutons, des
menus et des champs sont donnés tels qu'ils apparaissent à l'écran.

**Avant de commencer**

- Le CM50 doit avoir son option Ethernet, et ce PC doit être sur le même réseau.
- Notez l'adresse IP du régulateur et son adresse Modbus (« Device Address », de 1 à 247).
- Le CM50 accepte au plus 3 connexions Modbus en même temps. Fermez les autres logiciels Modbus si
  la connexion échoue.
- N'écrivez rien dans un régulateur qui conduit un vrai procédé sans en avoir l'autorisation.

## 1. Se connecter au régulateur

1. Démarrez **Modbus Control**.
2. Dans le menu **Options**, cochez **Use 1-based register numbers**. Le manuel du CM50 numérote
   les registres à partir de 1. Avec cette option, vous les tapez exactement comme ils sont
   imprimés.
3. Dans la barre de connexion, en haut, remplissez :

   | Champ | Valeur |
   |---|---|
   | **Host** | l'adresse IP du régulateur, par exemple `192.168.1.50` |
   | **Port** | `502` (fixe sur le CM50) |
   | **Unit** | l'adresse Modbus du régulateur (« Device Address »), habituellement `1` |
   | **Timeout** | `1000` ms |
   | **Period** | `500` ms (une lecture chaque demi-seconde) |

4. Cliquez sur **Connect**. Le voyant d'état devient vert et la barre d'état affiche
   « Connected to ... ».

Si le voyant reste jaune ou devient orange, vérifiez le câble, l'adresse IP, le port et le numéro
d'unité. Le régulateur peut aussi n'accepter que certaines adresses IP : demandez que l'adresse de
ce PC soit autorisée.

**Raccourci :** **File > Open** `examples\cm50.mbproj` (dans le dossier d'installation) charge la
connexion et les étiquettes de ce guide. Remplacez l'adresse du champ **Host** par celle de votre
régulateur.

## 2. Ajouter une étiquette

Une étiquette est une valeur lue dans le régulateur. Commencez par la variable de procédé (PV) de la
boucle 1.

1. Dans le panneau **Tags**, en bas, cliquez sur **Add**. La boîte de dialogue **Add tag** s'ouvre.
2. **Name** : `Loop 1 Process Variable`.
3. **Area** : **Input register**.
4. **Address** : `5009` (le numéro imprimé dans le manuel du CM50).
5. Attendez un instant : pendant la connexion, la boîte de dialogue lit le registre et affiche sa
   valeur brute, ainsi que ce qu'elle vaut comme valeur 16 bits et comme `float32`.
6. Cliquez sur **Detect encoding**. Le CM50 enregistre cette valeur en `float32` sur deux
   registres : la boîte de dialogue **Detect encoding** s'ouvre avec sa meilleure réponse
   sélectionnée (normalement `float32 ABCD`).
   - Pour en être certain, tapez la valeur affichée sur le régulateur dans **Value shown on the
     device (optional)**. L'encodage qui correspond reçoit un crochet dans la colonne **Match**.
   - Cliquez sur **Apply**. Le type et l'ordre des octets sont remplis pour vous.
7. **Unit** : `°C` (ou l'unité de votre boucle).
8. Cliquez sur **Add**. La nouvelle ligne apparaît dans le tableau et sa courbe apparaît sur la
   tendance.

Ajoutez de la même façon les deux autres étiquettes de la boucle :

| Name | Area | Address | Unit | Remarques |
|---|---|---|---|---|
| Loop 1 Control Output | Input register | `5013` | `%` | Lecture seule. |
| Loop 1 Local Setpoint 1 | Holding register | `6639` | `°C` | Cochez **Writable** seulement si vous avez le droit de modifier la consigne. |

Comparez chaque valeur de la colonne **Value** avec l'affichage du régulateur. Si une valeur semble
fausse, faites un clic droit sur la ligne et choisissez **Detect encoding...**.

**Avant d'écrire dans une étiquette modifiable**, réglez ses colonnes **Min** et **Max** sur la plage
permise par le régulateur. Une écriture hors de ces limites est refusée.

## 3. Placer les curseurs sur la tendance

La tendance affiche chaque étiquette tracée sous forme de courbe. Deux curseurs, **C1** (rouge) et
**C2** (bleu), servent à y mesurer des temps et des valeurs.

1. Provoquez un changement : double-cliquez sur la cellule **Value** de **Loop 1 Local Setpoint 1**,
   tapez une nouvelle consigne (un petit changement, par exemple 2 °C au-dessus de la consigne
   actuelle) et confirmez. Attendez que la variable de procédé soit stabilisée.
2. Au-dessus de la tendance, réglez **Window** sur **5 min** pour voir tout le changement. La
   molette de la souris zoome dans le temps; un glisser avec le bouton droit déplace la vue.
3. Cliquez sur **Place cursors**. Cliquez sur la tendance juste avant le changement : c'est **C1**.
   Cliquez de nouveau une fois la variable de procédé stabilisée : c'est **C2**.
4. Pour déplacer un curseur, faites glisser sa ligne avec le bouton gauche.
5. Ouvrez l'onglet **Cursors**, à droite. Il affiche les temps `t1`, `t2`, `Δt` et, pour chaque
   courbe, sa valeur à C1 et à C2 et la variation `ΔY`.

Placer un curseur met la tendance en pause pour que les curseurs restent en place. Cliquez sur
**Run** pour suivre de nouveau les données les plus récentes; **Clear cursors** retire les deux
curseurs.

## 4. Afficher les repères sur une courbe

Les repères (onglet **Setpoints**) sont des niveaux en pourcentage d'une réponse à l'échelon : le
repère 63.2 % est l'instant où la variable de procédé a parcouru 63.2 % de sa variation entre C1 et
C2. Ils sont dessinés comme des marqueurs sur la courbe.

1. Gardez les deux curseurs de part et d'autre du changement (étape 3).
2. Ouvrez l'onglet **Analysis**, à droite. Dans **1. Signals** :
   - **Process value (PV)** : choisissez **Loop 1 Process Variable**.
   - **Controller output (CO)** : choisissez **Loop 1 Control Output** (ou l'étiquette que vous avez
     modifiée).
3. Ouvrez l'onglet **Setpoints** (à côté d'Analysis).
   - Dans **Show setpoints on**, la PV est déjà cochée. Cochez d'autres courbes si vous voulez aussi
     leurs repères.
   - **Setpoints (%)** donne la liste des pourcentages, `0 %` et `63.2 %` par défaut. Pour en
     ajouter un, tapez-le (par exemple `28.3`) et cliquez sur **Add**.
4. Sur la tendance, chaque repère apparaît comme un marqueur sur la courbe, avec l'étiquette
   `63.2 %` et des lignes de repère pointillées vers l'axe du temps et l'axe des valeurs. Le tableau
   des repères, en dessous, donne pour chacun la valeur cible, le temps et le temps écoulé depuis
   C1.
5. Cochez **Show time and value on plot labels** pour afficher le temps et la valeur sur chaque
   marqueur.

Placez le pointeur sur n'importe quelle valeur pour voir comment elle a été calculée.

**Pour aller plus loin :** l'onglet **Analysis** ajuste aussi un modèle du premier ordre (gain K,
temps mort L, constante de temps T) et propose des réglages PID. Consultez
[Analyse (réponse à l'échelon)](GUIDE_UTILISATEUR.md#analyse-réponse-à-léchelon) dans le Guide de
l'utilisateur. Pour conserver votre travail, utilisez **File > Save** (le projet) ou
**File > Save session...** (le projet, l'enregistrement des données et les curseurs).
