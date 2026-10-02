pub fn executable(name: &str) -> String {
    if cfg!(windows) { format!("{name}.exe") } else { name.to_string() }
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
}
