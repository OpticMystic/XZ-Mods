#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
#[path="../../spec/generated/support_contract.rs"]
#[allow(dead_code)]
mod support_contract;
mod support;
mod support_store;
mod support_tracking;
mod controller_identity;
mod controller_queue;
mod controller_notify;
mod app_updates;
mod eject;
mod host;
mod live_stream;
mod screen_protocol;
mod screen_transport;
mod stream_privacy;
use serde_json::{json, Value};
use std::{collections::HashMap, io::{BufRead, BufReader, Write}, path::PathBuf, process::{Command, Stdio}, sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc, Mutex}, time::{Duration, Instant}};
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
            "audio"=>dialog.add_filter("Compatible audio",&["wav","flac","mp3"]).blocking_pick_file(),
            "image"=>dialog.add_filter("PNG or JPEG",&["png","jpg","jpeg"]).blocking_pick_file(),
            "file"=>dialog.blocking_pick_file(),
            _=>return Err("Unknown file picker".to_string()),
        };
        Ok(selected.map(|path|path.to_string()))
    }).await.map_err(|e|e.to_string())?
}

#[tauri::command]
async fn pick_stem_files(app:tauri::AppHandle)->Result<Vec<String>,String>{
    tauri::async_runtime::spawn_blocking(move||Ok(app.dialog().file().add_filter("Audio stems or a ZIP",&["wav","flac","aif","aiff","ogg","mp3","zip"]).blocking_pick_files().unwrap_or_default().into_iter().map(|p|p.to_string()).collect())).await.map_err(|e|e.to_string())?
}

fn resource_root(app: &tauri::AppHandle) -> Result<PathBuf,String> {
    if let Some(path)=std::env::var_os("XZ_BUILDER_RESOURCES") { return Ok(PathBuf::from(path)); }
    app.path().resource_dir().map(|p|p.join("resources")).map_err(|e|e.to_string())
}

// Report collection reads removable media. Python checks its budget between reads, but a blocked filesystem
// call cannot see that, so the host enforces the deadline: after the deadline or a cancel, it waits a short
// grace for the partial result, then ends the child. A stalled drive never holds the single job slot.
const SUPPORT_DEADLINE:Duration=Duration::from_secs(20);
const SUPPORT_GRACE:Duration=Duration::from_millis(2500);
struct Finished(Arc<AtomicBool>);
impl Drop for Finished{fn drop(&mut self){self.0.store(true,Ordering::Release);}}
fn watchdog(child:Arc<Mutex<std::process::Child>>,finished:Arc<AtomicBool>,stalled:Arc<AtomicBool>,cancel:PathBuf,limit:Duration){
    std::thread::spawn(move||{
        let started=Instant::now();let mut stopping:Option<Instant>=None;
        while !finished.load(Ordering::Acquire){
            if stopping.is_none(){
                if started.elapsed()>=limit{let _=std::fs::write(&cancel,b"cancel");stopping=Some(Instant::now());}
                else if cancel.exists(){stopping=Some(Instant::now());}
            }
            if stopping.is_some_and(|t|t.elapsed()>=SUPPORT_GRACE){
                stalled.store(true,Ordering::Release);
                if let Ok(mut child)=child.lock(){let _=child.kill();}
                break;
            }
            std::thread::sleep(Duration::from_millis(100));
        }
    });
}

