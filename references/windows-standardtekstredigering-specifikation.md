# Funktionsspecifikation for standardtekstredigering på Windows

## Formål

Dette dokument fastlægger krav til den synlige, redigerbare tekstmodel og de
almindelige tekstredigeringshandlinger i MarkDownIt på Windows. Kravene gælder
dokumentets redigerbare tekstområde, uanset om programmet gemmer Markdown som
rå tekst eller med interne markører.

En implementering skal følge den observerbare adfærd, som brugere forventer af
et almindeligt Windows-tekstredigeringsfelt, for de handlinger der er beskrevet
her. Funktioner uden for specifikationen må ikke ændre resultatet af de
beskrevne musebevægelser, taster eller standardgenveje.

## Omfang og afgrænsning

Specifikationen omfatter:

- Indsættelsespunkt, kaldet caret i dette dokument.
- Sammenhængende tekstmarkering.
- Mus, tastatur, clipboard, sletning og navigation.
- Fortryd og gentag.
- Visuel og logisk scroll.
- Grundlæggende anvendelse af fed, kursiv og links.
- Unicode, herunder sammensatte tegn og emoji.

Specifikationen omfatter ikke:

- Stavekontrol.
- Flere samtidige markeringer.
- Rektangulær eller kolonnebaseret markering.
- Automatisk fortolkning af skrevet Markdown-syntaks.

Den sidste afgrænsning betyder, at almindeligt tekstinput ikke automatisk må
omskrive eller formatere Markdown-markører. Hvis brugeren skriver `# `, `- `
eller `**`, skal tegnene forblive tekst, medmindre brugeren anvender en separat
og eksplicit formatkommando. At eksisterende Markdown fortolkes ved rendering
er ikke det samme som automatisk formatering og er fortsat tilladt.

Tab, Enter og klik i specialiserede dokumentelementer følger reglerne i afsnittet
"Udvidede krav: dokumentbevidst redigering og søgning".

## Normative ord

- "Skal" angiver et ufravigeligt krav.
- "Må ikke" angiver en forbudt adfærd.
- "Kan" angiver en tilladt implementeringsdetalje, hvis den ikke ændrer observerbar adfærd.

## Grundlæggende model

### Tekstpositioner

1. Dokumentet skal have en lineær rækkefølge af redigerbare tekstpositioner.
2. En position ligger mellem to grafemklynger eller ved starten eller slutningen af dokumentet.
3. Et indtastet tegn, en klippet tekst eller indsat tekst erstatter den aktuelle markering, hvis markeringen ikke er tom.
4. En tom markering repræsenterer et caret ved én tekstposition.
5. En ikke-tom markering repræsenterer intervallet fra markeringens begyndelse, inklusive begyndelsen, til dens slutning, eksklusive slutningen.
6. Intern Markdown-syntaks, skjulte formatmarkører og andre ikke-visuelle lag må ikke kunne vælges eller placere caretet mellem deres enkelte tegn.

### Grafemklynger og Unicode

1. Alle bevægelser, sletninger og markeringer på tegnniveau skal bruge grafemklynger, ikke UTF-8-bytes, UTF-16-kodeenheder eller Unicode-kodepunkter.
2. En grafemklynge kan bestå af flere kodepunkter, eksempelvis `a` efterfulgt af en kombinerende accent, et emoji med hudtone eller en emoji-ZWJ-sekvens.
3. Backspace, Delete, venstre pil og højre pil må aldrig efterlade en ugyldig UTF-sekvens eller dele en synlig grafemklynge.
4. Indsat tekst skal accepteres som Unicode. Ugyldige sekvenser fra en ekstern kilde skal afvises eller erstattes konsekvent med U+FFFD, før de når dokumentmodellen.
5. Windows IME- og emoji-input skal behandles som tekstinput. Midlertidig IME-komposition må ikke oprettes som en fortrydelseshændelse, før teksten er bekræftet.

### Markeringens anchor og active end

1. Markeringen skal lagre både et anchor og en active end.
2. Anchor er den position, hvor en markering startede.
3. Active end er den position, som senest blev flyttet ved mus eller tastatur.
4. Den læste tekst og tegnede markering bestemmes af den sorterede begyndelse og slutning, også når active end ligger før anchor.
5. En udvidende handling med Shift skal beholde anchor og kun flytte active end.
6. En ikke-udvidende handling skal normalt kollapse markeringen, så anchor og active end får samme position.

## Fokus og caret

### Fokus

1. Et venstreklik i et redigerbart tekstområde skal give tekstområdet tastaturfokus.
2. Når tekstområdet har fokus og markeringen er tom, skal et synligt blinkende caret vises på den aktive position.
3. Caretet skal bruge systemets blinkindstilling og systemets caret-bredde, hvor Windows stiller disse til rådighed.
4. Når tekstområdet mister fokus, skal caretet skjules.
5. Når tekstområdet igen får fokus, skal caretet vises på den aktive position, hvis markeringen er tom.
6. En ikke-tom markering skal blive synlig, når området mister fokus, men må bruge systemets inaktive markeringsfarve.

### Caretets placering

1. Caretet skal tegnes ved den visuelle indstiksposition, som svarer til den aktive tekstposition.
2. Caretets højde skal svare til den aktuelle tekstlinjes højde.
3. Caretet skal flytte med indholdet ved scroll, ændret vinduesstørrelse, skrifttypeændring og ombrydning.
4. Efter en handling, der flytter caretet, skal programmet scrolle mindst muligt, så caretet er fuldt synligt lodret og vandret.
5. Caretet må ikke blinke oven på en ikke-tom markering.

