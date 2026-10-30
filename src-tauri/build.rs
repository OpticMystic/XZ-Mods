fn main() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    for path in ["ui", "manual", "localization", "tools/xz_i18n.py"] {
        println!("cargo:rerun-if-changed={}", root.join(path).display());
    }
    let python = if cfg!(windows) { "python" } else { "python3" };
    let status = std::process::Command::new(python)
        .arg("tools/xz_check_quality.py")
        .current_dir(root)
        .status()
        .expect("Python is required to verify XZ localization before building");
    assert!(status.success(), "XZ localization is stale or invalid; review catalogs and run tools/xz_i18n.py build");
    tauri_build::build()
}
