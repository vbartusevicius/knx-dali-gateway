"""Structural check of a built .knxprod against the firmware header.

    python3 knxprod/scripts/verify_knxprod.py <file.knxprod> <KnxDaliGateway.h> [--signed]

Fails (exit 1, one line per problem) unless the archive holds exactly the
Catalog, Hardware and one application program of one manufacturer, all in
schema project/20 (imported by ETS 5.7 and ETS 6); the application id carries
the header's OpenKNX id, application number and version; Catalog and Hardware
reference that exact application (fingerprint included); no name or text
says "OpenKNX" (reserved for the OpenKNX organisation's projects); every
ParameterRefRef/ComObjectRefRef resolves, and every <choose> names an existing
ParameterRef with <when> tests in KNX syntax (ETS crashes on a <choose> without
ParamRefId when the device is inserted); the application
program has its registration hash and the id's fingerprint derives from it
(first and last hash byte, as ETS computes it). With --signed it also requires the
manufacturer signature and the embedded knx_master.xml.
"""

import base64
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

NAMESPACE = "http://knx.org/xml/project/20"
RESERVED_NAME = "openknx"
TOOL_ATTRIBUTES = {"CreatedBy", "ToolVersion"}
SIGNATURE_BYTES = 128


def header_ids(header_text):
    def define(name):
        match = re.search(rf"#define MAIN_{name}\s+(\S+)", header_text)
        return int(match.group(1), 0)

    return define("OpenKnxId"), define("ApplicationNumber"), define("ApplicationVersion")


def namespace(root):
    return root.tag[1:].split("}")[0] if root.tag.startswith("{") else ""


def reserved_names(name, root):
    found = []
    for element in root.iter():
        texts = [(k, v) for k, v in element.attrib.items() if k not in TOOL_ATTRIBUTES]
        texts.append(("text", element.text or ""))
        found += [f"{name}: <{element.tag.split('}')[-1]}> {k} {v.strip()[:60]!r} uses the reserved name OpenKNX"
                  for k, v in texts if RESERVED_NAME in v.lower()]
    return found


SEPARATOR_HINTS = {None, "Headline", "HorizontalRuler", "Information", "Error"}
WHEN_TEST = re.compile(r"(?:(?:=|!=|<|>|<=|>=)-?\d+|-?\d+(?: -?\d+)*)")


def dynamic_problems(name, root):
    ids = {e.get("Id") for e in root.iter() if e.get("Id")}
    refs = {e.get("Id") for e in root.iter() if e.tag.endswith("}ParameterRef")}
    problems = []
    for element in root.iter():
        tag = element.tag.split("}")[-1]
        if tag in ("ParameterRefRef", "ComObjectRefRef") and element.get("RefId") not in ids:
            problems.append(f"{name}: <{tag}> RefId {element.get('RefId')!r} does not resolve")
        elif tag == "choose" and element.get("ParamRefId") not in refs:
            problems.append(f"{name}: <choose> ParamRefId {element.get('ParamRefId')!r} is not a ParameterRef")
        elif tag == "ParameterSeparator" and element.get("UIHint") not in SEPARATOR_HINTS:
            problems.append(f"{name}: <ParameterSeparator> UIHint {element.get('UIHint')!r} is not one ETS renders")
        elif tag == "when" and element.get("default") != "true" and not WHEN_TEST.fullmatch(element.get("test", "")):
            problems.append(f"{name}: <when> test {element.get('test')!r} is not KNX syntax")
    return problems[:20]


