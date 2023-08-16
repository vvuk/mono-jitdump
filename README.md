# mono-profiler-jitdump

## The mono profiler plugin

Build this repo, and set up a symlink (or copy) in your editor tree.

1. edit `Makefile` and put in paths to Unity and Mono sources
2. `make`
3. cd `path/to/Unity.app/Contents/lib`
4. `ln -s path-to-this-repo/libmono-profiler-jitdump.dylib .`

## The samply profiler

Install https://github.com/mstange/samply via `cargo install samply`.  (Install Rust via https://rustup.rs)

## Profile

```
samply record --save-only -o profile-data.json -- path-to/Unity.app/Contents/MacOS/Unity -monoProfiler jitdump -projectpath path/to/Project
[... do things, exit or kill the process ...]
samply load profile-data.json
```

