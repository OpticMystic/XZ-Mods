use crate::{controller_identity as identity, support, support_store as store};
use reqwest::Method;
use serde_json::{json, Value};
use std::{io::{Read, Write}, path::{Path,PathBuf}, process::{Command,Stdio}, sync::{Arc,Mutex,atomic::{AtomicBool,Ordering}}, time::Duration};
use tauri::{Emitter,Manager};

#[derive(Default)]
pub struct Queue { running:AtomicBool, pub(crate) stopping:AtomicBool }
struct Running<'a>(&'a AtomicBool);
impl Drop for Running<'_> {fn drop(&mut self){self.0.store(false,Ordering::Release);}}

// Internal operations bypass renderer job state: loader keys never enter JS or logs.
fn backend_at(resources:&Path,data:&Path,request:&Value)->Result<Value,String> {
    let mut command=Command::new(resources.join("backend").join(crate::host::executable("xz-mods-service")));
    command.env("XZ_BUILDER_RESOURCES",resources).env("XZ_BUILDER_DATA",data).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
    #[cfg(windows)] {use std::os::windows::process::CommandExt;command.creation_flags(0x08000000);}
    let mut child=command.spawn().map_err(|_|"Controller file reader unavailable")?;
    child.stdin.take().ok_or("Controller reader input unavailable")?.write_all(&serde_json::to_vec(request).map_err(|_|"Invalid controller request")?).map_err(|_|"Controller reader input unavailable")?;
    let stdout=child.stdout.take().ok_or("Controller reader output unavailable")?;
    let child=Arc::new(Mutex::new(child));let finished=Arc::new(AtomicBool::new(false));
    let timed_out=Arc::new(AtomicBool::new(false));let guard=crate::Finished(finished.clone());
    let watch_child=child.clone();let watch_timeout=timed_out.clone();
    std::thread::spawn(move||{
        for _ in 0..200 {if finished.load(Ordering::Acquire){return}std::thread::sleep(Duration::from_millis(100));}
        watch_timeout.store(true,Ordering::Release);if let Ok(mut child)=watch_child.lock(){let _=child.kill();}
    });
    let cap=crate::support_contract::REPORT_BYTES*6+65536;
    let mut raw=Vec::new();let read=stdout.take((cap+1) as u64).read_to_end(&mut raw);
    if raw.len()>cap {if let Ok(mut child)=child.lock(){let _=child.kill();}}
    let exit=child.lock().map_err(|_|"Controller reader unavailable")?.wait().map_err(|_|"Controller reader unavailable")?;
    drop(guard);
    if read.is_err() || !exit.success() || timed_out.load(Ordering::Acquire) || raw.len()>cap {return Err("Controller file scan did not finish within its limits".into());}
    let text=std::str::from_utf8(&raw).map_err(|_|"Invalid controller reader response")?;
    let event=text.lines().filter_map(|s|serde_json::from_str::<Value>(s).ok()).find(|v|v["event"]=="result").ok_or("Missing controller reader result")?;
    if event["ok"]!=true {return Err("Controller file changed, is unavailable, or failed validation".into());}
    Ok(event["result"].clone())
}
pub(crate) fn backend(app:&tauri::AppHandle,request:&Value)->Result<Value,String> {
    backend_at(&crate::resource_root(app)?,&app.path().app_local_data_dir().map_err(|_|"App data unavailable")?,request)
}
pub(crate) fn prepare_loader(app:&tauri::AppHandle,request:&mut Value)->Result<(),String> {
    let volume=request["volume"].as_str().ok_or("Choose a loader destination")?;
    let observed=backend(app,&json!({"method":"controller_identity","volume":volume}))?;
    let previous=if observed["loader_identity"].is_null(){None}else{Some(serde_json::from_value::<identity::LoaderIdentity>(observed["loader_identity"].clone()).map_err(|_|"Invalid loader identity")?)};
    let root=support::root(app)?;let _lock=identity::lock(&root)?;let mut install=identity::load(&root)?;
    let candidate=install.stage(previous.as_ref())?;
    identity::save(&root,&install)?;
    request["_loader_identity"]=serde_json::to_value(candidate).map_err(|_|"Invalid loader identity")?;
    request["_previous_loader_identity"]=observed["loader_identity"].clone();
    request["_loader_volume_identity"]=observed["identity"].clone();
    Ok(())
}
pub(crate) fn finish_loader(app:&tauri::AppHandle,request:&Value,event:&Value)->Result<(),String> {
    if event["ok"]!=true || request["_loader_identity"].is_null(){return Ok(());}
    let candidate:identity::LoaderIdentity=serde_json::from_value(request["_loader_identity"].clone()).map_err(|_|"Invalid loader identity")?;
    if event["result"]["loader_identity"]["loader_id"]!=candidate.loader_id{return Err("Loader identity write was not confirmed".into());}
    let root=support::root(app)?;let _lock=identity::lock(&root)?;let mut install=identity::load(&root)?;
    if !install.reconcile(&candidate){return Err("Loader identity write could not be recorded".into());}
    identity::save(&root,&install)?;drop(_lock);wake(app.clone());Ok(())
}

