# Building the .knxprod

## Local signed build (tested)

1. Put the libraries from the same ETS 5.7 installation in `knxprod/ETS/`:
   - `Knx.Ets.XmlSigning.dll`
   - `Knx.Ets.Xml.ObjectModel.dll`
   - `Knx.Ets.Xml.RegistrationRelevanceInformation.dll`
   - `Knx.Ets.ObjectModel.dll` and `log4net.dll` (included in the tested set)
2. Build: `docker compose --profile knxprod build producer producer-wine`.
3. Sign: `ETS_CV_DIR=knxprod/ETS knxprod/scripts/package.sh`.
4. Output: `knxprod/build/KnxDaliGateway.knxprod`.

No Windows VM or extracted private key is needed. The libraries are ignored by
Git and excluded from Docker build contexts. They are mounted read-only and
copied into a disposable Wine prefix, never baked into an image.

## Chain

- Native OpenKNXproducer 4.3.12 expands includes/channels and generates the header.
- `prepare_xml.py` removes the upstream template's unused help/icon placeholders
  and provides a Hash attribute, preserving real assets and embedded scripts.
- Windows OpenKNXproducer 4.3.12 runs under Wine 10 in an amd64 Linux container:
  ETS's own libraries hash, sign and package the expanded XML.
- `verify_knxprod.py` checks structure, header/application identity, registration
  hash/fingerprint, cross-file references, signature presence/size and master data.
  This is a structural check, not cryptographic signature verification or an ETS
  import test.

The native Linux signer fails on ETS 5.7's Windows `shlwapi.dll` call. Wine
implements that API. Its built-in `mscoree` must remain enabled so .NET can load
ETS assemblies (`WINEDLLOVERRIDES="mscoree=b;mshtml="`). On Apple Silicon,
Wine startup emits Rosetta/syswow64 warnings, but the tested signing command
returns success and the resulting archive passes the verifier.

Both OpenKNXproducer binaries and the `project/20` KNX master data are fetched
from upstream with SHA-256 checks. Master data is cached in the Wine image so
signing does not require a runtime download.

Without `ETS_CV_DIR`, the script uses knx-rs-prod 0.10.1 for unsigned packaging.
An already supplied `ETS_SIGNING_KEY_FILE` is an optional alternative; no key
extraction is performed by this repository. Unsigned files cannot be imported
into ETS.

## Linux pipeline

`.github/workflows/knxprod.yml` supports the same Wine route:

- ZIP the licensed ETS DLL set and encrypt it with GPG symmetric encryption
  (AES-256, a strong unique passphrase). Store only the encrypted bundle at
  `knxprod/ets-cv.zip.gpg`, subject to your redistribution/licensing permissions.
- Set repository secret `ETS_CV_PASSPHRASE` to that passphrase. Do not commit it
  or the plaintext ZIP/DLLs. The encrypted bundle has not been created here.
- Push jobs decrypt into the runner's temporary directory, mount the DLLs into
  Wine, build/verify, and delete the temporary inputs. Pull requests never
  receive the secret and build unsigned.
- Tags require signing inputs and attach the result to the release. Without
  signing inputs, ordinary pushes produce unsigned artifacts with a warning.
- Existing `ETS_SIGNING_KEY` support remains as a fallback, but is unnecessary
  with the Wine route.

This workflow has not yet been run on GitHub.

## ETS compatibility and remaining checks

The product uses schema `project/20`, targeting ETS **5.7 and ETS 6**; earlier
ETS5 versions are not claimed. The signed archive was generated locally using
ETS libraries version 5.7.298.38537. Actual import, project insertion and device
download in ETS 5.7 and ETS 6 are still required.

Full XSD validation is still unavailable: obtain the `project/20` schema
folder (including its imports) from KNX Manufacturer Tool 5.7 or KNX member
support. It is separate from ETS's runtime DLLs and is not included here;
Wine signing currently uses `-N`, explicitly skipping that validation.

Product names: OpenKNXproducer's built-in template prefixes the catalog section,
catalog item, product, hardware and application names with "OpenKNX". That name
is reserved for projects of the OpenKNX GitHub organisation (they asked us not to
distribute with it), so `scripts/prepare_xml.py` replaces those names before
hashing and signing, and `scripts/verify_knxprod.py` fails any archive whose
names or texts still say "OpenKNX". Only the producer's tool attribution
(`CreatedBy`, its XML namespace URI) keeps the word.

Before distribution, confirm allocation of application number 90 and OpenKNX
id `0xFF` under manufacturer M-00FA with OpenKNX, or use your own assigned
manufacturer/product identity.
