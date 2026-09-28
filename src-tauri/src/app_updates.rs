use serde_json::{json,Value};
use std::{sync::{Mutex,atomic::Ordering},time::{Duration,SystemTime,UNIX_EPOCH}};
use tauri::Manager;
use tauri_plugin_updater::{Update,UpdaterExt};

pub const RELEASE_PAGE:&str="https://github.com/OpticMystic/XZ-Mods/releases/latest";
pub const PORTABLE_URL:&str="https://github.com/OpticMystic/XZ-Mods/releases/latest/download/XZ-Mods-Builder-preview-win64.zip";
const RELEASE_API:&str="https://api.github.com/repos/OpticMystic/XZ-Mods/releases/latest";

pub struct Updates { pub inner:Mutex<UpdateState> }
pub struct UpdateState { pending:Option<Update>, checking:bool, pub status:Value }
impl Default for Updates {
    fn default()->Self{Self{inner:Mutex::new(UpdateState{pending:None,checking:false,status:json!({"phase":"idle","message":"Check for a newer version of XZ Mods."})})}}
}
fn state(app:&tauri::AppHandle,status:Value){if let Ok(mut s)=app.state::<Updates>().inner.lock(){s.status=status;}}
fn now()->u64{SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs()}
fn marker(app:&tauri::AppHandle)->Result<std::path::PathBuf,String>{app.path().app_local_data_dir().map(|p|p.join("app-update-notes.json")).map_err(|e|e.to_string())}
fn write_marker(app:&tauri::AppHandle,version:&str)->Result<(),String>{
    let target=marker(app)?;let parent=target.parent().ok_or("Settings folder unavailable")?;
    std::fs::create_dir_all(parent).map_err(|e|e.to_string())?;
    std::fs::write(target,serde_json::to_vec(&json!({"version":version})).map_err(|e|e.to_string())?).map_err(|e|e.to_string())
}
pub fn bundled_notes()->Value{serde_json::from_str(include_str!("../../release-notes.json")).expect("Validated release notes")}

pub fn parse_notes(text:&str)->Value{
    let mut features=Vec::<String>::new();let mut fixes=Vec::<String>::new();let mut changes=Vec::<String>::new();let mut section=2;
    for raw in text.lines().take(200){
        let line=raw.trim();if line.is_empty(){continue;}
        if line.starts_with('#'){
            let title=line.trim_start_matches('#').trim().to_ascii_lowercase();
            section=if matches!(title.as_str(),"new features"|"features"|"what's new"|"added"){0}else if matches!(title.as_str(),"fixes"|"bug fixes"|"fixed"){1}else{2};continue;
        }
        if line.starts_with("XZ Mods "){continue;}
        let value=line.trim_start_matches("- ").trim_start_matches("* ").replace("**","").replace('`',"");
        let value:String=value.chars().take(2000).collect();
        match section{0=>features.push(value),1=>fixes.push(value),_=>changes.push(value)}
    }
    json!({"features":features,"fixes":fixes,"changes":changes})
}
fn newer(current:&str,latest:&str)->Result<std::cmp::Ordering,String>{
    let current=semver::Version::parse(current.trim_start_matches('v')).map_err(|_|"Installed version is invalid")?;
    let latest=semver::Version::parse(latest.trim_start_matches('v')).map_err(|_|"Published version is invalid")?;
    Ok(latest.cmp(&current))
}
fn approved_download(url:&reqwest::Url)->bool{
    if url.scheme()=="https"&&url.host_str()==Some("github.com")&&url.path().starts_with("/OpticMystic/XZ-Mods/releases/download/")&&url.path().ends_with("-setup.exe"){return true;}
    #[cfg(feature="updater-test")]
    if url.scheme()=="http"&&url.host_str()==Some("127.0.0.1")&&url.path().ends_with("-setup.exe"){return true;}
    false
}
#[cfg(feature="updater-test")]
fn test_url(key:&str)->Option<reqwest::Url>{
    std::env::var(key).ok().and_then(|s|s.parse::<reqwest::Url>().ok()).filter(|u|u.scheme()=="http"&&u.host_str()==Some("127.0.0.1"))
}

