# vttest Snapshot

- Date: 2026-05-05
- Repository: `vttest`
- Commit: `d37932dc8a7e5dab702ba34020bdbb12cdf12858`
- Reason: preserve the embedded `vttest` repository contents independently of the gitlink stored in the outer repository

## Included artifacts

- Bundle: `vttest-d37932d.bundle`
- Source archive: `vttest-d37932d.tar.gz`

## Restore

### From the bundle

```sh
git clone vttest-d37932d.bundle restored-vttest
cd restored-vttest
git checkout d37932dc8a7e5dab702ba34020bdbb12cdf12858
```

### From the archive

Extract `vttest-d37932d.tar.gz` into a target directory.

## Validation

- `git bundle verify vttest-d37932d.bundle`