## Museadfærd

### Enkeltklik

1. Et venstreklik på en synlig tekstposition skal kollapse en eksisterende markering ved den hit-testede position.
2. Et venstreklik mellem to tegn skal placere caretet ved den nærmeste gyldige indstiksposition. Valget mellem før og efter det ramte tegn skal følge klikpunktets vandrette halvdel.
3. Et klik i den tomme højre del af en visuel tekstlinje skal placere caretet ved slutningen af den pågældende visuelle linje.
4. Et klik under den sidste synlige tekstlinje i et almindeligt tekstafsnit skal placere caretet ved slutningen af den nærmeste efterfølgende eller foregående redigerbare tekst. Implementeringen må ikke placere caretet i en skjult formatmarkør.
5. Shift+venstreklik skal bevare den eksisterende anchor og flytte active end til den hit-testede position.
6. Hvis der ikke fandtes en markering før Shift+venstreklik, skal den daværende caret-position bruges som anchor.
7. Ctrl+venstreklik har ingen særbetydning i denne basisfunktion. Det skal udføre samme tekstplaceringshandling som et almindeligt venstreklik. Eventuel linkaktivering med Ctrl+klik kræver en særskilt specifikation.

### Trækmarkering

1. Et venstreklik efterfulgt af bevægelse med venstre museknap nede skal starte en markering med klikpositionen som anchor.
2. Under træk skal active end kontinuerligt følge den hit-testede museposition.
3. Markeringen skal opdateres, også når active end passerer anchor og markeringen skifter retning.
4. Trækmarkering skal kunne krydse afsnit og visuelle linjeskift.
5. Trækmarkering skal afsluttes, når venstre museknap slippes, også hvis musemarkøren er uden for tekstområdets klientrektangel.
6. Tekstområdet skal tage musecapture under en aktiv trækmarkering og frigive capture ved afslutning eller afbrudt capture.
7. Når brugeren trækker over den øverste eller nederste kant af klientområdet, skal visningen autoscrolle i den pågældende retning og fortsat opdatere active end.
8. Autoscrollens hastighed skal stige med afstanden uden for kanten og have en fast øvre grænse, så brugeren kan styre den.
9. Autoscroll skal stoppe ved dokumentets begyndelse eller slutning, når museknappen slippes, når capture mistes, eller når markøren vender tilbage til klientområdet.

### Dobbeltklik

1. Et dobbeltklik på tekst skal markere ordet under klikpunktet.
2. Et ord er en maksimal sammenhængende sekvens af bogstaver, tal eller tegn, som tekstsegmentering klassificerer som del af et ord.
3. Tegnsætning og symboler, der ikke er del af et ord, skal kunne markeres som deres egen sammenhængende enhed ved dobbeltklik.
4. Hvidt mellemrum mellem ord skal ikke medtages i en dobbeltklikmarkering.
5. Dobbeltklik på en tom linje skal ikke markere nabotekst. Caretet skal placeres på linjen.
6. Efter dobbeltklik skal træk med venstre museknap udvide eller formindske markeringen i hele ordgrænser, ikke tegn for tegn.

### Trippelklik

1. Et trippelklik på tekst skal markere det komplette afsnit, som klikpunktet ligger i.
2. Afsnittets afsluttende linjeskift skal medtages, hvis et sådant findes. Det gør det muligt at erstatte eller flytte afsnittet uden at efterlade et tomt afsnit.
3. Det sidste afsnit i dokumentet skal markeres uden at læse forbi dokumentets slutning.
4. Et afsnit uden synlig tekst skal markeres som sin tomme redigerbare position plus eventuelt afsluttende afsnitsskift.
5. Efter trippelklik skal træk med venstre museknap udvide eller formindske markeringen i hele afsnit.
6. Windows leverer ikke en separat besked for trippelklik. Implementeringen skal derfor tælle klik på samme måde som Windows-dobbeltklik: inden for systemets dobbeltkliktid og dobbeltklikrektangel.
7. Et klik uden for tids- eller afstandstærsklen skal starte en ny kliksekvens og må ikke udløse afsnitsmarkering.

## Tastaturinput og erstatning

### Almindeligt tekstinput

1. Bekræftet tekstinput fra tastatur, IME, emoji-panel eller clipboard skal indsættes på den aktive position.
2. Hvis markeringen ikke er tom, skal hele markeringen erstattes af den indgående tekst i én mutation.
3. Efter indsættelse skal markeringen være tom, og caretet skal stå umiddelbart efter den indsatte tekst.
4. Indsættelse af tekst må ikke fortolke almindelige Markdown-tegn som formatkommandoer. Et indtastet `*`, `_`, `[`, `]` eller andet tegn skal forblive synlig tekst, medmindre brugeren eksplicit anvender en formatkommando.
5. Kontroltegn, der ikke repræsenterer accepteret tekstinput, må ikke indsættes i dokumentet.
6. Et linjeskift indsat fra en ekstern tekstkilde skal normaliseres til dokumentets interne afsnitsrepræsentation uden at ændre den viste rækkefølge af linjer.

### Enter

