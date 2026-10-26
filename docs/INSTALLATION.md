# Build and install XFCE Tweaks

This project builds an extension-enabled xfdesktop into a user-owned private prefix. It does not overwrite the distribution's `/usr/bin/xfdesktop`.

## Fedora build

From the project root, run:

```sh
./scripts/install-dependencies.sh
./scripts/install-test-dependencies.sh   # Xephyr/Xvfb preview tools
./scripts/build.sh
./scripts/preview.py
```

`install-dependencies.sh` installs Fedora build/runtime packages with `sudo dnf`; the rest run as your normal user. The build verifies checksums for the bundled upstream archives, builds the private xfdesktop and dependencies, installs the public SDK and Tweaks Manager, builds the modules, and creates desktop launchers under `~/.local/lib/xfce-tweaks-modular/`. By default, this only prepares a build and does not change the active desktop session.

Use **Preview Modular Desktop** to run the host in Xephyr with isolated Xfconf, data, and desktop files. It is the recommended first run for plugin developers.

## Optional host patch workflow

The source tree in `host/` already contains the modular extension bridge. `patches/0001-native-extension-host.patch` is the reviewable delta against the pristine upstream baseline imported in commit `c492a6af7626ecfea64197aaca3fce7248f1dca0`. To reproduce it in a clean baseline checkout:

```sh
git worktree add --detach /tmp/xfce-tweaks-baseline c492a6af7626ecfea64197aaca3fce7248f1dca0
./scripts/apply-host-patch.sh /tmp/xfce-tweaks-baseline
```

The helper checks the exact baseline and clean worktree before applying. Do not apply the patch to a distribution source tree with local changes. Review and adapt the patch for the target upstream release before proposing it upstream.

## Activating this host in an existing session

The optional activation helper supports a dedicated systemd user service that already starts the XFCE desktop. Supply that service's name; for example:

```sh
python3 scripts/activate-live.py activate --unit my-xfdesktop.service
```

Rollback is available with:

```sh
python3 scripts/activate-live.py rollback --unit my-xfdesktop.service
```

The helper requires an existing running `systemd --user` service, X11, and a build in the private prefix. It does not set up a service or change ordinary XFCE session autostart. Other session setups need their own integration. Read [architecture and platform limits](ARCHITECTURE.md) before adapting it.

## Building standalone Tweak packages

After building, make or refresh all separate `.xfce-tweak` imports with:

```sh
./scripts/package-tweaks.sh
```

Packages are written to `packages/`, one archive per Tweak, and can be imported through XFCE Tweaks Manager. Developers can also build/package one module with `scripts/tweak-tool.py`; see [Developing a Tweak](DEVELOPING.md) and the [SDK API reference](API-REFERENCE.md).

## Uninstallation

In XFCE Tweaks Manager, use **Uninstall** next to a module and confirm. The manager disables it first, removes its installed module directory, and retains its saved Xfconf settings. The operation is limited to the user module directory and the configured private build prefix. A library already mapped by xfdesktop remains resident until xfdesktop exits; restart the modular desktop before reimporting the same ID.

To remove the entire private development prefix, close the preview and use `python3 scripts/remove-development-build.py`. This keeps the source repository, imported user Tweaks, and existing distribution desktop intact.
