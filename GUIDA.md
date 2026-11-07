# Livetrack Degi Digital — Guida

Software Windows per la riproduzione di basi multitraccia dal vivo: **9 tracce indipendenti**, ciascuna instradabile su singolo canale, coppia (1-2, 3-4…) o gruppo (1-2-3-4). Nativo C++20 / JUCE 8, offline, ASIO prioritaria.

## Tracce e routing

- **9 tracce**, nomi personalizzabili: doppio click sul nome nella strip del mixer (es. "Tastiere", "Basso", "Cue", …). I nomi vengono salvati nel progetto.
- **Uscite per traccia**: nella strip (o in Impostazioni → Audio) scegli per ogni traccia:
  - singolo canale: `1`, `2`, `3`, `4`, …
  - coppia: `1-2`, `3-4`, `5-6`, `7-8`
  - gruppo: `1-2-3-4`, `5-6-7-8`
  
  Esempio: Tastiere → 1-2 stereo, Basso → 1-2 stereo, Cue → singolo 3.
- Un file stereo su una coppia esce L/R; su un singolo esce mono; su un gruppo 4 canali viene duplicato sulle due coppie.

## Installazione / avvio

- **Portable**: esegui `LivetrackDegiDigital.exe` dalla cartella di rilascio. Nessuna installazione, nessuna connessione richiesta.
- I progetti vengono salvati in `%APPDATA%\DEGI Digital\last_session.liveset` (ripristino automatico all'avvio) e manualmente dove preferisci con **Salva** (formato `.liveset`, JSON).

## Interfaccia consigliata: Behringer UMC404HD

- 4 uscite fisiche indipendenti: routing 1=Backing L, 2=Backing R, 3=Click, 4=Cue.
- Il driver **UMC ASIO Driver** (Behringer) va installato dal sito Behringer/Music Tribe.
- **ASIO nativo nel programma**: JUCE richiede lo Steinberg ASIO SDK (licenza Steinberg, download gratuito con registrazione da steinberg.net/developers). Decomprimi lo SDK in `external\asiosdk` e rilancia `configure.bat` + `build.bat`. Senza SDK il programma usa **WASAPI Exclusive** (funziona subito, latenza leggermente superiore).
- Latenza consigliata: 48 kHz, buffer 128 in prova (~3 ms), 256 in live (sicuro). Usa il Direct Monitor dell'UMC per il monitoraggio zero-latency.

## Primo avvio

1. **Impostazioni → Audio**: seleziona **Driver ASIO** e il dispositivo (es. interfaccia con ≥ 4 uscite).
2. **Sample rate**: 44100 / 48000 / 96000 Hz. **Buffer**: 64–1024 campioni (256 = buon compromesso; 512 = massima sicurezza live).
3. **Applica** e controlla l'header: dispositivo, frequenza, buffer, contatore **xrun**. Se gli xrun salgono, aumenta il buffer.
4. Verifica il **routing**: default `1=Backing L, 2=Backing R, 3=Click, 4=Cue`. Il Click/Cue non devono mai arrivare alle casse del pubblico.

## Modalità PROVE (sala prove)

- Doppio click sul brano per caricarlo (o SPAZIO sul primo brano).
- **SPAZIO** Play/Pausa, clic sul righello = seek.
- **Loop A/B**: posiziona il cursore, premi *Imposta A* e *Imposta B*, attiva **LOOP**.
- Mixer: fader 0–2×, M/S, combo di routing per uscita, collega L/R opzionale.
- **STOP** ferma e azzera; **PANIC** taglia tutto istantaneamente.

## Modalità LIVE

- Il brano selezionato si **arma** automaticamente (banner "ARMATO — premi SPAZIO").
- **SPAZIO** avvia il brano; a fine brano lo stop è automatico e il **successivo viene caricato e armato** da solo.
- Con *Impostazioni → Auto-start successivo* il brano seguente parte **da solo, gapless** (senza interruzione tra i brani).
- **STOP** protetto: richiede doppia pressione entro 2 secondi.
- **PERFORMANCE** → fullscreen con pulsanti grandi: SPAZIO play/pausa, frecce navigazione, ESC per uscire.

## MIDI

**Impostazioni → MIDI**: attiva un ingresso, premi **Impara** sul comando e muovi il controllo (CC, Nota o Program Change). Le mappature vengono salvate nel progetto.

## Brani

- **+** → titolo, artista, BPM, note e un file per traccia (WAV/AIFF/FLAC/MP3).
- La traccia **Click** può essere **generata dal BPM** (100 misure, accento sul primo battito).
- Durate e frequenze diverse tra i file vengono convertite automaticamente: le tracce restano sempre sincronizzate dal primo campione.

## Affidabilità live

- Riproduzione interamente in memoria: nessun accesso a disco nel callback audio.
- Temporizzazione solo dal clock del dispositivo (mai da timer grafici).
- Scollegamento della scheda → stop sicuro + avviso, **nessun** cambio dispositivo automatico, **nessuna** ripartenza automatica.
- Salvataggio automatico della scaletta a ogni modifica.
