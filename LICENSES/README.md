# Retained upstream licence texts

These files are verbatim copies of component notices from the inspected dependency distributions and Python wheels. `NOTICE-INVENTORY.json` records their hashes and source roles. No software binaries, private keys, proprietary SDK headers or first-party numerical implementation are copied into this directory.

The texts do not select a licence for Pentarnion's first-party code. Keep each upstream copyright, licence and exception intact. The dependency lock and actual binary inventory determine which optional tools/components are used; the presence of a retained licence alone is not a declaration that its code is linked into every build.

`python/LICENSE.txt` includes the original **Additional Conditions for this Windows binary build**, addressing Microsoft Distributable Code. Those conditions apply to the Microsoft components, not to Python or Pentarnion as a whole. Do not call Microsoft's runtime DLLs open source, relicense them under the first-party project licence, or remove their notices.

MATLAB is an optional external prerequisite. No MATLAB SDK headers, import libraries, proprietary runtime DLLs or MATLAB licence files are supplied here. Its own applicable licence remains separate from any licence granted for Pentarnion's adapter source.

These retained texts complement `THIRD_PARTY_NOTICES.md` and `docs/REDISTRIBUTION.md`; they do not replace corresponding-source and relinking arrangements for binary releases.