1. Enter uden en markering skal indsætte ét afsnitsskift ved caret-positionen.
2. Enter med en markering skal erstatte markeringen med ét afsnitsskift.
3. Efter Enter skal caretet stå i det nye afsnit efter det indsatte afsnitsskift.
4. Shift+Enter, Ctrl+Enter og Alt+Enter har ingen påkrævet betydning i denne basisfunktion. Implementeringen må ikke bruge dem til en ikke-standard funktion uden en særskilt kravspecifikation.

### Backspace og Delete

1. Backspace med en ikke-tom markering skal slette hele markeringen.
2. Delete med en ikke-tom markering skal slette hele markeringen.
3. Backspace med en tom markering skal slette grafemklyngen umiddelbart før caretet.
4. Delete med en tom markering skal slette grafemklyngen umiddelbart efter caretet.
5. Backspace ved dokumentets begyndelse skal være en no-op.
6. Delete ved dokumentets slutning skal være en no-op.
7. Hvis en sletning fjerner et afsnitsskift, skal de to tilstødende afsnit blive ét afsnit. Caretet skal stå på den oprindelige skilleposition.
8. Ctrl+Backspace skal slette fra caretet til den forrige ordgrænse. Mellemrum eller tegnsætning mellem caretet og ordet skal indgå efter Windows' normale ordnavigation.
9. Ctrl+Delete skal slette fra caretet til den næste ordgrænse. Mellemrum eller tegnsætning mellem caretet og ordet skal indgå efter Windows' normale ordnavigation.
10. Ctrl+Backspace og Ctrl+Delete må ikke krydse dokumentets begyndelse eller slutning.
11. Shift har ingen yderligere virkning sammen med Backspace eller Delete. Markeringens eksisterende indhold følger altid reglerne ovenfor.

## Tastaturnavigation

### Fælles regler for navigation

1. Navigationstaster skal flytte caretet, når markeringen er tom.
2. Navigation med Shift skal udvide eller formindske markeringen fra anchor.
3. Navigation uden Shift fra en ikke-tom markering skal kollapse markeringen i bevægelsesretningen, uden efterfølgende ekstra bevægelse.
4. Venstreorienteret navigation, herunder Venstre, Home, Ctrl+Home og Page Up, skal kollapse til markeringens begyndelse.
5. Højreorienteret navigation, herunder Højre, End, Ctrl+End og Page Down, skal kollapse til markeringens slutning.
6. Op og Ned uden Shift fra en ikke-tom markering skal kollapse til den af de to markeringsender, der ligger nærmest den ønskede visuelle retning. Implementeringen skal vælge dette konsekvent.
7. En navigation, der rammer dokumentets grænse, skal være en no-op med hensyn til position og markering.
8. Efter vandret navigation eller redigering skal den gemte ønskede vandrette position for lodret navigation nulstilles.

### Venstre og højre pil

1. Venstre pil skal flytte ét grafem til venstre.
2. Højre pil skal flytte ét grafem til højre.
3. Ctrl+Venstre skal flytte til begyndelsen af det foregående ord eller den foregående ordgrænse.
4. Ctrl+Højre skal flytte til begyndelsen af det næste ord eller den næste ordgrænse.
5. Shift+Venstre og Shift+Højre skal udføre samme bevægelse og beholde anchor.
6. Ctrl+Shift+Venstre og Ctrl+Shift+Højre skal udføre ordbevægelse og beholde anchor.
7. Ordnavigation skal bruge samme segmenteringsregler som dobbeltklik, så mus og tastatur har identiske ordgrænser.

### Op og ned pil

1. Op og Ned skal bevæge sig mellem visuelle, ombrudte tekstlinjer, ikke kun mellem logiske afsnitslinjer.
2. Lodret navigation skal forsøge at bevare brugerens ønskede vandrette position.
3. Hvis den nye visuelle linje er kortere end den ønskede position, skal caretet placeres ved linjens slutning, men den oprindelige ønskede position skal huskes.
4. Hvis brugeren derefter flytter til en længere linje, skal caretet vende tilbage til den oprindelige ønskede position.
5. Shift+Op og Shift+Ned skal følge de samme lodrette regler og udvide markeringen.
6. Lodret navigation skal scrolle visningen mindst muligt, når målpositionen ligger uden for klientområdet.

### Home og End

1. Home skal flytte til begyndelsen af den aktuelle visuelle linje.
2. End skal flytte til slutningen af den aktuelle visuelle linje.
3. Ctrl+Home skal flytte til dokumentets første redigerbare position.
4. Ctrl+End skal flytte til dokumentets sidste redigerbare position.
5. Shift+Home, Shift+End, Ctrl+Shift+Home og Ctrl+Shift+End skal udføre den tilsvarende bevægelse og beholde anchor.

### Page Up og Page Down

1. Page Up skal flytte caretet omtrent én synlig klienthøjde op.
2. Page Down skal flytte caretet omtrent én synlig klienthøjde ned.
3. Målpositionen skal bestemmes ud fra den ønskede vandrette position på den nærmeste visuelle tekstlinje efter scrollen.
4. Visningen skal scrolle sammen med caretet.
5. Shift+Page Up og Shift+Page Down skal udføre den tilsvarende bevægelse og beholde anchor.

### Markér alt

