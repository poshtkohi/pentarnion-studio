Native workbook document component

The Qt table, worksheet selector, bounded asynchronous loading and rectangular
clipboard presentation follow the GTG Cell Studio DatasetPreview design. This
component adds an independent editable document/model and native preservation
codec; it does not share or execute either Studio's scientific engine.

Implemented: UTF-8 CSV/TSV and XLSX, sparse typed cells, worksheet selection,
rectangular quoted-TSV paste/copy, bounded undo/redo, explicit Save/Save As,
active-sheet CSV/TSV export, dirty-state reporting and formula-bar entry.
Loading uses a two-worker pool and never accesses widgets from a worker.

XLSX saves retain every unedited ZIP-part payload, original cell style attributes,
other worksheets, stored formulas, shared strings and opaque parts. Touched sheet
XML and calculation metadata are updated; the stale calc-chain is removed and
the workbook is marked for recalculation by a spreadsheet application. Studio
does not evaluate formulas or reproduce Excel's display formatting. Shared/array/
data-table formula ranges are protected against partial edits. Digitally signed
packages and legacy XLS editing are not supported. No macros or external links
are executed. No Microsoft Excel installation is required.

Limits:16 MiB input/output,64 MiB expanded ZIP,4,096 package parts,16 MiB per
part,64 worksheets,100,000 retained cells,8 Mi-characters of cell text,65,536
characters per cell,2 Mi-characters per edit and16 undo operations. Oversized or
malformed files fail instead of being truncated and saved. Native Excel row/
column coordinate bounds are retained. Original file hashes are checked before
and during atomic replacement; failed saves preserve original files and edits.

Dependencies: Qt6 Core/Gui/Widgets/Xml, existing MiniZip/zlib, C++20.
No tests, builds or desktop graphics were executed by the implementing agent.
