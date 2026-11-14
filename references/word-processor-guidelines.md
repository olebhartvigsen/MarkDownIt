# Caret- og afsnitsmodel

Referencen er bindende for begge MarkDownIts native builds, Win32 og macOS.
Positioner, paragraph-grænser og reglerne for Return, Backspace og Delete
gælder den delte model (`src/caret.h`, `src/editcontroller.cpp`), som de to
builds bruger sammen. Platformens tekstlayout leverer kun en kandidat:
DirectWrite på Windows, Core Text på macOS. Acceptkriterierne A-H er testlisten
på begge platforme.

## Implementeringsstatus

Planen er gennemgået mod begge native implementeringer, den Win32 og den macOS. Afsnit 19 om browser-API'er gælder ikke for MarkDownIt.

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

På macOS:

- Samme canoniske model og samme `EditController` som Win32. Klik, piletaster,
  Return, Backspace og Delete går gennem den, så kriterium G er den samme kode
  i de to builds.
- Core Text leverer kandidaten (`CTLineGetStringIndexForPosition` i
  `src/macos/text_engine_mac.cpp`). `ContentView::PlaceCaretAt` normaliserer
  den til den canoniske position, så hit-test ikke selv vælger afsnit eller
  celle (afsnit 20).
- Kriterierne A-H køres af den portable suite i macOS-jobbet sammen med resten
  af pakken og et sanitizer-build: `caret_test`, `hittest_test`,
  `navigation_test`, `table_interaction_test`, `editcontroller_test` og
  `textbuffer_test` med flere.
- QA-turen fotograferer caret-tilstandene `edit`, `end`, `tail`, `tail2`,
  `narrow` og `wide`, så caret og sideombrydning ses i begge builds.
- Uden automatisk dækning på macOS: visuel caret i RTL-løb, IME-komposition med
  en rigtig inputkilde og caret-placering omkring et billede uden tekst.

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

## Et klik, der åbner et dokument, er ikke et klik i dokumentet

En musesekvens begynder med et tryk og slutter med slip. Nogle tryk
besvarer vinduet selv: et kort på forsiden, der åbner det dokument det
peger på, og et modificeret klik på et link. De tryk placerer ingen caret,
og resten af sekvensen hører til trykket, ikke til det dokument der lige
blev åbnet.

Begge platforme bliver ved med at sende bevægelsen og slippet til samme
visning, efter at dokumentet er skiftet: Windows fordi `WM_MOUSEMOVE` med
venstre museknap nede stadig når frem, macOS fordi AppKit sender
`mouseDragged:` og `mouseUp:` til den visning der fik trykket. Uden en
spærring læser dokumentet den bevægelse som et træk og markerer fra
dokumentets begyndelse, så et håndled der flytter to punkter under klikket
efterlader en markering på den nyåbnede side.

Reglen ligger i `src/mousegesture.h` og bruges af begge skaller: trykket
kalder `OnPress()`, og når vinduet selv har besvaret det, `Consume()`.
Bevægelses- og slipbehandlerne spørger `Consumed()` og gør intet, og
slippet kalder `OnRelease()`. `tests/mousegesture_test.cpp` holder
tilstanden, og `tools/macos-smoke/launch_smoke.sh` trykker på et kort,
flytter seks punkter og kræver en tom markering.

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
| I mellemrummet mellem to linjer | linjen nedenunder      |

Mellemrummet mellem to linjebånd hører til linjen **nedenunder**. Reglen er
bindende for begge builds: en vertikal pilebevægelse eller et klik i
mellemrummet under en linje skal flytte caret'en videre ned, ikke blive
stående på linjen ovenover. Ellers ser Down ud som om den er død, og et klik
under en overskrift lander på den forkerte linje.

Reglen om mellemrummet er nødvendig, men ikke tilstrækkelig. Tre ting skal
også holde, og de er bindende for begge builds:

- **En vertikal bevægelse spørger layoutet om næste tegnede linje.** Den må
  ikke probe et punkt en linjehøjde væk. Sidste linje i en blok har luft under
  sig, og en probe der lander i den luft svarer med samme linje, så Down ser
  død ud ved hvert afsnitsslut. Spørg linjelisten i den rækkefølge siden tegner
  den, og hit-test midten af linjen `delta` pladser væk.
- **En caret-position uden geometri skal stadig svare.** En position mellem to
  blokke (den nye linje der afslutter en blok), på den tomme linje mellem to
  afsnit og ved dokumentets slutning hører ikke til nogen tegnet linje. Et
  geometrispørgsmål må ikke svare "ingen linje" for dem: svar med slutningen af
  den nærmeste linje ovenover, eller starten af den nærmeste nedenunder. Ellers
  fryser både pilene og scrollen, fordi begge spørger først.
- **Et bånd uden tekst springes over.** En spacer tegner ingen glyffer og har
  ingen tekstposition. Et trin skal gå videre til næste linje der har tekst,
  ikke give op.

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

