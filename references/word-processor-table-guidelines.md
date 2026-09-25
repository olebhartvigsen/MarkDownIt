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

**Indsæt række over:**

```text
┌─────┬─────┐
│     │     │
├─────┼─────┤
│ A   │ B   │
├─────┼─────┤
│ C   │ D   │
└─────┴─────┘
```

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

Tjek:

* [ ] Kolonnen indsættes korrekt.
* [ ] Rækkeantallet bevares.
* [ ] Bredde beregnes korrekt.
* [ ] Tabellen overstiger ikke utilsigtet dokumentets bredde.

---

# 25. Slet række

* [ ] Aktiv række kan slettes.
* [ ] Flere markerede rækker kan slettes.
* [ ] Sidste række håndteres korrekt.
* [ ] Sletning af sidste række skal enten:

  * slette tabellen, eller
  * efterlade en tom række.
* [ ] Adfærden er eksplicit defineret.
* [ ] Cursor placeres korrekt bagefter.

---

# 26. Slet kolonne

Samme princip.

* [ ] Aktiv kolonne slettes.
* [ ] Flere kolonner kan slettes.
* [ ] Sidste kolonne håndteres korrekt.
* [ ] Tabellen må ikke ende i en ugyldig 0-kolonne tilstand.

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
* [ ] Paste fra Excel.
* [ ] Paste fra HTML.
* [ ] Paste som ren tekst.
* [ ] Formatting håndteres.
* [ ] Tabelstruktur valideres før indsættelse.

---

# 40. Paste fra Excel

Dette bør være et specifikt testområde.

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

* [ ] Tab-separeret tekst genkendes.
* [ ] Linjeskift genkendes.
* [ ] Antal kolonner bestemmes korrekt.
* [ ] Manglende værdier håndteres.
* [ ] Formatting håndteres.
* [ ] Quoted values håndteres.

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
* [ ] Paste fra Excel testes.
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

Stadig åbne krav: fletning og opdeling af celler (§21-22),
kolonnebredde-drag (§31), cellejustering (§29-30), cellebaggrund
(§35), Excel-paste som tabel (§40), tabel-til-tekst og
tekst-til-tabel (§41-42), og en dokumentvalidator ud over tabeller
(§60). De kræver den mere omfattende tabelmodel, som nævnt ovenfor.
