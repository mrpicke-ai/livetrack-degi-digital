#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../AppData.h"

namespace ldd
{
inline juce::String getHelpText()
{
    return R"(GUIDA RAPIDA - LIVETRACK DEGI DIGITAL

CONCETTO
Il programma e' organizzato in brani. Ogni brano usa le 6 tracce indipendenti
(tastiere, basso, click, cue...) e ogni traccia esce sui canali fisici che
scegli nel patchbay della console: click sul jack = canale singolo (es. 3),
Shift+click = coppia stereo (1-2), click sul jack attivo in coppia = togli
quella uscita ("-"). Il nome di ogni traccia e' quello del file importato
(rinominabile con doppio click sulla targhetta).

PRIMO AVVIO
1. MENU -> Impostazioni -> scheda Audio -> Driver ASIO e dispositivo.
2. Sample rate (44100/48000/96000) e Buffer (256 = buon compromesso;
   128 piu' reattivo ma piu' rischio di xrun; 512 il piu' sicuro).
3. Applica. Controlla l'header: dispositivo, frequenza, buffer, xrun.

LA CONSOLE
- Riga in alto: slot dei brani della scaletta. Clicca uno slot per caricare
  e armare il brano. "+ BRANO" aggiunge uno slot vuoto.
- Titolo grande: brano attualmente caricato. PLAY/STOP/PANIC a destra.
- Timeline unica: mixdown del brano caricato. Clicca o trascina per spostare
  la testina. I marker sono i cue del brano.
- Fila centrale: 6 canali + MASTER. Ogni canale ha nome, nome file, fader,
  meter, M/S e i jack del patchbay. Sotto fader e LED: mini-waveform della
  traccia caricata.

SALA PROVE (modalita' PROVE)
- SPAZIO o PLAY: avvia / mette in pausa. STOP riporta all'inizio.
- Loop A/B: premi Imposta A e Imposta B con la posizione voluta, poi LOOP.
- PANIC interrompe tutto istantaneamente.

LIVE (modalita' LIVE)
- Seleziona un brano: caricato e ARMATO automaticamente.
- SPAZIO avvia il brano armato. Alla fine si ferma da solo e il successivo
  viene caricato e armato automaticamente (gapless).
- Freccia destra/sinistra: brano successivo/precedente.
- PERFORMANCE passa a fullscreen con pulsanti grandi: SPAZIO play/pausa,
  ESC per uscire.

BRANI E SCALETTA
- MENU -> Brano: Aggiungi / Modifica / Duplica / Elimina. In modifica
  assegni un file audio a ogni traccia (click su "genera" per il click
  di metronomo dal BPM).
- La scaletta viene salvata automaticamente con il progetto (.liveset).
- All'apertura TUTTI i brani vengono decodificati in RAM con una finestra di
  avanzamento: attendi il completamento. Poi selezione e cambio brano sono
  immediati, senza accessi al disco. La stessa attesa vale dopo importazioni,
  modifiche ai file o cambio del sample rate.
- MENU -> Scaletta -> Esporta scaletta: salva solo la scaletta in un file
  (.ldscaletta) da riaprire su un altro PC con Importa scaletta.
- MENU -> Progetto: Apri / Salva / Nuovo.

MIDI
- Impostazioni -> scheda MIDI: attiva un ingresso, premi "Impara" e muovi
  il controllo sul controller. Supporta CC, Note e Program Change.
- Consigliato un pedale MIDI per Play/Pause e uno per PANIC.

PEDALIERA MIDI (HOTONE AMPERO II STAGE)
- La striscia "PEDALIERA MIDI" in basso controlla l'Ampero via MIDI:
  PRESET = numero patch 1-300 (A60-1 = 296, predefinito per i nuovi brani).
  Il programma invia Bank MSB + Program Change, quindi raggiunge TUTTI i 300
  patch, non solo il banco corrente. Gli 8 tasti accendono/spengono i pedali
  via Control Change. A inizio brano i pedali vengono SEMPRE spenti; solo gli
  eventi a tempo possono riattivarli. Dopo STOP gli eventi sono di nuovo pronti.
- CLICK su un pedale = invio immediato (controllo live manuale) + salva.
- DOPPIO CLICK su un pedale = rinomina e cambia il numero CC.
- I CC dell'Ampero II Stage sono FISSI di fabbrica (MIDI Control Information
  List Hotone): NON si assegnano sull'Ampero, vanno solo scelti qui.
  Mappa utile:
    CC 79-85 = footswitch FS1-FS7 (replica dei pedali fisici in STOMP mode:
               e' il default dei tasti 1-7, quindi il tasto 1 fa quello che
               hai assegnato al footswitch FS1 sullo Stage)
    CC 48-53 = slot A1-A6 on/off (singolo blocco della catena)
    CC 54-59 = slot B1-B6 on/off
    CC 60    = tuner on/off (default del tasto 8)
    CC 25    = scene 1-5 (valore = numero scena)
    CC 76    = tap tempo | CC 26/27 = patch -/+ | CC 28 = patch/stomp mode
  Per i pedali singoli la via piu' semplice: configura sullo Stage i
  footswitch FS1-FS7 sui blocchi che vuoi (STOMP mode) e lascia i default.
- Collegamento tipico: MIDI OUT della scheda audio (es. UMC404HD) ->
  MIDI IN dell'Ampero (jack 3.5 mm TRS, con l'adattatore DIN in dotazione
  Hotone: gli adattatori generici possono non funzionare). Alternativa:
  cavo USB-C diretto PC -> Ampero (MIDI su USB, niente driver).
- Sullo Stage: GLOBAL -> MIDI Settings -> MIDI IN SOURCE = Mixed (default)
  e INPUT CH (TRS) = Omni (default) oppure lo stesso canale del programma.
- Nel programma: MENU -> Impostazioni -> scheda MIDI -> Uscita MIDI (la
  voce MIDI della scheda, NON il driver ASIO) + Canale (default 1).
- Test rapido: imposta un PRESET, premi INVIA ORA e verifica che l'Ampero
  cambi patch; poi gioca con i pedali. Funziona sia in PATCH che in STOMP.
- I CC vengono inviati ~150 ms dopo il Program Change: serve perche' l'Ampero
  impiega qualche istante a caricare il patch e ignorerebbe i CC precedenti.

PEDALI A TEMPO (EVENTI)
- Per accendere o spegnere un pedale in un punto preciso del brano (es. il
  ritornello) usa gli EVENTI: posiziona la testina, premi REC EVENTI e clicca
  il pedale. Puoi farlo da fermo oppure mentre il brano suona. Ogni click viene
  inviato subito all'Ampero, memorizzato con il tempo esatto e mostrato come
  marcatore sulla timeline. Il pedale resta acceso/spento come appena impostato.
- In riproduzione normale gli eventi si eseguono da soli quando la testina li
  raggiunge, in live come in sala prove.
- Spostare la testina (click sulla timeline) riapplica gli stati corretti per
  la nuova posizione: non partono a raffica ne' restano indietro.
- EVENTI... apre la lista degli eventi del brano: rivedili, elimina quelli
  sbagliati e aggiungine a mano (tempo + pedale + stato) senza registrare.
  Gli eventi si salvano con la scaletta.
- ATTENZIONE: con REC attivo i click NON cambiano lo stato base dei pedali:
  servono solo a registrare eventi. Con REC spento, il click cambia lo stato
  base (quello inviato a inizio brano).

AFFIDABILITA'
- Click e cue non finiscono mai sulle uscite della base: routing separato.
- Se la scheda audio si scollega, la riproduzione si interrompe in sicurezza
  e compare un avviso: NON viene cambiato dispositivo automaticamente.
- Se i contatori xrun salgono, aumenta il buffer in Impostazioni.

TASTI
SPAZIO play/pausa | Frecce brano prec/succ | ESC esci da fullscreen
)";
}

class HelpComponent : public juce::Component
{
public:
    HelpComponent()
    {
        text.setMultiLine (true);
        text.setReadOnly (true);
        text.setScrollbarsShown (true);
        text.setCaretVisible (false);
        text.setColour (juce::TextEditor::backgroundColourId, theme::screen);
        text.setColour (juce::TextEditor::textColourId, theme::screenText);
        text.setColour (juce::TextEditor::outlineColourId, theme::surfaceDark);
        text.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));
        text.setText (getHelpText());
        addAndMakeVisible (text);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
    }

    void resized() override
    {
        text.setBounds (getLocalBounds().reduced (8));
    }

private:
    juce::TextEditor text;

    JUCE_DECLARE_NON_COPYABLE (HelpComponent)
};

class HelpWindow : public juce::DocumentWindow
{
public:
    HelpWindow() : DocumentWindow ("Guida", theme::background, DocumentWindow::closeButton)
    {
        setContentOwned (new HelpComponent(), true);
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        centreWithSize (760, 620);
    }

    void closeButtonPressed() override { setVisible (false); }

    JUCE_DECLARE_NON_COPYABLE (HelpWindow)
};
}
