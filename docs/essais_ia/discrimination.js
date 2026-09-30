// Six situations, un appel reel a Ollama chacune : combien de reponses distinctes ?
const fs=require("fs");
const F=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
const code=n=>F.find(x=>x.type==="function"&&x.name===n).func;
const url=F.find(x=>x.type==="http request"&&x.name==="Ollama").url;
const SIT=[
 ["long / moderee",  {statut:"DERIVE HAUTE - cycle trop long", cycle:4700, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["long / severe",   {statut:"DERIVE HAUTE - cycle trop long", cycle:14699, moyenne:4612, LSC:7906, LIC:1317, alerte:true}],
 ["court / moderee", {statut:"DERIVE BASSE - cycle trop court", cycle:3300, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["court / severe",  {statut:"DERIVE BASSE - cycle trop court", cycle:2000, moyenne:4000, LSC:4600, LIC:3400, alerte:true}],
 ["defaut RTH1 (M1)",{type_alerte:"defaut",etat:"ARRET_SECURITE",statut:"DEFAUT MACHINE - ARRET_SECURITE",code_defaut:"Defaut_RTH1",libelle:"défaut thermique moteur M1 (disque abrasif)",moteur:"M1",nb_pieces:3}],
 ["defaut RTH2 (M2)",{type_alerte:"defaut",etat:"ARRET_SECURITE",statut:"DEFAUT MACHINE - ARRET_SECURITE",code_defaut:"Defaut_RTH2",libelle:"défaut thermique moteur M2 (entraînement câble)",moteur:"M2",nb_pieces:3}],
];
const run=(name,msg,store)=>new Function("msg","flow","node","context","global","env",code(name))(msg,{get:k=>store[k],set:(k,v)=>{store[k]=v;}},{warn(){},status(){},error(){}},{get(){},set(){}},{get(){},set(){}},{get(){}});
(async()=>{
  const vues=new Set(); let total=0;
  for(const [nom,p] of SIT){
    const store={};
    const out=run("Preparer prompt IA",{payload:{...p}},store); const req=Array.isArray(out)?out[0]:out;
    const t0=Date.now();
    const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(req.payload)});
    const body=await r.json(); const dt=(Date.now()-t0)/1000; total+=dt;
    const res=run("Extraire diagnostic",{...req,payload:body},store); const dg=(Array.isArray(res)?res[0]:res).payload;
    const cle=JSON.stringify([dg.causes_probables,dg.action_immediate]); vues.add(cle);
    console.log(nom.padEnd(18),dt.toFixed(1).padStart(5)+" s  | "+dg.causes_probables.join(" / ")+"  ->  "+dg.action_immediate+(dg.fallback?"  [REPLI]":""));
  }
  console.log("\nreponses distinctes :",vues.size+" / 6","| latence moyenne :",(total/6).toFixed(1)+" s");
})().catch(e=>console.log("ERREUR",e.message));
