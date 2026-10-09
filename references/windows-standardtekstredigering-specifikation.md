# Funktionsspecifikation for standardtekstredigering på Windows

Dokumentet beskriver MarkDownIts tekstredigering på Windows. Det skelner mellem
Windows-konventioner, projektets egne krav og krav, der endnu mangler testbevis.

## Formål

Dokumentet fastlægger krav til synlig tekst i MarkDownIt på Windows.
Det beskriver også redigering. Kravene gælder dokumentets tekstområde.
De gælder både rå Markdown og en model med interne markører.
Brugeren skal møde samme regler i begge tilfælde.

Editoren skal følge Windows-konventioner for de handlinger, der beskrives her.
Projektets egne valg er angivet særskilt. Andre funktioner må ikke ændre
resultatet af de beskrevne klik, taster eller genveje.

## Omfang og afgrænsning

Specifikationen omfatter:

- Indsættelsespunkt, kaldet caret i dette dokument.
- Sammenhængende tekstmarkering.
- Mus, tastatur, clipboard, sletning og navigation.
- Fortryd og gentag.
- Visuel og logisk scroll.
- Grundlæggende anvendelse af fed, kursiv og links.
- Unicode, herunder sammensatte tegn og emoji.
- Tekstflytning, dokumentbevidst Tab og Enter samt linkaktivering.
- Søgning og erstatning.

Specifikationen omfatter ikke:

- Stavekontrol.
- Flere samtidige markeringer.
- Rektangulær eller kolonnebaseret markering.
- Automatisk fortolkning af skrevet Markdown-syntaks.

Almindeligt input må ikke omskrive eller formatere Markdown-markører.
Skrevet `# `, `- ` og `**` skal forblive tekst. Formatering kræver en særskilt
kommando fra brugeren. Programmet må stadig vise eksisterende Markdown med
format. Det er ikke automatisk formatering af nyt input.

Nye bogstavelige tegn skal forblive bogstavelige efter parse, relayout og
gemning samt ved genåbning af den gemte fil. Kilden skal escape tegnene eller
bruge en tilsvarende varig repræsentation. Brugeren skal stadig se sine tegn.
Reglen gælder også paste og erstatning. Den må ikke omfortolke Markdown, som
allerede fandtes i den åbnede fil.

Tab, Enter og klik i specialiserede dokumentelementer følger reglerne i
afsnittet "Udvidede krav: dokumentbevidst redigering og søgning".

## Kilder og kravprioritet

Windows har flere slags tekstfelter med forskellig adfærd. Dokumentet kræver
ikke, at MarkDownIt kopierer alle funktioner i Rich Edit eller Word.
Standardgenveje og clipboardformat følger [Rich Edit-genveje][rich-edit] og
[Windows Clipboard][clipboard-formats]. Grafemregler følger
[Unicode Text Segmentation][unicode-segmentation]. De er et projektkrav,
ikke en påstand om, at alle Windows-tekstfelter sletter hele grafemklynger.

Følgende lokale referencer gælder sammen med dette dokument:

- [Caret- og afsnitsmodellen](word-processor-guidelines.md) styrer strukturelle
  positioner. Acceptkriterium D styrer Enter ved afsnitsslut. Det har forrang
  for det modstridende eksempel i referencens afsnit 5.
- [Tabelmodellen](word-processor-table-guidelines.md) styrer celler og deres
  grænser. Dens noter for MarkDownIt afgrænser den generelle ide-samling: Enter
  og Shift+Enter i celler er no-op. Tab i sidste celle opretter en række.
- [Søgning og erstatning](find-replace.md) styrer søgefelt, match og erstatning.
  MarkDownIt bruger et fast søgebånd, ikke en modal dialog.

De særlige regler for tabeller og søgning har forrang for basisreglerne her.
Implementeringsstatus er ikke et krav og må ikke lempe et krav. Nye konflikter
skal afklares i referencerne, før kode ændres. Kilderne står til sidst.

## Tilstand og kommandoer

Navigation, markering, kopiering, scroll, søgning og linkaktivering læser
dokumentet. De skal virke uden at slå redigering til. De må ændre fokus, caret,
markering eller visning efter de regler, der gælder for handlingen. De må ikke
ændre dokumenttekst, formatering eller fortrydelseshistorik.

Indtastning, sletning, klip, indsætning, formatering, erstatning, tekstflytning,
fortryd og gentag kræver en skrivbar redigeringstilstand. I skrivebeskyttet
tilstand skal de være no-op. En genvej må ikke selv slå redigering til.
Søgebåndets erstatningsknapper skal være inaktive i den tilstand.

Genveje gælder det felt, der har fokus. Ctrl+A i søgefeltet markerer feltets
tekst, ikke dokumentet. Tab og Shift+Tab i en dialog eller et søgebånd flytter
fokus mellem kontroller. Kun dokumentets tekstområde bruger dokumentreglerne. En
genvej må ikke samtidig udløse en kommando og indsætte et kontroltegn.

## Normative ord

- "Skal" angiver et ufravigeligt krav.
- "Må ikke" angiver en forbudt adfærd.
- "Kan" angiver en tilladt implementeringsdetalje, hvis den ikke ændrer
  observerbar adfærd.

## Grundlæggende model

### Tekstpositioner

1. Dokumentet skal have en lineær rækkefølge af redigerbare tekstpositioner.
2. En position ligger mellem to grafemklynger eller ved starten eller slutningen
   af dokumentet.
3. Et indtastet tegn eller indsat tekst erstatter en ikke-tom markering. Klip
   sletter markeringen efter kopiering og indsætter ikke tekst.
4. En tom markering repræsenterer et caret ved én tekstposition.
5. En ikke-tom markering repræsenterer intervallet fra markeringens begyndelse,
   inklusive begyndelsen, til dens slutning, eksklusive slutningen.
6. Intern Markdown-syntaks, skjulte formatmarkører og andre ikke-visuelle lag må
   ikke kunne vælges eller placere caretet mellem deres enkelte tegn.

Den lineære rækkefølge er en logisk orden, ikke en byteadresse. Alle inputveje
skal bruge én kanonisk positionmodel for afsnit, celler og grafemer. En tom blok
skal have en gyldig position. Afsnitsslut og næste afsnits start er forskellige
strukturelle positioner, også når de tegnes tæt på hinanden.

Hit-test må kun levere en kandidat i den ramte blok eller celle. Modellen skal
normalisere kandidaten, før en kommando bruger den. UTF-8-offsets i kilden og
UTF-16-offsets i DirectWrite skal oversættes eksplicit til og fra modellen.
Skjulte markører må ikke blive en genvej til en anden blok.

Ved ombrydning og skift mellem LTR- og RTL-løb kan én logisk position have
flere visuelle placeringer. Caretets visuelle side skal bevares som layoutdata.
Den må ikke ændre markeringens logiske interval. Se
[DirectWrite-hit-test][hit-test] og [caret i tovejs-tekst][bidi-caret].

### Grafemklynger og Unicode

1. Alle bevægelser, sletninger og markeringer på tegnniveau skal bruge
   grafemklynger, ikke UTF-8-bytes, UTF-16-kodeenheder eller
   Unicode-kodepunkter.
2. En grafemklynge kan bestå af flere kodepunkter, eksempelvis `a` efterfulgt af
   en kombinerende accent, et emoji med hudtone eller en emoji-ZWJ-sekvens.
3. Backspace, Delete, venstre pil og højre pil må aldrig efterlade en ugyldig
   UTF-sekvens eller dele en synlig grafemklynge.
4. Indsat tekst skal accepteres som Unicode. Ugyldige sekvenser fra en ekstern
   kilde skal afvises eller erstattes konsekvent med U+FFFD, før de når
   dokumentmodellen.
5. Windows IME- og emoji-input skal behandles som tekstinput. Midlertidig
   IME-komposition må ikke oprettes som en fortrydelseshændelse, før teksten er
   bekræftet.

Implementeringen skal fastlåse og oplyse den anvendte Unicode-version. Grafemer
skal følge UAX #29's udvidede grafemklynger. Ordgrænser skal bruge samme
Unicode-baserede segmentering i mus, tastatur og Whole word. Eventuelle
tilpasninger skal beskrives og have testeksempler. IME-bekræftelse skal være én
handling. Annulleret komposition skal bevare tekst, formatering, markering og
historik fra før kompositionen.

### Markeringens anchor og active end

1. Markeringen skal lagre både et anchor og en active end.
2. Anchor er den position, hvor en markering startede.
3. Active end er den position, som senest blev flyttet ved mus eller tastatur.
4. Den læste tekst og tegnede markering bestemmes af den sorterede begyndelse og
   slutning, også når active end ligger før anchor.
5. En udvidende handling med Shift skal beholde anchor og kun flytte active end.
6. En ikke-udvidende handling skal normalt kollapse markeringen, så anchor og
   active end får samme position.

## Fokus og caret

### Fokus

1. Et venstreklik i et redigerbart tekstområde skal give tekstområdet
   tastaturfokus.
2. Når tekstområdet har fokus og markeringen er tom, skal et synligt blinkende
   caret vises på den aktive position.
3. Caretet skal bruge systemets blinkindstilling og systemets caret-bredde, hvor
   Windows stiller disse til rådighed.
4. Når tekstområdet mister fokus, skal caretet skjules.
5. Når tekstområdet igen får fokus, skal caretet vises på den aktive position,
   hvis markeringen er tom.
6. En ikke-tom markering skal blive synlig, når området mister fokus, men må
   bruge systemets inaktive markeringsfarve.

