#!/bin/sh
# deploy.sh - build the homebrew apps, regenerate the launcher manifest, and
# push everything to the head unit. The launcher re-reads the manifest on each
# summon, so new/removed tiles appear on the next knob-summon (no reboot).
#
#   ./scripts/deploy.sh                 # build + push ALL enabled apps + core + manifest
#   ./scripts/deploy.sh evo_gps     # build + push just these apps (+ manifest)
#   ./scripts/deploy.sh --core          # push just the launcher runtime + manifest
#   ./scripts/deploy.sh --dry-run       # build + gen manifest, print pushes, NO ssh
#   ./scripts/deploy.sh --restart ...   # also restart the launcher after pushing
#
# Connection + layout come from scripts/evo_env.sh (override via env, e.g.
#   EVO_HOST=192.168.0.9 EVO_PASS=secret ./scripts/deploy.sh).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/evo_env.sh"

DRY=0; RESTART=0; CORE_ONLY=0; SELECT=""
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;;
    --restart) RESTART=1 ;;
    --core)    CORE_ONLY=1 ;;
    -*)        echo "unknown flag: $a" >&2; exit 2 ;;
    *)         SELECT="$SELECT $a" ;;
  esac
done

if [ "$DRY" = 0 ] && [ -z "${EVO_PASS:-}" ]; then
  echo "EVO_PASS is not set - cannot reach the unit." >&2
  echo "Set it in scripts/evo_env.local.sh (copy evo_env.local.sh.example) or pass" >&2
  echo "EVO_PASS=... on the command line, or use --dry-run to test without pushing." >&2
  exit 2
fi

say() { printf '%s\n' "$*"; }
push() { # <localfile> <remotepath> [chmod]
  if [ "$DRY" = 1 ]; then
    say "  PUSH $1 -> $EVO_USER@$EVO_HOST:$2${3:+  (chmod $3)}"
  else
    say "  push $(basename "$2")"
    evo_push "$1" "$2"
    [ -n "${3:-}" ] && evo_ssh "chmod $3 '$2'" || true
  fi
}

# does this app.desc match the SELECT list? (empty SELECT = all)
selected() { # <projdirname> <D_name>
  [ -z "$SELECT" ] && return 0
  for s in $SELECT; do
    sl="$(printf '%s' "$s" | tr 'A-Z' 'a-z')"
    [ "$sl" = "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" ] && return 0
    [ "$sl" = "$(printf '%s' "$2" | tr 'A-Z' 'a-z')" ] && return 0
  done
  return 1
}

# ---- 1. build + collect per-app push list --------------------------------
PUSHLIST=""      # newline-separated "localfile|remotepath|chmod"
add_push() { PUSHLIST="$PUSHLIST$1|$2|${3:-}
"; }

if [ "$CORE_ONLY" = 0 ]; then
  for desc in "$PROJECTS"/*/app.desc "$PROJECTS"/*/app.*.desc; do
    [ -f "$desc" ] || continue
    proj="$(dirname "$desc")"; pname="$(basename "$proj")"
    read_desc "$desc"
    [ "$D_enabled" = "0" ] && continue
    selected "$pname" "$D_name" || continue

    # build
    bdir="$D_build"; [ "$bdir" = "__self__" ] && bdir="$pname"
    if [ -n "$bdir" ]; then
      say "== build $D_name ($bdir)"
      ( cd "$PROJECTS/$bdir" && make ) >/dev/null 2>&1 \
        && say "   ok" || { say "   ! make failed in $bdir (see: make -C projects/$bdir)"; }
    else
      say "== $D_name: no build (external)"
    fi

    # deploy files
    if [ "$D_deploy" = "__auto__" ]; then
      app="$(evo_makefile_app "$PROJECTS/$bdir" 2>/dev/null || true)"
      [ -n "$app" ] && add_push "$BIN_DIR/$app" "$EVO_DIR/$app" "+x" \
        || say "   ! can't infer binary for $D_name (set deploy = in app.desc)"
    elif [ -n "$D_deploy" ]; then
      for item in $D_deploy; do
        case "$item" in
          bin/*) add_push "$BIN_DIR/${item#bin/}" "$EVO_DIR/${item#bin/}" "+x" ;;
          /*)    add_push "$item" "$EVO_DIR/$(basename "$item")" "+x" ;;
          *)     add_push "$proj/$item" "$EVO_DIR/$(basename "$item")" "+x" ;;
        esac
      done
    fi

    # icon
    if [ -n "$D_icon" ]; then
      isrc="$(evo_find_icon "$proj" "$D_icon" || true)"
      [ -n "$isrc" ] && add_push "$isrc" "$EVO_ICONDIR/$D_icon" \
        || say "   ! icon not found: $D_icon"
    fi
  done
fi

# ---- 2. regenerate the manifest ------------------------------------------
say "== manifest"
sh "$HERE/gen_manifest.sh" >/dev/null 2>&1 || sh "$HERE/gen_manifest.sh"

# ---- 3. core launcher runtime --------------------------------------------
add_push "$BIN_DIR/evo_launcher" "$EVO_DIR/evo_launcher" "+x"
add_push "$BIN_DIR/evo_hmistate" "$EVO_DIR/evo_hmistate" "+x"
AUTOSTART="$PROJECTS/doom_evo/controller/evo_autostart.sh"
[ -f "$AUTOSTART" ] && add_push "$AUTOSTART" "$EVO_DIR/evo_autostart.sh" "+x"
add_push "$MANIFEST" "$EVO_DIR/launcher.manifest" ""

# ---- 4. push --------------------------------------------------------------
say "== push to $EVO_USER@$EVO_HOST:$EVO_DIR"
if [ "$DRY" = 0 ]; then
  evo_ssh "mkdir -p '$EVO_DIR' '$EVO_ICONDIR'" || { say "!! cannot reach unit ($EVO_HOST)"; exit 1; }
fi
printf '%s' "$PUSHLIST" | while IFS='|' read -r lf rp cm; do
  [ -n "$lf" ] || continue
  push "$lf" "$rp" "$cm"
done

# ---- 5. optional launcher restart -----------------------------------------
# Stop the running daemons (can't overwrite a busy exe anyway) and start the
# freshly-pushed ones directly - don't rely on the boot supervisor, which has
# usually already exited by the time a deploy runs.
if [ "$RESTART" = 1 ] && [ "$DRY" = 0 ]; then
  say "== restart launcher daemons"
  evo_ssh "slay -f evo_launcher 2>/dev/null; slay -f evo_hmistate 2>/dev/null; sleep 2;
    export LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib; unset LD_PRELOAD;
    cd $EVO_DIR;
    ( ./evo_hmistate -daemon >/dev/shmem/evo_hmistate.log 2>&1 & ); sleep 1;
    ( ./evo_launcher  -daemon >/dev/shmem/evo_launcher.log  2>&1 & ); sleep 2;
    echo \"   launcher=\$(pidin -F %N 2>/dev/null | grep -c evo_launcher) hmistate=\$(pidin -F %N 2>/dev/null | grep -c evo_hmistate)\""
fi

say ""
[ "$DRY" = 1 ] && say "DRY RUN - nothing was pushed." \
  || say "Done. Summon the launcher (iDrive up/down on the Startmenu) to see changes."
