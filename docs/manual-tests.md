# Test manuali su hardware

Da fare con la scheda collegata e i telefoni del gruppo Pushover a portata di mano. Per vedere
cosa succede: `pio device monitor` (i log iniziano con `[alert]`, `[net]`, `[http]`).
Segnare ogni voce quando riuscita.

Prima di iniziare: LED a riposo = un lampo brevissimo ogni 5 s (Wi-Fi e ora ok). I suoni del
buzzer sono descritti nel README, sezione "Cosa dice il buzzer".

- [ ] **1. Pressione breve**
  - Passi: premere e rilasciare in meno di 2,5 s.
  - Buzzer: un bip acuto appena il bottone scende, poi due bip quando Pushover accetta.
  - LED: lampeggio medio (0,3 s acceso / 0,3 s spento) per 60 s dalla pressione, poi torna al
    lampo ogni 5 s. Il fisso di 3 s di "inviata" non si vede dentro la finestra: la consegna la
    dicono i due bip. Cronometrare: il lampeggio medio finisce a circa 60 s dalla pressione.
  - Telefoni: notifica normale "Richiesta di aiuto", priorità 0, senza suoneria di emergenza.
- [ ] **2. Pressione lunga**
  - Passi: tenere premuto oltre 2,5 s (l'emergenza parte senza aspettare il rilascio).
  - Buzzer: bip acuto alla pressione, trillo veloce allo scadere dei 2,5 s (anche con il dito
    ancora sul bottone), poi un trillo breve ogni 5 s finché nessuno conferma (ritmo completo
    nel test 11).
  - LED: lampeggio veloce continuo (0,1 s acceso / 0,1 s spento).
  - Telefoni: notifica "EMERGENZA" a priorità 2, suona anche in silenzioso/Non disturbare e si
    ripete circa ogni 30 s finché non si conferma.
- [ ] **3. Doppia pressione**
  - Passi: premere breve, poi di nuovo entro 60 s.
  - LED: dopo la prima, lampeggio medio; dopo la seconda, lampeggio veloce.
  - Buzzer: bip a ogni pressione; dopo la seconda, il trillo veloce di emergenza armata
    (appena finito l'invio della prima richiesta, se era ancora in corso). I due bip di
    "richiesta consegnata" non devono suonare dopo il trillo.
  - Telefoni: prima la notifica normale, poi l'emergenza. Ripetere aspettando più di 60 s tra
    le due pressioni: la seconda deve valere come nuova richiesta leggera (a lampeggio medio
    finito). Variante al limite: seconda pressione poco prima della fine del lampeggio medio,
    tenendo il dito giù 1-2 s così che il rilascio cada dopo i 60 s: deve essere emergenza.
- [ ] **4. Conferma dal telefono**
  - Passi: dopo un'emergenza, premere "Acknowledge" su un telefono (entro ~10 s il dispositivo
    interroga la ricevuta).
  - Buzzer: tre note che salgono, due volte; poi i trilli di attesa smettono.
  - LED: da lampeggio veloce a fisso, per 3 minuti, poi torna al lampo ogni 5 s.
  - Telefoni: l'allarme smette di suonare; tutti ricevono "<nome> ha preso in carico
    l'emergenza".
- [ ] **5. Pressione durante "confermata"**
  - Passi: a LED fisso (dopo il test 4) fare una pressione breve.
  - Buzzer: bip acuto alla pressione, poi due bip quando la richiesta è consegnata.
  - LED: lampeggio medio per 60 s, poi di nuovo fisso fino alla fine dei 3 minuti di
    "confermata" (se la pressione è vicina alla fine, torna direttamente a riposo).
  - Telefoni: notifica normale "Richiesta di aiuto", nessuna emergenza.
  - Poi, a lampeggio medio: una seconda pressione breve, oppure una pressione
    tenuta 2,5 s. LED: lampeggio veloce; telefoni: nuova emergenza a tutti.
- [ ] **6. Pressione durante emergenza non confermata**
  - Passi: dopo l'emergenza, senza confermare, premere ancora (breve e lunga).
  - LED: continua a lampeggiare veloce.
  - Buzzer: bip a ogni pressione; una pressione lunga nei primi 30 s dell'emergenza dà il
    trillo veloce di armata, dopo i 30 s le due note che scendono (silenziato, test 13). Poi
    riprendono i trilli di attesa (se non silenziato).
  - Telefoni: nessun nuovo allarme oltre alle ripetizioni normali.
- [ ] **7. Wi-Fi spento durante un'emergenza**
  - Passi: spegnere il router/hotspot, premere a lungo, attendere, riaccendere.
  - Buzzer: bip grave alla pressione; dopo il trillo di emergenza armata, due toni gravi
    lunghi ogni 5 s (l'emergenza non esce). Al ritorno della rete e all'invio, tornano i
    trilli di attesa.
  - LED: lampeggio veloce già durante l'assenza di rete (l'emergenza ha la precedenza
    sull'offline). Dopo che l'emergenza è confermata o finita, se la rete è ancora assente:
    acceso 1 s / spento 1 s.
  - Telefoni: nulla finché la rete è assente; appena torna (il dispositivo ritenta la
    connessione Wi-Fi ogni 60 s) arriva l'emergenza.
  - Variante internet: lasciare acceso il Wi-Fi ma staccare il cavo internet del router, poi
    premere a lungo. Buzzer: dopo il primo invio fallito, i due toni gravi al posto dei trilli
    di attesa, e ogni pressione dà il bip grave; ricollegando il cavo, all'invio riuscito
    tornano i trilli. Premendo di continuo, ogni pressione dà il bip grave.
  - Variante silenzio: con il Wi-Fi spento e l'emergenza da più di 30 s, pressione lunga:
    due note che scendono, poi silenzio (LED sempre a lampeggio veloce). Riaccendere la rete:
    l'emergenza parte, nessun suono di richiamo.
  - Variante: spegnere la rete e fare una pressione breve; al ritorno arriva la richiesta con
    "Richiesta delle HH:MM" con l'ora della pressione.
- [ ] **8. Riavvio durante un'emergenza**
  - Passi: dopo l'emergenza (non ancora confermata) premere EN/reset o togliere e ridare
    corrente.
  - LED: dopo il riavvio, lampeggio veloce subito.
  - Telefoni: se la ricevuta era già salvata il dispositivo riprende ad interrogarla senza
    reinviare; se non c'era, rimanda l'emergenza. Confermare: LED fisso.
  - Variante A: riavviare a LED fisso ("confermata", entro i 3 minuti), togliendo e ridando
    corrente. LED: dopo il riavvio torna **fisso** subito, anche prima che Wi-Fi e ora siano
    pronti; nel log `[alert] restored mode=Confirmed`. Il buzzer **non** deve suonare le tre
    note della conferma. Una pressione breve in questo momento vale come richiesta leggera
    (bip grave se la rete non è ancora pronta; LED a lampeggio medio per 60 s; la richiesta
    parte appena la rete è pronta, con due bip).
  - Variante B: come la A senza premere. Appena l'ora è sincronizzata (log `[net] wall time
    valid`) il LED resta fisso solo per il tempo che mancava ai 3 minuti dalla conferma
    originale, poi torna a riposo (log `mode Confirmed -> Idle`).
  - Variante C: confermare, togliere corrente e ridarla dopo più di 3 minuti. LED: fisso al
    riavvio, poi a riposo appena l'ora è sincronizzata; nessun nuovo allarme.
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
  - LED: due lampi brevi ogni 2 s a riposo, dopo i 60 s di lampeggio medio della pressione
    breve (durante un'emergenza resta il lampeggio veloce).
  - Buzzer: due toni gravi lunghi una volta quando compare l'errore. Durante un'emergenza
    rifiutata gli stessi toni si ripetono (ogni 5 s per 2 minuti, ogni 30 s fino a 10
    minuti, poi ogni 2 minuti) al posto dei trilli di attesa. Le pressioni successive danno il bip grave.
  - Telefoni: nessuna notifica; il dispositivo riprova ogni 5 minuti.
  - Poi ripristinare il token giusto.
- [ ] **11. Richiamo lungo durante un'emergenza**
  - Passi: fare un'emergenza e non confermarla per almeno 13 minuti.
  - Buzzer: un trillo breve ogni 5 s per i primi 2 minuti, poi uno ogni 30 s fino a 10
    minuti, poi uno ogni 2 minuti. Una pressione
    durante l'attesa suona il bip ma non sposta il trillo successivo, che arriva sempre ogni
    5 s (aspettando solo la fine del bip).
- [ ] **12. Finestra "premi ancora" con internet staccato**
  - Passi: lasciare acceso il Wi-Fi ma staccare il cavo internet del router; fare una pressione
    breve e cronometrare. Gli invii falliscono e il ciclo principale resta bloccato a lungo in
    ciascun tentativo.
  - LED: lampeggio medio che finisce comunque a circa 60 s dalla pressione (non più tardi),
    poi lo stato normale.
  - Ripetere premendo una seconda volta a lampeggio medio in corso: deve partire l'emergenza
    (trillo veloce, anche se arriva solo a tentativo finito), con i due toni gravi finché
    internet manca.
- [ ] **13. Silenziare e riattivare i richiami**
  - Passi: fare un'emergenza (pressione lunga) e non confermarla.
  - Subito dopo il trillo di armata, un'altra pressione lunga: di nuovo il trillo di armata,
    i richiami continuano (i primi 30 s non si silenzia).
  - Dopo almeno 30 s, pressione lunga: bip alla pressione, poi due note che scendono; nessun
    richiamo dopo (aspettare almeno 2 minuti). Una pressione breve dà ancora il bip; il LED
    lampeggia sempre veloce; i telefoni continuano a ricevere l'emergenza.
  - Pressione lunga di nuovo: il richiamo suona subito e riprende al suo ritmo.
  - Silenziare ancora e confermare da un telefono: suonano le tre note della conferma. Poi
    una nuova emergenza (pressione lunga): trillo di armata e richiami normali, non
    silenziata.
  - Riavviare durante un'emergenza silenziata: i richiami ripartono (ogni 5 s).