### Caretets placering

1. Caretet skal tegnes ved den visuelle indstiksposition, som svarer til den
   aktive tekstposition.
2. Caretets højde skal svare til den aktuelle tekstlinjes højde.
3. Caretet skal flytte med indholdet ved scroll, ændret vinduesstørrelse,
   skrifttypeændring og ombrydning.
4. Efter en handling, der flytter caretet, skal programmet scrolle mindst
   muligt, så caretet er fuldt synligt lodret og vandret.
5. Caretet må ikke blinke oven på en ikke-tom markering.

## Museadfærd

### Enkeltklik

1. Et venstreklik på en synlig tekstposition skal kollapse en eksisterende
   markering ved den hit-testede position.
2. Et venstreklik mellem to tegn skal placere caretet ved den nærmeste gyldige
   indstiksposition. Valget før eller efter skal følge layoutets
   leading/trailing-side, ikke en antagelse om tekstens retning. I enkel
   LTR-tekst svarer det til tegnets vandrette halvdel.
3. Et klik i den tomme højre del af en LTR-linje skal placere caretet ved
   linjens slutning. For RTL-linjer bruges layoutets retning og nærmeste gyldige
   side.
4. Et klik i et afsnits tomme område under sidste linje skal placere caretet ved
   samme afsnits slutning. Klik i en celle skal blive i cellen. Mellem blokke
   vælges den nærmeste blok efter layoutets blokgrænser; ved lige afstand vælges
   den foregående. Hit-test må ikke vælge dokumentets nærmeste tegn uden først
   at vælge blok eller celle.
5. Shift+venstreklik skal bevare den eksisterende anchor og flytte active end
   til den hit-testede position.
6. Hvis der ikke fandtes en markering før Shift+venstreklik, skal den daværende
   caret-position bruges som anchor.
7. Ctrl+venstreklik uden for et link følger almindeligt venstreklik. På et link
   gælder afsnittet "Linkaktivering med musen". Tryk i en eksisterende markering
   følger først reglerne for tekstflytning.

### Trækmarkering

1. Et venstreklik efterfulgt af bevægelse med venstre museknap nede skal starte
   en markering med klikpositionen som anchor.
2. Under træk skal active end kontinuerligt følge den hit-testede museposition.
3. Markeringen skal opdateres, også når active end passerer anchor og
   markeringen skifter retning.
4. Trækmarkering skal kunne krydse afsnit og visuelle linjeskift.
5. Trækmarkering skal afsluttes, når venstre museknap slippes, også hvis
   musemarkøren er uden for tekstområdets klientrektangel.
6. Tekstområdet skal tage musecapture under en aktiv trækmarkering og frigive
   capture ved afslutning eller afbrudt capture.
7. Når brugeren trækker over den øverste eller nederste kant af klientområdet,
   skal visningen autoscrolle i den pågældende retning og fortsat opdatere
   active end.
8. Autoscrollens hastighed skal stige med afstanden uden for kanten og have en
   fast øvre grænse, så brugeren kan styre den.
9. Autoscroll skal stoppe ved dokumentets begyndelse eller slutning, når
   museknappen slippes, når capture mistes, eller når markøren vender tilbage
   til klientområdet.

### Dobbeltklik

1. Et dobbeltklik på tekst skal markere ordet under klikpunktet.
2. Et ord er en maksimal sammenhængende sekvens af bogstaver, tal eller tegn,
   som tekstsegmentering klassificerer som del af et ord.
3. Tegnsætning og symboler, der ikke er del af et ord, skal kunne markeres som
   deres egen sammenhængende enhed ved dobbeltklik.
4. Hvidt mellemrum mellem ord skal ikke medtages i en dobbeltklikmarkering.
5. Dobbeltklik på en tom linje skal ikke markere nabotekst. Caretet skal
   placeres på linjen.
6. Efter dobbeltklik skal træk med venstre museknap udvide eller formindske
   markeringen i hele ordgrænser, ikke tegn for tegn.

### Trippelklik

1. Et trippelklik på tekst skal markere det komplette afsnit, som klikpunktet
   ligger i.
2. Afsnittets afsluttende linjeskift skal medtages, hvis et sådant findes. Det
   gør det muligt at erstatte eller flytte afsnittet uden at efterlade et tomt
   afsnit. Et listepunkt er en undtagelse: trippelklik markerer kun indholdet.
   Listemarkør, indrykning og punktets afsluttende linjeskift må ikke medtages.
   Backspace på denne markering skal efterlade et tomt punkt. De næste punkter
   skal beholde deres tekst og niveau. Det gælder også formateret indhold.
3. Det sidste afsnit i dokumentet skal markeres uden at læse forbi dokumentets
   slutning.
4. Et afsnit uden synlig tekst skal markeres som sin tomme redigerbare position
   plus eventuelt afsluttende afsnitsskift.
5. Efter trippelklik skal træk med venstre museknap udvide eller formindske
   markeringen i hele afsnit.
6. Windows leverer ikke en separat besked for trippelklik. Implementeringen skal
   derfor tælle klik på samme måde som Windows-dobbeltklik: inden for systemets
   dobbeltkliktid og dobbeltklikrektangel.
7. Et klik uden for tids- eller afstandstærsklen skal starte en ny kliksekvens
   og må ikke udløse afsnitsmarkering.

## Tastaturinput og erstatning

### Almindeligt tekstinput

1. Bekræftet tekstinput fra tastatur, IME, emoji-panel eller clipboard skal
   indsættes på den aktive position.
2. Hvis markeringen ikke er tom, skal hele markeringen erstattes af den
   indgående tekst i én mutation.
3. Efter indsættelse skal markeringen være tom, og caretet skal stå umiddelbart
   efter den indsatte tekst.
4. Indsættelse af tekst må ikke fortolke almindelige Markdown-tegn som
   formatkommandoer. Et indtastet `*`, `_`, `[`, `]` eller andet tegn skal
   forblive synlig tekst, medmindre brugeren eksplicit anvender en
   formatkommando.
5. Kontroltegn, der ikke repræsenterer accepteret tekstinput, må ikke indsættes
   i dokumentet.
6. Et linjeskift indsat fra en ekstern tekstkilde skal normaliseres til
   dokumentets interne afsnitsrepræsentation uden at ændre den viste rækkefølge
   af linjer.

### Enter

1. Enter uden en markering skal indsætte ét afsnitsskift ved caret-positionen.
2. Enter med en markering skal erstatte markeringen med ét afsnitsskift.
3. Efter Enter skal caretet stå i det nye afsnit efter det indsatte
   afsnitsskift.
4. Shift+Enter skal indsætte et synligt hårdt linjeskift i samme almindelige
   afsnit. Det må ikke oprette et nyt afsnit. I en tabelcelle er det en no-op.
   Ctrl+Enter og Alt+Enter har ingen påkrævet betydning her.
5. Enter ved et afsnits slutning skal oprette et nyt tomt afsnit lige efter det
   aktuelle. Caretet skal stå i det nye afsnit, ikke i et afsnit, der allerede
   fandtes. Enter ved en ombrudt linjes slutning splitter ved den logiske
   position, ikke ved næste blok.

### Backspace og Delete

1. Backspace med en ikke-tom markering skal slette hele markeringen.
2. Delete med en ikke-tom markering skal slette hele markeringen.
3. Backspace med en tom markering skal slette grafemklyngen umiddelbart før
   caretet.
4. Delete med en tom markering skal slette grafemklyngen umiddelbart efter
   caretet.
5. Backspace ved dokumentets begyndelse skal være en no-op, bortset fra
   fjernelse af et tomt punkt efter den særlige dokumentregel nedenfor.
6. Delete ved dokumentets slutning skal være en no-op.
7. Hvis en sletning fjerner et afsnitsskift, skal de to tilstødende afsnit blive
   ét afsnit. Caretet skal stå på den oprindelige skilleposition.
8. Ctrl+Backspace med tom markering skal slette til forrige fælles ordgrænse.
   Mellemrum og tegnsætning på vejen indgår. Med markering slettes kun
   markeringen.
9. Ctrl+Delete med tom markering skal slette til næste fælles ordgrænse.
   Mellemrum og tegnsætning på vejen indgår. Med markering slettes kun
   markeringen.
10. Ctrl+Backspace og Ctrl+Delete må ikke krydse dokumentets begyndelse eller
    slutning.
11. Shift+Delete uden Ctrl er klip, ikke almindelig sletning. Genvejen følger
    alle regler for Ctrl+X, også ved tom markering og clipboard-fejl.
    Shift+Backspace følger her Backspace. Denne sidste regel er et projektvalg,
    ikke et krav om at kopiere alle Rich Edit-genveje for tovejs-input.

## Tastaturnavigation

### Fælles regler for navigation

1. Navigationstaster skal flytte caretet, når markeringen er tom.
2. Navigation med Shift skal udvide eller formindske markeringen fra anchor.
3. Venstre og Højre uden Shift fra en ikke-tom markering skal kun kollapse
   markeringen i pilens retning. Der må ikke ske en ekstra tegnbevægelse. I
   LTR-tekst er det henholdsvis begyndelsen og slutningen.
4. Ctrl+Venstre og Ctrl+Højre følger her samme kollapsregel. Op og Ned uden
   Shift kollapser her til henholdsvis begyndelsen og slutningen. Det er
   projektets afgrænsede regel, ikke en universel Windows-regel.
