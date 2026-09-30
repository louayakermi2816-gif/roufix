// Rejoue les 19 observations dans les noeuds du flux construit, avec Ollama reel.
const fs = require("fs");
const F = JSON.parse(fs.readFileSync(process.argv[2] || "flows_obs.json", "utf8"));
const fn = n => F.find(x => x.type === "function" && x.name === n).func;
const run = (name, msg, store) => new Function("msg", "flow", "node", "context", "global", "env", fn(name))(
    msg, { get: k => store[k], set: (k, v) => { store[k] = v; } },
    { warn() {}, status() {}, error() {} }, { get() {}, set() {} }, { get() {}, set() {} }, { get() {} });

// Listes candidates telles que "Preparer prompt IA" les publie
const liste = p => { const s = {}; run("Preparer prompt IA", { payload: p }, s); return s.diag_candidats.causes; };
const L = {
    long:  liste({ statut: "DERIVE HAUTE", cycle: 4700, moyenne: 4000, LSC: 4600, LIC: 3400, alerte: true }),
    rth1:  liste({ type_alerte: "defaut", etat: "ARRET_SECURITE", statut: "DEFAUT", code_defaut: "Defaut_RTH1", moteur: "M1", nb_pieces: 1 }),
    rth2:  liste({ type_alerte: "defaut", etat: "ARRET_SECURITE", statut: "DEFAUT", code_defaut: "Defaut_RTH2", moteur: "M2", nb_pieces: 1 }),
    court: liste({ statut: "DERIVE BASSE", cycle: 2000, moyenne: 4000, LSC: 4600, LIC: 3400, alerte: true }),
};

// Couverture : chaque cause du referentiel a-t-elle une fiche de symptomes ?
const code = fn("Analyser observation");
const tout = new Set();
for (const p of [
    { statut: "DERIVE HAUTE", cycle: 4700, moyenne: 4000, LSC: 4600, LIC: 3400 },
    { statut: "DERIVE HAUTE", cycle: 14000, moyenne: 4000, LSC: 4600, LIC: 3400 },
    { statut: "DERIVE BASSE", cycle: 3300, moyenne: 4000, LSC: 4600, LIC: 3400 },
    { statut: "DERIVE BASSE", cycle: 2000, moyenne: 4000, LSC: 4600, LIC: 3400 },
    { statut: "TENDANCE", cycle: 4000, moyenne: 4000 },
    { type_alerte: "defaut", etat: "ARRET_SECURITE", moteur: "M1" },
    { type_alerte: "defaut", etat: "ARRET_SECURITE", moteur: "M2" },
    { type_alerte: "defaut", etat: "ARRET_SECURITE" },
    { type_alerte: "defaut", etat: "ARRET_SECURITE", moteur: "M1+M2" }]) { liste(p).forEach(c => tout.add(c)); }
const manquantes = [...tout].filter(c => !code.includes("\"" + c + "\""));
console.log("causes du referentiel :", tout.size, "| sans fiche :", manquantes.length ? manquantes : "aucune");

const OBS = [
    ["long", "Il y a une fuite d'air qui siffle près du vérin", 0],
    ["long", "Le manomètre du réseau affiche 3 bars au lieu de 6", 0],
    ["long", "La tige du vérin sort par saccades et grince", 1],
    ["long", "Le vérin semble sec, il n'a pas été graissé depuis longtemps", 1],
    ["long", "Le témoin du capteur St s'allume bien après que le vérin soit arrivé", 2],
    ["long", "L'électrovanne du vérin claque avec un temps de retard", 3],
    ["long", "Quelqu'un a changé la durée de rognage sur la machine", 4],
    ["rth1", "Le moteur M1 est plein de poussière et ses ouïes sont obstruées", 2],
    ["rth1", "Le disque est dur à faire tourner à la main et ça frotte", 1],
    ["rth1", "Le disque est très usé, il a perdu beaucoup de diamètre", 0],
    ["rth1", "Le moteur est froid mais le relais thermique a quand même sauté", 4],
    ["rth1", "On a enchaîné les pièces sans aucune pause toute la matinée", 3],
    ["rth2", "Le câble est coincé dans le guide", 0],
    ["rth2", "On passe un câble beaucoup plus gros que d'habitude", 1],
    ["rth2", "Il y a un point dur quand on tourne l'entraînement à la main", 2],
    ["rth2", "Les ailettes de refroidissement du moteur M2 sont encrassées", 3],
    ["court", "Le capteur de fin de course reste allumé en permanence", 0],
    ["court", "Le vérin ne va pas jusqu'au bout de sa course", 1],
    ["court", "Il n'y avait pas de câble dans la machine", 2],
];

(async () => {
    let ok = 0, surs = 0, fauxSurs = 0, t = 0;
    for (const [fam, texte, att] of OBS) {
        const store = { diag_anomalie: "alerte", diag_candidats: { causes: L[fam] } };
        const [req] = run("Analyser observation", { payload: texte, topic: "observation" }, store);
        const r = await (await fetch("http://localhost:11434/api/embed", { method: "POST",
            headers: { "Content-Type": "application/json" }, body: JSON.stringify(req.payload) })).json();
        const res = run("Classer observation", { ...req, payload: r }, store).payload;
        const bon = res.cause === L[fam][att];
        ok += bon; t += res.duree;
        if (res.etat === "ok") { surs++; if (!bon) { fauxSurs++; } }
        console.log((bon ? "OK " : "NON") + " " + res.etat.padEnd(9) + " " + res.score.toFixed(2) + "  "
            + texte.slice(0, 58).padEnd(58) + " -> " + res.cause.slice(0, 42));
    }
    console.log("\nbonne cause : " + ok + "/" + OBS.length + " | affirmees (>= 0,60) : " + surs
        + ", dont fausses : " + fauxSurs + " | " + Math.round(t / OBS.length) + " ms en moyenne");
})().catch(e => console.log("ERREUR", e.message));
