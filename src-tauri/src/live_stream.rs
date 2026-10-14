use serde_json::{json, Value};
use std::{
    io::{Read, Write},
    net::{Ipv4Addr, Shutdown, SocketAddr, TcpListener, TcpStream, UdpSocket},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    thread,
    time::{Duration, Instant},
};
use tauri::Manager;

const PIXELS: usize = 800 * 480 * 2;
#[derive(Default)]
pub struct LiveStream {
    session: Mutex<Option<Session>>,
}
struct Session {
    stop: Arc<AtomicBool>,
    latest: Arc<Mutex<Latest>>,
    url: String,
    host: String,
}
#[derive(Default)]
struct Latest {
    bmp: Vec<u8>,
    at: Option<Instant>,
    frames: u64,
    error: String,
    reason: &'static str,
    capture_ms: u128,
}
#[derive(Debug)]
struct CaptureError {
    message: String,
    reason: &'static str,
}
impl Drop for Session {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
    }
}

fn target(host: &str) -> Result<SocketAddr, String> {
    let ip: Ipv4Addr = host.parse().map_err(|_| "Enter the XZ's IPv4 address")?;
    if ip.is_unspecified() || ip.is_multicast() || ip.is_broadcast() || ip.is_loopback() {
        return Err("Choose the XZ's LAN address".into());
    }
    Ok(SocketAddr::from((ip, 50010)))
}
fn capture(socket: &mut TcpStream) -> Result<Vec<u8>, CaptureError> {
    let interrupted = |e: std::io::Error| CaptureError {
        message: format!("XZ screen connection interrupted: {e}"),
        reason: "stream",
    };
    socket.write_all(b"XZSC\x01\0\0\0").map_err(interrupted)?;
    let mut header = [0u8; 20];
    socket.read_exact(&mut header).map_err(interrupted)?;
    if &header[..16] != b"XZSF\x01\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0" {
        return Err(CaptureError {
            message: "The XZ returned an unsupported screen format. Update its USB loader.".into(),
            reason: "protocol",
        });
    }
    let mut pixels = vec![0; PIXELS];
    socket.read_exact(&mut pixels).map_err(interrupted)?;
    Ok(bitmap(&pixels))
}
fn bitmap(pixels: &[u8]) -> Vec<u8> {
    // Top-down RGB565 BMP with explicit bit masks. No image codec or video decoder.
    let mut bmp = vec![0u8; 66];
    bmp[..2].copy_from_slice(b"BM");
    for (offset, value) in [
        (2, (66 + PIXELS) as u32),
        (10, 66),
        (14, 40),
        (18, 800),
        (22, (-480i32) as u32),
        (30, 3),
        (34, PIXELS as u32),
        (54, 0xf800),
        (58, 0x07e0),
        (62, 0x001f),
    ] {
        bmp[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    bmp[26..28].copy_from_slice(&1u16.to_le_bytes());
    bmp[28..30].copy_from_slice(&16u16.to_le_bytes());
    bmp.extend_from_slice(pixels);
    bmp
}
fn run_capture(address: SocketAddr, stop: Arc<AtomicBool>, latest: Arc<Mutex<Latest>>) {
    while !stop.load(Ordering::Acquire) {
        let result = (|| -> Result<(), CaptureError> {
            let mut socket=TcpStream::connect_timeout(&address,Duration::from_secs(2)).map_err(|e|{
                let receiver=SocketAddr::new(address.ip(),50005);
                if TcpStream::connect_timeout(&receiver,Duration::from_millis(400)).is_ok(){
                    CaptureError{reason:"loader",message:"The XZ is online, but its screen service is unavailable. Update its USB loader, then power cycle with both decks stopped.".into()}
                }else{CaptureError{reason:"network",message:format!("Cannot reach the XZ screen. Check its network connection and address. {e}")}}
            })?;
            socket
                .set_read_timeout(Some(Duration::from_secs(2)))
                .map_err(|e| CaptureError {
                    message: e.to_string(),
                    reason: "stream",
                })?;
            socket
                .set_write_timeout(Some(Duration::from_secs(2)))
                .map_err(|e| CaptureError {
                    message: e.to_string(),
                    reason: "stream",
                })?;
            while !stop.load(Ordering::Acquire) {
                let started = Instant::now();
                let bmp = capture(&mut socket)?;
                if stop.load(Ordering::Acquire) {
                    break;
                }
                {
                    let mut value = latest.lock().unwrap();
                    value.bmp = bmp;
                    value.at = Some(Instant::now());
                    value.frames += 1;
                    value.capture_ms = started.elapsed().as_millis();
                    value.error.clear();
                    value.reason = "";
                }
                // One frame per second by default, including controller/network time.
                while started.elapsed() < Duration::from_secs(1) && !stop.load(Ordering::Acquire) {
                    thread::sleep(Duration::from_millis(25));
                }
            }
            let _ = socket.shutdown(Shutdown::Both);
            Ok(())
        })();
        if let Err(error) = result {
            let mut value = latest.lock().unwrap();
            value.error = error.message;
            value.reason = error.reason;
            value.bmp.clear();
            value.at = None;
        }
        for _ in 0..40 {
            if stop.load(Ordering::Acquire) {
                break;
            }
            thread::sleep(Duration::from_millis(50));
        }
    }
}
const PAGE: &str = r#"<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;width:100%;height:100%;background:#000;overflow:hidden}img{width:100%;height:100%;object-fit:contain}p{color:#eee;font:20px system-ui;text-align:center}</style></head><body><img alt="XDJ-XZ screen"><p hidden>XZ screen disconnected</p><script>const image=document.querySelector('img'),message=document.querySelector('p');let busy=false;function next(){if(busy)return;busy=true;const frame=new Image();frame.onload=()=>{image.src=frame.src;image.hidden=false;message.hidden=true;busy=false;setTimeout(next,500)};frame.onerror=()=>{image.hidden=true;message.hidden=false;busy=false;setTimeout(next,1000)};frame.src='frame.bmp?t='+Date.now()}next();</script></body></html>"#;
fn serve(listener: TcpListener, stop: Arc<AtomicBool>, latest: Arc<Mutex<Latest>>, route: String) {
    while !stop.load(Ordering::Acquire) {
        match listener.accept() {
            Ok((mut socket, _)) => {
                let _ = socket.set_read_timeout(Some(Duration::from_millis(250)));
                let _ = socket.set_write_timeout(Some(Duration::from_millis(500)));
                let mut request = [0u8; 4096];
                let count = socket.read(&mut request).unwrap_or(0);
                let text = String::from_utf8_lossy(&request[..count]);
                let path = text
                    .split_whitespace()
                    .nth(1)
                    .unwrap_or("")
                    .split('?')
                    .next()
                    .unwrap_or("");
                let (code, mime, body) = if text.starts_with("GET ") && path == format!("/{route}/")
                {
                    ("200 OK", "text/html", PAGE.as_bytes().to_vec())
                } else if text.starts_with("GET ") && path == format!("/{route}/frame.bmp") {
                    let value = latest.lock().unwrap();
                    if !value.bmp.is_empty()
                        && value
                            .at
                            .is_some_and(|at| at.elapsed() < Duration::from_secs(3))
                    {
                        ("200 OK", "image/bmp", value.bmp.clone())
                    } else {
                        (
                            "503 Service Unavailable",
                            "text/plain",
                            b"XZ screen disconnected".to_vec(),
                        )
                    }
                } else {
                    ("404 Not Found", "text/plain", Vec::new())
                };
                let header=format!("HTTP/1.1 {code}\r\nContent-Type: {mime}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",body.len());
                let _ = socket.write_all(header.as_bytes());
                let _ = socket.write_all(&body);
            }
            Err(_) => thread::sleep(Duration::from_millis(20)),
        }
    }
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
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).map_err(|e| e.to_string())?;
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    let port = listener.local_addr().map_err(|e| e.to_string())?.port();
    let route = format!("xz-{}-{port}", std::process::id());
    let url = format!("http://127.0.0.1:{port}/{route}/");
    let stop = Arc::new(AtomicBool::new(false));
    let latest = Arc::new(Mutex::new(Latest::default()));
    let (s, l) = (stop.clone(), latest.clone());
    thread::spawn(move || run_capture(address, s, l));
    let (s, l) = (stop.clone(), latest.clone());
    thread::spawn(move || serve(listener, s, l, route));
    *session = Some(Session {
        stop,
        latest,
        url: url.clone(),
        host: address.ip().to_string(),
    });
    Ok(json!({"url":url,"frame_url":format!("{url}frame.bmp"),"width":800,"height":480}))
}
#[tauri::command]
pub fn live_stream_status(app: tauri::AppHandle) -> Result<Value, String> {
    let state = app.state::<LiveStream>();
    let session = state
        .session
        .lock()
        .map_err(|_| "Screen state unavailable")?;
    let Some(session) = session.as_ref() else {
        return Ok(json!({"phase":"stopped"}));
    };
    let value = session
        .latest
        .lock()
        .map_err(|_| "Screen frame unavailable")?;
    let connected = value
        .at
        .is_some_and(|at| at.elapsed() < Duration::from_secs(3));
    Ok(
        json!({"phase":if connected {"live"} else {"connecting"},"url":session.url,"host":session.host,"frames":value.frames,"capture_ms":value.capture_ms,"age_ms":value.at.map(|at|at.elapsed().as_millis()),"error":value.error,"reason":value.reason,"width":800,"height":480}),
    )
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
    #[test]
    fn rgb565_top_down_bitmap_is_exact() {
        let pixels = vec![0x12; PIXELS];
        let bmp = bitmap(&pixels);
        assert_eq!(&bmp[..2], b"BM");
        assert_eq!(&bmp[66..], pixels);
        assert_eq!(i32::from_le_bytes(bmp[22..26].try_into().unwrap()), -480);
        assert_eq!(u32::from_le_bytes(bmp[54..58].try_into().unwrap()), 0xf800);
    }
    #[test]
    fn wire_capture_handles_fragmented_frame() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let sender = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            let mut request = [0; 8];
            socket.read_exact(&mut request).unwrap();
            assert_eq!(&request, b"XZSC\x01\0\0\0");
            for byte in b"XZSF\x01\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0\x01\0\0\0" {
                socket.write_all(&[*byte]).unwrap();
            }
            socket.write_all(&vec![0xff; PIXELS]).unwrap();
        });
        let mut socket = TcpStream::connect(address).unwrap();
        assert_eq!(capture(&mut socket).unwrap().len(), PIXELS + 66);
        sender.join().unwrap();
    }
    #[test]
    fn rejects_bad_header_before_payload_allocation() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let sender = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            let mut request = [0; 8];
            socket.read_exact(&mut request).unwrap();
            socket.write_all(&[0; 20]).unwrap();
        });
        assert!(capture(&mut TcpStream::connect(address).unwrap())
            .unwrap_err()
            .message
            .contains("unsupported"));
        sender.join().unwrap();
    }
    #[test]
    fn http_never_serves_a_stale_frame_and_closes_on_stop() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        listener.set_nonblocking(true).unwrap();
        let latest = Arc::new(Mutex::new(Latest {
            bmp: bitmap(&vec![0x55; PIXELS]),
            at: Some(Instant::now()),
            ..Latest::default()
        }));
        let stop = Arc::new(AtomicBool::new(false));
        let s = stop.clone();
        let l = latest.clone();
        let worker = thread::spawn(move || serve(listener, s, l, "test".into()));
        let get = |path: &str| {
            let mut client = TcpStream::connect(address).unwrap();
            client
                .set_read_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            write!(client, "GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n").unwrap();
            let mut bytes = Vec::new();
            client.read_to_end(&mut bytes).unwrap();
            bytes
        };
        let response = get("/test/frame.bmp");
        assert!(response.starts_with(b"HTTP/1.1 200"));
        assert!(response.ends_with(&vec![0x55; PIXELS]));
        latest.lock().unwrap().at = Some(Instant::now() - Duration::from_secs(4));
        assert!(get("/test/frame.bmp").starts_with(b"HTTP/1.1 503"));
        assert!(get("/other/frame.bmp").starts_with(b"HTTP/1.1 404"));
        stop.store(true, Ordering::Release);
        worker.join().unwrap();
        assert!(TcpStream::connect(address).is_err());
    }
    #[test]
    fn interrupted_capture_discards_frame_and_reconnects() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let stop = Arc::new(AtomicBool::new(false));
        let latest = Arc::new(Mutex::new(Latest::default()));
        let s = stop.clone();
        let l = latest.clone();
        let worker = thread::spawn(move || run_capture(address, s, l));
        let send_frame = |client: &mut TcpStream, color: u8| {
            let mut request = [0; 8];
            client.read_exact(&mut request).unwrap();
            client
                .write_all(b"XZSF\x01\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0\x01\0\0\0")
                .unwrap();
            client.write_all(&vec![color; PIXELS]).unwrap();
        };
        let (mut client, _) = listener.accept().unwrap();
        send_frame(&mut client, 0x11);
        let until = Instant::now() + Duration::from_secs(2);
        while latest.lock().unwrap().frames < 1 && Instant::now() < until {
            thread::sleep(Duration::from_millis(10));
        }
        assert_eq!(latest.lock().unwrap().frames, 1);
        client.shutdown(Shutdown::Both).unwrap();
        drop(client);
        let until = Instant::now() + Duration::from_secs(3);
        while latest.lock().unwrap().at.is_some() && Instant::now() < until {
            thread::sleep(Duration::from_millis(10));
        }
        {
            let value = latest.lock().unwrap();
            assert!(value.at.is_none() && value.bmp.is_empty());
            assert_eq!(value.reason, "stream");
        }
        let (mut client, _) = listener.accept().unwrap();
        send_frame(&mut client, 0x22);
        let until = Instant::now() + Duration::from_secs(2);
        while latest.lock().unwrap().frames < 2 && Instant::now() < until {
            thread::sleep(Duration::from_millis(10));
        }
        {
            let value = latest.lock().unwrap();
            assert_eq!(value.frames, 2);
            assert_eq!(value.bmp[66], 0x22);
            assert!(value.error.is_empty());
        }
        stop.store(true, Ordering::Release);
        client.shutdown(Shutdown::Both).unwrap();
        worker.join().unwrap();
    }
}
