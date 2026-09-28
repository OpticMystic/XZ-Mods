#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod app_updates;
use serde_json::{json, Value};
use std::{collections::HashMap, io::{BufRead, BufReader, Write}, path::PathBuf, process::{Command, Stdio}, sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Mutex}};
use tauri::Manager;
use tauri_plugin_dialog::DialogExt;

#[derive(Default)]
struct Jobs { updating:AtomicBool, counter: AtomicU64, entries: Mutex<HashMap<u64, (Value, PathBuf)>> }
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

#[tauri::command]
async fn pick_stem_files(app:tauri::AppHandle)->Result<Vec<String>,String>{
    tauri::async_runtime::spawn_blocking(move||Ok(app.dialog().file().add_filter("Audio stems",&["wav","flac","aif","aiff","ogg","mp3"]).blocking_pick_files().unwrap_or_default().into_iter().map(|p|p.to_string()).collect())).await.map_err(|e|e.to_string())?
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
    if state.updating.load(Ordering::Acquire){return Err("An app update is running".into());}
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
            let mut final_event:Option<Value>=None;
            for line in BufReader::new(child.stdout.take().ok_or("Backend output unavailable")?).lines(){
                let line=line.map_err(|e|e.to_string())?;
                if line.len()>65536{continue;}
                if let Ok(mut event)=serde_json::from_str::<Value>(&line){
                    let done=event["event"]=="result";event["running"]=json!(true);
                    app.state::<Jobs>().entries.lock().map_err(|_|"Job state unavailable")?.insert(id,(event.clone(),cancel.clone()));
                    if done{final_event=Some(event.clone());}
                }
            }
            let exit=child.wait().map_err(|e|e.to_string())?;
            let Some(mut event)=final_event else{return Err("Builder backend ended without a result".into());};
            if !exit.success(){return Err("Builder backend exited unexpectedly after its result".into());}
            event["running"]=json!(false);
            app.state::<Jobs>().entries.lock().map_err(|_|"Job state unavailable")?.insert(id,(event,cancel.clone()));
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
// The webview can only ask for these exact pages, so page content can never make the shell open an arbitrary URL.
const LINKS:[&str;5]=[app_updates::RELEASE_PAGE,app_updates::PORTABLE_URL,"https://vj.tools","https://overcue.gg","https://github.com/OverCue-gg/overcue-stems-format"];
#[tauri::command]
fn open_link(url:String)->Result<(),String>{
    let url=*LINKS.iter().find(|link|**link==url).ok_or("This link is not available in XZ Mods")?;
    #[cfg(windows)] {
        use std::os::windows::process::CommandExt;
        Command::new("rundll32.exe").args(["url.dll,FileProtocolHandler",url])
            .creation_flags(0x08000000).spawn().map_err(|e|e.to_string())?;
        Ok(())
    }
    #[cfg(not(windows))] {Err(format!("Visit {url} in your browser"))}
}
#[tauri::command]
fn open_licenses(app:tauri::AppHandle)->Result<(),String>{
    let folder=resource_root(&app)?.join("licenses");
    if !folder.is_dir(){return Err("License notices are missing from this installation".into());}
    Command::new("explorer.exe").arg(folder).spawn().map_err(|e|e.to_string())?;Ok(())
}
fn main(){
    tauri::Builder::default().plugin(tauri_plugin_dialog::init()).plugin(tauri_plugin_updater::Builder::new().build()).manage(Jobs::default()).manage(app_updates::Updates::default())
        .invoke_handler(tauri::generate_handler![start_job,job_status,cancel_job,pick_path,pick_stem_files,open_link,open_licenses,app_updates::app_information,app_updates::acknowledge_release_notes,app_updates::app_update_status,app_updates::check_app_update,app_updates::install_app_update])
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
