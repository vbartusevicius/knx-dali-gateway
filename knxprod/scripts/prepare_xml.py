import re
import sys
from pathlib import Path


def prepare(text):
    def application(match):
        tag = match.group(0)
        tag = re.sub(r' ContextHelpFile="%FILE-HELP-de%"| IconFile="%FILE-ICONS%"', "", tag)
        if not re.search(r'\sHash=', tag):
            tag = tag.replace("<ApplicationProgram ", '<ApplicationProgram Hash="" ', 1)
        return tag

    return re.sub(r"<ApplicationProgram\s[^>]*>", application, text)


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    target.write_text(prepare(source.read_text(encoding="utf-8-sig")), encoding="utf-8")
