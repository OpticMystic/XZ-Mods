// My reports: one receipt per sent report. The receipt key, digest and the reporter's own short summary
// are sealed with current-user DPAPI. Only the safe tracking state lives beside it in plain JSON.
use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use crate::support::{self, hash, protect, valid_hex64, valid_id};

pub(crate) const RECEIPT_LIMIT:usize=200;
const RECORD_CAP:usize=16384;
const STATE_CAP:usize=16384;
const DRAFT_CAP:usize=crate::support_contract::REPORT_BYTES*2+4096;

pub(crate) fn now()->u64{SystemTime::now().duration_since(UNIX_EPOCH).map(|d|d.as_secs()).unwrap_or(0)}
pub(crate) fn dir_for(root:&Path)->Result<PathBuf,String>{
 let p=root.join("receipts");std::fs::create_dir_all(&p).map_err(|_|"My reports folder unavailable")?;support::unlinked(&p)?;Ok(p)
}
pub(crate) fn dir(app:&tauri::AppHandle)->Result<PathBuf,String>{dir_for(&support::root(app)?)}

#[derive(Clone,Debug,PartialEq)]
pub(crate) struct Receipt{pub report_id:String,pub key:String,pub sha256:String,pub bytes:u64,pub protocol:u8,pub kind_hint:Option<String>,pub topic:Option<String>,pub summary:String,pub created_at:u64}
fn summary(text:&str)->String{let one:String=text.split_whitespace().collect::<Vec<_>>().join(" ");let mut out:String=one.chars().take(80).collect();if one.chars().count()>80{out.push('…')}out}
fn option(v:&Value,list:&str)->Option<String>{v.as_str().filter(|s|support::listed(list,s)).map(str::to_string)}
fn topic(v:&Value)->Option<String>{v.as_str().filter(|s|support::contract()["reporter_hints"]["topic"].as_array().is_some_and(|a|a.iter().any(|t|t==*s))).map(str::to_string)}
impl Receipt{
 pub(crate) fn from_report(id:&str,key:&str,sha:&str,bytes:u64,report:&Value)->Receipt{
  let v2=report["schema"]=="xz-support-report/2";
  Receipt{report_id:id.into(),key:key.into(),sha256:sha.into(),bytes,protocol:if v2{2}else{1},kind_hint:option(&report["kind_hint"],"kind_hints"),
   topic:topic(&report["reporter_hints"]["topic"]),summary:summary(report[if v2{"description"}else{"public_issue"}].as_str().or(report["public_issue"]["title"].as_str()).unwrap_or("")),created_at:now()}
 }
 fn to_json(&self)->Value{json!({"schema":"xz-support-receipt-record/1","report_id":self.report_id,"receipt_key":self.key,"payload_sha256":self.sha256,"bytes":self.bytes,
  "protocol":self.protocol,"kind_hint":self.kind_hint,"topic":self.topic,"summary":self.summary,"created_at":self.created_at})}
 fn from_json(v:&Value)->Result<Receipt,String>{
  let id=v["report_id"].as_str().filter(|s|valid_id(s)).ok_or("Invalid receipt record")?;
  let key=v["receipt_key"].as_str().filter(|s|valid_hex64(s)).ok_or("Invalid receipt record")?;
  let sha=v["payload_sha256"].as_str().filter(|s|valid_hex64(s)).ok_or("Invalid receipt record")?;
  if v["schema"]!="xz-support-receipt-record/1"{return Err("Invalid receipt record".into())}
  Ok(Receipt{report_id:id.into(),key:key.into(),sha256:sha.into(),bytes:v["bytes"].as_u64().unwrap_or(0),protocol:v["protocol"].as_u64().filter(|p|*p==1||*p==2).ok_or("Invalid receipt record")? as u8,
   kind_hint:option(&v["kind_hint"],"kind_hints"),topic:topic(&v["topic"]),summary:summary(v["summary"].as_str().unwrap_or("")),created_at:v["created_at"].as_u64().unwrap_or(0)})
 }
}
fn write_atomic(path:&Path,bytes:&[u8])->Result<(),String>{
 let temporary=path.with_extension("tmp");std::fs::write(&temporary,bytes).map_err(|_|"My reports could not be saved")?;
 std::fs::rename(&temporary,path).map_err(|_|"My reports could not be saved".into())
}
fn record_path(dir:&Path,id:&str)->Result<PathBuf,String>{if !valid_id(id){return Err("Invalid report ID".into())}Ok(dir.join(format!("{id}.dpapi")))}
fn state_path(dir:&Path,id:&str)->Result<PathBuf,String>{if !valid_id(id){return Err("Invalid report ID".into())}Ok(dir.join(format!("{id}.state.json")))}
pub(crate) fn load(dir:&Path,id:&str)->Result<(Receipt,Value),String>{
 let sealed=support::read(&record_path(dir,id)?,RECORD_CAP)?;
 let receipt=Receipt::from_json(&serde_json::from_slice(&protect(&sealed,true)?).map_err(|_|"Invalid receipt record")?)?;
 if receipt.report_id!=id{return Err("Invalid receipt record".into())}
 let state=support::read(&state_path(dir,id)?,STATE_CAP).ok().and_then(|b|serde_json::from_slice::<Value>(&b).ok()).filter(|v|v["report_id"]==id).unwrap_or_else(||json!({"report_id":id,"state":"unknown"}));
 Ok((receipt,state))
}
pub(crate) fn update(dir:&Path,id:&str,change:impl FnOnce(&mut Value))->Result<Value,String>{
 let (_,mut state)=load(dir,id)?;change(&mut state);state["report_id"]=json!(id);state["schema"]=json!("xz-support-receipt-state/1");
 write_atomic(&state_path(dir,id)?,&serde_json::to_vec(&state).map_err(|_|"My reports could not be saved")?)?;Ok(state)
}
fn ids(dir:&Path)->Vec<String>{
 let mut out:Vec<String>=std::fs::read_dir(dir).map(|e|e.flatten().filter_map(|e|e.file_name().to_str().and_then(|n|n.strip_suffix(".dpapi")).filter(|s|valid_id(s)).map(str::to_string)).collect()).unwrap_or_default();out.sort();out
}
// The key is sealed before the first upload byte. A retry must present the same key and digest.
pub(crate) fn begin_send(dir:&Path,r:&Receipt)->Result<(),String>{
 let path=record_path(dir,&r.report_id)?;
 if path.exists(){
  let (existing,_)=load(dir,&r.report_id)?;
  if existing.key!=r.key||existing.sha256!=r.sha256||existing.bytes!=r.bytes{return Err("A different report already uses this ID".into())}
 }else{
  if ids(dir).len()>=RECEIPT_LIMIT{return Err("My reports is full. Forget an old report, then send again.".into())}
  write_atomic(&path,&protect(&serde_json::to_vec(&r.to_json()).map_err(|_|"Receipt could not be sealed")?,false)?)?;
 }
 update(dir,&r.report_id,|s|if s["state"]!="accepted"{s["state"]=json!("sending");})?;Ok(())
}
pub(crate) fn accept(dir:&Path,id:&str,receipt:&Value,at:u64)->Result<Value,String>{
 let revision=receipt["tracking"]["revision"].as_u64().unwrap_or(1);
 update(dir,id,|s|{s["state"]=json!("accepted");s["accepted_at"]=receipt["accepted_at"].clone();s["diagnostics_expires_at"]=receipt["diagnostics_expires_at"].clone();
  s["private_logs"]=receipt["private_logs"].clone();s["tracking"]=receipt["tracking"].clone();s["seen_revision"]=json!(revision);
  s["checked_at"]=json!(at);s["next_check_at"]=json!(at+900);s["failures"]=json!(0);s["last_error"]=Value::Null;})?;
 view_of(dir,id)
}
pub(crate) fn mark_unconfirmed(dir:&Path,id:&str,error:&str)->Result<Value,String>{
 let code=if error.contains("OutcomeUnknown"){"outcome_unknown"}else if error.contains("HTTP 4"){"refused"}else{"failed"};
 update(dir,id,|s|if s["state"]!="accepted"{s["state"]=json!("unconfirmed");s["last_error"]=json!(code);})
}
pub(crate) fn clear_draft(root:&Path,id:&str){if valid_id(id){for suffix in ["draft","attempt","consent","review","receipt"]{let _=std::fs::remove_file(root.join(format!("{id}.{suffix}")));}}}
pub(crate) fn forget(dir:&Path,root:&Path,id:&str)->Result<(),String>{
 std::fs::remove_file(record_path(dir,id)?).map_err(|_|"This report is not in My reports")?;let _=std::fs::remove_file(state_path(dir,id)?);clear_draft(root,id);Ok(())
}
pub(crate) fn seen(dir:&Path,id:&str)->Result<Value,String>{update(dir,id,|s|{let revision=s["tracking"]["revision"].clone();if !revision.is_null(){s["seen_revision"]=revision;}})}
// The renderer receives this projection only: never the key, digest-derived links or server text.
pub(crate) fn view(r:&Receipt,s:&Value)->Value{
 let t=&s["tracking"];let revision=t["revision"].as_u64().unwrap_or(0);
 json!({"report_id":r.report_id,"state":s["state"].as_str().unwrap_or("unknown"),"protocol":r.protocol,"kind_hint":r.kind_hint,"topic":r.topic,"summary":r.summary,
  "classified_kind":t["classified_kind"],"lifecycle":t["lifecycle"],"status_text_key":t["status_text_key"],"status_text":t["status_text"],"question":t["question"],"revision":revision,
  "created_at":r.created_at,"accepted_at":s["accepted_at"],"updated_at":t["updated_at"],"retention_until":t["retention_until"],"diagnostics_expires_at":s["diagnostics_expires_at"],
  "unseen":revision>s["seen_revision"].as_u64().unwrap_or(revision),"checked_at":s["checked_at"],"last_error":s["last_error"],"links_revoked":s["links_revoked"]==true})
}
pub(crate) fn view_of(dir:&Path,id:&str)->Result<Value,String>{let (r,s)=load(dir,id)?;Ok(view(&r,&s))}
pub(crate) fn list(dir:&Path)->Vec<(Receipt,Value)>{
 let mut out:Vec<(Receipt,Value)>=ids(dir).iter().take(RECEIPT_LIMIT).filter_map(|id|load(dir,id).ok()).collect();
 out.sort_by(|a,b|b.0.created_at.cmp(&a.0.created_at).then(a.0.report_id.cmp(&b.0.report_id)));out
}
// Older app versions kept a sent draft's receipt key in plaintext beside the draft. Move each one into
// DPAPI after checking the draft's digest and identity. No recollection and no upload. An accepted
// report's plaintext copy is removed; an unconfirmed one keeps its exact bytes for a retry or lookup.
pub(crate) fn migrate_legacy(root:&Path,dir:&Path)->usize{
 let mut moved=0;
 let Ok(entries)=std::fs::read_dir(root) else{return 0};
 for entry in entries.flatten(){
  let Some(id)=entry.file_name().to_str().and_then(|n|n.strip_suffix(".draft")).filter(|s|valid_id(s)).map(str::to_string) else{continue};
  if dir.join(format!("{id}.dpapi")).exists(){continue}
  let accepted=root.join(format!("{id}.receipt")).is_file();let attempted=root.join(format!("{id}.attempt")).is_file();
  if !accepted&&!attempted{continue}
  let Ok(raw)=support::read(&root.join(format!("{id}.draft")),DRAFT_CAP) else{continue};
  let Ok(v)=serde_json::from_slice::<Value>(&raw) else{continue};
  let (Some(body),Some(sha),Some(key))=(v["body"].as_str(),v["sha256"].as_str(),v["key"].as_str()) else{continue};
  if hash(body.as_bytes())!=sha||!valid_hex64(key){continue}
  let Ok(report)=serde_json::from_str::<Value>(body) else{continue};
  if report["report_id"]!=id.as_str()||!matches!(report["schema"].as_str(),Some("xz-support-report/1"|"xz-support-report/2")){continue}
  let receipt=Receipt::from_report(&id,key,sha,body.len() as u64,&report);
  if begin_send(dir,&receipt).is_err(){continue}
  let state=if accepted{"accepted"}else{"unconfirmed"};
  if update(dir,&id,|s|{s["state"]=json!(state);s["migrated"]=json!(true);s["next_check_at"]=json!(0);if !accepted{s["last_error"]=json!("outcome_unknown");}}).is_err(){continue}
  if accepted{clear_draft(root,&id);}
  moved+=1;
 }
 moved
}
#[tauri::command]
pub fn support_reports(app:tauri::AppHandle)->Result<Value,String>{
 let root=support::root(&app)?;let dir=dir_for(&root)?;let migrated=migrate_legacy(&root,&dir);
 Ok(json!({"reports":list(&dir).iter().map(|(r,s)|view(r,s)).collect::<Vec<_>>(),"limit":RECEIPT_LIMIT,"migrated":migrated}))
}
#[tauri::command]
pub fn support_report_seen(app:tauri::AppHandle,id:String)->Result<Value,String>{let dir=dir(&app)?;seen(&dir,&id)?;view_of(&dir,&id)}
#[tauri::command]
pub fn support_forget(app:tauri::AppHandle,id:String)->Result<Value,String>{let root=support::root(&app)?;forget(&dir_for(&root)?,&root,&id)?;Ok(json!({"forgotten":true}))}

