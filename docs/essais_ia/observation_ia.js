// La sortie reste bornee (enum), mais l'IA recoit en plus l'observation libre de
// l'operateur. Un tableau if/else ne sait pas lire une phrase : l'IA le peut-elle ?
const fs=require("fs");
const F=JSON.parse(fs.readFileSync("C:/Users/GM Louay/.node-red/flows.json","utf8"));
const code=F.find(x=>x.type==="function"&&x.name==="Preparer prompt IA").func;
const run=(msg)=>{const s={};return new Function("msg","flow","node","context","global","env",code)(msg,{get:k=>s[k],set:(k,v)=>{s[k]=v;}},{warn(){},status(){},error(){}},{get(){},set(){}},{get(){},set(){}},{get(){}});};
const CAS=[
 ["cycle long", {statut:"DERIVE HAUTE - cycle trop long", cycle:4700, moyenne:4000, LSC:4600, LIC:3400, alerte:true}, [
   [null, null],
   ["On entend un sifflement d'air au niveau du verin.", "Pression pneumatique insuffisante"],
   ["Le verin avance par a-coups et grince.", "Verin de fixation lent (frottement, graissage)"],
   ["Le voyant du detecteur St s'allume en retard alors que le verin est deja en butee.", "Detecteur de fin de course St ou Rt mal regle"],
 ]],
 ["defaut RTH1", {type_alerte:"defaut",etat:"ARRET_SECURITE",statut:"DEFAUT MACHINE",code_defaut:"Defaut_RTH1",libelle:"défaut thermique moteur M1 (disque abrasif)",moteur:"M1",nb_pieces:3}, [
   [null, null],
   ["Le moteur M1 est couvert de poussiere, les grilles d'aeration sont bouchees.", "Ventilation du moteur M1 obstruee"],
   ["Le disque tourne difficilement a la main, on entend un frottement.", "Blocage ou frottement du disque abrasif"],
   ["Le moteur M1 est froid au toucher, et pourtant RTH1 a declenche.", "Relais thermique RTH1 deregle ou defaillant"],
 ]],
];
(async()=>{
  let ok=0,n=0,tt=0;
  for(const [nom,p,obs] of CAS){
    console.log("\n=== "+nom+" ===");
    for(const [texte,attendu] of obs){
      const req=(a=>Array.isArray(a)?a[0]:a)(run({payload:{...p}}));
      const body={...req.payload};
      if(texte){
        body.prompt=body.prompt.replace("\n\nClasse les causes de la plus probable a la moins probable au vu de ces mesures,",
          "\nOBSERVATION DE L'OPERATEUR : \""+texte+"\"\n\nClasse les causes de la plus probable a la moins probable au vu de ces mesures et de l'observation de l'operateur,");
      }
      const t0=Date.now();
      const j=await (await fetch("http://localhost:11434/api/generate",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)})).json();
      const dt=(Date.now()-t0)/1000; tt+=dt;
      let o={}; try{o=JSON.parse(j.response);}catch(e){}
      const c1=(o.causes_probables||[])[0];
      const verdict=attendu?(c1===attendu?"OK ":"NON"):"   ";
      if(attendu){n++; if(c1===attendu) ok++;}
      console.log(`${verdict} ${dt.toFixed(1).padStart(5)} s | obs: ${(texte||"(aucune)").slice(0,62).padEnd(62)} -> 1re cause: ${c1}`);
    }
  }
  console.log(`\nobservation correctement prise en compte : ${ok}/${n}  | latence moyenne ${(tt/8).toFixed(1)} s`);
})().catch(e=>console.log("ERREUR",e.message));
