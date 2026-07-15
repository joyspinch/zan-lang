#!/bin/bash
# Convert the libpq closure in /tmp/pqbundle from @loader_path to @rpath so it
# matches zanc's link model (`-Wl,-rpath,<driver_dir>` -> @rpath resolves to the
# driver dir in both dev and --publish builds, exactly like sqlite/Linux .so).
set -e
cd /tmp/pqbundle
for f in *.dylib; do
  chmod u+w "$f"
  # id
  cur=$(otool -D "$f" | tail -n1)
  case "$cur" in @loader_path/*) install_name_tool -id "@rpath/$(basename "$cur")" "$f";; esac
  # deps
  otool -L "$f" | tail -n +2 | awk '{print $1}' | while read dep; do
    case "$dep" in
      @loader_path/*) install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$f";;
    esac
  done
done
# link-time stand-in so `-lpq` resolves (ld looks for libpq.dylib on macOS)
cp -f libpq.5.dylib libpq.dylib
chmod u+w libpq.dylib
install_name_tool -id "@rpath/libpq.5.dylib" libpq.dylib
# ad-hoc re-sign all (install_name_tool invalidates signatures)
for f in *.dylib; do codesign -f -s - "$f" >/dev/null 2>&1; done
echo "=== deps (want @rpath/* + /usr/lib only) ==="
for f in *.dylib; do echo "-- $f (id=$(otool -D "$f"|tail -n1))"; otool -L "$f" | tail -n +2; done

echo "=== @rpath isolation test ==="
mkdir -p /tmp/iso2 && cp *.dylib /tmp/iso2/
cat > /tmp/iso2/t.c <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(){ void*h=dlopen("libpq.5.dylib",RTLD_NOW); if(!h){printf("FAIL %s\n",dlerror());return 1;}
  void*(*v)(void)=dlsym(h,"PQlibVersion"); printf("OK libpq loaded via rpath\n"); return 0; }
EOF
# build with LC_RPATH=@loader_path so bare "libpq.5.dylib"->@rpath resolves beside the exe
clang -arch arm64 -Wl,-rpath,@loader_path /tmp/iso2/t.c -o /tmp/iso2/t \
  -L/tmp/iso2 -lpq
cd /tmp/iso2 && env -i ./t