#[cfg(test)]pub(crate) mod tests{
 use super::*;
 pub(crate) fn temp(name:&str)->PathBuf{
  let base=std::env::var_os("XZ_SUPPORT_TEST_OUTPUT").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
  let p=base.join(format!("{name}-{}-{}",std::process::id(),hash(format!("{:?}",std::time::Instant::now()).as_bytes())[..8].to_string()));std::fs::create_dir_all(&p).unwrap();p
 }
 fn receipt(id:&str)->Receipt{Receipt::from_report(id,&"b".repeat(64),&"c".repeat(64),100,&json!({"schema":"xz-support-report/2","kind_hint":"BUG","reporter_hints":{"topic":"NOT_WORKING"},"description":"Decks 3 and 4   went\nsilent after the cable was moved during a long set at the club tonight, twice."}))}
 #[cfg(windows)]#[test]fn sealed_record_holds_the_key_and_the_view_never_does(){
  let dir=temp("receipts");let r=receipt(&"a".repeat(32));begin_send(&dir,&r).unwrap();
  let sealed=std::fs::read(dir.join(format!("{}.dpapi","a".repeat(32)))).unwrap();
  assert!(!String::from_utf8_lossy(&sealed).contains(&"b".repeat(64)),"key is not stored in plaintext");
  let (loaded,state)=load(&dir,&"a".repeat(32)).unwrap();assert_eq!(loaded,r);assert_eq!(state["state"],"sending");
  assert_eq!(loaded.summary.chars().count(),81);assert!(loaded.summary.starts_with("Decks 3 and 4 went silent"));
  let tracking=crate::support_tracking::tests::status(&"a".repeat(32),1,"received","received");
  let view=accept(&dir,&"a".repeat(32),&json!({"accepted_at":"2026-10-27T10:00:00.000Z","diagnostics_expires_at":"2026-11-10T10:00:00.000Z","private_logs":"stored","tracking":tracking}),1000).unwrap();
  let text=view.to_string();assert!(!text.contains(&"b".repeat(64)));assert!(!text.contains(&"c".repeat(64)));assert_eq!(view["unseen"],false);assert_eq!(view["lifecycle"],"received");
  // Retrying with the same key and digest is allowed; another key for the same ID is not.
  begin_send(&dir,&r).unwrap();let mut other=r.clone();other.key="d".repeat(64);assert!(begin_send(&dir,&other).is_err());
  assert_eq!(load(&dir,&"a".repeat(32)).unwrap().1["state"],"accepted","an accepted receipt never returns to sending");
  // A newer revision is unseen until the reporter opens it; the notice is then gone across restarts.
  update(&dir,&"a".repeat(32),|s|s["tracking"]["revision"]=json!(3)).unwrap();assert_eq!(view_of(&dir,&"a".repeat(32)).unwrap()["unseen"],true);
  seen(&dir,&"a".repeat(32)).unwrap();assert_eq!(view_of(&dir,&"a".repeat(32)).unwrap()["unseen"],false);
  let root=temp("forget-root");forget(&dir,&root,&"a".repeat(32)).unwrap();assert!(list(&dir).is_empty());assert!(forget(&dir,&root,&"a".repeat(32)).is_err());
  assert!(load(&dir,"../owner-key").is_err());
 }
 #[cfg(windows)]#[test]fn receipt_list_is_bounded(){
  let dir=temp("receipt-limit");for i in 0..RECEIPT_LIMIT{begin_send(&dir,&receipt(&format!("{i:032x}"))).unwrap();}
  assert!(begin_send(&dir,&receipt(&"f".repeat(32))).unwrap_err().contains("full"));assert_eq!(list(&dir).len(),RECEIPT_LIMIT);
 }
 #[cfg(windows)]#[test]fn legacy_sent_drafts_move_into_protected_storage_once(){
  let root=temp("legacy");let dir=dir_for(&root).unwrap();
  let write=|id:&str,schema:&str,marker:Option<&str>|{
   let body=serde_json::to_string(&json!({"schema":schema,"report_id":id,"public_issue":{"title":"Four decks unavailable","description":"Steps"},"description":"Silent decks","private":{"items":[]}})).unwrap();
   std::fs::write(root.join(format!("{id}.draft")),serde_json::to_vec(&json!({"body":body,"sha256":hash(body.as_bytes()),"key":"e".repeat(64)})).unwrap()).unwrap();
   if let Some(m)=marker{std::fs::write(root.join(format!("{id}.{m}")),b"{}").unwrap();}
  };
  write(&"1".repeat(32),"xz-support-report/1",Some("receipt"));write(&"2".repeat(32),"xz-support-report/2",Some("attempt"));write(&"3".repeat(32),"xz-support-report/1",None);
  std::fs::write(root.join(format!("{}.draft","4".repeat(32))),br#"{"body":"{}","sha256":"0","key":"x"}"#).unwrap();std::fs::write(root.join(format!("{}.receipt","4".repeat(32))),b"{}").unwrap();
  assert_eq!(migrate_legacy(&root,&dir),2);
  let accepted=load(&dir,&"1".repeat(32)).unwrap();assert_eq!((accepted.0.protocol,accepted.1["state"].as_str()),(1,Some("accepted")));assert_eq!(accepted.0.summary,"Four decks unavailable");
  assert!(!root.join(format!("{}.draft","1".repeat(32))).exists(),"plaintext key removed after migration");
  let unknown=load(&dir,&"2".repeat(32)).unwrap();assert_eq!((unknown.0.protocol,unknown.1["state"].as_str()),(2,Some("unconfirmed")));
  assert!(root.join(format!("{}.draft","2".repeat(32))).exists(),"unconfirmed bytes kept for retry");
  assert!(root.join(format!("{}.draft","3".repeat(32))).exists()&&load(&dir,&"3".repeat(32)).is_err(),"unsent drafts stay drafts");
  assert!(load(&dir,&"4".repeat(32)).is_err(),"a damaged draft is not migrated");
  assert_eq!(migrate_legacy(&root,&dir),0,"migration runs once per report");assert_eq!(list(&dir).len(),2);
 }
}
