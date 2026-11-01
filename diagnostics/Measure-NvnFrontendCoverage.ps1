param([Parameter(Mandatory=$true)][string]$ReferenceRoot,[string]$Log='build\nvn-frontend-audit\boot.stderr.log',[string]$Output='build\nvn-frontend-audit\api-coverage.json')
$ErrorActionPreference='Stop'
$text=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $Log))
$resolved=@([regex]::Matches($text,'\[NVN bridge\] resolve (nvn\w+)')|ForEach-Object {$_.Groups[1].Value}|Sort-Object -Unique)
$called=@([regex]::Matches($text,'\[NVN bridge\] call (nvn\w+)')|ForEach-Object {$_.Groups[1].Value}|Sort-Object -Unique)
$gl=[IO.File]::ReadAllText((Join-Path $ReferenceRoot 'runtime\nvn_gl.c'))
$start=$gl.IndexOf('static const struct { const char *name; ImportFn fn; } k_nvn[]')
if($start -lt 0){throw 'Reference dispatch table changed; update coverage parser.'}
$stub=[IO.File]::ReadAllText((Join-Path $ReferenceRoot 'runtime\nvn_stub.c'))
$special=$stub.IndexOf('static ImportFn pick(const char *name) {')
if($special -lt 0){throw 'Reference special resolver changed; update coverage parser.'}
$known=@([regex]::Matches($gl.Substring($start)+$stub.Substring($special),'"(nvn\w+)"')|ForEach-Object {$_.Groups[1].Value}|Sort-Object -Unique)
$result=@{scope='Observed log only; static dispatch names, not semantic compatibility';resolvedCount=$resolved.Count;calledCount=$called.Count;missingCalled=@($called|Where-Object {$_ -notin $known});missingResolved=@($resolved|Where-Object {$_ -notin $known});called=$called;resolved=$resolved}
$result|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Resolved $($resolved.Count), called $($called.Count); reference missing called names: $($result.missingCalled.Count)."
