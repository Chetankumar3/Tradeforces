$registry = if ($args.Count -ge 1) { $args[0] } else { 'us-central1-docker.pkg.dev/project-cdd074dc-6291-4d7f-a2a/tradeforces' }
$version = if ($args.Count -ge 2) { $args[1] } else { '1.0.0' }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir
Set-Location ..

Set-Location backend/microservices/main
docker build -t "$registry/tradeforces-main:$version" -f Dockerfile ../../
docker push "$registry/tradeforces-main:$version"
Write-Host "Main service pushed successfully"

Set-Location ../vm_creator
docker build -t "$registry/tradeforces-vmcreator:$version" -f Dockerfile ../../
docker push "$registry/tradeforces-vmcreator:$version"
Write-Host "VM Creator service pushed successfully"

Set-Location ../shadow_engine
docker build -t "$registry/tradeforces-shadow:$version" -f Dockerfile ../../
docker push "$registry/tradeforces-shadow:$version"
Write-Host "Shadow Engine service pushed successfully"