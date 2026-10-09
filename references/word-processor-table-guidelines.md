Ja. Hvis det skal bruges som **implementeringsspecifikation for en WYSIWYG-wordprocessor**, vil jeg anbefale at beskrive tabelunderstøttelsen som en samlet funktionel specifikation. Det vigtigste er, at tabellen behandles som en strukturel blok med celler, og at cursor/selection/navigation følger samme principper som resten af editoren.

# Tabelunderstøttelse i en WYSIWYG-wordprocessor

## 1. Grundlæggende dokumentmodel

En tabel bør være en selvstændig strukturel blok:

```text
Document
 ├── Paragraph
 ├── Paragraph
 ├── Table
 │    ├── Row
 │    │    ├── Cell
 │    │    │    └── Paragraph/Text
 │    │    └── Cell
 │    │         └── Paragraph/Text
 │    └── Row
 │         ├── Cell
 │         └── Cell
 └── Paragraph
```

Tjek:

* [ ] En tabel er en selvstændig block.
* [ ] En tabel består af rækker.
* [ ] En række består af celler.
* [ ] En celle kan indeholde tekst.
* [ ] En celle kan indeholde flere paragraphs.
* [ ] En celle kan indeholde inline formatting.
* [ ] En celle kan være tom.
* [ ] En celle har en entydig position.
* [ ] Tabellen har antal rækker og kolonner.
* [ ] Celler har stabile interne identifikatorer.
* [ ] Det er muligt at identificere:

  * tabel
  * række
  * kolonne
  * celle
  * caret-position i cellen.

---

# 2. Tabel som strukturel enhed

En tabel skal ikke blot betragtes som en række HTML-elementer.

Editoren skal kende relationerne:

```text
table
   ↓
row
   ↓
cell
   ↓
paragraph
   ↓
text
   ↓
caret
```

Det skal eksempelvis altid være muligt at afgøre:

```text
currentTable()
currentRow()
currentColumn()
currentCell()
currentParagraph()
currentCaretPosition()
```

Det er vigtigt for keyboard-navigation, mouse clicks og tabelkommandoer.

---

# 3. Indsættelse af tabel

Der bør være en **Indsæt tabel**-funktion.

Eksempel:

**Indsæt → Tabel**

Menuen kan tilbyde:

```text
Indsæt tabel

┌────┬────┬────┬────┬────┐
│ 1×1│ 2×1│ 3×1│ 4×1│ 5×1│
├────┼────┼────┼────┼────┤
│ 1×2│ 2×2│ 3×2│ 4×2│ 5×2│
├────┼────┼────┼────┼────┤
│ 1×3│ 2×3│ 3×3│ 4×3│ 5×3│
└────┴────┴────┴────┴────┘
```

Tjek:

* [ ] Brugeren kan vælge antal rækker/kolonner.
* [ ] Tabellen indsættes ved den aktuelle caret-position.
* [ ] Cursor placeres i første celle.
* [ ] Før/efterliggende paragraphs bevares.
* [ ] Tabellen kan indsættes mellem to paragraphs.
* [ ] En tabel kan indsættes i et tomt dokument.
* [ ] En tabel kan indsættes efter en anden tabel.
* [ ] Undo fjerner hele indsættelsen som én operation.

---

# 4. Menu for tabel

Når cursoren befinder sig i en tabel, bør editoren vise en tabelrelateret menu/toolbar.

Eksempel:

```text
Tabel

[Indsæt række]
[Indsæt kolonne]

[Slet række]
[Slet kolonne]
[Slet tabel]

[Flet celler]
[Opdel celle]

────────────────

[Justér venstre]
[Centrér]
[Justér højre]

────────────────

[Kant]
[Baggrund]
[Cellens størrelse]

────────────────

[Fortryd]
```

En mere komplet menu kunne være:

### Rækker

* [ ] Indsæt række over
* [ ] Indsæt række under
* [ ] Slet række
* [ ] Markér række

### Kolonner

* [ ] Indsæt kolonne til venstre
* [ ] Indsæt kolonne til højre
* [ ] Slet kolonne
* [ ] Markér kolonne

### Celler

* [ ] Flet celler
* [ ] Opdel celle
* [ ] Slet celle
* [ ] Markér celle

### Tabel

* [ ] Slet tabel
* [ ] Egenskaber
* [ ] Automatisk tilpasning
* [ ] Fast kolonnebredde
* [ ] Fordel kolonner
* [ ] Fordel rækker

---

# 5. Mouse click i tabel

Dette er et af de vigtigste områder.

Et museklik skal ikke bare identificere `<td>`.

Flowet bør være:

```text
Mouse coordinates
       ↓
Table
       ↓
Row
       ↓
Cell
       ↓
Paragraph
       ↓
Text node
       ↓
Caret position
```

Eksempel:

```text
┌─────────────┬─────────────┐
│ Første celle│ Anden celle │
│             │             │
└─────────────┴─────────────┘
```

Klik midt i:

```text
Anden celle
```

skal give:

```text
table = T1
row = R1
column = 2
cell = C2
caret = position X
```

---

# 6. Klik på tekst i en celle

Test:

```text
┌──────────────────────┐
│ Dette er noget tekst │
└──────────────────────┘
```

* [ ] Klik før første tegn.
* [ ] Klik mellem tegn.
* [ ] Klik efter sidste tegn.
* [ ] Klik i whitespace.
* [ ] Klik langt til højre for teksten.
* [ ] Klik under teksten.
* [ ] Cursor bliver i samme celle.
* [ ] Cursor flytter ikke utilsigtet til nabocellen.

**Kritisk regel:**

> Klik i whitespace i en celle skal normalt placere cursoren i slutningen af tekstens paragraph – ikke i nabocellen.

---

# 7. Klik i tom celle

Eksempel:

```text
┌─────────────┬─────────────┐
│ Tekst       │             │
└─────────────┴─────────────┘
```

Klik i den tomme celle:

* [ ] Cursor placeres i cellen.
* [ ] Cursor er synlig.
* [ ] Cellen bliver aktiv.
* [ ] Det er muligt at skrive direkte.
* [ ] Første tekst oprettes i cellen.
* [ ] Klik på forskellige steder i den tomme celle giver samme logiske caret-position.

**Bindende regel for caret-normalisering:** En celle ejer hele sit
byteinterval mellem rørene, også når cellen er tom og ikke renderer tekst.
Normalisering til nærmest renderet tekst må aldrig flytte en caret-position
over en cellegrænse. Klik i en tom celle parkerer altid careten ved cellens
eget indholdsstart, også når den ønskede position ikke matcher noget renderet
tegn. Dette gælder både klik, dobbeltklik, drag-drop og tastaturnavigation.

**Testspor:** `HitTest.EmptyTableCellClickKeepsCaretInClickedCell` og
`HitTest.NonEmptyCellClickDoesNotCrossCellBoundary` i
`tests/hittest_test.cpp` fastlægger reglen som regressionstests.

---

# 8. Klik på cellekant

Cellekanten skal have en anden semantik end cellens indhold.

```text
┌───────────────┬───────────────┐
│               │               │
│    CELL 1     │    CELL 2     │
│               │               │
└───────────────┴───────────────┘
```

Tjek:

* [ ] Klik inde i cellen → caret.
* [ ] Klik på cellekanten → afhænger af editorens design.
* [ ] Klik og drag over kanten må kunne starte selection af celler.
* [ ] Der skal være en tydelig visuel forskel mellem caret og celle-selection.

---

# 9. Tab-tasten

`Tab` er en central tabelnavigation.