1. Ctrl+A skal markere hele dokumentets redigerbare tekst.
2. Markeringens anchor skal sættes til dokumentets begyndelse og active end til dokumentets slutning.
3. Ctrl+A må ikke medtage skjulte Markdown-markører, hvis disse ikke er en del af den synlige tekstmodel.

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

1. Hver aliasgenvej i tabellen skal udføre nøjagtig samme handling som den tilsvarende primære genvej.
2. Ctrl+Shift+Z kan understøttes som et ekstra alias for gentag, men må ikke erstatte Ctrl+Y.
3. Ctrl+U er ikke påkrævet, fordi understregning ligger uden for denne specifikations formateringsomfang. En senere specifikation, der tilføjer understregning, skal tildele Ctrl+U til denne kommando.

## Clipboard

### Kopiér

1. Ctrl+C skal kopiere den aktuelle markering til Windows Clipboard, hvis markeringen ikke er tom.
2. En kopioperation må ikke ændre dokumentet, markeringen eller caret-positionen.
3. Ctrl+C med en tom markering skal være en no-op og må ikke rydde eller overskrive det eksisterende clipboard-indhold.
4. Programmet skal mindst lægge markeringens tekst på clipboard som `CF_UNICODETEXT`.
5. Tekst i `CF_UNICODETEXT` skal være NUL-termineret og bruge CRLF som linjeskift på clipboard, hvor markeringen indeholder afsnitsskift.
6. Tekst uden visuelle formatmarkører skal kopieres som den tekst, brugeren har markeret. Skjulte interne Markdown-markører må ikke medtages.

### Klip

1. Ctrl+X skal kopiere og derefter slette den aktuelle markering som én brugerhandling.
2. Ctrl+X med en tom markering skal være en no-op og må ikke ændre clipboard eller dokument.
3. Klip i et skrivebeskyttet dokument skal være en no-op. Det må ikke ændre clipboard.
4. Hvis clipboard ikke kan åbnes eller tekst ikke kan placeres på clipboard, må dokumentet ikke slettes.
5. Et vellykket klip skal kunne fortrydes som én fortrydelseshændelse. Fortrydelse skal gendanne både tekst og den oprindelige markering.

### Indsæt

1. Ctrl+V skal indsætte den bedste tilgængelige tekstrepræsentation fra Windows Clipboard.
2. Denne basisfunktion skal mindst understøtte `CF_UNICODETEXT`.
3. Hvis clipboard ikke har en understøttet tekstrepræsentation, skal Ctrl+V være en no-op.
4. Indsætning skal erstatte en ikke-tom markering i én mutation.
5. Efter indsætning skal caretet stå efter den indsatte tekst, og markeringen skal være tom.
6. Clipboard-tekst med CRLF, CR eller LF skal normaliseres konsekvent til dokumentets interne linjeskiftsrepræsentation.
7. Indsætning skal ikke implicit fortolke Markdown-tegn, oprette links eller anvende fed eller kursiv.
8. En vellykket indsætning skal være én fortrydelseshændelse, uanset tekstens længde eller antal afsnit.

## Fortryd og gentag

### Grundregler

1. Ctrl+Z skal fortryde den seneste fortrydelsesberettigede brugerhandling.
2. Ctrl+Y skal gentage den senest fortrudte brugerhandling.
3. Hvis der ikke er noget at fortryde eller gentage, skal den pågældende genvej være en no-op.
4. Ctrl+Shift+Z kan understøttes som et ekstra alias for gentag, men Ctrl+Y er påkrævet.
5. Hver fortrydelsespost skal mindst kunne gendanne den erstattede tekst, den indsatte tekst, markeringen før handlingen og markeringen efter handlingen.
6. Fortrydelse og gentag må ikke afhænge af, at layout, ombrydning eller vinduesbredde er uændret.

### Hændelsesgrænser

1. Indsætning fra clipboard, klip, sletning af en markering, Enter og anvendelse eller fjernelse af formatering skal hver være én separat fortrydelseshændelse.
2. En sammenhængende sekvens af almindelig tastaturindtastning kan samles til én fortrydelseshændelse.
3. Sammenlægning af indtastning må kun ske, når indtastningen er sammenhængende på samme caret-position og der ikke er sket navigation, markering, indsætning, formatering, fokusændring eller anden mutation imellem.
4. Et afsnitsskift skal altid afslutte en påbegyndt sammenlægning.
5. En sletningssekvens kan samles, når alle sletninger har samme retning og er sammenhængende. Backspace og Delete må ikke samles i samme post.
6. Caret-bevægelse, museklik, markering, Ctrl+V, Ctrl+X, Ctrl+Z, Ctrl+Y og formatkommandoer skal afslutte en påbegyndt sammenlægning.
7. En ny redigering efter én eller flere fortrydelser skal rydde gentagelseshistorikken.
8. En no-op må ikke oprette en fortrydelsespost.

### Tilstand efter fortryd og gentag

1. Fortrydelse skal gendanne den præcise markering, der fandtes før den fortrudte handling.
2. Gentag skal gendanne den præcise markering, der fandtes efter den gentagne handling.
3. Efter fortryd eller gentag skal visningen opdateres, og den aktive ende af markeringen skal gøres synlig.
4. Formatering, som fortrydes eller gentages, skal gendannes uden at ændre den underliggende synlige tekst.

## Grundlæggende inlineformatering

### Fælles regler

