// Observation libre de l'operateur -> cause du referentiel.
// (a) embeddings sur le libelle seul, (b) embeddings sur une fiche de symptomes,
// (c) llama3.2:1b interroge sur la seule observation, ordre des causes melange.
const OLL="http://localhost:11434";
// Referentiel : libelle (identique a Node-RED) + symptomes typiques observables
const REF={
 long:[
  ["Pression pneumatique insuffisante","fuite d'air, sifflement pneumatique, manometre bas, compresseur faible, tous les mouvements du verin ralentis"],
  ["Verin de fixation lent (frottement, graissage)","tige du verin qui frotte ou grince, mouvement saccade, verin sec ou mal lubrifie, joints uses"],
  ["Detecteur de fin de course St ou Rt mal regle","voyant du detecteur de position allume en retard ou trop tot, capteur decale de sa cible, verin en butee non detecte"],
  ["Distributeur A+/A- lent a commuter","electrovanne ou bobine du distributeur qui commute en retard, claquement tardif, bobine chaude"],
  ["Temporisation de rognage mal parametree","duree de rognage differente de trois secondes, parametre modifie, reglage de la minuterie change"]],
 rth1:[
  ["Disque abrasif M1 use : effort de coupe excessif","disque abrasif use, diametre reduit, coupe qui force, etincelles, disque a remplacer"],
  ["Blocage ou frottement du disque abrasif","disque dur a tourner a la main, frottement, bruit de frottement, odeur de brule, disque qui accroche"],
  ["Ventilation du moteur M1 obstruee","moteur M1 encrasse, poussiere, grilles ou ouies d'aeration bouchees, ventilateur du moteur bloque"],
  ["Cadence trop elevee pour le refroidissement de M1","pieces enchainees sans pause, cadence tres elevee, moteur qui n'a pas le temps de refroidir"],
  ["Relais thermique RTH1 deregle ou defaillant","moteur froid mais relais declenche, calibre du relais thermique trop bas, relais qui saute sans raison"]],
 rth2:[
  ["Bourrage ou coincement du cable dans le guide","cable coince, plie ou bloque dans le guide, cable qui n'avance plus"],
  ["Cable de section trop importante pour l'entrainement","cable plus gros ou plus rigide que prevu, mauvaise reference de cable, section superieure"],
  ["Frein ou point dur sur la ligne d'entrainement","point dur en tournant l'entrainement a la main, rouleau freine, resistance mecanique"],
  ["Ventilation du moteur M2 obstruee","moteur M2 encrasse, poussiere, ailettes ou grilles de refroidissement bouchees"],
  ["Relais thermique RTH2 deregle ou defaillant","moteur M2 froid mais relais declenche, calibre du relais RTH2 trop bas"]],
 court:[
  ["Detecteur de fin de course colle ou en court-circuit","capteur de fin de course toujours actif, voyant allume en permanence, cable du capteur en court-circuit"],
  ["Course du verin non effectuee","verin qui ne va pas jusqu'au bout, course incomplete, verin qui s'arrete a mi-course"],
  ["Cable absent : serrage a vide","pas de cable dans la machine, serrage sur rien, guide vide"]]};
// Observations de test : formulations volontairement differentes des fiches
const OBS=[
 ["long","Il y a une fuite d'air qui siffle pres du verin",0],
 ["long","Le manometre du reseau affiche 3 bars au lieu de 6",0],
 ["long","La tige du verin sort par saccades et grince",1],
 ["long","Le verin semble sec, il n'a pas ete graisse depuis longtemps",1],
 ["long","Le temoin du capteur St s'allume bien apres que le verin soit arrive",2],
 ["long","L'electrovanne du verin claque avec un temps de retard",3],
 ["long","Quelqu'un a change la duree de rognage sur la machine",4],
 ["rth1","Le moteur M1 est plein de poussiere et ses ouies sont obstruees",2],
 ["rth1","Le disque est dur a faire tourner a la main et ca frotte",1],
 ["rth1","Le disque est tres use, il a perdu beaucoup de diametre",0],
 ["rth1","Le moteur est froid mais le relais thermique a quand meme saute",4],
 ["rth1","On a enchaine les pieces sans aucune pause toute la matinee",3],
 ["rth2","Le cable est coince dans le guide",0],
 ["rth2","On passe un cable beaucoup plus gros que d'habitude",1],
 ["rth2","Il y a un point dur quand on tourne l'entrainement a la main",2],
 ["rth2","Les ailettes de refroidissement du moteur M2 sont encrassees",3],
 ["court","Le capteur de fin de course reste allume en permanence",0],
 ["court","Le verin ne va pas jusqu'au bout de sa course",1],
 ["court","Il n'y avait pas de cable dans la machine",2]];
