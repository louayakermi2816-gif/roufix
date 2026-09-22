// ==========================================
// ROUFIX - Contrôleur ESP32
// Mapping matériel figé (validé sous Proteus)
// Version : contrôle + Wi-Fi + MQTT
// ==========================================
#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h>   // strcat, strcmp, strncpy : construction des causes sans String

// --- Paramètres MQTT ---
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* topic_statut = "roufix/machine1/systeme/statut";
const char* topic_data   = "roufix/machine1/data";
String clientId;

// --- Objets Réseau ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Temporisations réseau ---
unsigned long dernierEssaiMQTT = 0;
const unsigned long delaiReconnexionMQTT = 5000;
unsigned long dernier_heartbeat = 0;
const unsigned long DELAI_HEARTBEAT = 10000;   // 10s pour les tests, 30-60s en final
unsigned long dernier_check_wifi = 0;
const unsigned long DELAI_CHECK_WIFI = 5000;
unsigned long messages_perdus = 0;             // visibilité du mode dégradé

// --- Identifiants Wi-Fi ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --- Entrées ---
#define S1_PIN 32
#define CP_PIN 33
#define ST_PIN 25
#define RT_PIN 27
#define DCY_PIN 5
#define RTH1_PIN 34
#define RTH2_PIN 35

// --- Sorties ---
#define KM1_PIN 26
#define KM2_PIN 19
#define A_PLUS_PIN 21
#define A_MINUS_PIN 23

// --- Constantes logiques (Active LOW via optocoupleurs) ---
#define ETAT_APPUYE LOW
// Securite positive : le contact du capot est ferme quand le capot est
// ferme (broche a la masse). Capot ouvert, fil coupe ou canal d'isolation
// mort donnent tous le niveau haut, donc l'arret.
#define CAPOT_OUVERT HIGH
#define CAPTEUR_ACTIF LOW
#define DCY_INACTIF HIGH
#define DEFAUT_THERMIQUE HIGH

// --- États de la machine ---
enum EtatMachine { ATTENTE, FIXATION, TEMPORISATION, RETOUR };
const char* nomsEtats[] = {"ATTENTE", "FIXATION", "TEMPORISATION", "RETOUR"};

EtatMachine etat_cycle = ATTENTE;
EtatMachine etat_precedent_affichage = RETOUR;

// Mémoire de l'arrêt sécurité : elle permet aux messages de vie
// d'annoncer ARRET_SECURITE plutôt qu'ATTENTE tant que la machine est
// bloquée capot ouvert ou en défaut thermique.
bool securite_active = false;
char causes_actives[64] = "Aucun";

// --- Variables anti-rebond et temps ---
int etat_precedent_S1 = HIGH;
int etat_stable_S1 = HIGH;
unsigned long dernier_changement = 0;
const unsigned long delai_anti_rebond = 50;

unsigned long chrono_depart = 0;
const unsigned long TEMPS_ROGNAGE = 3000;
unsigned long temps_debut_cycle = 0;

// ==========================================
// FONCTIONS RESEAU
// ==========================================

// Publication d'un message JSON structuré
// type_msg : "evenement" ou "heartbeat"
void publierEtat(const char* type_msg, const char* etat, const char* defaut, unsigned long duree) {
  // Mode dégradé : si pas de MQTT, on sort sans bloquer
  if (!mqttClient.connected()) {
    messages_perdus++;                          // trace du nombre de pertes
    return;
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"type_message\":\"%s\",\"etat\":\"%s\",\"defaut\":\"%s\",\"duree_cycle\":%lu}",
           type_msg, etat, defaut, duree);

  mqttClient.publish(topic_data, payload);

  Serial.print("   [MQTT DATA] Publie : ");
  Serial.println(payload);
}

void gererMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Keep-alive : doit tourner à chaque passage
  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }

  // Reconnexion : soumise au délai
  unsigned long tempsActuel = millis();
  if (tempsActuel - dernierEssaiMQTT >= delaiReconnexionMQTT) {
    dernierEssaiMQTT = tempsActuel;

    if (mqttClient.connect(clientId.c_str(), NULL, NULL,
                           topic_statut, 0, true, "{\"statut\":\"Hors ligne\"}")) {
      static int nb_connexions = 0;
      nb_connexions++;
      Serial.print("[MQTT] Connexion #");
      Serial.print(nb_connexions);
      Serial.println(" au broker etablie.");

      if (messages_perdus > 0) {
        Serial.print("[MQTT] Messages perdus pendant la coupure : ");
        Serial.println(messages_perdus);
        messages_perdus = 0;
      }

      mqttClient.publish(topic_statut, "{\"statut\":\"En ligne\"}", true);

      // Resynchronisation : la supervision a pu rater des evenements
      // pendant la coupure. On lui redonne l'etat courant immediatement,
      // sans quoi elle resterait sur une information perimee.
      publierEtat("evenement",
                  securite_active ? "ARRET_SECURITE" : nomsEtats[etat_cycle],
                  securite_active ? causes_actives : "Aucun",
                  0);
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Tirage interne active sur les entrees a usage general.
  // Le montage (Proteus comme Wokwi) place une resistance de tirage de
  // 10 kohm vers 3V3 sur chaque entree : les optocoupleurs PC817 ont une
  // sortie a collecteur ouvert, ils tirent vers la masse quand ils
  // conduisent et laissent la broche libre sinon. La resistance de tirage
  // INTERNE vient en parallele (10k // 45k), sans changer les niveaux :
  // c'est une securite si une resistance externe venait a manquer.
  pinMode(S1_PIN, INPUT_PULLUP);
  pinMode(CP_PIN, INPUT_PULLUP);
  pinMode(ST_PIN, INPUT_PULLUP);
  pinMode(RT_PIN, INPUT_PULLUP);
  pinMode(DCY_PIN, INPUT_PULLUP);

  // ATTENTION : GPIO34 et GPIO35 sont des entrees SEULES sur ESP32, elles
  // n'ont NI pull-up NI pull-down internes. Le tirage externe de 10 kohm
  // vers 3V3 est donc le SEUL a definir leur etat, il est indispensable.
  //
  // Logique retenue, et elle est a securite positive : au repos la broche
  // est tiree a l'etat HAUT = DEFAUT. Le contact NF du relais thermique
  // (ou l'optocoupleur en Proteus) tire la broche a la masse tant que tout
  // va bien. Un fil coupe ou un capteur debranche est donc vu comme un
  // defaut, jamais comme un fonctionnement normal.
  // En simulation, les deux entrees RTH doivent donc etre reliees a la
  // masse pour que la machine soit autorisee a demarrer.
  pinMode(RTH1_PIN, INPUT);
  pinMode(RTH2_PIN, INPUT);

  pinMode(KM1_PIN, OUTPUT);
  pinMode(KM2_PIN, OUTPUT);
  pinMode(A_PLUS_PIN, OUTPUT);
  pinMode(A_MINUS_PIN, OUTPUT);

  // Sorties a l'arret tant que la logique n'a pas tourne
  digitalWrite(KM1_PIN, LOW);
  digitalWrite(KM2_PIN, LOW);
  digitalWrite(A_PLUS_PIN, LOW);
  digitalWrite(A_MINUS_PIN, LOW);

  Serial.println("Initialisation ROUFIX : mapping materiel definitif...");
  Serial.println("Lancement de la connexion Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setKeepAlive(60);     // 60s au lieu de 15s par defaut
  mqttClient.setBufferSize(512);   // marge pour le payload JSON

  clientId = "ROUFIX_ENSTAB_Louay_";
  clientId += WiFi.macAddress();
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  // ==========================================
  // 1. GESTION RESEAU (independante de l'etat machine)
  // ==========================================
  if (millis() - dernier_check_wifi >= DELAI_CHECK_WIFI) {
    dernier_check_wifi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      // Une deconnexion doit etre traitee, pas seulement signalee : sans
      // tentative de reconnexion, une coupure Wi-Fi serait definitive
      // jusqu'au prochain redemarrage de la carte.
      Serial.println("[RESEAU] Statut : Deconnecte - nouvelle tentative...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }

  gererMQTT();

  // Heartbeat periodique
  if (millis() - dernier_heartbeat >= DELAI_HEARTBEAT) {
    dernier_heartbeat = millis();
    // Tant que la securite est active, le message de vie l'annonce : sinon
    // la supervision ne verrait plus le defaut entre deux evenements.
    publierEtat("heartbeat",
                securite_active ? "ARRET_SECURITE" : nomsEtats[etat_cycle],
                securite_active ? causes_actives : "Aucun",
                0);
  }

  // ==========================================
  // 2. LECTURE DES ENTREES
  // ==========================================
  int lecture_Cp   = digitalRead(CP_PIN);
  int lecture_Dcy  = digitalRead(DCY_PIN);
  int lecture_Rth1 = digitalRead(RTH1_PIN);
  int lecture_Rth2 = digitalRead(RTH2_PIN);

  // ==========================================
  // 3. BLOC SECURITE (Priorite Absolue)
  // ==========================================
  if (lecture_Cp == CAPOT_OUVERT || lecture_Dcy == DCY_INACTIF ||
      lecture_Rth1 == DEFAUT_THERMIQUE || lecture_Rth2 == DEFAUT_THERMIQUE) {

    // Resynchronisation anti-etat-fantome sur S1
    etat_precedent_S1 = digitalRead(S1_PIN);
    etat_stable_S1 = etat_precedent_S1;

    // 1. COUPURE D'ABORD (priorite absolue)
    digitalWrite(KM1_PIN, LOW);
    digitalWrite(A_PLUS_PIN, LOW);
    digitalWrite(KM2_PIN, LOW);
    digitalWrite(A_MINUS_PIN, LOW);

    // 2. PUBLICATION ENSUITE
    // Les causes sont evaluees a CHAQUE passage, et non uniquement lorsqu'un
    // cycle est en cours : une securite deja active au demarrage (capot
    // ouvert, RTH declenche) doit elle aussi etre publiee, faute de quoi
    // l'operateur n'a aucun moyen de comprendre le refus de demarrage.
    // Construction sans objet String : ce bloc tourne en boucle tant que la
    // securite est active, une allocation par passage fragmenterait le tas.
    char causes[64] = "";
    if (lecture_Cp == CAPOT_OUVERT)        { strcat(causes, "Capot_Ouvert "); }
    if (lecture_Dcy == DCY_INACTIF)        { strcat(causes, "Arret_DCY "); }
    if (lecture_Rth1 == DEFAUT_THERMIQUE)  { strcat(causes, "Defaut_RTH1 "); }
    if (lecture_Rth2 == DEFAUT_THERMIQUE)  { strcat(causes, "Defaut_RTH2 "); }
    size_t n = strlen(causes);
    if (n > 0 && causes[n - 1] == ' ') { causes[n - 1] = '\0'; }

    // On publie a l'ENTREE en securite, et a chaque CHANGEMENT de cause
    // (le capot se ferme mais le thermique reste, par exemple).
    if (!securite_active || strcmp(causes, causes_actives) != 0) {
      strncpy(causes_actives, causes, sizeof(causes_actives) - 1);
      causes_actives[sizeof(causes_actives) - 1] = '\0';
      securite_active = true;

      Serial.print("SECURITE: ");
      Serial.print(causes_actives);
      Serial.println(" -> arret immediat.");

      // Duree reelle du cycle avorte, mesuree a l'instant de la coupure.
      unsigned long duree_avortee = (temps_debut_cycle > 0)
                                  ? (millis() - temps_debut_cycle) : 0;

      publierEtat("evenement", "ARRET_SECURITE", causes_actives, duree_avortee);

      // Remise a zero de temps_debut_cycle. Sans elle, l'evenement ATTENTE
      // publie juste apres porterait duree_cycle = millis() -
      // temps_debut_cycle, c'est-a-dire la duree du cycle PLUS toute la
      // duree de l'arret : la supervision compterait cette piece avortee
      // comme une bonne piece, avec un temps de cycle aberrant qui
      // fausserait la moyenne, les limites de controle SPC, la
      // disponibilite et la performance.
      temps_debut_cycle = 0;
    }

    etat_cycle = ATTENTE;
  }

  // ==========================================
  // 4. BLOC CYCLE AUTOMATIQUE
  // ==========================================
  else {
    // Sortie de securite : on l'annonce une seule fois
    if (securite_active) {
      securite_active = false;
      strcpy(causes_actives, "Aucun");
      Serial.println("SECURITE: Conditions retablies, machine disponible.");
      publierEtat("evenement", "ATTENTE", "Securite_Retablie", 0);
    }

    // Condition de fond : KM1 tourne en permanence
    digitalWrite(KM1_PIN, HIGH);

    // ---- Couche evenementielle (1 seule execution par transition) ----
    if (etat_cycle != etat_precedent_affichage) {
      switch (etat_cycle) {
        case ATTENTE: {
          Serial.println("ETAT 0 : En attente... (Appuyez sur S1)");
          unsigned long temps_total = 0;
          if (temps_debut_cycle > 0) {
            temps_total = millis() - temps_debut_cycle;
            Serial.print(">>> TEMPS DU DERNIER CYCLE : ");
            Serial.print(temps_total);
            Serial.println(" ms <<<");
          }
          // Si temps_total vaut 0, le cycle a ete avorte : la supervision
          // ne comptera pas de piece (elle exige duree_cycle > 0).
          publierEtat("evenement", "ATTENTE", "Aucun", temps_total);
          break;
        }
        case FIXATION:
          Serial.println("ETAT 1 : FIXATION (A+ et KM2 ON) -> Attente St");
          temps_debut_cycle = millis();
          publierEtat("evenement", "FIXATION", "Aucun", 0);
          break;
        case TEMPORISATION:
          // La duree annoncee est lue dans TEMPS_ROGNAGE, jamais recopiee.
          Serial.print("ETAT 2 : TEMPORISATION (");
          Serial.print(TEMPS_ROGNAGE / 1000.0, 1);
          Serial.println(" s de rognage...)");
          chrono_depart = millis();
          publierEtat("evenement", "TEMPORISATION", "Aucun", 0);
          break;
        case RETOUR:
          Serial.println("ETAT 3 : RETOUR (A- ON) -> Attente Rt");
          publierEtat("evenement", "RETOUR", "Aucun", 0);
          break;
      }
      etat_precedent_affichage = etat_cycle;
    }

    // ---- Couche action continue et surveillance ----
    switch (etat_cycle) {
      case ATTENTE: {
        int lecture_S1 = digitalRead(S1_PIN);
        if (lecture_S1 != etat_precedent_S1) { dernier_changement = millis(); }
        if ((millis() - dernier_changement) > delai_anti_rebond) {
          if (lecture_S1 != etat_stable_S1) {
            etat_stable_S1 = lecture_S1;
            if (etat_stable_S1 == ETAT_APPUYE) { etat_cycle = FIXATION; }
          }
        }
        etat_precedent_S1 = lecture_S1;
        break;
      }
      case FIXATION: {
        if (digitalRead(ST_PIN) == CAPTEUR_ACTIF) { etat_cycle = TEMPORISATION; }
        break;
      }
      case TEMPORISATION: {
        if (millis() - chrono_depart >= TEMPS_ROGNAGE) { etat_cycle = RETOUR; }
        break;
      }
      case RETOUR: {
        if (digitalRead(RT_PIN) == CAPTEUR_ACTIF) { etat_cycle = ATTENTE; }
        break;
      }
    }

    // ---- Pilotage groupe des sorties ----
    if (etat_cycle == FIXATION || etat_cycle == TEMPORISATION) {
      digitalWrite(A_PLUS_PIN, HIGH);
      digitalWrite(KM2_PIN, HIGH);
    } else {
      digitalWrite(A_PLUS_PIN, LOW);
      digitalWrite(KM2_PIN, LOW);
    }

    if (etat_cycle == RETOUR) {
      digitalWrite(A_MINUS_PIN, HIGH);
    } else {
      digitalWrite(A_MINUS_PIN, LOW);
    }
  }
}
