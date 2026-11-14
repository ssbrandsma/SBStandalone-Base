#!/bin/sh
# Publish an already-built dual-platform release, preserving live catalogs.
set -eu
config=${SB_DEPLOY_CONFIG:-/tmp/standalonebase-deployment.env}
test -r "$config" || { echo "Missing private deployment configuration: $config" >&2; exit 1; }
. "$config"
: "${BASE_ROOT:?}" "${RADIO_ROOT:?}" "${MERGED_XML:?}" "${BOOTSTRAP_CONFIG:?}"
: "${WEB_OWNER:?}" "${BOOTSTRAP_OWNER:?}" "${CONTAINER_NAME:?}"
version=${SB_RELEASE_VERSION:?Set the release version}
stage=${SB_RELEASE_STAGE:?Set the validated release staging directory}
replace_version=${SB_REPLACE_RELEASE_VERSION:-}
test -z "$replace_version" || test "$replace_version" = "$version" || {
  echo 'Replacement authorization must name the exact release version' >&2; exit 1;
}

# Recheck archives, both hardware entries and live-file preconditions before writes.
python3 - "$stage" "$version" <<'PY'
import hashlib,json,re,sys
from pathlib import Path
from xml.etree import ElementTree as E
stage=Path(sys.argv[1]);version=sys.argv[2]
assert re.fullmatch(r'\d+\.\d+\.\d+',version)
m=json.loads((stage/f'release-{version}.json').read_text())
assert m['version']==version
assert len(m['artifacts'])==3
for a in m['artifacts']:
    assert Path(a['file']).name==a['file']
    b=(stage/a['file']).read_bytes()
    assert hashlib.sha1(b).hexdigest()==a['sha1']
    assert hashlib.sha256(b).hexdigest()==a['sha256']
for path,digest in json.loads((stage/'preconditions.json').read_text()).items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest, f'Live catalog changed: {path}'
expected={(a['name'],a['target']):a for a in m['applets']}
assert set(expected)=={(n,t) for n in ('StandaloneBase','StandaloneRadio') for t in ('baby','fab4')}
config=json.loads((stage/'bootstrap-config.json').read_text())
public=json.loads((stage/'public-config.json').read_text())
for entries in [config['applets'],public['applets']]:
    for key,a in expected.items():
        found=[e for e in entries if (e['name'],e.get('target'))==key]
        assert len(found)==1 and all(found[0][k]==a[k] for k in ('version','url','sha'))
for file,names in [('base-extensions.xml',{'StandaloneBase'}),('radio-extensions.xml',{'StandaloneRadio'}),('merged-extensions.xml',{'StandaloneBase','StandaloneRadio'})]:
    entries=E.parse(stage/file).getroot().find('applets')
    for key,a in expected.items():
        if key[0] not in names: continue
        found=[e for e in entries if (e.get('name'),e.get('target'))==key]
        assert len(found)==1
        e=found[0]
        assert e.get('version')==a['version'] and e.findtext('url')==a['url'] and e.findtext('sha')==a['sha']
PY

stamp=$(date +%Y%m%d-%H%M%S)
backup=${SB_RELEASE_BACKUP_ROOT:-/var/backups/standalonebase}/$version-$stamp
mkdir -p "$backup" "$BASE_ROOT" "$RADIO_ROOT"
cp -p "$MERGED_XML" "$backup/merged-extensions.xml"
cp -p "$BOOTSTRAP_CONFIG" "$backup/bootstrap-config.json"
cp -p "$BASE_ROOT/extensions.xml" "$backup/base-extensions.xml"
cp -p "$RADIO_ROOT/extensions.xml" "$backup/radio-extensions.xml"
for path in "$MERGED_XML" "$BASE_ROOT/extensions.xml" "$RADIO_ROOT/extensions.xml"; do
  if test -f "$path.sha1"; then cp -p "$path.sha1" "$backup/$(basename "$(dirname "$path")")-extensions.xml.sha1"; fi
done
public_existed=0
if test -f "$BASE_ROOT/config.json"; then
  cp -p "$BASE_ROOT/config.json" "$backup/public-config.json"
  public_existed=1
fi
# Same-version refreshes require an explicit, version-scoped opt-in. Preserve
# the old ZIPs and manifest as well as catalogs so rollback restores matching
# advertised hashes and bytes. The default immutable-artifact policy remains.
for target in baby fab4; do
  file=StandaloneBase-$version-$target.zip
  if test -f "$BASE_ROOT/$file"; then
    if ! cmp -s "$stage/$file" "$BASE_ROOT/$file"; then
      test "$replace_version" = "$version" || {
        echo "Refusing replacement of published $file without explicit version authorization" >&2; exit 1;
      }
    fi
    cp -p "$BASE_ROOT/$file" "$backup/$file"
  fi
