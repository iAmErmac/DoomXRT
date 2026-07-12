# RT Runtime Data

GZDoom RT uses two different kinds of external data:

- immutable source assets, such as an extracted `gzdoom-rt` release `rt/`
  directory;
- generated runtime data, such as patched `textures.json`, normal-map aliases,
  and local autoload wrappers.

Keep immutable assets under `~/Games/gzdoom-rt/assets/` and generated runtime
data under the user cache. Do not patch or generate files inside the immutable
asset source.

Recommended layout:

```text
~/Games/gzdoom-rt/
  wad/
  mods/
  assets/
    gzdoom-rt-runtime/
      1.0.2/
        rt/

~/.cache/gzdoom-rt/
  runtime/
    current -> <fingerprint>
    <fingerprint>/
```

Prepare the runtime explicitly with:

```bash
tools/rt-runtime/prepare-runtime.sh \
  ~/Games/gzdoom-rt/assets/gzdoom-rt-runtime/1.0.2/rt \
  ~/.cache/gzdoom-rt/runtime/current
```

The engine also resolves and prepares this runtime on launch when it is missing.
Resolution order:

- `GZDOOM_RT_RUNTIME_DIR`
- `rt_runtime_dir`
- `$XDG_CACHE_HOME/gzdoom-rt/runtime/current`
- `~/.cache/gzdoom-rt/runtime/current`

Immutable asset source resolution order:

- `GZDOOM_RT_ASSET_DIR`
- `rt_asset_dir`
- `~/Games/gzdoom-rt/assets/gzdoom-rt-runtime/1.0.2/rt`
- `~/Games/gzdoom-rt/rt`
- `rt`

If `GZDOOM_RT_PREPARE_SCRIPT` or `rt_prepare_script` is set, that script is
used for composition. Otherwise the engine searches upward from the working
directory for `tools/rt-runtime/prepare-runtime.sh`. A basic engine fallback can
compose a runtime with symlinks and copied metadata, but the script path is the
preferred route because it applies repo-owned compatibility patches.

The generated runtime is an `rt/`-shaped directory. Large immutable directories
are symlinked from the source asset. Mutable paths are generated in cache:

- `data/` is copied and patched;
- `mat/` is a generated directory whose existing files are symlinks to source
  assets, while compatibility aliases are written only to cache;
- `autoload/` is generated locally.
