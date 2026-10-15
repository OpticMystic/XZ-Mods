use crate::screen_transport::ScreenSession;
use serde_json::{json, Value};
use std::{
    net::{Ipv4Addr, SocketAddr, UdpSocket},
    sync::Mutex,
    time::{Duration, Instant},
};
use tauri::Manager;

#[derive(Default)]
pub struct LiveStream {
    session: Mutex<Option<ScreenSession>>,
}

fn privacy_path(app: &tauri::AppHandle) -> Result<std::path::PathBuf, String> {
    Ok(app
        .path()
        .app_config_dir()
        .map_err(|e| e.to_string())?
        .join("stream-privacy.json"))
}
fn read_privacy(app: &tauri::AppHandle) -> Result<bool, String> {
    match std::fs::read(privacy_path(app)?) {
        Ok(bytes) => serde_json::from_slice(&bytes)
            .map_err(|e| format!("Cannot read stream privacy preference: {e}")),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(false),
        Err(e) => Err(e.to_string()),
    }
}
#[tauri::command]
pub fn live_stream_privacy(app: tauri::AppHandle, enabled: Option<bool>) -> Result<bool, String> {
    let state = app.state::<LiveStream>();
    let session = state
        .session
        .lock()
        .map_err(|_| "Screen state unavailable")?;
    if let Some(enabled) = enabled {
        let path = privacy_path(&app)?;
        std::fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
        std::fs::write(
            &path,
            if enabled {
                b"true".as_slice()
            } else {
                b"false".as_slice()
            },
        )
        .map_err(|e| e.to_string())?;
        if let Some(session) = session.as_ref() {
            session.set_privacy(enabled);
        }
        Ok(enabled)
    } else {
        read_privacy(&app)
    }
}

fn target(host: &str) -> Result<SocketAddr, String> {
    let ip: Ipv4Addr = host.parse().map_err(|_| "Enter the XZ's IPv4 address")?;
    if ip.is_unspecified() || ip.is_multicast() || ip.is_broadcast() || ip.is_loopback() {
        return Err("Choose the XZ's LAN address".into());
    }
    Ok(SocketAddr::from((ip, 50010)))
}
#[tauri::command]
pub fn live_stream_start(app: tauri::AppHandle, host: String) -> Result<Value, String> {
    let address = target(host.trim())?;
    let state = app.state::<LiveStream>();
    let mut session = state
        .session
        .lock()
        .map_err(|_| "Screen state unavailable")?;
    if session.is_some() {
        return Err("Stop the current screen stream before reconnecting".into());
    }
    let next = ScreenSession::start(address)?;
    next.set_privacy(read_privacy(&app)?);
    let result = next.connection_info();
    *session = Some(next);
    Ok(result)
}
#[tauri::command]
pub fn live_stream_status(app: tauri::AppHandle) -> Result<Value, String> {
    let state = app.state::<LiveStream>();
    let session = state
        .session
        .lock()
        .map_err(|_| "Screen state unavailable")?;
    Ok(session
        .as_ref()
        .map_or_else(|| json!({"phase":"stopped"}), ScreenSession::status))
}
#[tauri::command]
pub fn live_stream_stop(app: tauri::AppHandle) -> Result<(), String> {
    stop(&app.state::<LiveStream>())
}
pub fn stop(state: &LiveStream) -> Result<(), String> {
    state
        .session
        .lock()
        .map_err(|_| "Screen state unavailable")?
        .take();
    Ok(())
}

#[tauri::command]
pub async fn live_stream_detect() -> Result<Vec<String>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let socket = UdpSocket::bind("0.0.0.0:0").map_err(|e| e.to_string())?;
        socket.set_broadcast(true).map_err(|e| e.to_string())?;
        socket
            .set_read_timeout(Some(Duration::from_millis(200)))
            .map_err(|e| e.to_string())?;
        for address in ["255.255.255.255:50006", "169.254.255.255:50006"] {
            let _ = socket.send_to(b"VJDISCOVER", address);
        }
        let until = Instant::now() + Duration::from_secs(2);
        let mut hosts = Vec::new();
        let mut buffer = [0; 128];
        while Instant::now() < until {
            if let Ok((count, peer)) = socket.recv_from(&mut buffer) {
                if buffer[..count].starts_with(b"VJXZ 1 ")
                    && target(&peer.ip().to_string()).is_ok()
                    && !hosts.contains(&peer.ip().to_string())
                {
                    hosts.push(peer.ip().to_string());
                }
            }
        }
        Ok(hosts)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_non_controller_targets() {
        for host in [
            "127.0.0.1",
            "0.0.0.0",
            "224.0.0.1",
            "255.255.255.255",
            "example.com",
            "1.2.3.4;reboot",
        ] {
            assert!(target(host).is_err());
        }
        assert!(target("169.254.168.59").is_ok());
    }
}
