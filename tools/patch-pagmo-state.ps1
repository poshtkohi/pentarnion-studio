param([Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$declaration='namespace penta::evolution::session_detail { struct StateAccess; }'
foreach($name in @('sade','cmaes','sga','nsga2','cstrs_self_adaptive')){
    $path=Join-Path $SourceRoot "include/pagmo/algorithms/$name.hpp"
    $source=[IO.File]::ReadAllText($path)
    if($source.Contains('friend struct penta::evolution::session_detail::StateAccess;')){continue}
    $offset=$source.IndexOf('namespace pagmo')
    if($offset -lt 0 -or !$source.Contains('friend class boost::serialization::access;')){throw "Unexpected pinned Pagmo header: $name"}
    $source=$source.Insert($offset,$declaration+"`n")
    $source=$source.Replace('friend class boost::serialization::access;',"friend class boost::serialization::access;`n    friend struct penta::evolution::session_detail::StateAccess;")
    [IO.File]::WriteAllText($path,$source,[Text.UTF8Encoding]::new($false))
}
