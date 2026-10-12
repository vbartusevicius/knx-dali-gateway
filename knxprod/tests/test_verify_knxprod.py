"""verify_knxprod.py accepts a well-formed archive and names each defect."""

import base64
import hashlib
import sys
import zipfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_knxprod as v

HEADER = "#define MAIN_OpenKnxId 0xFF\n#define MAIN_ApplicationNumber 90\n#define MAIN_ApplicationVersion 1\n"
NS = "http://knx.org/xml/project/20"
DIGEST = hashlib.md5(b"application").digest()
APP = f"M-00FA_A-FF5A-01-{DIGEST[0]:02X}{DIGEST[-1]:02X}"
HP = f"M-00FA_H-0xFF5A-1_HP-FF5A-01-{DIGEST[0]:02X}{DIGEST[-1]:02X}"


def archive(tmp_path, ns=NS, app=APP, hp=HP, catalog_hp=HP, signed=True, hash_=None):
    hash_ = base64.b64encode(DIGEST).decode() if hash_ is None else hash_
    files = {
        "M-00FA/Catalog.xml": f'<KNX xmlns="{ns}"><CatalogItem Hardware2ProgramRefId="{catalog_hp}"/></KNX>',
        "M-00FA/Hardware.xml": f'<KNX xmlns="{ns}"><Hardware2Program Id="{hp}">'
                               f'<ApplicationProgramRef RefId="{app}"/></Hardware2Program></KNX>',
        f"M-00FA/{app}.xml": f'<KNX xmlns="{ns}"><ApplicationProgram Id="{app}" Hash="{hash_}"/></KNX>',
    }
    if signed:
        files["M-00FA.signature"] = b"\xef\xbb\xbf" + base64.b64encode(bytes(128))
        files["knx_master.xml"] = f'<KNX xmlns="{ns}"/>'
    path = tmp_path / "test.knxprod"
    with zipfile.ZipFile(path, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return path


def test_well_formed_signed_archive_passes(tmp_path):
    assert v.verify(archive(tmp_path), HEADER, signed=True) == []


def test_unsigned_archive_fails_only_when_a_signature_is_required(tmp_path):
    path = archive(tmp_path, signed=False)
    assert v.verify(path, HEADER) == []
    assert any("not signed" in p for p in v.verify(path, HEADER, signed=True))


def test_ets6_only_schema_is_rejected(tmp_path):
    assert any("schema" in p for p in v.verify(archive(tmp_path, ns="http://knx.org/xml/project/23"), HEADER))


def test_application_id_must_match_the_firmware_header(tmp_path):
    other = HEADER.replace("ApplicationVersion 1", "ApplicationVersion 2")
    assert any("firmware header" in p for p in v.verify(archive(tmp_path), other))


def test_stale_fingerprint_in_catalog_is_caught(tmp_path):
    stale = HP[:-4] + "0000"
    assert any("Catalog.xml" in p for p in v.verify(archive(tmp_path, catalog_hp=stale), HEADER))


@pytest.mark.parametrize("hash_", ["", base64.b64encode(bytes(16)).decode()])
def test_missing_or_mismatching_hash_is_caught(tmp_path, hash_):
    assert v.verify(archive(tmp_path, hash_=hash_), HEADER)