async fn post(origin:&str,path:&str,key:&str,body:&Value,cancel:&AtomicBool)->Result<Value,String> {
    support::exchange(origin,Method::POST,path,key,Some(serde_json::to_vec(body).map_err(|_|"Invalid controller request")?),&[],65535,cancel).await
}
pub(crate) async fn register(root:&Path,origin:&str,install:&mut identity::Installation,cancel:&AtomicBool)->Result<(),String> {
    if !install.registered {
        let reply=post(origin,"/v2/installations",&install.installation_key,&json!({"installation_id":install.installation_id}),cancel).await?;
        if reply["installation_id"]!=install.installation_id || reply["registered"]!=true{return Err("Installation registration not confirmed".into());}
        install.registered=true;identity::save(root,install)?;
    }
    for index in 0..install.loaders.len() {
        let loader=&install.loaders[index];
        // A new stick's key is registered only after its identity write/readback.
        if loader.staged.as_ref().is_some_and(|s|s.loader_key==loader.identity.loader_key){continue;}
        let hash=support::hash(loader.identity.loader_key.as_bytes());
        if loader.revoke {
            if !loader.revoked {
                let path=format!("/v2/installations/{}/loaders/{}/revoke",install.installation_id,loader.identity.loader_id);
                let current=post(origin,&path,&install.installation_key,&json!({"expected_key_hash":hash}),cancel).await;
                let reply=match current {
                    Err(error) if error.contains("rotation_conflict") => {
                        if let Some(registered)=&loader.registered_hash {
                            post(origin,&path,&install.installation_key,&json!({"expected_key_hash":registered}),cancel).await?
                        } else {
                            // No registration was confirmed, and the current key is absent.
                            json!({"loader_id":loader.identity.loader_id,"revoked":true})
                        }
                    },
                    other=>other?,
                };
                if reply["loader_id"]!=loader.identity.loader_id || reply["revoked"]!=true{return Err("Loader revocation not confirmed".into());}
                install.loaders[index].revoked=true;identity::save(root,install)?;
            }
            continue;
        }
        if loader.registered_hash.as_deref()!=Some(&hash) {
            let reply=post(origin,&format!("/v2/installations/{}/loaders",install.installation_id),&install.installation_key,
                &json!({"loader_id":loader.identity.loader_id,"loader_key_hash":hash,"previous_key_hash":loader.registered_hash}),cancel).await?;
            if reply["loader_id"]!=loader.identity.loader_id || reply["registered"]!=true{return Err("Loader registration not confirmed".into());}
            install.loaders[index].registered_hash=Some(hash);install.loaders[index].revoked=false;identity::save(root,install)?;
        }
    }
    Ok(())
}
fn pending_path(root:&Path,id:&str)->Result<PathBuf,String> {
    if !support::valid_id(id){return Err("Invalid pending report ID".into());}
    let dir=root.join("controller-queue");std::fs::create_dir_all(&dir).map_err(|_|"Controller queue unavailable")?;support::unlinked(&dir)?;
    Ok(dir.join(format!("{id}.dpapi")))
}
fn load_pending(root:&Path,id:&str)->Result<Value,String> {
    let path=pending_path(root,id)?;let raw=support::protect(&support::read(&path,65536)?,true)?;
    let value:Value=serde_json::from_slice(&raw).map_err(|_|"Invalid private queue record")?;
    if value["pending"]["report_id"]!=id {return Err("Invalid private queue record".into());}Ok(value)
}
fn save_pending(root:&Path,id:&str,value:&Value)->Result<(),String>{identity::seal(&pending_path(root,id)?,value)}
fn ids(root:&Path)->Vec<String> {
    std::fs::read_dir(root.join("controller-queue")).map(|entries|entries.flatten().filter_map(|e|e.file_name().to_str().and_then(|s|s.strip_suffix(".dpapi")).filter(|s|support::valid_id(s)).map(str::to_string)).take(200).collect()).unwrap_or_default()
}
fn pending_projection(value:&Value,installation:&str)->Result<Value,String> {
    let id=value["report_id"].as_str().filter(|s|support::valid_id(s)).ok_or("Invalid controller report")?;
    let reference=value["report_reference"].as_str().ok_or("Missing controller reference")?;
    if crate::support_contract::parse_report_reference(reference).and_then(crate::support_contract::format_report_reference).as_deref()!=Some(reference)
        || value["installation_id"]!=installation || !value["loader_id"].is_null() && !value["loader_id"].as_str().is_some_and(identity::canonical_loader)
        || value["origin"]!="controller" || !support::contract()["pending_lifecycles"].as_array().is_some_and(|a|a.contains(&value["comments_state"])) {
        return Err("Invalid controller queue identity or lifecycle".into());
    }
    let description=if value["description"].is_null(){Value::Null}else{json!(value["description"].as_str().filter(|s|s.chars().count()<=280&&s.len()<=1120).ok_or("Invalid controller description")?)};
    let screen=&value["screen_state"];
    if !screen.is_null() {
        let fields=support::contract()["screen_state_schema"]["properties"].as_object().ok_or("Invalid screen contract")?;
        let object=screen.as_object().ok_or("Invalid screen state")?;
        if object.len()!=fields.len() || fields.iter().any(|(k,s)|!s["enum"].as_array().is_some_and(|v|v.contains(&screen[k]))) {return Err("Invalid screen state".into());}
    }
    let topic=&value["topic"];
    if !topic.is_null() && !support::contract()["reporter_hints"]["topic"].as_array().is_some_and(|a|a.contains(topic)){return Err("Invalid controller topic".into());}
    let created=value["created_at"].as_u64().ok_or("Invalid controller time")?;
    let until=value["reminders_until"].as_u64().ok_or("Invalid controller time")?;
    Ok(json!({"report_id":id,"report_reference":reference,"installation_id":installation,"loader_id":value["loader_id"],"origin":"controller",
        "comments_state":value["comments_state"],"comments_pending":value["comments_pending"]==true,"remind":value["remind"]==true&&until>store::now()*1000,
        "created_at":created,"reminders_until":until,"description":description,"topic":topic,"screen_state":screen,"details_expired":value["details_expired"]==true}))
}
pub(crate) fn list(root:&Path)->Value {
    let mut pending:Vec<Value>=ids(root).iter().filter_map(|id|load_pending(root,id).ok()).filter(|v|v["pending"]["comments_pending"]==true).map(|v|{
        let mut p=v["pending"].clone();if v["dismissed"]==true || p["reminders_until"].as_u64().unwrap_or(0)<=store::now()*1000 {p["remind"]=json!(false);}
        p["completion_pending"]=json!(!v["completion"].is_null());p
    }).collect();
    pending.sort_by(|a,b|b["created_at"].as_u64().cmp(&a["created_at"].as_u64()));
    json!({"pending":pending})
}
pub(crate) async fn claim(root:&Path,origin:&str,install:&identity::Installation,pending:&Value,cancel:&AtomicBool)->Result<(),String> {
    let projection=pending_projection(pending,&install.installation_id)?;
    let id=projection["report_id"].as_str().ok_or("Missing controller report")?;
    let reference=projection["report_reference"].as_str().ok_or("Missing controller reference")?;
    let reply=post(origin,&format!("/v2/installations/{}/pending/{reference}/claim",install.installation_id),&install.installation_key,&json!({}),cancel).await?;
    let projected=pending_projection(&reply["pending"],&install.installation_id)?;
    if projected["report_id"]!=id || projected["report_reference"]!=reference {return Err("Controller claim identity changed".into());}
    let key=reply["receipt_key"].as_str().filter(|s|support::valid_hex64(s)).ok_or("Missing controller receipt key")?;
    let sha=reply["receipt"]["payload_sha256"].as_str().filter(|s|support::valid_hex64(s)).ok_or("Invalid controller digest")?;
    let bytes=reply["bytes"].as_u64().filter(|n|*n<=crate::support_contract::REPORT_BYTES as u64).ok_or("Invalid controller size")?;
    let receipt=support::validate_receipt(&reply["receipt"],id,sha,false)?;
    if reply["receipt"]["report_reference"]!=reference{return Err("Controller claim reference changed".into());}
    let report=json!({"schema":"xz-support-report/2","description":projected["description"],"reporter_hints":{"topic":projected["topic"]}});
    let record=store::Receipt::from_report(id,key,sha,bytes,&report);let dir=store::dir_for(root)?;
    store::begin_send(&dir,&record)?;store::accept(&dir,id,&receipt,store::now())?;
    let mut local=if pending_path(root,id)?.exists(){load_pending(root,id)?}else{json!({})};
    local["pending"]=projected;save_pending(root,id,&local)
}
async fn flush(root:&Path,origin:&str,cancel:&AtomicBool)->Result<(),String> {
    let dir=store::dir_for(root)?;
    for id in ids(root) {
        let mut local=load_pending(root,&id)?;
        if local["completion"].is_null() && local["dismiss_pending"]!=true {continue;}
        let (receipt,_)=store::load(&dir,&id)?;
        let reference=local["pending"]["report_reference"].as_str().ok_or("Missing controller reference")?;
        if !local["completion"].is_null() {
            let reply=post(origin,&format!("/v2/reports/{reference}/complete"),&receipt.key,&local["completion"],cancel).await?;
            let accepted=support::validate_receipt(&reply["receipt"],&id,&receipt.sha256,false)?;
            if reply["report_reference"]!=reference || reply["comments_pending"]!=false || !matches!(reply["comments_state"].as_str(),Some("completed"|"completed_without_comments")){return Err("Completion was not confirmed".into());}
            store::accept(&dir,&id,&accepted,store::now())?;
            local["pending"]["comments_pending"]=json!(false);local["pending"]["remind"]=json!(false);local["pending"]["comments_state"]=reply["comments_state"].clone();
            local["completion"]=Value::Null;local["dismiss_pending"]=json!(false);
        } else {
            let reply=post(origin,&format!("/v2/reports/{reference}/dismiss-reminders"),&receipt.key,&json!({}),cancel).await?;
            if reply["report_reference"]!=reference || reply["dismissed"]!=true{return Err("Reminder dismissal was not confirmed".into());}
            local["dismiss_pending"]=json!(false);
        }
        save_pending(root,&id,&local)?;
    }
    Ok(())
}
async fn poll(root:&Path,origin:&str,install:&identity::Installation,cancel:&AtomicBool)->Result<(),String> {
    let mut after=0;
    for _ in 0..10 {
        let reply=support::exchange(origin,Method::GET,&format!("/v2/installations/{}/pending?after={after}",install.installation_id),&install.installation_key,None,&[],65535,cancel).await?;
        let page=reply["pending"].as_array().filter(|a|a.len()<=20).ok_or("Invalid controller queue page")?;
        for pending in page {
            let projected=pending_projection(pending,&install.installation_id)?;
            let id=projected["report_id"].as_str().ok_or("Invalid controller report")?.to_owned();
            if let Ok(mut local)=load_pending(root,&id) {
                if local["pending"]["report_reference"]!=projected["report_reference"] {return Err("Controller reference changed".into());}
                if store::load(&store::dir_for(root)?,&id).is_ok() {
                    local["pending"]=projected;save_pending(root,&id,&local)?;continue;
                }
            }
            claim(root,origin,install,pending,cancel).await?;
        }
        if reply["next_after"].is_null(){return Ok(());}
        let next=reply["next_after"].as_u64().filter(|n|*n>after).ok_or("Invalid controller queue cursor")?;after=next;
    }
    Err("Controller queue needs another check to finish".into())
}
async fn scan(app:&tauri::AppHandle,root:&Path,install:&mut identity::Installation,cancel:&AtomicBool)->Result<(),String> {
    let handle=app.clone();let scanned=tauri::async_runtime::spawn_blocking(move||backend(&handle,&json!({"method":"controller_scan"}))).await.map_err(|_|"Controller scan unavailable")??;
    let loaders=scanned["loaders"].as_array().filter(|l|l.len()<=4).ok_or("Invalid loader scan")?;
    let mut changed=false;
    for found in loaders {
        if let Ok(observed)=serde_json::from_value::<identity::LoaderIdentity>(found["loader_identity"].clone()) {
            changed=install.reconcile(&observed)||changed;
        }
    }
    if changed {identity::save(root,install)?;register(root,support::ORIGIN,install,cancel).await?;}
    let files=scanned["reports"].as_array().filter(|r|r.len()<=32).ok_or("Invalid controller scan")?;
    for file in files {
        if cancel.load(Ordering::Acquire){break;}
        let id=file["report_id"].as_str().filter(|s|support::valid_id(s)).ok_or("Invalid controller report ID")?;
        let digest=file["sha256"].as_str().filter(|s|support::valid_hex64(s)).ok_or("Invalid controller digest")?;
        let handle=app.clone();let mut request=file.clone();request["method"]=json!("controller_read");
        let read=match tauri::async_runtime::spawn_blocking(move||backend(&handle,&request)).await {Ok(Ok(v))=>v,_=>continue};
        let body=read["body"].as_str().filter(|s|s.len()<=crate::support_contract::REPORT_BYTES).ok_or("Invalid controller body")?;
        if support::hash(body.as_bytes())!=digest {continue;}
        let report:Value=serde_json::from_str(body).map_err(|_|"Invalid controller body")?;
        if !report["installation_id"].is_null() && report["installation_id"]!=install.installation_id {continue;}
        let accepted=match support::exchange(support::ORIGIN,Method::POST,&format!("/v2/installations/{}/pending",install.installation_id),&install.installation_key,Some(body.as_bytes().to_vec()),
            &[("X-Report-ID",id.into()),("X-Report-SHA256",digest.into()),("X-Report-Consent",support::consent_text().into())],65535,cancel).await {Ok(v)=>v,Err(_)=>continue};
        if accepted["schema"]!="xz-controller-accepted/1" || accepted["report_id"]!=id || accepted["payload_sha256"]!=digest {continue;}
        let reference=accepted["report_reference"].as_str().ok_or("Missing controller reference")?;
        if crate::support_contract::parse_report_reference(reference).and_then(crate::support_contract::format_report_reference).as_deref()!=Some(reference){continue;}
        let handle=app.clone();let mut request=file.clone();request["method"]=json!("controller_sent");request["acceptance"]=accepted;
        let _=tauri::async_runtime::spawn_blocking(move||backend(&handle,&request)).await;
    }
    Ok(())
}
pub(crate) async fn sync(app:&tauri::AppHandle)->Result<Value,String> {
    let state=app.state::<Queue>();
    if state.running.compare_exchange(false,true,Ordering::AcqRel,Ordering::Acquire).is_err(){return Ok(json!({"checking":true}));}
    let _running=Running(&state.running);let root=support::root(app)?;let _lock=identity::lock(&root)?;let mut install=identity::load(&root)?;
    let cancel=&state.stopping;
    let mut scan_error=None;
    let result=tokio::time::timeout(Duration::from_secs(120),async {
        register(&root,support::ORIGIN,&mut install,cancel).await?;
        flush(&root,support::ORIGIN,cancel).await?;
        // Skip USB IO while the owner's foreground loader/collection job is active.
        let busy=app.state::<crate::Jobs>().entries.lock().map(|e|e.values().any(|(v,_)|v["running"]==true)).unwrap_or(true);
        if !busy {scan_error=scan(app,&root,&mut install,cancel).await.err();}
        poll(&root,support::ORIGIN,&install,cancel).await
    }).await.unwrap_or_else(|_|Err("Controller queue check timed out; saved work will retry".into()));
    let mut view=list(&root);view["connected"]=json!(result.is_ok());view["identity"]=install.view();
    view["usb_scan_error"]=scan_error.map(Value::String).unwrap_or(Value::Null);
    view["error"]=result.err().map(Value::String).unwrap_or(Value::Null);
    let _=app.emit("xz-controller-queue-changed",&view);Ok(view)
}
pub(crate) fn wake(app:tauri::AppHandle) { tauri::async_runtime::spawn(async move{let _=sync(&app).await;}); }
pub(crate) fn start(app:tauri::AppHandle) {
    tauri::async_runtime::spawn(async move {
        loop {
            if app.state::<Queue>().stopping.load(Ordering::Acquire){break;}
            let _=sync(&app).await;
            tokio::time::sleep(Duration::from_secs(180)).await;
        }
    });
}
#[tauri::command] pub fn support_pending_reports(app:tauri::AppHandle)->Result<Value,String> {Ok(list(&support::root(&app)?))}
#[tauri::command] pub async fn support_pending_details(app:tauri::AppHandle,id:String)->Result<Value,String> {
    let root=support::root(&app)?;let local=load_pending(&root,&id)?;
    let (receipt,_)=store::load(&store::dir_for(&root)?,&id)?;
    let reference=local["pending"]["report_reference"].as_str().ok_or("Missing controller reference")?;
    let raw=support::read_report_pages(support::ORIGIN,&id,&receipt.key,&format!("/v2/reports/{reference}/details"),&app.state::<Queue>().stopping).await?;
    if support::hash(&raw)!=receipt.sha256{return Err("Controller details do not match the saved receipt".into());}
    serde_json::from_slice(&raw).map_err(|_|"Invalid controller details".into())
}
#[tauri::command] pub async fn support_poll_pending(app:tauri::AppHandle)->Result<Value,String> {sync(&app).await}
#[tauri::command] pub fn support_loader_identities(app:tauri::AppHandle)->Result<Value,String> {let root=support::root(&app)?;let _lock=identity::lock(&root)?;Ok(identity::load(&root)?.view())}
fn enqueue_completion(root:&Path,id:&str,comments:Option<&str>)->Result<(),String> {
    let mut local=load_pending(root,id)?;
    if local["pending"]["comments_pending"]!=true{return Err("This report is already completed".into());}
    let action=if let Some(text)=comments {
        if text.trim().is_empty() || text.len()>4096{return Err("Describe what happened in at most 4096 UTF-8 bytes".into());}
        json!({"action":"complete","comments":text})
    }else{json!({"action":"complete_without_comments"})};
    if !local["completion"].is_null() && local["completion"]!=action{return Err("A completion is awaiting confirmation. Check it before changing the comments.".into());}
    local["completion"]=action;save_pending(root,id,&local)
}
#[tauri::command] pub async fn support_complete_pending(app:tauri::AppHandle,id:String,comments:Option<String>)->Result<Value,String> {
    {let root=support::root(&app)?;let _lock=identity::lock(&root)?;enqueue_completion(&root,&id,comments.as_deref())?;}
    sync(&app).await
}
#[tauri::command] pub async fn support_dismiss_pending(app:tauri::AppHandle,id:String)->Result<Value,String> {
    {let root=support::root(&app)?;let _lock=identity::lock(&root)?;let mut local=load_pending(&root,&id)?;
    local["dismissed"]=json!(true);local["dismiss_pending"]=json!(true);save_pending(&root,&id,&local)?;}
    sync(&app).await
}
#[tauri::command] pub async fn support_revoke_loader(app:tauri::AppHandle,loader_id:String)->Result<Value,String> {
    {let root=support::root(&app)?;let _lock=identity::lock(&root)?;let mut install=identity::load(&root)?;
    let loader=install.loaders.iter_mut().find(|l|l.identity.loader_id==loader_id).ok_or("Unknown loader")?;loader.revoke=true;identity::save(&root,&install)?;}
    sync(&app).await
}