5. Home, End, Ctrl+Home, Ctrl+End, Page Up og Page Down skal nå deres angivne
   mål i ét tastetryk, også med en aktiv markering. Uden Shift bliver
   markeringen tom ved målet. Home bruger markeringens første visuelle linje,
   End dens sidste. Med Shift bruges active ends linje, og anchor bevares. Se
   [Windows Home/End][home-key].
6. I tovejs-tekst skal pile følge visuel retning. Logisk begyndelse må ikke
   antages at ligge længst til venstre. Ordbevægelse skal følge tekstløbets
   retning og de fælles ordgrænser.
7. Navigation ved en grænse må ikke skabe en ugyldig position. Shift bevarer
   anchor. En ikke-tom markering kan stadig kollapse ved en gyldig grænse, selv
   om målet ligger ved dokumentets begyndelse eller slutning.
8. Efter vandret navigation eller redigering skal den gemte ønskede vandrette
   position for lodret navigation nulstilles.

### Venstre og højre pil

1. Venstre pil med tom markering skal flytte til næste gyldige grafemposition
   mod venstre i layoutet.
2. Højre pil med tom markering skal flytte til næste gyldige grafemposition mod
   højre i layoutet.
3. Ctrl+Venstre skal flytte til begyndelsen af det foregående ord eller den
   foregående ordgrænse.
4. Ctrl+Højre skal flytte til begyndelsen af det næste ord eller den næste
   ordgrænse.
5. Shift+Venstre og Shift+Højre skal udføre samme bevægelse og beholde anchor.
6. Ctrl+Shift+Venstre og Ctrl+Shift+Højre skal udføre ordbevægelse og beholde
   anchor.
7. Ordnavigation skal bruge samme segmenteringsregler som dobbeltklik, så mus og
   tastatur har identiske ordgrænser.

### Op og ned pil

1. Op og Ned skal bevæge sig mellem visuelle, ombrudte tekstlinjer, ikke kun
   mellem logiske afsnitslinjer.
2. Lodret navigation skal forsøge at bevare brugerens ønskede vandrette
   position.
3. På en kortere visuel linje placeres caretet ved linjens slutning. Den
   oprindeligt ønskede vandrette position skal stadig huskes.
4. Hvis brugeren derefter flytter til en længere linje, skal caretet vende
   tilbage til den oprindelige ønskede position.
5. Shift+Op og Shift+Ned skal følge de samme lodrette regler og udvide
   markeringen.
6. Lodret navigation skal scrolle visningen mindst muligt, når målpositionen
   ligger uden for klientområdet.

### Home og End

1. Home skal flytte til den visuelle linjes logiske begyndelse. Med en markering
   vælges linjen efter de fælles navigationsregler.
2. End skal flytte til den visuelle linjes logiske slutning. Med en markering
   vælges linjen efter de fælles navigationsregler.
3. Ctrl+Home skal flytte til dokumentets første redigerbare position.
4. Ctrl+End skal flytte til dokumentets sidste redigerbare position.
5. Shift+Home, Shift+End, Ctrl+Shift+Home og Ctrl+Shift+End skal udføre den
   tilsvarende bevægelse og beholde anchor.

### Page Up og Page Down

1. Page Up skal flytte caretet omtrent én synlig klienthøjde op.
2. Page Down skal flytte caretet omtrent én synlig klienthøjde ned.
3. Målpositionen skal bestemmes ud fra den ønskede vandrette position på den
   nærmeste visuelle tekstlinje efter scrollen.
4. Visningen skal scrolle sammen med caretet.
5. Shift+Page Up og Shift+Page Down skal udføre den tilsvarende bevægelse og
   beholde anchor.

### Markér alt

1. Ctrl+A uden for en tabel skal markere hele dokumentets redigerbare tekst.
   Inde i en tabel følger gentagne Ctrl+A tabelreferencens trin: celle, række,
   tabel og dokument. En anden dokumenthandling nulstiller trinnene.
2. Ved dokumentomfang skal anchor være dokumentets begyndelse og active end dets
   slutning. Ved et tabeltrin bruges det pågældende områdes grænser.
3. Ctrl+A må ikke medtage skjulte Markdown-markører, hvis disse ikke er en del
   af den synlige tekstmodel.

### Påkrævede standardgenveje

| Genvej | Handling |
| --- | --- |
| Ctrl+A | Markér al redigerbar tekst. |
| Ctrl+B | Slå fed til eller fra, eller anvend eller fjern fed på markeringen. |
| Ctrl+C og Ctrl+Insert | Kopiér markeringen. |
| Ctrl+I | Slå kursiv til eller fra, eller anvend eller fjern kursiv på markeringen. |
| Ctrl+K | Opret eller redigér link for en markering. |
| Ctrl+V og Shift+Insert | Indsæt tekst fra clipboard. |
| Ctrl+X og Shift+Delete | Klip markeringen. |
| Ctrl+Y | Gentag den senest fortrudte handling. |
| Ctrl+Z | Fortryd seneste handling. |
| Ctrl+Venstre og Ctrl+Højre | Navigér efter ordgrænse. |
| Ctrl+Backspace og Ctrl+Delete | Slet til forrige eller næste ordgrænse. |
| Home og End | Navigér til starten eller slutningen af den visuelle linje. |
| Ctrl+Home og Ctrl+End | Navigér til dokumentets begyndelse eller slutning. |
| Shift plus enhver påkrævet navigationstast | Udvid eller formindsk markeringen. |

1. Hver aliasgenvej i tabellen skal udføre nøjagtig samme handling som den
   tilsvarende primære genvej.
2. Ctrl+Shift+Z kan understøttes som et ekstra alias for gentag, men må ikke
   erstatte Ctrl+Y.
3. Ctrl+U er ikke påkrævet, fordi understregning ligger uden for denne
   specifikations formateringsomfang. En senere specifikation, der tilføjer
   understregning, skal tildele Ctrl+U til denne kommando.

## Clipboard

### Kopiér

1. Ctrl+C skal kopiere den aktuelle markering til Windows Clipboard, hvis
   markeringen ikke er tom.
2. En kopioperation må ikke ændre dokumentet, markeringen eller
   caret-positionen.
3. Ctrl+C med en tom markering skal være en no-op og må ikke rydde eller
   overskrive det eksisterende clipboard-indhold.
4. Programmet skal mindst lægge markeringens tekst på clipboard som
   `CF_UNICODETEXT`.
5. Tekst i `CF_UNICODETEXT` skal være NUL-termineret og bruge CRLF som
   linjeskift på clipboard, hvor markeringen indeholder afsnitsskift.
6. Tekst uden visuelle formatmarkører skal kopieres som den tekst, brugeren har
   markeret. Skjulte interne Markdown-markører må ikke medtages.

### Klip

1. Ctrl+X skal kopiere og derefter slette den aktuelle markering som én
   brugerhandling.
2. Ctrl+X med en tom markering skal være en no-op og må ikke ændre clipboard
   eller dokument.
3. Klip i et skrivebeskyttet dokument skal være en no-op. Det må ikke ændre
   clipboard.
4. Hvis clipboard ikke kan åbnes eller tekst ikke kan placeres på clipboard, må
   dokumentet ikke slettes.
5. Et vellykket klip skal kunne fortrydes som én fortrydelseshændelse.
   Fortrydelse skal gendanne både tekst og den oprindelige markering.

### Indsæt

1. Ctrl+V skal indsætte ren Unicode-tekst fra Windows Clipboard.
   `CF_UNICODETEXT` har forrang i denne funktion. Eksternt indhold følger
   altid reglen "Ekstern indsætning er kun tekst" nedenfor.
2. Denne basisfunktion skal mindst understøtte `CF_UNICODETEXT`.
3. Hvis clipboard ikke har en understøttet tekstrepræsentation, skal Ctrl+V være
   en no-op.
4. Indsætning skal erstatte en ikke-tom markering i én mutation.
5. Efter indsætning skal caretet stå efter den indsatte tekst, og markeringen
   skal være tom.
6. Clipboard-tekst med CRLF, CR eller LF skal normaliseres konsekvent til
   dokumentets interne linjeskiftsrepræsentation.
7. Indsætning må ikke fortolke Markdown-tegn eller automatisk oprette links.
   Teksten kan arve et eksplicit valgt skriveformat, men må ikke få formatering
   alene ud fra tegnene. I en tabelcelle gælder cellens strukturelle validering.
8. En vellykket indsætning skal være én fortrydelseshændelse, uanset tekstens
   længde eller antal afsnit.

### Ekstern indsætning er kun tekst

Alt indhold kopieret fra et andet program skal indsættes som ren tekst.
Det gælder Ctrl+V, Shift+Insert, menu, kontekstmenu og enhver anden paste-vej.
Reglen har forrang for forslag om rig paste i tabelreferencen. Den gælder også
indhold fra Word, Excel, en browser og andre tekstredigeringsprogrammer.

Programmet må kun bruge clipboardets rene tekstrepræsentation. RTF, HTML,
Word-formater, typografier og indlejrede objekter må ikke importeres via paste.
Hvis både tekst og rige formater findes, bruges kun teksten. Hvis der kun
findes rige formater eller objekter, er paste en no-op. Programmet må ikke
falde tilbage til at læse eller fortolke RTF eller HTML.

Ekstern tekst må ikke medbringe skrifttype, størrelse, farve, fed, kursiv,
linkdestination, listeformat eller tabelstruktur. Den kan bruge dokumentets
skriveformat efter de almindelige regler, men aldrig kildens format. Synlige
URL'er og Markdown-tegn forbliver bogstavelig tekst. Tabs og linjeskift er
tekst, ikke en ordre om at oprette en liste eller tabel. I en celle gælder
stadig cellens regler for sikre tegn og linjeskift.