#[tauri::command]
pub fn app_information(app:tauri::AppHandle)->Result<Value,String>{
    let version=app.package_info().version.to_string();
    let show_notes=marker(&app).ok().and_then(|p|std::fs::read(p).ok()).and_then(|b|serde_json::from_slice::<Value>(&b).ok()).is_some_and(|v|v["version"]==version);
    let installed=std::env::current_exe().ok().and_then(|p|p.parent().map(|d|d.join("uninstall.exe").is_file())).unwrap_or(false);
    Ok(json!({"version":version,"installed":installed,"show_release_notes":show_notes,"release_notes":bundled_notes(),"test_build":cfg!(feature="updater-test")}))
}
#[tauri::command]
pub fn acknowledge_release_notes(app:tauri::AppHandle)->Result<(),String>{
    let path=marker(&app)?;if path.exists(){std::fs::remove_file(path).map_err(|e|e.to_string())?;}Ok(())
}
#[tauri::command]
pub fn app_update_status(app:tauri::AppHandle)->Result<Value,String>{
    app.state::<Updates>().inner.lock().map(|s|s.status.clone()).map_err(|_|"Update state unavailable".into())
}
async fn check_inner(app:&tauri::AppHandle)->Result<Value,String>{
    let _=rustls::crypto::ring::default_provider().install_default();
    let current=app.package_info().version.to_string();
    let api=RELEASE_API.parse::<reqwest::Url>().map_err(|e|e.to_string())?;
    #[cfg(feature="updater-test")]
    let api=test_url("XZ_UPDATER_TEST_API").unwrap_or(api);
    let client=reqwest::Client::builder().user_agent(format!("XZ-Mods/{current}")).timeout(Duration::from_secs(20)).build().map_err(|e|e.to_string())?;
    let response=client.get(api).header("Accept","application/vnd.github+json").send().await.map_err(|_|"Could not reach GitHub. Check your connection and try again.")?;
    if response.status()==reqwest::StatusCode::NOT_FOUND{return Ok(json!({"phase":"not_ready","current_version":current,"message":"No public app release is available yet.","checked_at":now()}));}
    if !response.status().is_success(){return Err(format!("GitHub could not complete the update check (HTTP {}). Try again later.",response.status().as_u16()));}
    let bytes=response.bytes().await.map_err(|_|"Could not read the release information")?;
    if bytes.len()>2*1024*1024{return Err("Release information was unexpectedly large".into());}
    let release:Value=serde_json::from_slice(&bytes).map_err(|_|"GitHub returned invalid release information")?;
    if release["draft"]==true||release["prerelease"]==true{return Err("The update service returned an unpublished or prerelease build".into());}
    let latest=release["tag_name"].as_str().ok_or("Release version is missing")?.trim_start_matches('v').to_string();
    let comparison=newer(&current,&latest)?;
    if comparison!=std::cmp::Ordering::Greater{
        let phase=if comparison==std::cmp::Ordering::Less{"ahead"}else{"current"};
        let message=if phase=="ahead"{"You are running a newer build than the latest published release."}else{"XZ Mods is up to date."};
        return Ok(json!({"phase":phase,"current_version":current,"latest_version":latest,"message":message,"checked_at":now()}));
    }
    let has_feed=release["assets"].as_array().is_some_and(|a|a.iter().any(|v|v["name"]=="latest.json"));
    if !has_feed{return Ok(json!({"phase":"not_ready","current_version":current,"latest_version":latest,"message":"A newer release is published, but its signed installer is not available through the updater yet.","notes":parse_notes(release["body"].as_str().unwrap_or("")),"checked_at":now()}));}
    let builder=app.updater_builder().timeout(Duration::from_secs(20));
    #[cfg(feature="updater-test")]
    let builder=if let Some(url)=test_url("XZ_UPDATER_TEST_ENDPOINT"){builder.endpoints(vec![url]).map_err(|e|e.to_string())?}else{builder};
    let update=builder.build().map_err(|e|e.to_string())?.check().await.map_err(|_|"The signed update feed could not be read. Try again later.")?;
    let Some(mut update)=update else{return Ok(json!({"phase":"current","current_version":current,"latest_version":latest,"message":"XZ Mods is up to date.","checked_at":now()}));};
    if !approved_download(&update.download_url){return Err("The update points to an unexpected installer location".into());}
    if newer(&current,&update.version)?!=std::cmp::Ordering::Greater{return Err("The update is not newer than this app".into());}
    update.timeout=Some(Duration::from_secs(600));
    let result=json!({"phase":"available","current_version":current,"latest_version":update.version,"message":"A newer version is ready to install.","notes":parse_notes(update.body.as_deref().unwrap_or("")),"checked_at":now()});
    app.state::<Updates>().inner.lock().map_err(|_|"Update state unavailable")?.pending=Some(update);
    Ok(result)
}
#[tauri::command]
pub async fn check_app_update(app:tauri::AppHandle)->Result<Value,String>{
    if app.state::<crate::Jobs>().updating.load(Ordering::Acquire){return Err("An app update is already running".into());}
    {
        let state=app.state::<Updates>();let mut s=state.inner.lock().map_err(|_|"Update state unavailable")?;
        if s.checking{return Err("An update check is already running".into());}
        s.checking=true;s.pending=None;s.status=json!({"phase":"checking","message":"Checking for app updates..."});
    }
    let result=check_inner(&app).await;
    let state=app.state::<Updates>();let mut s=state.inner.lock().map_err(|_|"Update state unavailable")?;s.checking=false;
    s.status=match &result{Ok(value)=>value.clone(),Err(error)=>json!({"phase":"error","message":error,"checked_at":now()})};
    result
}
#[tauri::command]
pub async fn install_app_update(app:tauri::AppHandle)->Result<(),String>{
    let update={
        let jobs=app.state::<crate::Jobs>();let entries=jobs.entries.lock().map_err(|_|"Job state unavailable")?;
        if entries.values().any(|(v,_)|v["running"]==true){return Err("Finish or cancel the current USB or stem operation before updating the app".into());}
        if jobs.updating.swap(true,Ordering::AcqRel){return Err("An app update is already running".into());}
        let pending=app.state::<Updates>().inner.lock().map_err(|_|"Update state unavailable")?.pending.clone();
        let Some(update)=pending else{jobs.updating.store(false,Ordering::Release);return Err("Check for updates before installing".into());};update
    };
    state(&app,json!({"phase":"downloading","message":"Downloading and verifying the installer...","latest_version":update.version,"downloaded":0}));
    let progress_app=app.clone();let mut downloaded=0u64;
    let result=update.download(move |chunk,total|{downloaded+=chunk as u64;state(&progress_app,json!({"phase":"downloading","message":"Downloading and verifying the installer...","downloaded":downloaded,"total":total}));},||{}).await;
    let bytes=match result{
        Ok(bytes)=>bytes,
        Err(_)=>{app.state::<crate::Jobs>().updating.store(false,Ordering::Release);state(&app,json!({"phase":"error","message":"The installer could not be downloaded or verified. Your current app is unchanged. Check for updates and try again."}));return Err("Update download or signature verification failed".into());}
    };
    if let Err(error)=write_marker(&app,&update.version){app.state::<crate::Jobs>().updating.store(false,Ordering::Release);state(&app,json!({"phase":"error","message":error}));return Err(error);}
    state(&app,json!({"phase":"installing","message":"Restarting to install the update...","latest_version":update.version}));
    match update.install(bytes){
        Ok(())=>Ok(()),
        Err(_)=>{app.state::<crate::Jobs>().updating.store(false,Ordering::Release);state(&app,json!({"phase":"error","message":"The Windows installer could not start. Your current app is unchanged."}));Err("Could not start the Windows installer".into())}
    }
}