#[cfg(test)] mod tests {
    use super::*;
    fn pending(id:&str,install:&str)->Value {json!({"report_id":id,"report_reference":"R-4F7K-2M9Q","installation_id":install,"loader_id":"L-4F7K-2M9Q","origin":"controller","comments_state":"pending_comments","comments_pending":true,"remind":true,"created_at":1793059200000u64,"reminders_until":9999999999999u64,"description":"typed on XZ","topic":"NOT_WORKING","screen_state":null,"details_expired":false})}
    #[test] fn queue_projection_rejects_foreign_identity_and_screen_text() {
        let id="a".repeat(32);let mut p=pending(&id,"I-4F7K-2M9Q");assert!(pending_projection(&p,"I-4F7K-2M9Q").is_ok());assert!(pending_projection(&p,"I-0000-0000").is_err());
        p["screen_state"]=json!({"title":"track"});assert!(pending_projection(&p,"I-4F7K-2M9Q").is_err());
    }
    #[cfg(windows)] #[tokio::test] async fn offline_registration_is_persisted_and_retry_transmits_hashes_only() {
        let root=store::tests::temp("controller-register");let _lock=identity::lock(&root).unwrap();let mut install=identity::load(&root).unwrap();let candidate=install.stage(None).unwrap();assert!(install.reconcile(&candidate));identity::save(&root,&install).unwrap();
        assert!(register(&root,"http://127.0.0.1:1",&mut install,&AtomicBool::new(false)).await.is_err());assert!(!identity::load(&root).unwrap().registered);
        let (origin,server)=support::focused_tests::server(vec![support::focused_tests::ok(&json!({"installation_id":install.installation_id,"registered":true})),support::focused_tests::ok(&json!({"loader_id":candidate.loader_id,"registered":true}))]);
        register(&root,&origin,&mut install,&AtomicBool::new(false)).await.unwrap();let requests=server.join().unwrap();assert_eq!(requests.len(),2);
        assert!(!requests.join("").contains(&candidate.loader_key));assert!(requests[1].contains(&support::hash(candidate.loader_key.as_bytes())));
        assert!(identity::load(&root).unwrap().registered);assert!(install.loaders[0].registered_hash.is_some());
    }
    #[cfg(windows)] #[tokio::test] async fn claim_stores_receipt_in_dpapi_and_completion_retry_keeps_exact_comments() {
        let root=store::tests::temp("controller-claim");let install=identity::load(&root).unwrap();let id="a".repeat(32);let p=pending(&id,&install.installation_id);
        let receipt=support::focused_tests::receipt(&id,&"e".repeat(64),1,"received","received");
        let (origin,server)=support::focused_tests::server(vec![support::focused_tests::ok(&json!({"receipt_key":"f".repeat(64),"receipt":receipt,"bytes":100,"pending":p}))]);
        claim(&root,&origin,&install,&p,&AtomicBool::new(false)).await.unwrap();server.join().unwrap();
        let text=list(&root).to_string();assert!(text.contains("typed on XZ"));assert!(!text.contains(&"f".repeat(64)));
        enqueue_completion(&root,&id,Some("My comments")).unwrap();assert!(enqueue_completion(&root,&id,Some("changed")).is_err());
        assert!(flush(&root,"http://127.0.0.1:1",&AtomicBool::new(false)).await.is_err());assert_eq!(load_pending(&root,&id).unwrap()["completion"]["comments"],"My comments");
        let (origin,server)=support::focused_tests::server(vec![support::focused_tests::ok(&json!({"report_reference":"R-4F7K-2M9Q","comments_pending":false,"comments_state":"completed","receipt":receipt}))]);
        flush(&root,&origin,&AtomicBool::new(false)).await.unwrap();let sent=server.join().unwrap();assert!(sent[0].contains("My comments"));assert!(list(&root)["pending"].as_array().unwrap().is_empty());
    }
    #[cfg(windows)] #[tokio::test] async fn revocation_recovers_a_lost_rotation_response() {
        let root=store::tests::temp("controller-revoke");let mut install=identity::load(&root).unwrap();install.registered=true;
        let first=install.stage(None).unwrap();install.reconcile(&first);let previous=support::hash(first.loader_key.as_bytes());install.loaders[0].registered_hash=Some(previous.clone());
        let next=install.stage(Some(&first)).unwrap();install.reconcile(&next);install.loaders[0].revoke=true;
        let conflict=support::focused_tests::ok(&json!({"error":"rotation_conflict"})).replacen("200 OK","409 Conflict",1);
        // The new key was never registered, so revocation falls back to the last confirmed hash.
        let (origin,server)=support::focused_tests::server(vec![conflict,support::focused_tests::ok(&json!({"loader_id":first.loader_id,"revoked":true}))]);
        register(&root,&origin,&mut install,&AtomicBool::new(false)).await.unwrap();let requests=server.join().unwrap();
        assert!(requests[0].contains(&support::hash(next.loader_key.as_bytes())));assert!(requests[1].contains(&previous));assert!(install.loaders[0].revoked);
    }
    #[cfg(windows)] #[test] fn dismissal_expiry_and_without_comments_have_separate_local_state() {
        let root=store::tests::temp("controller-dismiss");let id="a".repeat(32);let mut p=pending(&id,"I-4F7K-2M9Q");
        save_pending(&root,&id,&json!({"pending":p,"dismissed":true})).unwrap();assert_eq!(list(&root)["pending"][0]["remind"],false);
        p["reminders_until"]=json!(0);save_pending(&root,&id,&json!({"pending":p})).unwrap();assert_eq!(list(&root)["pending"][0]["remind"],false);
        enqueue_completion(&root,&id,None).unwrap();assert_eq!(load_pending(&root,&id).unwrap()["completion"],json!({"action":"complete_without_comments"}));
    }
}
