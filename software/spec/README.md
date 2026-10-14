# The system specification

What the two firmwares of a repeater-v2 unit must agree on. Start with [`SYSTEM.md`](SYSTEM.md).

| File | What |
|---|---|
| [`SYSTEM.md`](SYSTEM.md) | the plan: the repositories, the unit, the decisions (`S-1`…), the order of work, what is open |
| [`LINK-PROTOCOL.md`](LINK-PROTOCOL.md) | the UART between the LoRa box and the LTE box, version 1.0 |
| [`FIRMWARE-MANIFEST.md`](FIRMWARE-MANIFEST.md) | what a release says about itself, how it is signed, and the descriptor inside an image |
| [`fixtures/link/link-v1.json`](fixtures/link/link-v1.json) | every message of the link as bytes; both firmwares test against it |
| [`fixtures/manifest/`](fixtures/manifest/) | signed manifests, images with descriptors, and the ones that must be refused |
| [`tools/linkref.py`](tools/linkref.py) | the link as code; generates and checks its fixtures |
| [`tools/manifestref.py`](tools/manifestref.py) | the manifest, signature and descriptor as code; generates and checks its fixtures; makes a key |
| [`tools/specsync.py`](tools/specsync.py) | copies this directory into a firmware repository and checks a copy |
| [`proposals/`](proposals/) | what the MeshMaster side needs; proposals, not decisions |

## Changing it

This directory is the only place the specification is edited. Then:

```
python3 tools/linkref.py gen fixtures/link/link-v1.json      # when a message changed
python3 tools/manifestref.py gen fixtures/manifest           # when the manifest or descriptor changed
python3 tools/specsync.py copy ../../../twinoak.meshcore.repeater-firmware
python3 tools/specsync.py copy ../../../twinoak.meshcore.walter-firmware
```

and each firmware repository gets a task for what the change means there. A wire change is a new
protocol version, not an edit: fixtures of an existing version do not change once a firmware that
speaks it has been released.

The fixtures are bytes, not text. `.gitattributes` here stops git touching a line ending in this
directory, because the copies are compared by hash.
