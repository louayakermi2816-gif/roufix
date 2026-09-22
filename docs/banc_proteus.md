# Banc de qualification électrique — couche d'isolation ROUFIX (Proteus 8)

Ce document décrit le banc Proteus, son programme d'essais et les résultats
mesurés. Il ne contient aucun code ni aucun réseau : le banc qualifie la carte
d'isolation seule, contre les limites des datasheets. La validation du firmware,
du MQTT et de la supervision est faite sous Wokwi (voir `contrat_interface.md`
et `matrice_validation.md`).

Fichiers :

| Fichier | Rôle |
|---|---|
| `proteus/ROUFIX_test.pdsprj` | banc nominal : 7 canaux d'entrée, 1 canal de sortie instrumenté |
| `proteus/ROUFIX_banc_isolement.pdsprj` | copie avec la chaîne d'essai d'isolement (500 V) |
| `proteus/ROUFIX_banc_rouelibre.pdsprj` | copie avec inductance de bobine, générateur d'impulsion et graphe transitoire |
| `proteus/ROUFIX_doc.pdsprj` | schéma de documentation (ESP32 câblé, exclu de la simulation) |

## 1. Principe

L'ESP32 n'a pas de modèle de simulation dans Proteus, et le banc ne cherche pas
à en exécuter le code. Il est remplacé par son **équivalent électrique**, tel que
la datasheet le définit :

| Côté ESP32 | Représentation sur le banc | Valeur datasheet ESP32 |
|---|---|---|
| Broche de sortie à l'état haut | batterie B2 + interrupteur SW8 | 3,3 V nominal ; **V_OH min = 2,64 V** (0,8 × VDD) ; I_OH max 40 mA |
| Broche d'entrée | voltmètre sur le nœud collecteur, critères écrits | lit « bas » sous **0,825 V** (0,25 × VDD), « haut » au-dessus de **2,475 V** (0,75 × VDD) |
| Masse ESP32 | `GND_ESP` = masse de simulation | — |

