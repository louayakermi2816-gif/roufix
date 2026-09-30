module.exports = c =>
  /Temporisation/.test(c) ? "tempo" :
  /Pression|alimentation pneumatique/.test(c) ? "air" :
  /Distributeur/.test(c) ? "distributeur" :
  /Detecteur/.test(c) ? "detecteur" :
  /Verin|Course du verin/.test(c) ? "verin" :
  /[Cc]able/.test(c) ? "cable" : c;
