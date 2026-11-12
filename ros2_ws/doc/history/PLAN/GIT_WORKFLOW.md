> Historical snapshot. Current system documentation: [index](../../README.md).

# Git workflow: laptop and Raspberry Pi

The shared repository is `https://github.com/pochihh/Project-Crazy-Diamond`,
branch `main`. Both machines use that history:

| Machine | Checkout |
|---|---|
| Laptop | `/Users/toby/Projects/Project-Crazy-Diamond` |
| Pi | `/home/toby/Project-Crazy-Diamond` |

Both Pi systemd services run from the Pi checkout. The former
`~/crazy-diamond-stage2` deployment is retained as historical material, not an
active source directory. Source is synchronized with Git, not copied as tarballs.

## What is tracked

DSP firmware and CCS projects, the shared SPI protocol/host transport, all ROS
packages, web source and npm lockfile, tests, service/build scripts, project YAML
profiles, plans and UI reference assets belong in Git. Generated DSP/ROS/web
builds, dependencies, recordings, logs, `.local/`, machine credentials and private
SSH keys remain local and ignored. Build outputs are regenerated on their target
machine; a Git pull does not program the DSP.

The Pi uses its own repository-scoped SSH deploy key with write permission.
The laptop uses its existing GitHub credentials. Their transport URLs can differ
while referring to the same repository. The private Pi key stays on the Pi.

## Normal edits on either machine

Start with a clean checkout, then:

```bash
git switch main
git pull --ff-only
# Edit the relevant files, then inspect the diff.
git status --short
git diff
git add <changed-files>
git commit -m "Describe the change"
git push origin main
```

On the other machine, run `git pull --ff-only`. If Git reports local changes or
divergent history, preserve/commit the changes and reconcile them before deploying;
do not force-push or reset away unsaved work. Use `codex/<name>` or another agreed
feature branch for work that should not yet be deployed from `main`.

Saving controller parameters in the UI edits tracked project YAML on the Pi.
Commit and push those profile changes before pulling another update. Apply-only
tuning stays in memory; it is not saved or synchronized by Git.

## Update and deploy on the Pi

Finish any recording, disable outputs, and check `git status` before stopping the
services. Then, from the Pi checkout:

```bash
cd ~/Project-Crazy-Diamond
sudo systemctl stop diamond-ui diamond-bridge
git pull --ff-only

export PATH="$HOME/.local/bin:$PATH"
taskset -c 0-2 npm --prefix web_ui ci
taskset -c 0-2 npm --prefix web_ui test
taskset -c 0-2 npm --prefix web_ui run build
taskset -c 0-2 bash scripts/build_ros.sh

sudo cp scripts/diamond-bridge.service scripts/diamond-ui.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl start diamond-bridge diamond-ui
systemctl status diamond-bridge diamond-ui
```

Continue to startup only if the builds/tests succeed. The Pi has Node.js 22
installed under `~/.local/`, with `node`, `npm`, and `npx` in `~/.local/bin`.
ROS 2 Jazzy remains installed under `/opt/ros/jazzy`. Installing dependencies or
pulling source does not restart services automatically. Bridge startup is disarmed
and establishes new startup zeros.

## Check synchronization

```bash
git status --short --branch
git rev-parse HEAD
git ls-remote origin refs/heads/main
```

The laptop's HEAD, Pi's HEAD and remote `main` should match after deployment, with
no uncommitted source/configuration changes. Recordings and runtime diagnostic logs
stay on the Pi and remain accessible after code updates. Historical absolute paths
in earlier acceptance reports refer to the deployment used for those tests.
