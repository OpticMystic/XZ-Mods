// Private storage outside Windows: the counterpart of DPAPI in support::protect.
// A random 256-bit key lives in a file only the current OS user can read (0600, in a 0700 folder) under the
// app's local data folder. Sealed records are "XZP1" + 12-byte nonce + AES-256-GCM ciphertext and tag.
// The key is kept out of the Keychain on purpose: the beta is ad-hoc signed, so every update would look like
// a new app to the Keychain and ask the user for access again.
use ring::{aead, rand::{SecureRandom, SystemRandom}};
use std::{io::Write, os::unix::fs::{DirBuilderExt, OpenOptionsExt}, path::PathBuf};

const MAGIC:&[u8;4]=b"XZP1";
const NONCE:usize=12;
const KEY:usize=32;

fn key_dir()->Result<PathBuf,String>{
    // Tests resolve the temp folder: on macOS it sits under /var, a symlink that unlinked() refuses.
    #[cfg(test)] { return Ok(std::env::temp_dir().canonicalize().map_err(|_|"Temp folder unavailable")?.join(format!("xz-private-storage-test-{}",std::process::id()))); }
    #[allow(unreachable_code)]
    {
        let home=std::env::var_os("HOME").filter(|h|!h.is_empty()).ok_or("Private storage folder unavailable")?;
        let home=PathBuf::from(home);
        let base=if cfg!(target_os="macos"){home.join("Library/Application Support")}
            else{std::env::var_os("XDG_DATA_HOME").filter(|d|!d.is_empty()).map(PathBuf::from).unwrap_or_else(||home.join(".local/share"))};
        Ok(base.join("com.opticmystic.xzmods").join("private"))
    }
}

fn read_key(path:&std::path::Path)->Result<Option<[u8;KEY]>,String>{
    match std::fs::symlink_metadata(path){
        Err(e) if e.kind()==std::io::ErrorKind::NotFound=>return Ok(None),
        Err(_)=>return Err("Private storage key unavailable".into()),
        Ok(m) if !m.is_file()||m.file_type().is_symlink()||m.len()!=KEY as u64=>return Err("Private storage key is damaged".into()),
        Ok(_)=>{}
    }
    let bytes=std::fs::read(path).map_err(|_|"Private storage key unavailable")?;
    bytes.as_slice().try_into().map(Some).map_err(|_|"Private storage key is damaged".into())
}

fn key()->Result<[u8;KEY],String>{
    let dir=key_dir()?;
    std::fs::DirBuilder::new().recursive(true).mode(0o700).create(&dir).map_err(|_|"Private storage folder unavailable")?;
    crate::support::unlinked(&dir)?;
    std::fs::set_permissions(&dir,std::os::unix::fs::PermissionsExt::from_mode(0o700)).map_err(|_|"Private storage folder unavailable")?;
    let path=dir.join("private-storage.key");
    if let Some(key)=read_key(&path)?{return Ok(key);}
    let mut key=[0u8;KEY];
    SystemRandom::new().fill(&mut key).map_err(|_|"Private storage random source unavailable")?;
    // Write a temporary file, then link it into place: a second window racing here either wins or reads
    // the complete winner, never a half-written key.
    let temporary=dir.join(format!("private-storage.key.{}.tmp",crate::controller_identity::random_hex(8)?));
    let written=(||{
        let mut file=std::fs::OpenOptions::new().write(true).create_new(true).mode(0o600).open(&temporary)?;
        file.write_all(&key)?;file.sync_all()?;drop(file);
        std::fs::hard_link(&temporary,&path)
    })();
    let _=std::fs::remove_file(&temporary);
    match written{
        Ok(())=>Ok(key),
        Err(e) if e.kind()==std::io::ErrorKind::AlreadyExists=>read_key(&path)?.ok_or("Private storage key unavailable".into()),
        Err(_)=>Err("Private storage key could not be saved".into()),
    }
}

fn sealing_key()->Result<aead::LessSafeKey,String>{
    let key=key()?;
    Ok(aead::LessSafeKey::new(aead::UnboundKey::new(&aead::AES_256_GCM,&key).map_err(|_|"Private storage key is damaged")?))
}

pub(crate) fn protect(input:&[u8],decrypt:bool)->Result<Vec<u8>,String>{
    let key=sealing_key()?;
    if decrypt{
        if input.len()<MAGIC.len()+NONCE+aead::AES_256_GCM.tag_len()||&input[..MAGIC.len()]!=MAGIC{return Err("Private record was not sealed on this computer".into())}
        let nonce=aead::Nonce::try_assume_unique_for_key(&input[MAGIC.len()..MAGIC.len()+NONCE]).map_err(|_|"Private record is damaged")?;
        let mut data=input[MAGIC.len()+NONCE..].to_vec();
        let plain=key.open_in_place(nonce,aead::Aad::from(MAGIC),&mut data).map_err(|_|"Private record was not sealed on this computer")?;
        Ok(plain.to_vec())
    }else{
        let mut nonce=[0u8;NONCE];
        SystemRandom::new().fill(&mut nonce).map_err(|_|"Private storage random source unavailable")?;
        let mut data=input.to_vec();
        key.seal_in_place_append_tag(aead::Nonce::assume_unique_for_key(nonce),aead::Aad::from(MAGIC),&mut data).map_err(|_|"Private record could not be sealed")?;
        let mut out=Vec::with_capacity(MAGIC.len()+NONCE+data.len());
        out.extend_from_slice(MAGIC);out.extend_from_slice(&nonce);out.extend_from_slice(&data);
        Ok(out)
    }
}

#[cfg(test)] mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;
    #[test] fn sealed_records_round_trip_hide_the_plaintext_and_refuse_tampering() {
        let secret=b"owner key 0123456789abcdef0123456789abcdef";
        let sealed=protect(secret,false).unwrap();
        assert!(!sealed.windows(secret.len()).any(|w|w==secret));
        assert_ne!(sealed,protect(secret,false).unwrap(),"every seal uses a fresh nonce");
        assert_eq!(protect(&sealed,true).unwrap(),secret);
        let mut tampered=sealed.clone();let last=tampered.len()-1;tampered[last]^=1;
        assert!(protect(&tampered,true).is_err());
        assert!(protect(b"corrupt",true).is_err());
        let path=key_dir().unwrap().join("private-storage.key");
        assert_eq!(std::fs::metadata(&path).unwrap().permissions().mode()&0o777,0o600);
        assert_eq!(std::fs::metadata(key_dir().unwrap()).unwrap().permissions().mode()&0o777,0o700);
    }
}