Côté machine, un capteur industriel (fin de course, bouton, contact auxiliaire
de relais thermique, interrupteur de protecteur) est un **contact sec sous
24 V** : il est modélisé par la batterie B1 (24 V), un contact `SW-SPST` (NO ou
NC selon l'organe) et la résistance série R1.

Trois domaines, trois masses, trois sources — et rien d'autre :

| Domaine | Source | Masse | Bornes |
|---|---|---|---|
| ESP32 (3,3 V) | rail implicite `+3.3V` (borne POWER) | `GND_ESP` → masse de simulation | |
| Commande relais (5 V) | batterie B4 | `GND_5V` | bornes rondes (DEFAULT) `+5V` |
| Machine (24 V) | batterie B1 | `GND_24V` | bornes rondes (DEFAULT) `+24V` |

Les bornes `+5V` et `+24V` sont volontairement de type DEFAULT : une borne
POWER portant une tension crée une source implicite référencée à la masse de
simulation, ce qui relierait les domaines et fausserait l'essai d'isolement.

## 2. Modèles Proteus utilisés — ce qu'ils contiennent

Vérifié dans les fichiers de modèles de l'installation (`DATA/MODELS`).

| Composant | Modèle | Points utiles |
|---|---|---|
| PC817 | `PC8X7` (schématique) | LED : diode `IS = 2,03e-13, N = 1,75, RS = 5 Ω` → V_F ≈ 1,16 V à 10 mA. Phototransistor NPN `BF = 350`, courant de base = I_F × CTR / 35 000. **CTR par défaut = 600 %** pour un `PC817` sans suffixe (maximum datasheet) ; surcharge par la propriété `CTR=<valeur>`. **1 TΩ** entre anode et collecteur, 1 TΩ entre cathode et émetteur : résistance d'isolement du modèle |
| 2N2222 (bibliothèque BIPOLAR) | générique Labcenter `LX_NPN_SSHF` | `BETAF = 200, IKF = 100 mA, RB = 80 Ω, **RC = 30 Ω**` — résistance de collecteur irréaliste (voir §4.2) |
| 2N2222A (bibliothèque ZETEX) | SPICE constructeur | `BF = 220, IKF = 0,52 A, RB = 0,13 Ω, RE = 0,22 Ω, RC = 0,12 Ω, VAF = 104` |
| Relais générique animé | `ACTVRLY` | bobine = résistance `RCOIL` pure (pas d'inductance), contacts = interrupteurs commandés (`ROFF` = 100 MΩ, `RCONTACT` = 0,1 Ω), seuils `VON` / `VOFF`. Aucun chemin conducteur bobine ↔ contacts |
| Voltmètre DC | instrument | résistance d'entrée **100 MΩ** (voir §5, piège n° 3) |

Réglages du relais RL5 pour représenter le SRD-05VDC-SL-C de la nomenclature :
`Value = 5V`, `Coil Resistance = 70` (71,4 mA sous 5 V), `Activate Voltage = 3.75`
(75 % du nominal), `Drop Out Voltage = 0.5` (10 %).

## 3. Bloc d'entrée — 7 canaux

Schéma d'un canal : `+24V → SWn → R1 2,2 kΩ → ampèremètre → PC817 (A, K → GND_24V)` ;
`E → GND_ESP` ; `C → R2 10 kΩ → 3.3V_ESP` ; borne GPIO et voltmètre sur le nœud
collecteur.

| Canal | Borne | GPIO | Contact au repos | Lecture au repos |
|---|---|---|---|---|
| S1 | `S1_D32` | 32 | ouvert | 3,29 V |
| Cp | `Cp_D33` | 33 | **fermé** (capot fermé) | 0,18 V |
| St | `St_D25` | 25 | ouvert | 3,29 V |
| Rt | `Rt_D27` | 27 | ouvert | 3,29 V |
| Dcy | `Dcy_D5` | 5 | ouvert | 3,29 V |
| Rth1 | `Rth1_D34` | 34 (input-only) | **fermé** (contact NC du relais thermique) | 0,18 V |
| Rth2 | `Rth2_D35` | 35 (input-only) | **fermé** (contact NC) | 0,18 V |

Les trois canaux fermés au repos sont ceux de la **sécurité positive** : un fil
coupé donne le niveau haut, qui signifie « arrêt » dans le firmware
(`CAPOT_OUVERT HIGH`, `DEFAUT_THERMIQUE HIGH`).

### 3.1 Programme d'essais et résultats (canal de référence Dcy)

| Essai | B1 | Contact | I_F attendu | V coll. attendu | Critère | **I_F mesuré** | **V mesuré** | Verdict |
|---|---|---|---|---|---|---|---|---|
| Nominal, actif | 24 V | fermé | 10,4 mA | < 0,3 V | V < 0,825 V | **10,4 mA** | **0,18 V** | conforme |
| Nominal, repos | 24 V | ouvert | 0 | 3,3 V | V > 2,475 V | **0,00 mA** | **3,29 V** | conforme |
| 24 V bas (−10 %) | 21,6 V | fermé | 9,3 mA | < 0,3 V | V < 0,825 V | **9,29 mA** | **0,18 V** | conforme |
| 24 V haut (+10 %) | 26,4 V | fermé | 11,5 mA | < 0,3 V | P(R1) = 0,29 W | **11,5 mA** | **0,18 V** | conforme — **R1 en 0,5 W** |
| Fil coupé | 24 V | ouvert | 0 | 3,3 V | = état « arrêt » | **0,00 mA** | **3,29 V** | conforme |

Remarques :

- Le 0,18 V mesuré est le V_CE(sat) du modèle ; la datasheet PC817 donne 0,1 V
  typique, **0,2 V maximum** (I_F = 20 mA, I_C = 1 mA). Le modèle est au
  maximum garanti : la mesure est pessimiste, donc valable comme preuve.
- Le courant de LED mesuré (10,4 mA) correspond à V_F = 1,16 V, valeur du
  modèle : (24 − 1,16) / 2 200 = 10,38 mA.
- Le 10 kΩ externe est **obligatoire** sur GPIO 34 et 35 (pas de tirage
  interne) ; il est présent sur les sept canaux par conception.
- Les sept canaux ont été relevés simultanément en simulation : les trois
  canaux fermés lisent 10,4 mA / 0,18 V, les quatre canaux ouverts 0 / 3,29 V.

## 4. Bloc de sortie — canal KM1 (GPIO 26)

Les quatre voies de sortie (KM1, KM2, A+, A−) sont **strictement identiques** :
même résistance de limitation 220 Ω, même PC817, même polarisation de base
1 kΩ, même 2N2222A, même relais et même diode de roue libre. La voie KM1 a
donc été qualifiée en détail et instrumentée ; les trois autres en sont des
copies conformes, alimentées par le même rail 5 V et chargées par la même
impédance. Qualifier le **type de voie** plutôt que chaque exemplaire est la
pratique usuelle sur un banc de caractérisation. Le schéma de documentation
(`ROUFIX_doc.pdsprj`) montre, lui, les quatre voies câblées.

Schéma : `B2 (3,3 V) → SW8 → A1 → R26 220 Ω → PC817 (A, K → GND_ESP)` ;
`C → +5V` ; `E → A2 → R25 1 kΩ → base Q` ; `collecteur Q → A3 → bas de bobine RL5`,
haut de bobine → `+5V`, D9 1N4007 en roue libre (cathode côté +5V) ;
`émetteur Q → GND_5V`. Contact RL5 : `+24V → contact → L5 (24 V) ‖ D10 → GND_24V`.
V1 entre collecteur et émetteur de Q ; V2 aux bornes de L5.

### 4.1 Qualification du modèle de transistor

Premier essai nominal avec le composant « 2N2222 » de la bibliothèque BIPOLAR :

| Modèle de Q | A1 | A2 | A3 | V1 | Relais | Datasheet 2N2222A : V_CE(sat) ≤ 0,3 V (150 mA / 15 mA) |
|---|---|---|---|---|---|---|
| Générique Labcenter `LX_NPN_SSHF` (RC = 30 Ω) | 9,71 mA | 3,71 mA | **49,4 mA** | **1,54 V** | non | incompatible |
| Constructeur Zetex 2N2222A | 9,71 mA | 4,01 mA | **70,0 mA** | **0,09 V** | oui | compatible |

Lecture : avec le modèle générique, 5 V = I × (70 Ω + 30 Ω) + V_sat →
I = 49,4 mA et V1 = 30 Ω × 49,4 mA ≈ 1,5 V. Le transistor est saturé ; le
1,54 V est la chute dans une résistance de collecteur fictive 300 fois trop
grande (un 2N2222 réel : RC ≈ 0,1 Ω). Le modèle constructeur a été retenu ; la
nomenclature (2N2222, TO-92) n'est pas modifiée, le suffixe A désignant la
tenue 40 V du même composant.

### 4.2 Programme d'essais et résultats (Q = 2N2222A Zetex)

Attendus calculés avec V_F = 1,16 V (modèle).

| Essai | B2 | CTR | SW8 | A1 att. | A2 att. | A3 att. | V1 att. | **A1** | **A2** | **A3** | **V1** | **V2** | Verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Nominal actif | 3,3 V | 600 | fermé | 9,7 mA | ≈ 4 mA | ≈ 70 mA | < 0,3 V | **9,71** | **4,01** | **70,0** | **0,09** | **23,8** | conforme |
| Pire cas ESP32 (V_OH min) | **2,64 V** | 600 | fermé | 6,7 mA | ≈ 4 mA | ≈ 70 mA | < 0,3 V | **6,85** | **4,00** | **70,0** | **0,09** | **23,8** | conforme |
| Pire cas ESP32 + PC817 (CTR min) | 2,64 V | **50** | fermé | 6,7 mA | ≈ 3,4 mA | ≈ 70 mA | < 0,4 V | **6,85** | **3,33** | **69,9** | **0,09** | **23,8** | conforme |
| Broche flottante (reset ESP32) | — | — | ouvert | 0 | 0 | 0 | ≈ 5 V | **0** | **0** | **0** | **5,00** | **0** | charge au repos |
| Optocoupleur mort | 3,3 V | **0** | fermé | 6,9–9,7 mA | 0 | 0 | ≈ 5 V | **6,85** | **0** | **0** | **5,00** | **0** | charge au repos |

Unités : A1, A2, A3 en mA ; V1, V2 en V.

Lecture des essais :

- **Pire cas combiné** : à CTR = 50 %, le courant de base tombe de 4,00 à
  3,33 mA — l'optocoupleur est devenu l'élément limitant, et non plus R25.
  Avec 3,33 mA pour 70 mA de bobine, le gain forcé vaut 21. Le modèle Zetex
  (BF = 220, typique) sature sans effort ; la marge se calcule contre la
  datasheet : h_FE minimum du 2N2222A ≈ 75 à 10 mA, 100 à 150 mA →
  3,33 mA × 75 ≈ 250 mA disponibles pour 70 mA demandés, **coefficient ≈ 3,5**
  au pire ESP32, au pire PC817 et au pire transistor simultanément.
- **Broche flottante** : un pilote optocoupleur actif-haut exige un courant
  positif ; une broche indéfinie (ESP32 en reset, planté, débranché) ne peut
  pas coller le relais. C'est la sécurité au démarrage.
- **Optocoupleur mort** : la LED conduit (A1 = 6,85 mA) mais rien ne traverse :
  une panne de la barrière met la charge au repos, jamais en marche.
- V2 = 23,8 V : 0,2 V dans le contact (0,1 Ω) pour ≈ 2 A dans le modèle de
  lampe. Une bobine de contacteur 24 V tire plutôt 0,2 à 0,3 A ; la valeur
  simulée est un cas plus sévère pour le contact (SRD : 10 A). *(Option :
  régler la résistance de L5 sur celle de la bobine du contacteur réel.)*

## 5. Essais d'isolement (`ROUFIX_banc_isolement.pdsprj`)

Principe : une batterie B_ISO de **500 V** est insérée entre la masse d'un
domaine et la masse de simulation, avec un ampèremètre A_ISO en série
(gamme µA). Tout le domaine est soulevé de 500 V par rapport à l'ESP32 ; le
moindre chemin conducteur se traduit par un courant mesurable. 500 V est la
condition sous laquelle la datasheet PC817 spécifie la résistance d'isolement
(**R_ISO ≥ 5 × 10¹⁰ Ω à 500 V DC**). Le modèle ne simule pas le claquage :
l'essai prouve la **topologie** (absence de chemin), la tenue (5 kV eff.) est
la datasheet.

| Essai | Domaine soulevé | Attendu | **A_ISO mesuré** | Autres instruments | Lecture |
|---|---|---|---|---|---|
| I-1 | 24 V (`GND_24V`) | 7 nA (14 chemins de 1 TΩ dans les 7 PC817 d'entrée) | **+0,01 µA** | 7 canaux d'entrée et bloc de sortie **inchangés** | courant de barrière < 10 nA (résolution) → R_ISO > 50 GΩ, conforme |
| I-2 | 5 V (`GND_5V`) | 1 nA (2 chemins de 1 TΩ dans U13) | **+0,00 µA** | A1, A2, A3, V1, V2 **inchangés** | idem ; aucun chemin bobine ↔ contact dans le relais |
| I-3 contre-essai | 24 V, **U1 percé** (fil entre A et C du PC817 du canal Cp) | ≈ 43 mA : (524 − 3,3) V / (2,2 kΩ + 10 kΩ) | **42,7 mA** (signe selon le sens de câblage) | ampèremètre du canal Cp : **42,7 mA** ; voltmètre du nœud GPIO33 : **+430 V** ; les 6 autres canaux inchangés | le banc **détecte** une barrière percée : GPIO33 verrait 430 V — l'ESP32 est détruit. C'est ce contre quoi la couche d'isolation protège |

Pièges rencontrés et corrigés (à connaître pour refaire l'essai) :

1. **Bornes POWER** : les flèches `5V` / `24V` du schéma initial créaient des
   sources implicites référencées à la masse de simulation, donc à `GND_ESP` :
   les trois domaines étaient reliés. Remplacées par des bornes DEFAULT
   portant le nom des batteries.
2. **Résistance interne du voltmètre** : V_ISO branché entre `GND_24V` et la
   masse, à l'intérieur de la boucle de A_ISO, a d'abord fait lire 5,0 µA —
   soit 500 V / 100 MΩ, le courant du voltmètre lui-même, et une résistance
   d'isolement apparente 500 fois sous la spec. V_ISO a été rebranché aux
   bornes de la batterie seule ; la lecture est tombée à 0,01 µA.
3. **Affichage `+88.8`** : valeur par défaut d'un instrument quand la
   simulation est arrêtée, pas une mesure. Toute lecture se fait simulation en
   marche.
4. **Instruments insérés en série** : deux broches qui se touchent ne sont pas
   toujours connectées dans ISIS ; chaque ampèremètre est relié par des fils
   explicites (l'oubli sur A2 a d'abord ouvert la boucle d'émetteur : A2 = 0,
   relais au repos).
5. **Bornes de même nom** : toutes les bornes portant le même libellé forment
   un seul nœud sur la feuille ; une borne GPIO placée du mauvais côté de la
   résistance de tirage a d'abord ramené 3,3 V sur le collecteur.

## 6. Roue libre — transitoire de coupure (`ROUFIX_banc_rouelibre.pdsprj`)

Montage : inductance L1 = 100 mH en série avec la bobine RL5 (le modèle de
relais n'en a pas) ; C1 = 1 nF aux bornes de l'ensemble (capacité de bobinage) ;
B2 + SW8 remplacés par un générateur PULSE (0 → 3,3 V, largeur 20 ms, période
50 ms) ; mesure à l'oscilloscope, voie A sur le nœud collecteur, voie B sur
`GND_5V` avec **Invert** et mode **A+B** : le domaine 5 V étant flottant, seule
la différence A − B a un sens et donne le V_CE.

100 mH et 1 nF sont des **hypothèses d'ordre de grandeur** : la datasheet du
SRD-05VDC ne donne ni l'inductance ni la capacité de bobinage.

### 6.1 Passe 1 — avec la diode de roue libre D9

Réglages : 1 V/div, 2 ms/div, déclenchement sur front montant de A.

| Grandeur | Attendu | **Mesuré** |
|---|---|---|
| V_CE en conduction | ≈ 0,1 V | **≈ 0,1 V** |
| V_CE au repos (Q1 bloqué, bobine éteinte) | 5 V | **4,95 V** |
| **Plateau à la coupure** | 5 V + V_F | **5,7 V** |
| Chute dans D9 (plateau − repos) | V_F de la 1N4007 à 70 mA | **0,75 V** — datasheet : V_F ≤ 1 V à 1 A |
| **Durée du plateau** | 2,9 ms (calcul ci-dessous) | **3,0 ms** |

La durée n'est pas « 5 τ » : la diode impose une chute quasi constante V_F qui
accélère l'extinction. Le courant s'annule quand
I₀·e^(−t/τ) = (V_F/R)·(1 − e^(−t/τ)), soit

t = τ · ln[(I₀ + V_F/R) / (V_F/R)] = 1,43 ms × ln(80,7 / 10,7) = **2,9 ms**

avec τ = L/R = 100 mH / 70 Ω = 1,43 ms. Mesure et calcul se recoupent à 3 %.

### 6.2 Passe 2 — sans diode de roue libre

D9 supprimé, la simulation s'est d'abord arrêtée sur une **erreur fatale** :

```
[SPICE] DELMIN increased ... due to lack of time precision
[SPICE] TRAN: Timestep too small; timestep = 1.93e-019: trouble with node #U13#00206
```

Cause physique, et non défaut de l'outil : privés de chemin, les 70 mA de la
bobine chargent C1 à I/C = **70 V/µs** vers les ≈ 700 V que donnerait
I × √(L/C). Le modèle SPICE du 2N2222A ne contient pas le claquage par
avalanche, il poursuit donc un état que le composant réel n'atteint jamais ; le
front raide se couple en outre dans le réseau interne du PC817 (nœud `#U13`),
très raide numériquement.

Le mécanisme manquant a été ajouté explicitement : une **diode Zener 1N4754A
(39 V)** entre collecteur et émetteur de Q1, qui représente l'avalanche de la
jonction — V_(BR)CEO du 2N2222A = **40 V minimum** (datasheet). La simulation
converge alors, et le résultat devient celui du composant réel.

Réglages : 5 V/div, 50 µs/div, front montant, One-Shot.

| Grandeur | Attendu | **Mesuré** |
|---|---|---|
| **Plateau d'avalanche** | ≈ 39 V (V_Z ≈ V_(BR)CEO) | **≈ 39–40 V** |
| **Durée du plateau** | t = L·I / (V_Z − V_alim) = 0,1 × 0,07 / 34 = 206 µs | **≈ 200 µs** |
| Retour | chute à 5 V puis oscillation résiduelle L1-C1 | conforme |

### 6.3 Conclusion sur D9

Sans diode de roue libre, **chaque coupure** met la jonction du transistor en
avalanche pendant ≈ 200 µs sous 70 mA : puissance crête ≈ 2,8 W et énergie
½·L·I² = **0,25 mJ par cycle**, dissipées dans une jonction que la datasheet
n'autorise pas à fonctionner en avalanche — destruction par cycles répétés.
Avec D9 : 5,7 V et 0,75 V dans un composant dimensionné pour cela.

Les ≈ 700 V figurent ici comme **ce que la bobine tenterait d'imposer**, donc
comme la raison pour laquelle l'avalanche est inévitable — pas comme une
mesure : le transistor claque bien avant.

Captures associées : `docs/images/rouelibre_avec_D9.png`,
`docs/images/rouelibre_sans_D9.png`.

## 7. Ce que le banc prouve — et ce qu'il ne prouve pas

Prouvé (mesuré) : niveaux logiques aux broches ESP32 dans les limites datasheet
sur toute la plage 24 V ± 10 % ; courant de LED dans les capacités de la broche ;
saturation du transistor de puissance avec un coefficient ≈ 3,5 au pire cas
combiné ; charge au repos sur broche flottante et sur barrière morte ; absence
de chemin conducteur entre les trois domaines ; détection d'une barrière percée.

Non couvert ici, par construction : l'exécution du firmware, le réseau, la
supervision et le diagnostic (banc Wokwi) ; la tenue diélectrique réelle
(datasheet) ; les valeurs inductives exactes de la bobine (hypothèses).
