# ESP Alert — bottone di chiamata per paziente allettato (v1, Pushover)

Data: 2026-10-26

## Intento e perché

Un paziente allettato deve poter chiamare aiuto premendo un bottone. Chi lo
assiste (familiari, badanti) può trovarsi in casa o fuori, anche lontano.
Esistono due situazioni:

- **Emergenza** (infarto, caduta, malore): l'avviso deve essere impossibile da
  ignorare, quindi suona anche col telefono in silenzioso e si ripete finché
  qualcuno non risponde.
- **Richiesta leggera** (acqua, cambio posizione, una mano): una notifica
  normale, senza suonerie forti.

Decisione dell'utente: la v1 usa **Pushover** come canale verso i telefoni,
senza app né server propri. La decisione si rivede solo se nei test reali la
notifica Pushover si dimostra insufficiente. I telefoni dei destinatari sono
Android.

La priorità assoluta è l'affidabilità. L'utente ha chiesto un approccio
"solido, robusto e sicuro".

## Obiettivi

1. Con un **solo bottone** il paziente invia una richiesta leggera o
   un'emergenza (logica sotto).
2. L'emergenza arriva a tutti i destinatari come Pushover priorità 2 (emergency):
   ripetuta finché qualcuno non conferma.
3. Il dispositivo sa **se e chi** ha confermato, lo mostra al paziente (LED) e
   avvisa gli altri destinatari ("<nome> ha preso in carico").
4. Un allarme di emergenza non va **mai perso in silenzio**: se l'invio fallisce
   si riprova finché non riesce; se scade senza conferma, si rimanda.
5. Lo stato di un'emergenza in corso sopravvive a un riavvio dell'ESP32.
6. Le credenziali non finiscono nel repository e le comunicazioni sono cifrate
   e verificate (TLS con verifica del certificato).

## Non-obiettivi (v1)

- App Android propria, server proprio, schermata a tutto schermo con swipe.
- Avviso "dispositivo offline". Il passo successivo consigliato è un heartbeat
  verso healthchecks.io con integrazione Pushover; il codice v1 deve renderlo
  facile da aggiungere, ma non lo implementa.
- Escalation con telefonata (modulo SIM o Twilio).
- Annullamento di un falso allarme dal bottone (chi riceve conferma e chiude).
- Batteria tampone o UPS: in v1 l'ESP32 è alimentato da USB.
- iPhone.

## Anti-obiettivi (cose che rendono il progetto un fallimento anche se "funziona")

- Il dispositivo segnala "inviato" o "preso in carico" quando non è vero.
- Un'emergenza viene declassata a richiesta leggera per colpa
  dell'interpretazione del bottone.
- Un'emergenza si perde in silenzio (errore di rete, riavvio, scadenza).
- Le chiavi Pushover finiscono su git o vengono stampate nei log.
- Il dispositivo si blocca e smette di rispondere al bottone senza riavviarsi.

Questo **non è un dispositivo medico certificato** e non sostituisce il 112. Il
README lo deve dire chiaramente.

## Vincoli

- Hardware attuale: ESP32 classico (ESP32-D0WD-V3 rev 3.1, 4 MB flash, porta
  `/dev/cu.usbserial-0001`). Hardware esterno disponibile: **un bottone e un
  LED**.
- Finché bottone e LED esterni non sono collegati, si usano il **tasto BOOT
  (GPIO0)** e il **LED di bordo** (su molte devkit è GPIO2, da verificare). I
  pin, la logica attiva-alta o attiva-bassa e il pull-up sono definiti in un
  unico punto di configurazione, così si passa all'hardware definitivo senza
  toccare la logica.
- Connessione: Wi-Fi di casa. Se manca internet, l'allarme non può partire;
  questo limite è accettato in v1 (la mitigazione futura è il modulo SIM).
- Pushover: API HTTPS `api.pushover.net`, token dell'applicazione e chiave di
  un **gruppo** con i destinatari.

## Comportamento del bottone (principio: "nel dubbio, emergenza")

Principio: **nel dubbio, emergenza.** Una persona in difficoltà tende a tenere
premuto o a premere ripetutamente; entrambi i gesti devono portare
all'emergenza.

