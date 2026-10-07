SUMMARY = "TI PRU code generation tools 2.3.3"
DESCRIPTION = "Fetches the official PRU CGT installer and unpacks it. The installer stub is i386, so the recipe runs it with Debian buster's 32-bit loader. clpru itself is a static x86-64 binary."
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

inherit native

SRC_URI = " \
    https://software-dl.ti.com/codegen/esd/cgt_public_sw/PRU/2.3.3/ti_cgt_pru_2.3.3_linux_installer_x86.bin;unpack=0;name=cgt \
    http://archive.debian.org/debian/pool/main/g/glibc/libc6-i386_2.28-10+deb10u1_amd64.deb;unpack=0;name=libc \
"
SRC_URI[cgt.sha256sum] = "1f1405167214f2c0ef848591b17a7799fdcd9f55f11bc90db9ace3490d426215"
SRC_URI[libc.sha256sum] = "7e825fa243f32b105f519a1f48f49b47a7c0289e92cb15dd18e1172dd886be25"

INSANE_SKIP:${PN} += "already-stripped"

do_configure[noexec] = "1"

do_compile () {
    # UNPACKDIR is a bitbake variable, not an exported environment variable.
    python3 - "${UNPACKDIR}" << 'PY'
import sys, tarfile, pathlib
workdir = pathlib.Path(sys.argv[1])
root = workdir / "i386-root"
root.mkdir(exist_ok=True)
deb = next(workdir.glob("libc6-i386_*.deb")).read_bytes()
if not deb.startswith(b"!<arch>\n"):
    sys.exit("libc6-i386 deb is not an ar archive")
off = 8
data = None
data_name = None
while off + 60 <= len(deb):
    hdr = deb[off:off + 60]
    name = hdr[0:16].decode().strip().rstrip("/")
    size = int(hdr[48:58].decode().strip())
    body = deb[off + 60:off + 60 + size]
    if name.startswith("data.tar"):
        data = body
        data_name = name
    off += 60 + size
    if off % 2:
        off += 1
if data is None:
    sys.exit("libc6-i386 deb has no data.tar")
blob = workdir / data_name
blob.write_bytes(data)
mode = "r:xz" if data_name.endswith(".xz") else "r:gz"
with tarfile.open(blob, mode) as tar:
    tar.extractall(root, filter="data")
PY
    install -d ${WORKDIR}/itmp ${WORKDIR}/cgt-prefix
    ldso=$(find ${UNPACKDIR}/i386-root -path '*/lib32/ld-linux.so.2' | head -n 1)
    libdir=$(dirname "$(find ${UNPACKDIR}/i386-root -path '*/lib32/libc.so.6' | head -n 1)")
    chmod +x ${UNPACKDIR}/ti_cgt_pru_2.3.3_linux_installer_x86.bin
    TMPDIR=${WORKDIR}/itmp "$ldso" --library-path "$libdir" \
        ${UNPACKDIR}/ti_cgt_pru_2.3.3_linux_installer_x86.bin \
        --mode unattended --prefix ${WORKDIR}/cgt-prefix
    test -x ${WORKDIR}/cgt-prefix/ti-cgt-pru_2.3.3/bin/clpru
}

do_install () {
    install -d ${D}${datadir}
    cp -a ${WORKDIR}/cgt-prefix/ti-cgt-pru_2.3.3 ${D}${datadir}/ti-cgt-pru
}

