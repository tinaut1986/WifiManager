#!/bin/sh
# Print "MAJOR MINOR MICRO" for a version string, or "0 0 0" when it is not a
# plain numeric vX.Y.Z (optionally followed by -... ). Used to feed makerom.
v="${1#v}"
v="${v%%-*}"
M="${v%%.*}"
r="${v#*.}"
m="${r%%.*}"
p="${r#*.}"
p="${p%%.*}"
case "$M.$m.$p" in
    *[!0-9.]* | *..* | .* | *. ) echo "0 0 0" ;;
    *                          ) echo "${M:-0} ${m:-0} ${p:-0}" ;;
esac