- [ ] Backspace i et tomt punkt fjerner punktet som beskrevet nedenfor.
- [ ] Trippelklik og Backspace sletter punktets indhold, men bevarer punkt og linjeskift.
* [ ] Backspace ved paragraph-start merger paragraphs, undtagen et tomt punkt.
* [ ] Delete ved paragraph-end merger paragraphs.
* [ ] Backspace i tom paragraph.
* [ ] Delete i tom paragraph.
* [ ] Undo efter merge.
* [ ] Formatting efter merge bevares.

### Backspace i et tomt punkt

Når caret står i et punkt uden indhold, skal ét Backspace fjerne punktet.
Reglen gælder en aktiv punktopstilling med `-`, `*` eller `+` som markør.
Den gælder ikke en nummereret liste, kode eller bogstavelig tekst.
En aktiv markering følger de normale regler for sletning.

Trippelklik i et listepunkt skal kun markere indholdet. Markør, indrykning
og linjeskift skal bevares, når Backspace sletter denne markering.
Punktet bliver tomt. De næste punkter skal beholde deres tekst og niveau.
Et nyt Backspace uden markering fjerner så det tomme punkt.

Editoren skal fjerne markøren og punktets indrykning. Den skal beholde
linjeskiftet og lade caret stå i et tomt afsnit på samme linje.
Den må ikke samle linjen med det forrige punkt eller slette nabotekst.
Reglen gælder også i et indrykket punkt og ved dokumentets grænser.
Et tomt indrykket `-` under et punkt er et nyt punkt, ikke en overskrift.
Undo skal gendanne punktet og caret i én handling. Redo skal fjerne det igen.

Før og efter Backspace (`|` viser caret, ikke kildetekst):

```text
Før:
- Første punkt
- |
- Næste punkt

Efter:
- Første punkt
|
- Næste punkt
```

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
backspaceAtEmptyBullet()
backspaceAtEmptyIndentedBullet()
undoRedoBackspaceAtEmptyBullet()
tripleClickDeleteMiddleListItemText()
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

> Backspace i et tomt punkt skal fjerne markør og indrykning i én handling.
> Caret skal blive i et tomt afsnit på samme linje, også efter parse og layout.
> Linjeskift og nabotekst skal bevares. Undo og Redo skal gendanne begge tilstande.
> Tests skal dække `-`, `*` og `+`, indrykning, LF og CRLF samt dokumentets grænser.
> Kode, bogstavelig tekst og en aktiv markering skal følge deres egne regler.

> Trippelklik og Backspace i punkt 3 i en liste med fem punkter skal kun slette
> punktets indhold. Alle fem punkter og deres niveauer skal bevares efter parse.
> Tests skal dække LF, CRLF, fed, links samt Undo og Redo.

### G. Consistency

> Mouse click, arrow navigation, selection, Return, Backspace og Delete skal alle anvende samme canonical positionmodel.

### H. Regression

> Alle boundary cases skal være automatiserede tests, så ændringer i hit-testing eller DOM-rendering ikke genintroducerer fejlen.

**Hvis jeg skulle debugge den konkrete fejl, ville jeg især kigge efter forskellen mellem `paragraph-end` og `next-paragraph-start`.** Det er meget typisk, at mouse-hit-testeren returnerer en DOM-position, der visuelt ser rigtig ud, men som efter normalisering bliver fortolket som starten på det næste block-element. Return bruger derefter den forkerte paragraph og skaber derfor et ekstra afsnit eller splitter det forkerte sted.

---

## 17. Lodret navigation holder kolonnen

Up og Down flytter careten til den næste tegnede linje og beholder læserens kolonne. Tre regler følger af det:

- Kolonnen klemmes ind i den linje, flyttet lander på. En listes tekst starter inde i markørgutteren, og et citat starter længere inde end brødteksten. En kolonne til venstre for linjen er stadig en position på linjen, ikke et tegn på at der ikke er nogen linje. Uden klemmet sprang ét tryk på Down hele listen over.
- En position ved et blødt ombrud har to hjem: slutningen af den øverste linje og starten af den nederste. Efter en vandret flytning tegnes careten på den øverste, efter en lodret på den nederste, og flyttet skal starte fra netop den linje careten er tegnet på. Ellers lander det en linje forkert, og linjen imellem kan ikke nås med piletasterne.
- En kørsel som md4c selv bygger (et blødt linjeskift, et hårdt, et `<br>`, et NUL) har ingen bytes i kilden, men den har en plads: det byte teksten stopper ved. Mappes den til 0, falder careten til toppen af dokumentet, så snart den træder på linjeskiftet.

Tests: `RendererMac.VerticalMoveEntersTheListBelow`, `RendererMac.VerticalMoveStepsThroughANestedListOneLineAtATime`, `RendererMac.SoftBreakKeepsTheCaretInItsBlock` og `HitTest.SoftBreakMapsToItsNewline`.

## 18. En ombruet fortsættelseslinje hører til blokken

