# Test manuali su hardware

Da fare con la scheda collegata e i telefoni del gruppo Pushover a portata di mano. Per vedere
cosa succede: `pio device monitor` (i log iniziano con `[alert]`, `[net]`, `[http]`).
Segnare ogni voce quando riuscita.

Prima di iniziare: LED a riposo = un lampo brevissimo ogni 5 s (Wi-Fi e ora ok).

- [ ] **1. Pressione breve**
  - Passi: premere e rilasciare in meno di 1,5 s.
  - LED: fisso per 3 s, poi torna al lampo ogni 5 s.
  - Telefoni: notifica normale "Richiesta di aiuto", priorità 0, senza suoneria di emergenza.
- [ ] **2. Pressione lunga**
  - Passi: tenere premuto oltre 1,5 s (l'emergenza parte senza aspettare il rilascio).
  - LED: lampeggio veloce continuo (0,1 s acceso / 0,1 s spento).
  - Telefoni: notifica "EMERGENZA" a priorità 2, suona anche in silenzioso/Non disturbare e si
    ripete circa ogni 30 s finché non si conferma.
- [ ] **3. Doppia pressione**
  - Passi: premere breve, poi di nuovo entro 60 s.
  - LED: dopo la prima, fisso 3 s; dopo la seconda, lampeggio veloce.
  - Telefoni: prima la notifica normale, poi l'emergenza. Ripetere aspettando più di 60 s tra
    le due pressioni: la seconda deve valere come nuova richiesta leggera.
- [ ] **4. Conferma dal telefono**
  - Passi: dopo un'emergenza, premere "Acknowledge" su un telefono (entro ~10 s il dispositivo
    interroga la ricevuta).
  - LED: da lampeggio veloce a fisso, per 10 minuti, poi torna al lampo ogni 5 s.
  - Telefoni: l'allarme smette di suonare; tutti ricevono "<nome> ha preso in carico
    l'emergenza".
- [ ] **5. Pressione durante "confermata"**
  - Passi: a LED fisso (dopo il test 4) premere il bottone.
  - LED: lampeggio veloce.
  - Telefoni: nuova emergenza a tutti. Controllo: dopo i 10 minuti senza premere, il LED torna
    a riposo, e una pressione breve vale di nuovo come richiesta leggera.
- [ ] **6. Pressione durante emergenza non confermata**
  - Passi: dopo l'emergenza, senza confermare, premere ancora (breve e lunga).
  - LED: continua a lampeggiare veloce.
  - Telefoni: nessun nuovo allarme oltre ai ripetizioni normali.
- [ ] **7. Wi-Fi spento durante un'emergenza**
  - Passi: spegnere il router/hotspot, premere a lungo, attendere, riaccendere.
  - LED: lampeggio veloce già durante l'assenza di rete (l'emergenza ha la precedenza
    sull'offline). Dopo che l'emergenza è confermata o finita, se la rete è ancora assente:
    acceso 1 s / spento 1 s.
  - Telefoni: nulla finché la rete è assente; appena torna (il dispositivo ritenta la
    connessione Wi-Fi ogni 60 s) arriva l'emergenza.
  - Variante: spegnere la rete e fare una pressione breve; al ritorno arriva la richiesta con
    "Richiesta delle HH:MM" con l'ora della pressione.
- [ ] **8. Riavvio durante un'emergenza**
  - Passi: dopo l'emergenza (non ancora confermata) premere EN/reset o togliere e ridare
    corrente.
  - LED: dopo il riavvio, lampeggio veloce subito.
  - Telefoni: se la ricevuta era già salvata il dispositivo riprende ad interrogarla senza
    reinviare; se non c'era, rimanda l'emergenza. Confermare: LED fisso.
  - Variante: riavviare a LED fisso entro i 10 minuti; dopo il riavvio e la sincronizzazione
    dell'ora il LED torna fisso per il tempo che resta.
- [ ] **9. Scadenza senza conferma**
  - Solo in una build di prova, mai nel firmware vero: in
    `lib/alert_core/src/pushover_protocol.cpp`, nella funzione `emergencyBody`, abbassare
    `expire` (per esempio da `"1500"` a `"60"`; `retry` deve restare almeno `"30"`).
    I test `pio test -e native` del protocollo falliranno con questa modifica: è normale.
    Caricare, fare l'emergenza e non confermare.
  - LED: lampeggio veloce senza interruzioni.
  - Telefoni: l'allarme si ripete e dopo la scadenza arriva una **nuova** emergenza, senza
    pause. Nei log compare `expired=1` seguito da un nuovo invio.
  - **Alla fine ripristinare i valori originali (`retry` 30, `expire` 1500), rieseguire i test
    nativi e ricaricare il firmware.** Non rilasciare mai la build di prova.
- [ ] **10. Errore di configurazione**
  - Passi: in `secrets.h` mettere un token sbagliato, ricaricare, premere.
  - LED: due lampi brevi ogni 2 s a riposo (durante un'emergenza resta il lampeggio veloce).
  - Telefoni: nessuna notifica; il dispositivo riprova ogni 5 minuti.
  - Poi ripristinare il token giusto.
