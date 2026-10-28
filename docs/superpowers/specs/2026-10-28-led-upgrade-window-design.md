# LED: finestra "premi ancora = emergenza" e revisione dei pattern

Data: 2026-10-28. Estende `2026-10-26-esp-alert-pushover-design.md` (sezione LED).

## Perché

Dopo una pressione breve (richiesta leggera) una seconda pressione entro 60 s dalla
prima diventa un'emergenza. Oggi il LED non mostra questa finestra: resta al lampo di
riposo e diventa fisso 3 s solo quando Pushover ha accettato la richiesta. Il paziente
non ha modo di vedere che ripremendo scala la priorità. Richiesta dell'utente: rendere
visibile la finestra.

## Decisioni prese con l'utente

- Nuovo pattern **"premi ancora = emergenza"**: lampeggio medio, 0,3 s acceso / 0,3 s
  spento (periodo 600 ms, acceso per `nowMs % 600 < 300`). Sta a metà tra il riposo e
  l'emergenza (0,1 / 0,1 s) e si distingue da offline (1 / 1 s) ed errore di
  configurazione (due lampi ogni 2 s).
- Dura quanto la finestra di upgrade: da quando il bottone è sceso per la richiesta
  leggera fino a 60 s dopo (`kLightUpgradeWindowMs`), cioè esattamente finché una
  seconda pressione diventerebbe emergenza. Compare appena il gesto viene elaborato
  (al rilascio).
- Sta sotto l'emergenza attiva e sopra confermata, errore di configurazione e offline:
  per quei 60 s è l'informazione su cui il paziente può agire.
- **Finestra "confermata" da 10 a 3 minuti** (`kConfirmedWindowS` = 180): 10 minuti di
  LED fisso dopo "ricevuto" sono troppi. Cambia solo la costante e la documentazione;
  la logica (anche al riavvio, con l'ora salvata in NVS) è già parametrica.

## Decisioni del builder (riviste dopo la revisione avversariale della spec)

1. **Ordine di priorità** (vince il primo vero). La finestra entra al secondo posto,
   come proposto all'utente; il resto non cambia:
   1. Emergenza attiva (lampeggio veloce)
   2. Finestra "premi ancora" (lampeggio medio, 60 s)
   3. Confermata (fisso, 3 min)
   4. Errore di configurazione
   5. Offline
   6. Richiesta leggera inviata (fisso 3 s)
   7. Riposo

   Dentro la finestra il fisso 3 s di "inviata" non si vede: la consegna la annuncia il
   buzzer (due bip). Scartata l'alternativa "inviata sopra la finestra": un fisso che
   interrompe il lampeggio si confonde con "confermata" e, se il ciclo resta bloccato
   in un invio successivo, nasconderebbe proprio l'invito a ripremere.
2. **La finestra si chiude solo a pressione elaborata, non a 60 s esatti.** Un tocco
   viene emesso al rilascio, fino a 2,5 s dopo la pressione, e può restare in coda
   mentre il ciclo è bloccato in una richiesta. Oggi `expireTimedState()` dimentica la
   prima pressione a 60 s esatti e `nextCommand()` la chiama ogni ~20 ms con la rete
   su: un secondo tocco premuto a 59 s e rilasciato a 60,3 s diventa una richiesta
   leggera invece di un'emergenza (difetto preesistente, riprodotto dal revisore sulla
   macchina a stati reale). Correzione: la decisione resta strettamente "da pressione a
   pressione < 60 s", ma il ricordo della prima pressione si cancella solo 1 ora dopo
   la fine della finestra (`kLightPressMemoryMs`, interno alla macchina), molto più di
   qualsiasi ritardo di elaborazione e molto meno del riavvolgimento di `millis()`
   (49,7 giorni). Il LED invece guarda l'età della pressione: lampeggio medio solo
   mentre `now - pressione < 60 s`.
3. **I cambi di pattern a tempo li esegue il task del LED.** Il ciclo principale può
   restare bloccato in una richiesta HTTPS fino a circa 105 s (DNS fino a ~31 s, poi
   connessione, handshake, scrittura, intestazioni e corpo fino a 15 s ciascuno; vedi
   `src/pushover_client.cpp`). Se il lampeggio medio lo spegnesse il ciclo, il LED
   potrebbe dire "premi ancora" fino a ~105 s dopo la chiusura della finestra, e il
   paziente ripremerebbe convinto di chiamare un'emergenza. Lo stesso vale, in modo
   meno grave, per la fine di "inviata" e di "confermata".
   Quindi la macchina a stati produce un **piano LED**: il pattern di adesso e, per
   ciascuna scadenza futura già nota (fine finestra, fine "inviata", fine
   "confermata"), il pattern da quell'istante in poi, calcolato con la stessa
   `led()` a quell'istante (al più 4 passi). Gli istanti sono memorizzati come
   distanza dall'istante di calcolo, così il confronto resta corretto anche al
   riavvolgimento di `millis()`; un `now` del task del LED leggermente precedente
   all'istante di calcolo vale come distanza 0. La scelta del passo è una funzione
   pura, testata nei test nativi. Il ciclo principale ripubblica il piano a ogni giro,
   come oggi il pattern.
   Il piano passa fra i due task sotto uno spinlock (`portMUX`, sezione critica di
   FreeRTOS) che copia la struttura intera in scrittura e in lettura: nessuna lettura
   mescolata di due piani. Un `std::atomic` di più di 4 byte non è lock-free
   sull'ESP32 e non si usa.
   Non previsti dal piano (restano legati al ciclo, come oggi): cambi di rete, errori
   di configurazione, nuove pressioni. L'inizio della finestra compare quando il ciclo
   elabora il tocco; se il ciclo è bloccato in quel momento compare a richiesta finita,
   mentre la decisione "seconda pressione = emergenza" usa l'istante della pressione e
   resta corretta.
