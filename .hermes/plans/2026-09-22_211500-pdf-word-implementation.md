# Implementeringsplan: PDF- og Word-understoettelse (milepæl 1)

Spec: `.hermes/plans/2026-09-22_203000-pdf-word-support.md`.

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
| T1 | Vendoring (miniz, pugixml, pdfio) + CMake + smoke test | i gang |
| T2 | Oracle-harness: fixtures, goldens, checkers, CI-job | i gang |
| T3 | DOCX-import: zip, document.xml, styles, numbering, tabeller, links | planlagt |
| T4 | DOCX-eksport: OPC-pakke, stilarter, nummerering, tabeller | planlagt |
| T5 | PDF-eksport: layout/paginering, standard-14 fonte, WinAnsi | planlagt |
| T6 | UI: filmenu/ribbon, gem-dialog, kompatibilitetsrapport | planlagt |
| T7 | Robusthed: 200 sider, fejlbeskeder, CFB-detektion, krypteret PDF | planlagt |

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
