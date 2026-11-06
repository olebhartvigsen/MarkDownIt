# Implementeringsplan: PDF- og Word-understoettelse (milepæl 1)

Spec: `references/office-import-export-spec.md` (flyttet hertil i `9d3d068`).

## Arkitektur

Zero-dependency som resten af projektet. Alt statisk linket, ingen ny runtime.

| Lag | Indhold | Placering |
|-----|---------|-----------|
| Container | miniz 3.0.2 (ZIP + zlib-API til FlateDecode) | third_party/miniz |
| XML | pugixml 1.15 (OOXML-dele) | third_party/pugixml |
| PDF | pdfio 1.6.5 (skrivning, standard-14 fonte, TTF-indlejring) | third_party/pdfio |
| Interop | docx ind/ud, pdf ud, kompatibilitetsrapport | src/office |
| UI | filmenu/ribbon, dialoger, advarsler | src/app.cpp, src/ribbon.xml |

Interop-laget er portabelt C++ uden Windows-headere. Det kan derfor bygges og
testes lokalt og i CI-jobbet uden GUI, og det er den del der måles mod
referencefilerne.

`zlib.h`-shim i third_party/miniz lader pdfio bruge miniz som zlib, så der ikke
kommer endnu en afhængighed.

## Målemetode

Strategien er den samme som for mermaid-rendereren: en uafhængig reference
producerer facit, og vores implementering måles mod den, ikke mod en skærmkopi.

1. **Fixtures.** `tools/office-oracle/make_fixtures.py` genererer et korrekt
   Word-dokument og en korrekt PDF med uafhængige producenter (python-docx,
   reportlab). Genereringen er byte-deterministisk, og filerne er committet i
   `tests/office/fixtures/`.
2. **Goldens.** Uafhængige læsere (python-docx, pypdf) udtrækker
   indholdsmodellen til JSON i `tests/office/golden/`, plus den forventede
   markdown som import-mappingen skal give.
3. **Import måles** ved at vores C++-importer kører på fixturen og skal give
   samme markdown som oraklets mapping.
4. **Eksport måles** ved at python-docx og pypdf åbner vores eksporterede filer
   i CI og sammenligner mod goldens. Altså en fremmed læser, ikke vores egen.
5. **Drift-gate.** Et CI-job genskaber fixtures og goldens og fejler hvis de
   committede filer ikke kan reproduceres byte-identisk. Committede goldens
   redigeres aldrig i hånden.

## Gates

| Gate | Hvad den beviser | Hvor |
|------|------------------|------|
| Lokal zig-compile | Portablet lag kompilerer og tests kører | Container |
| CI windows-2022 | MSVC-compile, alle tests, exe-artefakt | GitHub Actions |
| CI ubuntu oracle | Fixtures/goldens kan genskabes, intet drift | GitHub Actions |
| CI ubuntu verify-export | Vores eksporterede docx/pdf læses korrekt af fremmed læser | GitHub Actions |
| Manuel | Word/PDF-læser åbner filerne uden reparation | Windows-maskinen |

## Opgaver

| # | Opgave | Status |
|---|--------|--------|
| T1 | Vendoring (miniz, pugixml, pdfio) + CMake + smoke test | færdig |
| T2 | Oracle-harness: fixtures, goldens, checkers, CI-job | færdig |
| T3 | DOCX-import: zip, document.xml, styles, numbering, tabeller, links | færdig |
| T4 | DOCX-eksport: OPC-pakke, stilarter, nummerering, tabeller | færdig |
| T5 | PDF-eksport: layout/paginering, standard-14 fonte, WinAnsi | færdig |
| T6 | UI: filmenu/ribbon, gem-dialog, kompatibilitetsrapport | færdig |
| T7 | Robusthed: 200 sider, fejlbeskeder, CFB-detektion, krypteret PDF | færdig |

## Uden for milepæl 1

OCR af scannede PDF'er, PDF/A, adgangskodebeskyttet eksport, skrivning af
legacy `.doc`, PDF-import og de tunge Word-funktioner (ændringssporing,
kommentarer, fodnoter, indholdsfortegnelse). Disse vises i stedet som
kompatibilitetsadvarsler, så brugeren får en ærlig besked i stedet for tavs
datatab. Rækkefølgen for milepæl 2 besluttes når milepæl 1 er grøn.

## Verifikationsgrænser

- Container-miljøet har ingen Windows-session og intet `/mnt/c`, så jeg kan ikke
  lægge en bygget exe ind på brugerens maskine eller se GUI'et. Den manuelle
  del er brugerens.
- MSVC er den rigtige compile-gate. zig-clang accepterer kode MSVC afviser, så
  en grøn lokal compile er ikke et bevis.
- PDF- og docx-filerne valideres maskinelt, men "åbner Word den uden
  reparationsdialog" er en manuel kontrol.


---

## Lukket (31. oktober 2026)

Alle syv opgaver er bygget og gate't af CI. T1 og T2 står ikke længere
som "i gang", og T3 til T7 er ikke længere "planlagt": `src/office/`
rummer import, eksport og broen imellem, og office-oracle-jobbet i
`.github/workflows/build.yml` genskaber fixtures og goldens og måler
eksporten med fremmede læsere (python-docx, pypdf).

To præciseringer til teksten ovenfor:

- Drift-gaten fejler på indholdsdrift, men behandler forskelle der
  alene kommer fra komprimeringsværktøjet som advarsler
  (`tools/office-oracle/check_drift.py`).
- De fire åbne punkter (sideopsætning over round-trip, PDF/A, billeder
  i modellen, advarsler om sektionsskift) er stadig åbne på begge
  platforme og registreret i `references/macos-port-status.md` og i
  gap-closure-planen fra 23. oktober.
