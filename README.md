# mono-profiler-jitdump

This is a simple Mono profiler plugin that writes [jit-dump](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/jitdump-specification.txt) compatible data files on OSX (and Linux).  It interfaces with compatible profilers such as [samply](https://github.com/mstange/samply) (OSX + Linux) or `perf` (Linux) to provide them with JIT method information at runtime.

Install [samply](https://github.com/mstange/samply) via `cargo install samply`. (Install Rust and `cargo`` via https://rustup.rs)

Samply automatically handles domain reload (which causes all jit methods to be evicted) by simply evicting any jit method information if a new jit code block is received that overlaps any existing methods. Overlapping methods are removed from the data table.

It is not possible to attach to an existing process to begin profiling. `samply` must launch the process.

To verify that the profiler module is being loaded, `Editor.log` will contain `## Initialized jitdump profiler module`.

## Building and installing the profiler plugin

You need to build this profiler plugin and make it available to Unity.

1. `make`
2. cd `path/to/Unity.app/Contents/lib`
3. `ln -s path-to-this-repo/build/libmono-profiler-jitdump.dylib .` (or `cp`)

## Profiling

Use `samply record`, together with passing `-monoProfiler jitdump` to Unity:

```
samply record --save-only -o profile-data.json -- path-to/Unity.app/Contents/MacOS/Unity -monoProfiler jitdump -projectpath path/to/Project
[... do things, exit or kill the process ...]
samply load profile-data.json
```

In the resulting view, JIT symbols will appear as purple (in the "JIT" category). Samply also profiles all threads and child processes, meaning you will see full profiles for out of process asset import, shader compilation, and similar.