1. Følgende kommandoer er omfattet: fed, kursiv og link.
2. Kommandoerne skal kunne udløses fra brugergrænsefladen og via standardgenvejene Ctrl+B, Ctrl+I og Ctrl+K.
3. Den samme kommando skal have samme observerbare resultat, uanset om den kommer fra tastatur, menu eller værktøjslinje.
4. Formatering skal anvendes på den synlige tekstmodel. Implementeringen kan ændre intern Markdown, men markering, caret og den viste tekst må følge reglerne her.
5. En formatkommando på en ikke-tom markering skal være én fortrydelseshændelse.
6. En formatkommando må ikke flytte markeringens begyndelse eller slutning på den synlige tekst.
7. Når en formatkommando afsluttes, skal den oprindelige synlige tekst stadig være markeret.
8. Delvis overlap med eksisterende formatløb skal splitte løbene ved markeringsgrænserne. Tekst uden for markeringen må ikke miste eller få formatering.
9. Flere inlineformater kan eksistere på samme tekst. Fed og kursiv skal derfor kunne kombineres i vilkårlig rækkefølge.

### Fed og kursiv på en markering

1. Ctrl+B eller kommandoen Fed skal undersøge hele den ikke-tomme markering.
2. Hvis hver eneste tegnposition i markeringen allerede er fed, skal kommandoen fjerne fed fra hele markeringen.
3. Hvis mindst én tegnposition i markeringen ikke er fed, skal kommandoen anvende fed på hele markeringen.
4. Ctrl+I eller kommandoen Kursiv skal følge de samme regler for kursiv.
5. En markeringsgrænse midt i et eksisterende fedt eller kursivt løb skal bevare formateringen uden for markeringen.
6. Fed og kursiv skal ændre udseendet straks efter kommandoen uden at ændre tekstens rækkefølge eller den aktive markering.

### Fed og kursiv ved et tomt caret

1. Ctrl+B eller Ctrl+I ved et tomt caret skal slå den pågældende egenskab for efterfølgende indtastning til eller fra.
2. Kommandoen må ikke indsætte tomme formatløb, synlige symboler eller skjult tekst i dokumentet.
3. Den valgte egenskab skal gælde for efterfølgende indtastet tekst ved caretet, indtil brugeren slår den fra, flytter caretet til en tekstposition med en entydig anden formattilstand eller erstatter den med en eksplicit formatkommando.
4. Backspace over nyligt indtastet formateret tekst skal slette tekst efter de almindelige sletteregler. Den må ikke efterlade et tomt formateringsobjekt.
5. En ændring af en tom-carets skriveformat skal kunne fortrydes og gentages som en selvstændig tilstandsændring, hvis den efterfølgende påvirker indtastning. Implementeringen må alternativt registrere den sammen med den første efterfølgende tekstindsættelse, hvis Ctrl+Z da gendanner den observerbare tilstand korrekt.

### Link på en markering

1. Ctrl+K eller kommandoen Indsæt link kræver en ikke-tom tekstmarkering.
2. Ved en ikke-tom markering skal programmet bede brugeren om en linkdestination i en modal eller tilsvarende fokuseret dialog.
3. Annullerer brugeren dialogen, må dokument, markering, caret og fortrydelseshistorik ikke ændres.
4. Bekræfter brugeren en gyldig destination, skal den markerede synlige tekst blive linktekst med den angivne destination.
5. Linkkommandoen må ikke ændre den markerede synlige tekst til URL'en. Den oprindelige synlige tekst skal bevares som linkets etiket.
6. Anvendes linkkommandoen på en markering, der helt ligger i ét eksisterende link, skal dialogen vise den eksisterende destination og en bekræftelse skal opdatere denne destination uden at dele eller duplikere linkteksten.
7. Anvendes linkkommandoen på en markering, som delvist overlapper et eksisterende link, skal den markerede tekst blive ét sammenhængende link med den bekræftede destination. Umarkerede dele af det gamle link skal bevares med deres oprindelige destination.
8. En tom destination skal ikke oprette et link. Dialogen skal forhindre bekræftelse eller behandle handlingen som annulleret.
9. Ctrl+K ved et tomt caret skal åbne dialogen, men må ikke ændre dokumentet, medmindre en særskilt funktion for indsættelse af nyt link er specificeret.
10. Linkformatering skal kunne kombineres med fed og kursiv.
11. Et link må ikke indeholde delvist overlappende links. Implementeringen skal splitte eller sammenlægge interne formatløb, så linkintervaller altid er indlejrede eller adskilte.

### Fjernelse af link

1. Når caretet eller hele markeringen ligger i ét link, skal en brugergrænsefladekommando kunne fjerne linkets destination og beholde dets synlige tekst.
2. Linkfjernelse skal være én fortrydelseshændelse.
3. Ctrl+K er ikke påkrævet som genvej for linkfjernelse, fordi Ctrl+K er reserveret til oprettelse eller redigering af link.

## Scroll og synlighed

1. Musehjulet skal scrolle indholdet lodret uden at flytte caret eller ændre markeringen.
2. Shift+musehjul kan scrolle vandret, hvis dokumentet har vandret scroll. Det er ikke påkrævet for dokumenter uden vandret scroll.
3. Scrollbar-bevægelser skal ikke ændre caret eller markeringen.
4. En aktiv trækmarkering skal fortsat fungere under autoscroll.
5. Efter en redigerings-, navigations-, fortrydelses- eller gentagelseshandling skal programmet kun scrolle, hvis active end ikke er tilstrækkeligt synlig.
6. Brugeren skal kunne scrolle væk fra caretet uden at programmet straks tvinger visningen tilbage. Automatisk tilbage-scroll må først ske ved en efterfølgende handling, der kræver synligt caret.
7. En ændring af ombrydning ved vinduesresize må bevare den aktive tekstposition og markeringens interval, selv om deres skærmkoordinater ændres.

