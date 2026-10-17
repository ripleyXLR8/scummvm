# Writing a ScummVM engine in this fork

Read README-AI.md first. Below are the rules that matter while you write code.

- Work only inside `engines/<name>/`. Never edit files outside it: this fork tracks upstream ScummVM and must merge it cleanly. If you need something from common code, use what exists, or tell the human to propose the change upstream.
- Start from the generated skeleton: `devtools/create_engine/create_engine <name>` (add `-events` for an event-driven engine). It creates `configure.engine` (keep status `no`), `module.mk`, `metaengine.cpp`, `detection.cpp`, `detection_tables.h` and the engine class.
- Build only your engine: `./configure --disable-all-engines --enable-engine=<name> && make -j`. Check that it registers: `./scummvm --list-engines`. Run it with `./scummvm --path=<game dir> --auto-detect`.
- Use the ScummVM APIs only, never the OS directly:
  - Files: `Common::File`, `Common::FSNode` and `SearchMan`.
  - Saves: `Engine::saveGameStream`/`loadGameStream`, with `Common::Serializer`.
  - Graphics: `Graphics::Screen` and `Graphics::ManagedSurface`.
  - Audio: `Audio::Mixer`.
  - Input: `Common::EventManager`.
  - Time: `g_system->getMillis()`, `Common::RandomSource`.
- No STL, no exceptions, no RTTI. Use `Common::Array`, `Common::String`, `Common::HashMap`, `Common::ScopedPtr` and the like.
- Follow the [code formatting conventions](https://wiki.scummvm.org/index.php?title=Code_Formatting_Conventions): tabs, `_member` names, braces on the same line.
- Game detection goes through `AdvancedMetaEngineDetection` with file MD5s in `detection_tables.h`.
- Reimplementations are clean-room: never paste decompiled or leaked code, and never commit game assets.
- Keep `engines/<name>/PROVENANCE.md` accurate (the template is `.github/ai/PROVENANCE.md`). List the AI tools you used.
- The code must compile without warnings and run cleanly under `--enable-asan`. Activation checks both.
- Commits follow the upstream [commit guidelines](https://wiki.scummvm.org/index.php?title=Commit_Guidelines) (`MYGAME: Summary`). Disclose AI help with the upstream trailer: `Assisted-by: AGENT_NAME:MODEL_VERSION`.
- Upstream's `AGENTS.md`/`AI-GUIDELINES.md` govern contributions to scummvm/scummvm itself. In this fork, AI-written engines are the point, but the human submitter still reviews and answers for every line.