Ukendt oprindelse skal behandles som ekstern. Et formatnavn på clipboardet
er ikke i sig selv bevis for, at indholdet kommer fra MarkDownIt. Reglen
ændrer ikke intern tekstflytning i dokumentet eller særskilt filimport.

## Fortryd og gentag

### Grundregler

1. Ctrl+Z skal fortryde den seneste fortrydelsesberettigede brugerhandling.
2. Ctrl+Y skal gentage den senest fortrudte brugerhandling.
3. Hvis der ikke er noget at fortryde eller gentage, skal den pågældende genvej
   være en no-op.
4. Ctrl+Shift+Z kan understøttes som et ekstra alias for gentag, men Ctrl+Y er
   påkrævet.
5. Hver fortrydelsespost skal mindst kunne gendanne den erstattede tekst, den
   indsatte tekst, markeringen før handlingen og markeringen efter handlingen.
6. Fortrydelse og gentag må ikke afhænge af, at layout, ombrydning eller
   vinduesbredde er uændret.

### Hændelsesgrænser

1. Indsætning fra clipboard, klip, sletning af en markering, Enter og anvendelse
   eller fjernelse af formatering skal hver være én separat
   fortrydelseshændelse.
2. En sammenhængende sekvens af almindelig tastaturindtastning kan samles til én
   fortrydelseshændelse.
3. Input må kun samles, når nye tegn følger direkte efter de foregående.
   Navigation eller ændret markering afslutter sekvensen. Det gør paste,
   formatering, skift af fokus og enhver anden mutation også.
4. Et afsnitsskift skal altid afslutte en påbegyndt sammenlægning.
5. En sletningssekvens kan samles, når alle sletninger har samme retning og er
   sammenhængende. Backspace og Delete må ikke samles i samme post.
6. Caret-bevægelse, museklik, markering, Ctrl+V, Ctrl+X, Ctrl+Z, Ctrl+Y og
   formatkommandoer skal afslutte en påbegyndt sammenlægning.
7. En ny redigering efter én eller flere fortrydelser skal rydde
   gentagelseshistorikken.
8. En no-op må ikke oprette en fortrydelsespost.

### Tilstand efter fortryd og gentag

1. Fortrydelse skal gendanne den præcise markering, der fandtes før den
   fortrudte handling.
2. Gentag skal gendanne den præcise markering, der fandtes efter den gentagne
   handling.
3. Efter fortryd eller gentag skal visningen opdateres, og den aktive ende af
   markeringen skal gøres synlig.
4. Formatering, som fortrydes eller gentages, skal gendannes uden at ændre den
   underliggende synlige tekst.

## Grundlæggende inlineformatering

### Fælles regler

1. Følgende kommandoer er omfattet: fed, kursiv og link.
2. Kommandoerne skal kunne udløses fra brugergrænsefladen og via
   standardgenvejene Ctrl+B, Ctrl+I og Ctrl+K.
3. Den samme kommando skal have samme observerbare resultat, uanset om den
   kommer fra tastatur, menu eller værktøjslinje.
4. Formatering skal anvendes på den synlige tekstmodel. Implementeringen kan
   ændre intern Markdown, men markering, caret og den viste tekst må følge
   reglerne her.
5. En formatkommando på en ikke-tom markering skal være én fortrydelseshændelse.
6. En formatkommando må ikke flytte markeringens begyndelse eller slutning på
   den synlige tekst.
7. Når en formatkommando afsluttes, skal den oprindelige synlige tekst stadig
   være markeret.
8. Delvis overlap med eksisterende formatløb skal splitte løbene ved
   markeringsgrænserne. Tekst uden for markeringen må ikke miste eller få
   formatering.
9. Flere inlineformater kan eksistere på samme tekst. Fed og kursiv skal derfor
   kunne kombineres i vilkårlig rækkefølge.

### Fed og kursiv på en markering

1. Ctrl+B eller kommandoen Fed skal undersøge hele den ikke-tomme markering.
2. Hvis hver eneste tegnposition i markeringen allerede er fed, skal kommandoen
   fjerne fed fra hele markeringen.
3. Hvis mindst én tegnposition i markeringen ikke er fed, skal kommandoen
   anvende fed på hele markeringen.
4. Ctrl+I eller kommandoen Kursiv skal følge de samme regler for kursiv.
5. En markeringsgrænse midt i et eksisterende fedt eller kursivt løb skal bevare
   formateringen uden for markeringen.
6. Fed og kursiv skal ændre udseendet straks efter kommandoen uden at ændre
   tekstens rækkefølge eller den aktive markering.

### Fed og kursiv ved et tomt caret

1. Ctrl+B eller Ctrl+I ved et tomt caret skal slå den pågældende egenskab for
   efterfølgende indtastning til eller fra.
2. Kommandoen må ikke indsætte tomme formatløb, synlige symboler eller skjult
   tekst i dokumentet.
3. Det valgte format gælder for nye tegn ved caretet. Det ophører, når brugeren
   slår det fra eller flytter caretet til tekst med et entydigt andet format. En
   ny eksplicit formatkommando erstatter også valget.
4. Backspace over nyligt indtastet formateret tekst skal slette tekst efter de
   almindelige sletteregler. Den må ikke efterlade et tomt formateringsobjekt.
5. En ændring af en tom-carets skriveformat skal kunne fortrydes og gentages som
   en selvstændig tilstandsændring, hvis den efterfølgende påvirker indtastning.
   Implementeringen må alternativt registrere den sammen med den første
   efterfølgende tekstindsættelse, hvis Ctrl+Z da gendanner den observerbare
   tilstand korrekt.

### Link på en markering

1. Ctrl+K eller kommandoen Indsæt link kræver en ikke-tom tekstmarkering.
2. Ved en ikke-tom markering skal programmet bede brugeren om en linkdestination
   i en modal eller tilsvarende fokuseret dialog.
3. Annullerer brugeren dialogen, må dokument, markering, caret og
   fortrydelseshistorik ikke ændres.
4. Bekræfter brugeren en gyldig destination, skal den markerede synlige tekst
   blive linktekst med den angivne destination.
5. Linkkommandoen må ikke ændre den markerede synlige tekst til URL'en. Den
   oprindelige synlige tekst skal bevares som linkets etiket.
6. Når markeringen dækker præcis ét helt eksisterende link, skal dialogen vise
   destinationen. En bekræftelse opdaterer kun dette links destination og
   bevarer etiketten.
7. Når markeringen kun dækker en del af et link eller overlapper flere links,
   skal kun den markerede tekst få den nye destination. Umarkerede dele beholder
   deres destination. Reglen gælder også, når markeringen ligger helt inde i et
   større link.
8. En tom destination skal ikke oprette et link. Dialogen skal forhindre
   bekræftelse eller behandle handlingen som annulleret.
9. Ctrl+K ved et tomt caret skal åbne dialogen uden at ændre dokumentet.
   Indsætning af et nyt link uden markering kræver en særskilt specifikation.
10. Linkformatering skal kunne kombineres med fed og kursiv.
11. Links må hverken være indlejrede eller delvist overlappe hinanden. En synlig
    tekstposition må højst have én linkdestination. Fed og kursiv kan stadig
    overlappe et link.

### Fjernelse af link

1. Når caretet eller hele markeringen ligger i ét link, skal en
   brugergrænsefladekommando kunne fjerne linkets destination og beholde dets
   synlige tekst.
2. Linkfjernelse skal være én fortrydelseshændelse.
3. Ctrl+K er ikke påkrævet som genvej for linkfjernelse, fordi Ctrl+K er
   reserveret til oprettelse eller redigering af link.

## Scroll og synlighed

1. Musehjulet skal scrolle indholdet lodret uden at flytte caret eller ændre
   markeringen.
2. Shift+musehjul kan scrolle vandret, hvis dokumentet har vandret scroll. Det
   er ikke påkrævet for dokumenter uden vandret scroll.
3. Scrollbar-bevægelser skal ikke ændre caret eller markeringen.
4. En aktiv trækmarkering skal fortsat fungere under autoscroll.
5. Efter en redigerings-, navigations-, fortrydelses- eller gentagelseshandling
   skal programmet kun scrolle, hvis active end ikke er tilstrækkeligt synlig.
6. Brugeren skal kunne scrolle væk fra caretet uden at programmet straks tvinger
   visningen tilbage. Automatisk tilbage-scroll må først ske ved en
   efterfølgende handling, der kræver synligt caret.
7. En ændring af ombrydning ved vinduesresize må bevare den aktive tekstposition
   og markeringens interval, selv om deres skærmkoordinater ændres.

## Fejl, skrivebeskyttelse og grænsetilfælde

1. Alle redigerende kommandoer skal være no-op i et skrivebeskyttet dokument.
   Navigation, markering, Ctrl+A og Ctrl+C skal fortsat fungere.
2. I skrivebeskyttet tilstand må Ctrl+X og Ctrl+V ikke ændre clipboard, dokument
   eller fortrydelseshistorik.
3. En clipboard-fejl må ikke slette tekst, ændre markering eller oprette en
   fortrydelsespost.
4. En mislykket intern mutation, parseropdatering eller relayout efter en
   redigeringshandling skal rulle hele handlingen tilbage. Dokumenttekst,
   formatering, markering og fortrydelseshistorik skal forblive konsistente.
