# Uploading this project to GitHub

This source tree includes the clean upstream baseline and a sanitized project snapshot, plus separate `.xfce-tweak` packages under `packages/`. Build outputs, session settings, live-install backups, test screenshots/logs, and generated launchers are excluded. The bundled upstream source archives remain under `upstream/` with checksums. The project history has been reduced to these clean source snapshots so earlier local development notes are not published.

Create an empty repository on GitHub, then from this folder:

```sh
git remote add origin https://github.com/YOUR-ACCOUNT/YOUR-REPOSITORY.git
git push -u origin modular-tweaks
```

The prepared folder contains a clean source snapshot commit on top of the pinned upstream baseline. Review `git status` and the commit list before pushing. Keep local desktop/session settings, live backups, and user-specific media out of the repository. If you prefer GitHub's web uploader, upload a ZIP of this folder without the `.git/` directory.

To refresh distributable manager imports before a release, run `./scripts/build.sh` followed by `./scripts/package-tweaks.sh`. Each archive is small enough to store in the repository; alternatively attach the files from `packages/` to a GitHub Release.
