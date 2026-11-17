# Short GitHub release pages

TL;DR text for each published release. Apply after this branch is on `main` (the "Full release notes" links point there):

```powershell
foreach ($v in 'v2.0.3','v2.0.2','v2.0.1','v2.0.0','v2.0.0-preview.1') { gh release edit $v -R simcruisegame/simcruise --notes-file "docs/release/release-pages/$v.md" }
```

The text first published on each release page is kept in `docs/release/notes/<version>.md`.