5. Programmet skal afvise eller sikkert begrænse tekstinput, hvis operationen
   overskrider dokumentets tilladte størrelse. En afvisning må ikke slette den
   eksisterende markering.
6. En tom dokumentmodel skal have én gyldig redigerbar position ved offset nul.
7. Ctrl+A i et tomt dokument skal resultere i en tom markering ved offset nul og
   må ikke fejle.
8. Kopiering af en markering, der begynder eller slutter på et afsnitsskift,
   skal bevare netop de markerede afsnitsskift på clipboard.
9. Markering, kopi, klip, paste, sletning og navigation skal virke med dansk
   `æ`, `ø` og `å`. Det gælder også kombinerende tegn, RTL-tekst og emoji.
10. En ændring i dokumentlayout må aldrig konvertere en eksisterende ikke-tom
    markering til et andet tekstinterval.

## Acceptance criteria

Udvikleren skal levere automatiske tests af modellen. Hit-test, fokus, clipboard
og IME skal også have en testliste til Windows. Hvert kriterium kræver et
konkret resultat. En test af, at en handling ikke fejler, er ikke nok. Følgende
kriterier skal være opfyldt.

### Caret og mus

1. Klik mellem `a` og `b` i teksten `ab` placerer et tomt caret mellem tegnene.
2. Klik til højre for den sidste synlige glyph på en ombrudt linje placerer
   caretet ved linjens slutning.
3. Shift+klik fra position 2 til position 8 skaber markeringen `[2, 8)`.
   Shift+klik derefter på position 1 skaber `[1, 2)` med anchor på position 2 og
   active end på position 1.
4. Træk fra et afsnit til det næste markerer alle tekstpositioner mellem start
   og slutpunkt, også når trækretningen vendes.
5. Dobbeltklik på `word,` markerer `word` uden komma og uden mellemrum.
   Dobbeltklik direkte på kommaet markerer kommaet.
6. Trippelklik i et afsnit med afsluttende afsnitsskift markerer afsnittets
   tekst og skiftet, men ikke tekst i næste afsnit.
7. Træk efter dobbeltklik flytter markeringsgrænsen i ordgrænser. Træk efter
   trippelklik flytter den i afsnitsgrænser.
8. Træk over den nederste vindueskant starter autoscroll og udvider markeringen,
   indtil brugeren slipper museknappen eller dokumentets slutning nås.

### Unicode og input

1. Venstre pil, højre pil, Backspace og Delete behandler `e` plus kombinerende
   accent som én grafemklynge.
2. Venstre pil, højre pil, Backspace og Delete behandler et
   regionalindikator-par som én grafemklynge.
3. Backspace efter en emoji-ZWJ-sekvens sletter hele den viste emoji og
   efterlader gyldig tekst.
4. Indtastning af `*tekst*` viser de indtastede asterisker som tekst og anvender
   ikke kursiv automatisk.
5. Indtastning med en aktiv markering erstatter hele markeringen og placerer
   caretet efter den indsatte tekst.
6. Enter erstatter en aktiv markering med ét afsnitsskift.

### Sletning og navigation

1. Backspace ved dokumentets begyndelse og Delete ved dokumentets slutning er
   no-op og opretter ikke fortrydelseshistorik. Et tomt punkt følger dog den
   særlige regel for Backspace nedenfor.
2. Backspace eller Delete med markeringen `bcd` i `abcde` efterlader `ae` og et
   caret mellem `a` og `e`.
3. Ctrl+Backspace og Ctrl+Delete stopper ved samme ordgrænser, som dobbeltklik
   og Ctrl+pil bruger.
4. Venstre pil fra markeringen `[3, 7)` kollapser til position 3. Højre pil
   kollapser til position 7. Ingen af dem flytter en ekstra grafem.
5. Shift+Ctrl+Højre udvider markeringen til næste ordgrænse uden at ændre
   anchor.
6. Gentagen Ned gennem korte og lange ombrudte linjer gendanner den oprindeligt
   ønskede vandrette position, når en tilstrækkeligt lang linje nås.
7. Home og End flytter til henholdsvis start og slutning af den visuelle linje.
   Ctrl+Home og Ctrl+End flytter til dokumentets grænser.
8. Ctrl+A uden for en tabel markerer hele den synlige redigerbare dokumenttekst.
   I en tabel prøves de fire trin fra celle til dokument, uden ændring af tekst
   eller historik.

### Acceptkriterier for clipboard

1. Ctrl+C med en tom markering bevarer den eksisterende clipboard-tekst uændret.
2. Ctrl+C med en flerlinjet markering udstiller den markerede tekst som
   `CF_UNICODETEXT` med CRLF-linjeskift og NUL-terminering.
3. Ctrl+X sletter ikke dokumenttekst, hvis clipboard-operationen fejler.
4. Ctrl+V med `CF_UNICODETEXT` erstatter markeringen som én mutation og
   normaliserer CRLF, CR og LF ens.
5. Ctrl+V med et clipboard uden understøttet tekstformat er en no-op.
6. Ekstern paste bruger kun ren tekst, også når clipboardet tilbyder RTF,
   HTML eller Word-formater. Alle paste-veje følger AC-13 nedenfor.

### Acceptkriterier for fortryd og gentag

1. Indtastning af en sammenhængende række tegn kan fortrydes som én handling,
   når der ikke ligger navigation eller anden handling imellem.
2. En indsætning fra clipboard fortrydes som én handling, også når den
   indeholder flere afsnit.
3. Ctrl+Z efter Ctrl+X gendanner den slettede tekst og den oprindelige
   markering.
4. Ctrl+Y efter Ctrl+Z genskaber både dokumentets indhold og markeringen efter
   den oprindelige handling.
5. Efter Ctrl+Z og derefter ny indtastning er Ctrl+Y en no-op.
6. No-op-handlinger ved dokumentets grænser opretter ikke fortrydelsesposter.

### Formatering

1. Ctrl+B på en markeret tekstsekvens uden fed gør hele sekvensen fed uden at
   ændre teksten eller markeringens grænser.
2. Ctrl+B på en fuldt fed markering fjerner kun fed fra den markerede tekst.
3. Ctrl+B på en markering, der delvist er fed, gør hele markeringen fed og
   bevarer formateringen uden for markeringen.
4. Ctrl+I følger de samme regler for kursiv, også når teksten samtidig er fed.
5. Ctrl+B ved et tomt caret påvirker efterfølgende indtastning, men indsætter
   ingen tom eller synlig markup.
6. Ctrl+K med markeret tekst og en bekræftet destination bevarer etiketteksten
   og anvender destinationen som link.
7. Annullering af linkdialogen ændrer hverken dokument, markering eller
   fortrydelseshistorik.
8. Opdatering af et helt markeret link bevarer etiketten. Ved delvis markering
   får kun den markerede tekst den nye destination. Resten bevarer den gamle, og
   ingen links er indlejrede eller overlappende.
9. Ctrl+Z og Ctrl+Y gendanner og gentager hver formatkommando som en enkelt
   handling.

### Fokus, scroll og skrivebeskyttelse

1. Caretet vises ved fokus, skjules ved fokus-tab og følger teksten ved scroll.
2. Musehjul og scrollbar ændrer ikke markeringens tekstinterval.
3. Når en navigation flytter caretet uden for synligt område, bliver caretet
   gjort synligt med mindst mulig scroll.
4. I skrivebeskyttet tilstand virker markering og Ctrl+C, mens Ctrl+X og Ctrl+V
   ikke ændrer clipboard, dokument eller fortrydelseshistorik.
5. Resize og ombrydning bevarer samme tekstinterval for en eksisterende
   markering.

## Implementeringsstatus i MarkDownIt

Dette afsnit lemper ikke kravene. Det samler tidligere noter om kodekoblinger og
kendte huller. Noterne er ikke et testresultat. Et acceptkriterium kræver bevis,
før det kan erklæres opfyldt.

### Tidligere registrerede kodekoblinger, ikke testbevis

- Ctrl+B og Ctrl+I går gennem de samlede formatkommandoer, så markeringens
  eksisterende formatstatus bruges ved til- og frakobling.
- Ctrl+B og Ctrl+I ved tomt caret gemmer en pending-formattilstand. Den næste
  indtastede tekst omsluttes i samme redigeringshandling, og der indsættes ikke
  et tomt formatpar ved caretet.
- Ctrl+Backspace og Ctrl+Delete sletter via de samme ordgrænsefunktioner som
  Ctrl+Venstre og Ctrl+Højre.
- Ctrl+Insert kopierer, Shift+Insert indsætter, og Shift+Delete klipper efter de
  tilsvarende Ctrl-genveje.
- Klipning sletter først dokumenttekst efter en succesfuld clipboard-operation.
- Indsætning over en eksisterende markering registreres som én
  `RecordAndApply`-mutation med én undo-post.
- `CF_UNICODETEXT` eksporteres med CRLF-linjeskift, og indsat clipboardtekst
  normaliseres til LF.
- Ctrl+K bruger linkdialogen og opretter ikke et tomt link, hvis markeringen er
  tom.
- Søgning, erstatning, tekstflytning med drag and drop, dokumentbevidst Tab og
  Enter samt linkaktivering er koblet til editorens aktuelle model.
- Automatisk Markdown-formatering er ikke en del af denne status. Den er
  afgrænset fra specifikationen og indgår ikke i acceptance-kriterierne.
- Søgning og tekstflytning afviser kandidater, der ikke ligger på gyldige
  grafemgrænser. Det er en modelkontrol, ikke dokumentation for fuld Unicode-
  segmentering.

