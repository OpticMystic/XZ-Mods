use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{path::{Path, PathBuf}, sync::{Arc, Mutex, OnceLock, atomic::{AtomicBool, Ordering}}, time::Duration};
use tauri::Manager;
use tauri_plugin_dialog::DialogExt;
use crate::{support_store as store, support_tracking as tracking};
pub(crate) const ORIGIN:&str="https://xz-mods-support.will-026.workers.dev";
const CAP:usize=crate::support_contract::REPORT_BYTES;
#[derive(Default)]
pub struct Support { busy:Arc<AtomicBool>, cancel:Arc<AtomicBool>, pub(crate) checking:Arc<AtomicBool>, inbox:Mutex<Vec<u8>> }
// The generated contract is the only source of kinds, consent text, capability domains and status text.
pub(crate) fn contract()->&'static Value{static C:OnceLock<Value>=OnceLock::new();C.get_or_init(||serde_json::from_str(crate::support_contract::CONTRACT_JSON).expect("Validated support contract"))}
pub(crate) fn consent_text()->&'static str{contract()["consent"].as_str().expect("Contract consent")}
pub(crate) fn listed(list:&str,value:&str)->bool{contract()[list].as_array().is_some_and(|items|items.iter().any(|item|item==value))}
fn lower_hex(v:&str,len:usize)->bool{v.len()==len&&v.bytes().all(|c|c.is_ascii_digit()||(b'a'..=b'f').contains(&c))}
pub(crate) fn valid_id(id:&str)->bool{lower_hex(id,32)}
pub(crate) fn valid_hex64(v:&str)->bool{lower_hex(v,64)}
pub(crate) fn hash(b:&[u8])->String{format!("{:x}",Sha256::digest(b))}
pub(crate) fn unlinked(p:&Path)->Result<(),String>{
 for parent in p.ancestors(){if let Ok(m)=std::fs::symlink_metadata(parent){
  #[cfg(windows)] {use std::os::windows::fs::MetadataExt;if m.file_attributes()&0x400!=0{return Err("Linked report path refused".into())}}
  if m.file_type().is_symlink(){return Err("Linked report path refused".into())}
 }} Ok(())
}
pub(crate) fn root(app:&tauri::AppHandle)->Result<PathBuf,String>{
 let p=app.path().app_local_data_dir().map_err(|_|"App data unavailable")?.join("support-reports");
 std::fs::create_dir_all(&p).map_err(|_|"Private report folder unavailable")?;unlinked(&p)?;Ok(p)
}
pub(crate) fn read(path:&Path,cap:usize)->Result<Vec<u8>,String>{
 use std::io::Read;
 let m=std::fs::symlink_metadata(path).map_err(|_|"Report file unavailable")?;
 #[cfg(windows)] {use std::os::windows::fs::MetadataExt;if m.file_attributes()&0x400!=0{return Err("Linked report file refused".into())}}
 if !m.is_file()||m.file_type().is_symlink()||m.len()>cap as u64{return Err("Invalid report file size or type".into())}
 let mut b=Vec::new();std::fs::File::open(path).map_err(|_|"Report unavailable")?.take((cap+1)as u64).read_to_end(&mut b).map_err(|_|"Report unavailable")?;
 if b.len()>cap{return Err("Report too large".into())}Ok(b)
}
pub(crate) struct Draft{pub body:Vec<u8>,pub key:String,pub report:Value}
fn draft(dir:&Path,id:&str,sha:&str)->Result<Draft,String>{
 if !valid_id(id){return Err("Invalid report ID".into())}
 let consent=read(&dir.join(format!("{id}.consent")),4096).map_err(|_|"Press Send to approve this exact report".to_string())?;
 checked(&read(&dir.join(format!("{id}.draft")),CAP*2)?,&consent,id,sha)
}
// Send is consent to one exact frozen body. The collector records that consent with the body's SHA256 and
// byte count (support_consent); only a body matching that record is uploaded. Viewing pages is optional.
fn checked(bytes:&[u8],consent:&[u8],id:&str,sha:&str)->Result<Draft,String>{
 let v:Value=serde_json::from_slice(bytes).map_err(|_|"Invalid draft")?;
 let body=v["body"].as_str().ok_or("Invalid draft")?.as_bytes().to_vec();
 if body.len()>CAP||hash(&body)!=sha||v["sha256"]!=sha{return Err("Stale preview or changed draft".into())}
 let c:Value=serde_json::from_slice(consent).map_err(|_|"Press Send to approve this exact report")?;
 if c["schema"]!="xz-support-consent/1"||c["report_id"]!=id||c["sha256"]!=sha||c["bytes"].as_u64()!=Some(body.len() as u64)||c["consent"]!=consent_text(){
  return Err("Send approves one exact report. Check it and send again.".into())
 }
 let key=v["key"].as_str().ok_or("Receipt key missing")?.to_string();
 if !valid_hex64(&key){return Err("Invalid receipt key".into())}
 let report:Value=serde_json::from_slice(&body).map_err(|_|"Invalid report")?;
 if report["report_id"]!=id||report["schema"]!="xz-support-report/2"{return Err("Report identity mismatch".into())}
 Ok(Draft{body,key,report})
}
struct Busy(Arc<AtomicBool>);impl Drop for Busy{fn drop(&mut self){self.0.store(false,Ordering::Release);}}
fn begin(state:&Support)->Result<Busy,String>{
 state.busy.compare_exchange(false,true,Ordering::AcqRel,Ordering::Acquire).map_err(|_|"A support request is already running")?;
 state.cancel.store(false,Ordering::Release);Ok(Busy(state.busy.clone()))
}
pub(crate) fn begin_checks(state:&Support)->Option<impl Drop>{
 state.checking.compare_exchange(false,true,Ordering::AcqRel,Ordering::Acquire).ok().map(|_|Busy(state.checking.clone()))
}
async fn cancellable<T>(future:impl std::future::Future<Output=Result<T,reqwest::Error>>,cancel:&AtomicBool)->Result<T,String>{
 tokio::pin!(future);
 loop{tokio::select!{r=&mut future=>return r.map_err(|_|"OutcomeUnknown: connection failed. Check receipt before retrying.".into()),_ = tokio::time::sleep(Duration::from_millis(50))=>if cancel.load(Ordering::Acquire){return Err("OutcomeUnknown: cancelled; the server may have accepted it. Check receipt.".into())}}}
}
pub(crate) struct Reply{pub value:Option<Value>}
// One bounded HTTPS exchange. No redirects, no compression, JSON only. A 304 has no body. A refusal
// surfaces only the service's short error code, never other response text.
pub(crate) async fn call(origin:&str,method:reqwest::Method,path:&str,key:&str,body:Option<Vec<u8>>,extra:&[(&str,String)],cap:usize,cancel:&AtomicBool)->Result<Reply,String>{
 let _=rustls::crypto::ring::default_provider().install_default();
 let client=reqwest::Client::builder().user_agent("xz-support/2").redirect(reqwest::redirect::Policy::none()).connect_timeout(Duration::from_secs(8)).timeout(Duration::from_secs(25)).https_only(!cfg!(test)).build().map_err(|_|"Support connection unavailable")?;
 let mut q=client.request(method,format!("{origin}{path}")).bearer_auth(key).header("Accept","application/json");
 if let Some(b)=body{q=q.header("Content-Type","application/json").header("Content-Length",b.len()).body(b)}
 for (name,value) in extra{q=q.header(*name,value.as_str())}
 let mut r=cancellable(q.send(),cancel).await?;
 let status=r.status().as_u16();
 if status==304{return Ok(Reply{value:None})}
 if !r.status().is_success(){
  let mut text=Vec::new();
  while let Ok(Some(chunk))=cancellable(r.chunk(),cancel).await{if text.len()+chunk.len()>4096{break}text.extend_from_slice(&chunk)}
  let code=serde_json::from_slice::<Value>(&text).ok().and_then(|v|v["error"].as_str().map(str::to_string)).filter(|c|!c.is_empty()&&c.len()<=40&&c.bytes().all(|b|b.is_ascii_lowercase()||b==b'_'));
  return Err(match code{Some(code)=>format!("Support service returned HTTP {status} ({code})."),None=>format!("Support service returned HTTP {status}. Retry setup or export offline.")})
 }
 if !r.headers().get("content-type").and_then(|v|v.to_str().ok()).unwrap_or("").starts_with("application/json")||r.headers().contains_key("content-encoding"){return Err("Unsupported support response".into())}
 if r.content_length().is_some_and(|n|n>cap as u64){return Err("Support response too large".into())}
 let mut bytes=Vec::new();while let Some(chunk)=cancellable(r.chunk(),cancel).await?{if bytes.len()+chunk.len()>cap{return Err("Support response too large".into())}bytes.extend_from_slice(&chunk)}
 Ok(Reply{value:Some(serde_json::from_slice(&bytes).map_err(|_|"Invalid support response")?)})
}
pub(crate) async fn exchange(origin:&str,method:reqwest::Method,path:&str,key:&str,body:Option<Vec<u8>>,extra:&[(&str,String)],cap:usize,cancel:&AtomicBool)->Result<Value,String>{
 call(origin,method,path,key,body,extra,cap,cancel).await?.value.ok_or("Unexpected empty support response".into())
}
#[tauri::command]
pub fn support_cancel(state:tauri::State<Support>){state.cancel.store(true,Ordering::Release);}
pub(crate) fn open(url:&str)->Result<(),String>{
 #[cfg(windows)] {use std::os::windows::process::CommandExt;std::process::Command::new("rundll32.exe").args(["url.dll,FileProtocolHandler",url]).creation_flags(0x08000000).spawn().map_err(|_|"Browser unavailable")?;Ok(())}
 #[cfg(not(windows))] {let _=url;Err("Open the status page on Windows".into())}
}
fn iso(v:&Value)->Option<String>{
 v.as_str().filter(|s|(20..=32).contains(&s.len())&&s.as_bytes()[4]==b'-'&&s.ends_with('Z')&&s.bytes().all(|b|b.is_ascii_digit()||b"-:.TZ".contains(&b))).map(str::to_string)
}
// A receipt proves private storage of these exact bytes. Only safe, validated fields reach the store or the renderer.
pub(crate) fn validate_receipt(r:&Value,id:&str,sha:&str,fresh:bool)->Result<Value,String>{
 let logs=r["private_logs"].as_str().unwrap_or("");
 if r["schema"]!="xz-support-receipt/2"||r["report_id"]!=id||r["payload_sha256"]!=sha||r["publication_mode"]!="none"||!(logs=="stored"||(!fresh&&logs=="gone")){
  return Err("Unexpected receipt; private storage not confirmed".into())
 }
 let (reference,item)=tracking::identifiers(r)?;
 let projection=tracking::projection(&r["tracking"],id)?;
 if projection["report_reference"]!=reference||projection["item_number"]!=item{return Err("Receipt identifiers do not match status".into())}
 Ok(json!({"accepted_at":iso(&r["accepted_at"]).ok_or("Unexpected receipt time")?,"diagnostics_expires_at":iso(&r["expires_at"]).ok_or("Unexpected receipt time")?,
  "private_logs":logs,"tracking":projection}))
}
pub(crate) async fn submit(origin:&str,id:&str,sha:&str,d:&Draft,cancel:&AtomicBool)->Result<Value,String>{
 let extra=[("X-Report-ID",id.to_string()),("X-Report-SHA256",sha.to_string()),("X-Report-Consent",consent_text().to_string())];
 let receipt=exchange(origin,reqwest::Method::POST,"/v2/reports",&d.key,Some(d.body.clone()),&extra,65536,cancel).await?;
 validate_receipt(&receipt,id,sha,true)
}
// Upload the exact frozen bytes the reporter approved. The receipt key is protected before the first byte leaves,
// so an uncertain send is retried with the same ID, key and digest rather than creating another report.
#[tauri::command]
pub async fn support_send(app:tauri::AppHandle,state:tauri::State<'_,Support>,id:String,sha256:String)->Result<Value,String>{
 let _guard=begin(&state)?;let dir=root(&app)?;let d=draft(&dir,&id,&sha256)?;let receipts=store::dir(&app)?;
 store::begin_send(&receipts,&store::Receipt::from_report(&id,&d.key,&sha256,d.body.len() as u64,&d.report))?;
 std::fs::write(dir.join(format!("{id}.attempt")),sha256.as_bytes()).map_err(|_|"Cannot retain send attempt")?;
 match submit(ORIGIN,&id,&sha256,&d,&state.cancel).await{
  Ok(receipt)=>{
   let view=store::accept(&receipts,&id,&receipt,store::now())?;
   // The server holds the body and DPAPI holds the key: drop the plaintext key and draft.
   store::clear_draft(&dir,&id);
   tracking::initialize_links(ORIGIN,&receipts,&id).await;
   Ok(view)
  },
  Err(error)=>{let _=store::mark_unconfirmed(&receipts,&id,&error);Err(error)}
 }
}
// Offline fallback: the same consented bytes to a file the reporter chooses.
#[tauri::command]
pub async fn support_export(app:tauri::AppHandle,state:tauri::State<'_,Support>,id:String,sha256:String)->Result<Value,String>{
 let _guard=begin(&state)?;let d=draft(&root(&app)?,&id,&sha256)?;
 let selected=app.dialog().file().set_file_name("Private XZ Mods report.json").add_filter("Private report",&["json"]).blocking_save_file();
 if let Some(path)=selected{let path=path.into_path().map_err(|_|"Invalid export path")?;std::fs::write(path,&d.body).map_err(|_|"Export failed")?;Ok(json!({"exported":true}))}else{Ok(json!({"exported":false}))}
}
// Owner credentials are protected by Windows DPAPI for the current OS user. Receipt records use the same.
#[cfg(windows)]
pub(crate) fn protect(input:&[u8],decrypt:bool)->Result<Vec<u8>,String>{
 #[repr(C)]struct Blob{len:u32,data:*mut u8}
 #[link(name="crypt32")]extern "system"{fn CryptProtectData(i:*const Blob,d:*const u16,e:*const Blob,r:*mut u8,p:*mut u8,f:u32,o:*mut Blob)->i32;fn CryptUnprotectData(i:*const Blob,d:*mut *mut u16,e:*const Blob,r:*mut u8,p:*mut u8,f:u32,o:*mut Blob)->i32;}
 #[link(name="kernel32")]extern "system"{fn LocalFree(p:*mut u8)->*mut u8;}
 let i=Blob{len:input.len()as u32,data:input.as_ptr()as *mut u8};let mut o=Blob{len:0,data:std::ptr::null_mut()};
 let ok=unsafe{if decrypt{CryptUnprotectData(&i,std::ptr::null_mut(),std::ptr::null(),std::ptr::null_mut(),std::ptr::null_mut(),1,&mut o)}else{CryptProtectData(&i,std::ptr::null(),std::ptr::null(),std::ptr::null_mut(),std::ptr::null_mut(),1,&mut o)}};
 if ok==0{return Err("Windows private key storage unavailable".into())}let result=unsafe{std::slice::from_raw_parts(o.data,o.len as usize).to_vec()};unsafe{LocalFree(o.data);}Ok(result)
}
#[cfg(not(windows))]pub(crate) fn protect(_: &[u8],_:bool)->Result<Vec<u8>,String>{Err("Reports and the owner connection need Windows private storage".into())}
#[tauri::command]
pub fn support_connect(app:tauri::AppHandle,key:String)->Result<Value,String>{
 if key.len()<32||key.len()>256||!key.bytes().all(|c|c.is_ascii_graphic()){return Err("Invalid report-only owner key".into())}
 let bytes=protect(key.as_bytes(),false)?;std::fs::write(root(&app)?.join("owner-key.dpapi"),bytes).map_err(|_|"Owner connection could not be saved")?;Ok(json!({"connected":true}))
}
#[tauri::command]
pub fn support_owner_status(app:tauri::AppHandle)->Result<Value,String>{Ok(json!({"connected":root(&app)?.join("owner-key.dpapi").is_file()}))}
const OWNER_PAGE:usize=24*1024;
fn decode_page(encoded:&str)->Result<Vec<u8>,String>{
 if encoded.is_empty()||encoded.len()>OWNER_PAGE*4/3||encoded.len()%4!=0{return Err("Invalid owner page encoding size".into())}
 let digit=|b:u8|->Result<u8,String>{match b{b'A'..=b'Z'=>Ok(b-b'A'),b'a'..=b'z'=>Ok(b-b'a'+26),b'0'..=b'9'=>Ok(b-b'0'+52),b'+'=>Ok(62),b'/'=>Ok(63),_=>Err("Invalid owner page encoding".into())}};
 let mut out=Vec::with_capacity(encoded.len()/4*3);
 for (index,c) in encoded.as_bytes().chunks_exact(4).enumerate(){
  let a=digit(c[0])?;let b=digit(c[1])?;let final_chunk=index+1==encoded.len()/4;
  if c[2]==b'='{
   if !final_chunk||c[3]!=b'='||b&15!=0{return Err("Invalid owner page padding".into())}
   out.push((a<<2)|(b>>4));
  }else{
   let d=digit(c[2])?;out.push((a<<2)|(b>>4));out.push((b<<4)|(d>>2));
   if c[3]==b'=' {if !final_chunk||d&3!=0{return Err("Invalid owner page padding".into())}}
   else{out.push((d<<6)|digit(c[3])?);}
  }
 }
 Ok(out)
}
#[derive(Default)]
struct OwnerReport{bytes:Vec<u8>,sha:Option<String>,total:Option<usize>,complete:bool}
impl OwnerReport{
 fn push(&mut self,page:&Value,id:&str)->Result<bool,String>{
  if self.complete||page.get("next_offset").is_none()||page["schema"]!="xz-support-owner-page/1"||page["report_id"]!=id||page["encoding"]!="base64"{return Err("Unexpected owner report page".into())}
  let sha=page["payload_sha256"].as_str().ok_or("Owner page hash missing")?;
  if !valid_hex64(sha){return Err("Invalid owner report hash".into())}
  // The nested receipt must name the same report and digest on every page (OWNER-PAGING.md).
  let receipt=&page["receipt"];
  if !matches!(receipt["schema"].as_str(),Some("xz-support-receipt/1"|"xz-support-receipt/2"))||receipt["report_id"]!=id||receipt["payload_sha256"]!=sha{return Err("Owner page receipt does not match the report".into())}
  let total=page["total_bytes"].as_u64().filter(|n|*n>0&&*n<=CAP as u64).ok_or("Invalid owner report size")? as usize;
  if self.sha.as_deref().is_some_and(|h|h!=sha)||self.total.is_some_and(|n|n!=total){return Err("Owner report changed between pages".into())}
  if page["offset"].as_u64()!=Some(self.bytes.len() as u64){return Err("Owner report page is not contiguous".into())}
  let chunk=decode_page(page["data_base64"].as_str().ok_or("Owner page data missing")?)?;
  let end=self.bytes.len()+chunk.len();
  if chunk.len()>OWNER_PAGE||end>total{return Err("Owner page exceeds report bounds".into())}
  let finished=end==total;
  if if finished{!page["next_offset"].is_null()}else{page["next_offset"].as_u64()!=Some(end as u64)||chunk.len()!=OWNER_PAGE}{return Err("Invalid owner page continuation".into())}
  self.sha=Some(sha.to_string());self.total=Some(total);self.bytes.extend_from_slice(&chunk);
  if finished{
   if hash(&self.bytes)!=sha{return Err("Owner report integrity failed".into())}
   // Only parse/display after the entire original byte sequence is verified.
   let report:Value=serde_json::from_slice(&self.bytes).map_err(|_|"Invalid verified owner report")?;
   if !matches!(report["schema"].as_str(),Some("xz-support-report/1"|"xz-support-report/2"))||report["report_id"]!=id{return Err("Verified owner report identity mismatch".into())}
   self.complete=true;
  }
  Ok(finished)
 }
}
async fn read_owner(origin:&str,id:&str,key:&str,cancel:&AtomicBool)->Result<Vec<u8>,String>{
 read_report_pages(origin,id,key,&format!("/v2/admin/reports/{id}"),cancel).await
}
pub(crate) async fn read_report_pages(origin:&str,id:&str,key:&str,endpoint:&str,cancel:&AtomicBool)->Result<Vec<u8>,String>{
 let mut report=OwnerReport::default();
 for _ in 0..CAP.div_ceil(OWNER_PAGE){
  if cancel.load(Ordering::Acquire){return Err("Owner report read cancelled".into())}
  let path=format!("{endpoint}?offset={}",report.bytes.len());
  let page=exchange(origin,reqwest::Method::GET,&path,key,None,&[],65535,cancel).await?;
  if report.push(&page,id)?{return Ok(report.bytes)}
 }
 Err("Owner report exceeded the bounded page count".into())
}
fn version(v:&str)->bool{
 if v=="unknown"{return true}
 if v.is_empty()||v.len()>64{return false}
 let (core,rest)=v.split_at(v.find(['-','+']).unwrap_or(v.len()));
 let parts:Vec<&str>=core.split('.').collect();
 if parts.len()!=3||parts.iter().any(|p|p.is_empty()||!p.bytes().all(|b|b.is_ascii_digit())){return false}
 let ok=|s:&str|!s.is_empty()&&s.bytes().all(|b|b.is_ascii_lowercase()||b.is_ascii_digit()||b==b'.');
 match rest.split_once('+'){
  Some((pre,build))=>ok(build)&&(pre.is_empty()||pre.strip_prefix('-').is_some_and(ok)),
  None=>rest.is_empty()||rest.strip_prefix('-').is_some_and(ok),
 }
}
fn enum_or_null(v:&Value,list:&str)->Result<Value,String>{
 if v.is_null(){return Ok(Value::Null)}
 v.as_str().filter(|s|listed(list,s)).map(|s|json!(s)).ok_or_else(||format!("Unexpected owner {list} value"))
}
fn bounded_list(v:&Value,max:usize,item:impl Fn(&Value)->Option<Value>)->Result<Value,String>{
 let items=v.as_array().filter(|a|a.len()<=max).ok_or("Unexpected owner list")?;
 items.iter().map(|x|item(x).ok_or("Unexpected owner list value".to_string())).collect::<Result<Vec<_>,_>>().map(Value::Array)
}
// Owner rows are rebuilt from validated fields only: kind, severity, impact, baseline, affected features,
// in use since, reach and status (FEEDBACK-TAXONOMY.md section 5). Eligibility is the contract rule only.
fn owner_row(r:&Value)->Result<Value,String>{
 let (reference,item)=tracking::identifiers(r)?;
 let id=r["report_id"].as_str().filter(|s|valid_id(s)).ok_or("Unexpected owner report ID")?;
 let desk=if r["desk"].is_null(){Value::Null}else{
  let d=&r["desk"];let reach=&d["reach"];
  json!({"expectation":enum_or_null(&d["expectation"],"expectations")?,"impact":enum_or_null(&d["impact"],"impacts")?,
   "severity":enum_or_null(&d["severity"],"severities")?,
   "affected_features":bounded_list(&d["affected_features"],16,|x|x.as_str().filter(|s|listed("affected_features",s)).map(|s|json!(s)))?,
   "in_use_since":d["in_use_since"].as_str().filter(|s|version(s)).ok_or("Unexpected in-use version")?,
   "proposed_setting":enum_or_null(&d["proposed_setting"],"affected_features")?,
   "reach":{"versions":bounded_list(&reach["versions"],16,|x|x.as_str().filter(|s|version(s)).map(|s|json!(s)))?,
    "interfaces":bounded_list(&reach["interfaces"],2,|x|x.as_str().filter(|s|matches!(*s,"MODERN"|"CLASSIC")).map(|s|json!(s)))?,
    "decks":bounded_list(&reach["decks"],4,|x|x.as_u64().filter(|n|(1..=4).contains(n)).map(|n|json!(n)))?}})
 };
 let lifecycle=r["lifecycle"].as_str().filter(|s|listed("lifecycles",s)).ok_or("Unexpected owner lifecycle")?;
 let origin=r["origin"].as_str().unwrap_or("loader");
 if !matches!(origin,"loader"|"controller"){return Err("Unexpected report origin".into())}
 let installation=r["installation_id"].as_str();let loader=r["loader_id"].as_str();
 if installation.is_some_and(|s|!crate::controller_identity::canonical_installation(s))||loader.is_some_and(|s|!crate::controller_identity::canonical_loader(s)){return Err("Unexpected loader identity".into())}
 let comments=r["comments_state"].as_str().unwrap_or("completed");
 if !contract()["pending_lifecycles"].as_array().is_some_and(|a|a.iter().any(|v|v==comments)){return Err("Unexpected comments state".into())}
 let pending=matches!(comments,"pending_comments"|"expired_reminders");
 Ok(json!({"origin":origin,"installation_id":installation,"loader_id":loader,"comments_state":comments,"comments_pending":pending,"comments_status":if pending{contract()["controller_queue"]["awaiting_text"].clone()}else{Value::Null},"report_id":id,"report_reference":reference,"item_number":item,"revision":r["revision"].as_u64().ok_or("Unexpected owner revision")?,
  "kind":enum_or_null(&r["kind"],"kinds")?,"severity":enum_or_null(&r["severity"],"severities")?,"impact":enum_or_null(&r["impact"],"impacts")?,
  "expectation":enum_or_null(&r["expectation"],"expectations")?,"outcome":enum_or_null(&r["outcome"],"outcomes")?,"lifecycle":lifecycle,"desk":desk,
  "reach_count":r["reach_count"].as_u64().filter(|n|*n<=1_000_000).ok_or("Unexpected owner reach")?,
  "automatic_fix_eligible":r["automatic_fix_eligible"].as_bool().ok_or("Unexpected owner eligibility")?,
  "automation_enabled":r["automation_enabled"].as_bool().ok_or("Unexpected owner automation flag")?,
  "created_at":r["created_at"].as_u64().ok_or("Unexpected owner time")?}))
}
fn owner_list(v:&Value)->Result<Value,String>{
 let rows=v["reports"].as_array().filter(|a|a.len()<=100).ok_or("Unexpected owner inbox")?;
 let cursor=match &v["next_cursor"]{Value::Null=>Value::Null,c=>json!(c.as_str().filter(|s|!s.is_empty()&&s.len()<=1024&&s.bytes().all(|b|b.is_ascii_alphanumeric()||b"+/=".contains(&b))).ok_or("Unexpected owner cursor")?)};
 Ok(json!({"reports":rows.iter().map(owner_row).collect::<Result<Vec<_>,_>>()?,"next_cursor":cursor,"high_water":v["high_water"].as_u64().ok_or("Unexpected owner high water")?}))
}
fn query(value:&str)->String{value.bytes().map(|b|if b.is_ascii_alphanumeric(){(b as char).to_string()}else{format!("%{b:02X}")}).collect()}
// The event cursor finds changes to old records. It is saved only after a page is processed; a fresh
// install records a baseline instead of flagging every historic event.
async fn owner_changes(origin:&str,dir:&Path,key:&str,cancel:&AtomicBool)->Result<Value,String>{
 let file=dir.join("owner-events.json");
 let saved=read(&file,256).ok().and_then(|b|serde_json::from_slice::<Value>(&b).ok()).and_then(|v|v["next_event_cursor"].as_u64());
 let mut cursor=saved.unwrap_or(0);let mut changed=std::collections::BTreeMap::<String,u64>::new();let mut complete=false;
 for _ in 0..20{
  let page=exchange(origin,reqwest::Method::GET,&format!("/v2/admin/reports?events_after={cursor}&limit=100"),key,None,&[],65535,cancel).await?;
  let events=page["events"].as_array().filter(|a|a.len()<=100).ok_or("Unexpected owner events")?;
  for event in events{
   let id=event["report_id"].as_str().filter(|s|valid_id(s)).ok_or("Unexpected owner event")?;
   let revision=event["revision"].as_u64().ok_or("Unexpected owner event")?;
   if saved.is_some(){let entry=changed.entry(id.to_string()).or_insert(0);*entry=(*entry).max(revision);}
  }
  let next=page["next_event_cursor"].as_u64().filter(|n|*n>=cursor).ok_or("Unexpected owner event cursor")?;
  cursor=next;std::fs::write(&file,serde_json::to_vec(&json!({"next_event_cursor":cursor})).unwrap()).map_err(|_|"Owner event cursor could not be saved")?;
  if page["has_more"]!=true{complete=true;break}
 }
 Ok(json!({"baseline":saved.is_none(),"complete":complete,"changed":changed.into_iter().map(|(id,revision)|json!({"report_id":id,"revision":revision})).collect::<Vec<_>>()}))
}
#[tauri::command]
pub async fn support_inbox(app:tauri::AppHandle,state:tauri::State<'_,Support>,action:String,id:Option<String>,cursor:Option<String>)->Result<Value,String>{
 let _guard=begin(&state)?;let dir=root(&app)?;
 let key=protect(&read(&dir.join("owner-key.dpapi"),4096)?,true)?;
 let key=std::str::from_utf8(&key).map_err(|_|"Owner key unavailable")?;
 let report=||->Result<String,String>{let id=id.clone().ok_or("Choose a report")?;if !valid_id(&id){return Err("Invalid report ID".into())}Ok(id)};
 match action.as_str(){
  "list"=>owner_list(&exchange(ORIGIN,reqwest::Method::GET,"/v2/admin/reports?limit=50",key,None,&[],65535,&state.cancel).await?),
  "search"=>{
   let raw=id.as_deref().filter(|s|s.len()<=64).ok_or("Enter a report reference or item number")?;
   let reference=crate::support_contract::parse_report_reference(raw).and_then(crate::support_contract::format_report_reference)
    .or_else(||crate::support_contract::parse_item_number(raw).and_then(crate::support_contract::format_item_number)).ok_or("Enter a reference like R-4F7K-2M9Q or XZ-1042")?;
   owner_list(&exchange(ORIGIN,reqwest::Method::GET,&format!("/v2/admin/reports?limit=50&q={}",query(&reference)),key,None,&[],65535,&state.cancel).await?)
  },
  "more"=>{
   let cursor=cursor.filter(|c|!c.is_empty()&&c.len()<=1024).ok_or("Invalid inbox cursor")?;
   owner_list(&exchange(ORIGIN,reqwest::Method::GET,&format!("/v2/admin/reports?limit=50&cursor={}",query(&cursor)),key,None,&[],65535,&state.cancel).await?)
  },
  "completion"=>{
   let id=report()?;
   let value=exchange(ORIGIN,reqwest::Method::GET,&format!("/v2/admin/reports/{id}/completion"),key,None,&[],32768,&state.cancel).await?;
   if value["report_id"]!=id{return Err("Unexpected completion identity".into())}
   let c=&value["completion"];if c.is_null(){return Ok(json!({"report_id":id,"completion":null}))}
   let status=c["comments_state"].as_str().filter(|s|contract()["pending_lifecycles"].as_array().is_some_and(|a|a.iter().any(|v|v==*s))).ok_or("Unexpected comments state")?;
   let description=if c["description"].is_null(){Value::Null}else{json!(c["description"].as_str().filter(|s|s.len()<=4096).ok_or("Invalid completion text")?)};
   Ok(json!({"report_id":id,"completion":{"comments_state":status,"description":description,"completed_at":c["completed_at"].as_u64()}}))
  },
  "changes"=>owner_changes(ORIGIN,&dir,key,&state.cancel).await,
  "read"=>{
   let id=report()?;state.inbox.lock().map_err(|_|"Inbox busy")?.clear();
   let bytes=tokio::time::timeout(Duration::from_secs(60),read_owner(ORIGIN,&id,key,&state.cancel)).await.map_err(|_|"Owner report read timed out; retry Read")??;
   // Verified bytes are kept; the display copy is indented for reading only.
   let text=serde_json::to_string_pretty(&serde_json::from_slice::<Value>(&bytes).map_err(|_|"Invalid verified owner report")?).map_err(|_|"Invalid verified owner report")?;
   let pages=(text.chars().count()+2999)/3000;*state.inbox.lock().map_err(|_|"Inbox busy")?=text.into_bytes();Ok(json!({"pages":pages}))
  },
  "delete"=>{let id=report()?;exchange(ORIGIN,reqwest::Method::DELETE,&format!("/v1/admin/reports/{id}"),key,None,&[],65535,&state.cancel).await},
  _=>Err("Invalid inbox action".into())
 }
}
#[tauri::command]
pub fn support_inbox_page(state:tauri::State<Support>,page:usize)->Result<Value,String>{
 let b=state.inbox.lock().map_err(|_|"Inbox busy")?;if page>CAP*2/3000+1{return Err("Invalid page".into())}
 let text=std::str::from_utf8(&b).map_err(|_|"Invalid inbox text")?;let pages:Vec<String>=text.chars().collect::<Vec<_>>().chunks(3000).map(|c|c.iter().collect()).collect();
 pages.get(page).map(|t|json!({"text":t,"pages":pages.len()})).ok_or("Invalid page".into())
}
#[cfg(test)]mod tests{use super::*;#[test]fn ids_and_hash(){assert!(valid_id(&"a".repeat(32)));assert!(!valid_id("../owner-key.dpapi"));assert!(!valid_id(&"A".repeat(32)));assert_eq!(hash(b"abc"),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");}#[test]fn duplicate_job(){let s=Support::default();let g=begin(&s).unwrap();assert!(begin(&s).is_err());drop(g);assert!(begin(&s).is_ok());}}