| Gesto | Effetto |
|---|---|
| Pressione singola breve (< 1,5 s; 2,5 s dalla revisione del 2026-10-27) | Richiesta leggera |
| Pressione tenuta ≥ 1,5 s (2,5 s dal 2026-10-27) | Emergenza (scatta al raggiungimento della soglia, senza aspettare il rilascio) |
| Una nuova pressione qualsiasi entro 60 s da una richiesta leggera | Emergenza (copre il "premo di nuovo perché non arriva nessuno" e le pressioni ripetute in preda al panico) |
| Qualsiasi pressione mentre un'emergenza è attiva (non confermata) | Nessun nuovo allarme; il LED ribadisce lo stato |
| Qualsiasi pressione durante la finestra "confermata" (LED fisso, "aiuto in arrivo") | **Rivisto il 2026-10-27:** vale come sempre (breve = leggera, lunga o seconda entro 60 s = emergenza). In origine era una nuova emergenza verso tutti; cambiato per decisione dell'utente, perché il paziente non vede il LED fisso e non può sapere che la finestra è aperta. Una pressione fatta prima della conferma ed elaborata dopo resta ignorata: per un tocco conta quando il bottone scende, per una pressione lunga quando scatta la soglia dei 2,5 s. |

Le soglie (2,5 s e 60 s) sono costanti configurabili. Il debounce è a carico
del builder.

La richiesta leggera parte subito alla prima pressione breve, senza attesa per
distinguere un doppio clic: la richiesta leggera arriva prima, e l'emergenza
eventualmente segue.

## Messaggi Pushover

- **Richiesta leggera:** priorità 0, suono di default, testo tipo "Richiesta di
  aiuto (non urgente)".