### Resterende krav, som skal verificeres eller implementeres

1. Den portable grafemmodel skal valideres mod hele kravsættet for Unicode-
   grafemklynger, især kombinerende tegn, regionalindikatorer og emoji-
   ZWJ-sekvenser. De nuværende grænsekontroller er ikke en påstand om fuld
   Unicode-segmentering.
2. Hit-test, dobbeltklik, ordbevægelse, clipboard, linkaktivering og
   selection-preservation ved relayout kræver fortsat målrettede Windows-tests.
3. De portable regressionstests skal køres sammen med hele testpakken, og
   tabelnavigation skal også dække tomme celler og dokumentets grænser.
4. Den endelige Windows acceptance-test skal køres med MSVC og Windows SDK. En
   Linux- eller WSL-syntakskontrol kan ikke erstatte denne build-gate.

### Afvigelser fundet ved dokumentreview

Statisk læsning af `src/app.cpp` viser, at klip og indsætning i visningstilstand
kan kalde `SetEdit(true)`. Det opfylder ikke kravet om no-op uden redigering.
Home og End med markering kollapser til markeringens ender, også med Ctrl. Det
opfylder ikke det præciserede krav om at nå linje- eller dokumentgrænsen. Disse
kodeveje er ikke ændret eller runtime-testet ved dette review.

`tests/editor_features_test.cpp` tester, at hjælperen `MoveTableCell` stopper
ved sidste celle. Det beviser ikke den samlede Tab-kommando, som skal oprette en
række. Den kræver en test af kommandoen og dens undo-handling. De tidligere
statuspunkter ovenfor er historiske noter, ikke en godkendelse.

## Udvidede krav: dokumentbevidst redigering og søgning

Dette afsnit er normativt. Det beskriver funktioner, som skal følge den samme
caret-, markerings-, clipboard- og undo-model som resten af editoren. Det ændrer
ikke afgrænsningen af automatisk Markdown-formatering ovenfor. Kravene til Tab
og Enter beskriver tastens dokumentbevidste handling, ikke en automatisk
omskrivning af almindeligt tekstinput.

### Drag and drop af tekst

1. Et tryk i en eksisterende markering starter ikke en ny markering. Hvis
   markøren flyttes mindst systemets drag-tærskel, flyttes hele markeringen.
   Shift+klik følger dog altid markeringsreglen. Uden træk kollapser et
   almindeligt klik ved slip til den normaliserede klikposition.
2. Et drop ved eller inden for den valgte tekst er en no-op. Teksten må ikke
   duplikeres eller slettes.
3. Et drop før eller efter markeringen flytter den valgte tekst én gang.
   Markeringen flyttes med teksten og forbliver aktiv efter operationen.
4. Drag-operationen er én undo-handling. Ctrl+Z gendanner både dokumenttekst og
   den oprindelige markering, og Ctrl+Y genskaber flytningen.
5. Drag skal flytte synlig tekst og dens formatering gennem dokumentmodellen.
   Kilde, slutpunkt og drop skal være gyldige grafem- og strukturgrænser.
   Gyldige UTF-8-grænser alene er ikke nok. En flytning må ikke skabe links i
   links, tabeller i celler eller ugyldige Markdown-markører.
6. Mouse capture frigives ved venstre museknap-op, også hvis drop-positionen
   ligger uden for viewporten eller operationen bliver afvist.
7. Esc, mistet capture og afvist drop afbryder flytningen. Tekst, formatering,
   markering og historik skal forblive som før forsøget. Autoscroll følger
   trækmarkeringens regler. Tekstflytning mellem programmer og Ctrl-drag som
   kopi ligger uden for dette afsnit.

### Særlige tastaturregler for dokumentelementer

1. Enter i en almindelig paragraf opretter det almindelige afsnitsskift, som
   editorens paragrafmodel definerer.
2. Enter i en kodeblok indsætter ét LF-tegn og indsætter ikke en ekstra tom
   paragraf uden for kodeblokken.
3. Enter i en aktiv liste fortsætter samme liste med samme listetype. Enter på
   et tomt listepunkt afslutter listepunktet uden at oprette en ny tom linje i
   listen.
4. Enter og Shift+Enter i en tabelcelle er no-op, også med markeret tekst.
   MarkDownIts cellemodel har én fysisk Markdown-linje. Den generelle
   tabelreferences forslag om flere afsnit i celler gælder ikke denne model.
5. Tab i en tabel flytter til næste celle i rækkeorden. Shift+Tab flytter til
   forrige celle. Tomme celler må ikke springes over. Shift+Tab i første celle
   er no-op. Tab i sidste celle opretter én tom række med samme antal kolonner
   og placerer caretet i dens første celle. En aktiv tekstmarkering kollapser
   ved navigationen; dens tekst må ikke slettes. Ren navigation har ingen
   undo-post, mens rækkeindsættelsen har præcis én.
6. Tab i en kodeblok indsætter fire ASCII-mellemrum. Shift+Tab fjerner én
   kodeindrykning, når linjen har den tilsvarende indrykning.
7. Tab uden for tabel og kodeblok bruger listeindrykning på liste-linjer og
   almindelig indrykning på andre linjer. Shift+Tab udfører den tilsvarende
   udrykning.
8. Alle dokumentelementregler skal oprette højst én undo-post pr. tastetryk. En
   no-op må ikke oprette en undo-post.
9. Enkeltklik i en celle skal blive i den ramte celle. Dobbeltklik markerer
   cellen, og trippelklik markerer rækken, som tabelreferencen beskriver. Disse
   strukturelle valg er forskellige fra ord- og afsnitsvalg uden for tabeller.
   Cellens Backspace og Delete må ikke slette dens separatorer.
10. Ingen inputvej må skabe en tabel inde i en celle. Det gælder også
    indsætning, erstatning, tekstflytning og undo/redo. En ugyldig ændring skal
    afvises før mutation. En celle kan vise et indtastet `|`, men kilden skal
    beskytte det, så det ikke bliver en ny celle. Beskyttelsen er ikke
    automatisk formatering.
11. Op og Ned med tom markering i en tabel er en undtagelse fra visuel
    linjenavigation. De flytter til samme kolonne i forrige eller næste række.
    Målpositionen skal følge ønsket visuel X og være en gyldig grafemgrænse i
    målcellen. En kort celle må ikke overskrive den huskede X. Rå byteoffsets
    må ikke kopieres mellem celler. Ved første og sidste række er handlingen
    no-op. Shift+Op og Shift+Ned følger den samme bevægelse og bevarer anchor.
12. En opdeling af en celle skal ske ved synlige grafemgrænser. Format og
    linkdestination skal følge teksten. De øvrige rækker får tomme celler,
    så alle rækker beholder samme kolonneantal. Parse af resultatet skal
    bekræfte det præcise antal, også ved tomme celler.
13. Backspace med tom markering i et tomt punkt fjerner markør og indrykning.
    Caret bliver i et tomt afsnit på samme linje. Linjeskift og nabotekst bevares.
    Det er én undo-handling, også i et indrykket punkt og ved dokumentets grænser.
    Reglen gælder aktive punktopstillinger, ikke nummererede lister, kode eller
    bogstavelig tekst. Caret-referencens afsnit 15 og kriterium F styrer dette valg.

### Linkaktivering med musen

1. Et klik på et link i skrivebeskyttet tilstand aktiverer linket.
2. Ctrl+klik på et link i redigeringstilstand aktiverer linket. Et almindeligt
   klik i redigeringstilstand placerer caret eller ændrer markering, så links
   ikke blokerer tekstredigering.
3. Eksterne links må kun åbnes med `http`, `https` eller `mailto`. Skemaet
   kontrolleres uden forskel på store og små bogstaver. Andre skemaer,
   kontroltegn og ugyldige destinationer skal afvises uden ekstern handling.
   Listen er projektets tilladelsesliste, ikke en garanti for linkets sikkerhed.
   `mailto` må åbne en kladde i brugerens mailprogram, men må ikke sende mail.
4. Interne ankerlinks flytter viewporten til det tilsvarende dokumentafsnit. De
   må ikke starte et eksternt program.
5. Linkaktivering må ikke ændre dokumenttekst, dirty-state, markering eller
   undo-historik.
6. Hvis hit-test rammer skjulte linkmarkører, skal linket stadig kunne aktiveres
   uden at placere caret på en skjult kildeposition.

### Søgning og erstatning

1. Ctrl+F åbner eller fokuserer søgebåndet i Find-tilstand. Ctrl+H åbner samme
   bånd i Find and Replace-tilstand. Eksisterende søgetekst markeres i feltet.
   Båndet har Match case, Whole word, Previous, Next, matchtæller og Luk.
   Erstatningsfelt, Replace og Replace All er inaktive i Find-tilstand.
2. Søgning skal virke i alle dokumentvisninger uden at slå redigering til.
   Søgeomfanget er dokumentets tekst, ikke brugergrænsefladens labels. Synlig
   tekst i afsnit, overskrifter, lister, kodeblokke og celler indgår. Skjulte
   formatmarkører og linkdestinationer indgår ikke. Kildevisningen bruger samme
   tekstomfang med en mapping til kilden. Match må ikke krydse afsnits- eller
   cellegrænser i denne basisfunktion.
3. Søgning sker løbende, når feltet ændres. Alle match fremhæves, og det
   aktuelle match har et særskilt udseende. Søgning er som standard uden forskel
   på store og små bogstaver. Dansk `æ/Æ`, `ø/Ø` og `å/Å` skal virke. Whole word
   bruger de fælles Unicode-ordgrænser. Tegn er bogstavelige, ikke
   regex-syntaks.