#[cfg(test)]pub(crate) mod focused_tests{
 use super::*;
 pub(crate) fn report_body(id:&str)->String{serde_json::to_string(&json!({"schema":"xz-support-report/2","report_id":id,"kind_hint":"BUG","description":"Decks 3 and 4 went silent.","reporter_hints":{"topic":"NOT_WORKING"},"private":{"items":[]}})).unwrap()}
 pub(crate) fn fixture()->(Vec<u8>,Vec<u8>,String){let body=report_body(&"a".repeat(32));let sha=hash(body.as_bytes());(serde_json::to_vec(&json!({"body":body,"sha256":sha,"key":"b".repeat(64)})).unwrap(),serde_json::to_vec(&json!({"schema":"xz-support-consent/1","report_id":"a".repeat(32),"sha256":sha,"bytes":body.len(),"consent":"private-report/v2"})).unwrap(),sha)}
 #[test]fn send_needs_consent_to_the_exact_bytes(){
  let (b,c,sha)=fixture();let id="a".repeat(32);
  let d=checked(&b,&c,&id,&sha).unwrap();assert_eq!(hash(&d.body),sha);assert_eq!(d.key,"b".repeat(64));
  // No page-visit marker is needed. The consent record must match ID, digest, size and the v2 consent text.
  assert!(checked(&b,&c,&id,&"0".repeat(64)).is_err());assert!(checked(&b,&c,&"c".repeat(32),&sha).is_err());
  for (field,value) in [("sha256",json!("0".repeat(64))),("bytes",json!(1)),("consent",json!("public-summary-private-logs/v1")),("report_id",json!("c".repeat(32))),("schema",json!("xz-support-review/1"))]{
   let mut changed:Value=serde_json::from_slice(&c).unwrap();changed[field]=value;assert!(checked(&b,&serde_json::to_vec(&changed).unwrap(),&id,&sha).is_err(),"{field}");
  }
  assert!(checked(&b,br#"{"sha256":"x","complete":true}"#,&id,&sha).is_err());
  // A v1 public-summary draft can no longer be sent.
  let v1=serde_json::to_string(&json!({"schema":"xz-support-report/1","report_id":id,"public_issue":{"title":"t","description":"d"}})).unwrap();let v1sha=hash(v1.as_bytes());
  let draft=serde_json::to_vec(&json!({"body":v1,"sha256":v1sha,"key":"b".repeat(64)})).unwrap();
  let consent=serde_json::to_vec(&json!({"schema":"xz-support-consent/1","report_id":id,"sha256":v1sha,"bytes":v1.len(),"consent":"private-report/v2"})).unwrap();
  assert!(checked(&draft,&consent,&id,&v1sha).err().unwrap().contains("identity"));
 }
 #[test]fn contract_values_come_from_the_generated_module(){assert_eq!(consent_text(),"private-report/v2");assert_eq!(CAP,524288);assert!(listed("kinds","DEGRADATION"));assert!(!listed("kinds","bug"));}
 #[tokio::test]async fn cancellation_is_unknown(){let cancel=AtomicBool::new(true);let future=async{tokio::time::sleep(Duration::from_secs(3)).await;Ok::<_,reqwest::Error>(())};assert!(cancellable(future,&cancel).await.unwrap_err().contains("OutcomeUnknown"));}
 pub(crate) fn server(responses:Vec<String>)->(String,std::thread::JoinHandle<Vec<String>>){
  use std::io::{Read,Write};let listener=std::net::TcpListener::bind("127.0.0.1:0").unwrap();let origin=format!("http://{}",listener.local_addr().unwrap());
  let t=std::thread::spawn(move||{let mut seen=Vec::new();for response in responses{let(mut stream,_)=listener.accept().unwrap();stream.set_read_timeout(Some(Duration::from_millis(500))).unwrap();let mut all=Vec::new();let mut buf=[0u8;65536];
   loop{match stream.read(&mut buf){Ok(0)=>break,Ok(n)=>{all.extend_from_slice(&buf[..n]);let text=String::from_utf8_lossy(&all).to_string();if let Some(head)=text.find("\r\n\r\n"){let length=text[..head].to_lowercase().lines().find_map(|l|l.strip_prefix("content-length: ").map(|n|n.trim().parse::<usize>().unwrap_or(0))).unwrap_or(0);if all.len()>=head+4+length{break}}},Err(_)=>break}}
   stream.write_all(response.as_bytes()).unwrap();seen.push(String::from_utf8_lossy(&all).to_string())}seen});(origin,t)
 }
 pub(crate) fn ok(body:&Value)->String{let b=serde_json::to_string(body).unwrap();format!("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{b}",b.len())}
 pub(crate) fn receipt(id:&str,sha:&str,revision:u64,lifecycle:&str,key:&str)->Value{json!({"schema":"xz-support-receipt/2","report_id":id,"report_reference":"R-4F7K-2M9Q","item_number":null,"payload_sha256":sha,"accepted_at":"2026-10-27T10:00:00.000Z","expires_at":"2026-11-10T10:00:00.000Z","private_logs":"stored","publication_mode":"none","tracking":crate::support_tracking::tests::status(id,revision,lifecycle,key)})}
 #[tokio::test]async fn actual_loopback_send_uses_v2_headers_and_exact_bytes(){
  let (b,c,sha)=fixture();let id="a".repeat(32);let d=checked(&b,&c,&id,&sha).unwrap();
  let (origin,t)=server(vec![ok(&receipt(&id,&sha,1,"received","received"))]);
  let result=submit(&origin,&id,&sha,&d,&AtomicBool::new(false)).await.unwrap();assert_eq!(result["tracking"]["lifecycle"],"received");assert_eq!(result["private_logs"],"stored");
  let request=t.join().unwrap().remove(0);let lower=request.to_lowercase();
  assert!(lower.starts_with("post /v2/reports "));assert!(lower.contains(&format!("authorization: bearer {}","b".repeat(64))));assert!(lower.contains("x-report-consent: private-report/v2"));
  assert!(lower.contains(&format!("x-report-sha256: {sha}")));assert!(lower.contains(&format!("x-report-id: {id}")));
  assert!(request.ends_with(&String::from_utf8(d.body.clone()).unwrap()),"submitted bytes equal the consented body");
  // A receipt for other bytes, or one that does not confirm private storage, is not acceptance.
  for (field,value) in [("report_reference",json!("R-4F7K-2M9R")),("item_number",json!("XZ-1042")),("payload_sha256",json!("0".repeat(64))),("private_logs",json!("gone")),("schema",json!("xz-support-receipt/1")),("publication_mode",json!("public"))]{
   let mut bad=receipt(&id,&sha,1,"received","received");bad[field]=value;assert!(validate_receipt(&bad,&id,&sha,true).is_err(),"{field}");
  }
 }
 #[tokio::test]async fn actual_loopback_refusals_redirects_size_and_encoding(){
  let(origin,t)=server(vec!["HTTP/1.1 302 Found\r\nLocation: https://example.invalid/secret\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".into()]);assert!(exchange(&origin,reqwest::Method::GET,"/receipt","fixture",None,&[],65536,&AtomicBool::new(false)).await.unwrap_err().contains("302"));t.join().unwrap();
  let refusal=r#"{"error":"conflict","detail":"C:/Users/secret"}"#;
  let(origin,t)=server(vec![format!("HTTP/1.1 409 Conflict\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{refusal}",refusal.len())]);
  let error=exchange(&origin,reqwest::Method::GET,"/receipt","fixture",None,&[],65536,&AtomicBool::new(false)).await.unwrap_err();assert!(error.contains("(conflict)"));assert!(!error.contains("secret"));t.join().unwrap();
  for response in ["HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 999999\r\nConnection: close\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"]{
   let(origin,t)=server(vec![response.into()]);assert!(exchange(&origin,reqwest::Method::GET,"/receipt","fixture",None,&[],65536,&AtomicBool::new(false)).await.is_err());t.join().unwrap();
  }
 }
 #[test]fn owner_rows_keep_only_validated_taxonomy(){
  let row=json!({"report_id":"a".repeat(32),"report_reference":"R-4F7K-2M9Q","item_number":"XZ-1042","revision":3,"kind":"REGRESSION","severity":"LIVE_SET_STOPPER","impact":"RESTORES_EXPECTED","expectation":"STOCK_XZ",
   "desk":{"expectation":"STOCK_XZ","impact":"RESTORES_EXPECTED","severity":"LIVE_SET_STOPPER","affected_features":["sync_lamp","master_handoff"],"in_use_since":"0.5.0-preview.21",
    "reach":{"versions":["0.5.0-preview.22.6"],"interfaces":["MODERN"],"decks":[3,4]},"proposed_setting":null},
   "reach_count":4,"lifecycle":"triaged","outcome":"CLASSIFIED","automatic_fix_eligible":true,"automation_enabled":false,"created_at":1761559200000u64,"private_note":"never shown"});
  let clean=owner_row(&row).unwrap();assert_eq!(clean["desk"]["affected_features"],json!(["sync_lamp","master_handoff"]));assert!(clean.get("private_note").is_none());
  for (path,value) in [("kind",json!("bug")),("lifecycle",json!("released_now")),("reach_count",json!(-1))]{let mut bad=row.clone();bad[path]=value;assert!(owner_row(&bad).is_err(),"{path}");}
  let mut bad=row.clone();bad["desk"]["affected_features"]=json!(["<img>"]);assert!(owner_row(&bad).is_err());
  let mut bad=row.clone();bad["desk"]["reach"]["decks"]=json!([5]);assert!(owner_row(&bad).is_err());
  let mut bad=row.clone();bad["desk"]["in_use_since"]=json!("v1");assert!(owner_row(&bad).is_err());
  for good in ["unknown","0.5.0","0.5.0-preview.22.6","1.2.3+build.7","1.2.3-rc.1+b2"]{assert!(version(good),"{good}");}
  for bad in ["","1.2","1.2.3-","1.2.3-RC","x.1.2","1.2.3+"]{assert!(!version(bad),"{bad}");}
  assert!(owner_list(&json!({"reports":[row],"next_cursor":"bad cursor","high_water":1})).is_err());
  assert_eq!(query("ab+/="),"ab%2B%2F%3D");
 }
 #[tokio::test]async fn actual_owner_event_cursor_baselines_then_reports_changes(){
  let dir=crate::support_store::tests::temp("owner-events");
  let (origin,t)=server(vec![ok(&json!({"events":[{"seq":7,"report_id":"a".repeat(32),"revision":2}],"next_event_cursor":7,"has_more":false})),
   ok(&json!({"events":[{"seq":8,"report_id":"b".repeat(32),"revision":1},{"seq":9,"report_id":"b".repeat(32),"revision":2}],"next_event_cursor":9,"has_more":true})),
   ok(&json!({"events":[],"next_event_cursor":9,"has_more":false}))]);
  let first=owner_changes(&origin,&dir,"synthetic-owner",&AtomicBool::new(false)).await.unwrap();assert_eq!(first["baseline"],true);assert_eq!(first["changed"],json!([]));
  let second=owner_changes(&origin,&dir,"synthetic-owner",&AtomicBool::new(false)).await.unwrap();assert_eq!(second["changed"],json!([{"report_id":"b".repeat(32),"revision":2}]));
  let requests=t.join().unwrap();assert!(requests[0].contains("events_after=0&limit=100"));assert!(requests[1].contains("events_after=7&"));assert!(requests[2].contains("events_after=9&"));
 }
}

