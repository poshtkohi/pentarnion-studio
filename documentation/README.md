# Documentation

The editable Quick Start and Theory and Programming Language Fundamentals sources
are in `source/`. They describe the Studio 0.2 source profile. This public authoring
copy omits references to unpublished private manuscripts; the mathematical
conventions remain documented by `language.txt`, `specs/` and the implementation.

Previously rendered DOCX/PDF files are deliberately omitted until regenerated from
this public text and reviewed page by page. No unpublished paper or personal dataset
is included here. This source curation is not a numerical validation of every example.

To create editable DOCX files, install Python, `python-docx` and `lxml`, then run
`python source/build_documents.py`. Outputs go to `source/output/`. Equations remain
native OMML. Export the resulting files to PDF with an appropriately licensed Word
or LibreOffice installation; inspect every page and refresh contents/cross-reference
fields after edits. No proprietary renderer or Codex runtime is required by Studio.

To check all complete listings against a built matching Studio checker, use
`python source/check_examples.py --checker PATH_TO_PENTA_CHECK`. Run generated
examples only after reviewing them and selecting appropriate CPU/time limits.
