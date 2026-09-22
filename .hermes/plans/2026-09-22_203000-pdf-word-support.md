# Specifikation: PDF- og Word-understøttelse i wordprocessor

## Formål

Wordprocessoren skal kunne importere, redigere og eksportere dokumenter i PDF- og Microsoft Word-format med størst mulig bevarelse af indhold, struktur og formatering.

## PDF

### Import af PDF

Systemet skal kunne:

- Importere PDF-filer som nye eller eksisterende dokumenter.
- Udtrække redigerbar tekst, afsnit og grundlæggende struktur fra tekstbaserede PDF’er.
- Bevare sidestørrelse, sideorientering og sidetal, når oplysningerne findes i kildefilen.
- Importere billeder, tabeller og hyperlinks, hvor det teknisk er muligt.
- Genkende og advare brugeren, hvis en PDF er scannet eller billedbaseret og derfor kræver OCR.
- Tilbyde OCR for skannede PDF’er, herunder understøttelse af dansk tekst.
- Vise en tydelig advarsel, hvis formatering, skrifttyper eller avancerede elementer ikke kan genskabes korrekt.
- Respektere adgangskodebeskyttede PDF’er ved at anmode om adgangskoden, før filen åbnes.

### Eksport til PDF

Systemet skal kunne:

- Eksportere ethvert dokument til PDF.
- Bevare tekst, afsnit, tabeller, billeder, sidetal, sidehoved/-fod og links.
- Indlejre eller på anden måde sikre korrekt gengivelse af anvendte skrifttyper.
- Eksportere med samme sidestørrelse, marginer og orientering som dokumentet.
- Understøtte eksport af hele dokumentet, valgte sider eller et markeret område.
- Tilbyde valgfri adgangskodebeskyttelse og begrænsning af redigering/udskrivning.
- Understøtte PDF-versioner, der er egnede til almindelig deling og langtidsarkivering, eksempelvis PDF/A.
- Give brugeren mulighed for at angive filnavn og placering før eksport.

## Microsoft Word

### Import af Word

Systemet skal kunne:

- Importere `.docx` som standardformat og, hvor muligt, ældre `.doc`-filer.
- Bevare dokumentets tekst, afsnitsstruktur, overskrifter, lister, tabeller, billeder og hyperlinks.
- Bevare eller tydeligt angive manglende understøttelse af:
  - skrifttyper og tekstformatering
  - sidehoved og sidefod
  - fodnoter og slutnoter
  - kommentarer
  - ændringssporing
  - indholdsfortegnelser
  - felter, formularer og referencer
- Importere dokumentets sideopsætning, herunder marginer, orientering, sektionsskift og sidestørrelse.
- Vise en kompatibilitetsadvarsel, når avancerede Word-funktioner ikke kan redigeres eller bevares.

### Eksport til Word

Systemet skal kunne:

- Eksportere dokumenter til `.docx`.
- Bevare almindelig tekstformatering, stilarter, overskrifter, lister, tabeller, billeder, hyperlinks samt sideopsætning.
- Eksportere sidehoved/-fod, sidetal, fodnoter og slutnoter, hvis disse funktioner findes i wordprocessoren.
- Tilbyde eksport til ældre `.doc`, hvis dette understøttes, med tydelig advarsel om eventuelle kompatibilitetstab.
- Validere den eksporterede fil, så den kan åbnes i nyere versioner af Microsoft Word uden fejl.
- Advare brugeren, hvis elementer ændres, fjernes eller erstattes under eksporten.

## Generelle krav

- Import og eksport skal kunne gennemføres fra dokumentets filmenu.
- Systemet skal vise meningsfulde fejlbeskeder ved ugyldige, beskadigede eller ikke-understøttede filer.
- Originalfilen må ikke overskrives uden brugerens udtrykkelige godkendelse.
- Eksport skal bevare dokumentets sprog, specialtegn og Unicode-tegn, herunder danske tegn som `æ`, `ø` og `å`.
- Systemet skal kunne håndtere dokumenter med mindst 200 sider uden væsentligt tab af funktionalitet.
- Brugeren skal kunne se en forhåndsvisning eller modtage en kompatibilitetsrapport før eksport, når der er risiko for tab af indhold eller formatering.