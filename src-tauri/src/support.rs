use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{path::PathBuf, sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}}, time::Duration};
use tauri::Manager;
use tauri_plugin_dialog::DialogExt;
const ORIGIN:&str="https://xz-mods-support.will-026.workers.dev";
const CAP:usize=524288;
#[derive(Default)]
pub struct Support { busy:Arc<AtomicBool>, cancel:Arc<AtomicBool>, inbox:Mutex<Vec<u8>> }
fn valid_id(id:&str)->bool{id.len()==32&&id.bytes().all(|c|c.is_ascii_digit()||(b'a'..=b'f').contains(&c))}
fn hash(b:&[u8])->String{format!("{:x}",Sha256::digest(b))}
fn root(app:&tauri::AppHandle)->Result<PathBuf,String>{
 let p=app.path().app_local_data_dir().map_err(|_|"App data unavailable")?.join("support-reports");
 std::fs::create_dir_all(&p).map_err(|_|"Private report folder unavailable")?;
 for parent in p.ancestors(){if let Ok(m)=std::fs::symlink_metadata(parent){
  #[cfg(windows)] {use std::os::windows::fs::MetadataExt;if m.file_attributes()&0x400!=0{return Err("Linked report path refused".into())}}
  if m.file_type().is_symlink(){return Err("Linked report path refused".into())}
 }} Ok(p)
}
fn read(path:&std::path::Path,cap:usize)->Result<Vec<u8>,String>{
 use std::io::Read;
 let m=std::fs::symlink_metadata(path).map_err(|_|"Report file unavailable")?;
 #[cfg(windows)] {use std::os::windows::fs::MetadataExt;if m.file_attributes()&0x400!=0{return Err("Linked report file refused".into())}}
 if !m.is_file()||m.file_type().is_symlink()||m.len()>cap as u64{return Err("Invalid report file size or type".into())}
 let mut b=Vec::new();std::fs::File::open(path).map_err(|_|"Report unavailable")?.take((cap+1)as u64).read_to_end(&mut b).map_err(|_|"Report unavailable")?;
 if b.len()>cap{return Err("Report too large".into())}Ok(b)
}
struct Draft{body:Vec<u8>,key:String,public:Value}
fn draft(app:&tauri::AppHandle,id:&str,sha:&str)->Result<Draft,String>{
 if !valid_id(id){return Err("Invalid report ID".into())}
 let p=root(app)?;
 checked(&read(&p.join(format!("{id}.draft")),CAP*2)?, &read(&p.join(format!("{id}.review")),4096)?,id,sha)
}
fn checked(bytes:&[u8],review_bytes:&[u8],id:&str,sha:&str)->Result<Draft,String>{
 let v:Value=serde_json::from_slice(bytes).map_err(|_|"Invalid draft")?;
 let body=v["body"].as_str().ok_or("Invalid draft")?.as_bytes().to_vec();
 if body.len()>CAP||hash(&body)!=sha||v["sha256"]!=sha{return Err("Stale preview or changed draft".into())}
 let review:Value=serde_json::from_slice(review_bytes).map_err(|_|"Review every preview page")?;
 if review["sha256"]!=sha||review["complete"]!=true{return Err("Review every preview page".into())}
 let key=v["key"].as_str().ok_or("Receipt key missing")?.to_string();
 if key.len()!=64||!key.bytes().all(|c|c.is_ascii_digit()||(b'a'..=b'f').contains(&c)){return Err("Invalid receipt key".into())}
 let public:Value=serde_json::from_slice(&body).map_err(|_|"Invalid report")?;
 if public["report_id"]!=id||public["schema"]!="xz-support-report/1"{return Err("Report identity mismatch".into())}
 Ok(Draft{body,key,public})
}
struct Busy(Arc<AtomicBool>);impl Drop for Busy{fn drop(&mut self){self.0.store(false,Ordering::Release);}}
fn begin(state:&Support)->Result<Busy,String>{
 state.busy.compare_exchange(false,true,Ordering::AcqRel,Ordering::Acquire).map_err(|_|"A support request is already running")?;
 state.cancel.store(false,Ordering::Release);Ok(Busy(state.busy.clone()))
}
async fn cancellable<T>(future:impl std::future::Future<Output=Result<T,reqwest::Error>>,cancel:&AtomicBool)->Result<T,String>{
 tokio::pin!(future);
 loop{tokio::select!{r=&mut future=>return r.map_err(|_|"OutcomeUnknown: connection failed. Check receipt before retrying.".into()),_ = tokio::time::sleep(Duration::from_millis(50))=>if cancel.load(Ordering::Acquire){return Err("OutcomeUnknown: cancelled; the server may have accepted it. Check receipt.".into())}}}
}
async fn request(method:reqwest::Method,path:&str,key:&str,body:Option<Vec<u8>>,headers:Option<(&str,&str)>,cap:usize,cancel:&AtomicBool)->Result<Value,String>{
 exchange(ORIGIN,method,path,key,body,headers,cap,cancel).await
}
async fn exchange(origin:&str,method:reqwest::Method,path:&str,key:&str,body:Option<Vec<u8>>,headers:Option<(&str,&str)>,cap:usize,cancel:&AtomicBool)->Result<Value,String>{
 let _=rustls::crypto::ring::default_provider().install_default();
 let client=reqwest::Client::builder().user_agent("xz-support/1").redirect(reqwest::redirect::Policy::none()).connect_timeout(Duration::from_secs(8)).timeout(Duration::from_secs(25)).https_only(!cfg!(test)).build().map_err(|_|"Support connection unavailable")?;
 let mut q=client.request(method,format!("{origin}{path}")).bearer_auth(key).header("Accept","application/json");
 if let Some(b)=body{q=q.header("Content-Type","application/json").header("Content-Length",b.len()).body(b)}
 if let Some((id,sha))=headers{q=q.header("X-Report-ID",id).header("X-Report-SHA256",sha).header("X-Report-Consent","public-summary-private-logs/v1")}
 let mut r=cancellable(q.send(),cancel).await?;
 if !r.status().is_success(){return Err(format!("Support service returned HTTP {}. Retry setup or export offline.",r.status().as_u16()))}
 if !r.headers().get("content-type").and_then(|v|v.to_str().ok()).unwrap_or("").starts_with("application/json")||r.headers().contains_key("content-encoding"){return Err("Unsupported support response".into())}
 if r.content_length().is_some_and(|n|n>cap as u64){return Err("Support response too large".into())}
 let mut bytes=Vec::new();while let Some(chunk)=cancellable(r.chunk(),cancel).await?{if bytes.len()+chunk.len()>cap{return Err("Support response too large".into())}bytes.extend_from_slice(&chunk)}
 serde_json::from_slice(&bytes).map_err(|_|"Invalid support response".into())
}
#[tauri::command]
pub fn support_cancel(state:tauri::State<Support>){state.cancel.store(true,Ordering::Release);}
fn open(url:&str)->Result<(),String>{
 #[cfg(windows)] {use std::os::windows::process::CommandExt;std::process::Command::new("rundll32.exe").args(["url.dll,FileProtocolHandler",url]).creation_flags(0x08000000).spawn().map_err(|_|"Browser unavailable")?;Ok(())}
 #[cfg(not(windows))] {let _=url;Err("Open the reviewed GitHub draft on Windows".into())}
}
#[tauri::command]
pub async fn support_action(app:tauri::AppHandle,state:tauri::State<'_,Support>,action:String,id:String,sha256:String,consent:bool,issue_url:Option<String>)->Result<Value,String>{
 let _guard=begin(&state)?;let d=draft(&app,&id,&sha256)?;
 match action.as_str(){
 "export"=>{
  let selected=app.dialog().file().set_file_name("Private sanitized diagnostics.json").add_filter("Private sanitized JSON",&["json"]).blocking_save_file();
  if let Some(path)=selected{let path=path.into_path().map_err(|_|"Invalid export path")?;std::fs::write(path,&d.body).map_err(|_|"Export failed")?;Ok(json!({"exported":true,"privacy":"Private sanitized diagnostics"}))}else{Ok(json!({"exported":false}))}
 },
 "github"=>{
  let mut url=reqwest::Url::parse("https://github.com/OpticMystic/XZ-Mods/issues/new").unwrap();
  let title=d.public["public_issue"]["title"].as_str().ok_or("Public title missing")?;
  let description=d.public["public_issue"]["description"].as_str().ok_or("Public description missing")?;
  url.query_pairs_mut().append_pair("title",title).append_pair("body",&format!("{description}\n\nXZ support report: {id}"));
  open(url.as_str())?;Ok(json!({"public_issue":"pending"}))
 },
 "send"|"receipt"|"issue"=>{
  if action=="send"&&!consent{return Err("Explicit consent to this reviewed SHA256 is required".into())}
  let path=format!("/v1/reports/{id}");
  let result=if action=="send"{
   std::fs::write(root(&app)?.join(format!("{id}.attempt")),sha256.as_bytes()).map_err(|_|"Cannot retain send attempt")?;
   request(reqwest::Method::POST,"/v1/reports",&d.key,Some(d.body),Some((&id,&sha256)),65536,&state.cancel).await?
  }else if action=="receipt"{request(reqwest::Method::GET,&format!("{path}/receipt"),&d.key,None,None,65536,&state.cancel).await?}
  else{
   let url=issue_url.ok_or("Paste a GitHub issue URL")?;
   let prefix="https://github.com/OpticMystic/XZ-Mods/issues/";let n=url.strip_prefix(prefix).ok_or("Use the fixed repository issue URL")?;
   if n.is_empty()||n.starts_with('0')||n.len()>20||!n.bytes().all(|c|c.is_ascii_digit()){return Err("Invalid issue number".into())}
   request(reqwest::Method::POST,&format!("{path}/issue"),&d.key,Some(serde_json::to_vec(&json!({"issue_url":url})).unwrap()),None,65536,&state.cancel).await?
  };
  if action!="issue"&&(result["schema"]!="xz-support-receipt/1"||result["report_id"]!=id||result["payload_sha256"]!=sha256||result["private_logs"]!="stored"||!matches!(result["public_issue"].as_str(),Some("pending"|"user_provided"|"verified"))){return Err("Unexpected receipt; private storage not confirmed".into())}
  // Never forward arbitrary server fields or authentication data to the renderer.
  let safe=json!({"private_logs":result["private_logs"],"public_issue":result["public_issue"],"issue_url":result["issue_url"],"expires_at":result["expires_at"]});
  std::fs::write(root(&app)?.join(format!("{id}.receipt")),serde_json::to_vec(&safe).unwrap()).map_err(|_|"Receipt received but local save failed")?;Ok(safe)
 },_=>Err("Unknown support action".into())}
}
// Owner credentials are protected by Windows DPAPI for the current OS user.
#[cfg(windows)]
fn protect(input:&[u8],decrypt:bool)->Result<Vec<u8>,String>{
 #[repr(C)]struct Blob{len:u32,data:*mut u8}
 #[link(name="crypt32")]extern "system"{fn CryptProtectData(i:*const Blob,d:*const u16,e:*const Blob,r:*mut u8,p:*mut u8,f:u32,o:*mut Blob)->i32;fn CryptUnprotectData(i:*const Blob,d:*mut *mut u16,e:*const Blob,r:*mut u8,p:*mut u8,f:u32,o:*mut Blob)->i32;}
 #[link(name="kernel32")]extern "system"{fn LocalFree(p:*mut u8)->*mut u8;}
 let i=Blob{len:input.len()as u32,data:input.as_ptr()as *mut u8};let mut o=Blob{len:0,data:std::ptr::null_mut()};
 let ok=unsafe{if decrypt{CryptUnprotectData(&i,std::ptr::null_mut(),std::ptr::null(),std::ptr::null_mut(),std::ptr::null_mut(),1,&mut o)}else{CryptProtectData(&i,std::ptr::null(),std::ptr::null(),std::ptr::null_mut(),std::ptr::null_mut(),1,&mut o)}};
 if ok==0{return Err("Windows private key storage unavailable".into())}let result=unsafe{std::slice::from_raw_parts(o.data,o.len as usize).to_vec()};unsafe{LocalFree(o.data);}Ok(result)
}
#[cfg(not(windows))]fn protect(_: &[u8],_:bool)->Result<Vec<u8>,String>{Err("Owner connection requires Windows private storage".into())}
#[tauri::command]
pub fn support_connect(app:tauri::AppHandle,key:String)->Result<Value,String>{
 if key.len()<32||key.len()>256||!key.bytes().all(|c|c.is_ascii_graphic()){return Err("Invalid report-only owner key".into())}
 let bytes=protect(key.as_bytes(),false)?;std::fs::write(root(&app)?.join("owner-key.dpapi"),bytes).map_err(|_|"Owner connection could not be saved")?;Ok(json!({"connected":true}))
}
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
  if sha.len()!=64||!sha.bytes().all(|c|c.is_ascii_digit()||(b'a'..=b'f').contains(&c)){return Err("Invalid owner report hash".into())}
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
   if report["schema"]!="xz-support-report/1"||report["report_id"]!=id{return Err("Verified owner report identity mismatch".into())}
   self.complete=true;
  }
  Ok(finished)
 }
}
async fn read_owner(origin:&str,id:&str,key:&str,cancel:&AtomicBool)->Result<Vec<u8>,String>{
 let mut report=OwnerReport::default();
 for _ in 0..CAP.div_ceil(OWNER_PAGE){
  if cancel.load(Ordering::Acquire){return Err("Owner report read cancelled".into())}
  let path=format!("/v1/admin/reports/{id}?offset={}",report.bytes.len());
  let page=exchange(origin,reqwest::Method::GET,&path,key,None,None,65535,cancel).await?;
  if report.push(&page,id)?{return Ok(report.bytes)}
 }
 Err("Owner report exceeded the bounded page count".into())
}
#[tauri::command]
pub async fn support_inbox(app:tauri::AppHandle,state:tauri::State<'_,Support>,action:String,id:Option<String>)->Result<Value,String>{
 let _guard=begin(&state)?;
 let key=protect(&read(&root(&app)?.join("owner-key.dpapi"),4096)?,true)?;
 let key=std::str::from_utf8(&key).map_err(|_|"Owner key unavailable")?;
 let path=if action=="list"{"/v1/admin/reports".to_string()}else{let id=id.ok_or("Choose a report")?;if !valid_id(&id){return Err("Invalid report ID".into())}format!("/v1/admin/reports/{id}")};
 let method=match action.as_str(){"list"|"read"=>reqwest::Method::GET,"delete"=>reqwest::Method::DELETE,_=>return Err("Invalid inbox action".into())};
 state.inbox.lock().map_err(|_|"Inbox busy")?.clear();
 let bytes=if action=="read"{
  tokio::time::timeout(Duration::from_secs(60),read_owner(ORIGIN,path.rsplit('/').next().ok_or("Invalid report path")?,key,&state.cancel)).await.map_err(|_|"Owner report read timed out; retry Read selected report")??
 }else{
  let value=request(method,&path,key,None,None,65535,&state.cancel).await?;
  serde_json::to_vec(&value).map_err(|_|"Invalid inbox response")?
 };
 if bytes.len()>CAP{return Err("Inbox response too large".into())}let pages=(std::str::from_utf8(&bytes).map_err(|_|"Invalid inbox text")?.chars().count()+2999)/3000;*state.inbox.lock().map_err(|_|"Inbox busy")?=bytes;Ok(json!({"pages":pages}))
}
#[tauri::command]
pub fn support_inbox_page(state:tauri::State<Support>,page:usize)->Result<Value,String>{
 let b=state.inbox.lock().map_err(|_|"Inbox busy")?;if page>CAP/3000+1{return Err("Invalid page".into())}
 // Preserve verified original UTF-8 bytes in storage; page only for display.
 let text=std::str::from_utf8(&b).map_err(|_|"Invalid inbox text")?;let pages:Vec<String>=text.chars().collect::<Vec<_>>().chunks(3000).map(|c|c.iter().collect()).collect();
 pages.get(page).map(|t|json!({"text":t,"pages":pages.len()})).ok_or("Invalid page".into())
}
#[cfg(test)]mod tests{use super::*;#[test]fn ids_and_hash(){assert!(valid_id(&"a".repeat(32)));assert!(!valid_id("../owner-key.dpapi"));assert!(!valid_id(&"A".repeat(32)));assert_eq!(hash(b"abc"),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");}#[test]fn duplicate_job(){let s=Support::default();let g=begin(&s).unwrap();assert!(begin(&s).is_err());drop(g);assert!(begin(&s).is_ok());}}

