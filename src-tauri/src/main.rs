#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
use serde_json::{json, Value};
use std::{collections::HashMap, io::{BufRead, BufReader, Write}, path::PathBuf, process::{Command, Stdio}, sync::{atomic::{AtomicU64, Ordering}, Mutex}};
use tauri::Manager;
use tauri_plugin_dialog::DialogExt;

#[derive(Default)]
struct Jobs { counter: AtomicU64, entries: Mutex<HashMap<u64, (Value, PathBuf)>> }
impl Drop for Jobs {
    fn drop(&mut self){if let Ok(entries)=self.entries.lock(){for (value,path) in entries.values(){if value["running"]==true{let _=std::fs::write(path,b"cancel");}}}}
}

#[tauri::command]
async fn pick_path(app:tauri::AppHandle,kind:String)->Result<Option<String>,String>{
    tauri::async_runtime::spawn_blocking(move || {
        let dialog=app.dialog().file();
        let selected=match kind.as_str(){
            "folder"=>dialog.blocking_pick_folder(),
            "audio"=>dialog.add_filter("Compatible audio",&["wav","flac"]).blocking_pick_file(),
            "file"=>dialog.blocking_pick_file(),
            _=>return Err("Unknown file picker".to_string()),
        };
        Ok(selected.map(|path|path.to_string()))
    }).await.map_err(|e|e.to_string())?
}

fn resource_root(app: &tauri::AppHandle) -> Result<PathBuf,String> {
    if let Some(path)=std::env::var_os("XZ_BUILDER_RESOURCES") { return Ok(PathBuf::from(path)); }
    app.path().resource_dir().map(|p|p.join("resources")).map_err(|e|e.to_string())
}

#[tauri::command]
fn start_job(app: tauri::AppHandle, request: Value) -> Result<u64,String> {
    if !request.is_object(){return Err("Expected a builder operation".into());}
    let root=resource_root(&app)?;
    let state=app.state::<Jobs>();
    let mut entries=state.entries.lock().map_err(|_|"Job state unavailable")?;
    if entries.values().any(|(value,_)|value["running"]==true){return Err("Wait for the current operation or cancel it".into());}
    let id=state.counter.fetch_add(1,Ordering::Relaxed)+1;
    let folder=app.path().app_local_data_dir().map_err(|e|e.to_string())?.join("jobs");
    std::fs::create_dir_all(&folder).map_err(|e|e.to_string())?;
    let cancel=folder.join(format!("{}-{id}.cancel",std::process::id()));
    entries.insert(id,(json!({"running":true,"stage":"starting","message":"Starting operation"}),cancel.clone()));
    drop(entries);
    std::thread::spawn(move || {
        let result=(|| -> Result<(),String> {
            let backend=root.join("backend").join("xz-mods-service.exe");
            let mut command=Command::new(backend);
            command.env("XZ_BUILDER_RESOURCES",&root).env("XZ_BUILDER_CANCEL_FILE",&cancel)
                .env("XZ_BUILDER_DATA",app.path().app_local_data_dir().map_err(|e|e.to_string())?)
                .env("XZ_AUDIO_HELPER",root.join("xz-audio-helper.exe"))
                .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
            #[cfg(windows)] {use std::os::windows::process::CommandExt;command.creation_flags(0x08000000);}
            let mut child=command.spawn().map_err(|e|format!("Builder backend is unavailable: {e}"))?;
            child.stdin.take().ok_or("Backend input unavailable")?.write_all(&serde_json::to_vec(&request).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            let mut final_seen=false;
            for line in BufReader::new(child.stdout.take().ok_or("Backend output unavailable")?).lines(){
                let line=line.map_err(|e|e.to_string())?;
                if line.len()>65536{continue;}
                if let Ok(mut event)=serde_json::from_str::<Value>(&line){
                    let done=event["event"]=="result";event["running"]=json!(!done);
                    app.state::<Jobs>().entries.lock().map_err(|_|"Job state unavailable")?.insert(id,(event,cancel.clone()));
                    final_seen|=done;
                }
            }
            child.wait().map_err(|e|e.to_string())?;
            if !final_seen{return Err("Builder backend ended without a result".into());}
            Ok(())
        })();
        if let Err(error)=result {
            if let Ok(mut entries)=app.state::<Jobs>().entries.lock(){entries.insert(id,(json!({"running":false,"ok":false,"error":error}),cancel.clone()));}
        }
        let _=std::fs::remove_file(cancel);
    });
    Ok(id)
}

#[tauri::command]
fn job_status(app: tauri::AppHandle,id:u64)->Result<Value,String>{
    app.state::<Jobs>().entries.lock().map_err(|_|"Job state unavailable")?.get(&id).map(|(v,_)|v.clone()).ok_or("Unknown job".into())
}
#[tauri::command]
fn cancel_job(app:tauri::AppHandle,id:u64)->Result<(),String>{
    let state=app.state::<Jobs>();let entries=state.entries.lock().map_err(|_|"Job state unavailable")?;
    let (value,path)=entries.get(&id).ok_or("Unknown job")?;
    if value["running"]==true{std::fs::write(path,b"cancel").map_err(|e|e.to_string())?;}
    Ok(())
}
#[tauri::command]
fn open_vj_tools()->Result<(),String>{
    #[cfg(windows)] {
        use std::os::windows::process::CommandExt;
        Command::new("rundll32.exe").args(["url.dll,FileProtocolHandler","https://vj.tools"])
            .creation_flags(0x08000000).spawn().map_err(|e|e.to_string())?;
        Ok(())
    }
    #[cfg(not(windows))] {Err("Visit https://vj.tools in your browser".into())}
}
fn main(){
    tauri::Builder::default().plugin(tauri_plugin_dialog::init()).manage(Jobs::default())
        .invoke_handler(tauri::generate_handler![start_job,job_status,cancel_job,pick_path,open_vj_tools])
        .on_window_event(|window,event|{
            if matches!(event,tauri::WindowEvent::CloseRequested{..}) {
                if let Ok(entries)=window.state::<Jobs>().entries.lock(){
                    for(value,path) in entries.values(){if value["running"]==true{let _=std::fs::write(path,b"cancel");}}
                }
            }
        })
        .setup(|app|{
            let config=app.config().app.windows[0].clone();
            tauri::WebviewWindowBuilder::from_config(app,&config)?
                .visible(std::env::var_os("XZ_BUILDER_HIDDEN").is_none()).build()?;
            Ok(())
        }).run(tauri::generate_context!()).expect("XZ Mods failed to start");
}
