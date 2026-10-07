# Working on Loom

## Slots

`C:\Users\3hoze\Desktop\Loom` belongs to the user. Agents never edit, build,
run or read from it, and never run git commands that write to it. It holds
the user's unfinished work on its own branch, `dev`.

Agents work in a slot: `Loom 1` to `Loom 8` beside it, each a git worktree of
the same repository. `slots.ps1` hands them out. Any slot's copy works on any
slot, since `merge` and `release` find the slot by the feature name it was
claimed with. If PowerShell refuses to run it, call it through
`powershell -ExecutionPolicy Bypass -File`.

```powershell
$slots = 'C:\Users\3hoze\Desktop\Loom 1\slots.ps1'
& $slots status
& $slots claim <feature-name>
& $slots merge <feature-name>
& $slots release <feature-name>
```

A feature or bug fix, start to finish:

1. `claim <feature-name>` takes a free slot, brings it up to the latest
   `master` and creates `feature/<feature-name>` there. It prints the slot's
   path. Work only in that slot, by absolute path.
2. Commit to the branch as you go. This policy is the user's permission to
   commit on feature branches and to merge them as below.
3. Build, run, test and exercise the change, then get the `code-reviewer`
   agent's acceptance.
4. If the change shows on screen, open a demo that shows it, using the slot's
   own build: the editor with a demo project, `Loom Demos`, or
   `Space Explorers`. Start it with `Start-Process -PassThru` and keep its
   `Id` for `review.ps1`. Leave it open on the user's desktop. The demo and the
   review window below are the only windows agents put on the user's screen.
5. Run `review.ps1` from the slot in the background with the longest timeout
   the tool allows, since it blocks until the user answers. It shows what was
   asked, what changed and what to look for, then prints
   `{"decision": ..., "feedback": ...}`. If it exits without printing that,
   the user hasn't answered (it timed out or was killed), so open it again
   rather than reworking.

   ```powershell
   & "<slot>\review.ps1" -Feature <feature-name> -Prompt '<the request, verbatim>' `
       -Changes '<what changed, per file>' -Expect '<where to look and what should happen>' `
       -DemoProcessId <the demo's process id>
   ```

   Given the demo's process id, it closes the demo once the user approves or
   denies. On `deny`, rework the change from the feedback and go back to
   step 3.
6. On `approve`, `merge <feature-name>` rebases onto `master`, runs
   `run-tests.ps1` and `run-web-tests.ps1`, then fast-forwards `master`. If
   anything fails, nothing is merged. On rebase conflicts, resolve them and
   `git rebase --continue` (or `git rebase --abort`) in the slot, then run
   `merge` again. If `master` moved while the tests ran, run it again.
7. `release <feature-name>` frees the slot. To drop unmerged work, use
   `release <feature-name> -Abandon`.

Rules:

- Never take over a slot someone else has claimed, even an idle one. The
  user frees those.
- Never `git clean -x` in a slot. The prebuilt libraries under
  `External Libraries` (Vulkan, boost\stage, openssl\bin and openssl\lib) are
  junctions into the user's Loom.
- Never push to `origin` unless the user asks.
- `master` is never checked out anywhere, so `merge` can move it without
  touching a working tree. Don't check it out.
