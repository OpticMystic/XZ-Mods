// Report status for My reports. Rust owns every receipt key and HTTP call; the renderer gets the safe projection.
// Checks run at launch, after the app's update check and when My reports opens, at most once per 15 minutes
// per report unless the reporter asks. Sequential, jittered, inside one total time budget, with backoff.
use serde_json::{json, Value};
use std::{path::Path, sync::atomic::AtomicBool, time::{Duration, Instant}};
use crate::support::{self, contract, hash, listed, valid_id, ORIGIN};
use crate::support_store::{self as store, Receipt};

const CHECK_BUDGET:Duration=Duration::from_secs(20);
const ACTIVE_INTERVAL:u64=900;
const CLOSED_INTERVAL:u64=86400;
const MAX_BACKOFF:u64=21600;

fn timestamp(v:&Value)->Result<Value,String>{
 v.as_str().filter(|s|(20..=32).contains(&s.len())&&s.as_bytes()[4]==b'-'&&s.ends_with('Z')&&s.bytes().all(|b|b.is_ascii_digit()||b"-:.TZ".contains(&b))).map(|s|json!(s)).ok_or("Unexpected status time".into())
}
fn optional(v:&Value,list:&str)->Result<Value,String>{
 if v.is_null(){return Ok(Value::Null)}
 v.as_str().filter(|s|listed(list,s)).map(|s|json!(s)).ok_or_else(||format!("Unexpected status {list}"))
}
// The status projection is rebuilt from validated fields. Text comes from the contract's fixed status text,
// so a status response can never put its own words on the reporter's screen.
pub(crate) fn projection(v:&Value,id:&str)->Result<Value,String>{
 let c=contract();
 if v["schema"]!="xz-support-status/2"||v["report_id"]!=id{return Err("Unexpected report status".into())}
 let revision=v["revision"].as_u64().filter(|r|*r>=1).ok_or("Unexpected status revision")?;
 let lifecycle=v["lifecycle"].as_str().filter(|s|listed("lifecycles",s)).ok_or("Unexpected status lifecycle")?;
 let key=v["status_text_key"].as_str().filter(|k|c["status_text"].get(*k).is_some()).ok_or("Unexpected status text")?;
 let question=match v["question"].as_str(){None if v["question"].is_null()=>Value::Null,Some(q) if c["questions"].as_object().is_some_and(|o|o.values().any(|x|x==q))=>json!(q),_=>return Err("Unexpected status question".into())};
 let kind_hint=if v["kind_hint"].is_null(){Value::Null}else{optional(&v["kind_hint"],"kind_hints")?};
 Ok(json!({"schema":"xz-support-status/2","report_id":id,"revision":revision,"classified_kind":optional(&v["classified_kind"],"kinds")?,"kind_hint":kind_hint,
  "outcome":optional(&v["outcome"],"outcomes")?,"lifecycle":lifecycle,"status_text_key":key,"status_text":c["status_text"][key].clone(),"question":question,
  "created_at":timestamp(&v["created_at"])?,"updated_at":timestamp(&v["updated_at"])?,"retention_until":timestamp(&v["retention_until"])?,
  "diagnostics_expires_at":timestamp(&v["diagnostics_expires_at"])?}))
}
// Read capability = SHA256(UTF-8(domain + receipt key)). It goes only in the URL fragment, never a query.
pub(crate) fn read_capability(key:&str)->String{hash(format!("{}{}",contract()["capability_domains"]["read"].as_str().expect("Contract read domain"),key).as_bytes())}
pub(crate) fn tracking_url(origin:&str,id:&str,key:&str)->String{format!("{origin}/track#id={id}&read={}",read_capability(key))}
fn interval(lifecycle:&str)->u64{if matches!(lifecycle,"closed"|"released"){CLOSED_INTERVAL}else{ACTIVE_INTERVAL}}
fn jitter(id:&str,now:u64)->Duration{Duration::from_millis(50+u64::from_str_radix(&hash(format!("{id}{now}").as_bytes())[..4],16).unwrap_or(0)%200)}
// One status read under full receipt authority. 304 keeps the last-known state.
async fn check_one(origin:&str,dir:&Path,r:&Receipt,state:&Value,now:u64,cancel:&AtomicBool)->Result<bool,String>{
 let id=&r.report_id;
 match state["state"].as_str().unwrap_or(""){
  "accepted"=>{
   let revision=state["tracking"]["revision"].as_u64().unwrap_or(0);
   let etag=[("If-None-Match",format!("\"{revision}\""))];
   let reply=support::call(origin,reqwest::Method::GET,&format!("/v2/reports/{id}/status"),&r.key,None,if revision>0{&etag[..]}else{&[]},65536,cancel).await;
   let reply=match reply{Err(e) if e.contains("HTTP 404")=>{store::update(dir,id,|s|{s["state"]=json!("expired");s["next_check_at"]=json!(u64::MAX/2);})?;return Ok(true)},other=>other?};
   let Some(value)=reply.value else{store::update(dir,id,|s|{s["checked_at"]=json!(now);s["next_check_at"]=json!(now+interval(s["tracking"]["lifecycle"].as_str().unwrap_or("")));s["failures"]=json!(0);s["links_ready"]=json!(true);})?;return Ok(false)};
   let next=projection(&value,id)?;let newer=next["revision"].as_u64().unwrap_or(0)>revision;
   if next["revision"].as_u64().unwrap_or(0)<revision{return Err("Report status went back; keeping the last one".into())}
   store::update(dir,id,|s|{if newer{s["tracking"]=next.clone();}s["checked_at"]=json!(now);s["next_check_at"]=json!(now+interval(next["lifecycle"].as_str().unwrap_or("")));s["failures"]=json!(0);s["links_ready"]=json!(true);})?;
   Ok(newer)
  },
  // An uncertain send: ask the service whether it holds these exact bytes. Never upload again from here.
  "unconfirmed"|"sending_interrupted"=>{
   match support::exchange(origin,reqwest::Method::GET,&format!("/v2/reports/{id}/receipt"),&r.key,None,&[],65536,cancel).await{
    Ok(value)=>{let receipt=support::validate_receipt(&value,id,&r.sha256,false)?;store::accept(dir,id,&receipt,now)?;store::update(dir,id,|s|s["links_ready"]=json!(true))?;Ok(true)},
    Err(e) if e.contains("HTTP 404")=>{store::update(dir,id,|s|{s["state"]=json!("not_received");s["next_check_at"]=json!(u64::MAX/2);})?;Ok(true)},
    Err(e)=>Err(e)
   }
  },
  _=>Ok(false)
 }
}
pub(crate) async fn check_all(origin:&str,dir:&Path,force:bool,only:Option<&str>,cancel:&AtomicBool)->Value{
 let started=Instant::now();let (mut checked,mut failed)=(0,0);let mut updated=Vec::new();
 for (receipt,state) in store::list(dir){
  if only.is_some_and(|id|id!=receipt.report_id){continue}
  if started.elapsed()>=CHECK_BUDGET{break}
  let now=store::now();
  if !force&&state["next_check_at"].as_u64().is_some_and(|t|t>now){continue}
  if checked>0{tokio::time::sleep(jitter(&receipt.report_id,now)).await;}
  checked+=1;
  match tokio::time::timeout(CHECK_BUDGET.saturating_sub(started.elapsed()).max(Duration::from_secs(1)),check_one(origin,dir,&receipt,&state,now,cancel)).await{
   Ok(Ok(true))=>updated.push(receipt.report_id.clone()),
   Ok(Ok(false))=>{},
   _=>{failed+=1;let _=store::update(dir,&receipt.report_id,|s|{let failures=s["failures"].as_u64().unwrap_or(0)+1;s["failures"]=json!(failures);s["next_check_at"]=json!(now+(ACTIVE_INTERVAL<<failures.min(5)).min(MAX_BACKOFF));});}
  }
 }
 let reports:Vec<Value>=store::list(dir).iter().map(|(r,s)|store::view(r,s)).collect();
 let unseen:Vec<Value>=reports.iter().filter(|v|v["unseen"]==true).cloned().collect();
 json!({"checked":checked,"failed":failed,"updated":updated,"unseen":unseen})
}
#[tauri::command]
pub async fn support_check_statuses(app:tauri::AppHandle,state:tauri::State<'_,support::Support>,reason:String,id:Option<String>)->Result<Value,String>{
 if !matches!(reason.as_str(),"launch"|"update"|"open"|"refresh"){return Err("Unknown status check".into())}
 if id.as_deref().is_some_and(|i|!valid_id(i)){return Err("Invalid report ID".into())}
 let Some(_guard)=support::begin_checks(&state) else{return Ok(json!({"checked":0,"busy":true,"updated":[],"unseen":[]}))};
 let dir=store::dir(&app)?;Ok(check_all(ORIGIN,&dir,reason=="refresh",id.as_deref(),&AtomicBool::new(false)).await)
}
// A full-auth read creates the derived read capability on the service, so the status link works at once.
pub(crate) async fn initialize_links(origin:&str,dir:&Path,id:&str){
 let Ok((r,_))=store::load(dir,id) else{return};
 if tokio::time::timeout(Duration::from_secs(10),support::exchange(origin,reqwest::Method::GET,&format!("/v2/reports/{id}/receipt"),&r.key,None,&[],65536,&AtomicBool::new(false))).await.is_ok_and(|r|r.is_ok()){
  let _=store::update(dir,id,|s|s["links_ready"]=json!(true));
 }
}
async fn link(app:&tauri::AppHandle,id:&str)->Result<String,String>{
 let dir=store::dir(app)?;let (r,s)=store::load(&dir,id)?;
 if s["state"]!="accepted"{return Err("The status link works once the report is received".into())}
 if s["links_revoked"]==true{return Err("Status links for this report are turned off".into())}
 if s["links_ready"]!=true{initialize_links(ORIGIN,&dir,id).await;}
 Ok(tracking_url(ORIGIN,id,&r.key))
}
#[tauri::command]
pub async fn support_tracking_link(app:tauri::AppHandle,id:String)->Result<Value,String>{Ok(json!({"url":link(&app,&id).await?}))}
#[tauri::command]
pub async fn support_open_tracking(app:tauri::AppHandle,id:String)->Result<Value,String>{support::open(&link(&app,&id).await?)?;Ok(json!({"opened":true}))}
// Turns off every shared read link and browser session for this report. The app's own checks keep working.
#[tauri::command]
pub async fn support_revoke_links(app:tauri::AppHandle,id:String)->Result<Value,String>{
 let dir=store::dir(&app)?;let (r,_)=store::load(&dir,&id)?;
 let reply=support::exchange(ORIGIN,reqwest::Method::POST,&format!("/v2/reports/{id}/capabilities"),&r.key,Some(br#"{"action":"revoke"}"#.to_vec()),&[],4096,&AtomicBool::new(false)).await?;
 if reply["revoked"]!=true{return Err("Status links were not confirmed as turned off".into())}
 store::update(&dir,&id,|s|s["links_revoked"]=json!(true))?;store::view_of(&dir,&id)
}

#[cfg(test)]pub(crate) mod tests{
 use super::*;
 use crate::support::focused_tests::{ok,server,receipt};
 pub(crate) fn status(id:&str,revision:u64,lifecycle:&str,key:&str)->Value{json!({"schema":"xz-support-status/2","report_id":id,"revision":revision,"classified_kind":null,"kind_hint":"BUG","outcome":null,
  "lifecycle":lifecycle,"status_text_key":key,"status_text":contract()["status_text"][key],"question":null,"created_at":"2026-10-27T10:00:00.000Z","updated_at":"2026-10-27T11:00:00.000Z",
  "retention_until":"2027-10-27T10:00:00.000Z","diagnostics_expires_at":"2026-11-10T10:00:00.000Z"})}
 #[test]fn projection_uses_contract_text_and_rejects_unknown_values(){
  let id="a".repeat(32);let mut v=status(&id,2,"needs_info","needs_info");v["status_text"]=json!("<b>Click here</b>");v["question"]=json!("Were decks 1 and 2 stopped?");v["extra"]=json!("C:/Users/secret");
  let p=projection(&v,&id).unwrap();assert_eq!(p["status_text"],"We need one more detail.");assert!(p.get("extra").is_none());assert_eq!(p["question"],"Were decks 1 and 2 stopped?");
  for (field,value) in [("lifecycle",json!("fixed")),("status_text_key",json!("hacked")),("question",json!("Type your password")),("revision",json!(0)),("report_id",json!("b".repeat(32))),("classified_kind",json!("bug")),("updated_at",json!("yesterday"))]{
   let mut bad=status(&id,2,"received","received");bad[field]=value;assert!(projection(&bad,&id).is_err(),"{field}");
  }
 }
 #[test]fn read_link_uses_the_contract_domain_in_a_fragment(){
  let key="0123456789abcdef".repeat(4);let url=tracking_url("https://support.example",&"a".repeat(32),&key);
  assert_eq!(read_capability(&key),hash(format!("xz-support/read/v2\n{key}").as_bytes()));
  assert!(url.starts_with(&format!("https://support.example/track#id={}&read=","a".repeat(32))));assert!(!url.contains('?'));assert!(!url.contains(&key));
 }
 #[cfg(windows)]#[tokio::test]async fn actual_status_checks_store_changes_honour_304_and_back_off(){
  let dir=crate::support_store::tests::temp("tracking");let id="a".repeat(32);let sha="c".repeat(64);
  let r=Receipt::from_report(&id,&"b".repeat(64),&sha,10,&json!({"schema":"xz-support-report/2","description":"x"}));store::begin_send(&dir,&r).unwrap();
  let first=support::validate_receipt(&receipt(&id,&sha,1,"received","received"),&id,&sha,true).unwrap();store::accept(&dir,&id,&first,store::now()).unwrap();
  // Not due yet: nothing is requested unless the reporter refreshes.
  assert_eq!(check_all("http://127.0.0.1:1",&dir,false,None,&AtomicBool::new(false)).await["checked"],0);
  let (origin,t)=server(vec![ok(&status(&id,2,"needs_info","needs_info")),"HTTP/1.1 304 Not Modified\r\nETag: \"2\"\r\nConnection: close\r\n\r\n".into()]);
  let result=check_all(&origin,&dir,true,None,&AtomicBool::new(false)).await;assert_eq!(result["updated"],json!([id.clone()]));assert_eq!(result["unseen"][0]["status_text"],"We need one more detail.");
  let again=check_all(&origin,&dir,true,None,&AtomicBool::new(false)).await;assert_eq!(again["updated"],json!([]));assert_eq!(again["unseen"].as_array().unwrap().len(),1);
  let requests=t.join().unwrap();assert!(requests[0].to_lowercase().contains("if-none-match: \"1\""));assert!(requests[1].to_lowercase().contains("if-none-match: \"2\""));
  assert!(requests.iter().all(|q|q.to_lowercase().contains(&format!("authorization: bearer {}","b".repeat(64)))));
  // Offline: the last-known state stays and the next check backs off.
  let failed=check_all("http://127.0.0.1:1",&dir,true,None,&AtomicBool::new(false)).await;assert_eq!(failed["failed"],1);
  let (_,state)=store::load(&dir,&id).unwrap();assert_eq!(state["tracking"]["lifecycle"],"needs_info");assert!(state["next_check_at"].as_u64().unwrap()>store::now()+ACTIVE_INTERVAL);
 }
 #[cfg(windows)]#[tokio::test]async fn actual_unconfirmed_send_is_recovered_by_receipt_not_reupload(){
  let dir=crate::support_store::tests::temp("recover");let id="a".repeat(32);let sha="c".repeat(64);
  let r=Receipt::from_report(&id,&"b".repeat(64),&sha,10,&json!({"schema":"xz-support-report/2","description":"x"}));store::begin_send(&dir,&r).unwrap();
  store::mark_unconfirmed(&dir,&id,"OutcomeUnknown: cancelled").unwrap();
  let (origin,t)=server(vec![ok(&receipt(&id,&sha,1,"received","received"))]);
  let result=check_all(&origin,&dir,true,None,&AtomicBool::new(false)).await;assert_eq!(result["updated"],json!([id.clone()]));
  assert!(t.join().unwrap()[0].to_lowercase().starts_with(&format!("get /v2/reports/{id}/receipt ")));assert_eq!(store::load(&dir,&id).unwrap().1["state"],"accepted");
  let other="d".repeat(32);let r=Receipt::from_report(&other,&"b".repeat(64),&sha,10,&json!({"schema":"xz-support-report/2","description":"x"}));store::begin_send(&dir,&r).unwrap();store::mark_unconfirmed(&dir,&other,"OutcomeUnknown").unwrap();
  let (origin,t)=server(vec!["HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: 21\r\nConnection: close\r\n\r\n{\"error\":\"not_found\"}".into()]);
  check_all(&origin,&dir,true,Some(&other),&AtomicBool::new(false)).await;t.join().unwrap();assert_eq!(store::load(&dir,&other).unwrap().1["state"],"not_received");
 }
}