#[cfg(test)]
mod tests{
    use super::*;
    #[test]fn release_notes_prioritize_features_then_fixes(){let n=parse_notes("# XZ Mods 1.2.3\n## Fixes\n- Keep USB settings.\n## New features\n- App updates.\n");assert_eq!(n["features"][0],"App updates.");assert_eq!(n["fixes"][0],"Keep USB settings.");}
    #[test]fn versions_do_not_downgrade(){assert_eq!(newer("0.1.7","v0.1.5").unwrap(),std::cmp::Ordering::Less);assert_eq!(newer("0.1.7","0.1.8").unwrap(),std::cmp::Ordering::Greater);assert!(newer("0.1.7","garbage").is_err());}
    #[test]fn download_origin_is_restricted(){assert!(approved_download(&"https://github.com/OpticMystic/XZ-Mods/releases/download/v0.1.8/XZ.Mods_0.1.8_x64-setup.exe".parse().unwrap()));assert!(!approved_download(&"https://example.com/x64-setup.exe".parse().unwrap()));assert!(!approved_download(&"https://github.com/Other/Repo/releases/download/v1/x64-setup.exe".parse().unwrap()));}
    #[test]fn bundled_notes_have_current_version_and_clear_groups(){let n=bundled_notes();let item=n["releases"].as_array().unwrap().iter().find(|r|r["version"]==env!("CARGO_PKG_VERSION")).unwrap();assert!(item["features"].as_array().is_some());assert!(item["fixes"].as_array().is_some());}
}
