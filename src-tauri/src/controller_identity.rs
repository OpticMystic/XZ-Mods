use crate::{support, support_contract as contract};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::{fs::{File, OpenOptions}, io::Write, path::Path};

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct LoaderIdentity {
    pub version: u8,
    pub loader_id: String,
    pub loader_key: String,
    pub installation_id: String,
    pub created_at: String,
    pub app_version: String,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct Loader {
    pub identity: LoaderIdentity,
    pub staged: Option<LoaderIdentity>,
    pub registered_hash: Option<String>,
    pub revoke: bool,
    pub revoked: bool,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct Installation {
    pub version: u8,
    pub installation_id: String,
    pub installation_key: String,
    pub registered: bool,
    pub loaders: Vec<Loader>,
}

pub(crate) fn random_hex(bytes: usize) -> Result<String, String> {
    let mut data = vec![0u8; bytes];
    #[cfg(windows)] {
        #[link(name="bcrypt")]
        extern "system" { fn BCryptGenRandom(algorithm:usize, data:*mut u8, length:u32, flags:u32)->i32; }
        if unsafe { BCryptGenRandom(0,data.as_mut_ptr(),data.len() as u32,2) } != 0 { return Err("Private identity random source unavailable".into()); }
    }
    #[cfg(not(windows))] {
        use std::io::Read;
        File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut data)).map_err(|_| "Private identity random source unavailable")?;
    }
    Ok(data.iter().map(|b|format!("{b:02x}")).collect())
}
fn random_number() -> Result<u64,String> { u64::from_str_radix(&random_hex(5)?,16).map_err(|_|"Invalid random identity".into()) }
fn created_at() -> Result<String,String> {
    #[cfg(windows)] {
        #[repr(C)] #[derive(Default)] struct SystemTime { year:u16,month:u16,weekday:u16,day:u16,hour:u16,minute:u16,second:u16,millis:u16 }
        #[link(name="kernel32")] extern "system" { fn GetSystemTime(time:*mut SystemTime); }
        let mut t=SystemTime::default();unsafe { GetSystemTime(&mut t); }
        Ok(format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",t.year,t.month,t.day,t.hour,t.minute,t.second,t.millis))
    }
    #[cfg(not(windows))] { Err("Loader identities need Windows private storage".into()) }
}
pub(crate) fn canonical_installation(id:&str)->bool { contract::parse_installation_id(id).and_then(contract::format_installation_id).as_deref()==Some(id) }
pub(crate) fn canonical_loader(id:&str)->bool { contract::parse_loader_id(id).and_then(contract::format_loader_id).as_deref()==Some(id) }
/* The support contract and the deployed support service take [a-z0-9.] prerelease tags: 0.5.1-Beta is 0.5.1-beta there. */
pub(crate) fn contract_version()->String { env!("CARGO_PKG_VERSION").to_ascii_lowercase() }
impl LoaderIdentity {
    pub(crate) fn valid(&self)->bool {
        self.version==1 && canonical_installation(&self.installation_id) && canonical_loader(&self.loader_id) && support::valid_hex64(&self.loader_key)
            && self.created_at.len()==24 && self.created_at.ends_with('Z') && self.app_version.len()<=64
            && (self.app_version=="unknown" || semver::Version::parse(&self.app_version).is_ok())
    }
}
// One OS lock covers native reads, rotations and registration across app windows.
// It is held across a queue sync, so another process cannot register stale state.
pub(crate) fn lock(root:&Path)->Result<File,String> {
    std::fs::create_dir_all(root).map_err(|_|"Controller identity folder unavailable")?;
    support::unlinked(root)?;
    let path=root.join("controller-identity.lock");support::unlinked(&path)?;
    let mut options=OpenOptions::new();options.read(true).write(true).create(true).truncate(false);
    #[cfg(windows)] { use std::os::windows::fs::OpenOptionsExt;options.share_mode(0); }
    options.open(path).map_err(|_|"Another window is checking controller reports".into())
}
pub(crate) fn seal(path:&Path,value:&impl Serialize)->Result<(),String> {
    support::unlinked(path)?;
    let bytes=serde_json::to_vec(value).map_err(|_|"Invalid private controller state")?;
    let sealed=support::protect(&bytes,false)?;
    let temporary=path.with_extension(format!("{}.tmp",random_hex(8)?));
    let result=(|| {
        let mut file=OpenOptions::new().write(true).create_new(true).open(&temporary).map_err(|_|"Private controller state could not be saved")?;
        file.write_all(&sealed).and_then(|_|file.sync_all()).map_err(|_|"Private controller state could not be saved")?;
        drop(file);
        std::fs::rename(&temporary,path).map_err(|_|"Private controller state could not be saved")
    })();
    let _=std::fs::remove_file(temporary);result.map_err(str::to_string)
}
pub(crate) fn load(root:&Path)->Result<Installation,String> {
    let path=root.join("installation.dpapi");support::unlinked(&path)?;
    if !path.try_exists().map_err(|_|"Private installation state unavailable")? {
        let value=Installation {version:1,installation_id:contract::format_installation_id(random_number()?).ok_or("Invalid installation ID")?,installation_key:random_hex(32)?,registered:false,loaders:vec![]};
        save(root,&value)?;return Ok(value);
    }
    let bytes=support::protect(&support::read(&path,131072)?,true)?;
    let value:Installation=serde_json::from_slice(&bytes).map_err(|_|"Invalid private installation state")?;
    if value.version!=1 || !canonical_installation(&value.installation_id) || !support::valid_hex64(&value.installation_key) || value.loaders.len()>64
        || value.loaders.iter().any(|l| !l.identity.valid() || l.identity.installation_id!=value.installation_id || l.staged.as_ref().is_some_and(|s|!s.valid()||s.installation_id!=value.installation_id||s.loader_id!=l.identity.loader_id) || l.registered_hash.as_ref().is_some_and(|h|!support::valid_hex64(h))) {
        return Err("Invalid private installation state".into());
    }
    Ok(value)
}
pub(crate) fn save(root:&Path,value:&Installation)->Result<(),String> { seal(&root.join("installation.dpapi"),value) }

