// ==========================================
// ROUFIX - Contrôleur ESP32
// Mapping matériel figé (validé sous Proteus)
// Version : contrôle + Wi-Fi + MQTT
// ==========================================
#include <WiFi.h>
#include <PubSubClient.h>

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
#define CAPOT_OUVERT LOW
#define CAPTEUR_ACTIF LOW
#define DCY_INACTIF HIGH
#define DEFAUT_THERMIQUE HIGH

// --- États de la machine ---
enum EtatMachine { ATTENTE, FIXATION, TEMPORISATION, RETOUR };
const char* nomsEtats[] = {"ATTENTE", "FIXATION", "TEMPORISATION", "RETOUR"};

EtatMachine etat_cycle = ATTENTE;
EtatMachine etat_precedent_affichage = RETOUR;

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

      mqttClient.publish(topic_statut, "{\"statut\":\"En ligne\"}", true);
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(S1_PIN, INPUT);
  pinMode(CP_PIN, INPUT);
  pinMode(ST_PIN, INPUT);
  pinMode(RT_PIN, INPUT);
  pinMode(DCY_PIN, INPUT);
  pinMode(RTH1_PIN, INPUT);
  pinMode(RTH2_PIN, INPUT);

  pinMode(KM1_PIN, OUTPUT);
  pinMode(KM2_PIN, OUTPUT);
  pinMode(A_PLUS_PIN, OUTPUT);
  pinMode(A_MINUS_PIN, OUTPUT);

  Serial.println("Initialisation ROUFIX : mapping materiel definitif...");
  Serial.println("Lancement de la connexion Wi-Fi...");
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
      Serial.println("[RESEAU] Statut : Deconnecte (Tentative en cours...)");
    }
  }

  gererMQTT();

  // Heartbeat periodique
  if (millis() - dernier_heartbeat >= DELAI_HEARTBEAT) {
    dernier_heartbeat = millis();
    publierEtat("heartbeat", nomsEtats[etat_cycle], "Aucun", 0);
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
    if (etat_cycle != ATTENTE) {
      String causes = "";

      if (lecture_Cp == CAPOT_OUVERT) {
        Serial.println("SECURITE: Capot ouvert ! Arret immediat.");
        causes += "Capot_Ouvert ";
      }
      if (lecture_Dcy == DCY_INACTIF) {
        Serial.println("SECURITE: Arret Depart Cycle ! Arret immediat.");
        causes += "Arret_DCY ";
      }
      if (lecture_Rth1 == DEFAUT_THERMIQUE) {
        Serial.println("SECURITE: Defaut Thermique Moteur 1 (Rth1) !");
        causes += "Defaut_RTH1 ";
      }
      if (lecture_Rth2 == DEFAUT_THERMIQUE) {
        Serial.println("SECURITE: Defaut Thermique Moteur 2 (Rth2) !");
        causes += "Defaut_RTH2 ";
      }

      causes.trim();

      publierEtat("evenement", "ARRET_SECURITE", causes.c_str(), 0);
      etat_cycle = ATTENTE;
    }
  }

  // ==========================================
  // 4. BLOC CYCLE AUTOMATIQUE
  // ==========================================
  else {
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
          publierEtat("evenement", "ATTENTE", "Aucun", temps_total);
          break;
        }
        case FIXATION:
          Serial.println("ETAT 1 : FIXATION (A+ et KM2 ON) -> Attente St");
          temps_debut_cycle = millis();
          publierEtat("evenement", "FIXATION", "Aucun", 0);
          break;
        case TEMPORISATION:
          Serial.println("ETAT 2 : TEMPORISATION (3 sec de rognage...)");
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
