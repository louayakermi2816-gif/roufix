// ============================================================
// PC817 — barriere galvanique d'entree, modele comportemental
//
// Wokwi est un simulateur NUMERIQUE : il ne calcule ni courant de
// diode ni gain de transistor. Cette puce ne cherche donc pas a
// simuler l'optocoupleur, elle applique le CONTRAT D'INTERFACE que
// le banc Proteus a mesure :
//
//     contact ferme  -> collecteur tire a la masse  (0,18 V mesure)
//     contact ouvert -> collecteur en haute impedance (3,29 V via le
//                       tirage externe de 10 kOhm)
//
// Le collecteur ouvert est reproduit fidelement : la broche C n'est
// jamais forcee a l'etat haut, elle est relachee. C'est la resistance
// de tirage du schema qui definit l'etat de repos, exactement comme
// sur la carte.
//
// Brochage : A et K cote machine (24 V), C et E cote ESP32 (3,3 V).
// Aucun lien electrique entre les deux cotes a l'interieur de la puce.
//
// Attribut "defaut" (diagram.json) :
//   0 = aucun    voie saine
//   1 = ouvert   optocoupleur detruit ou fil coupe : C reste relache,
//                l'ESP32 lit en permanence l'etat haut
//   2 = court    voie collee : C reste a la masse
// ============================================================

#include "wokwi-api.h"
#include <stdlib.h>

typedef struct {
  pin_t pin_a;
  pin_t pin_k;
  pin_t pin_c;
  uint32_t attr_defaut;
} etat_t;

static void appliquer(etat_t *etat) {
  switch (attr_read(etat->attr_defaut)) {
    case 1:  // voie morte : le collecteur ne conduit plus jamais
      pin_mode(etat->pin_c, INPUT);
      return;
    case 2:  // voie collee : le collecteur reste a la masse
      pin_mode(etat->pin_c, OUTPUT_LOW);
      return;
    default:
      break;
  }

  // La LED conduit quand l'anode est au potentiel machine et que la
  // cathode est ramenee au 0 V machine. K est tire au haut en interne :
  // une cathode debranchee interrompt donc la conduction, comme en vrai.
  const bool conduit = (pin_read(etat->pin_a) == HIGH) &&
                       (pin_read(etat->pin_k) == LOW);

  pin_mode(etat->pin_c, conduit ? OUTPUT_LOW : INPUT);
}

static void sur_changement(void *user_data, pin_t pin, uint32_t value) {
  appliquer((etat_t *)user_data);
}

void chip_init(void) {
  etat_t *etat = malloc(sizeof(etat_t));

  // Cote machine (24 V)
  etat->pin_a = pin_init("A", INPUT_PULLDOWN);
  etat->pin_k = pin_init("K", INPUT_PULLUP);

  // Cote ESP32 (3,3 V) — C demarre en haute impedance
  etat->pin_c = pin_init("C", INPUT);
  pin_init("E", INPUT);

  etat->attr_defaut = attr_init("defaut", 0);

  const pin_watch_config_t surveillance = {
    .user_data = etat,
    .edge = BOTH,
    .pin_change = sur_changement,
  };
  pin_watch(etat->pin_a, &surveillance);
  pin_watch(etat->pin_k, &surveillance);

  appliquer(etat);
}
