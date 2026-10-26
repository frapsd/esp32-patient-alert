# ESP Alert — bottone di chiamata per paziente allettato

> **ATTENZIONE: questo NON è un dispositivo medico certificato e NON sostituisce il 112.**
> **Se la situazione è grave, chiamare subito il 112.**
> **Senza internet di casa (o senza corrente) l'allarme NON parte.** Il dispositivo non
> avvisa nessuno se è spento o scollegato dalla rete.

## Cosa fa

Un ESP32 con un bottone e un LED. Il paziente preme il bottone e sui telefoni di chi lo
assiste (familiari, badanti) arriva una notifica Pushover:

- **Richiesta leggera** (acqua, cambio posizione...): notifica normale, senza suoneria forte.
- **Emergenza**: notifica Pushover a priorità 2. Suona anche col telefono in silenzio e si
  ripete ogni 30 secondi finché qualcuno non preme "conferma" sul telefono.

Quando qualcuno conferma, il LED diventa fisso ("aiuto in arrivo") e gli altri ricevono il
messaggio "<nome> ha preso in carico l'emergenza". Se nessuno conferma entro 1500 s (25 minuti),
il dispositivo rimanda subito una nuova emergenza, senza interruzioni. Se il dispositivo si
riavvia durante un'emergenza, la riprende da dove era.

Principio: nel dubbio, emergenza.

## Come si usa il bottone

| Gesto | Effetto |
|---|---|
| Pressione breve (meno di 1,5 s) | Richiesta leggera, parte subito |
| Pressione tenuta 1,5 s o più | Emergenza (scatta allo scadere di 1,5 s, senza aspettare il rilascio) |
| Un'altra pressione entro 60 s da una richiesta leggera | Emergenza |
| Qualsiasi pressione mentre un'emergenza è attiva e non confermata | Nessun nuovo allarme (il LED continua a lampeggiare veloce) |
| Qualsiasi pressione mentre il LED è fisso "confermata" | Nuova emergenza verso tutti |
| Pressione dopo la fine della finestra "confermata" (10 minuti) | Come all'inizio: breve = leggera, lunga = emergenza |

Se manca la rete, la pressione viene ricordata e l'invio parte appena la connessione torna.
Una richiesta leggera in coda riporta l'ora della pressione ("Richiesta delle 14:32"); la
coda tiene al massimo 4 richieste leggere.

## Cosa dice il LED

| LED | Significato |
|---|---|
| Un lampo brevissimo (0,05 s) ogni 5 s | Tutto a posto: Wi-Fi e ora ok, nessun allarme |
| Acceso 1 s, spento 1 s | Offline: niente Wi-Fi, oppure ora non ancora sincronizzata |
| Fisso per 3 s | Richiesta leggera inviata |
| Lampeggio veloce continuo (0,1 s acceso, 0,1 s spento) | Emergenza in invio o in attesa di conferma |
| Fisso | Emergenza confermata, "aiuto in arrivo" (dura 10 minuti) |
| Due lampi brevi ogni 2 s | Errore di configurazione: Pushover rifiuta le richieste (token o chiave sbagliati, quota mensile finita, ecc.) |

Quando due situazioni sono vere insieme, vince la prima di questa lista:

1. Emergenza attiva (lampeggio veloce) — resta così anche se la rete è assente.
2. Emergenza confermata, finché dura la finestra di 10 minuti (fisso).
3. Errore di configurazione.
4. Offline.
5. "Richiesta leggera inviata" (3 s).
6. Riposo.

Nota: "richiesta inviata" (3 s) e "confermata" (10 min) sono entrambi fissi; si distinguono
dalla durata.

## Configurare Pushover

1. Creare un account su <https://pushover.net>.
2. Installare l'app **Pushover** su **ogni telefono** che deve ricevere gli avvisi (Android) e
   accedere con il proprio account. L'app è gratuita per 30 giorni di prova; poi serve una
   licenza una tantum per ogni piattaforma (Android).
3. Sul sito: **Create an Application/API Token**. Il valore ottenuto è il **token
   dell'applicazione** (`PUSHOVER_APP_TOKEN`).
4. Sul sito: **Create a Delivery Group** e aggiungere gli utenti che devono ricevere gli avvisi
   (ognuno invita/ha il proprio account). La **chiave del gruppo** è `PUSHOVER_GROUP_KEY`.