4. **Scadenze aggiornate a ogni giro del ciclo.** Oggi gli stati a tempo si chiudono
   solo in `onGesture()` e in `nextCommand()`, e `nextCommand()` gira solo con la rete
   su: dopo ~49,7 giorni offline senza pressioni `millis()` si riavvolge e uno stato
   vecchio può sembrare di nuovo aperto. Difetto preesistente e improbabile; la
   correzione costa poco: `tick(nowMs)` della macchina a stati chiude gli stati scaduti
   (con persistenza se cambia qualcosa) e il ciclo lo chiama a ogni giro, dopo aver
   elaborato i gesti in coda. Resta un caso trascurabile nei timer di ritentativo
   (al più un ritardo di 5 minuti una volta ogni 49,7 giorni), non toccato.

## Revisione di tutti i pattern

| Pattern | Oggi | Valutazione | Cambia? |
|---|---|---|---|
| Riposo: lampo 0,05 s ogni 5 s | Wi-Fi e ora ok | Discreto di notte, dice "acceso e collegato" | No |
| Offline: 1 s / 1 s | Niente Wi-Fi o ora non sincronizzata | Distinto da tutti gli altri | No |
| Inviata: fisso 3 s | Pushover ha accettato la richiesta leggera | Dentro la finestra è coperta dal lampeggio medio (resta il buzzer); ambiguità con "confermata" già documentata | No |
| Finestra "premi ancora": 0,3 / 0,3 s | (nuovo) | Vedi sopra | Nuovo |
| Emergenza: 0,1 / 0,1 s | In invio o in attesa di conferma | Il più urgente, copre tutto (anche offline: lo segnala il buzzer) | No |
| Confermata: fisso 10 min | Qualcuno ha premuto "ricevuto" | Vedi sotto | Durata: 3 min |
| Errore di configurazione: due lampi ogni 2 s | Pushover rifiuta le richieste | Coperto durante l'emergenza: limite già documentato, lo segnala il buzzer | No |

**Perché dopo "ricevuto" il LED resta fisso e poi torna a lampeggiare** (domanda
dell'utente): è il comportamento previsto. "Ricevuto" sul telefono porta lo stato a
"confermata" (entro ~10 s, al successivo controllo della ricevuta): LED fisso, "aiuto in
arrivo", per `kConfirmedWindowS` (era 600 s, ora 180). Il dispositivo non può sapere
quando l'aiuto è arrivato davvero, quindi alla fine torna allo stato normale: lampo ogni
5 s, oppure 1 s / 1 s se offline, oppure due lampi ogni 2 s se c'è un errore di
configurazione. Durante "confermata" il fisso può anche lasciare il posto, per 60 s, al
lampeggio medio se il paziente fa un tocco, o al lampeggio veloce se chiama una nuova
emergenza; altrimenti un ritorno al lampeggio prima della fine è un difetto da
segnalare.

## Cosa non cambia

- Il significato dei gesti, la finestra di 60 s, il buzzer e i suoi suoni. Il buzzer
  non annuncia la finestra "premi ancora" (richiesta solo visiva): la frase del README
  "il buzzer ripete quello che dice il LED" va corretta di conseguenza.
- Gli altri pattern e le loro durate.

## Documentazione

- README in italiano e inglese: tabella LED con la nuova riga, lista delle priorità,
  durata "confermata" 3 minuti, frase sul buzzer, nota sulla richiesta leggera in coda
  tra i limiti noti.
- CHANGELOG (Unreleased).
- `docs/manual-tests.md`: richiesta leggera (lampeggio medio per 60 s), doppio tocco,
  tocco durante la finestra confermata (lampeggio medio 60 s, poi di nuovo fisso),
  durate "confermata" a 3 minuti, e un test nuovo con internet staccato e Wi-Fi su (il
  lampeggio medio si ferma comunque a 60 s dalla pressione).
- Nota nella spec originale che rimanda a questa.

## Punti di codice toccati

`LedPattern` (nuovo valore), `ledOn()`, `ledName()` in `src/main.cpp`, la macchina a
stati (`led()`, piano LED, `tick()`, ricordo della prima pressione), il task del LED
(piano al posto del singolo pattern), `publishIo()` e `loop()`, `kConfirmedWindowS`.

## Verifica

- Test nativi (scritti prima del codice) per: il nuovo pattern (tempi); la finestra in
  `led()` (inizio dalla pressione, fine a 60 s esatti, priorità rispetto a
  emergenza/confermata/errore/offline/inviata); il secondo tocco premuto a 59 s ed
  elaborato dopo 60 s con `nextCommand()`/`tick()` in mezzo (deve essere emergenza);
  un primo tocco elaborato dopo la sua finestra (nessun lampeggio medio); il piano LED
  (passi e pattern a ciascuna scadenza, incluse finestra + "inviata" + "confermata"
  insieme); la scelta del passo (prima, a e dopo ciascun istante, istante già passato,
  `now` leggermente prima del calcolo, riavvolgimento di `millis()`); `tick()`
  (chiusura degli stati scaduti con persistenza, nessun alias dopo il riavvolgimento).
- Compilazione firmware, flash sull'ESP32 e avvio.
- Non verificabile qui: l'effetto visivo reale sul LED e la distinguibilità dei
  pattern per il paziente (test manuale, da fare con il paziente).
