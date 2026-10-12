# Building the .knxprod

`knxprod/scripts/package.sh` builds `knxprod/build/KnxDaliGateway.knxprod` in the
`producer` container, locally and in CI (`.github/workflows/knxprod.yml`).

## ETS 5 and ETS 6

One file serves both: the product XML uses schema `project/20`, the newest
schema ETS 5.7 imports, and ETS 6 imports `project/20` product data as well. No
ETS 6-only (`project/21+`) feature is used; the verifier rejects any other
schema.

## Chain

| Step | Tool | Output |
| --- | --- | --- |
| Expand includes, channels, KO numbering | OpenKNXproducer 4.3.12 (`create -d`) | monolithic product XML + firmware header |
| Registration hash, fingerprint in every id, split, sign, embed master data, zip | knx-rs-prod 0.10.1 (pure Rust, GPL-3.0, SHA-256-pinned source) | `.knxprod` |
| Structure check | `scripts/verify_knxprod.py` (+ tests in `tests/`) | pass/fail |

`knx_master.xml` for `project/20` comes from update.knx.org, pinned by SHA-256
in the image. knx-rs-prod reproduces ETS's hashing and signature format
byte-for-byte (verified by its author against products of MDT, Gira, ABB,
Siemens and OpenKNX; ETS 6.3/6.4 import confirmed). OpenKNXproducer writes no
`Hash` attribute and knx-rs-prod only fills an existing one, so `package.sh`
inserts an empty one first.

## The signing key

ETS accepts only product data signed with the RSA key inside ETS's
`Knx.Ets.XmlSigning.dll`. It is the same key in every ETS installation, is
KNX Association property and is never committed, published or baked into an
image.

- Take it from your own ETS installation (ETS 5 or 6, the free demo is
  enough): open `Knx.Ets.XmlSigning.dll` from the ETS program folder in a .NET
  decompiler such as ILSpy and copy the embedded `<RSAKeyValue>` XML into a file
  outside the repository, e.g. `~/.config/knx-dali-gateway/ets-signing-key.xml`.
- Local signed build: `ETS_SIGNING_KEY_FILE=~/.config/knx-dali-gateway/ets-signing-key.xml knxprod/scripts/package.sh`
- CI: store the same file content as the repository secret `ETS_SIGNING_KEY`.
  Pushes to `main` build a signed file (artifact `knxprod`); pull requests build
  unsigned; a `v*` tag fails without the secret and attaches the signed file to
  the GitHub release.

Without a key the script still builds and verifies everything except the
signature; that archive is not importable.

## Verification

`verify_knxprod.py` checks the archive against the committed firmware header:
one manufacturer folder with Catalog, Hardware and one application program,
schema `project/20` throughout, application id = OpenKNX id + application number
+ version from the header, fingerprint = first and last byte of the
registration hash, Catalog and Hardware referencing exactly that application,
and for signed builds the 128-byte RSA signature and the `project/20` master.

Still manual, once per release: import into ETS 5.7 and ETS 6, add the device to
a project, download it to a gateway.

## Open points

- First signed build has not been imported into ETS yet (needs the key).
- Product identity: OpenKNX id `0xFF` with application number 90 is fine for
  own use; distribution needs an application number registered with OpenKNX
  (manufacturer M-00FA) or an own KNX manufacturer id.
