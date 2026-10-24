import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from prepare_xml import CATALOG_DESCRIPTION, REPO_URL, prepare


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


PRODUCER_NAMES = (
    '<KNX CreatedBy="OpenKNXproducer">'
    '<CatalogSection Id="M-00FA_CS-OpenKNX" Name="OpenKNX" Number="OpenKNX" DefaultLanguage="de-DE">'
    '<CatalogItem Id="c" Name="OpenKNX: KNX DALI Gateway" Number="1"/></CatalogSection>'
    '<Hardware Id="h" Name="OpenKNX-KNX-DALI-GW-C6-proto"><Products>'
    '<Product Id="p" Text="OpenKNX: KNX DALI Gateway"/></Products></Hardware></KNX>'
)


def test_replaces_the_reserved_openknx_names_from_the_producer_template():
    root = ET.fromstring(prepare(PRODUCER_NAMES))
    section = root.find("CatalogSection")
    assert section.attrib == {"Id": "M-00FA_CS-KnxDaliGateway", "Name": "KNX DALI Gateway",
                              "Number": "KnxDaliGateway", "DefaultLanguage": "de-DE",
                              "VisibleDescription": CATALOG_DESCRIPTION}
    assert REPO_URL in section.find("CatalogItem").get("VisibleDescription")
    assert section.find("CatalogItem").get("Name") == "KNX DALI Gateway"
    assert root.find("Hardware").get("Name") == "KNX-DALI-GW-C6-proto"
    assert root.find(".//Product").get("Text") == "KNX DALI Gateway"
    assert root.get("CreatedBy") == "OpenKNXproducer"
    assert prepare(prepare(PRODUCER_NAMES)) == prepare(PRODUCER_NAMES)
