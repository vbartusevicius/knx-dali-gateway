import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from prepare_xml import prepare


def test_removes_only_unused_generated_asset_references():
    source = '<KNX><ApplicationProgram Id="a" ContextHelpFile="%FILE-HELP-de%" IconFile="%FILE-ICONS%"/></KNX>'
    attrs = ET.fromstring(prepare(source)).find("ApplicationProgram").attrib
    assert attrs == {"Id": "a", "Hash": ""}


def test_preserves_real_assets_and_existing_hash():
    source = '<ApplicationProgram Hash="abc" Id="a" ContextHelpFile="help.zip" IconFile="icons.zip"/>'
    assert prepare(source) == source


def test_preserves_xml_namespace_and_script_content():
    source = '<KNX xmlns="http://knx.org/xml/project/20"><ApplicationProgram Id="a"/>' \
             '<Script><![CDATA[if (x < 3) { y = "%FILE-ICONS%"; }]]></Script></KNX>'
    result = prepare(source)
    assert result == source.replace('<ApplicationProgram Id=', '<ApplicationProgram Hash="" Id=')
    assert prepare(result) == result
