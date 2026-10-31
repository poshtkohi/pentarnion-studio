# LibTIFF 4.7.2 without optional JBIG

This is the configuration and patch used by the tested Windows research-preview
runtime. The public Git tree contains the editable recipe, retained upstream
licence, header configuration and header-install patch. It contains no DLL or
downloaded upstream archive.

Download [tiff-4.7.2.tar.gz](https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz)
and verify SHA256
`672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a`.
Place it at `sources/tiff-4.7.2.tar.gz` beside this README before invoking
`rebuild_libtiff.py --help`. Supply matching codec development libraries and an
explicit UCRT64 toolchain. Build output must be new and outside this directory.

The recipe disables JBIG and preserves the other preview codec configuration.
It does not remove licence obligations for TIFF or those other codecs. A public
binary release must verify the actual import closure and retain matching sources,
patches and notices. Do not substitute the canonical development TIFF DLL, which
has a different optional-codec dependency closure.

`SHA256.json` inventories the files actually present in this public recipe, not
the former private full vendor bundle. The upstream source identity and original
patch provenance are recorded separately in `proofs/source-provenance.json`.
