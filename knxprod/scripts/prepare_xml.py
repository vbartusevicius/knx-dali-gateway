import re
import sys
from pathlib import Path

# OpenKNXproducer's built-in template names every product "OpenKNX: ..." in an
# "OpenKNX" catalog section. That name is reserved for the OpenKNX organisation's
# own projects, so it is replaced before the hash and signature are computed.
CATALOG_SECTION = {"Id": "M-00FA_CS-KnxDaliGateway", "Name": "KNX DALI Gateway", "Number": "KnxDaliGateway"}
RESERVED_PREFIX = re.compile(r'(\s(?:Name|Text)=")OpenKNX(?::\s*|-)')


def rebrand(text):
    def section(match):
        tag = match.group(0)
        for attr, value in CATALOG_SECTION.items():
            tag = re.sub(rf'(\s{attr}=")[^"]*"', rf'\g<1>{value}"', tag, count=1)
        return tag

    text = re.sub(r'<CatalogSection\s[^>]*Id="M-00FA_CS-OpenKNX"[^>]*>', section, text)
    return RESERVED_PREFIX.sub(r"\g<1>", text)


def prepare(text):
    def application(match):
        tag = match.group(0)
        tag = re.sub(r' ContextHelpFile="%FILE-HELP-de%"| IconFile="%FILE-ICONS%"', "", tag)
        if not re.search(r'\sHash=', tag):
            tag = tag.replace("<ApplicationProgram ", '<ApplicationProgram Hash="" ', 1)
        return tag

    return rebrand(re.sub(r"<ApplicationProgram\s[^>]*>", application, text))


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    target.write_text(prepare(source.read_text(encoding="utf-8-sig")), encoding="utf-8")