#[cfg(test)]mod owner_page_tests{
 use super::*;
 fn base64(bytes:&[u8])->String{
  const ALPHABET:&[u8]=b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  let mut out=String::new();for c in bytes.chunks(3){let a=c[0];let b=*c.get(1).unwrap_or(&0);let d=*c.get(2).unwrap_or(&0);out.push(ALPHABET[(a>>2)as usize]as char);out.push(ALPHABET[(((a&3)<<4)|(b>>4))as usize]as char);out.push(if c.len()>1{ALPHABET[(((b&15)<<2)|(d>>6))as usize]as char}else{'='});out.push(if c.len()>2{ALPHABET[(d&63)as usize]as char}else{'='});}out
 }
 fn payload()->Vec<u8>{let mut b=serde_json::to_vec(&json!({"schema":"xz-support-report/2","report_id":"a".repeat(32),"private":{"items":[{"content":"???".repeat(60000)}]}})).unwrap();b.resize(CAP,b' ');b}
 fn page(bytes:&[u8],offset:usize)->Value{let end=(offset+OWNER_PAGE).min(bytes.len());json!({"schema":"xz-support-owner-page/1","report_id":"a".repeat(32),"payload_sha256":hash(bytes),"total_bytes":bytes.len(),"offset":offset,"encoding":"base64","data_base64":base64(&bytes[offset..end]),"next_offset":if end==bytes.len(){None}else{Some(end)},"receipt":{"schema":"xz-support-receipt/2","report_id":"a".repeat(32),"payload_sha256":hash(bytes)}})}
 #[test]fn full_cap_multibyte_report_keeps_exact_bytes(){let bytes=payload();let mut report=OwnerReport::default();for offset in (0..bytes.len()).step_by(OWNER_PAGE){let p=page(&bytes,offset);assert!(serde_json::to_vec(&p).unwrap().len()<65536);let done=report.push(&p,&"a".repeat(32)).unwrap();assert_eq!(done,offset+OWNER_PAGE>=bytes.len());}assert!(report.complete);assert_eq!(report.bytes,bytes);}
 #[test]fn rejects_drift_gaps_repeated_pages_and_early_end(){
  let bytes=payload();for key in ["report_id","payload_sha256","total_bytes","offset","next_offset","receipt_id","receipt_hash","receipt_schema"]{
   let mut report=OwnerReport::default();assert!(!report.push(&page(&bytes,0),&"a".repeat(32)).unwrap());let mut p=page(&bytes,OWNER_PAGE);
   match key{"receipt_id"=>p["receipt"]["report_id"]=json!("b".repeat(32)),"receipt_hash"=>p["receipt"]["payload_sha256"]=json!("b".repeat(64)),"receipt_schema"=>p["receipt"]["schema"]=json!("xz-support-status/2"),
    _=>p[key]=match key{"report_id"=>json!("b".repeat(32)),"payload_sha256"=>json!("b".repeat(64)),"total_bytes"=>json!(CAP-1),"offset"=>json!(0),_=>Value::Null}}
   assert!(report.push(&p,&"a".repeat(32)).is_err(),"{key}");
  }
  let mut report=OwnerReport::default();assert!(report.push(&page(&bytes,OWNER_PAGE),&"a".repeat(32)).is_err());
 }
 #[test]fn rejects_invalid_base64_bounds_and_final_hash(){
  for text in ["","AA=Z","AB==","AAB=","AAA","!!!!","AAAA===="]{assert!(decode_page(text).is_err(),"{text}");}
  assert_eq!(decode_page("AA==").unwrap(),vec![0]);assert_eq!(decode_page("AAA=").unwrap(),vec![0,0]);assert!(decode_page(&"A".repeat(32772)).is_err());
  let b=br#"{"schema":"xz-support-report/1","report_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"#;let mut p=page(b,0);p["payload_sha256"]=json!("0".repeat(64));p["receipt"]["payload_sha256"]=json!("0".repeat(64));assert!(OwnerReport::default().push(&p,&"a".repeat(32)).unwrap_err().contains("integrity"));
  let mut p=page(b,0);p["total_bytes"]=json!(CAP+1);assert!(OwnerReport::default().push(&p,&"a".repeat(32)).is_err());
  assert!(OwnerReport::default().push(&page(b,0),&"a".repeat(32)).unwrap(),"a verified v1 report still reads");
 }
 #[tokio::test]async fn actual_owner_http_pages_authenticate_every_request(){
  use std::io::{Read,Write};let bytes=payload();let expected=bytes.clone();let listener=std::net::TcpListener::bind("127.0.0.1:0").unwrap();let origin=format!("http://{}",listener.local_addr().unwrap());
  let server=std::thread::spawn(move||{for offset in (0..bytes.len()).step_by(OWNER_PAGE){let(mut socket,_)=listener.accept().unwrap();socket.set_read_timeout(Some(Duration::from_secs(3))).unwrap();let mut input=[0u8;4096];let n=socket.read(&mut input).unwrap();let input=String::from_utf8_lossy(&input[..n]).to_lowercase();assert!(input.contains(&format!("get /v2/admin/reports/{}?offset={offset} ","a".repeat(32))));assert!(input.contains("authorization: bearer synthetic-owner-test-key"));let body=serde_json::to_vec(&page(&bytes,offset)).unwrap();let header=format!("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",body.len());socket.write_all(header.as_bytes()).unwrap();socket.write_all(&body).unwrap();}});
  let result=read_owner(&origin,&"a".repeat(32),"synthetic-owner-test-key",&AtomicBool::new(false)).await.unwrap();server.join().unwrap();assert_eq!(result,expected);
 }
 #[tokio::test]async fn cancelled_owner_read_never_connects(){assert!(read_owner("http://127.0.0.1:1",&"a".repeat(32),"synthetic",&AtomicBool::new(true)).await.unwrap_err().contains("cancelled"));}
}
