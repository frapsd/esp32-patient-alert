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

## Decisioni del builder

1. **Una seconda pressione lunga riattiva** i richiami (interruttore). Un paziente che ha
   silenziato per sbaglio o ci ripensa deve poter tornare indietro; costo minimo, stesso
   gesto. Alla riattivazione il richiamo suona subito (così il paziente sente che è tornato)
   e il ritmo riprende dal punto del diradamento in cui l'emergenza si trova.
2. **Suono "silenziato"**: due note discendenti, 1300 Hz 250 ms poi 800 Hz 400 ms. Distinto
   da tutti gli altri (nessun altro suono scende; il "non esce" è a 500 Hz).
3. **Durata del silenzio:** fino alla fine di quell'emergenza, cioè finché il buzzer non
   riceve più un richiamo (conferma da telefono). Il rinvio automatico dopo 25 minuti senza
   conferma è la stessa emergenza: resta silenziata. Il passaggio da "non esce" a "in attesa
   di conferma" (rete tornata) pure. Un riavvio dimentica il silenzio (lo stato vive nel
   task del bottone, non in NVS): dopo un calo di corrente i richiami ripartono, ed è il lato
   sicuro.
4. **Chi decide:** il task del bottone (core 0, ogni 10 ms), che sa se un'emergenza è attiva
   perché il ciclo principale gli pubblica il richiamo corrente. Così il suono di conferma è
   immediato anche se il ciclo principale è bloccato in un invio (fino a ~105 s). La logica
   sta in `CueScheduler` (puro, test nativi): una pressione lunga con un richiamo attivo e
   nessuna nuova emergenza nello stesso istante commuta il silenzio; senza richiamo attivo
   resta "emergenza armata" come oggi.
   Limite noto: nei pochi millisecondi tra la conferma elaborata dal ciclo e la
   pubblicazione del nuovo stato, il task del bottone vede ancora il richiamo; una pressione
   lunga che cade proprio lì suona "silenziato" ma la macchina a stati la legge come nuova
   emergenza (che parte regolarmente, solo senza il trillo di armata). Finestra di pochi ms
   dopo un'elaborazione, trascurabile; il richiamo della nuova emergenza arriva comunque
   entro 5 s.
5. **Diradamento:** il ritmo dipende dal tempo dall'inizio dell'emergenza (inizio del
   richiamo nel task del bottone), come oggi per i 2 minuti. Costanti in `include/config.h`:
   `kEmergencyChirpMs` 5000, `kEmergencyChirpFastForMs` 120000, `kEmergencyChirpSlowMs`
   30000, nuove `kEmergencyChirpSlowForMs` 600000 e `kEmergencyChirpSlowestMs` 120000.

## Cosa non cambia

- Macchina a stati, invii, Pushover, LED.
- Il significato dei gesti fuori da un'emergenza attiva e non confermata. Durante
  "confermata" una pressione lunga è ancora una nuova emergenza.

## Documentazione

README (italiano e inglese): tabella dei gesti (pressione lunga durante un'emergenza =
silenzia/riattiva il buzzer), tabella del buzzer (nuovo suono, ritmo del richiamo a tre
fasi), riga sul limite "il bottone non spegne il richiamo" se presente. CHANGELOG
(Unreleased). `docs/manual-tests.md`: test 6 (pressione lunga durante emergenza), test 11
(ritmo), nuovo test di silenzio con Wi-Fi spento e riattivazione. Nota nella spec originale.

## Verifica

- Test nativi (prima del codice) su `CueScheduler`: pressione lunga con richiamo attivo →
  suono "silenziato", nessun `dropPending`, nessun richiamo dopo; seconda pressione lunga →
  richiamo subito, poi al ritmo corrente; fine dell'emergenza (richiamo None) azzera il
  silenzio, così la prossima emergenza suona; pressione lunga senza richiamo → emergenza
  armata come oggi; nuova emergenza dal ciclo nello stesso tick di una pressione lunga →
  armata; passaggio Failed → EmergencyWaiting resta silenziato; bip alla pressione e notizie
  (Confirmed) suonano da silenziato; ritmo 5 s / 30 s / 120 s ai confini 120 s e 600 s.
- Compilazione firmware, flash, avvio. Non verificabile qui: il suono reale (test manuali).