5. Su ogni telefono Android (importante: l'emergenza deve poter suonare):
   - nell'app Pushover, consentire l'override di **Non disturbare** per le notifiche
     di priorità emergenza;
   - impostare un suono e un canale di notifica da **sveglia/allarme** per le emergenze
     (il suono inviato è `persistent`, ogni utente può cambiarlo nell'app);
   - nelle impostazioni Android, **disattivare l'ottimizzazione della batteria** per Pushover
     e lasciare le notifiche attive.
6. Limite: 10.000 messaggi al mese per applicazione. Un messaggio a un gruppo conta una volta
   per ogni membro del gruppo.
7. Provare tutto con la checklist in [docs/manual-tests.md](docs/manual-tests.md).

## Segreti (`secrets.h`)

```
cp include/secrets.example.h include/secrets.h
```

Poi modificare `include/secrets.h` con SSID e password del Wi-Fi, token dell'applicazione e
chiave del gruppo. Il file è escluso da git (`.gitignore`): non va mai committato né condiviso.

## Compilare, testare, caricare

Serve PlatformIO. Installazione usata qui:

```
pipx install --python python3.12 platformio
```

(con il Python 3.14 di Homebrew `pipx install` non funziona). Il comando `pio` si trova in
`~/.local/bin`. Piattaforma: `espressif32@6.13.0`.

```
pio test -e native      # test della logica sul computer, senza scheda
pio run -e esp32dev     # compila il firmware
pio run -e esp32dev -t upload   # carica sulla scheda
pio device monitor      # log seriali a 115200
```

La porta seriale è impostata in `platformio.ini` (`/dev/cu.usbserial-0001`); cambiarla se la
scheda compare con un altro nome. I log non stampano token né chiavi.

### Ripristinare il firmware originale della scheda

Esiste un backup completo della flash originale (`~/esp32-backup.bin`). Per
rimetterlo:

```
esptool --port <porta> write-flash 0 ~/esp32-backup.bin
```

## Sicurezza

- Le comunicazioni con `api.pushover.net` sono cifrate (TLS) e il certificato viene
  **verificato** contro CA radice incorporate nel firmware (`include/pushover_ca.h`):
  DigiCert Global Root G2, DigiCert Global Root CA, ISRG Root X1, USERTrust RSA Certification
  Authority. Il controllo non è mai disattivato. Le prime due coprono la catena attuale di
  Pushover; le altre sono di riserva se Pushover cambia autorità. Se le radici scadono o
  Pushover cambia catena, la verifica fallisce e il firmware va aggiornato.
- Il token dell'applicazione sta nel dispositivo e permette **solo di inviare messaggi al
  gruppo**: chi lo estraesse potrebbe causare falsi allarmi, non leggere altro. Rischio
  accettato; se succede, rigenerare il token su pushover.net.
- `secrets.h` non va mai su git.
- L'ora serve per la verifica TLS: viene presa via NTP (`pool.ntp.org`, `time.google.com`) e
  finché non è valida il dispositivo si considera offline.
- Se il Wi-Fi resta giù per 60 s, il dispositivo riavvia la connessione Wi-Fi. Se il firmware
  si blocca, un watchdog (60 s) riavvia la scheda.

## Spostare il bottone sul pin definitivo

Ora il bottone è il tasto BOOT (GPIO0) e il LED è quello di bordo (GPIO2, da verificare sulla
propria scheda). Per l'hardware finale modificare **solo** `include/config.h`
(`PIN_BUTTON`, `BUTTON_ACTIVE_LOW`, `PIN_LED`, `LED_ACTIVE_HIGH`).

Il bottone definitivo **non deve stare su GPIO0**: quel pin decide la modalità di avvio, e se è
premuto all'accensione la scheda non parte normalmente. Usare un altro GPIO libero con pull-up
interno (il firmware lo attiva: `INPUT_PULLUP`), con il bottone collegato tra il pin e GND.

## Limiti e prossimi passi

- Senza internet o senza corrente l'allarme non parte, e nessuno è avvisato che il dispositivo
  è fuori uso. Alimentazione solo USB, nessuna batteria tampone.
- Passo successivo consigliato: heartbeat verso healthchecks.io con avviso Pushover se il
  dispositivo smette di farsi sentire.
- Altri miglioramenti: escalation con telefonata (modulo SIM), UPS/batteria tampone.
- Solo Android testato/previsto; niente annullamento di un falso allarme dal bottone.
