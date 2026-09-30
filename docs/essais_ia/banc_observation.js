// Banc de l'observation operateur : rejoue un jeu de phrases dans les noeuds
// reels du flux (Preparer prompt IA -> Analyser observation -> Ollama /api/embed
// -> Classer observation). Usage : node banc_observation.js phrases_dev.json
// Cause exacte si la cause attendue est au niveau de gravite mesure ; organe
// sinon (le degre vient alors de la mesure du cycle).
const fs = require("fs");
const organe = require(require("path").join(__dirname, "organe.js"));
const F = JSON.parse(fs.readFileSync(process.env.FLOWS || require("path").join(__dirname, "../../node-red/flows.json"), "utf8"));
const fn = n => F.find(x => x.type === "function" && x.name === n).func;
const run = (name, msg, store) => new Function("msg", "flow", "node", "context", "global", "env", fn(name))(
    msg, { get: k => store[k], set: (k, v) => { store[k] = v; } },
    { warn() {}, status() {}, error() {} }, { get() {}, set() {} }, { get() {}, set() {} }, { get() {} });
const P = {
  long_mod:  { statut: "DERIVE HAUTE", cycle: 4700, moyenne: 4000, LSC: 4600, LIC: 3400, alerte: true },
  long_sev:  { statut: "DERIVE HAUTE", cycle: 9000, moyenne: 4612, LSC: 7906, LIC: 1317, alerte: true },
  court_mod: { statut: "DERIVE BASSE", cycle: 3300, moyenne: 4000, LSC: 4600, LIC: 3400, alerte: true },
  court_sev: { statut: "DERIVE BASSE", cycle: 2000, moyenne: 4000, LSC: 4600, LIC: 3400, alerte: true },
  rth1: { type_alerte: "defaut", etat: "ARRET_SECURITE", statut: "DEFAUT", code_defaut: "Defaut_RTH1", moteur: "M1", nb_pieces: 1 },
  rth2: { type_alerte: "defaut", etat: "ARRET_SECURITE", statut: "DEFAUT", code_defaut: "Defaut_RTH2", moteur: "M2", nb_pieces: 1 },
};
const SET = JSON.parse(fs.readFileSync(require("path").join(__dirname, process.argv[2] || "phrases_validation.json"), "utf8"));
(async () => {
  let ok = 0, faux = 0, prudent = 0;
  for (const [lk, texte, att] of SET) {
    const store = {};
    run("Preparer prompt IA", { payload: P[lk] }, store);
    store.diag_anomalie = "alerte"; store.diag_timestamp = Date.now();
    const [req] = run("Analyser observation", { payload: texte, topic: "observation" }, store);
    const r = await (await fetch("http://localhost:11434/api/embed", { method: "POST",
      headers: { "Content-Type": "application/json" }, body: JSON.stringify(req.payload) })).json();
    const res = run("Classer observation", { ...req, payload: r }, store).payload;
    const auNiveau = (store.diag_candidats.retenues || store.diag_candidats.causes).includes(att);
    const bon = auNiveau ? res.cause === att : organe(res.cause) === organe(att);
    let v;
    if (res.etat === "ok") { if (bon) { v = "OK"; ok++; } else { v = "FAUX-SUR"; faux++; } }
    else { v = bon ? "bon-prudent" : "prudent"; prudent++; }
    console.log(v.padEnd(11), lk.padEnd(9), res.etat.padEnd(9), res.score.toFixed(2), (auNiveau ? "cause " : "organe"),
      texte.slice(0, 46).padEnd(46), "->", res.cause.slice(0, 40) + (res.degre_mesure ? " [degre mesure]" : ""));
  }
  console.log(`\nOK ${ok} | FAUX-SUR ${faux} | incertain ${prudent} | total ${SET.length}`);
})().catch(e => console.log("ERREUR", e.message));