Hvis cursor er i:

```text
┌───────┬───────┬───────┐
│ A     │ B     │ C     │
└───────┴───────┴───────┘
```

Tryk Tab:

```text
A → B → C
```

Altså:

```text
cell[0,0]
   ↓ Tab
cell[0,1]
   ↓ Tab
cell[0,2]
```

Tjek:

* [ ] Tab flytter til næste celle.
* [ ] Cursor placeres i den næste celles indhold.
* [ ] Hvis cellen er tom, placeres cursor i cellen.
* [ ] Tab må ikke indsætte et tab-tegn i cellen.
* [ ] Selection erstattes korrekt, hvis der er markeret tekst.

---

# 10. Tab fra sidste celle

Dette skal defineres eksplicit.

Eksempel:

```text
┌───────┬───────┐
│ A     │ B     │
├───────┼───────┤
│ C     │ D|    │
└───────┴───────┘
```

Tryk Tab.

Standardadfærden bør være:

```text
┌───────┬───────┐
│ A     │ B     │
├───────┼───────┤
│ C     │ D     │
├───────┼───────┤
│       │       │
└───────┴───────┘
```

Altså:

> Tab i sidste celle opretter en ny række og placerer cursor i første celle.

Tjek:

* [ ] Ny række oprettes.
* [ ] Samme antal kolonner.
* [ ] Formatting kopieres efter defineret regel.
* [ ] Cursor placeres i første celle.
* [ ] Undo fjerner hele rækkeindsættelsen.

---

# 11. Shift+Tab

`Shift+Tab` skal navigere baglæns.

```text
D ← C ← B ← A
```

Tjek:

* [ ] Shift+Tab flytter til forrige celle.
* [ ] Fra første celle går den til sidste celle i forrige række.
* [ ] Fra første celle i første række håndteres grænsen korrekt.
* [ ] Der indsættes ikke et tab-tegn.

---

# 12. Arrow keys

Piletaster skal have tabelspecifik logik.

### Left / Right

Inde i en celle:

```text
tekst|tekst
```

→ almindelig tekstnavigation.

Men ved kanten:

```text
| første celle
```

Venstre skal normalt **ikke automatisk flytte til nabocellen**.

Tilsvarende:

```text
sidste tegn|
```

Højre skal normalt blive i cellen.

Det er vigtigt at undgå, at almindelig tekstnavigation pludselig springer mellem celler.

---

# 13. Up / Down

`↑` og `↓` er mere komplekse.

Eksempel:

```text
┌──────────┬──────────┐
│ abc      │ 123      │
│ def|     │ 456      │
└──────────┴──────────┘
```

Pil op/ned skal forsøge at bevare den **visuelle X-position**.

Tjek:

* [ ] Pil op bevæger caret visuelt op.
* [ ] Pil ned bevæger caret visuelt ned.
* [ ] Hvis der ikke findes en tilsvarende tekstlinje, håndteres kanten konsistent.
* [ ] Ved overgang mellem celler er adfærden defineret.
* [ ] Multi-line celler fungerer.
* [ ] Celler med forskellig højde fungerer.

---

# 14. Navigation mellem celler

Det bør defineres som en matrix:

```text
              Up
              ↑
              │
Left ← Current Cell → Right
              │
              ↓
             Down
```

Men:

**Tab er strukturel navigation**, mens piletaster primært er **tekst/visuel navigation**.

Det bør ikke blandes sammen.

---

# 15. Enter i en tabelcelle

`Enter` skal normalt **ikke oprette en ny tabelrække**.

Eksempel:

```text
┌───────────────┐
│ Dette er|     │
└───────────────┘
```

Enter:

```text
┌───────────────┐
│ Dette er      │
│ |             │
└───────────────┘
```

Altså en ny paragraph/linje **inde i cellen**, afhængigt af editorens dokumentmodel.

Tjek:

* [ ] Enter opretter ny paragraph i cellen.
* [ ] Cellen bevares.
* [ ] Tabellen bevares.
* [ ] Naboceller påvirkes ikke.
* [ ] Formatting håndteres korrekt.

---

# 16. Enter ved slutningen af cellen

Dette skal defineres eksplicit.

```text
┌───────────────┐
│ Tekst|        │
└───────────────┘
```

Enter bør normalt give:

```text
┌───────────────┐
│ Tekst         │
│ |             │
└───────────────┘
```

Ikke en ny række.

**Tab** er tabelnavigation.

**Enter** er tekst/paragraph-navigation.

Det er en vigtig adskillelse.

---

# 17. Backspace ved starten af celle

Dette er endnu en vigtig boundary case.

```text
┌──────────────┬──────────────┐
│ Tekst        │ |Anden tekst │
└──────────────┴──────────────┘
```

Backspace ved starten af anden celle bør **ikke automatisk merge cellerne**.

Standardadfærden bør være:

* [ ] Backspace arbejder inden for cellen.
* [ ] Når paragraphens start nås, håndteres den interne paragraphstruktur.
* [ ] Cellegrænsen overskrides ikke utilsigtet.
* [ ] Sammenlægning af celler kræver eksplicit tabelkommando.

---

# 18. Selection af tekst

Tekstselection skal fungere normalt:

```text
┌─────────────────────┐
│ Dette er [tekst]    │
└─────────────────────┘
```

Tjek:

* [ ] Drag markerer tekst.
* [ ] Shift+arrow fungerer.
* [ ] Ctrl/Cmd+A har korrekt tabeladfærd.
* [ ] Delete sletter tekst uden at slette cellen.
* [ ] Formatting kan anvendes på selection.

---

# 19. Selection af celler

Der bør være en separat celle-selection.

Eksempel:

```text
┌────────┬────────┬────────┐
│        │████████│████████│
├────────┼────────┼────────┤
│        │████████│████████│
└────────┴────────┴────────┘
```

Tjek:

* [ ] En hel celle kan markeres.
* [ ] Flere celler kan markeres.
* [ ] Række kan markeres.
* [ ] Kolonne kan markeres.
* [ ] Hele tabel kan markeres.
* [ ] Selection har tydelig visuel feedback.

---

# 20. Mouse drag over celler

Det skal være muligt at markere flere celler med musen.

```text
Mouse down
     ↓
Cell A
     ↓
drag
     ↓
Cell B
     ↓
Cell C
     ↓
Mouse up
```

Tjek:

* [ ] Selection starter i en celle.
* [ ] Drag til nabocelle inkluderer denne.
* [ ] Drag over flere rækker fungerer.
* [ ] Selection kan gå både højre/venstre.
* [ ] Selection kan gå op/ned.
* [ ] Selection er rektangulær.
* [ ] Selection af celler forveksles ikke med tekstselection.

---

# 21. Flet celler

Hvis:

```text
┌──────┬──────┐
│ A    │ B    │
└──────┴──────┘
```

er markeret:

```text
┌──────────────┐
│ A       B    │
└──────────────┘
```

Tjek:

* [ ] Minimum to celler kan flettes.
* [ ] Kun sammenhængende celler kan flettes.
* [ ] Tekst fra begge celler bevares.
* [ ] Paragraphstruktur bevares efter defineret regel.
* [ ] Række/kolonnestruktur opdateres.
* [ ] Caret placeres i den nye celle.
* [ ] Undo fungerer.

---

# 22. Opdel celle

En celle:

```text
┌──────────────┐
│ Tekst        │
└──────────────┘
```

kan opdeles:

```text
┌───────┬───────┐
│ Tekst │       │
└───────┴───────┘
```

Tjek:

* [ ] Brugeren kan vælge antal celler.
* [ ] Indhold placeres efter defineret regel.
* [ ] Formatting bevares.
* [ ] Tabelgeometrien forbliver gyldig.

---

# 23. Indsæt række

Ved aktiv celle:

```text
┌─────┬─────┐
│ A   │ B   │ ← aktiv
├─────┼─────┤
│ C   │ D   │
└─────┴─────┘
```

**Indsæt række over** lægger en ny tom række umiddelbart før aktiv række,
**indsæt række under** umiddelbart efter. Reglerne gælder uanset om cellen
er tom eller har indhold: kommandoen kigger kun på rækkeplaceringen, aldrig
på cellens tekst.

* Den nye række har samme kolonnetal som resten af tabellen, og alle
  celler er tomme. Rækken skrives som `|        |        |` (én
  mellemrums-cluster pr. celle), så et genparse giver det samme antal
  kolonner.
* Cellens indhold påvirkes ikke: teksten i aktiv række og i alle rækker
  over og under flytter sig med rækken, intet slettes og intet flettes.
* Overskriftsrækken (række 0) og skillelinjen (række 1) definerer
  tabellens form. En ny række kan ikke indsættes mellem dem eller over
  overskriften; kommandoen afviser disse positioner.
* Caret placeres i den nye rækkes første celle (indsæt over) eller i
  første celle i rækken efter (indsæt under). Begge er nye tomme celler,
  så der er ingen tekst at gemme eller vælge.

Tjek:

* [ ] Korrekt antal celler.
* [ ] Korrekt placering.
* [ ] Cellebredder bevares.
* [ ] Formatting efter defineret regel.
* [ ] Cursor/selection bevares eller flyttes deterministisk.

---

# 24. Indsæt kolonne

Samme princip:

```text
┌─────┬─────┬─────┐
│ A   │     │ B   │
├─────┼─────┼─────┤
│ C   │     │ D   │
└─────┴─────┴─────┘
```

Placeringen følger caretens kolonne: **til højre** indsætter efter
caret-cellens afsluttende `|`, **til venstre** før dens åbne `|`. Det
gælder både i tomme og udfyldte celler; kun kolonnepositionen tæller.

* Alle rækker får én ny celle, så kolonnetallet bliver ens på tværs af
  rækker (§60). De nye celler er tomme og skrives med samme
  mellemrumsbredde som en indsæt-række-celle.
* Caretens egen celle berøres ikke: dens tekst, padding og position
  relativt til kolonnerne omkring den bevares. Teksten i kolonner til
  højre flyttes men ændres ikke.
* I skillelinjen skrives den nye celle som `--------|`, så tabellen
  stadig genparses som tabel.
* Rækker der er håndredigeret kortere (ragged) og mangler det antal
  rørtegn der skal til, kopieres uændret: en kort række får ikke
  tilfældigt en ekstra celle.

Tjek:

* [ ] Kolonnen indsættes korrekt.
* [ ] Rækkeantallet bevares.
* [ ] Bredde beregnes korrekt.
* [ ] Tabellen overstiger ikke utilsigtet dokumentets bredde.
* [ ] Caret placeres i den nye kolonnes celle på samme række.

---

# 25. Slet række

Den række hvor caret står (den fysiske linje), slettes som helhed: alle
celler i rækken forsvinder, både tomme og udfyldte. Sletningen er
repræsentativ for rækken, ikke for cellen.

* Rækker over og under berøres ikke: deres tekst, kolonneantal og
  breddeforholdene bevares. Kolonnetallet pr. række må ikke ændre sig, og
  md4c genparser resten til én tabel.
* Skillelinjen og overskriften kan ikke slettes særskilt. Er caret på
  række 0 eller 1, afvises kommandoen. Tabellens form defineres netop af
  disse to linjer.
* Den sidste række i tabellen kan godt slettes. Hvis der efter
  sletningen kun står overskrift og skillelinje tilbage, forbliver det
  en gyldig tabel: et antal kolonner og rækken nul. Alternativet var at
  slette hele tabellen, men det ville fjerne overskriften som brugeren
  selv har skrevet.
* Efter sletningen placeres caret ved startpositionen af den slettede
  række eller (hvis den var sidst) i sidste tilbageværende række. Caret
  står aldrig i restroområdet mellem to linjer.

Tjek:

* [ ] Aktiv række kan slettes.
* [ ] Rækker før og efter sletningen er uændrede.
* [ ] Sidste række håndteres korrekt.
* [ ] Efter sletning af sidste række er der en tom tabel eller
  tabellen er helt væk, og adfærden er eksplicit defineret.
* [ ] Cursor placeres korrekt bagefter.

---

# 26. Slet kolonne

Samme princip: den kolonne hvor caret står, slettes i alle rækker.

* I hver række fjernes cellens indhold inklusive dens afsluttende `|`.
  Er cellen i en række tom, fjernes dens mellemrum på samme måde.
* Kolonner til venstre og højre bevares uændrede: deres tekst og
  justering flyttes blot med. Rækkeantallet er det samme.
* Skillelinjens tilsvarende segment `--------|` fjernes også, ellers
  ville kolonnetallet i linjen overstige kroppens.
* Den sidste kolonne kan ikke slettes (tabellen må ikke ende i 0
  kolonner); kommandoen afvises når tabellen kun har én kolonne.
* Caret placeres i cellen umiddelbart til højre for den slettede
  kolonne, på samme række. Er den slettede den sidste kolonne, placeres
  caret i den nye sidste celle på rækken.

Tjek:

* [ ] Aktiv kolonne slettes.
* [ ] Kolonner før og efter er uændrede.
* [ ] Sidste kolonne håndteres korrekt.
* [ ] Tabellen må ikke ende i en ugyldig 0-kolonne tilstand.
* [ ] Caret placeres deterministisk efter sletningen.

---

# 27. Slet tabel

Når cursor befinder sig i tabel:

```text
Tabel → Slet tabel
```

Tjek:

* [ ] Hele tabellen slettes.
* [ ] Efterfølgende paragraph bevares.
* [ ] Foregående paragraph bevares.
* [ ] Hvis tabellen er eneste indhold, findes stadig en gyldig caret-position.
* [ ] Cursor placeres efter tabellen.
* [ ] Undo genskaber hele tabellen.

---

# 28. Formatering af celler

En tabel bør understøtte:

* [ ] Fed
* [ ] Kursiv
* [ ] Understregning
* [ ] Skrifttype
* [ ] Skriftstørrelse
* [ ] Tekstfarve
* [ ] Baggrundsfarve
* [ ] Tekstjustering
* [ ] Lodret justering
* [ ] Indrykning.

For tekst:

```text
Fed | Kursiv | Understregning
```

skal fungere på samme måde som uden for tabellen.

---

# 29. Horisontal justering

Celleindhold:

* [ ] Venstre
* [ ] Centreret
* [ ] Højre
* [ ] Justeret

skal kunne anvendes.

Vigtigt:

> Justering af tekst skal ikke ændre tabelcellens placering eller bredde.

---

# 30. Vertikal justering

Celle:

```text
┌─────────────┐
│             │
│    Text     │
│             │
└─────────────┘
```

Muligheder:

* [ ] Top
* [ ] Midt
* [ ] Bund

---

# 31. Kolonnebredde

Brugeren bør kunne ændre kolonnebredde med musen.

```text
┌───────────┬─────────────┐
│           │             │
│           │             │
└───────────┴─────────────┘
            ↑
         dragger
```

Tjek:

* [ ] Mouse cursor ændres ved kolonnegrænse.
* [ ] Drag ændrer kolonnebredde.
* [ ] Nabokolonne håndteres efter valgt resize-model.
* [ ] Minimumsbredde håndhæves.
* [ ] Tabellen må ikke blive negativ/ugyldig.
* [ ] Undo fungerer.

---

# 32. Rækkehøjde

Hvis relevant:

* [ ] Række kan ændres i højde.
* [ ] Minimumshøjde respekteres.
* [ ] Tekst må ikke skjules.
* [ ] Auto-height fungerer.
* [ ] Resize fungerer med mus.

---

# 33. Tabelbredde

Tabel kan være:

* [ ] Automatisk bredde.
* [ ] 100 % af tilgængelig bredde.
* [ ] Fast bredde.

Tjek:

* [ ] Tabellen respekterer page/document width.
* [ ] Tabellen fungerer ved zoom.
* [ ] Tabellen fungerer ved ændring af vinduesbredde.

---

# 34. Kantlinjer

Brugeren bør kunne kontrollere:

* [ ] Alle kanter.
* [ ] Udvendige kanter.
* [ ] Indvendige kanter.
* [ ] Top.
* [ ] Bund.
* [ ] Venstre.
* [ ] Højre.
* [ ] Ingen kant.

Samt eventuelt:

* [ ] Linjetype.
* [ ] Linjetykkelse.

---

# 35. Cellens baggrund

For markerede celler:

* [ ] Baggrundsfarve kan ændres.
* [ ] Farven anvendes på alle valgte celler.
* [ ] Selection bevares.
* [ ] Undo fungerer.

---

# 36. Header-rækker

Hvis editoren understøtter semantiske tabeller:

* [ ] En række kan markeres som header.
* [ ] Header har korrekt semantik.
* [ ] Header kan styles separat.
* [ ] Header kan gentages ved sideskift i print/PDF.
* [ ] Headerstruktur bevares ved redigering.

---

# 37. Tabelnavigation med keyboard – samlet specifikation

| Tast          | Adfærd               |
| ------------- | -------------------- |
| `Tab`         | Næste celle          |
| `Shift+Tab`   | Forrige celle        |
| `Enter`       | Ny paragraph i celle |
| `Shift+Enter` | Soft line break      |
| `←`           | Venstre i tekst      |
| `→`           | Højre i tekst        |
| `↑`           | Visuel linje op      |
| `↓`           | Visuel linje ned     |
| `Home`        | Start af linje       |
| `End`         | Slut af linje        |
| `Ctrl+Home`   | Dokumentstart        |
| `Ctrl+End`    | Dokumentslut         |
| `Backspace`   | Slet bagud i cellen  |
| `Delete`      | Slet fremad i cellen |

Der skal være eksplicitte regler for boundary cases.

---

# 38. Ctrl/Cmd+A

Dette er et klassisk problem.

Hvis cursor er i en tabel:

```text
Ctrl+A
```

bør editoren have en defineret semantik.

Mulige niveauer:

```text
1. tekst i celle
2. hele cellen
3. hele tabellen
4. hele dokumentet
```

En robust editor kan eksempelvis bruge gentagne `Ctrl+A`:

```text
Ctrl+A
→ tekst/celle

Ctrl+A igen
→ tabel

Ctrl+A igen
→ dokument
```

Men den konkrete adfærd skal være eksplicit specificeret og testes.

---

# 39. Copy / Paste

Tabeldata skal kunne kopieres.

### Tekst

```text
cell → copy → cell
```

### Flere celler

```text
┌─────┬─────┐
│ A   │ B   │
├─────┼─────┤
│ C   │ D   │
└─────┴─────┘
```

Copy/paste skal kunne genskabe:

```text
A B
C D
```

Tjek:

* [ ] Paste i én celle.
* [ ] Paste i flere celler.
* [ ] Paste fra Word.
* [x] Paste fra Excel (§40, lukket 2026-10-09).
* [ ] Paste fra HTML.
* [ ] Paste som ren tekst.
* [ ] Formatting håndteres.
* [ ] Tabelstruktur valideres før indsættelse.

---

# 40. Paste fra Excel

Status: implementeret 2026-10-09.

Excel-data:

```text
A    B    C
D    E    F
```

skal kunne blive:

```text
┌───┬───┬───┐
│ A │ B │ C │
├───┼───┼───┤
│ D │ E │ F │
└───┴───┴───┘
```

Tjek:

* [x] Tab-separeret tekst genkendes.
* [x] Linjeskift genkendes.
* [x] Antal kolonner bestemmes korrekt.
* [x] Manglende værdier håndteres.
* [ ] Formatting håndteres.
* [x] Quoted values håndteres.

Implementeringen konverterer tab-separeret tekst til en Markdown-tabel, når
alle betingelser er opfyldt:

* Hver linje skal være tab-separeret, og alle linjer skal have samme
  antal kolonner. Uens antal kolonner (inklusiv enkeltlinjer uden tabulator)
  afvises, og teksten indsættes som almindelig tekst.
* Konverteringen sker kun, når caret står uden for en tabel. Paste inde i en
  celle bruger den eksisterende celle-sanitering og må aldrig introducere
  rør-tegnet eller linjeskift i cellen.
* Excel-quoting understøttes: en celle i anførselstegn kan indeholde
  tabulatorer, og fordoblede anførselstegn bliver ét literalt tegn.
* Den første række bliver header-rækken, hvorefter der genereres en
  skillelinje. Caret ender i den første celle i første brødtekst-række.
* Indsættelsen sker på egne linjer og er ét undo-trin: hele indsættelsen
  (inklusiv nye linjer før og efter tabellen) sker som ét enkelt splice.
  En strukturel paste afbryder en igangværende skrive-coalesce, så den
  ikke flettes med sidste tastetryk.
* Rør-tegn i celleværdier escapes (`\|`), så de ikke bryder tabelstrukturen.
* Tomme celler i excel-udvalgets kanter bevares, fordi Excel kopierer hele
  rektanglet.
* Ensartet tab-indrykket tekst (fx en kodblok) har en tom første kolonne og
  konverteres ikke, fordi rigtige spreadsheet-data altid har mindst én
  værdi i første kolonne.