#[cfg(test)]mod focused_tests{
 use super::*;
 fn fixture()->(Vec<u8>,Vec<u8>,String){let body=serde_json::to_string(&json!({"schema":"xz-support-report/1","report_id":"a".repeat(32),"public_issue":{"title":"Safe","description":"Steps"}})).unwrap();let sha=hash(body.as_bytes());(serde_json::to_vec(&json!({"body":body,"sha256":sha,"key":"b".repeat(64)})).unwrap(),serde_json::to_vec(&json!({"sha256":sha,"complete":true})).unwrap(),sha)}
 #[test]fn stale_preview_and_key_boundary(){let (b,r,sha)=fixture();assert!(checked(&b,&r,&"a".repeat(32),&sha).is_ok());assert!(checked(&b,&r,&"a".repeat(32),&"0".repeat(64)).is_err());assert!(checked(&b,br#"{"complete":false}"#,&"a".repeat(32),&sha).is_err());assert!(checked(&b,&r,&"c".repeat(32),&sha).is_err());}
 #[cfg(windows)]#[test]fn synthetic_owner_dpapi_roundtrip(){let raw=b"synthetic-test-owner-key-not-a-credential";let sealed=protect(raw,false).unwrap();assert_ne!(&sealed,raw);assert_eq!(protect(&sealed,true).unwrap(),raw);}
 #[tokio::test]async fn cancellation_is_unknown(){let cancel=AtomicBool::new(true);let future=async{tokio::time::sleep(Duration::from_secs(3)).await;Ok::<_,reqwest::Error>(())};assert!(cancellable(future,&cancel).await.unwrap_err().contains("OutcomeUnknown"));}
 fn server(response:&'static str)->(String,std::thread::JoinHandle<String>){
  use std::io::{Read,Write};let listener=std::net::TcpListener::bind("127.0.0.1:0").unwrap();let origin=format!("http://{}",listener.local_addr().unwrap());let t=std::thread::spawn(move||{let(mut stream,_)=listener.accept().unwrap();stream.set_read_timeout(Some(Duration::from_secs(3))).unwrap();let mut buf=[0u8;4096];let n=stream.read(&mut buf).unwrap();stream.write_all(response.as_bytes()).unwrap();String::from_utf8_lossy(&buf[..n]).to_string()});(origin,t)
 }
 #[tokio::test]async fn actual_loopback_transport_headers_and_no_redirect(){
  let (origin,t)=server("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}");
  let result=exchange(&origin,reqwest::Method::POST,"/v1/reports","fixture-receipt-key",Some(b"{}".to_vec()),Some((&"a".repeat(32),&"b".repeat(64))),65536,&AtomicBool::new(false)).await.unwrap();assert_eq!(result["ok"],true);let request=t.join().unwrap().to_lowercase();assert!(request.contains("authorization: bearer fixture-receipt-key"));assert!(request.contains("content-length: 2"));assert!(request.contains("x-report-consent: public-summary-private-logs/v1"));
  let(origin,t)=server("HTTP/1.1 302 Found\r\nLocation: https://example.invalid/secret\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");assert!(exchange(&origin,reqwest::Method::GET,"/receipt","fixture",None,None,65536,&AtomicBool::new(false)).await.unwrap_err().contains("302"));t.join().unwrap();
 }
 #[tokio::test]async fn actual_loopback_rejects_size_and_encoding(){
  for response in ["HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 999999\r\nConnection: close\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"]{
   let(origin,t)=server(response);assert!(exchange(&origin,reqwest::Method::GET,"/receipt","fixture",None,None,65536,&AtomicBool::new(false)).await.is_err());t.join().unwrap();
  }
 }
}