def verify(archive, header_text, signed=False):
    problems = []
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        manufacturers = {n.split("/")[0] for n in names if "/" in n}
        if len(manufacturers) != 1:
            return [f"expected one manufacturer folder, found {sorted(manufacturers)}"]
        manu = manufacturers.pop()
        apps = [n for n in names if re.fullmatch(rf"{manu}/{manu}_A-[0-9A-F]{{4}}-[0-9A-F]{{2}}-[0-9A-F]{{4}}\.xml", n)]
        for required in (f"{manu}/Catalog.xml", f"{manu}/Hardware.xml"):
            if required not in names:
                problems.append(f"missing {required}")
        if len(apps) != 1:
            problems.append(f"expected one application program, found {apps}")
        if problems:
            return problems
        docs = {name: ET.fromstring(z.read(name)) for name in (f"{manu}/Catalog.xml", f"{manu}/Hardware.xml", apps[0])}
        for name, root in docs.items():
            if namespace(root) != NAMESPACE:
                problems.append(f"{name}: schema {namespace(root)!r}, ETS 5.7 and ETS 6 need {NAMESPACE}")
            problems += reserved_names(name, root)

        app_id = apps[0].split("/")[1][:-4]
        openknx, number, version = header_ids(header_text)
        expected = f"{manu}_A-{openknx:02X}{number:02X}-{version:02X}-"
        if not app_id.startswith(expected):
            problems.append(f"application {app_id} does not match the firmware header ({expected}XXXX)")

        ns = {"k": NAMESPACE}
        program = docs[apps[0]].find(".//k:ApplicationProgram", ns)
        if program is None or program.get("Id") != app_id:
            problems.append(f"{apps[0]}: no ApplicationProgram with Id {app_id}")
        elif not program.get("Hash"):
            problems.append(f"{apps[0]}: ApplicationProgram has no registration Hash")
        else:
            digest = base64.b64decode(program.get("Hash"))
            if f"{digest[0]:02X}{digest[-1]:02X}" != app_id[-4:]:
                problems.append(f"{app_id}: fingerprint does not match the registration Hash")
        problems += dynamic_problems(apps[0], docs[apps[0]])
        if program is not None:
            for name in ("ContextHelpFile", "IconFile"):
                if "%" in program.get(name, ""):
                    problems.append(f"{apps[0]}: unresolved {name}: {program.get(name)}")
        refs = [e.get("RefId") for e in docs[f"{manu}/Hardware.xml"].iterfind(".//k:ApplicationProgramRef", ns)]
        hp_ids = {e.get("Id") for e in docs[f"{manu}/Hardware.xml"].iterfind(".//k:Hardware2Program", ns)}
        catalog_hps = {e.get("Hardware2ProgramRefId") for e in docs[f"{manu}/Catalog.xml"].iterfind(".//k:CatalogItem", ns)}
        if refs != [app_id]:
            problems.append(f"Hardware.xml references {refs}, the application is {app_id}")
        if not catalog_hps or not catalog_hps <= hp_ids:
            problems.append(f"Catalog.xml references {sorted(catalog_hps - hp_ids)}, Hardware.xml has {sorted(hp_ids)}")
        if any(not hp.endswith(app_id.split("_A-")[1]) for hp in hp_ids):
            problems.append(f"Hardware2Program ids {sorted(hp_ids)} do not carry the fingerprint of {app_id}")

        if signed:
            signature = f"{manu}.signature" if f"{manu}.signature" in names else f"{manu}/{manu}.signature"
            if signature not in names:
                problems.append(f"missing {manu}.signature: the archive is not signed")
            else:
                raw = z.read(signature).removeprefix(b"\xef\xbb\xbf")
                try:
                    if len(base64.b64decode(raw, validate=True)) != SIGNATURE_BYTES:
                        problems.append(f"{signature} is not a {SIGNATURE_BYTES}-byte RSA signature")
                except ValueError:
                    problems.append(f"{signature} is not base64")
            if "knx_master.xml" not in names:
                problems.append("missing knx_master.xml")
            elif namespace(ET.fromstring(z.read("knx_master.xml"))) != NAMESPACE:
                problems.append("knx_master.xml is not the project/20 master")
    return problems


def main(argv):
    archive, header = argv[1], argv[2]
    with open(header, encoding="utf-8") as f:
        problems = verify(archive, f.read(), "--signed" in argv[3:])
    for problem in problems:
        print(f"{archive}: {problem}")
    if not problems:
        print(f"{archive}: OK ({'signed' if '--signed' in argv[3:] else 'structure only, unsigned'})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