## Fejl, skrivebeskyttelse og grænsetilfælde

1. Alle redigerende kommandoer skal være no-op i et skrivebeskyttet dokument. Navigation, markering, Ctrl+A og Ctrl+C skal fortsat fungere.
2. I skrivebeskyttet tilstand må Ctrl+X og Ctrl+V ikke ændre clipboard, dokument eller fortrydelseshistorik.
3. En clipboard-fejl må ikke slette tekst, ændre markering eller oprette en fortrydelsespost.
4. En mislykket intern mutation, parseropdatering eller relayout efter en redigeringshandling skal rulle hele handlingen tilbage. Dokumenttekst, formatering, markering og fortrydelseshistorik skal forblive konsistente.
5. Programmet skal afvise eller sikkert begrænse tekstinput, hvis operationen overskrider dokumentets tilladte størrelse. En afvisning må ikke slette den eksisterende markering.
6. En tom dokumentmodel skal have én gyldig redigerbar position ved offset nul.
7. Ctrl+A i et tomt dokument skal resultere i en tom markering ved offset nul og må ikke fejle.
8. Kopiering af en markering, der begynder eller slutter på et afsnitsskift, skal bevare netop de markerede afsnitsskift på clipboard.
9. Markering, copy, cut, paste, sletning og navigation skal fungere på tværs af tekst med dansk `æ`, `ø` og `å`, kombinerende diakritiske tegn, højre mod venstre-tekst og emoji.
10. En ændring i dokumentlayout må aldrig konvertere en eksisterende ikke-tom markering til et andet tekstinterval.

## Acceptance criteria

Implementeringsagenten skal levere automatiserede enhedstests for modelreglerne
og en Windows-manuel testliste for hit-test, fokus, clipboard og IME. Hvert
kriterium skal verificeres med et konkret observerbart resultat. En test, der
kun viser, at en operation ikke fejlede, opfylder ikke kriteriet. Følgende
kriterier skal være opfyldt.

### Caret og mus

1. Klik mellem `a` og `b` i teksten `ab` placerer et tomt caret mellem tegnene.
2. Klik til højre for den sidste synlige glyph på en ombrudt linje placerer caretet ved linjens slutning.
3. Shift+klik fra position 2 til position 8 skaber markeringen `[2, 8)`. Shift+klik derefter på position 1 skaber `[1, 2)` med anchor på position 2 og active end på position 1.
4. Træk fra et afsnit til det næste markerer alle tekstpositioner mellem start og slutpunkt, også når trækretningen vendes.
5. Dobbeltklik på `word,` markerer `word` uden komma og uden mellemrum. Dobbeltklik direkte på kommaet markerer kommaet.
6. Trippelklik i et afsnit med afsluttende afsnitsskift markerer afsnittets tekst og skiftet, men ikke tekst i næste afsnit.
7. Træk efter dobbeltklik flytter markeringsgrænsen i ordgrænser. Træk efter trippelklik flytter den i afsnitsgrænser.
8. Træk over den nederste vindueskant starter autoscroll og udvider markeringen, indtil brugeren slipper museknappen eller dokumentets slutning nås.

### Unicode og input

1. Venstre pil, højre pil, Backspace og Delete behandler `e` plus kombinerende accent som én grafemklynge.
2. Venstre pil, højre pil, Backspace og Delete behandler et regionalindikator-par som én grafemklynge.
3. Backspace efter en emoji-ZWJ-sekvens sletter hele den viste emoji og efterlader gyldig tekst.
4. Indtastning af `*tekst*` viser de indtastede asterisker som tekst og anvender ikke kursiv automatisk.
5. Indtastning med en aktiv markering erstatter hele markeringen og placerer caretet efter den indsatte tekst.
6. Enter erstatter en aktiv markering med ét afsnitsskift.

### Sletning og navigation

1. Backspace ved dokumentets begyndelse og Delete ved dokumentets slutning er no-op og opretter ikke fortrydelseshistorik.
2. Backspace eller Delete med markeringen `bcd` i `abcde` efterlader `ae` og et caret mellem `a` og `e`.
3. Ctrl+Backspace og Ctrl+Delete stopper ved samme ordgrænser, som dobbeltklik og Ctrl+pil bruger.
4. Venstre pil fra markeringen `[3, 7)` kollapser til position 3. Højre pil kollapser til position 7. Ingen af dem flytter en ekstra grafem.
5. Shift+Ctrl+Højre udvider markeringen til næste ordgrænse uden at ændre anchor.
6. Gentagen Ned gennem korte og lange ombrudte linjer gendanner den oprindeligt ønskede vandrette position, når en tilstrækkeligt lang linje nås.
7. Home og End flytter til henholdsvis start og slutning af den visuelle linje. Ctrl+Home og Ctrl+End flytter til dokumentets grænser.
8. Ctrl+A markerer hele den synlige redigerbare dokumenttekst.

### Clipboard

