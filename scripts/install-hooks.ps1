# Installs the OSDS git hooks (scripts/hooks) into this clone. Run once per clone or worktree's main repo.
$root = git rev-parse --show-toplevel
$common = git rev-parse --git-common-dir
Copy-Item -Force (Join-Path $root 'scripts/hooks/pre-push') (Join-Path $common 'hooks/pre-push')
Write-Host "Installed pre-push hook into $common/hooks (pushes to main run check.ps1 -Tier fast)."
