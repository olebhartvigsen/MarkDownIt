
## Implementeringsstatus

Planen er gennemgået mod den native Win32-implementering. Afsnit 19 om browser-API'er gælder ikke for MarkDownIt.

Der var en konflikt mellem afsnit 5 og acceptkriterium D. Afsnit 5 flytter caret'en til det eksisterende næste afsnit, mens D kræver et nyt tomt afsnit. Implementeringen følger acceptkriterium D: Return ved paragraph-end opretter et tomt, addressérbart afsnit efter det aktuelle afsnit.

Følgende er implementeret og dækket af automatiske tests:

- Virtuelle tomme paragraph-noder for et tomt dokument og ekstra paragraph-separatorer.
- Et DirectWrite-layout med caret-position til virtuelle tomme paragraphs.
- Klik til højre for et afsnits tekst fastholder den pågældende blok og dens paragraph-end.
- Normalisering efter afsluttende formatteringsmarkører og link-destinationer.
- Atomisk Selection + Return med korrekt Undo og Redo.
- Shift+Return som en synlig Markdown-hard line break, uden at oprette et nyt paragraph.
- Backspace og Delete, der samler almindelige paragraphs ved deres separator.
- Balanceret Return-split inde i strong-formatering og linktekst.
- Ingen dirty/reparse ved Backspace eller Delete, når handlingen ikke ændrer dokumentet.

Følgende er stadig uden for denne afgrænsede rettelse og må ikke betragtes som godkendt uden en separat ændring:

- Kompleks Selection + Return på tværs af flere indlejrede formatterings- og link-runs.
- En selvstændig caret-position omkring et image uden tekst.
- Manuel Windows-verifikation af DirectWrite-hit-test, caret-tegning og zoom.

## 1. Definér den grundlæggende dokumentmodel

Før cursor- og Return-logikken implementeres, skal det være entydigt, hvad et afsnit er.

* [ ] Et afsnit (`paragraph`) er en selvstændig strukturel node.
* [ ] Tekst ligger som tekst-noder/runs inde i afsnittet.
* [ ] Et afsnit har en entydig startposition.
* [ ] Et afsnit har en entydig slutposition.
* [ ] Slutpositionen er **ikke nødvendigvis efter sidste synlige tegn**, men er en logisk caret-position.
* [ ] Der findes en klar definition af caret-positionen:

  * før første tegn
  * mellem tegn
  * efter sidste tegn
  * ved paragraph-end.
* [ ] En tom paragraph har stadig en gyldig caret-position.
* [ ] Dokumentets sidste paragraph har også en gyldig slutposition.
* [ ] Der skelnes mellem **tekstposition** og **strukturel position**.

Eksempel:

```text
Paragraph 1
"Dette er tekst|"

Paragraph 2
"Dette er næste afsnit"
```

`|` skal kunne eksistere som en legitim caret-position **efter `tekst`**, selv om der ikke findes et faktisk tegn dér.

---

# 2. Mouse click → caret-position

Dette er sandsynligvis dér, hvor din nuværende fejl ligger.

Ved et klik skal editoren gennemføre noget i stil med:

```text
Mouse coordinates
       ↓
DOM/layout element
       ↓
paragraph
       ↓
text node / structural node
       ↓
character offset
       ↓
normalized caret position
```

Tjek:

* [ ] Klik på et tegn placerer cursoren før/efter tegnet efter almindelig caret-logik.
* [ ] Klik mellem to tegn giver den forventede position.
* [ ] Klik efter sidste tegn i et afsnit giver **paragraph-end**.
* [ ] Klik langt ude til højre for sidste tegn giver stadig paragraph-end.
* [ ] Klik på den tomme plads efter sidste tegn må ikke flytte cursoren til næste paragraph.
* [ ] Klik på linjens visuelle slutning må ikke automatisk blive fortolket som starten på næste paragraph.
* [ ] Klik under sidste linje i paragraphen håndteres deterministisk.
* [ ] Klik på paragraphens padding/margin håndteres eksplicit.
* [ ] Klik på tom paragraph giver positionen i den tomme paragraph.
* [ ] Klik på sidste paragraph i dokumentet giver en gyldig position.
* [ ] Klik på slutningen af en paragraph efter inline-elementer fungerer.
* [ ] Klik efter eksempelvis `<strong>tekst</strong>` fungerer.
* [ ] Klik efter link, image, mention eller anden inline-node fungerer.
* [ ] Klik efter en trailing `<br>` fungerer.
* [ ] Klik i whitespace efter tekst fungerer.

### Kritisk regel

Implementeringen bør ikke bare gøre:

```text
click → nærmeste character
```

men:

```text
click
→ find nærmeste caret-position
→ begræns positionen til den aktuelle paragraph
→ normaliser positionen
```

Det er især vigtigt, fordi **"visuelt nærmest" og "strukturelt korrekt" ikke altid er det samme**.

---

# 3. Det kritiske edge case: klik efter sidste tegn

Dette bør være et særskilt testområde.

Dokument:

```text
Dette er et afsnit.
```

Test:

```text
Dette er et afsnit.|
```

Klik:

* [ ] direkte efter punktum
* [ ] 1 px efter punktum
* [ ] 5 px efter punktum
* [ ] langt ude i højre side af paragraphen
* [ ] under baseline
* [ ] over baseline
* [ ] i whitespace efter teksten

For alle disse skal editoren have en veldefineret adfærd.

Normalt vil den ønskede semantik være:

```text
paragraph = "Dette er et afsnit."
caret = paragraph.end
```

**ikke**

```text
paragraph = next paragraph
caret = paragraph.start
```

---

# 4. Return-tasten skal arbejde ud fra caret-positionen

Når brugeren trykker Return, skal editoren først afgøre:

> Hvilken strukturel position befinder caret'en sig i?

Ikke blot:

> Hvilken tekst-node har cursoren?

Eksempel:

```text
Før Return:

Paragraph A
"Dette er| tekst"

```

Return:

```text
Paragraph A
"Dette er"

Paragraph B
"| tekst"
```

Tjek:

* [ ] Return midt i tekst splitter paragraphen.
* [ ] Tekst før caret bliver i første paragraph.
* [ ] Tekst efter caret bliver i anden paragraph.
* [ ] Formatting før caret bevares.
* [ ] Formatting efter caret bevares.
* [ ] Inline elements på begge sider håndteres korrekt.
* [ ] Selection/caret placeres i den nye paragraph korrekt.
* [ ] Undo kan samle ændringen igen.

---

# 5. Return ved slutningen af paragraphen

Dette er det andet kritiske område.

Før:

```text
Paragraph A
"Dette er tekst|"

Paragraph B
"Dette er næste afsnit"
```

Return skal normalt give:

```text
Paragraph A
"Dette er tekst"

Paragraph B
"|Dette er næste afsnit"
```

**Ikke:**

```text
Paragraph A
"Dette er tekst"

Paragraph B
"Dette er næste afsnit"

Paragraph C
"|"
```

Det kræver, at editoren kan skelne mellem:

```text
paragraph.end
```

og

```text
start of next paragraph
```

---

# 6. Return i starten af paragraphen

Test også det modsatte.

```text
Paragraph A
"Dette er første afsnit"

Paragraph B
"|Dette er næste afsnit"
```

Return bør give:

```text
Paragraph A
"Dette er første afsnit"

Paragraph B
""

Paragraph C
"|Dette er næste afsnit"
```

eller den dokumentmodel, som editoren eksplicit har valgt.

Det vigtige er, at resultatet er **deterministisk**.

---

# 7. Return i en tom paragraph

Test:

```text
Paragraph A
"Tekst"

Paragraph B
"|"

Paragraph C
"Tekst"
```

* [ ] Return i tom paragraph fungerer.
* [ ] Return fører ikke til tab af paragraphen.
* [ ] Cursor placeres korrekt.
* [ ] Gentagne Return virker.
* [ ] Backspace efterfølgende kan samle paragraphs korrekt.

Test:

```text
|
```

i et helt tomt dokument:

* [ ] Return må ikke få editoren til at ende uden en gyldig paragraph.
* [ ] Gentagne Return skaber forventede paragraphs.

---

# 8. Selection + Return

Det er vigtigt ikke kun at teste en caret.

Test:

```text
Dette er [noget tekst] mere tekst
```

Tryk Return.

* [ ] Den valgte tekst erstattes.
* [ ] Selection slettes.
* [ ] Paragraph splittes korrekt.
* [ ] Formatting bevares på begge sider.
* [ ] Cursor placeres i den nye paragraph.

Test selection:

* [ ] fra midt i paragraph til slutningen
* [ ] fra starten til midt i paragraph
* [ ] hele paragraphen
* [ ] på tværs af to paragraphs
* [ ] på tværs af flere paragraphs.

---

# 9. Mus + Return som samlet state machine

Jeg ville eksplicit modellere editoren som:

```text
Document
  ↓
Paragraph
  ↓
Block position
  ↓
Caret/Selection
  ↓
Keyboard operation
  ↓
Document mutation
  ↓
Selection normalization
  ↓
Render
```

I stedet for at lade mouse handleren og keyboard handleren have hver deres fortolkning af cursoren.

Der bør være én canonical repræsentation, fx:

```text
CaretPosition {
    blockId
    inlinePath
    offset
}
```

