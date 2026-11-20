# m24sr-nfc: agent rules (Codex and other agents)

Read `CLAUDE.md`: its rules apply to every agent in this repository, not only Claude. If `CLAUDE.local.md` exists,
read it as well (notes for one machine, git-ignored).

Codex-specific notes:
- `tools/quality.sh` needs bash and the Linux tools (gcc 11, clang 21, clang-tidy 21, cppcheck 2.16). If your
  sandbox cannot run them, run `python build.py test` and say in your report that the gate was not run; never report
  it as passed.
- Do not commit, push or flash unless the task says so. Keep your notes out of tracked files.