1. Ctrl+C med en tom markering bevarer den eksisterende clipboard-tekst uændret.
2. Ctrl+C med en flerlinjet markering udstiller den markerede tekst som `CF_UNICODETEXT` med CRLF-linjeskift og NUL-terminering.
3. Ctrl+X sletter ikke dokumenttekst, hvis clipboard-operationen fejler.
4. Ctrl+V med `CF_UNICODETEXT` erstatter markeringen som én mutation og normaliserer CRLF, CR og LF ens.
5. Ctrl+V med et clipboard uden understøttet tekstformat er en no-op.

### Fortryd og gentag

1. Indtastning af en sammenhængende række tegn kan fortrydes som én handling, når der ikke ligger navigation eller anden handling imellem.
2. En indsætning fra clipboard fortrydes som én handling, også når den indeholder flere afsnit.
3. Ctrl+Z efter Ctrl+X gendanner den slettede tekst og den oprindelige markering.
4. Ctrl+Y efter Ctrl+Z genskaber både dokumentets indhold og markeringen efter den oprindelige handling.
5. Efter Ctrl+Z og derefter ny indtastning er Ctrl+Y en no-op.
6. No-op-handlinger ved dokumentets grænser opretter ikke fortrydelsesposter.

### Formatering

1. Ctrl+B på en umarkeret tekstsekvens gør hele sekvensen fed uden at ændre den markerede tekst eller markeringens grænser.
2. Ctrl+B på en fuldt fed markering fjerner kun fed fra den markerede tekst.
3. Ctrl+B på en markering, der delvist er fed, gør hele markeringen fed og bevarer formateringen uden for markeringen.
4. Ctrl+I følger de samme regler for kursiv, også når teksten samtidig er fed.
5. Ctrl+B ved et tomt caret påvirker efterfølgende indtastning, men indsætter ingen tom eller synlig markup.
6. Ctrl+K med markeret tekst og en bekræftet destination bevarer etiketteksten og anvender destinationen som link.
7. Annullering af linkdialogen ændrer hverken dokument, markering eller fortrydelseshistorik.
8. Opdatering af destinationen for en markering helt i et eksisterende link bevarer linkets tekst og skaber ikke overlappende links.
9. Ctrl+Z og Ctrl+Y gendanner og gentager hver formatkommando som en enkelt handling.

### Fokus, scroll og skrivebeskyttelse

1. Caretet vises ved fokus, skjules ved fokus-tab og følger teksten ved scroll.
2. Musehjul og scrollbar ændrer ikke markeringens tekstinterval.
3. Når en navigation flytter caretet uden for synligt område, bliver caretet gjort synligt med mindst mulig scroll.
4. I skrivebeskyttet tilstand virker markering og Ctrl+C, mens Ctrl+X og Ctrl+V ikke ændrer clipboard, dokument eller fortrydelseshistorik.
5. Resize og ombrydning bevarer samme tekstinterval for en eksisterende markering.

## Implementeringsstatus i MarkDownIt

Dette afsnit er ikke en lempelse af kravene ovenfor. Det registrerer, hvilke dele der er koblet til den nuværende editor og hvilke dele der fortsat skal implementeres, før acceptance criteria må erklæres opfyldt.

### Leveret i editorlaget

- Ctrl+B og Ctrl+I går gennem de samlede formatkommandoer, så markeringens eksisterende formatstatus bruges ved til- og frakobling.
- Ctrl+B og Ctrl+I ved tomt caret gemmer en pending-formattilstand. Den næste indtastede tekst omsluttes i samme redigeringshandling, og der indsættes ikke et tomt formatpar ved caretet.
- Ctrl+Backspace og Ctrl+Delete sletter via de samme ordgrænsefunktioner som Ctrl+Venstre og Ctrl+Højre.
- Ctrl+Insert kopierer, Shift+Insert indsætter, og Shift+Delete klipper efter de tilsvarende Ctrl-genveje.
- Klipning sletter først dokumenttekst efter en succesfuld clipboard-operation.
- Indsætning over en eksisterende markering registreres som én `RecordAndApply`-mutation med én undo-post.
- `CF_UNICODETEXT` eksporteres med CRLF-linjeskift, og indsat clipboardtekst normaliseres til LF.
- Ctrl+K bruger linkdialogen og opretter ikke et tomt link, hvis markeringen er tom.
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
4. Den endelige Windows acceptance-test skal køres med MSVC og Windows SDK.
   En Linux- eller WSL-syntakskontrol kan ikke erstatte denne build-gate.

## Udvidede krav: dokumentbevidst redigering og søgning

Dette afsnit er normativt. Det beskriver funktioner, som skal følge den
samme caret-, markerings-, clipboard- og undo-model som resten af editoren.
Det ændrer ikke afgrænsningen af automatisk Markdown-formatering ovenfor.
Kravene til Tab og Enter beskriver tastens dokumentbevidste handling, ikke en
automatisk omskrivning af almindeligt tekstinput.

### Drag and drop af tekst

1. Et tryk i en eksisterende markering starter ikke en ny markering. Hvis
   markøren flyttes mindst systemets drag-tærskel, flyttes hele markeringen.
2. Et drop inden for den valgte tekst er en no-op. Teksten må ikke duplikeres
   eller slettes.
3. Et drop før eller efter markeringen flytter den valgte kildeafstand én gang.
   Markeringen flyttes med teksten og forbliver aktiv efter operationen.