#[tauri::command]
fn start_job(app: tauri::AppHandle, mut request: Value) -> Result<u64,String> {
    if !request.is_object(){return Err("Expected a builder operation".into());}
    if request["method"].as_str().is_some_and(|m|m.starts_with("controller_")) || request.as_object().is_some_and(|o|o.keys().any(|k|k.starts_with('_'))) {return Err("Internal builder operation refused".into());}
    let root=resource_root(&app)?;
    let state=app.state::<Jobs>();
    let mut entries=state.entries.lock().map_err(|_|"Job state unavailable")?;
    if state.updating.load(Ordering::Acquire){return Err("An app update is running".into());}
    if entries.values().any(|(value,_)|value["running"]==true){return Err("Wait for the current operation or cancel it".into());}
    let id=state.counter.fetch_add(1,Ordering::Relaxed)+1;
    let folder=app.path().app_local_data_dir().map_err(|e|e.to_string())?.join("jobs");
    std::fs::create_dir_all(&folder).map_err(|e|e.to_string())?;
    let cancel=folder.join(format!("{}-{id}.cancel",std::process::id()));
    let deadline=matches!(request["method"].as_str(),Some("support_sources"|"support_collect")).then_some(SUPPORT_DEADLINE);
    entries.insert(id,(json!({"running":true,"stage":"starting","message":"Starting operation"}),cancel.clone()));
    drop(entries);
    std::thread::spawn(move || {
        let result=(|| -> Result<(),String> {
            if matches!(request["method"].as_str(),Some("prepare_usb"|"build_usb"|"update_usb")) {controller_queue::prepare_loader(&app,&mut request)?;}
            let backend=root.join("backend").join(host::executable("xz-mods-service"));
            let mut command=Command::new(backend);
            command.env("XZ_BUILDER_RESOURCES",&root).env("XZ_BUILDER_CANCEL_FILE",&cancel)
                .env("XZ_BUILDER_DATA",app.path().app_local_data_dir().map_err(|e|e.to_string())?)
                .env("XZ_AUDIO_HELPER",root.join(host::executable("xz-audio-helper")))
                .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
            #[cfg(windows)] {use std::os::windows::process::CommandExt;command.creation_flags(0x08000000);}
            let mut child=command.spawn().map_err(|e|format!("Builder backend is unavailable: {e}"))?;
            child.stdin.take().ok_or("Backend input unavailable")?.write_all(&serde_json::to_vec(&request).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            let stdout=child.stdout.take().ok_or("Backend output unavailable")?;
            let child=Arc::new(Mutex::new(child));
            let (finished,stalled)=(Arc::new(AtomicBool::new(false)),Arc::new(AtomicBool::new(false)));
            let _finished=Finished(finished.clone());
            if let Some(limit)=deadline{watchdog(child.clone(),finished,stalled.clone(),cancel.clone(),limit);}
            let mut final_event:Option<Value>=None;
            for line in BufReader::new(stdout).lines(){
                let line=line.map_err(|e|e.to_string())?;
                if line.len()>65536{continue;}
                if let Ok(mut event)=serde_json::from_str::<Value>(&line){
                    let done=event["event"]=="result";event["running"]=json!(true);
                    app.state::<Jobs>().entries.lock().map_err(|_|"Job state unavailable")?.insert(id,(event.clone(),cancel.clone()));
                    if done{final_event=Some(event.clone());}
                }
            }
            let exit=child.lock().map_err(|_|"Backend state unavailable")?.wait().map_err(|e|e.to_string())?;
            let Some(mut event)=final_event else{
                return Err(if stalled.load(Ordering::Acquire){"Collection stopped: a drive did not answer in time".into()}else{"Builder backend ended without a result".into()});
            };
            if !exit.success(){return Err("Builder backend exited unexpectedly after its result".into());}
            controller_queue::finish_loader(&app,&request,&event)?;
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
const LINKS:[&str;8]=["https://cash.app/$FireAtWill42",app_updates::RELEASE_PAGE,app_updates::PORTABLE_URL,app_updates::BETA_PAGE,app_updates::BETA_PORTABLE_URL,"https://www.vj.tools","https://overcue.gg","https://github.com/OverCue-gg/overcue-stems-format"];
#[tauri::command]
fn open_link(url:String)->Result<(),String>{
    let url=*LINKS.iter().find(|link|**link==url).ok_or("This link is not available in XZ Mods")?;
    #[cfg(windows)] {
        use std::os::windows::process::CommandExt;
        Command::new("rundll32.exe").args(["url.dll,FileProtocolHandler",url])
            .creation_flags(0x08000000).spawn().map_err(|e|e.to_string())?;
        Ok(())
    }
    #[cfg(target_os="macos")] {
        Command::new("/usr/bin/open").arg(url).spawn().map_err(|e|e.to_string())?;
        Ok(())
    }
    #[cfg(not(any(windows,target_os="macos")))] {Err(format!("Visit {url} in your browser"))}
}
#[tauri::command]
fn open_licenses(app:tauri::AppHandle)->Result<(),String>{
    let folder=resource_root(&app)?.join("licenses");
    if !folder.is_dir(){return Err("License notices are missing from this installation".into());}
    #[cfg(windows)] let opener="explorer.exe";
    #[cfg(target_os="macos")] let opener="/usr/bin/open";
    #[cfg(not(any(windows,target_os="macos")))] let opener="xdg-open";
    Command::new(opener).arg(folder).spawn().map_err(|e|e.to_string())?;Ok(())
}
#[tauri::command]
fn list_volumes()->Vec<String>{
    // The drive-letter bitmask never touches media, so an empty card reader polled every few seconds cannot raise a "no disk" prompt.
    #[cfg(windows)] let mut volumes:Vec<String>={
        #[link(name="kernel32")] extern "system" {fn GetLogicalDrives()->u32;}
        let mask=unsafe{GetLogicalDrives()};
        (2u8..26).filter(|bit|mask&(1<<bit)!=0).map(|bit|format!("{}:\\",(b'A'+bit) as char)).collect()
    };
    #[cfg(target_os="macos")] let mut volumes:Vec<String>=std::fs::read_dir("/Volumes").map(|entries|entries.flatten()
        .filter(|entry|std::fs::symlink_metadata(entry.path()).map(|meta|meta.is_dir()).unwrap_or(false))
        .map(|entry|entry.path().to_string_lossy().into_owned()).collect()).unwrap_or_default();
    #[cfg(not(any(windows,target_os="macos")))] let mut volumes:Vec<String>=Vec::new();
    volumes.sort();
    volumes
}
fn main(){
    tauri::Builder::default().plugin(tauri_plugin_dialog::init()).plugin(tauri_plugin_updater::Builder::new().build()).manage(support::Support::default()).manage(controller_queue::Queue::default()).manage(Jobs::default()).manage(app_updates::Updates::default()).manage(live_stream::LiveStream::default())
        .invoke_handler(tauri::generate_handler![controller_queue::support_pending_details,controller_queue::support_pending_reports,controller_queue::support_poll_pending,controller_queue::support_loader_identities,controller_queue::support_complete_pending,controller_queue::support_dismiss_pending,controller_queue::support_revoke_loader,controller_notify::support_notify_pending,support::support_send,support::support_export,support::support_cancel,support::support_connect,support::support_owner_status,support::support_inbox,support::support_inbox_page,support_store::support_reports,support_store::support_report_seen,support_store::support_forget,support_tracking::support_check_statuses,support_tracking::support_tracking_link,support_tracking::support_open_tracking,support_tracking::support_revoke_links,start_job,job_status,cancel_job,pick_path,pick_stem_files,open_link,open_licenses,list_volumes,eject::eject_volume,app_updates::app_information,app_updates::acknowledge_release_notes,app_updates::app_update_status,app_updates::check_app_update,app_updates::install_app_update,live_stream::live_stream_start,live_stream::live_stream_status,live_stream::live_stream_stop,live_stream::live_stream_detect,live_stream::live_stream_privacy])
        .on_window_event(|window,event|{
            if matches!(event,tauri::WindowEvent::CloseRequested{..}) {
                window.state::<controller_queue::Queue>().stopping.store(true,Ordering::Release);
                let _=live_stream::stop(&window.state::<live_stream::LiveStream>());
                if let Ok(entries)=window.state::<Jobs>().entries.lock(){
                    for(value,path) in entries.values(){if value["running"]==true{let _=std::fs::write(path,b"cancel");}}
                }
            }
        })
        .setup(|app|{
            controller_queue::start(app.handle().clone());
            let config=app.config().app.windows[0].clone();
            tauri::WebviewWindowBuilder::from_config(app,&config)?
                .visible(std::env::var_os("XZ_BUILDER_HIDDEN").is_none()).build()?;
            Ok(())
        }).run(tauri::generate_context!()).expect("XZ Mods failed to start");
}
