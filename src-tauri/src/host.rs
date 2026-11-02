pub fn executable(name: &str) -> String {
    if cfg!(windows) { format!("{name}.exe") } else { name.to_string() }
}

/// Launches the builder backend. Its file checks refuse any path under a symlink, and the macOS temp folder
/// sits under /var, a link to /private/var, so the backend gets the temp folder with links resolved.
pub fn backend_command(resources: &std::path::Path) -> std::process::Command {
    let mut command = std::process::Command::new(resources.join("backend").join(executable("xz-mods-service")));
    #[cfg(not(windows))]
    if let Ok(temp) = std::env::temp_dir().canonicalize() { command.env("TMPDIR", temp); }
    command
}

pub fn update_suffix() -> &'static str {
    if cfg!(target_os = "macos") { ".app.tar.gz" } else { "-setup.exe" }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn helpers_match_the_host() {
        assert_eq!(executable("uv"), if cfg!(windows) { "uv.exe" } else { "uv" });
    }
    #[cfg(unix)]
    #[test]
    fn backend_temp_folder_has_no_linked_parents() {
        let command = backend_command(std::path::Path::new("/resources"));
        let temp = command.get_envs().find(|(k, _)| *k == "TMPDIR").and_then(|(_, v)| v).map(std::path::PathBuf::from).unwrap();
        assert!(temp.ancestors().all(|p| !p.is_symlink()), "{}", temp.display());
    }
}
