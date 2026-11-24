#!/bin/sh
# evo_env.sh - shared config + helpers for the EVO homebrew launcher tooling.
# Sourced by gen_manifest.sh and deploy.sh.
#
# NO SECRETS live in this file (it is shared/committed). The head-unit root
# password and any site-specific values come from, in order of precedence:
#   1. the environment            (EVO_HOST=... EVO_PASS=... ./scripts/deploy.sh)
#   2. scripts/evo_env.local.sh   (gitignored; your unit's IP + password)
#   3. the placeholder defaults below
# For a different head unit, set at least EVO_HOST and EVO_PASS by one of those.

# ---- repo layout ----------------------------------------------------------
# ${BASH_SOURCE} works when this file is sourced interactively; $0 when a script
# (deploy.sh / gen_manifest.sh) executes and sources it.
EVO_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" 2>/dev/null && pwd)"
case "$EVO_ENV_DIR" in */scripts) REPO="$(dirname "$EVO_ENV_DIR")";; *) REPO="$(cd "$(dirname "$0")/.." && pwd)";; esac
BIN_DIR="$REPO/bin"
PROJECTS="$REPO/projects"
BUILD_DIR="$REPO/build"
MANIFEST="$BUILD_DIR/launcher.manifest"

# ---- local (gitignored) overrides: your unit's IP + password --------------
[ -f "$EVO_ENV_DIR/evo_env.local.sh" ] && . "$EVO_ENV_DIR/evo_env.local.sh"

# ---- head unit connection (placeholders; override per above) --------------
: "${EVO_HOST:=169.254.199.119}"     # head-unit IP (default: USB-ethernet link)
: "${EVO_USER:=root}"
: "${EVO_PASS:=}"                    # root password - set via env or evo_env.local.sh

# ---- on-unit layout -------------------------------------------------------
: "${EVO_DIR:=/fs/sda1/evo}"         # homebrew app dir on the unit (persistent)
: "${EVO_ICONDIR:=${EVO_DIR}/icons}"

# ---- SSH / file push over ssh+cat (no sftp/scp dependency on QNX dropbear) -
EVO_SSH_OPTS="-o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -o StrictHostKeyChecking=no -o ConnectTimeout=8"

evo_ssh() {   # evo_ssh '<remote command>'   (-n: never read stdin, so it is safe
              # to call inside a `... | while read` loop without swallowing the pipe)
  SSHPASS="$EVO_PASS" sshpass -e ssh -n $EVO_SSH_OPTS "$EVO_USER@$EVO_HOST" "$@"
}

evo_push() {  # evo_push <local-file> <remote-path>   (binary-safe, via cat)
  [ -f "$1" ] || { echo "  ! missing local file: $1" >&2; return 1; }
  SSHPASS="$EVO_PASS" sshpass -e ssh $EVO_SSH_OPTS "$EVO_USER@$EVO_HOST" "cat > '$2'" < "$1"
}

# ---- app.desc parser ------------------------------------------------------
# read_desc <descfile> sets: D_name D_exec D_icon D_order D_enabled D_build D_deploy
# Sentinels: D_build=__self__ (unset -> build this project), D_deploy=__auto__
# (unset -> push the binary named by the project Makefile's APP). An explicit
# empty value ("build =") disables that step.
read_desc() {
  D_name=""; D_exec=""; D_icon=""; D_order="100"; D_enabled="1"
  D_build="__self__"; D_deploy="__auto__"
  while IFS= read -r _line || [ -n "$_line" ]; do
    _line="${_line%%#*}"
    case "$_line" in *=*) : ;; *) continue ;; esac
    _key="${_line%%=*}"; _val="${_line#*=}"
    _key="$(printf '%s' "$_key" | tr -d '[:space:]')"
    _val="$(printf '%s' "$_val" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    case "$_key" in
      name)    D_name="$_val" ;;
      exec)    D_exec="$_val" ;;
      icon)    D_icon="$_val" ;;
      order)   D_order="$_val" ;;
      enabled) D_enabled="$_val" ;;
      build)   D_build="$_val" ;;
      deploy)  D_deploy="$_val" ;;
    esac
  done < "$1"
}

# on-unit absolute path for a bare name (leaves absolute paths untouched)
evo_onunit()     { case "$1" in /*) printf '%s' "$1";; *) printf '%s/%s' "$EVO_DIR" "$1";; esac; }
evo_onunit_icon(){ case "$1" in /*) printf '%s' "$1";; "") printf '' ;; *) printf '%s/%s' "$EVO_ICONDIR" "$1";; esac; }

# find an icon's source file on the host: project dir first, then central store
evo_find_icon() { # <projectdir> <iconname>  -> echoes host path or nothing
  [ -n "$2" ] || return 0
  for p in "$1/$2" "$1/icons/$2" "$PROJECTS/evo_launcher/icons/$2" "$REPO/shared/icons/$2"; do
    [ -f "$p" ] && { printf '%s' "$p"; return 0; }
  done
  return 1
}

# parse APP := <name> out of a project Makefile (for D_deploy=__auto__)
evo_makefile_app() { # <projectdir>
  [ -f "$1/Makefile" ] || return 1
  sed -n 's/^[[:space:]]*APP[[:space:]]*:=[[:space:]]*\([^[:space:]]*\).*/\1/p' "$1/Makefile" | head -1
}