eller endnu bedre en abstraktion som:

```text
Position {
    paragraphId
    offset
}
```

hvor `offset` er en **logisk tekst-/inline-position**.

---

# 10. Normalisering af caret

Efter **alle** handlinger bør caret normaliseres.

Fx:

```text
normalizeCaret(position)
```

skal håndtere:

* [ ] position før tekst
* [ ] position mellem tegn
* [ ] position efter tekst
* [ ] position i empty paragraph
* [ ] position efter inline element
* [ ] position omkring `<br>`
* [ ] position omkring links
* [ ] position omkring formatting nodes
* [ ] position omkring embeds
* [ ] position ved paragraph boundary.

Målet er:

> Der findes kun én canonical repræsentation af den samme logiske caret-position.

Det forhindrer eksempelvis, at:

```text
textNode offset=10
```

og

```text
paragraph offset=10
```

repræsenterer to forskellige positioner, selv om de visuelt er identiske.

---

# 11. Paragraph boundary skal være eksplicit

Undgå at udlede paragraph-grænsen alene fra DOM-strukturen.

Editoren bør kunne svare entydigt på:

```text
isAtParagraphStart()
isAtParagraphEnd()
getCurrentParagraph()
getPreviousParagraph()
getNextParagraph()
```

Og især:

```text
isAtParagraphEnd(caret)
```

skal være baseret på den **logiske dokumentmodel**.

Eksempel:

```text
"Hello <strong>world</strong>"
```

Caret efter `world` skal stadig være:

```text
paragraph.end = true
```

selv om DOM-cursoren måske befinder sig inde i en `<strong>` node.

---

# 12. Mouse hit-testing bør testes systematisk

Lav en testmatrix:

| Klikposition            | Forventet position             |
| ----------------------- | ------------------------------ |
| Før første tegn         | paragraph start                |
| På første tegn          | omkring første tegn            |
| Mellem tegn             | mellem tegn                    |
| På sidste tegn          | før/efter efter hit-test-regel |
| Lige efter sidste tegn  | paragraph end                  |
| Langt efter sidste tegn | paragraph end                  |
| På blank linje          | paragraph-position             |
| I næste paragraph       | næste paragraph                |
| På inline-element       | position omkring element       |
| På whitespace           | nærmeste gyldige caret         |
| På empty paragraph      | paragraph start/end            |

---

# 13. Test med flere linjer

Et paragraph kan naturligvis wrappe:

```text
Dette er en meget lang tekst som
automatisk fortsætter på næste
visuelle linje.
```

Her skal man skelne mellem:

**visuel linjeslutning**

og

**paragraph-slutning**.

Test:

* [ ] Klik ved slutningen af første visuelle linje.
* [ ] Klik ved slutningen af anden visuelle linje.
* [ ] Klik efter sidste visuelle linje.
* [ ] Return ved visuel linjeslutning.
* [ ] Return ved paragraph-end.

Return ved slutningen af en **wrapped line** skal ikke skabe et nyt paragraph på samme måde som Return ved paragraph-end.

---

# 14. Shift+Return

Denne skal testes separat.

```text
Tekst|mere tekst
```

Shift+Return:

```text
Tekst
|mere tekst
```

Det er typisk en **soft line break**, ikke et nyt paragraph.

Tjek:

* [ ] Shift+Return midt i tekst.
* [ ] Shift+Return ved paragraph-end.
* [ ] Shift+Return i tom paragraph.
* [ ] Flere Shift+Return efter hinanden.
* [ ] Return efter Shift+Return.
* [ ] Backspace over soft break.

---

# 15. Backspace/Delete skal bruge samme model

Hvis mouse/Return buggen skyldes forkert caret-position, vil Backspace ofte også have problemer.

Test:

```text
Paragraph A
"Tekst|"

Paragraph B
"Tekst"
```

Backspace ved start af paragraph B:

```text
Paragraph A
"Tekst|Tekst"
```

Tjek:

* [ ] Backspace ved paragraph-start merger paragraphs.
* [ ] Delete ved paragraph-end merger paragraphs.
* [ ] Backspace i tom paragraph.
* [ ] Delete i tom paragraph.
* [ ] Undo efter merge.
* [ ] Formatting efter merge bevares.

---

# 16. Undo/Redo

Alle disse operationer skal være atomiske.

Test:

```text
click → Return → type → undo
```

samt:

```text
click → Return → undo
```

og:

```text
click → Return → Return → Backspace → undo → redo
```

Efter hver operation:

* [ ] Dokumentstruktur korrekt.
* [ ] Selection korrekt.
* [ ] Caret korrekt.
* [ ] Formatting korrekt.

---

