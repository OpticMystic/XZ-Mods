# Antivirus false positives and code signing

XZ Mods ships unsigned Windows binaries. New unsigned programs have no reputation, so machine-learning
detections such as `Trojan:Win32/Bearfoos` (issue #2) and SmartScreen's "Windows protected your PC" can hit a
clean release. This page covers what every release does about it, where to report a false positive, and the
paths to code signing on Windows and macOS. Vendor details were checked on 2026-11-03.

## Every release

`tools/build_release.py` now finishes by running `tools/prepare_av_submission.py`. It writes a folder beside the
release output, for example `dist/0.5.2-av-submission/`:

| File | Contents |
| --- | --- |
| `CHECKLIST.md` | Where to submit what, in order, and the audit findings to fix before publishing |
| `submissions/<vendor>.txt` | Where and how to submit, form fields, and ready-to-paste text with the version, links and SHA-256s |
| `upload/` | Copies of the first-party Windows executables (the app, its service, the native helpers) |
| `SHA256SUMS.txt` | SHA-256 of the installer, the portable ZIP and every Windows PE file inside the installer |
| `manifest.json` | The same list with PE details: version resource, manifest, signature, debug path, sections, packer marks |

The folder is not a release asset. Nothing is uploaded unless you pass one of the opt-in flags below.

1. Before publishing, read the **Findings** in `CHECKLIST.md` and fix every "fix" line or accept it on purpose.
2. Publish the release. The tool reads the tag from `latest.json`; if you publish under a different tag, rerun it
   with `--tag <tag>` so the links in the text are right.
3. Submit the first-party files to Microsoft as described in `CHECKLIST.md`, starting with `xz-mods-builder.exe`.
4. When a user reports a detection from another vendor, use that vendor's text file. Rerun with
   `--detection "<name from their report>"` to fill in the detection name.

The tool reads the installer itself when 7-Zip is installed, so it also works on an old release:

```powershell
python tools/prepare_av_submission.py --installer XZ.Mods_0.2.1_x64-setup.exe --tag v0.2.1 --detection "Trojan:Win32/Bearfoos"
python tools/prepare_av_submission.py --release-dir dist/0.5.2 --output dist/0.5.2-av-submission-2
python tools/test_prepare_av_submission.py
```

Without 7-Zip it reads the build tree instead (`--app-exe`, `--resources`), which `build_release.py` passes.

### Opt-in checks

| Flag | What leaves this computer |
| --- | --- |
| `--defender-scan` | Runs `MpCmdRun.exe -Scan -ScanType 3 -DisableRemediation` on the installer and `upload/`. Defender's own sample-submission setting decides whether Defender sends anything to Microsoft. |
| `--virustotal-lookup` | SHA-256s of the first-party files, sent to the VirusTotal API. Results land in `manifest.json` with each engine that flags a file. |
| `--virustotal-upload` | Also uploads the first-party files VirusTotal has not seen. Uploaded files are shared with VirusTotal's antivirus partners and premium users. |

Both VirusTotal flags need a free VirusTotal account key in `VT_API_KEY` and respect the free tier's 4 requests a
minute. VirusTotal is not a dispute channel: it shows which engines flag a file and shares the sample with them, but
each vendor still needs its own false-positive report. It is the only channel here that is automatable with a free
account, so it is the only one wired in.

## Microsoft

| Question | Answer |
| --- | --- |
| Where | https://www.microsoft.com/en-us/wdsi/filesubmission, role **Software developer** ("Software providers wanting to validate detection of their products") |
| Account | A free personal Microsoft account. Signing in is needed to track submissions and get the result email. |
| Size limit | 50 MB per file. Archives must use the password `infected`. Microsoft asks for the specific files, not whole installers. |
| What to include | Product (Microsoft Defender Antivirus, or Microsoft Defender SmartScreen for a SmartScreen block), company name, detection name, the file, and the additional information text from `submissions/microsoft.txt` |
| Turnaround | Developer submissions get no priority. Medium priority means analyst review "within a few days"; low priority may never be reviewed. Results arrive by email. |
| Status | https://www.microsoft.com/en-us/wdsi/submissionhistory shows Submitted, In progress or Closed. "Rescan" on a submission shows the latest verdict. Details are kept for 30 days. |
| Disputing a verdict | Use the developer contact form shown with the submission result. |
| API or CLI | None for personal accounts. The Defender portal's unified submissions (500 MB, file or hash) need Microsoft Defender for Endpoint Plan 2 or Defender XDR. The Graph `threatSubmission` API is beta, needs a work or school account (personal accounts are not supported) and a Defender tenant. Neither is usable without an enterprise subscription. |

The 0.5.1-Beta.1 app executable is 87.6 MB because the interface, localisation data and manual media are compiled
into it, so it cannot be uploaded to Microsoft. Its SHA-256 and the download link go in the text instead. Keeping
`xz-mods-builder.exe` under 50 MB (loading large manual media from `resources/` instead) would make it submittable.

**SmartScreen** is reputation, not detection. Microsoft says there is "no need (or mechanism)" to submit a file for
SmartScreen reputation on consumer PCs; it builds from download volume. Unsigned files start from zero with every
version, while signed files can inherit the publisher certificate's reputation. A SmartScreen block on a file that
Defender does not detect can still be reported through the same portal with the product set to SmartScreen. On
Windows 11 with Smart App Control on, unsigned files without reputation are blocked outright.

## Other vendors

| Vendor | Where | How | Limits and notes |
| --- | --- | --- | --- |
| Avast and AVG (Gen Digital) | https://www.avast.com/false-positive-file-form.php | Web form, covers both engines | Email DL-Virus@gendigital.com for large files. AVG form: https://www.avg.com/submit-sample |
| Norton, Symantec (Gen Digital, Broadcom) | https://submit.norton.com/ | Web form; answer "Yes, I am the vendor" | Uploads above about 90 MB fail (developer reports); give hash and URL. Broadcom: https://symsubmit.symantec.com |
| Kaspersky | https://opentip.kaspersky.com/ | Scan the file or hash, press "Submit to reanalyze", leave an email | Fallback: newvirus@kaspersky.com. Developer allowlist: https://www.kaspersky.com/partners/allowlist-program |
| ESET | samples@eset.com | Email, subject "False positive", file in a ZIP with password `infected` | No reply unless ESET needs more. https://support.eset.com/en/kb141 |
| Bitdefender | https://www.bitdefender.com/submit/ | Web form, category "False positive" | Uploads above about 20 MB fail (user reports); use virus_submission@bitdefender.com with a link or a ZIP with password `infected` |
| McAfee, Trellix | https://www.mcafee.com/support/s/article/000001921 | Follow the article or email virus_research@mcafee.com | Trellix enterprise: datasubmission@trellix.com |
| Malwarebytes | https://forums.malwarebytes.com/forum/122-false-positives/ | Forum post with a free account | Paid subscribers can use support instead. https://help.malwarebytes.com/hc/en-us/articles/31589211404571 |
| Sophos | https://support.sophos.com/ ("Submit a sample") | Web form or samples@sophos.com with a ZIP using password `infected` | Sophos may refuse MSI files; the NSIS .exe is fine |
| Trend Micro | https://www.trendmicro.com/en_us/about/legal/detection-reevaluation.html | "File Re-classify Request" form (https://forms.office.com/r/cbngNKqZ6H) | For non-customers. Email: virus_doctor@trendmicro.com |

VirusTotal keeps a longer list of vendor contacts: https://docs.virustotal.com/docs/false-positive-contacts.
Rows marked "reports" come from developer forum posts, not vendor documentation.

## Code signing for Windows

No option gives instant SmartScreen reputation. Microsoft removed the EV certificate bypass in 2024; every signed
release still starts with warnings, but reputation then follows the certificate from release to release instead of
resetting with each new unsigned file. Signing also removes a strong feature from antivirus models: an unsigned
executable from an unknown publisher.

| Option | Cost | Who can get it | Publisher users see | Fit for XZ Mods |
| --- | --- | --- | --- | --- |
| SignPath Foundation | Free for open-source projects | Projects that meet its conditions, at SignPath's discretion | "SignPath Foundation" | Needs the Windows build moved to GitHub Actions on GitHub-hosted runners, manual approval of each release, MFA for everyone with access, and a "Code signing policy" on the home and download pages. Apply at https://signpath.org/apply. |
| Azure Artifact Signing (formerly Trusted Signing) | USD 9.99 a month (5,000 signatures), needs a paid pay-as-you-go Azure subscription | Individuals in the USA and Canada only; organisations in the USA, Canada, EU and UK | Your validated legal name | Works with the current local build through Tauri's `signCommand`; identity validation takes a few business days and uses a government ID and Microsoft Authenticator. |
| Certum Open Source Code Signing | About EUR 49 a year in the cloud (SimplySign), or EUR 69 with a card and reader | Individual open-source developers, worldwide | "Open Source Developer, <your name>" | Signs locally with signtool through SimplySign Desktop. Hard to automate in CI. Certificates now last at most 460 days. |
| OV certificate from a commercial CA | USD 150 to 300 a year plus a hardware token | Worldwide | Your name or company | Same SmartScreen behaviour as Artifact Signing. |
| Microsoft Store | Free individual developer account | Worldwide | Microsoft | MSIX packages are re-signed by Microsoft and never get SmartScreen warnings. Tauri does not build MSIX, and the in-app updater and installer hooks would need rework. A Store-submitted EXE installer must be signed by you. |

**Recommendation.** Apply to SignPath Foundation now: it is free, and the time it takes to review the project is
time you can spend moving the Windows build into GitHub Actions, which it requires. If you live in the USA or
Canada and can spend USD 10 a month, Azure Artifact Signing is the quickest to switch on (days, no build changes
beyond a sign command) and can be cancelled any month; Certum Open Source in the cloud is the cheapest per year
anywhere in the world. Pick one and keep it: SmartScreen reputation follows the signing identity, so switching
later restarts it.

### Tauri configuration

Tauri (CLI 2.12) signs the app executable, the NSIS plug-in DLLs, the uninstaller and the installer. It does not sign files under
`resources/`, so `xz-mods-service.exe`, `xz-audio-helper.exe` and `xz-overcue-check.exe` must be signed in
`prepare_resources.py` before bundling (`uv.exe` belongs to Astral and is left as published). In
`src-tauri/tauri.conf.json`, under `bundle.windows`:

```json
"signCommand": "artifact-signing-cli -e https://<region>.codesigning.azure.net -a <account> -c <profile> -d \"XZ Mods\" %1"
```

For a certificate in the Windows store (Certum through SimplySign Desktop, or an OV token):

```json
"certificateThumbprint": "<thumbprint>", "digestAlgorithm": "sha256", "timestampUrl": "http://time.certum.pl"
```

SignPath signs in its own service: the workflow uploads the unsigned files with `actions/upload-artifact`, then
`signpath/github-action-submit-signing-request` returns them signed. The installer must be built after the inner
files are signed, so the Windows workflow signs `resources/` and the app executable first, then bundles, then signs
the installer. Reference: https://v2.tauri.app/distribute/sign/windows/ and
https://docs.signpath.io/trusted-build-systems/github.

## Mac: Apple Developer Program

USD 99 a year. Enrol as an individual with an Apple Account that has two-factor authentication and your legal name;
fee waivers exist only for nonprofits, schools and government bodies. It unlocks the **Developer ID Application**
certificate (created by the account holder) and notarization. A Developer ID signed, notarized and stapled app opens
without the "cannot be opened" block that ad-hoc signed apps get.

What changes in this repository:

- `src-tauri/tauri.macos.conf.json`: replace `"signingIdentity": "-"` with
  `"Developer ID Application: <Name> (<TEAMID>)"`, or set `APPLE_SIGNING_IDENTITY`.
- `tools/build_macos.py`: the loop that ad-hoc signs every Mach-O file in `resources/` (`codesign --force --sign -`)
  must sign with the same identity plus `--options runtime --timestamp`, because notarization requires the hardened
  runtime and a secure timestamp on every executable, including `uv`, the PyInstaller backend and its dylibs. Test
  the frozen Python backend under the hardened runtime; it may need an entitlements file.
- Notarization: set `APPLE_API_ISSUER`, `APPLE_API_KEY` and `APPLE_API_KEY_PATH` (an App Store Connect API key;
  recommended for CI) or `APPLE_ID`, `APPLE_PASSWORD` (an app-specific password) and `APPLE_TEAM_ID`. Tauri then
  notarizes during `tauri build`. Notarize and staple the DMG too (`xcrun notarytool submit --wait`,
  `xcrun stapler staple`), and check with `spctl -a -vv` and `xcrun stapler validate`.
- `.github/workflows/build-macos-release.yml`: add the certificate as `APPLE_CERTIFICATE` (base64 .p12) and
  `APPLE_CERTIFICATE_PASSWORD`, plus the API key, as repository secrets.

Prepare now: create the Apple Account with your legal name and two-factor authentication, so enrolment is a payment
away. Reference: https://developer.apple.com/programs/enroll/ and https://v2.tauri.app/distribute/sign/macos/.

## Heuristic triggers in the Windows build

Checked against the published 0.2.1 and 0.5.1-Beta.1 installers.

| Trigger | Status |
| --- | --- |
| App exe had a lowercase "opticmystic" company and no copyright; installer had neither | Fixed: `bundle.publisher` and `bundle.copyright`, and `CompanyName` added to the installer through `installer-hooks.nsh` |
| `xz-mods-service.exe` (PyInstaller bootloader) had no version resource and the stock PyInstaller icon | Fixed in `build_backend.py`: version resource and the app icon on Windows |
| Installer downloaded `MicrosoftEdgeWebview2Setup.exe` to `%TEMP%` and ran it (`NSISdl`) | Fixed: `embedBootstrapper` ships Microsoft's signed bootstrapper inside the installer (about 1.8 MB) |
| `xz-audio-helper.pdb` and `xz-overcue-check.pdb` shipped in `resources/` | Fixed in `prepare_resources.py` |
| Zig-built `xz-audio-helper.exe` and `xz-overcue-check.exe` have no version resource or manifest | Open. Their build scripts live in the xdj-xz-toolkit repository; pass an `.rc` file with `VERSIONINFO` and a manifest to `zig cc` there. |
| The stock PyInstaller bootloader is shared with a lot of malware | Open. Building the bootloader from source gives a unique binary; worth doing if detections continue. |
| Unsigned binaries | Open until a signing option above is in place. |
| Downloads and runs code by design: uv installs Python and PyTorch for stem generation; the updater runs signed installers | By design; described in the submission text. |
| No UPX or other packer; ASLR and DEP on every first-party file | Fine |
| "inject", "keylog" and "hook" strings | Only in libraries: rustls key logging, WebView2, CPython, uv. No first-party code. |
| Player-only tools (`xz_directfb_hook`, `xz-updater`, `xz-doom`, the runtime) | Ship as ARM Linux ELF files and source, never as Windows PE files. The tool flags any PE file under `resources/runtime/`. |

## Sources

- Microsoft submission guide: https://learn.microsoft.com/en-us/defender-xdr/submission-guide
- Microsoft sample submission portal: https://www.microsoft.com/en-us/wdsi/filesubmission
- Defender for Endpoint submissions: https://learn.microsoft.com/en-us/defender-endpoint/admin-submissions-mde
- Graph fileThreatSubmission (beta): https://learn.microsoft.com/en-us/graph/api/security-filethreatsubmission-post-filethreats?view=graph-rest-beta
- SmartScreen overview: https://learn.microsoft.com/en-us/windows/security/operating-system-security/virus-and-threat-protection/microsoft-defender-smartscreen/
- SmartScreen reputation for developers: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- Code signing options for Windows: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- Artifact Signing FAQ: https://learn.microsoft.com/en-us/azure/artifact-signing/faq
- SignPath Foundation terms: https://signpath.org/terms
- Certum Open Source Code Signing: https://shop.certum.eu/open-source-code-signing.html and https://shop.certum.eu/open-source-code-signing-on-simplysign.html
- VirusTotal false-positive contacts: https://docs.virustotal.com/docs/false-positive-contacts
- Kaspersky false detections: https://support.kaspersky.com/us/1870
- Trend Micro detection re-evaluation: https://www.trendmicro.com/en_us/about/legal/detection-reevaluation.html
- Tauri NSIS template: https://github.com/tauri-apps/tauri/blob/dev/crates/tauri-bundler/src/bundle/windows/nsis/installer.nsi