* Konverteringen sker på rå (uescape't) klippetekst, så Excel-citater med
  tabulatorer og rør-tegn i celleværdier tolkes korrekt. Escape af
  Markdown-tegn sker først efter tabellen er bygget.
* input med mere end 4096 linjer eller 64 kolonner afvises og indsættes som
  almindelig tekst (grænse mod uinterpretérbare store datapakker).
* Enkelt linje med tabulatorer (én Excel-række uden header-række) indsættes
  som almindelig tekst; konverteringen kræver mindst to linjer.

---

# 41. Tabel → tekst

Der kan være en funktion:

**Tabel → Konverter til tekst**

Eksempel:

```text
┌─────┬─────┐
│ A   │ B   │
├─────┼─────┤
│ C   │ D   │
└─────┴─────┘
```

bliver:

```text
A    B
C    D
```

---

# 42. Tekst → tabel

Omvendt:

```text
A    B    C
D    E    F
```

→

```text
┌───┬───┬───┐
│ A │ B │ C │
├───┼───┼───┤
│ D │ E │ F │
└───┴───┴───┘
```

Dette bør bruge samme parser som paste fra Excel.

---

# 43. Tabel ved dokumentets begyndelse/slutning

Test:

```text
[TABEL]
```

uden paragraphs omkring.

* [ ] Cursor kan placeres før tabellen.
* [ ] Cursor kan placeres efter tabellen.
* [ ] Det er muligt at skrive tekst efter tabellen.
* [ ] Det er muligt at indsætte paragraph før tabellen.
* [ ] Backspace/Delete omkring tabellen har defineret adfærd.

Eksempel:

```text
| 
┌───────┐
│ Tabel │
└───────┘
|
```

Der skal være en entydig model for de to caret-positioner uden for tabellen.

---

# 44. Klik før og efter tabel

Dette er tabelversionen af dit tidligere paragraph-problem.

Dokument:

```text
Paragraph

┌───────────┐
│ Tabel     │
└───────────┘

Paragraph
```

Test:

* [ ] Klik lige over tabellen → forrige paragraph.
* [ ] Klik lige under tabellen → efterfølgende paragraph.
* [ ] Klik inde i tabel → tabelcelle.
* [ ] Klik i whitespace mellem tabel og paragraph → korrekt block.
* [ ] Klik efter sidste celle → ikke automatisk inde i tabellen.
* [ ] Klik i sidste celles whitespace → stadig sidste celle.

---

# 45. Tabel → paragraph boundary

Der skal være en særlig testpakke:

```text
paragraph
table
paragraph
```

Test:

```text
clickBeforeTable
clickInsideFirstCell
clickInsideLastCell
clickAfterTable
returnBeforeTable
returnInsideCell
returnAfterTable
backspaceBeforeTable
backspaceAfterTable
deleteBeforeTable
deleteAfterTable
```

Ingen af disse operationer må utilsigtet:

* flytte tekst ind i tabellen
* flytte tekst ud af tabellen
* slette tabelstruktur
* oprette ekstra celler
* oprette ekstra paragraphs.

---

# 46. Undo/Redo

Alle tabeloperationer skal være undoable:

* [ ] Indsæt tabel.
* [ ] Indsæt række.
* [ ] Indsæt kolonne.
* [ ] Slet række.
* [ ] Slet kolonne.
* [ ] Flet celler.
* [ ] Opdel celler.
* [ ] Resize kolonne.
* [ ] Resize række.
* [ ] Formatting.
* [ ] Cell background.
* [ ] Paste.
* [ ] Slet tabel.
* [ ] Navigation må **ikke** skabe undo entries.

Eksempel:

```text
Click cell
Tab
Tab
Type "Hello"
```

Undo skal normalt kun fjerne `"Hello"` – ikke flytte cursoren tilbage gennem cellerne.

---

# 47. Accessibility

Tabelunderstøttelsen bør også have semantik.

* [ ] Tabel identificeres som tabel for screenreader.
* [ ] Rækker identificeres.
* [ ] Celler identificeres.
* [ ] Header-celler identificeres.
* [ ] Navigation med keyboard fungerer uden mus.
* [ ] Aktiv celle kan identificeres visuelt.
* [ ] Focus state er tydelig.
* [ ] Tabelkommandoer kan betjenes med keyboard.

---

# 48. Print og pagination

For en wordprocessor er dette vigtigt.

* [ ] Tabel kan gå over flere sider.
* [ ] Række må ikke blive visuelt ødelagt ved sideskift.
* [ ] Header-række kan gentages.
* [ ] Celleindhold kan fortsætte over sideskift efter valgt regel.
* [ ] Borders gengives korrekt.
* [ ] Tabellen respekterer margins.
* [ ] PDF-output matcher editorens visning så langt som muligt.

---

# 49. Central caret-model

Jeg ville specifikt gøre dette til en del af den tekniske arkitektur.

En caret i en tabel bør kunne beskrives som:

```text
CaretPosition {
    block: Table
    tableId: T1
    row: 2
    column: 3
    cellId: C7
    paragraphId: P14
    offset: 5
}
```

Det gør det muligt for alle operationer at arbejde ud fra samme model.

Eksempel:

```text
Mouse click
      ↓
CaretPosition
      ↓
Tab
      ↓
nextCell()
      ↓
CaretPosition
```

og:

```text
CaretPosition
      ↓
Enter
      ↓
splitParagraph()
      ↓
CaretPosition
```

---

# 50. Tabel som state machine

Det kan være nyttigt at tænke tabelredigeringen som:

```text
                    ┌──────────────┐
                    │ Outside      │
                    │ table        │
                    └──────┬───────┘
                           │ click
                           ↓
                    ┌──────────────┐
                    │ Cell active  │
                    └──────┬───────┘
                           │
             ┌─────────────┼─────────────┐
             ↓             ↓             ↓
           typing         Tab          mouse
             │             │             │
             ↓             ↓             ↓
          text edit    next cell    new caret
                           │
                           ↓
                     last cell?
                       /      \
                     no        yes
                     ↓          ↓
                  next cell   new row
```

Det gør især `Tab`, `Return`, mouse-click og cell-selection meget lettere at implementere konsistent.

---

# 51. Den vigtigste mouse-click-regel

Jeg ville sætte denne regel meget tydeligt i specifikationen:

> **Et museklik i en tabel skal først identificere den celle, som brugeren klikker i. Derefter skal click-positionen omsættes til en caret-position inden for denne celle.**

Altså:

```text
Mouse
 ↓
Table hit-test
 ↓
Cell hit-test
 ↓
Paragraph hit-test
 ↓
Text hit-test
 ↓
Normalize caret
 ↓
Set selection
```

**Man må ikke først finde den nærmeste tekst-node i hele dokumentet og derefter forsøge at afgøre, hvilken celle den tilhører.**

Det er især dét, der kan give fejl ved klik i whitespace, ved cellekanter og ved tomme celler.

---

# 52. Samlet acceptance-test

En tabelimplementering bør som minimum kunne bestå denne sekvens:

```text
1. Opret 3×3 tabel
2. Cursor starter i celle 1,1
3. Skriv tekst
4. Tab → celle 1,2
5. Skriv tekst
6. Tab → celle 1,3
7. Shift+Tab → celle 1,2
8. Klik med mus i celle 1,3
9. Cursor placeres korrekt i teksten
10. Klik efter teksten
11. Cursor bliver i celle 1,3
12. Enter
13. Ny paragraph oprettes i celle 1,3
14. Tab
15. Cursor går til celle 2,1
16. Tab gennem resten af tabellen
17. Tab fra sidste celle
18. Ny række oprettes
19. Markér to celler med mus
20. Flet celler
21. Undo
22. Indsæt kolonne
23. Slet række
24. Copy/paste tabelområde
25. Gem dokument
26. Genåbn dokument
27. Verificér tabelstruktur og caret
```

### Den overordnede arkitektur

Hvis det er en editor, du selv er ved at bygge, ville jeg faktisk behandle **tabel, paragraph og selection som tre relaterede strukturelle systemer**, frem for at lave særskilt adfærd i hver mouse- og keyboard-handler:

```text
                    DOCUMENT
                       │
             ┌─────────┴─────────┐
             │                   │
         BLOCKS              SELECTION
             │                   │
      ┌──────┴──────┐            │
      │             │            │
  PARAGRAPH       TABLE          │
                    │             │
               ┌────┴────┐       │
               │         │       │
             ROW        CELL──────┘
                         │
                     PARAGRAPH
                         │
                       TEXT
                         │
                       CARET
```

Så bliver **mouse-click, Tab, Return, Backspace, selection, copy/paste og tabelmenuerne forskellige operationer på den samme dokumentmodel**. Det er langt mindre tilbøjeligt til at give de boundary-fejl, som du beskriver med paragraph/cursor-problemet.
Ja — det bør være et **eksplicit strukturelt krav**, ikke bare en UI-begrænsning. Hvis nested tabeller ikke er tilladt, skal det håndhæves i både dokumentmodel, brugergrænseflade, paste/import og programmatisk manipulation.

Jeg ville tilføje følgende afsnit til specifikationen:

## 53. Nested tabeller er ikke tilladt

En tabel må **ikke indeholde en anden tabel**.

Den gyldige struktur er:

```text
Document
 ├── Paragraph
 ├── Table
 │    ├── Row
 │    │    ├── Cell
 │    │    │    └── Paragraph
 │    │    └── Cell
 │    │         └── Paragraph
 │    └── Row
 │         └── Cell
 │              └── Paragraph
 └── Paragraph
```

Denne struktur er **ikke tilladt**:

```text
Table
 └── Cell
      └── Table
           └── Cell
```

### Dokumentmodellen

* [ ] En `Table` må kun være child af dokumentets/block-niveau.
* [ ] En `Table` må ikke være child af en `Cell`.
* [ ] En `Cell` må indeholde tekst/paragraphs og eventuelt understøttede inline-elementer, men ikke en `Table`.
* [ ] En `Table` må ikke være child af en anden `Table`.
* [ ] Dokumentmodellen skal kunne validere dette som en invariant.
* [ ] Det skal være umuligt at skabe en nested tabel gennem den normale editor-API.

Eksempel på invariant:

```text
Table.parent MUST NOT be Cell
Table.parent MUST NOT be Table
```

---

## 54. Indsæt tabel inde i en celle

Hvis cursoren befinder sig i en tabelcelle:

```text
┌───────────────┬───────────────┐
│ Celle 1       │ Celle 2       │
│               │               │
│ cursor |      │               │
└───────────────┴───────────────┘
```

og brugeren vælger:

**Indsæt → Tabel**

må editoren **ikke indsætte en tabel i cellen**.

Der bør i stedet være en af følgende klart definerede adfærd:

* [ ] Menupunktet er disabled.
* [ ] Der vises en kort forklaring: "Tabeller kan ikke indsættes i en tabelcelle."
* [ ] Eller indsættelsen placeres uden for den eksisterende tabel, hvis editorens dokumentmodel understøtter en entydig caret-position dér.

Jeg vil anbefale **disabled + tooltip**, fordi det er mest forudsigeligt.

---

## 55. Paste af nested tabel

Det er især vigtigt ved copy/paste fra Word, Excel, HTML osv.

Hvis brugeren indsætter:

```text
Outer table
 └── Cell
      └── Inner table
```

skal editoren ikke acceptere strukturen ukritisk.

Paste-processen bør være:

```text
Clipboard
    ↓
Parse
    ↓
Normalize
    ↓
Validate document structure
    ↓
Remove/flatten nested table
    ↓
Insert
```

Tjek:

* [ ] Nested HTML `<table>` opdages.
* [ ] Nested tables fjernes eller konverteres efter en defineret regel.
* [ ] Editorens dokumentmodel kan aldrig ende med nested tables.
* [ ] Paste fra Word testes.
* [x] Paste fra Excel testes. ( Automatiseret dækning findes i
  `tests/table_interaction_test.cpp` under `ExcelPaste.*`; manuel test af
  selve paste-flowet i Windows mangler stadig. )
* [ ] Paste fra browser/HTML testes.
* [ ] Paste fra editoren selv testes.

---

## 56. Copy af tabel

Hvis brugeren kopierer en tabel eller et område af en tabel:

* [ ] Kopiering af hele tabel er tilladt.
* [ ] Kopiering af celler er tilladt.
* [ ] Kopiering til et andet sted uden for tabellen er tilladt.
* [ ] Kopiering til en celle må ikke skabe nested table.

Eksempel:

```text
Kopiér:

┌─────┬─────┐
│ A   │ B   │
├─────┼─────┤
│ C   │ D   │
└─────┴─────┘
```

Hvis brugeren står inde i en eksisterende celle og trykker Paste, skal editoren have en eksplicit regel.

Muligheder:

```text
Paste som tabel
```

→ indsætter tabellen **efter den eksisterende tabel**.

eller:

```text
Paste som tekst
```

→ indsætter indholdet i cellen.

Men aldrig:

```text
┌─────────────┐
│ ┌───┬───┐   │
│ │ A │ B │   │
│ └───┴───┘   │
└─────────────┘
```

---

## 57. Drag & drop

Det samme gælder drag & drop.

* [ ] En tabel kan flyttes inden for dokumentet.
* [ ] En tabel kan ikke droppes i en celle.
* [ ] En tabel kan ikke droppes på en anden tabel.
* [ ] En celle kan ikke indeholde en tabel.
* [ ] Drag/drop skal validere destinationen før insertion.

Flow:

```text
Drag table
    ↓
Find destination
    ↓
Is destination inside Cell?
    ↓
YES → reject
NO  → allow
```

---

## 58. Tabelmenuen

Når cursoren er inde i en tabelcelle, skal **Indsæt tabel** enten:

* [ ] være disabled, eller
* [ ] ikke være tilgængelig i tabelkonteksten.

Eksempel:

```text
Indsæt
 ├── Billede
 ├── Link
 ├── ...
 └── Tabel        [disabled]
```

Tooltip:

> Tabeller kan ikke indsættes i tabeller.

---

## 59. Tastaturgenveje

Hvis editoren har en genvej til tabelindsættelse:

```text
Ctrl+Alt+T
```

eller lignende:

* [ ] Genvejen må ikke oprette nested tabel.
* [ ] Den skal respektere samme validering som menuen.
* [ ] Samme regel gælder uanset inputmetode.

Der må altså ikke være:

```text
Menu → nested table = blocked
Shortcut → nested table = allowed
```

Alle entry points skal anvende samme command:

```text
insertTable()
       ↓
validateInsertionPoint()
       ↓
createTable()
```

---

## 60. Strukturel validering

Editoren bør have en generel dokumentvalidator:

```text
validateDocument()
```

med mindst:

```text
validateNoNestedTables()
```

Den skal kontrollere:

* [ ] Table → Table er ulovligt.
* [ ] Cell → Table er ulovligt.
* [ ] Table → Cell → Table er ulovligt.
* [ ] Clipboard-import kan ikke omgå valideringen.
* [ ] Undo/Redo kan ikke skabe nested tables.
* [ ] Import af eksisterende dokumenter valideres.
* [ ] Serialization/deserialization bevarer invariant.

---

## 61. Acceptance tests for nested tables

Der bør være en særskilt testgruppe:

```text
NestedTableTests
```

med mindst:

```text
insertTableInsideCell()
pasteTableInsideCell()
pasteHtmlWithNestedTable()
pasteWordTableInsideCell()
pasteExcelTableInsideCell()
dragTableIntoCell()
dragTableIntoTable()
undoNestedTableInsertion()
redoNestedTableInsertion()
deserializeNestedTable()
```

For hver test:

```text
assert document.containsNestedTable() == false
```

---

## 62. Den overordnede regel

Jeg ville formulere den som en **hard invariant** i specifikationen:

> **En tabel må aldrig indeholde en tabel. Nested tables er ikke understøttet. Dette gælder uanset om tabellen oprettes via menu, tastatur, copy/paste, drag & drop, import, undo/redo eller editorens interne API.**

Det er bedre end kun at sige, at "brugeren kan ikke indsætte en tabel i en celle", fordi det gør det klart, at **nested tables er en egenskab, dokumentmodellen aldrig må kunne indeholde**.


---

## 63. Implementeringsstatus (2026-09-17)

Grundfunktionaliteten i dette dokument er implementeret. Det dækker:

* **Celle-grænser (§5, §8, §12, §17, §18, §51):** `TableCellAtOffset`,
  `DeleteBackwardInCell`, `DeleteForwardInCell` og venstre/højre-pilene
  klemmer caret inden for cellen; en pipe kan aldrig slettes ved
  Backspace/Delete.
* **Række-navigation og -udvidelse (§10):** Tab flytter struktur mellem
  celler; fra sidste celle indsættes en ny blank række, og caret
  placeres i dens første celle.
* **Indsætning (§3, §53, §62):** `InsertTableFromGrid` afviser at køre
  inde i en celle og placerer caret i den første overskriftscelle;
  Ctrl+A har fire trin (celle, række, tabel, dokument).
* **Paste (§55, §56):** paste i en celle filtrerer pipes og folder
  linjeskift (`SanitizePasteForTableCell`), så en ydre tabel ikke
  kan ødelægge cellen.
* **Slet tabel (§27):** kontekstmenu "Delete Table" og ribbon-knop
  `RemoveTable` fjerner hele tabellen; caret placeres i den følgende
  blok (eller forbliver på fjernelsesstedet, hvis tabellen er sidst).
* **Valg (§19, §38):** dobbeltklik vælger cellen, tredobbeltklik
  rækken, Ctrl+A trapper op.

Dokumentet er en ide-samling; krav under punkt 60 (fuld
dokumentvalidator) og de andre udbudte vurderingsfunktioner er
endnu ikke implementeret.

Kendte afvigelser (bevidste):

* Enter og Shift+Enter i en celle er no-op. En tabelcelle består af
  én fysisk linje i Markdown-kildeteksten, så et nyt afsnit inde
  i cellen kan ikke udtrykkes (jf. punkt 15-16).
* Ctrl+venstre/Ctrl+højre kan krydse cellegrænser, da ordbevægelse
  er dokumentglobal; Tab forbliver den strukturelle navigation.
* Strukturkommandoer (flet/opdel celler, kolonnebredde, baggrund,
  justering) mangler; de kræver en mere omfattende tabelmodel.

Der er gennemført en uafhængig kodegennemgang, hvor følgende fejl
blev fundet og rettet:

* Drag & drop ind i en celle kunne indsætte pipes og brække
  rækken; nu sanitizes eller afvises segmentet.
* Slet række kunne fjerne overskrifts- eller skillelinjen i
  større tabeller; vagten gælder nu for alle tabeller.
* Slet tabel placerede caret før tabellen; den rykker nu først
  ind i den følgende blok (jf. punkt 27).
* Markér-med-mus kunne trække valget ud over cellegrænsen,
  selvom klik var klemmet; drag bruger nu samme regel.
* Backspace på tabellens afsluttende pipe var stille no-op;
  celleopslagningen omfatter nu rækkens yderste pipes.

### Status 2026-09-25

Følgende huller er lukket siden 2026-09-17:

* **Indsæt række over (§23):** `AddTableRow(below)` understøtter nu
  begge retninger. En ny række over overskriften eller mellem
  overskrift og separatorlinje afvises, fordi disse linjer bærer
  tabellens struktur. Caret placeres i den nye rækkes første celle.
* **Indsæt kolonne til venstre (§4):** `AddTableColumn(right)`
  understøtter nu begge retninger med Tabler-ikoner
  (`table-add-row-above`, `table-add-column-left`) i
  Table Tools-dropdownen.
* **Add Column var defekt:** den gamle kode indsatte otte mellemrum
  uden en pipe og kunne derfor aldrig oprette en ny kolonne
  (md4c afgrænser kolonner med pipes; kommentaren i koden matchedede
  ikke koden). Den indsætter nu `"        |"` hhv. `"--------|"`;
  højre indsættes efter pipen der lukker cursor-cellen, venstre efter
  pipen der åbner den. Caret-forskydning akkumuleres pr. linje, så
  caret lander deterministisk.
* **Dokumentvalidator (§60):** `ValidateDocument(doc, errors)` i
  parser.cpp checker tabelinvarianter: rækker og celler ikke-tomme,
  ens kolonneantal på tværs af rækker, og celle-kildeomspænd inden for
  tabellens eget kildeområde. Dækket af tre nye parser-tests
  (velformet tabel, jagged kilde efter md4c-udjævning, og
  håndkonstruerede brud på invarianterne). Nested tables forbliver
  urepræsentable i dokumentmodellen (en `TableCell` kan kun holde
  tekst og inline-spans), så invarianten håndhæves strukturelt.

Stadig åbne krav: fletning af celler (§21),
kolonnebredde-drag (§31), cellejustering (§29-30), cellebaggrund
(§35), tabel-til-tekst og tekst-til-tabel (§41-42), og en
dokumentvalidator ud over tabeller (§60). Excel-paste som tabel (§40)
blev lukket 2026-10-09, medens de øvrige kræver den mere
omfattende tabelmodel, som nævnt ovenfor.

### Status 2026-10-03

Fire fejl i tabelkommandoerne er rettet. De er fundet ved at
transkribere råforslags-aritmetikken fra `app.cpp` og køre den, så
hver påstand nedenfor er verificeret mod faktisk output:

* **Slet kolonne fjernede to pipes i stedet for én.** Den gamle kode
  slettede fra den pipe der åbnede kolonnen til og med den pipe der
  lukkede den, så en tredjekolonnetabel faldt sammen til én kolonne,
  og naborcellerne blev fusioneret (`| a | b | c |` → `| a  c |`).
  `TableRemoveColumn` i `navigation.cpp` bevarer nu den åbnende pipe
  og fjerner kun den lukkende, så tabellen mister præcis én kolonne.
* **Caret blev ikke justeret ved slet kolonne.** Den stod tilbage på
  sin gamle offset og kunne lande forbi slutningen af den kortere
  tekst. Helperen modregner nu de fjernede bytes foran caret, så den
  bliver i samme celle.
* **Undo var delt op pr. linje (§46).** `AddTableColumn` og
  `RemoveTableColumn` kaldte `SpliceWithUndo` én gang pr. linje, så ét
  Ctrl+Z fjernede én linje og efterlod tabellen takket. Begge bygger
  nu hele tabellen og anvender ét splice, så kommandoen er ét
  historiktrin.
* **Indsæt række under ødelagde den sidste linje.** En tabel der
  ender ved EOF uden afsluttende newline fik den nye række sat direkte
  på den foregående rækkes linje (`| c | d ||        |        |`).
  Der indsættes nu et linjeskift før den nye række.

Logikken er flyttet ud af `app.cpp` til to bærbare funktioner,
`TableInsertColumn` og `TableRemoveColumn`, så den er dækket af
`tests/table_interaction_test.cpp` uden Windows-værktøjer.

Ribbonens tabelknapper var heller ikke med i `UI_PKEY_Enabled`-svaret
i `ribbon.cpp`, og `SetEdit` invaliderede dem ikke, så de var aktive
mens redigering var slået fra. De er nu tilføjet begge steder.
Enabling følger desuden caretens kontekst: række- og kolonnekommandoer
kræver en caret i en tabel, og *Indsæt tabel* deaktiveres i en celle,
fordi en tabel aldrig må indeholde en tabel (§54, §58).

Pop-up'en til *Indsæt tabel* lå fast på en venstreposition i
vinduet uanset hvor careten stod. Den forankres nu til caretens
skærmposition, skaleres efter DPI og holdes inden for skærmens
arbejdsområde.

En uafhængig kodegennemgang fandt desuden fire fejl, som nu er rettet
og dækket af `tests/table_interaction_test.cpp`:

* **En regression fra den første rettelse:** de nye hjælpefunktioner
  returnerede hele dokumentet, mens kaldet indsatte dem kun over
  tabellens eget område. På ethvert dokument med tekst før eller efter
  tabellen blev denne tekst derfor duplikeret. Hjælpefunktionerne
  returnerer nu tabellens egen omskrevne stræk.
* **Tab fra sidste celle satte caret uden for tabellen** (§10). Careten
  landede efter den nye rækkes newline, så det næste tastetryk skrev på
  rækkens ledende pipe i stedet for i en celle.
* **Slet tabel brugte offsets fra før sletningen som om de var efter**
  (§27), så caret kunne ende forbi slutningen af dokumentet.
* **Tabellenes slutoffset blev regnet som inde i tabellen.** Careten lige
  efter en tabel fik derfor indhold indsat med celle-saniteringen, så
  indsættelse af en tabel efter en tabel degraderede til tekst.
* **Backspace lige før en tabel slettede den blanklinje, der gør den til
  en tabel** (§45, `backspaceBeforeTable`). Der sættes nu en grænse, så
  tabellen ikke trækkes op i afsnittet oven over.

Den bærbare del af målsættet kan compiles og køres direkte på Linux med
`pip install ziglang` (fuld clang, ingen root). Se skillet
`markdownit-linux-portable-tests`. Det dækker parser, navigation,
editcontroller, textbuffer, undostack, formatting, inputfilter og
textdrag, men ikke `app.cpp` eller `ribbon.cpp`: båndets
enable/disable, popup-placering og DPI er fortsat Windows CI's
ansvar.

### Række- og kolonnekommandoer følger caretens række (§23-26)

Båndet viste tidligere *Fjern række* og *Indsæt række over* som
aktive, selv når de ville afvise handlingen, fordi enable-logikken kun
spurgte, om careten overhovedet stod i en tabel. Der er nu én
kilde til sandheden:

* `TableCapabilitiesFor(rowIndex, numCols)` i `navigation.cpp` svarer
  på, hvad der kan gøres. Båndet spørger den via
  `CaretTableCapabilities()`, og kommandoerne selv (`AddTableRow`,
  `RemoveTableRow`, `RemoveTableColumn`) spørger den samme funktion i
  deres egne guards. En aktiveret knap kan derfor ikke være en knap,
  der intet gør.

Reglerne er:

| Caret i | Tilføj række over | Tilføj række under | Fjern række |
|---|---|---|---|
| overskrift (række 0) | nej | **nej** | nej |
| skillelinje (række 1) | nej | ja | nej |
| brødtekst (række 2+) | ja | ja | ja |

*Indsæt række under* på overskriftsrækken var en rigtig fejl, ikke
bare en død knap: md4c kræver, at skillelinjen står umiddelbart
efter overskriften, så en række indsat mellem dem reparses som **nul**
tabeller, og tabellen forsvinder som tabel. Det er dækket af
`TableRowGuards.InsertingBelowHeaderDestroysTheTable`, som indsætter
og reparserer for at bevise det.

Kolonnefjernelse afvises på den eneste kolonne (`numCols <= 1`), fordi
resultatet ellers ville være en tabel uden kolonner.

### Kolonnejustering (§28)

`:---`, `:---:` og `---:` læses fra skillelinjen (md4c's
`MD_BLOCK_TD_DETAIL.align`) og gemmes i `Node::aligns`. Før dette blev
de hængt venstrestillet uanset hvad kilden sagde, så et dokument med en
højrestillet kolonne tegnede forkert. Rendererens tegneflade sætter nu
`SetParagraphAlignment`, hvilket også får markeringsrektet til at følge
justeringen, da DirectWrite laver layouten inden i cellen.

`TableSetColumnAlign` skriver kun én celle i skillelinjen og bevarer
bindestregetallet, så en luftet tabel (`| ---: |`) forbliver luftet og en
kompakt (`|---|`) forbliver kompakt. `TableAlignMark::None` fjerner
markeringen igen. En celle med færre end tre bindestreger fyldes op til
tre, fordi `:-` ikke læses som skillelinje af md4c.

Båndet fik tre knapper, *Venstrestil*, *Centreret* og *Højrestil*. De er
aktive i enhver tabel, fordi de skriver i skillelinjen uanset caretens
placering.

### Op og ned i en tabel (§27)

Pile-tasten går én række op eller ned og bevarer kolonnen, via den
bærbare `TableVerticalMove`. Uden for en tabel forbliver den en visuel
linjebevægelse.

* Caret på skillelinjen læses som overskriftsrækken, fordi skillelinjen
  ikke har tekst at placere en caret i. Ned fra den lander i første
  brødtextrække, op forbliver den.
* Rækken må ikke forlades med pile-tasten. Bevægelsen afvises, så kalderen
  falder tilbage til visuel linjebevægelse, i stedet for at sende careten
  ud af tabellen.
* Byte-forskydningen i cellen bevares, men klemmes til målcellens længde, så
  careten aldrig lander i næste kolonne.

### Rør-tegnet kun undviges i tabelceller (§24)

Et `|` i en tabelcelle ville dele cellen og er derfor undviet som `\|`.
Uden for en tabel er det et helt almindeligt tegn, og det undvies ikke
længere, så brugerens markdown ikke fyldes med skråstreger, der aldrig
blev bedt om. `EscapeForInsert` og `EscapeForPaste` tager derfor et
`inTableCell`-flag, som appen sætter fra `IsOffsetInTable`.

### Opdel celle (§22)

Markdown kan ikke udtrykke en flettet celle, så `| a | b |` har ingen
repræsentation for "én celle der spænder to kolonner". Derfor er der kun
denne ene retning: opdel. En sammenlægning ville kræve HTML (`colspan`)
eller opgive cellens struktur, og begge dele ville bryde med at
`.md`-filen forbliver ren Markdown.

`TableSplitCell` deler caretens celle i to (eller flere) celler:

* Kun caretens **egen række** får tekst. Alle andre rækker får en
  tom ledsager-celle, så tabellen bevarer ens kolonneantal på tværs
  af rækker (validatoren i §60).
* Teksten fordeles venstre mod højre i hele tegn, og resten lægges i
  den sidste celle, så ingen celle ender midt i en UTF-8-sekvens.
* Ledsagerne får samme bredde som cellen der deles, så rækken beholder
  sit rytme. Deler man kun én celle og lader resten af tabellen stå,
  får den række én celle mere end de andre, og md4c fylder korte
  rækker op på vej tilbage.
* Hver celle skrives `[padding][indhold][padding]`. En tom celle skal
  derfor stadig skrive sine mellemrum: `||` læses som to tomme
  celler og ændrer kolonnetallet ved gensparsing.
* Række 0 (overskrift) og række 1 (skillelinje) afvises. Overskriften
  og skillelinjen definerer tabellens form, og skillelinjen skal
  ligge umiddelbart efter overskriften.

Båndet fik knappen *Split Cell*, aktiv i brødtextrækker.

Kendte afvigelser: opdeling i mere end to celler er implementeret i
`TableSplitCell` (`pieces`-parameteren), men båndet tilbyder kun to,
fordi der ikke er nogen dialog til at vælge antallet. §22's krav om at
"brugeren kan vælge antal celler" er dermed ikke opfyldt i UI'en.