En listepunkt-tekst kan være brudt over flere kildelinjer. Markøren står kun på den første, og resten er fortsættelseslinjer. Reglen er, at alt der handler om blokkens struktur læses fra blokken, ikke fra den linje careten tilfældigvis står på.

- **Enter** fortsætter punktet med dets egen markør, også når careten står på en fortsættelseslinje, og i den indrykning punktet har. Markøren hentes fra punktets første linje, ikke fra den aktuelle linje, som ikke har nogen.
- **Et senere afsnit i punktet er stadig punktets tekst.** Enter i punktets andet eller tredje afsnit åbner et nyt punkt på punktets eget niveau, ikke et bart linjeskift. Kun en tom linje mellem afsnittene er en separator, og der indsættes et almindeligt linjeskift, så den næste linje ikke bliver slugt af et tomt punkt.
- **Et citat er en beholder som listen.** Enter i et citat indsætter citatets egne `> `-mærker, så den nye linje bliver i citatet, og et nested citat beholder sin dybde (`> > `). Står careten på citatets sidste linje, og den ikke bærer andet end mærkerne, afslutter Enter citatet i stedet: `> ` fjernes, og careten står på en tom linje uden for citatet. En liste inde i et citat fortsætter som `> - `.
- **Et task-punkt beholder sin afkrydsningsboks.** Enter på `- [ ] opgave` giver et nyt `- [ ] `, og Enter på `- [x] færdig` giver et nyt, ukrydset `- [ ] `. Afkrydsningsfeltet er en del af markøren, ikke af teksten.
- **Markøren er en strukturel præfiks, og Backspace ved punktets indholdsstart fjerner hele markøren** i én undo-post: indrykning, punkttegn eller tal og et eventuelt afkrydsningsfelt. Det gælder både punktopdelte og nummererede lister. Fjernes kun ét tegn, står `1.` tilbage som ren tekst, linjen er ikke længere et punkt, og nummereringen af de følgende punkter ændrer sig, uden at læseren har bedt om det. Citatets `> ` bliver stående, så punktet forsvinder men citatet består. Inde i en kodeblok, en tabel og en tematisk linje er det samme tekst bogstavelig, og der gælder den almindelige tegnesletning.
- **Indrykning** flytter hele punktet, altså markørlinjen og fortsættelseslinjerne sammen, i én splice og dermed én undo-post. Flyttes kun den linje careten står på, mister punktet sin fortsættelse, og to ekstra mellemrum gør teksten til en kodeblok under punktet. Uden for lister gælder den almindelige linjeindrykning, som specifikationen foreskriver. Står punktet allerede på venstre margen, er Shift+Tab en no-op: der er ingen niveau at forlade, og fjernes fortsættelseslinjernes indrykning, falder de ud af punktet.
- **Blok-toggles** (punktopdelt liste, nummereret liste, citat) sætter mærket på afsnittets første linje og lader careten blive, hvor læseren satte den. Sættes mærket på den fysiske linje under careten, splittes afsnittet i to.
- **Kodeblok** lægger hegnet om hele afsnittet, ikke om den linje careten står på, så et ombrudt afsnit ikke bliver delt med tekst uden for blokken. Står careten på åbningshegnet, fjerner samme kommando hele blokken igen.
- **Formatering af tekst** (fed, kursiv, kode, gennemstregning) arbejder på markeringen og er derfor upåvirket af, hvor linjen er brudt.

Tests: `InsertParagraphBreak.WrappedItemContinuesItsOwnMarker`, `InsertParagraphBreak.WrappedOrderedItemKeepsItsNumbering`, `InsertParagraphBreak.WrappedNestedItemContinuesAtItsOwnLevel`, `InsertParagraphBreak.BlankLineInsideAnItemAddsNoMarker`, `InsertParagraphBreak.ASecondParagraphOpensANewItem`, `InsertParagraphBreak.QuoteLineContinuesTheQuote`, `InsertParagraphBreak.NestedQuoteKeepsItsDepth`, `InsertParagraphBreak.TheLastQuoteLineEndsTheQuote`, `InsertParagraphBreak.TaskItemContinuesUnchecked`, `InsertParagraphBreak.QuotedItemKeepsTheQuoteAndItsMarker`, `ListMarkerBackspace.RemovesTheMarkerOfANonEmptyItem`, `EmptyBulletBackspace.RemovesMarkerAndIndentationInOneUndoStep`, `Formatting.IndentListMovesTheWholeItem`, `Formatting.OutdentListMovesTheWholeItem`, `Formatting.OutdentListAtTheLeftMarginLeavesTheContinuation`, `Formatting.IndentPlainParagraphStaysOnItsLine`, `Formatting.ToggleUnorderedListMarksTheParagraphStart`, `Formatting.ToggleBlockquoteMarksTheParagraphStart`, `Formatting.ToggleCodeBlockFencesTheWholeParagraph` og `Formatting.ToggleCodeBlockClosesTheBlockFromItsOpeningFence`.
