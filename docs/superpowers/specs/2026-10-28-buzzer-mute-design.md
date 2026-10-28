# Buzzer: silenziare i richiami di un'emergenza e diradarli nel tempo

Data: 2026-10-28. Estende `2026-10-26-esp-alert-pushover-design.md` e la sezione buzzer
del README.

## Perché

Durante un'emergenza il buzzer ripete un richiamo (trillo "in attesa di conferma", o due
toni gravi "non esce") ogni 5 s per 2 minuti, poi ogni 30 s, senza fine: con il Wi-Fi giù
finché la rete non torna, e anche con la rete finché nessuno preme "ricevuto" (Pushover
fa scadere l'emergenza dopo 25 minuti e il dispositivo la rimanda). Oggi il paziente non
ha modo di fermarlo. Richiesta dell'utente: non torturare il paziente.

## Decisioni prese con l'utente

1. **Silenzia / riattiva con una pressione lunga.** Durante un'emergenza attiva e non
   confermata, una pressione tenuta 2,5 s silenzia i richiami di quell'emergenza, con un
   suono breve di conferma. Oggi lì qualsiasi pressione è ignorata dalla macchina a stati
   (resta così: nessun nuovo allarme).
   - L'emergenza resta attiva: invii, ritentativi, ripetizioni sui telefoni, LED a
     lampeggio veloce non cambiano.
   - Le notizie una tantum suonano comunque (tre note della conferma, trillo di una nuova
     emergenza). Anche il bip alla pressione resta.
   - Pressione lunga e non breve: difficile da fare per sbaglio.
2. **Diradamento automatico** anche senza silenziare: ogni 5 s per i primi 2 minuti, ogni
   30 s fino a 10 minuti dall'inizio dell'emergenza, poi ogni 2 minuti. Mai fermo del tutto:
   con il Wi-Fi giù il buzzer è l'unico avviso per chi è in casa.
3. Rischio accettato: silenziare con il Wi-Fi giù lascia solo il LED; l'invio parte da solo
   appena torna la rete.

## Decisioni del builder (riviste dopo la revisione avversariale della spec)

1. **Una seconda pressione lunga riattiva** i richiami (interruttore): un paziente che ha
   silenziato per sbaglio deve poter tornare indietro. Alla riattivazione il richiamo suona
   subito e il ritmo riprende dalla fase di diradamento in cui l'emergenza si trova.
2. **Suono "silenziato"**: due note discendenti, 1300 Hz 250 ms poi 800 Hz 400 ms. Nessun
   altro suono scende; il "non esce" è a 500 Hz.
3. **Identità dell'emergenza.** La macchina a stati numera le emergenze
   (`emergencySerial()`, 8 bit, cresce solo quando si entra in emergenza da un gesto; il
   rinvio automatico dopo la scadenza Pushover o un rifiuto della ricevuta è la stessa
   emergenza). Il ciclo principale pubblica al task del bottone richiamo e numero insieme,
   in un'unica variabile atomica a 16 bit. Il task del bottone azzera il silenzio quando il
   numero cambia o il richiamo sparisce: non dipende dal cogliere l'istante, di ~20 ms, in
   cui il richiamo è assente tra due emergenze. Dopo un riavvio il task parte senza numero
   noto: il primo numero visto conta come emergenza nuova (non silenziata, fase veloce dei 5 s
   da capo).
4. **Quando una pressione lunga silenzia**, tutte vere:
   - un richiamo è attivo, con lo stesso numero di emergenza già quando il bottone è sceso
     (non solo allo scatto dei 2,5 s): una pressione iniziata prima che il ciclo
     pubblicasse l'emergenza, o che l'ha chiamata, non silenzia;
   - l'emergenza, come vista dal task del bottone, ha almeno 30 s (`kMuteGraceMs`): un
     paziente agitato che ripreme "per sicurezza" subito dopo il trillo di armata non la
     silenzia;
   - non c'è una conferma in arrivo: nessun suono Confirmed in coda né in riproduzione.
   Altrimenti la pressione lunga suona come oggi: trillo di "emergenza armata" e scarto
   delle notizie in coda (durante i primi 30 s il trillo rassicura: l'emergenza è partita).
5. **Silenziato non vuol dire cieco ai peggioramenti:** se, silenziati, il richiamo passa da
   "in attesa di conferma" a "non esce" (rinvio fallito), suona una volta il "non esce" come
   notizia, senza riattivare i richiami. Da "non esce" a "in attesa" (rete tornata) nessun
   suono.
6. **Altro comportamento del buzzer:** silenzia/riattiva interrompe il suono in corso come
   ogni input del paziente; il suono "silenziato" è una notizia, quindi se una pressione lo
   interrompe viene ripetuto. Da silenziato i bip alla pressione e le notizie (conferma,
   consegna) suonano come sempre.
7. **Limite residuo:** la macchina a stati decide in base all'istante dei 2,5 s rispetto alla
   conferma; il task del bottone vede la conferma solo quando il ciclo la pubblica (subito
   dopo averla elaborata, prima di qualsiasi scrittura su NVS). Se una pressione lunga scatta
   esattamente in quel microintervallo suona "silenziato" ma diventa una nuova emergenza:
   il nuovo numero di emergenza riattiva subito i richiami, manca solo il trillo di armata.
8. **Bitset delle notizie a 16 bit:** l'enum dei suoni ha già 8 valori (incluso None); il
   suono "silenziato" non entra in un `uint8_t`. `cueBit()`/`firstPending()` e la variabile
   atomica passano a 16 bit.
9. **Diradamento:** il ritmo dipende dal tempo da quando il task del bottone ha visto
   l'emergenza. Costanti in `include/config.h`: `kEmergencyChirpMs` 5000,
   `kEmergencyChirpFastForMs` 120000, `kEmergencyChirpSlowMs` 30000, nuove
   `kEmergencyChirpSlowForMs` 600000, `kEmergencyChirpSlowestMs` 120000, `kMuteGraceMs`
   30000.

## Cosa non cambia

- Macchina a stati, invii, Pushover, LED.
- Il significato dei gesti fuori da un'emergenza attiva e non confermata. Durante
  "confermata" una pressione lunga è ancora una nuova emergenza.

## Documentazione

README (italiano e inglese): tabella dei gesti (pressione lunga durante un'emergenza da
almeno 30 s = silenzia/riattiva il buzzer), eccezione al principio "nel dubbio, emergenza",
tabella del buzzer (nuovo suono, riga "armata" anche durante i primi 30 s, ritmo a tre
fasi, "non esce" una volta da silenziato), limiti (silenziare non annulla l'allarme; dopo un
riavvio i richiami ripartono). CHANGELOG. `docs/manual-tests.md`: test 2, 6, 7, 10, 11 e un
nuovo test di silenzio/riattivazione con Wi-Fi spento. Commenti in `config.h` e
`buzzer_cue.h` sulle fasi. Nota nella spec originale.

## Verifica

- Test nativi (prima del codice), su `CueScheduler`/`CueEngine` e sulla macchina a stati:
  silenzio con pressione lunga oltre 30 s (suono, niente `dropPending`, nessun richiamo
  dopo, bip e notizie sì); riattivazione (richiamo subito, poi al ritmo della fase); entro
  30 s → armata; pressione iniziata prima dell'emergenza → armata; conferma in coda o in
  riproduzione → armata; numero nuovo → silenzio azzerato, anche senza un richiamo assente
  in mezzo; "in attesa" → "non esce" da silenziato → "non esce" una volta; il contrario →
  niente; ritmo 5 / 30 / 120 s ai confini 120 s e 600 s; `cueBit`/`firstPending` con il
  nuovo suono; `emergencySerial()` cresce con hold e con doppio tocco, non con rinvio per
  scadenza o rifiuto; tabella dei suoni (udibilità) include il nuovo suono.
- Compilazione firmware, flash, avvio. Non verificabile qui: il suono reale (test manuali).