# 17. Regressionstest for mouse-click buggen

Jeg ville lave en specifik testpakke med navnet fx:

**`CaretParagraphBoundaryTests`**

med mindst disse tests:

```text
clickAtParagraphEnd()
clickBeyondParagraphEnd()
clickAtEmptyParagraph()
clickAtWrappedLineEnd()
returnAtParagraphEnd()
returnAtParagraphStart()
returnInParagraphMiddle()
returnInEmptyParagraph()
returnAfterClickAtParagraphEnd()
returnAfterClickBeyondParagraphEnd()
backspaceAtParagraphStart()
deleteAtParagraphEnd()
undoReturnAtParagraphEnd()
```

Den særligt vigtige test er:

```text
clickAtParagraphEnd()
→ assert caret.paragraph == expectedParagraph
→ assert caret.offset == paragraph.length

pressReturn()
→ assert document.paragraphCount == original + 1
→ assert new caret == newParagraph.start
```

---

# 18. Visuel test

Funktionaliteten bør også testes visuelt.

For hver caret-position:

* [ ] Cursor tegnes præcis dér, hvor editorens interne position siger den er.
* [ ] Cursor springer ikke visuelt til næste paragraph.
* [ ] Cursor har korrekt højde ved forskellige fonts.
* [ ] Cursor fungerer ved bold/italic.
* [ ] Cursor fungerer ved forskellige font sizes.
* [ ] Cursor fungerer ved links.
* [ ] Cursor fungerer ved RTL/LTR hvis relevant.
* [ ] Cursor fungerer ved zoom.
* [ ] Cursor fungerer ved browser scaling.

---

# 19. Browser/DOM-specifikke problemer

Hvis editoren er browserbaseret, bør følgende testes eksplicit:

* [ ] `contenteditable`
* [ ] `Selection`
* [ ] `Range`
* [ ] `getBoundingClientRect()`
* [ ] `caretRangeFromPoint()` hvis anvendt
* [ ] `caretPositionFromPoint()` hvis anvendt
* [ ] browserens native caret-positioner
* [ ] Chrome
* [ ] Edge
* [ ] Firefox
* [ ] Safari hvis relevant.

Man bør **ikke stole blindt på browserens Range-resultat** som dokumentmodellens sandhed. Browseren kan returnere en DOM-position, som efterfølgende skal oversættes til editorens egen canonical position.

---

# 20. Den vigtigste arkitekturregel

Jeg ville sætte denne regel øverst i implementationen:

> **Mouse hit-testing må aldrig selv bestemme dokumentets semantik.**

Den skal kun finde en kandidat-position.

Derefter:

```text
Mouse
  ↓
Hit testing
  ↓
DOM position
  ↓
Convert to editor position
  ↓
Normalize position
  ↓
Canonical Caret
```

Og Return:

```text
Keyboard
  ↓
Canonical Caret
  ↓
Determine paragraph boundary
  ↓
Split paragraph
  ↓
Normalize new Caret
  ↓
Render
```

Dermed bruger **mouse, keyboard, Backspace, Delete, navigation og selection den samme positionmodel**.

---

## 21. Acceptance criteria

Jeg ville afslutte implementeringen med disse krav:

### A. Caret

> Et museklik skal altid resultere i én entydig og gyldig caret-position i dokumentmodellen.

### B. Paragraph-end

> Klik efter sidste tegn i et paragraph skal placere caret i det pågældende paragraphs slutposition og ikke i næste paragraph.

### C. Return

> Return skal fortolke den aktuelle canonical caret-position og splitte paragraphen dér.

### D. Paragraph-end + Return

> Return fra paragraph-end skal oprette en ny paragraph efter den aktuelle paragraph og placere caret i starten af den nye paragraph.

### E. Wrapped lines

> Visuel slutning af en linje må ikke forveksles med slutningen af et paragraph.

### F. Empty paragraphs

> En tom paragraph skal have en gyldig caret-position og kunne modtage Return, Backspace og Delete.

### G. Consistency

> Mouse click, arrow navigation, selection, Return, Backspace og Delete skal alle anvende samme canonical positionmodel.

### H. Regression

> Alle boundary cases skal være automatiserede tests, så ændringer i hit-testing eller DOM-rendering ikke genintroducerer fejlen.

**Hvis jeg skulle debugge den konkrete fejl, ville jeg især kigge efter forskellen mellem `paragraph-end` og `next-paragraph-start`.** Det er meget typisk, at mouse-hit-testeren returnerer en DOM-position, der visuelt ser rigtig ud, men som efter normalisering bliver fortolket som starten på det næste block-element. Return bruger derefter den forkerte paragraph og skaber derfor et ekstra afsnit eller splitter det forkerte sted.