done
if test -f "$BASE_ROOT/release-$version.json"; then
  cp -p "$BASE_ROOT/release-$version.json" "$backup/release-$version.json"
fi
success=0
rollback() {
  result=$?
  if test "$success" != 1; then
    for target in baby fab4; do
      file=StandaloneBase-$version-$target.zip
      if test -f "$backup/$file"; then cp -p "$backup/$file" "$BASE_ROOT/$file"; else rm -f "$BASE_ROOT/$file"; fi
    done
    if test -f "$backup/release-$version.json"; then
      cp -p "$backup/release-$version.json" "$BASE_ROOT/release-$version.json"
    else rm -f "$BASE_ROOT/release-$version.json"; fi
    cp -p "$backup/merged-extensions.xml" "$MERGED_XML"
    cp -p "$backup/bootstrap-config.json" "$BOOTSTRAP_CONFIG"
    cp -p "$backup/base-extensions.xml" "$BASE_ROOT/extensions.xml"
    cp -p "$backup/radio-extensions.xml" "$RADIO_ROOT/extensions.xml"
    if test "$public_existed" = 1; then cp -p "$backup/public-config.json" "$BASE_ROOT/config.json"; else rm -f "$BASE_ROOT/config.json"; fi
    for path in "$MERGED_XML" "$BASE_ROOT/extensions.xml" "$RADIO_ROOT/extensions.xml"; do
      sha1sum "$path" | cut -d' ' -f1 > "$path.sha1"
    done
    docker restart "$CONTAINER_NAME" >/dev/null || true
    echo "Release failed; catalogs restored from $backup" >&2
  fi
  exit "$result"
}
trap rollback EXIT

radio=$(python3 -c 'import json,sys;print(next(a["file"] for a in json.load(open(sys.argv[1]))["artifacts"] if a["file"].startswith("StandaloneRadio-")))' "$stage/release-$version.json")
# Install immutable artifacts before advertising them. Existing same-version bytes
# must match unless the exact version replacement was explicitly authorized
# and its previous bytes were backed up above.
for target in baby fab4; do
  file=StandaloneBase-$version-$target.zip
  if test -f "$BASE_ROOT/$file" && test "$replace_version" != "$version"; then cmp "$stage/$file" "$BASE_ROOT/$file"; fi
  install -o root -g root -m 0644 "$stage/$file" "$BASE_ROOT/$file.new"
  mv "$BASE_ROOT/$file.new" "$BASE_ROOT/$file"
done
if test -f "$RADIO_ROOT/$radio"; then cmp "$stage/$radio" "$RADIO_ROOT/$radio"; fi
install -o root -g root -m 0644 "$stage/$radio" "$RADIO_ROOT/$radio.new"
mv "$RADIO_ROOT/$radio.new" "$RADIO_ROOT/$radio"
install -o root -g root -m 0644 "$stage/release-$version.json" "$BASE_ROOT/release-$version.json"

install -o root -g root -m 0644 "$stage/public-config.json" "$BASE_ROOT/config.json.new"
install -o root -g root -m 0644 "$stage/base-extensions.xml" "$BASE_ROOT/extensions.xml.new"
install -o root -g root -m 0644 "$stage/radio-extensions.xml" "$RADIO_ROOT/extensions.xml.new"
install -o "$WEB_OWNER" -g "$WEB_OWNER" -m 0644 "$stage/merged-extensions.xml" "$MERGED_XML.new"
install -o "$BOOTSTRAP_OWNER" -g "$BOOTSTRAP_OWNER" -m 0644 "$stage/bootstrap-config.json" "$BOOTSTRAP_CONFIG.new"
mv "$BASE_ROOT/config.json.new" "$BASE_ROOT/config.json"
mv "$BASE_ROOT/extensions.xml.new" "$BASE_ROOT/extensions.xml"
mv "$RADIO_ROOT/extensions.xml.new" "$RADIO_ROOT/extensions.xml"
mv "$MERGED_XML.new" "$MERGED_XML"
mv "$BOOTSTRAP_CONFIG.new" "$BOOTSTRAP_CONFIG"
for path in "$MERGED_XML" "$BASE_ROOT/extensions.xml" "$RADIO_ROOT/extensions.xml"; do
  sha1sum "$path" | cut -d' ' -f1 > "$path.sha1.new"
  chmod --reference="$path" "$path.sha1.new"
  chown --reference="$path" "$path.sha1.new"
  mv "$path.sha1.new" "$path.sha1"
done
docker restart "$CONTAINER_NAME" >/dev/null
docker ps --format '{{.Names}} {{.Status}}' | grep -F "$CONTAINER_NAME "
success=1
echo "Published Standalone Base $version for baby/fab4 and $radio"
echo "Rollback backup: $backup"
