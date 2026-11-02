#!/bin/bash
# NativePatch environment on Windows + WSL (called by install.cmd): Ubuntu packages, git name/e-mail, repo clone (dev
# branch), compilers (tools/setup.sh), relay in Downloads and a first build.
# It can be run again: it skips what is already there. All that needs PATH stays in Ubuntu (~/tc, with Adrenaline);
# on Windows it only creates: Documents\GitHub\NativePatch and Downloads\{vitabridge.py,vitabridge.cmd,vitabridge\}
set -e
REPO_URL=https://github.com/Maty-Espinoza/NativePatch.git
WIN=${WINHOME:-$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")}  # C:\Users\<user>
GH=$WIN/Documents/GitHub; DL=$WIN/Downloads
[ -d "$WIN" ] || { echo "Windows user folder not found ($WIN)"; exit 1; }

echo "== 1/5 Ubuntu packages (asks for the Ubuntu password)"
sudo apt-get update -qq && sudo apt-get install -y -qq git make python3 curl xz-utils bzip2 ca-certificates >/dev/null

echo "== 2/5 git"
if [ -z "$(git config --global user.name)" ]; then
  read -rp "   name for commits [Maty-Espinoza]: " n; git config --global user.name "${n:-Maty-Espinoza}"
  read -rp "   e-mail for commits [matias.espinozaq@33sur.cl]: " m; git config --global user.email "${m:-matias.espinozaq@33sur.cl}"
fi
GCM="/mnt/c/Program Files/Git/mingw64/bin/git-credential-manager.exe"   # the Windows GitHub sign-in
[ -f "$GCM" ] && git config --global credential.helper "$GCM" || echo "   (no Git for Windows: the first push will ask to sign in)"

echo "== 3/5 repo in $GH"
mkdir -p "$GH" && cd "$GH"
[ -d NativePatch ] || git clone -q -b dev $REPO_URL NativePatch
git -C NativePatch config core.filemode false
git -C NativePatch config user.name  "$(git config --global user.name)"    # also applies to the Windows git
git -C NativePatch config user.email "$(git config --global user.email)"

echo "== 4/5 PSP and Vita compilers in ~/tc (~3 min the first time)"
sh NativePatch/tools/setup.sh

echo "== 5/5 relay with the Vita and first build"
mkdir -p "$DL/vitabridge"
cp NativePatch/tools/vitabridge.py NativePatch/tools/windows/vitabridge.cmd "$DL/"
V="$DL/vitabridge" sh NativePatch/tools/build.sh
echo
echo "Done. Build: Documents\\GitHub\\NativePatch\\tools\\windows\\build.cmd   Relay: Downloads\\vitabridge.cmd"
