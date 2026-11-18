use serde_json::{json, Value};
use std::{path::PathBuf, process::{Child, Command, Stdio}, sync::{Arc, Mutex}, time::{Duration, Instant, SystemTime}};
use tauri::Manager;

#[derive(Default)]
pub struct Translation(pub Arc<Mutex<Job>>);
#[derive(Default)]
pub struct Job { child: Option<Child>, started: Option<Instant>, progress_baseline: Option<SystemTime>, result: Value }
fn stop(child: &mut Child) {
    #[cfg(windows)] {
        use std::os::windows::process::CommandExt;
        if let Some(root) = std::env::var_os("SystemRoot") {
            let _ = Command::new(PathBuf::from(root).join("System32/taskkill.exe"))
                .args(["/PID", &child.id().to_string(), "/T", "/F"])
                .creation_flags(0x08000000).stdout(Stdio::null()).stderr(Stdio::null()).status();
        }
    }
    let _ = child.kill(); let _ = child.wait();
}
impl Drop for Job {
    fn drop(&mut self) { if let Some(child) = self.child.as_mut() { stop(child); } }
}

#[tauri::command]
pub fn translation_action(app: tauri::AppHandle, action: String, python: String, model: String) -> Result<Value, String> {
    let folder = app.path().app_local_data_dir().map_err(|e|e.to_string())?.join("translation");
    let root = if cfg!(debug_assertions) { PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..") } else { crate::resource_root(&app)?.join("translation") };
    act(app.state::<Translation>().0.clone(), folder, root, !cfg!(debug_assertions), action, python, model)
}

fn act(state: Arc<Mutex<Job>>, folder: PathBuf, root: PathBuf, packaged: bool, action: String, python: String, model: String) -> Result<Value, String> {
    std::fs::create_dir_all(&folder).map_err(|e|e.to_string())?;
    let config = folder.join("provider.json");
    if action == "bundle" {
        let raw = std::fs::read_to_string(folder.join("bundle.js")).map_err(|e|e.to_string())?;
        let payload = raw.strip_prefix("window.XZ_L10N_DATA=").and_then(|s|s.trim().strip_suffix(';')).ok_or("Invalid generated bundle")?;
        return serde_json::from_str(payload).map_err(|e|e.to_string());
    }
    let mut job = state.lock().map_err(|_|"Translation state unavailable")?;
    if let Some(child) = job.child.as_mut() {
        if let Some(status) = child.try_wait().map_err(|e|e.to_string())? {
            job.result = json!({"running":false,"ok":status.success(),"exit_code":status.code()});
            job.child = None;
        }
    }
    if action == "cancel" {
        if let Some(mut child) = job.child.take() { stop(&mut child); job.result = json!({"running":false,"cancelled":true}); }
    }
    if action == "status" || action == "cancel" {
        let mut result = job.result.clone();
        result["running"] = json!(job.child.is_some());
        if let Ok(file) = std::fs::File::open(folder.join("provider.log")) {
            use std::io::{Read, Seek};
            let mut file = file;
            let length = file.metadata().map_err(|e|e.to_string())?.len();
            file.seek(std::io::SeekFrom::Start(length.saturating_sub(16384))).map_err(|e|e.to_string())?;
            let mut bytes = Vec::new(); file.take(16384).read_to_end(&mut bytes).map_err(|e|e.to_string())?;
            result["output"] = json!(String::from_utf8_lossy(&bytes));
        }
        let progress_path = folder.join("progress/progress.json");
        let modified = std::fs::metadata(&progress_path).ok().and_then(|m|m.modified().ok());
        result["progress"] = if modified != job.progress_baseline {
            std::fs::read(progress_path).ok().and_then(|b|serde_json::from_slice::<Value>(&b).ok()).unwrap_or(Value::Null)
        } else { Value::Null };
        return Ok(result);
    }
    if action == "detect" {
        let mut saved: Value = std::fs::read(&config).ok().and_then(|b|serde_json::from_slice(&b).ok()).filter(Value::is_object).unwrap_or(json!({"python":folder.join("python/Scripts/python.exe"),"model":folder.join("model")}));
        saved["running"] = json!(job.child.is_some());
        let runtime = PathBuf::from(saved["python"].as_str().unwrap_or_default());
        let model = PathBuf::from(saved["model"].as_str().unwrap_or_default());
        saved["runtime_present"] = json!(runtime.is_file());
        saved["model_present"] = json!(["model.bin","shared_vocabulary.json","sentencepiece.bpe.model"].iter().all(|name|model.join(name).is_file()));
        if !runtime.is_file() {
            let mut candidates = Vec::new();
            if let Some(local) = std::env::var_os("LOCALAPPDATA") {
                let installs = PathBuf::from(local).join("Programs/Python");
                candidates.extend(std::fs::read_dir(installs).into_iter().flatten().flatten()
                    .map(|entry|entry.path().join("python.exe")).filter(|path|path.is_file()));
            }
            if let Some(paths) = std::env::var_os("PATH") {
                candidates.extend(std::env::split_paths(&paths).filter(|path|path.is_absolute())
                    .map(|path|path.join("python.exe")).filter(|path|path.is_file()
                        && !path.to_string_lossy().to_lowercase().contains("windowsapps")
                        && !path.parent().and_then(|p|p.parent()).is_some_and(|p|p.join("pyvenv.cfg").is_file())));
            }
            candidates.sort(); candidates.dedup();
            if let Some(base) = candidates.last() { saved["setup_python"] = json!(base); }
        }
        return Ok(saved);
    }
    if job.child.is_some() { return Err("Translation is already running. Cancel it before changing setup.".into()); }
    if !matches!(action.as_str(), "connect" | "install" | "run") { return Err("Unknown translation action".into()); }
    let executable = PathBuf::from(&python);
    if !executable.is_absolute() || !executable.is_file() || executable.file_name().and_then(|s|s.to_str()) != Some("python.exe") { return Err("Choose the python.exe in your independent XZ runtime.".into()); }
    if action != "install" && !executable.parent().and_then(|p|p.parent()).is_some_and(|p|p.join("pyvenv.cfg").is_file()) { return Err("Choose an XZ-only virtual environment, or use Install pinned dependencies to create one.".into()); }
    let model_path = PathBuf::from(&model);
    if !model_path.is_absolute() || ["model.bin","shared_vocabulary.json","sentencepiece.bpe.model"].iter().any(|name|!model_path.join(name).is_file()) { return Err("Choose a Small100 model folder containing model.bin, shared_vocabulary.json and sentencepiece.bpe.model.".into()); }
    let mut command = Command::new(executable);
    command.env_remove("PYTHONPATH").env_remove("PYTHONHOME").env("PYTHONNOUSERSITE","1").env("PYTHONUTF8","1").env("PYTHONUNBUFFERED","1").stdin(Stdio::null());
    if action == "install" { command.arg(root.join("tools/xz_provider_setup.py")).arg(folder.join("python")).arg(&model_path); }
    else if action == "connect" { command.arg(root.join("tools/xz_provider_verify.py")).arg(&model_path); }
    else {
        command.arg(root.join("tools/xz_translate.py")).arg("--model").arg(&model_path).arg("--state").arg(folder.join("progress"));
        if packaged { command.arg("--source").arg(root.join("source.json")); }
        command.arg("--output").arg(folder.join("bundle.js"));
    }
    #[cfg(windows)] { use std::os::windows::process::CommandExt; command.creation_flags(0x08000000); }
    let log = std::fs::File::create(folder.join("provider.log")).map_err(|e|e.to_string())?;
    command.stdout(log.try_clone().map_err(|e|e.to_string())?).stderr(log);
    let saved_python = if action == "install" { folder.join("python/Scripts/python.exe") } else { PathBuf::from(python) };
    let pending_config = config.with_extension("pending.json");
    std::fs::write(&pending_config, serde_json::to_vec(&json!({"python":saved_python,"model":model})).unwrap()).map_err(|e|e.to_string())?;
    std::fs::rename(pending_config, config).map_err(|e|e.to_string())?;
    job.progress_baseline = std::fs::metadata(folder.join("progress/progress.json")).ok().and_then(|m|m.modified().ok());
    let child = command.spawn().map_err(|e|e.to_string())?;
    job.child = Some(child); job.started = Some(Instant::now()); job.result = json!({"running":true,"action":action,"log":folder.join("provider.log")});
    // Independent watchdog remains active even if the dialog closes. Checkpoints survive termination.
    let handle = state.clone();
    let started = job.started;
    std::thread::spawn(move || {
        let limit = if action == "run" { Duration::from_secs(48*3600) } else { Duration::from_secs(1800) };
        loop {
            std::thread::sleep(Duration::from_secs(1));
            let Ok(mut job) = handle.lock() else { break; };
            if job.started != started || job.child.is_none() { break; }
            let child = job.child.as_mut().unwrap();
            if let Ok(Some(status)) = child.try_wait() { job.result = json!({"running":false,"ok":status.success(),"exit_code":status.code()}); job.child = None; break; }
            let oversized = std::fs::metadata(folder.join("provider.log")).is_ok_and(|m|m.len()>16*1024*1024);
            if started.unwrap().elapsed() >= limit || oversized { stop(child); job.child = None; job.result = json!({"running":false,"error":"Translation time or log budget exceeded; checkpoints retained"}); break; }
        }
    });
    Ok(job.result.clone())
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;

    #[test]
    #[ignore = "requires the explicitly selected XZ Python runtime"]
    fn native_job_rejects_overlap_cancels_descendants_and_restarts() {
        let python = std::env::var("XZ_TEST_PYTHON").expect("Set XZ_TEST_PYTHON to the independent XZ venv");
        let root = std::env::temp_dir().join(format!("xz-translation-lifecycle-{}", std::process::id()));
        let model = root.join("model");
        std::fs::create_dir_all(root.join("tools")).unwrap();
        std::fs::create_dir_all(&model).unwrap();
        for name in ["model.bin", "shared_vocabulary.json", "sentencepiece.bpe.model"] {
            std::fs::write(model.join(name), b"fixture").unwrap();
        }
        let script = root.join("tools/xz_provider_verify.py");
        std::fs::write(&script, r#"import subprocess, sys, time
child = "import pathlib,sys,time; p=pathlib.Path(sys.argv[1])/'heartbeat'; n=0\nwhile True:\n n+=1; p.write_text(str(n)); time.sleep(.05)"
subprocess.Popen([sys.executable, '-c', child, sys.argv[1]], creationflags=subprocess.CREATE_NO_WINDOW)
time.sleep(60)
"#).unwrap();
        let state = Arc::new(Mutex::new(Job::default()));
        let call = |action: &str| act(state.clone(), root.join("state"), root.clone(), false,
            action.into(), python.clone(), model.to_string_lossy().into_owned());
        std::fs::create_dir_all(root.join("state/progress")).unwrap();
        std::fs::write(root.join("state/progress/progress.json"), br#"{"done":999,"published":true}"#).unwrap();
        assert!(call("powershell").is_err());
        assert_eq!(call("connect").unwrap()["running"], true);
        assert!(call("connect").unwrap_err().contains("already running"));
        assert_eq!(call("detect").unwrap()["running"], true);
        assert_eq!(call("status").unwrap()["progress"], Value::Null);
        let started = Instant::now();
        while !model.join("heartbeat").exists() && started.elapsed() < Duration::from_secs(15) {
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(model.join("heartbeat").exists(), "fixture descendant did not start");
        assert_eq!(call("cancel").unwrap()["cancelled"], true);
        let stopped = std::fs::read(model.join("heartbeat")).unwrap();
        std::thread::sleep(Duration::from_millis(500));
        assert_eq!(std::fs::read(model.join("heartbeat")).unwrap(), stopped, "descendant survived cancellation");
        std::fs::write(&script, "print('fixture complete')\n").unwrap();
        assert_eq!(call("connect").unwrap()["running"], true);
        let started = Instant::now();
        loop {
            let status = call("status").unwrap();
            if status["running"] == false {
                assert_eq!(status["ok"], true);
                assert!(status["output"].as_str().unwrap().contains("fixture complete"));
                break;
            }
            assert!(started.elapsed() < Duration::from_secs(15), "replacement job did not finish");
            std::thread::sleep(Duration::from_millis(50));
        }
    }

    #[test]
    #[ignore = "requires the independent local XZ runtime and model"]
    fn native_real_provider_connects_all_locales() {
        let python = std::env::var("XZ_TEST_PYTHON").unwrap();
        let model = std::env::var("XZ_TEST_MODEL").unwrap();
        let root = PathBuf::from(std::env::var("XZ_TEST_SOURCE").unwrap());
        let folder = std::env::temp_dir().join(format!("xz-real-provider-{}",std::process::id()));
        let state = Arc::new(Mutex::new(Job::default()));
        let call = |action: &str| act(state.clone(),folder.clone(),root.clone(),false,action.into(),python.clone(),model.clone());
        assert_eq!(call("connect").unwrap()["running"],true);
        let started=Instant::now();
        loop {
            let status=call("status").unwrap();
            if status["running"]==false {
                assert_eq!(status["ok"],true,"{status}");
                let output=status["output"].as_str().unwrap().trim();
                let proof:Value=serde_json::from_str(output.lines().last().unwrap()).unwrap();
                assert_eq!(proof["verified"],true);
                assert_eq!(proof["results"].as_object().unwrap().len(),14);
                assert_eq!(call("detect").unwrap()["python"],python);
                break;
            }
            assert!(started.elapsed()<Duration::from_secs(600),"native provider verification timed out");
            std::thread::sleep(Duration::from_millis(100));
        }
    }

    #[test]
    #[ignore = "creates an isolated XZ runtime with pinned public dependencies"]
    fn native_installs_independent_runtime_and_verifies() {
        let python=std::env::var("XZ_TEST_PYTHON").unwrap();
        let model=std::env::var("XZ_TEST_MODEL").unwrap();
        let root=PathBuf::from(std::env::var("XZ_TEST_SOURCE").unwrap());
        let folder=std::env::temp_dir().join(format!("xz-install-provider-{}",std::process::id()));
        let state=Arc::new(Mutex::new(Job::default()));
        let call=|action:&str|act(state.clone(),folder.clone(),root.clone(),false,action.into(),python.clone(),model.clone());
        assert_eq!(call("detect").unwrap()["runtime_present"],false);
        assert_eq!(call("install").unwrap()["running"],true);
        let started=Instant::now();
        loop {
            let status=call("status").unwrap();
            if status["running"]==false {
                assert_eq!(status["ok"],true,"{status}");
                let detected=call("detect").unwrap();
                assert_eq!(detected["runtime_present"],true);
                assert_eq!(detected["model_present"],true);
                assert_eq!(detected["python"],json!(folder.join("python/Scripts/python.exe")));
                let proof:Value=serde_json::from_str(status["output"].as_str().unwrap().trim().lines().last().unwrap()).unwrap();
                assert_eq!(proof["verified"],true);
                assert_eq!(proof["results"].as_object().unwrap().len(),14);
                break;
            }
            assert!(started.elapsed()<Duration::from_secs(1800),"isolated install timed out");
            std::thread::sleep(Duration::from_millis(100));
        }
    }

    #[test]
    #[ignore = "run only after the full corpus is complete; consumes an isolated cache copy"]
    fn packaged_cached_generation_uses_only_frozen_resources() {
        let python=std::env::var("XZ_TEST_PYTHON").unwrap();
        let model=std::env::var("XZ_TEST_MODEL").unwrap();
        let source=PathBuf::from(std::env::var("XZ_TEST_SOURCE").unwrap());
        let cache=PathBuf::from(std::env::var("XZ_TEST_CACHE").unwrap());
        let root=std::env::temp_dir().join(format!("xz-packaged-provider-{}",std::process::id()));
        let folder=root.join("state");
        std::fs::create_dir_all(root.join("tools")).unwrap();
        std::fs::create_dir_all(folder.join("progress")).unwrap();
        for name in ["xz_translate.py","xz_i18n.py","xz_manual_search.py","xz_translation_quality.py"] {
            std::fs::copy(source.join("tools").join(name),root.join("tools").join(name)).unwrap();
        }
        std::fs::copy(source.join("localization/source.json"),root.join("source.json")).unwrap();
        std::fs::create_dir_all(root.join("localization")).unwrap();
        std::fs::copy(source.join("localization/authored-overrides.json"),root.join("localization/authored-overrides.json")).unwrap();
        for entry in std::fs::read_dir(cache).unwrap().flatten() {
            if entry.path().extension().is_some_and(|e|e=="json") {
                let value=std::fs::read(entry.path()).ok().and_then(|b|serde_json::from_slice::<Value>(&b).ok());
                if value.is_some_and(|v|v["identity"].is_string()&&v["segments"].is_object()) {
                    std::fs::copy(entry.path(),folder.join("progress").join(entry.file_name())).unwrap();
                }
            }
        }
        let state=Arc::new(Mutex::new(Job::default()));
        let call=|action:&str|act(state.clone(),folder.clone(),root.clone(),true,action.into(),python.clone(),model.clone());
        assert_eq!(call("run").unwrap()["running"],true);
        let started=Instant::now();
        loop {
            let status=call("status").unwrap();
            if status["running"]==false {
                assert_eq!(status["ok"],true,"{status}");
                let receipt:Value=serde_json::from_slice(&std::fs::read(folder.join("progress/progress.json")).unwrap()).unwrap();
                assert_eq!(receipt["published"],true);
                assert_eq!(receipt["inference_calls"],0);
                let bundle=call("bundle").unwrap();
                let count=bundle["source"]["entries"].as_object().unwrap().len();
                assert_eq!(bundle["catalogs"].as_object().unwrap().len(),14);
                for catalog in bundle["catalogs"].as_object().unwrap().values() { assert_eq!(catalog["entries"].as_object().unwrap().len(),count); }
                break;
            }
            assert!(started.elapsed()<Duration::from_secs(600),"cached packaged generation timed out");
            std::thread::sleep(Duration::from_millis(100));
        }
    }
}
