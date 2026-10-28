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

## Decisioni del builder (da verificare in revisione)

1. **"Richiesta inviata" (fisso 3 s) vince sulla finestra.** Nella proposta all'utente
   il fisso 3 s veniva coperto; si sceglie invece di mostrarlo, perché è l'unica
   conferma visiva di consegna per un paziente che non sente il buzzer. Visivamente:
   lampeggio medio → fisso 3 s → lampeggio medio fino alla fine della finestra.
   Costo se sbagliato: 3 s di fisso confondibili con "confermata" (problema già
   presente e documentato, i due fissi si distinguono dalla durata).
2. **Nuovo ordine di priorità** (vince il primo vero):
   1. Emergenza attiva (lampeggio veloce)
   2. Richiesta leggera inviata (fisso 3 s)
   3. Finestra "premi ancora" (lampeggio medio, 60 s)
   4. Confermata (fisso, 3 min)
   5. Errore di configurazione
   6. Offline
   7. Riposo

   Rispetto a oggi "inviata" sale sopra confermata/errore/offline. Effetto pratico nullo
   o quasi: dentro "confermata" sono entrambi fissi; un invio riuscito azzera l'errore
   di configurazione; offline entro 3 s da un invio riuscito è raro e dura al più 3 s.
3. **La fine della finestra la decide il task del LED, non il ciclo principale.** Il
   ciclo principale può restare bloccato in una richiesta HTTPS fino a circa 45 s
   (connessione + handshake + risposta, `kHttpTimeoutMs` = 15 s ciascuno). Se fosse lui
   a spegnere il lampeggio medio, il LED potrebbe dire "premi ancora" fino a 45 s dopo
   la chiusura della finestra: il paziente ripremerebbe convinto di chiamare
   un'emergenza e partirebbe solo un'altra richiesta leggera. Quindi il ciclo
   principale pubblica al task del LED un piano: pattern attuale, istante di fine della
   finestra e pattern da mostrare dopo (calcolato come `led()` a quell'istante). Il
   task del LED, che gira ogni 10 ms su un altro core, passa da solo al pattern
   successivo all'istante giusto. La logica di scelta è pura e testata nei test nativi.
   Il piano si scambia fra i task in modo atomico (nessuna lettura mescolata di campi
   di due piani diversi).
   L'inizio della finestra resta legato al ciclo principale: il gesto è elaborato
   prima dell'invio della richiesta leggera, e il LED viene pubblicato subito dopo.
   Limite noto (già presente per tutti i pattern): se il ciclo è bloccato in una
   richiesta mentre arriva il tocco, il lampeggio medio compare solo quando la
   richiesta finisce; la decisione "seconda pressione = emergenza" invece usa l'istante
   della pressione e resta corretta.
4. **Scadenze aggiornate a ogni giro del ciclo.** Oggi gli stati a tempo (finestra di
   upgrade, "inviata", "confermata") si chiudono solo in `onGesture()` e in
   `nextCommand()`, e `nextCommand()` gira solo con la rete su. Dopo ~49,7 giorni
   offline senza pressioni `millis()` si riavvolge e uno stato vecchio può sembrare di
   nuovo aperto (lampeggio medio, o una pressione letta come seconda della finestra).
   Difetto preesistente, poco probabile, ma la correzione costa poco: un metodo
   `tick(nowMs)` della macchina a stati chiude gli stati scaduti (persistendo se cambia
   qualcosa) e il ciclo principale lo chiama a ogni giro.

## Revisione di tutti i pattern

| Pattern | Oggi | Valutazione | Cambia? |
|---|---|---|---|
| Riposo: lampo 0,05 s ogni 5 s | Wi-Fi e ora ok | Discreto di notte, dice "acceso e collegato" | No |
| Offline: 1 s / 1 s | Niente Wi-Fi o ora non sincronizzata | Distinto da tutti gli altri | No |
| Inviata: fisso 3 s | Pushover ha accettato la richiesta leggera | Ora interrompe il lampeggio medio; ambiguità con "confermata" già documentata | Solo priorità |
| Finestra "premi ancora": 0,3 / 0,3 s | (nuovo) | Vedi sopra | Nuovo |
| Emergenza: 0,1 / 0,1 s | In invio o in attesa di conferma | Il più urgente, copre tutto (anche offline: lo segnala il buzzer) | No |
| Confermata: fisso 10 min | Qualcuno ha premuto "ricevuto" | Vedi sotto | Durata: 3 min |
| Errore di configurazione: due lampi ogni 2 s | Pushover rifiuta le richieste | Coperto durante l'emergenza: limite già documentato, lo segnala il buzzer | No |

**Perché dopo "ricevuto" il LED resta fisso e poi torna a lampeggiare** (domanda
dell'utente): è il comportamento previsto. "Ricevuto" sul telefono porta lo stato a
"confermata": LED fisso, "aiuto in arrivo", per `kConfirmedWindowS` = 600 s (10
minuti, ora 3). Il dispositivo non può sapere quando l'aiuto è arrivato davvero, quindi
alla fine della finestra torna a riposo (lampo ogni 5 s, oppure 1 s / 1 s se offline). La durata è
configurabile in `include/config.h`. Se il ritorno al lampeggio avviene molto prima
della fine della finestra, o il lampeggio è quello veloce dell'emergenza, è un difetto da
segnalare, salvo che nel frattempo il paziente abbia chiamato una nuova emergenza
(pressione lunga, o due tocchi entro 60 s).

## Cosa non cambia

- Il significato dei gesti, la finestra di 60 s, il buzzer e i suoi suoni.
- Gli altri pattern e le loro durate.

## Documentazione

README in italiano e inglese (tabella LED con la nuova riga e lista delle priorità),
CHANGELOG (Unreleased), `docs/manual-tests.md` (test della richiesta leggera, del
doppio tocco e del tocco durante la finestra confermata), nota nella spec originale
che rimanda a questa.

## Verifica

- Test nativi per: il nuovo pattern (tempi), la finestra in `led()` (inizio dalla
  pressione, fine a 60 s esatti, priorità rispetto a inviata/confermata/errore/
  offline/emergenza), il piano LED (pattern dopo la fine), la scelta del pattern nel
  task del LED (prima e dopo l'istante di cambio, con riavvolgimento di `millis()`),
  `tick()` (chiusura degli stati scaduti, nessun alias dopo il riavvolgimento).
- Compilazione firmware, flash sull'ESP32 e avvio.
- Non verificabile qui: l'effetto visivo reale sul LED (va guardato a occhio, test
  manuale).