4. Next starter ved caretet eller efter det aktuelle match og går fremad i
   logisk orden. Previous går baglæns. Begge starter forfra ved dokumentets
   grænser. Matchen markeres og scrolles frem. I søgefeltet går Enter til næste
   match, Shift+Enter til forrige. Et dokumentklik flytter søgningens start.
5. Tom søgetekst giver ingen match. Uden match er Next, Previous, Replace og
   Replace All inaktive. Dokument, markering og historik forbliver uændrede.
6. Replace erstatter kun det aktuelle, stadig gyldige match som én mutation og
   én undo-post. Derefter vælges næste match af den oprindelige søgetekst. Hvis
   intet match findes, markeres den indsatte tekst. En tom erstatning sletter
   matchen. Erstatning følger kravet om skrivbar redigeringstilstand.
7. Replace All bruger alle ikke-overlappende match i én fast dokumentversion.
   Indsat tekst søges ikke igen under samme handling. Handlingen har én
   undo-post og viser det faktiske antal erstatninger. Nul ændringer er no-op.
8. Matchgrænser skal ligge på gyldige grafem- og strukturgrænser. Mapping til
   kildeoffsets skal bevares. Efter en redigering skal match genberegnes, før
   Replace kan bruge dem. Zoom, scroll og resize må ikke ændre matchlisten.
9. Esc eller Luk skjuler søgebåndet og giver fokus til dokumentet. Det bevarer
   allerede udførte erstatninger, dokumentets markering, clipboard og historik.
   Løbende søgning er en læsehandling, ikke en handling, som Luk skal fortryde.
10. Erstatning i et ensartet formatløb skal bevare formatet og
    linkdestinationen. Tekst uden for matchen må ikke ændres. Reglen for
    blandede formatløb skal afklares før den del implementeres, som angivet
    under "Åbne beslutninger".
11. Båndets kontroller skal have tilgængelige navne og kunne betjenes med
    tastatur. Fokus må ikke blive fanget i båndet. Matchtælleren skal opdateres
    efter ændringer, også i skrivebeskyttet tilstand.

### Acceptance criteria for de udvidede funktioner

1. Portable tests dokumenterer case-regler, whole-word-regler, tom query,
   non-overlapping matches, Replace All og tekstflytning før og efter en
   markering med forventet tekst og ny startposition.
2. Portable tests dokumenterer navigation til og fra tomme celler, Shift+Tab ved
   første celle og Tab ved sidste celle. Den samlede Tab-kommando skal oprette
   præcis én række og én undo-post, også ved EOF uden linjeskift. Enter og
   Shift+Enter i tabel bevarer markering og tekst. Enter i kodeblok og
   listefortsættelse testes med deres præcise tekst og caret-position.
3. Portable tests afviser kilde- og drop-positioner midt i en kombinerende
   grafemklynge, et regionalindikator-flag eller en emoji-ZWJ-sekvens og
   accepterer flytning af hele grafemklyngen.
4. Windows CI bygger både appen og testbinary med MSVC og kører hele testpakken.
5. Windows acceptance-testen verificerer Ctrl+F, Ctrl+H, Replace, Replace All,
   undo/redo, drag-and-drop, mouse capture, linkaktivering og alle dokument-
   tastaturregler med en rigtig Win32-window message loop.
6. Linux eller WSL må bruges til statisk kontrol, men kan ikke erstatte Windows
   runtime-verifikation eller MSVC-buildet.

## Supplerende acceptkriterier og testspor

Disse ID'er er stabile og supplerer testlisterne ovenfor. Testnavne og
testrapporter skal angive ID'et. Testresultatet skal vise tekst, struktur,
caret, anchor, active end og antal undo-poster, hvor handlingen påvirker dem. En
manuel test skal også angive Windows-version, DPI og inputmetode.

### AC-01: Genveje og fokus

Krav: "Tilstand og kommandoer" og "Påkrævede standardgenveje".

- Ctrl+Insert, Shift+Insert og Shift+Delete giver samme resultat som Ctrl+C,
  Ctrl+V og Ctrl+X. Shift+Delete med tom markering bevarer clipboard.
- Ctrl+A og Ctrl+Z i søgefeltet påvirker kun feltet. Tab flytter fokus mellem
  båndets kontroller og må ikke indrykke dokumentet.
- Windows-testen kontrollerer, at ingen genvej indsætter et kontroltegn.

### AC-02: Navigation med aktiv markering

Krav: "Fælles regler for navigation", "Home og End" og "Page Up og Page Down".

- Fra `[3, 7)` midt i et dokument når Ctrl+Home dokumentets begyndelse og
  Ctrl+End dets slutning i ét tastetryk. Gentag med omvendt markeringsretning.
- Home og End når linjegrænsen, ikke kun markeringsgrænsen. Page Up og Page Down
  når en linje omtrent én viewport væk, når dokumentet er langt nok.
- Shift-varianter bevarer anchor. Lodret navigation testes gennem både korte og
  lange linjer. Windows-testen måler den viste destination.

### AC-03: Kanonisk position og afsnit

Krav: "Tekstpositioner", "Enkeltklik" og "Enter" samt caret-referencens A-H.

- Klik efter formateret tekst, efter et link og i et tomt afsnit bliver i den
  ramte blok. Positionen må ikke lande i skjulte markører.
- Enter ved afsnitsslut med et eksisterende næste afsnit indsætter ét tomt
  afsnit imellem dem. Ved en ombrudt linjes slutning splitter Enter det samme
  afsnit ved caret. Shift+Enter bevarer afsnitsantal og skaber et synligt skift.
- Markering plus Enter og efterfølgende undo/redo bevarer tekst og formatering
  på begge sider. Tests dækker tomme afsnit og dokumentets grænser.
- Backspace i et tomt punkt fjerner markør og indrykning i én undo-handling.
  Tests dækker `-`, `*` og `+`, LF og CRLF, indrykning og dokumentets grænser.
  Efter parse og layout bliver caret på den tomme linje. Nabotekst bevares.
  Undo/redo gendanner tekst og caret. Kode og bogstavelig tekst ændrer ikke listeformat.
- Trippelklik i punkt 3 i en liste med fem punkter efterfulgt af Backspace
  efterlader fem punkter. Punkt 3 er tomt, og punkt 4 og 5 beholder deres niveau.
  Tests dækker LF, CRLF, fed, links og undo/redo. Næste Backspace fjerner det tomme punkt.

### AC-04: Unicode og tovejs-layout

Krav: "Grafemklynger og Unicode" og "Fælles regler for navigation".

- Automatiske tests bruger den fastlåste Unicode-versions
  `GraphemeBreakTest.txt` og `WordBreakTest.txt`. Beskrevne tilpasninger testes
  særskilt. Tests må ikke nøjes med at kontrollere UTF-8-grænser.
- Windows-tests med genereret LTR/RTL-testdata kontrollerer klik, visuelle pile,
  Home/End, sletning og markering ved retningsskift. Data kan beskrives med
  kodepunkter. Ingen handling må dele en grafemklynge.
- Resize og zoom bevarer logisk interval og gyldig visuel caret-side.

### AC-05: IME og tastaturinput

Krav: "Grafemklynger og Unicode" og "Almindeligt tekstinput".

- Windows-testen bekræfter komposition over en markering som én undo-handling.
  Annulleret komposition bevarer den oprindelige tekst og markering.
- Emoji-panel, dead keys og AltGr på dansk tastatur indsætter tegn uden at
  udløse en Ctrl-genvej. Ugyldig ekstern Unicode følger den valgte fejlregel.
- Indtastning, paste og erstatning med `*tekst*`, `# ` og `- ` forbliver synlig
  tekst efter parse, resize, gemning og genåbning. Ingen af dem slår format til.

### AC-06: Skrivebeskyttelse

Krav: "Tilstand og kommandoer" og "Fejl, skrivebeskyttelse og grænsetilfælde".

- Alle mutationsveje testes med redigering slået fra, også aliasgenveje,
  formatkommandoer, drag, Tab i sidste celle, Replace All, Ctrl+Z og Ctrl+Y.
  Tekst, formatering, historik og clipboard forbliver uændrede.
- Markering, kopi, søgning og linkaktivering virker fortsat. Ingen genvej
  skifter til redigering. Inaktive erstatningsknapper kan ikke aktiveres.

### AC-07: Fejl og transaktioner

Krav: "Clipboard" og "Fejl, skrivebeskyttelse og grænsetilfælde".

- Testene injicerer fejl ved clipboard-adgang, mutation, parse og relayout. Klip
  må ikke slette uden en vellykket kopiering. Dokument, formatering, markering
  og historik rulles tilbage ved en intern fejl.
- Input over den fastlagte størrelsesgrænse afvises uden at slette markeringen.
  Clipboarddata læses kun inden for den modtagne buffers grænser.
- Clipboard kan allerede være ændret efter en vellykket kopiering. Kravet om
  rollback af dokumentet er ikke et løfte om at gendanne systemets clipboard.

### AC-08: Formatering og links

Krav: "Grundlæggende inlineformatering" og "Linkaktivering med musen".

- Delvis linkændring bevarer destinationer uden for markeringen. Fjern link
  bevarer etiketten. Fed og kursiv kan kombineres med link uden tom markup.
- Tests af pending skriveformat kontrollerer både Ctrl+Z og Ctrl+Y, også når
  fortrydelsesposten først oprettes ved næste tekstinput.