impl Installation {
    pub(crate) fn reconcile(&mut self,observed:&LoaderIdentity)->bool {
        if !observed.valid() || observed.installation_id!=self.installation_id {return false;}
        let Some(loader)=self.loaders.iter_mut().find(|l|l.identity.loader_id==observed.loader_id) else{return false};
        if loader.staged.as_ref().is_some_and(|s|s.loader_key==observed.loader_key) {
            loader.identity=observed.clone();loader.staged=None;loader.revoke=false;loader.revoked=false;
        }
        loader.identity.loader_key==observed.loader_key
    }
    pub(crate) fn stage(&mut self,observed:Option<&LoaderIdentity>)->Result<LoaderIdentity,String> {
        let known=observed.filter(|o|self.reconcile(o)).map(|o|o.loader_id.clone());
        let id=match known {Some(id)=>id,None=>{
            if self.loaders.len()>=64{return Err("Loader list is full. Contact support before adding another loader.".into());}
            contract::format_loader_id(random_number()?).ok_or("Invalid loader ID")?
        }};
        let candidate=LoaderIdentity {version:1,loader_id:id.clone(),loader_key:random_hex(32)?,installation_id:self.installation_id.clone(),created_at:created_at()?,app_version:contract_version()};
        if let Some(loader)=self.loaders.iter_mut().find(|l|l.identity.loader_id==id) {loader.staged=Some(candidate.clone());}
        else {self.loaders.push(Loader {identity:candidate.clone(),staged:Some(candidate.clone()),registered_hash:None,revoke:false,revoked:false});}
        Ok(candidate)
    }
    pub(crate) fn view(&self)->Value {
        json!({"installation_id":self.installation_id,"registered":self.registered,"loaders":self.loaders.iter().map(|l|json!({"loader_id":l.identity.loader_id,"created_at":l.identity.created_at,"revoked":l.revoked,"revocation_pending":l.revoke&&!l.revoked,"registration_pending":l.registered_hash.as_deref()!=Some(&support::hash(l.identity.loader_key.as_bytes())),"write_pending":l.staged.is_some()})).collect::<Vec<_>>()})
    }
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn staged_identity_version_fits_the_support_contract() {
        let v=contract_version();
        assert!(semver::Version::parse(&v).is_ok()&&v.len()<=64);
        assert!(v.chars().all(|c|c.is_ascii_digit()||c.is_ascii_lowercase()||c=='.'||c=='-'||c=='+'),"{v}");
        assert!(v.eq_ignore_ascii_case(env!("CARGO_PKG_VERSION")));
    }
    #[cfg(windows)] #[test] fn identity_is_private_stable_and_rotation_waits_for_verified_write() {
        let root=crate::support_store::tests::temp("controller-identity");let guard=lock(&root).unwrap();let mut install=load(&root).unwrap();
        assert_eq!(load(&root).unwrap().installation_key,install.installation_key);
        let first=install.stage(None).unwrap();save(&root,&install).unwrap();
        assert!(install.reconcile(&first));assert!(install.loaders[0].staged.is_none());
        install.loaders[0].registered_hash=Some(support::hash(first.loader_key.as_bytes()));
        let second=install.stage(Some(&first)).unwrap();assert_eq!(first.loader_id,second.loader_id);assert_ne!(first.loader_key,second.loader_key);
        assert_eq!(install.loaders[0].identity.loader_key,first.loader_key,"failed write retains active key");
        save(&root,&install).unwrap();let mut restarted=load(&root).unwrap();assert!(restarted.reconcile(&second));assert_eq!(restarted.loaders[0].identity.loader_key,second.loader_key);
        save(&root,&restarted).unwrap();let file=std::fs::read(root.join("installation.dpapi")).unwrap();let text=String::from_utf8_lossy(&file);
        assert!(!text.contains(&first.loader_key)&&!text.contains(&second.loader_key)&&!text.contains(&install.installation_key));
        let view=restarted.view().to_string();assert!(!view.contains(&second.loader_key)&&!view.contains(&install.installation_key));
        assert!(lock(&root).is_err());drop(guard);assert!(lock(&root).is_ok());
    }
    #[cfg(windows)] #[test] fn a_foreign_stick_gets_a_new_identity_and_corrupt_state_is_never_reset() {
        let root=crate::support_store::tests::temp("controller-foreign");let _guard=lock(&root).unwrap();let mut install=load(&root).unwrap();
        let mut foreign:LoaderIdentity=serde_json::from_str(include_str!("../../spec/fixtures/loader-identity.json")).unwrap();foreign.installation_id="I-0000-0000".into();
        let new=install.stage(Some(&foreign)).unwrap();assert_ne!(new.loader_id,foreign.loader_id);
        std::fs::write(root.join("installation.dpapi"),b"corrupt").unwrap();assert!(load(&root).is_err());
    }
}