const cos=(a,b)=>{let d=0,x=0,y=0;for(let i=0;i<a.length;i++){d+=a[i]*b[i];x+=a[i]*a[i];y+=b[i]*b[i];}return d/Math.sqrt(x*y);};
const embed=async t=>(await (await fetch(OLL+"/api/embed",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({model:"nomic-embed-text",input:t,keep_alive:"10m"})})).json()).embeddings;
(async()=>{
  const res={a:0,b:0,c:0}; let tE=0,tL=0;
  // index des documents
  const docsA={},docsB={};
  for(const k in REF){ docsA[k]=await embed(REF[k].map(r=>"search_document: "+r[0])); docsB[k]=await embed(REF[k].map(r=>"search_document: "+r[0]+". Symptomes : "+r[1])); }
  let graine=7; const alea=()=>{graine=(graine*1103515245+12345)%2147483648;return graine/2147483648;};
  console.log("obs".padEnd(64),"attendu".padEnd(6),"a  b  c");
  for(const [fam,texte,att] of OBS){
    const t0=Date.now(); const q=(await embed(["search_query: "+texte]))[0]; tE+=Date.now()-t0;
    const best=docs=>docs.map((d,i)=>[cos(q,d),i]).sort((x,y)=>y[0]-x[0])[0];
    const [sa,ia]=best(docsA[fam]); const [sb,ib]=best(docsB[fam]);
    // (c) 1B : ordre melange pour neutraliser le biais de position
    const ordre=REF[fam].map((_,i)=>i).sort(()=>alea()-0.5);
    const prompt=`Observation d'un operateur de maintenance sur une machine de rognage de cables : "${texte}"\nParmi les causes suivantes, laquelle correspond le mieux a cette observation ?\n${ordre.map((i,n)=>(n+1)+". "+REF[fam][i][0]).join("\n")}\nReponds par le numero.`;
    const t1=Date.now();
    const j=await (await fetch(OLL+"/api/generate",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({model:"llama3.2:1b",prompt,stream:false,keep_alive:-1,format:{type:"object",properties:{numero:{type:"integer",enum:ordre.map((_,n)=>n+1)}},required:["numero"]},options:{temperature:0,num_predict:20}})})).json();
    tL+=Date.now()-t1;
    let ic=-1; try{ic=ordre[JSON.parse(j.response).numero-1];}catch(e){}
    const m=x=>x===att?"✔ ":"✘ "; res.a+=ia===att; res.b+=ib===att; res.c+=ic===att;
    console.log(texte.slice(0,63).padEnd(64),String(att).padEnd(6),m(ia)+m(ib)+m(ic), " (b: "+sb.toFixed(2)+" -> "+REF[fam][ib][0].slice(0,34)+")");
  }
  const N=OBS.length;
  console.log(`\n(a) embeddings, libelle seul        : ${res.a}/${N}`);
  console.log(`(b) embeddings, fiche de symptomes  : ${res.b}/${N}   | ${Math.round(tE/N)} ms par observation`);
  console.log(`(c) llama3.2:1b, ordre melange      : ${res.c}/${N}   | ${(tL/N/1000).toFixed(1)} s par observation`);
})().catch(e=>console.log("ERREUR",e.message));
