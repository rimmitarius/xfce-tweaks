# Host patch

`0001-native-extension-host.patch` contains the generic native extension bridge and SDK against the project's pristine first commit `c492a6af7626ecfea64197aaca3fce7248f1dca0`. Paths are relative to this project root, with xfdesktop under `host/` and the SDK under `sdk/`.

It intentionally excludes feature implementations, the manager and packaging tools. The complete source project includes those separately. To review against the baseline, use `git diff c492a6af7626ecfea64197aaca3fce7248f1dca0 -- host sdk`. A standalone upstream submission would relocate the public header into xfdesktop's own include tree and require upstream ABI/lifecycle review.

The exported patch also includes the additive per-view interaction/opacity helpers and bounded native icon compositing. Feature policy and implementations remain in modules; existing ABI 1 struct layouts are unchanged.
