# Official XDJ-XZ 1.26 updater import qualification

2026-09-12. Host-only read/import/hash tests. No XZ access, USB writes, flash, application restart or key-byte output/copy.

## Result

`packages/xdj-xz-toolkit/builder/firmware.py::import_application` successfully imported the actual OEM XDJXZ.UPD using the existing local key path. It returned `(patched_application, True)` and the expected patched application identity. A wrong all-zero effective key was rejected at ISO validation.

The exact official ZIP was independently downloaded as a stream for hashing and matched the existing local ZIP byte-for-byte by SHA256. Downloaded ZIP bytes were not saved. Only the existing ZIP's encrypted UPD was copied into `.tmp/private-xz-oem-import-audit/XDJXZ.UPD` for the importer test. No decrypted firmware was written to disk.

## Official source and identity

- Official working download, HTTPS200, fully streamed and hashed: https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip
- Official announcement confirms XDJ-XZ firmware1.26,11April2024: https://www.pioneerdj.com/en/news/2024/xdj-xz-xdj-rx3-xdj-rr-and-xdj-rx2-firmware/
- ZIP bytes:70775680.
- ZIP SHA256:`7c4159ca90b0cfd651725a68e10a11cf1a621239f703a0071f24355cc89fed7f`.
- Existing matching local ZIP:`E:/Github/XDJXZMe/XDJXZ_v126.zip`.
- ZIP contains exactly one file:`XDJXZ.UPD`.
- UPD bytes:70754320.
- UPD SHA256:`be0d8709cc859774f8a5785fa702ae909a3c78896ea95982935a2965376dc0a5`.
- UPD has16 trailer bytes beyond512-byte sector alignment. The importer decrypts the70754304 aligned bytes.
- Download verification JSON:`.tmp/private-xz-oem-import-audit/download-verification.json`.

The old Pioneer support URLs returned403 or redirected during this check. The AlphaTheta downloads endpoint above was verified directly, not merely inferred from naming.

## Actual archive layout and parser result

The aligned decrypted UPD is a Rock Ridge ISO9660 image. Its relevant member is `/images/pdj.tar.gz`. That tar contains `pdj/rbp`.

- Extracted stock rbp bytes:7009456.
- Stock rbp SHA256:`6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2`, matching firmware.py's pinned STOCK_SHA.
- Patched result bytes:7009456.
- Patched result SHA256:`3a7c6c507ea67b2484d0cbc58136a495374c8ede5eeef1ffdffcfe86c7a3dae3`.
- Expected patched MD5 checked inside importer:`6a7ccb454e52afa26a73f3380706c9ca`.
- Returned key/image verification flag:True.
- Wrong zero key rejected with `Image did not decrypt to ISO9660; check the boot key and input file`.

This verifies the key against a known genuine OEM encrypted input plus the exact decoded application hash. A generated-image self-round-trip with an arbitrary key is not equivalent evidence.

## GUI scope

The tested API accepts the extracted `.UPD`, not the outer ZIP. The GUI can truthfully instruct users to download the official ZIP, extract it, then select XDJXZ.UPD and their local key. To support selecting the ZIP directly, add a bounded ZIP importer selecting exactly the verified member and test it against this archive; do not claim that path is already covered by the UPD test.

Qualification applies to this exact1.26 archive. Unknown updater versions/variants must continue to fail on application identity rather than inherit a broad OEM-UPD support claim. Key provisioning and redistribution permissions are separate from successful parsing. Public resources still exclude firmware and key material.

The tests establish host import, archive provenance/hash equality and key rejection. They do not establish that a newly generated mod image boots, or authorize any current hardware test.
