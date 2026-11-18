# Choose a mod version

Open **USB loader** and use the **Mod version** panel.

1. Choose a build and review its description and runtime fingerprint.
2. Leave **Keep this version on the USB** checked to prevent the generated loader from applying a staged network update at boot.
3. Click **Save choice**.
4. Choose **Back up and update** for an existing loader, or prepare a new USB.

Saving the choice changes this computer's preference. Building or updating the loader applies that choice to the selected USB. Restart the controller from that USB and verify its behavior before using the build in a set. Other bootable USB partitions can still contain different loaders.

The choice survives app updates. Each archived build keeps its own runtime, receiver, startup script, settings schema and supporting files. A missing or changed archive stops the operation. The builder does not substitute the newest version.

This selector changes the **XZ Mods runtime**. The supported Pioneer firmware remains **XDJ-XZ 1.26**. Preview and development builds are not certified stable. The original 0.2.2-preview.2 bundle is distinct from later development bundles included in an app with the same version label.

If an older build needs a different settings format, the app shows which fields it must remove or reset. **Back up settings and continue** retains the supported values and saves the original profile. If loader replacement fails while the original files are still present, the app restores the original profile. **Restore a previous loader** remains available for an exact loader rollback.

Use **Import a build** to archive another local prepared resource folder, or an extracted XZ Mods app folder containing `resources`. Import checks the manifest and file hashes. It does not establish the publisher's identity or controller stability.

To allow network updates again, uncheck **Keep this version on the USB**, save the choice, and update that USB's loader. Changing the checkbox alone does not alter an existing loader.

## Verification

Run the source tests from `source/xdj-xz-toolkit`:

```powershell
python -m unittest builder.tests.test_mod_versions builder.tests.test_managed_usb builder.tests.test_firmware
```

`tools/verify_mod_versions.py` exercises the packaged backend with local official firmware and boot support. It creates encrypted test images in a new workspace, checks every selected runtime/receiver pair and the pin policy, then verifies settings adaptation and loader restoration. Generated firmware images are private local test artifacts and must not be published.

`tools/package_mod_version.py` adds a verified resource snapshot to an offline seed catalog. Its preferred version is used only when this computer has no saved choice.
