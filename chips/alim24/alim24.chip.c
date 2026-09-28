// ============================================================
// ALIM24 — alimentation du cote machine
//
// Le cote 24 V de la carte a sa propre source et son propre retour,
// separes de ceux de l'ESP32. Wokwi ne propose pas de symbole
// d'alimentation flottante : cette puce en tient lieu.
//
// VM   : rail machine (etat haut permanent)
// GNDM : retour machine (etat bas permanent)
//
// Elle n'a aucune broche du cote ESP32 : les deux domaines du schema
// ne se touchent qu'a travers les PC817 et les contacts de relais.
// ============================================================

#include "wokwi-api.h"

void chip_init(void) {
  pin_init("VM", OUTPUT_HIGH);
  pin_init("GNDM", OUTPUT_LOW);
}