- **Emergenza:** priorità 2, `retry` = 30 s (il minimo consentito), `expire`
  = 1500 s, suono `persistent` (i destinatari possono cambiarlo nell'app).
  Titolo e testo inequivocabili ("EMERGENZA — il paziente ha premuto il
  bottone"). Motivo di `expire` = 1500: Pushover limita i retry a 50 qualunque
  sia `expire` (massimo 10800). Con un `expire` più lungo l'allarme
  smetterebbe di suonare dopo circa 25 minuti, ma risulterebbe "non scaduto"
  per ore. Così invece la scadenza coincide con l'ultimo retry e
  il dispositivo rimanda subito una nuova emergenza, che continua a suonare
  senza interruzioni. (Verificato su pushover.net/api il 2026-10-26.)
- **Limiti dell'API rispettati** (pushover.net/api): ricevute interrogate non
  più spesso di una ogni 5 s; dopo un errore 5xx o di rete, nuovo tentativo
  non prima di 5 s; le risposte 4xx (compreso il 429 per quota esaurita) non
  si risolvono riprovando, e molti 4xx in pochi minuti fanno bloccare l'IP,
  quindi dopo un 4xx si riprova al massimo ogni 5 minuti; al massimo 2
  connessioni contemporanee.
- **Presa in carico:** quando la ricevuta indica `acknowledged`, si invia al
  gruppo un messaggio a priorità 0: "<nome dispositivo/utente> ha preso in
  carico l'emergenza". Il nome si ricava da `acknowledged_by_device` o da una
  mappa configurabile chiave utente → nome.
- **Scadenza senza conferma:** se la ricevuta indica `expired` senza
  `acknowledged`, si invia una **nuova** emergenza e si continua a monitorare.

## Ciclo di un'emergenza

1. Lo stato passa a `EMERGENZA_IN_INVIO`, salvato in memoria non volatile (NVS).
2. Invio a Pushover. Gli errori di rete, i 5xx e il 429 si riprovano con
   backoff (con un tetto massimo di pochi secondi tra un tentativo e l'altro,
   perché è un'emergenza). Un 4xx di configurazione (token o chiave errati) non
   si risolve riprovando: va segnalato con un pattern LED specifico, ma i
   tentativi continuano comunque, a cadenza più lenta.
3. Ricevuta ottenuta: stato `EMERGENZA_ATTESA_CONFERMA`, ricevuta salvata in NVS.
4. La ricevuta si interroga periodicamente (intervallo rispettoso dei limiti
   Pushover, indicativamente 10–15 s).
5. Se arriva la conferma: stato `CONFERMATA`, LED fisso per un tempo
   configurabile (es. 10 minuti; 3 minuti dal 2026-10-28), invio del messaggio "presa in carico",
   poi ritorno a `RIPOSO`.
6. Al riavvio, se in NVS c'è un'emergenza non conclusa, la si riprende:
   - con la ricevuta, si riprende il polling;
   - senza ricevuta, si rimanda l'emergenza.

La richiesta leggera segue lo stesso schema di invio con retry, ma non viene
persistita in NVS.

## LED (feedback al paziente)

Pattern distinti e ben riconoscibili per:

- riposo / connesso
- nessuna connessione Wi-Fi o internet
- richiesta leggera inviata (breve conferma)
- emergenza in invio o in attesa di conferma (lampeggio veloce continuo)
- emergenza confermata, "aiuto in arrivo" (fisso)
- errore di configurazione

I pattern esatti li sceglie il builder; vanno documentati nel README.

**Rivisto il 2026-10-28:** aggiunto il pattern della finestra "premi ancora = emergenza" e
portata la finestra "confermata" a 3 minuti; vedi
`2026-10-28-led-upgrade-window-design.md`. Richiami del buzzer silenziabili dal paziente con
una pressione lunga e diradati nel tempo: vedi `2026-10-28-buzzer-mute-design.md`.

## Robustezza

- Riconnessione Wi-Fi automatica; il bottone resta sempre reattivo anche
  mentre la rete è giù (la pressione viene registrata e l'invio parte appena
  possibile).
- Watchdog hardware/task: se il firmware si blocca, l'ESP32 si riavvia e
  riprende lo stato dall'NVS.
- Nessuna operazione bloccante lunga nel percorso di lettura del bottone.
- Ora sincronizzata via NTP prima delle chiamate TLS.

## Sicurezza

- TLS verso `api.pushover.net` con **verifica del certificato** (bundle delle
  CA o CA radice pinnata); mai `setInsecure()`.
- Credenziali (SSID, password Wi-Fi, token app, chiave gruppo) in un file
  `secrets.h` escluso da git; nel repository va un `secrets.example.h`.
- I log seriali non stampano token o chiavi.
- Un token rubato dal dispositivo permette solo di mandare messaggi al gruppo
  (falsi allarmi). È un rischio accettato e va documentato.

## Toolchain e test

- **PlatformIO** con framework Arduino. Sceglierlo al posto di arduino-cli
  permette i test unitari sul Mac (`pio test -e native`), indispensabili per
  il TDD della logica.
- La logica va separata dall'hardware: interprete del bottone, macchina a
  stati dell'allarme, decisioni di retry/backoff e parsing delle risposte
  Pushover devono essere codice C++ puro, testabile sul PC. Rete, GPIO, NVS e
  tempo stanno dietro interfacce sottili.
- Test hardware manuali documentati: pressione breve, pressione lunga, doppia
  pressione, Wi-Fi spento durante un'emergenza, riavvio durante un'emergenza,
  conferma da telefono, scadenza.

## Scelte minori

- **Richiesta leggera premuta senza connessione:** resta in coda e parte appena
  torna la rete, con l'ora della pressione nel testo ("richiesta delle 14:32").
  La finestra di 60 s si conta dalla pressione, non dall'invio.
- **429 da Pushover:** può voler dire che la quota mensile è esaurita. Lo si
  tratta come errore di configurazione (stesso pattern LED) e si continua a
  riprovare a cadenza lenta.
- **Riavvio durante lo stato "confermata":** in NVS si salva l'istante della
  conferma. Dopo il riavvio, se la finestra non è ancora finita, il LED
  torna fisso per il tempo che resta.
- **Nome di chi conferma:** si usa `acknowledged_by_device` dalla ricevuta;
  se manca, il testo dice "Qualcuno ha preso in carico l'emergenza".
- **Pin del bottone definitivo:** non GPIO0, perché decide la modalità di boot
  (premuto all'accensione impedisce l'avvio normale). Si sceglie un GPIO
  libero con pull-up interno, bottone verso GND.

## Lasciato al builder

Struttura dei file, nomi, libreria JSON, valori esatti di backoff entro i
vincoli sopra, pattern LED, intervallo esatto di polling entro i limiti
Pushover, durata dello stato "confermata".
