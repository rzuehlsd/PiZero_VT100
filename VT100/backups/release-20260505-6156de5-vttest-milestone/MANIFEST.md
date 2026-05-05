# VT100 vttest Compatibility Milestone

- Date: 2026-05-05
- Commit: `6156de564843b0d110d096e63258ce4bd36689d8`
- Tag: `vt100-vttest-milestone-2026-05-05`
- Scope: freeze the current external `vttest` compatibility milestone without including unrelated WLAN/kernel worktree changes

## Included rollback artifacts

- Annotated git tag: `vt100-vttest-milestone-2026-05-05`
- Git bundle: `vt100-vttest-milestone-2026-05-05.bundle`
- Source archive: `vt100-vttest-milestone-2026-05-05.tar.gz`

## Restore options

### Inside the current repository

```sh
git checkout vt100-vttest-milestone-2026-05-05
```

### From the portable bundle

```sh
git clone vt100-vttest-milestone-2026-05-05.bundle restored-vt100
cd restored-vt100
git checkout vt100-vttest-milestone-2026-05-05
```

### Reset current branch to this milestone

```sh
git checkout main
git reset --hard 6156de564843b0d110d096e63258ce4bd36689d8
```

## Validation

- `make -j4` in `VT100/`
- `git bundle verify vt100-vttest-milestone-2026-05-05.bundle`