4. Drag-operationen er én undo-handling. Ctrl+Z gendanner både dokumenttekst og
   den oprindelige markering, og Ctrl+Y genskaber flytningen.
5. Drag må ikke flytte skjulte Markdown-markører til en ugyldig caret-position.
   Kildeintervaller skal være gyldige UTF-8-grænser, og operationen skal
   respektere den eksisterende synlighedsmodel.
6. Mouse capture frigives ved venstre museknap-op, også hvis drop-positionen
   ligger uden for viewporten eller operationen bliver afvist.

### Særlige tastaturregler for dokumentelementer

1. Enter i en almindelig paragraf opretter det almindelige afsnitsskift, som
   editorens paragrafmodel definerer.
2. Enter i en kodeblok indsætter ét LF-tegn og indsætter ikke en ekstra tom
   paragraf uden for kodeblokken.
3. Enter i en aktiv liste fortsætter samme liste med samme listetype. Enter på
   et tomt listepunkt afslutter listepunktet uden at oprette en ny tom linje i
   listen.
4. Enter i en tabelcelle er en no-op, fordi en newline ellers bryder tabellens
   Markdown-række.
5. Tab i en tabel flytter til næste celle. Shift+Tab flytter til forrige celle.
   Ved dokumentets første eller sidste celle er handlingen en no-op, medmindre
   en separat tabelindsættelseshandling eksplicit er tilgængelig.
6. Tab i en kodeblok indsætter fire ASCII-mellemrum. Shift+Tab fjerner én
   kodeindrykning, når linjen har den tilsvarende indrykning.
7. Tab uden for tabel og kodeblok bruger listeindrykning på liste-linjer og
   almindelig indrykning på andre linjer. Shift+Tab udfører den tilsvarende
   udrykning.
8. Alle dokumentelementregler skal oprette højst én undo-post pr. tastetryk.
   En no-op må ikke oprette en undo-post.

### Linkaktivering med musen

1. Et klik på et link i skrivebeskyttet tilstand aktiverer linket.
2. Ctrl+klik på et link i redigeringstilstand aktiverer linket. Et almindeligt
   klik i redigeringstilstand placerer caret eller ændrer markering, så links
   ikke blokerer tekstredigering.
3. Eksterne links må kun åbnes for understøttede sikre skemaer, herunder
   `http`, `https` og `mailto`.
4. Interne ankerlinks flytter viewporten til det tilsvarende dokumentafsnit.
   De må ikke starte et eksternt program.
5. Linkaktivering må ikke ændre dokumenttekst, dirty-state, markering eller
   undo-historik.
6. Hvis hit-test rammer skjulte linkmarkører, skal linket stadig kunne
   aktiveres uden at placere caret på en skjult kildeposition.

### Søgning og erstatning

1. Ctrl+F åbner Find-dialogen. Dialogen indeholder søgetekst, Match case,
   Whole word, Find Next og Annuller.
2. Ctrl+H åbner Find and Replace-dialogen med søgetekst, erstatningstekst,
   Match case, Whole word, Find Next, Replace, Replace All og Annuller.
3. Søgning er som standard case-insensitiv. Whole word må kun matche ved
   Unicode-ordgrænser, og en del af et længere ord må ikke matches.
4. Find Next fortsætter fra den aktive caret-position, markerer hele matchen og
   starter forfra ved dokumentets begyndelse efter sidste match.
5. Empty query er en no-op. Find uden match ændrer hverken dokument,
   markering eller undo-historik.
6. Replace erstatter det næste match som én tekstmutation og markerer den
   indsatte tekst. Replace i skrivebeskyttet tilstand er en no-op.
7. Replace All erstatter alle ikke-overlappende match som én tekstmutation og
   én undo-post. Ctrl+Z gendanner hele dokumentteksten i én handling.
8. Erstatning skal bevare UTF-8, grafemgrænser og gyldige source offsets.
   Søgning må ikke skabe caret-positioner midt i en UTF-8-sekvens.
9. Dialogen annulleres uden sideeffekter. Den skal ikke ændre query,
   selection, dokument eller undo-historik, før brugeren bekræfter en handling.

### Acceptance criteria for de udvidede funktioner

1. Portable tests dokumenterer case-regler, whole-word-regler, tom query,
   non-overlapping matches, Replace All og tekstflytning før og efter en
   markering med forventet tekst og ny startposition.
2. Portable tests dokumenterer Tab-navigation til og fra en tom tabelcelle,
   Shift+Tab ved første celle, Tab ved sidste celle, Enter i tabel, Enter i
   kodeblok og listefortsættelse. Testene skal kontrollere både destinationer
   og no-op-resultater.
3. Portable tests afviser kilde- og drop-positioner midt i en kombinerende
   grafemklynge, et regionalindikator-flag eller en emoji-ZWJ-sekvens og
   accepterer flytning af hele grafemklyngen.
4. Windows CI bygger både appen og testbinary med MSVC og kører hele testpakken.
5. Windows acceptance-testen verificerer Ctrl+F, Ctrl+H, Replace, Replace All,
   undo/redo, drag-and-drop, mouse capture, linkaktivering og alle dokument-
   tastaturregler med en rigtig Win32-window message loop.
6. Linux eller WSL må bruges til statisk kontrol, men kan ikke erstatte Windows
   runtime-verifikation eller MSVC-buildet.
