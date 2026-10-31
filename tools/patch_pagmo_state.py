"""Idempotent friend-only adapter patch for pinned Pagmo 2.19.1 headers."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
declaration = "namespace penta::evolution::session_detail { struct StateAccess; }\n"
for name in ("sade", "cmaes", "sga", "nsga2", "cstrs_self_adaptive"):
    path = root / "include/pagmo/algorithms" / (name + ".hpp")
    source = path.read_text(encoding="utf-8")
    if "friend struct penta::evolution::session_detail::StateAccess;" in source:
        continue
    if "namespace pagmo" not in source or "friend class boost::serialization::access;" not in source:
        raise RuntimeError("Unexpected pinned Pagmo header: " + name)
    source = source.replace("namespace pagmo", declaration + "namespace pagmo", 1)
    source = source.replace("friend class boost::serialization::access;", "friend class boost::serialization::access;\n    friend struct penta::evolution::session_detail::StateAccess;")
    path.write_text(source, encoding="utf-8", newline="\n")
