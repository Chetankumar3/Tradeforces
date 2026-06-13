$registry = if ($args.Count -ge 1) { $args[0] } else { 'us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces' }
$version = if ($args.Count -ge 2) { $args[1] } else { '1.0.0' }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$env:DOCKER_BUILDKIT = '1'

$jobs = @(
  Start-Job -Name 'main' -ScriptBlock {
    param($registry, $version, $scriptDir)
    Set-Location $scriptDir
    & docker build --progress=auto --pull -t "$registry/tradeforces-main:$version" -f "microservices/main/Dockerfile" .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & docker push "$registry/tradeforces-main:$version"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'Main service pushed successfully'
  } -ArgumentList $registry, $version, $scriptDir,

  Start-Job -Name 'vmcreator' -ScriptBlock {
    param($registry, $version, $scriptDir)
    Set-Location $scriptDir
    & docker build --progress=auto --pull -t "$registry/tradeforces-vmcreator:$version" -f "microservices/vm_creator/Dockerfile" .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & docker push "$registry/tradeforces-vmcreator:$version"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'VM Creator service pushed successfully'
  } -ArgumentList $registry, $version, $scriptDir,

  Start-Job -Name 'shadow' -ScriptBlock {
    param($registry, $version, $scriptDir)
    Set-Location $scriptDir
    & docker build --progress=auto --pull -t "$registry/tradeforces-shadow:$version" -f "microservices/shadow_engine/Dockerfile" "microservices/shadow_engine"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & docker push "$registry/tradeforces-shadow:$version"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'Shadow Engine service pushed successfully'
  } -ArgumentList $registry, $version, $scriptDir,

  Start-Job -Name 'telemetry' -ScriptBlock {
    param($registry, $version, $scriptDir)
    Set-Location $scriptDir
    & docker build --progress=auto --pull -t "$registry/tradeforces-telemetry:$version" -f "microservices/telemetry/Dockerfile" "microservices/telemetry"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & docker push "$registry/tradeforces-telemetry:$version"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'Telemetry service pushed successfully'
  } -ArgumentList $registry, $version, $scriptDir
)

Wait-Job $jobs | Out-Null

$failed = $false
foreach ($job in $jobs) {
  Receive-Job -Job $job
  if ($job.State -ne 'Completed') {
    $failed = $true
  }
}

Remove-Job -Job $jobs -Force

if ($failed) {
  exit 1
}