- `http`, `https` og `mailto` accepteres af aktiveringskontrollen. Et afvist
  skema, en ugyldig destination eller et manglende anker må ikke starte et
  eksternt program. En teststub registrerer kald, så testen ikke sender mail.

### AC-09: Tekstflytning

Krav: "Drag and drop af tekst".

- Klik i en markering uden drag kollapser ved slip. Shift+klik udvider valget.
  Drop ved eller inde i kildeintervallet er no-op.
- Gyldigt drop før og efter intervallet bevarer tekstens formatering og giver én
  undo-post. Esc, mistet capture og afvist drop bevarer hele tilstanden.
- Windows-testen dækker systemets drag-tærskel, autoscroll og slip uden for
  vinduet. Capture skal være frigivet efter alle afslutninger.

### AC-10: Tabelprofil

Krav: "Særlige tastaturregler for dokumentelementer" og tabelreferencen.

- Dobbeltklik markerer celle, trippelklik række. Fire Ctrl+A markerer efter tur
  celle, række, tabel og dokument. En anden handling nulstiller trinnene.
- Tab i sidste celle opretter én række, også ved EOF. Ctrl+Z fjerner hele
  rækken, og Ctrl+Y genskaber den. Almindelig Tab-navigation har ingen historik.
- Enter og Shift+Enter med og uden markering bevarer cellen. Sletning ved
  cellens grænser bevarer separatorer. Indsat `|` bliver synlig tekst.
- Indsætning, erstatning og drag kan ikke skabe en tabel i en celle.
- Op og Ned testes med ulige cellelængder, ombrudte celler, kombinerende tegn
  og emoji. Mål er samme kolonne og nærmeste gyldige position ved ønsket X.
  Shift bevarer anchor, og en tabelgrænse giver no-op.
- Opdel celle testes med tom tekst, kombinerende tegn, emoji og formatløb.
  Tests viser kilden før og efter samt det parsede kolonneantal. Ingen
  grafemklynge deles, og formatet følger teksten.

### AC-11: Søgebånd og søgeomfang

Krav: "Søgning og erstatning" samt søgereferencens minimumskrav.

- Ctrl+F, Ctrl+H, Enter, Shift+Enter og Esc testes med båndets rigtige fokus.
  Dokumentet kan redigeres, mens båndet er åbent, når redigering er slået til.
- Samme søgetekst giver samme match i læse-, redigerings- og kildevisning.
  Skjulte markører og linkdestinationer tælles ikke. Tekst i tomme celler skaber
  ingen falske match. Match krydser ikke blok- eller cellegrænser.
- Matchtæller, fremhævning og wrap kontrolleres i begge retninger. Zoom, scroll
  og resize ændrer ikke matchene. Luk bevarer dokumentets historik og markering.

### AC-12: Erstatning og gyldige match

Krav: "Søgning og erstatning".

- Replace ændrer det aktuelle match, ikke det næste. Med `aa aa` som tekst og
  andet match valgt ændres kun andet match. Tom erstatning sletter matchen.
- Replace All af `aa` i `aaaa` med `a` giver `aa` og to erstatninger. Erstatning
  med tekst, der indeholder søgeteksten, behandler kun de oprindelige match.
- En redigering efter søgning må ikke efterlade gamle offsets som mål.
  Erstatning i fed eller linktekst bevarer løbets format og destination.
- Nul ændringer giver ingen undo-post. Én Ctrl+Z gendanner alle ændringer,
  formatering og markeringen før Replace All. Ctrl+Y genskaber eftertilstanden.

### AC-13: Ekstern paste som ren tekst

Krav: "Ekstern indsætning er kun tekst" og "Indsæt".

- Automatiske clipboardtests tilbyder `CF_UNICODETEXT` sammen med RTF, HTML
  og et Word-format. Kun den rene tekst indsættes. Tests skal også bekræfte,
  at de rige formater ikke læses. Kildens fed, farve og link følger ikke med.
- Med kun RTF, HTML eller objekter er paste no-op. Tekst, markering og
  historik bevares. Ukendt oprindelse følger samme regel.
- Windows-testen kopierer formateret indhold fra Word, Excel og en browser.
  Ctrl+V, Shift+Insert, menu og kontekstmenu giver samme rene tekstresultat.
  Der oprettes ingen tabel, liste eller link ud fra kildens format.
- Test med en aktiv markering kontrollerer erstatning som én mutation og én
  undo-post. En Ctrl+Z gendanner den oprindelige tekst og markering.
- Tests dækker almindelig tekst, kodeblok og tabelcelle. Tabs, linjeskift,
  URL'er og Markdown-tegn følger tekst- og cellereglerne uden kildeformat.

## Teststrategi og godkendelse

Modeltests skal være automatiske og køre i Windows CI sammen med resten af
testpakken. Windows-tests skal bygge app og testprogram med MSVC og Windows SDK.
De skal bruge en rigtig Win32 message loop, hvor fokus eller beskeder er en del
af kravet. Clipboard og ekstern linkåbning skal kunne erstattes med
testinterfaces, så fejl og afvisning kan testes uden eksterne sideeffekter.

Hvert acceptkriterium ovenfor og i de tidligere lister skal kobles til mindst én
test. CI skal kontrollere, at ingen kriterier mangler i testoversigten. Det
gælder også de særlige celleregler, ikke kun de portable hjælpefunktioner. Et
kriterium uden runtime-test eller manuel testjournal er ikke godkendt.

Manuel Windows-test er en udtrykkelig undtagelse for systemets IME, emoji-panel,
caret-blink, faktisk musecapture og visuel kontrol af DirectWrite ved DPI og
zoom. Modeldelen skal stadig have automatiske tests. En ny fase skal levere sine
tests sammen med koden. WSL-test er kun en supplerende kontrol.

| Kontrol | Grænse | Testspor |
| --- | --- | --- |
| Ugyldige caret- eller grafempositioner | 0 efter en handling | AC-03, AC-04 |
| Mutation uden skrivbar redigering | 0 | AC-06 |
| Undo-poster pr. atomisk ændring | 1; 0 ved no-op og ren navigation | AC-07, AC-09, AC-10, AC-12 |
| Ændrede tegn uden for et format- eller erstatningsinterval | 0 | AC-08, AC-12 |
| Manglende acceptkriterier i testoversigten | 0 | CI-kontrol |

## Åbne beslutninger

Disse punkter er ikke frie implementeringsvalg. Den nævnte ejer skal afklare
punktet og tilføje testdata, før den berørte del kan godkendes.

- Produktejer: fastlæg maksimal dokumentstørrelse, inputgrænse og mål for
  responstid. Angiv testmaskine og dokumentstørrelser. Denne reference
  fastlægger ikke vilkårlige tal uden et målegrundlag.
- Implementeringsansvarlig: fastlås Unicode-version, fejlregel for ugyldigt
  input og eventuelle ordtilpasninger. Angiv version og testfiler i rapporten.
- Produktejer: fastlæg formatarv ved erstatning over blandede formatløb samt
  Unicode-normalisering og fuld case folding i søgning. Dansk case og gyldige
  grafemgrænser er krav, selv om disse yderligere valg ikke er afklaret.
- Produktejer: fastlæg linkkommandoens resultat ved markering på tværs af afsnit
  eller celler. Kommandoen må ikke skabe ugyldige links eller ændre tekst uden
  for markeringen, mens det punkt afventer beslutning.
- Produktejer: afklar om Ctrl-drag som kopi og eksternt drag and drop skal med
  i en senere fase. De er ikke basisfunktioner. Rig paste fra andre programmer
  er ikke et åbent valg; ekstern paste er altid ren tekst.
- Implementeringsansvarlig: ret tabelreferencens noter om byteoffsets og
  afsnitsreferencens gamle Enter-eksempel ved næste review af de filer.
  Indtil da gælder de udtrykkelige afgrænsninger i denne reference. Noterne
  er ikke testbevis for grafemsikker tabelnavigation.

## Referencer

Microsoft-kilderne beskriver genveje og API-kontrakter. De lokale referencer
beskriver MarkDownIts model og produktvalg. En kildehenvisning er ikke bevis
for, at appen allerede opfylder et krav.

- [Rich Edit og dets standardgenveje][rich-edit].
- [HomeKey og Windows Home/End-adfærd][home-key].
- [Standard Clipboard Formats, herunder CF_UNICODETEXT][clipboard-formats].
- [DirectWrite HitTestPoint][hit-test].
- [Visuel caretplacering i tovejs-tekst][bidi-caret].
- [UAX #29, Unicode Text Segmentation][unicode-segmentation].
- [Caret- og afsnitsmodel](word-processor-guidelines.md).
- [Tabelmodel og MarkDownIts afgrænsninger](word-processor-table-guidelines.md).
- [Søgning og erstatning i MarkDownIt](find-replace.md).
- [Google Markdown style guide][markdown-style].

[rich-edit]: https://learn.microsoft.com/en-us/windows/win32/controls/about-rich-edit-controls#rich-edit-shortcut-keys
[home-key]: https://learn.microsoft.com/en-us/windows/win32/api/tom/nf-tom-itextselection-homekey
[clipboard-formats]: https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats
[hit-test]: https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextlayout-hittestpoint
[bidi-caret]: https://learn.microsoft.com/en-us/windows/win32/intl/displaying-the-caret-in-bidirectional-strings
[unicode-segmentation]: https://www.unicode.org/reports/tr29/
[markdown-style]: https://google.github.io/styleguide/docguide/style.html
