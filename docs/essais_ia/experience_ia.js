// Experience : le modele peut-il raisonner si on le libere, et a quel prix ?
// Lecture seule : aucun noeud du flux n'est modifie, Ollama est appele directement.
const fs=require("fs");
const F=JSON.parse(fs.readFileSync("C:/Users/GM Louay/.node-red/flows.json","utf8"));
const code=F.find(x=>x.type==="function"&&x.name==="Preparer prompt IA").func;
const URL="http://localhost:11434/api/generate";
const run=(msg,store)=>new Function("msg","flow","node","context","global","env",code)(msg,{get:k=>store[k],set:(k,v)=>{store[k]=v;}},{warn(){},status(){},error(){}},{get(){},set(){}},{get(){},set(){}},{get(){}});

const SIT=[
 ["long / moderee",  {statut:"DERIVE HAUTE - cycle trop long", cycle:4700, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["long / severe",   {statut:"DERIVE HAUTE - cycle trop long", cycle:14699, moyenne:4612, LSC:7906, LIC:1317, alerte:true}],
 ["court / moderee", {statut:"DERIVE BASSE - cycle trop court", cycle:3300, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["court / severe",  {statut:"DERIVE BASSE - cycle trop court", cycle:2000, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["defaut RTH1",     {type_alerte:"defaut",etat:"ARRET_SECURITE",statut:"DEFAUT MACHINE",code_defaut:"Defaut_RTH1",libelle:"défaut thermique moteur M1 (disque abrasif)",moteur:"M1",nb_pieces:3}],
 ["defaut RTH2",     {type_alerte:"defaut",etat:"ARRET_SECURITE",statut:"DEFAUT MACHINE",code_defaut:"Defaut_RTH2",libelle:"défaut thermique moteur M2 (entraînement câble)",moteur:"M2",nb_pieces:3}],
];
const HIST={
  graduel:[4010,4080,4150,4230,4300,4380,4450,4520,4610,4700],
  brutal: [3990,4020,3980,4010,3970,4030,4000,3990,4010,4700],
};

// Vocabulaire de la machine : ce qui en sort est une invention
const CONNU=/disque|moteur|\bM1\b|\bM2\b|v[ée]rin|distributeur|pneumati|pression|\bair\b|fin de course|d[ée]tecteur|capteur|\bSt\b|\bRt\b|c[aâ]ble|guide|relais|RTH|ventil|temporisation|alimentation|roulement|accouplement|graiss|lubrif|joint|[ée]tanch|serrage|bourrage|coinc|section|frottement|usure|us[ée]|cadence|refroid|consign|course|r[ée]glage|d[ée]r[ée]gl|blocage|bloqu|entra[iî]nement|arriv[ée]e/i;
const INVENTE=/hydraul|pompe|courroie|huile|logiciel|firmware|wi-?fi|r[ée]seau informatique|sonde de temp|capteur de temp|broche|convoyeur|laser|robot|automate programmable|variateur|codeur|encodeur|engrenage|cha[iî]ne/i;
const verifier=t=>INVENTE.test(t)?"INVENTE":(CONNU.test(t)?"ok":"hors-vocabulaire");

function promptLibre(base, hist){
  // reprend les FAITS et les listes du prompt actuel, change la consigne
  const m=base.match(/^([\s\S]*?)CAUSES :\n([\s\S]*?)\nACTIONS :\n([\s\S]*?)\n\nMESURES :\n([\s\S]*?)\n\nClasse/);
  const [,entete,causes,actions,mesures]=m;
  return `${entete.replace("capteurs de position et de securite.","fins de course St (serrage) et Rt (retour), relais thermiques RTH1 (M1) et RTH2 (M2).\nLe rognage est une temporisation fixe de 3 s : un temps de cycle ne peut varier que par le serrage (verin A+ jusqu'a St) et le retour (verin A- jusqu'a Rt).")}MESURES :
${mesures}${hist?"\n- Historique des 10 derniers cycles (ms) : "+hist.join(", "):""}

CAUSES CONNUES SUR CETTE MACHINE (indicatives) :
${causes}
ACTIONS DE MAINTENANCE POSSIBLES :
${actions}

Raisonne a partir des mesures${hist?" et de l'historique":""}. Tu peux reformuler ou combiner ces causes, ou en proposer une autre si les mesures l'exigent, mais uniquement sur les organes listes. N'invente aucun organe.
Reponds en JSON : "raisonnement" (2 phrases maximum, en francais), puis "causes_probables" (2 causes, la plus probable d'abord), puis "action_immediate" (une action).`;
}
const SCHEMA_LIBRE={type:"object",properties:{raisonnement:{type:"string"},causes_probables:{type:"array",items:{type:"string"},minItems:2,maxItems:2},action_immediate:{type:"string"}},required:["raisonnement","causes_probables","action_immediate"]};

async function appel(model, p, variante, hist){
  const req=(a=>Array.isArray(a)?a[0]:a)(run({payload:{...p}},{}));
  const body={...req.payload, model, keep_alive: model.includes("1b")?-1:"90s"};
  if(variante==="enum" && hist){ body.prompt=body.prompt.replace("\n\nClasse","\n- Historique des 10 derniers cycles (ms) : "+hist.join(", ")+"\n\nClasse"); }
  if(variante==="libre"){ body.prompt=promptLibre(req.payload.prompt,hist); body.format=SCHEMA_LIBRE; body.options={...body.options,num_predict:260}; }
  const t0=Date.now(); const r=await fetch(URL,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
  const j=await r.json(); const dt=(Date.now()-t0)/1000;
  let o=null; try{o=JSON.parse(j.response);}catch(e){}
  return {dt, o, brut:j.response};
}
const court=s=>String(s||"").replace(/\s+/g," ").slice(0,150);
(async()=>{
  const res=[];
  for(const model of ["llama3.2:1b","llama3.2:latest"]){
    console.log("\n################ "+model+" — variante LIBRE GUIDEE ################");
    for(const [nom,p] of SIT){
      const {dt,o,brut}=await appel(model,p,"libre");
      if(!o){ console.log(nom.padEnd(16),dt.toFixed(1)+" s  JSON INVALIDE :",court(brut)); res.push({model,nom,dt,valide:false}); continue; }
      const txt=[...(o.causes_probables||[]),o.action_immediate].join(" | ");
      const verdicts=[...(o.causes_probables||[]),o.action_immediate].map(verifier);
      console.log(nom.padEnd(16),dt.toFixed(1).padStart(5)+" s  ["+verdicts.join(",")+"]");
      console.log("   raisonnement :",court(o.raisonnement));
      console.log("   causes       :",court((o.causes_probables||[]).join(" / ")));
      console.log("   action       :",court(o.action_immediate));
      res.push({model,nom,dt,valide:true,o,verdicts});
    }
  }
  console.log("\n################ TEST DU CONTEXTE : meme derive (+17 %), historique different ################");
  for(const [model,variante] of [["llama3.2:1b","enum"],["llama3.2:1b","libre"],["llama3.2:latest","libre"]]){
    for(const h of ["graduel","brutal"]){
      const {dt,o}=await appel(model,SIT[0][1],variante,HIST[h]);
      console.log(`${model.padEnd(16)} ${variante.padEnd(6)} ${h.padEnd(8)} ${dt.toFixed(1).padStart(5)} s | ${court((o?.causes_probables||[]).join(" / "))}`);
      if(o?.raisonnement) console.log("   raisonnement :",court(o.raisonnement));
      res.push({model,variante,hist:h,dt,o});
    }
  }
  fs.writeFileSync("experience_ia_resultats.json",JSON.stringify(res,null,1));
  // liberer le 3B
  await fetch(URL,{method:"POST",body:JSON.stringify({model:"llama3.2:latest",keep_alive:0})});
  console.log("\nresultats : experience_ia_resultats.json");
})().catch(e=>console.log("ERREUR",e.message));