#[cfg(test)]mod owner_page_tests{
 use super::*;
 fn base64(bytes:&[u8])->String{
  const ALPHABET:&[u8]=b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  let mut out=String::new();for c in bytes.chunks(3){let a=c[0];let b=*c.get(1).unwrap_or(&0);let d=*c.get(2).unwrap_or(&0);out.push(ALPHABET[(a>>2)as usize]as char);out.push(ALPHABET[(((a&3)<<4)|(b>>4))as usize]as char);out.push(if c.len()>1{ALPHABET[(((b&15)<<2)|(d>>6))as usize]as char}else{'='});out.push(if c.len()>2{ALPHABET[(d&63)as usize]as char}else{'='});}out
 }
 fn payload()->Vec<u8>{let mut b=serde_json::to_vec(&json!({"schema":"xz-support-report/1","report_id":"a".repeat(32),"private":{"items":[{"content":"???".repeat(60000)}]}})).unwrap();b.resize(CAP,b' ');b}
 fn page(bytes:&[u8],offset:usize)->Value{let end=(offset+OWNER_PAGE).min(bytes.len());json!({"schema":"xz-support-owner-page/1","report_id":"a".repeat(32),"payload_sha256":hash(bytes),"total_bytes":bytes.len(),"offset":offset,"encoding":"base64","data_base64":base64(&bytes[offset..end]),"next_offset":if end==bytes.len(){None}else{Some(end)},"receipt":{"schema":"xz-support-receipt/1"}})}
 #[test]fn full_cap_multibyte_report_keeps_exact_bytes(){let bytes=payload();let mut report=OwnerReport::default();for offset in (0..bytes.len()).step_by(OWNER_PAGE){let p=page(&bytes,offset);assert!(serde_json::to_vec(&p).unwrap().len()<65536);let done=report.push(&p,&"a".repeat(32)).unwrap();assert_eq!(done,offset+OWNER_PAGE>=bytes.len());}assert!(report.complete);assert_eq!(report.bytes,bytes);}
 #[test]fn rejects_drift_gaps_repeated_pages_and_early_end(){
  let bytes=payload();for key in ["report_id","payload_sha256","total_bytes","offset","next_offset"]{
   let mut report=OwnerReport::default();assert!(!report.push(&page(&bytes,0),&"a".repeat(32)).unwrap());let mut p=page(&bytes,OWNER_PAGE);
   p[key]=match key{"report_id"=>json!("b".repeat(32)),"payload_sha256"=>json!("b".repeat(64)),"total_bytes"=>json!(CAP-1),"offset"=>json!(0),_=>Value::Null};
   assert!(report.push(&p,&"a".repeat(32)).is_err(),"{key}");
  }
  let mut report=OwnerReport::default();assert!(report.push(&page(&bytes,OWNER_PAGE),&"a".repeat(32)).is_err());
 }
 #[test]fn rejects_invalid_base64_bounds_and_final_hash(){
  for text in ["","AA=Z","AB==","AAB=","AAA","!!!!","AAAA===="]{assert!(decode_page(text).is_err(),"{text}");}
  assert_eq!(decode_page("AA==").unwrap(),vec![0]);assert_eq!(decode_page("AAA=").unwrap(),vec![0,0]);assert!(decode_page(&"A".repeat(32772)).is_err());
  let b=br#"{"schema":"xz-support-report/1","report_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"#;let mut p=page(b,0);p["payload_sha256"]=json!("0".repeat(64));assert!(OwnerReport::default().push(&p,&"a".repeat(32)).unwrap_err().contains("integrity"));
  let mut p=page(b,0);p["total_bytes"]=json!(CAP+1);assert!(OwnerReport::default().push(&p,&"a".repeat(32)).is_err());
 }
 #[tokio::test]async fn actual_owner_http_pages_authenticate_every_request(){
  use std::io::{Read,Write};let bytes=payload();let expected=bytes.clone();let listener=std::net::TcpListener::bind("127.0.0.1:0").unwrap();let origin=format!("http://{}",listener.local_addr().unwrap());
  let server=std::thread::spawn(move||{for offset in (0..bytes.len()).step_by(OWNER_PAGE){let(mut socket,_)=listener.accept().unwrap();socket.set_read_timeout(Some(Duration::from_secs(3))).unwrap();let mut input=[0u8;4096];let n=socket.read(&mut input).unwrap();let input=String::from_utf8_lossy(&input[..n]).to_lowercase();assert!(input.contains(&format!("get /v1/admin/reports/{}?offset={offset} ","a".repeat(32))));assert!(input.contains("authorization: bearer synthetic-owner-test-key"));let body=serde_json::to_vec(&page(&bytes,offset)).unwrap();let header=format!("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",body.len());socket.write_all(header.as_bytes()).unwrap();socket.write_all(&body).unwrap();}});
  let result=read_owner(&origin,&"a".repeat(32),"synthetic-owner-test-key",&AtomicBool::new(false)).await.unwrap();server.join().unwrap();assert_eq!(result,expected);
 }
 #[tokio::test]async fn cancelled_owner_read_never_connects(){assert!(read_owner("http://127.0.0.1:1",&"a".repeat(32),"synthetic",&AtomicBool::new(true)).await.unwrap_err().contains("cancelled"));}
}
