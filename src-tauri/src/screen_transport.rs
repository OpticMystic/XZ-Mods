use crate::screen_protocol::{self, CaptureError, Decoder, Event, Frame, Protocol, PIXELS};
use serde_json::{json, Value};
use std::{
    collections::VecDeque,
    io::{Read, Write},
    net::{Ipv4Addr, SocketAddr, TcpListener, TcpStream},
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc, Mutex,
    },
    thread,
    time::{Duration, Instant},
};
use tungstenite::{
    handshake::server::{Request, Response},
    protocol::WebSocketConfig,
    Message,
};

const PAGE: &str = include_str!("screen-view.html");
const MAX_FRAME_AGE_MS: u64 = 5000;
#[derive(Default)]
struct Latest {
    frame: Option<Frame>,
    at: Option<Instant>,
    frames: u64,
    capture_ms: u128,
    error: String,
    reason: &'static str,
    samples: VecDeque<(Instant, usize, bool)>,
    diagnostics: Option<Value>,
    privacy: bool,
    revision: u64,
}
impl Latest {
    fn fresh(&self) -> bool {
        // Protocol v2 is a format, not a measured delivery rate. A busy XZ can
        // take over a second to capture/encode a frame on a healthy socket.
        // Socket errors still clear the frame immediately in disconnected().
        self.at
            .is_some_and(|at| at.elapsed() < Duration::from_millis(MAX_FRAME_AGE_MS))
    }
    fn publish(&mut self, frame: Frame, elapsed: Duration) {
        let now = Instant::now();
        self.samples
            .push_back((now, frame.encoded_bytes, frame.changed));
        while self.samples.len() > 180
            || (self.samples.len() > 2
                && self.samples.front().unwrap().0.elapsed() > Duration::from_millis(2500))
        {
            self.samples.pop_front();
        }
        self.frame = Some(frame);
        self.at = Some(now);
        self.frames += 1;
        self.revision += 1;
        self.capture_ms = elapsed.as_millis();
        self.error.clear();
        self.reason = "";
    }
    fn disconnected(&mut self, error: CaptureError) {
        self.frame = None;
        self.at = None;
        self.samples.clear();
        self.diagnostics = None;
        self.error = error.message;
        self.reason = error.reason;
    }
    fn status(&self) -> Value {
        let span = self
            .samples
            .front()
            .zip(self.samples.back())
            .map(|(first, last)| last.0.duration_since(first.0).as_secs_f64())
            .unwrap_or(0.0);
        let measured = self.fresh() && span >= 2.0;
        let fps = measured.then(|| (self.samples.len() - 1) as f64 / span);
        let mbps = measured.then(|| {
            self.samples
                .iter()
                .skip(1)
                .map(|sample| sample.1)
                .sum::<usize>() as f64
                * 8.0
                / span
                / 1e6
        });
        let changed_fps = measured.then(|| {
            self.samples
                .iter()
                .skip(1)
                .filter(|sample| sample.2)
                .count() as f64
                / span
        });
        json!({"phase":if self.fresh(){"live"}else{"connecting"},"frames":self.frames,"capture_ms":self.capture_ms,
            "age_ms":self.at.map(|at|at.elapsed().as_millis()),"error":self.error,"reason":self.reason,"width":800,"height":480,
            "protocol":self.frame.as_ref().map(|frame|frame.protocol.version()),"target_fps":self.frame.as_ref().map(|frame|frame.protocol.target_fps()),
            "fps":fps,"changed_fps":changed_fps,"network_mbps":mbps,"device_copy_us":self.frame.as_ref().map(|frame|frame.copy_us),
            "device_encode_us":self.frame.as_ref().map(|frame|frame.encode_us),"pc_decode_us":self.frame.as_ref().map(|frame|frame.decode_us),"device_time_us":self.frame.as_ref().map(|frame|frame.device_time_us),
            "device_sequence":self.frame.as_ref().map(|frame|frame.sequence),"diagnostics":self.diagnostics,"privacy_enabled":self.privacy,"privacy_covered":self.frame.as_ref().is_some_and(|frame| crate::stream_privacy::covered(frame,self.privacy))})
    }
}

pub struct ScreenSession {
    stop: Arc<AtomicBool>,
    latest: Arc<Mutex<Latest>>,
    clients: Arc<AtomicUsize>,
    url: String,
    host: String,
}
impl ScreenSession {
    pub fn start(address: SocketAddr) -> Result<Self, String> {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).map_err(|e| e.to_string())?;
        listener.set_nonblocking(true).map_err(|e| e.to_string())?;
        let port = listener.local_addr().map_err(|e| e.to_string())?.port();
        let route = format!("xz-{}-{port}", std::process::id());
        let url = format!("http://127.0.0.1:{port}/{route}/");
        let stop = Arc::new(AtomicBool::new(false));
        let latest = Arc::new(Mutex::new(Latest::default()));
        let clients = Arc::new(AtomicUsize::new(0));
        let (s, l) = (stop.clone(), latest.clone());
        thread::Builder::new()
            .name("XZ screen capture".into())
            .spawn(move || run_capture(address, s, l))
            .map_err(|e| e.to_string())?;
        let (s, l, c) = (stop.clone(), latest.clone(), clients.clone());
        if let Err(error) = thread::Builder::new()
            .name("XZ screen HTTP".into())
            .spawn(move || serve(listener, s, l, c, route))
        {
            stop.store(true, Ordering::Release);
            return Err(error.to_string());
        }
        Ok(Self {
            stop,
            latest,
            clients,
            url,
            host: address.ip().to_string(),
        })
    }
    pub fn set_privacy(&self, enabled: bool) {
        let mut latest = self.latest.lock().unwrap();
        if latest.privacy != enabled {
            latest.privacy = enabled;
            latest.revision += 1;
        }
    }
    pub fn connection_info(&self) -> Value {
        json!({"url":self.url,"frame_url":format!("{}frame.bmp",self.url),"width":800,"height":480})
    }
    pub fn status(&self) -> Value {
        let mut result = self.latest.lock().unwrap().status();
        result["url"] = json!(self.url);
        result["host"] = json!(self.host);
        result["viewer_connections"] = json!(self.clients.load(Ordering::Acquire));
        result
    }
}
impl Drop for ScreenSession {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
    }
}

fn screen_socket(address: SocketAddr) -> Result<TcpStream, CaptureError> {
    let socket=TcpStream::connect_timeout(&address,Duration::from_secs(2)).map_err(|error|{
        let receiver=SocketAddr::new(address.ip(),50005);
        if TcpStream::connect_timeout(&receiver,Duration::from_millis(400)).is_ok(){
            CaptureError{reason:"loader",message:"The XZ is online, but its screen service is unavailable. Update its USB loader, then power cycle with both decks stopped.".into(),legacy_fallback:false}
        }else{CaptureError{reason:"network",message:format!("Cannot reach the XZ screen. Check its network connection and address. {error}"),legacy_fallback:false}}
    })?;
    socket.set_nodelay(true)?;
    socket.set_read_timeout(Some(Duration::from_millis(MAX_FRAME_AGE_MS)))?;
    socket.set_write_timeout(Some(Duration::from_secs(2)))?;
    Ok(socket)
}
fn negotiate(address: SocketAddr) -> Result<(TcpStream, Decoder, Frame, Duration), CaptureError> {
    let started = Instant::now();
    let mut socket = screen_socket(address)?;
    let mut decoder = Decoder::new(Protocol::Compressed);
    match decoder.capture(&mut socket) {
        Ok(frame) => Ok((socket, decoder, frame, started.elapsed())),
        Err(error) if error.legacy_fallback => {
            drop(socket);
            let mut socket = screen_socket(address)?;
            let mut decoder = Decoder::new(Protocol::Legacy);
            let started = Instant::now();
            let frame = decoder.capture(&mut socket)?;
            Ok((socket, decoder, frame, started.elapsed()))
        }
        Err(error) => Err(error),
    }
}
struct FrameClock {
    device_us: u32,
    received: Instant,
}
// The wire timestamp marks capture start. Compare delivery against encode
// completion, so variable controller work is not mistaken for a queued frame.
fn device_ready_us(frame: &Frame) -> u32 {
    frame
        .device_time_us
        .wrapping_add(frame.copy_us)
        .wrapping_add(frame.encode_us)
}
impl FrameClock {
    fn new(device_us: u32, received: Instant) -> Self {
        Self {
            device_us,
            received,
        }
    }
    fn check(&mut self, device_us: u32, received: Instant) -> Result<(), CaptureError> {
        let local = received.duration_since(self.received);
        let device = Duration::from_micros(device_us.wrapping_sub(self.device_us) as u64);
        if device > local + Duration::from_secs(5)
            || local.saturating_sub(device) > Duration::from_millis(500)
        {
            return Err(CaptureError {
                message: "Screen delivery fell behind. Reconnecting to the current picture.".into(),
                reason: "stream",
                legacy_fallback: false,
            });
        }
        // Source credit bounds the queue to two frames; rebase before the u32
        // device clock wraps and avoid accumulating clock drift over a long set.
        if local >= Duration::from_secs(60) {
            self.device_us = device_us;
            self.received = received;
        }
        Ok(())
    }
}
fn run_capture(address: SocketAddr, stop: Arc<AtomicBool>, latest: Arc<Mutex<Latest>>) {
    while !stop.load(Ordering::Acquire) {
        let result = (|| -> Result<(), CaptureError> {
            let (mut socket, mut decoder, first, elapsed) = negotiate(address)?;
            let protocol = first.protocol;
            let mut clock = FrameClock::new(device_ready_us(&first), Instant::now());
            if protocol == Protocol::Compressed {
                screen_protocol::acknowledge(&mut socket, first.sequence)?;
            }
            // V2 is paced by the player and bounded by two source credits.
            // Reading diagnostics must not add another frame delay on the PC.
            let interval = match protocol {
                Protocol::Legacy => Duration::from_secs(1),
                Protocol::Compressed => Duration::ZERO,
            };
            {
                let mut value = latest.lock().unwrap();
                value.samples.clear();
                value.publish(first, elapsed);
            }
            let mut tick = Instant::now();
            while !stop.load(Ordering::Acquire) {
                while tick.elapsed() < interval && !stop.load(Ordering::Acquire) {
                    thread::sleep(
                        interval
                            .saturating_sub(tick.elapsed())
                            .min(Duration::from_millis(5)),
                    );
                }
                if stop.load(Ordering::Acquire) {
                    break;
                }
                tick = Instant::now();
                let event = if protocol == Protocol::Legacy {
                    Event::Frame(decoder.capture(&mut socket)?)
                } else {
                    decoder.next_event(&mut socket)?
                };
                if stop.load(Ordering::Acquire) {
                    break;
                }
                match event {
                    Event::Frame(frame) => {
                        if protocol == Protocol::Compressed {
                            clock.check(device_ready_us(&frame), Instant::now())?;
                            screen_protocol::acknowledge(&mut socket, frame.sequence)?;
                        }
                        latest.lock().unwrap().publish(frame, tick.elapsed());
                    }
                    Event::Diagnostics(status) => {
                        latest.lock().unwrap().diagnostics = Some(status);
                    }
                }
            }
            Ok(())
        })();
        if let Err(error) = result {
            latest.lock().unwrap().disconnected(error);
        }
        for _ in 0..40 {
            if stop.load(Ordering::Acquire) {
                break;
            }
            thread::sleep(Duration::from_millis(50));
        }
    }
}

fn bitmap(pixels: &[u8]) -> Vec<u8> {
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
fn reply(socket: &mut TcpStream, code: &str, mime: &str, body: &[u8]) {
    let header=format!("HTTP/1.1 {code}\r\nContent-Type: {mime}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",body.len());
    let _ = socket.write_all(header.as_bytes());
    let _ = socket.write_all(body);
}
fn request_header(socket: &TcpStream) -> Option<String> {
    let started = Instant::now();
    let mut buffer = [0u8; 8192];
    while started.elapsed() < Duration::from_millis(500) {
        match socket.peek(&mut buffer) {
            Ok(0) => return None,
            Ok(count) => {
                if let Some(end) = buffer[..count]
                    .windows(4)
                    .position(|part| part == b"\r\n\r\n")
                {
                    return std::str::from_utf8(&buffer[..end + 4])
                        .ok()
                        .map(str::to_owned);
                }
                if count == buffer.len() {
                    return None;
                }
                thread::sleep(Duration::from_millis(1));
            }
            Err(error)
                if matches!(
                    error.kind(),
                    std::io::ErrorKind::WouldBlock
                        | std::io::ErrorKind::TimedOut
                        | std::io::ErrorKind::Interrupted
                ) => {}
            Err(_) => return None,
        }
    }
    None
}
struct ClientLease(Arc<AtomicUsize>);
impl Drop for ClientLease {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::AcqRel);
    }
}
fn serve(
    listener: TcpListener,
    stop: Arc<AtomicBool>,
    latest: Arc<Mutex<Latest>>,
    clients: Arc<AtomicUsize>,
    route: String,
) {
    while !stop.load(Ordering::Acquire) {
        match listener.accept() {
            Ok((mut socket, _)) => {
                let _ = socket.set_nonblocking(false);
                let _ = socket.set_nodelay(true);
                let _ = socket.set_read_timeout(Some(Duration::from_millis(250)));
                let _ = socket.set_write_timeout(Some(Duration::from_millis(500)));
                if clients.fetch_add(1, Ordering::AcqRel) >= 4 {
                    clients.fetch_sub(1, Ordering::AcqRel);
                    reply(
                        &mut socket,
                        "503 Service Unavailable",
                        "text/plain",
                        b"Close another screen viewer and retry",
                    );
                    continue;
                }
                let lease = ClientLease(clients.clone());
                let (s, l, r) = (stop.clone(), latest.clone(), route.clone());
                let _ = thread::Builder::new()
                    .name("XZ screen viewer".into())
                    .spawn(move || {
                        let _lease = lease;
                        serve_client(socket, s, l, &r);
                    });
            }
            Err(_) => thread::sleep(Duration::from_millis(10)),
        }
    }
}
fn serve_client(
    mut socket: TcpStream,
    stop: Arc<AtomicBool>,
    latest: Arc<Mutex<Latest>>,
    route: &str,
) {
    let Some(header) = request_header(&socket) else {
        return;
    };
    let path = header
        .split_whitespace()
        .nth(1)
        .unwrap_or("")
        .split('?')
        .next()
        .unwrap_or("");
    let upgrade = header.lines().any(|line| {
        line.split_once(':').is_some_and(|(key, value)| {
            key.eq_ignore_ascii_case("upgrade") && value.trim().eq_ignore_ascii_case("websocket")
        })
    });
    if upgrade {
        let origin = format!("http://{}", socket.local_addr().unwrap());
        let expected = format!("/{route}/frames");
        let config = WebSocketConfig::default()
            .read_buffer_size(1024)
            .write_buffer_size(0)
            .max_write_buffer_size(PIXELS + 1024)
            .max_message_size(Some(64))
            .max_frame_size(Some(64));
        let callback = move |request: &Request, response: Response| {
            let allowed = request.uri().path() == expected
                && request
                    .headers()
                    .get("origin")
                    .is_none_or(|value| value.to_str().is_ok_and(|value| value == origin));
            if allowed {
                Ok(response)
            } else {
                Err(tungstenite::http::Response::builder()
                    .status(403)
                    .body(Some("Screen viewer origin or path rejected".into()))
                    .unwrap())
            }
        };
        if let Ok(mut websocket) =
            tungstenite::accept_hdr_with_config(socket, callback, Some(config))
        {
            let mut sent = 0u64;
            let mut warned = false;
            while !stop.load(Ordering::Acquire) {
                let next = {
                    let value = latest.lock().unwrap();
                    if value.fresh() {
                        value.frame.clone().map(|frame| {
                            (
                                value.revision,
                                crate::stream_privacy::output(frame, value.privacy),
                            )
                        })
                    } else {
                        None
                    }
                };
                if let Some((number, frame)) = next {
                    if number != sent {
                        let mut packet = Vec::with_capacity(PIXELS + 16);
                        packet.extend_from_slice(b"XZPV");
                        packet.extend_from_slice(&(number as u32).to_le_bytes());
                        packet.extend_from_slice(&800u16.to_le_bytes());
                        packet.extend_from_slice(&480u16.to_le_bytes());
                        packet.extend_from_slice(&(frame.protocol.version() as u32).to_le_bytes());
                        packet.extend_from_slice(&frame.pixels);
                        if websocket.send(Message::Binary(packet.into())).is_err() {
                            break;
                        }
                        let until = Instant::now() + Duration::from_secs(2);
                        let mut acknowledged = false;
                        while !stop.load(Ordering::Acquire) && Instant::now() < until {
                            match websocket.read() {
                                Ok(Message::Binary(data))
                                    if data.len() == 4
                                        && u32::from_le_bytes(data[..].try_into().unwrap())
                                            == number as u32 =>
                                {
                                    acknowledged = true;
                                    break;
                                }
                                Ok(Message::Ping(_)) | Ok(Message::Pong(_)) => {
                                    let _ = websocket.flush();
                                }
                                Err(tungstenite::Error::Io(error))
                                    if matches!(
                                        error.kind(),
                                        std::io::ErrorKind::WouldBlock
                                            | std::io::ErrorKind::TimedOut
                                    ) => {}
                                _ => break,
                            }
                        }
                        if !acknowledged {
                            break;
                        }
                        sent = number;
                        warned = false;
                    }
                } else if !warned {
                    if websocket
                        .send(Message::Text("{\"live\":false}".into()))
                        .is_err()
                    {
                        break;
                    }
                    warned = true;
                }
                thread::sleep(Duration::from_millis(5));
            }
            let _ = websocket.close(None);
        }
        return;
    }
    let mut consumed = vec![0; header.len()];
    if socket.read_exact(&mut consumed).is_err() {
        return;
    }
    if !header.starts_with("GET ") {
        reply(
            &mut socket,
            "405 Method Not Allowed",
            "text/plain",
            b"Read-only screen endpoint",
        );
        return;
    }
    if path == format!("/{route}/") {
        reply(
            &mut socket,
            "200 OK",
            "text/html; charset=utf-8",
            PAGE.as_bytes(),
        );
    } else if path == format!("/{route}/frame.bmp") {
        let pixels = {
            let value = latest.lock().unwrap();
            if value.fresh() {
                value
                    .frame
                    .clone()
                    .map(|frame| crate::stream_privacy::output(frame, value.privacy).pixels)
            } else {
                None
            }
        };
        if let Some(pixels) = pixels {
            reply(&mut socket, "200 OK", "image/bmp", &bitmap(&pixels));
        } else {
            reply(
                &mut socket,
                "503 Service Unavailable",
                "text/plain",
                b"XZ screen disconnected",
            );
        }
    } else if path == format!("/{route}/status.json") {
        let status = latest.lock().unwrap().status();
        reply(
            &mut socket,
            "200 OK",
            "application/json",
            status.to_string().as_bytes(),
        );
    } else {
        reply(&mut socket, "404 Not Found", "text/plain", b"");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tungstenite::client::IntoClientRequest;
    #[test]
    fn device_clock_wrap_is_valid_but_late_frames_are_rejected() {
        let now = Instant::now();
        let mut clock = FrameClock::new(u32::MAX - 29999, now);
        assert!(clock.check(17000, now + Duration::from_millis(47)).is_ok());
        assert!(clock.check(18000, now + Duration::from_secs(1)).is_err());
        let mut clock = FrameClock::new(100, now);
        assert!(clock.check(6_000_100, now).is_err());
    }
    fn frame(color: u8) -> Frame {
        Frame {
            pixels: vec![color; PIXELS].into(),
            sequence: 1,
            protocol: Protocol::Compressed,
            device_time_us: 0,
            copy_us: 1,
            encode_us: 1,
            decode_us: 1,
            encoded_bytes: 64,
            changed: true,
        }
    }
    #[test]
    fn slow_encoding_is_not_mistaken_for_network_backlog() {
        let now = Instant::now();
        let mut first = frame(0);
        first.copy_us = 10_000;
        first.encode_us = 10_000;
        let mut next = first.clone();
        next.device_time_us = 500_000;
        next.copy_us = 500_000;
        next.encode_us = 1_300_000;
        let mut clock = FrameClock::new(device_ready_us(&first), now);
        assert!(clock
            .check(device_ready_us(&next), now + Duration::from_millis(2280))
            .is_ok());
        assert!(
            clock
                .check(device_ready_us(&next), now + Duration::from_millis(3000))
                .is_err(),
            "Actual delivery backlog is still rejected"
        );
    }
    #[test]
    fn slow_controller_response_survives_the_two_second_boundary() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let source = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            let mut request = [0; 8];
            socket.read_exact(&mut request).unwrap();
            assert_eq!(&request, b"XZSC\x02\0\0\0");
            let send = |socket: &mut TcpStream, sequence: u32, time: u32| {
                let mut header = [0; 32];
                header[..12].copy_from_slice(b"XZSF\x02\0\x20\x03\xe0\x01\0\0");
                header[12..16].copy_from_slice(&(PIXELS as u32).to_le_bytes());
                header[16..20].copy_from_slice(&sequence.to_le_bytes());
                header[20..24].copy_from_slice(&time.to_le_bytes());
                socket
                    .write_all(&header)
                    .and_then(|_| socket.write_all(&vec![0; PIXELS]))
            };
            send(&mut socket, 1, 0).unwrap();
            socket.read_exact(&mut request).unwrap();
            thread::sleep(Duration::from_millis(2300));
            let _ = send(&mut socket, 2, 2_300_000);
            let _ = socket.read_exact(&mut request);
        });
        let stop = Arc::new(AtomicBool::new(false));
        let latest = Arc::new(Mutex::new(Latest::default()));
        let (s, l) = (stop.clone(), latest.clone());
        let capture = thread::spawn(move || run_capture(address, s, l));
        let until = Instant::now() + Duration::from_millis(3400);
        while latest.lock().unwrap().frames < 2 && Instant::now() < until {
            thread::sleep(Duration::from_millis(10));
        }
        let frames = latest.lock().unwrap().frames;
        stop.store(true, Ordering::Release);
        source.join().unwrap();
        capture.join().unwrap();
        assert!(frames >= 2, "Slow valid frames must not force a reconnect");
    }
    fn local_server() -> (
        SocketAddr,
        Arc<AtomicBool>,
        Arc<Mutex<Latest>>,
        thread::JoinHandle<()>,
    ) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        listener.set_nonblocking(true).unwrap();
        let stop = Arc::new(AtomicBool::new(false));
        let latest = Arc::new(Mutex::new(Latest::default()));
        latest.lock().unwrap().publish(frame(0x55), Duration::ZERO);
        let (s, l) = (stop.clone(), latest.clone());
        let worker = thread::spawn(move || {
            serve(listener, s, l, Arc::new(AtomicUsize::new(0)), "test".into())
        });
        (address, stop, latest, worker)
    }
    fn get(address: SocketAddr, path: &str, fragment: bool) -> Vec<u8> {
        let mut socket = TcpStream::connect(address).unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        if fragment {
            socket.write_all(b"GET ").unwrap();
            thread::sleep(Duration::from_millis(40));
            write!(socket, "{path} HTTP/1.1\r\nHost: localhost\r\n\r\n").unwrap();
        } else {
            write!(socket, "GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n").unwrap();
        }
        let mut data = Vec::new();
        socket.read_to_end(&mut data).unwrap();
        data
    }
    #[test]
    fn privacy_filters_http_websocket_and_existing_viewers() {
        let (address, stop, latest, worker) = local_server();
        {
            let mut value = latest.lock().unwrap();
            value.privacy = true;
            value.revision += 1;
        }
        let still = get(address, "/test/frame.bmp", false);
        assert!(still.ends_with(include_bytes!("stream-logo.rgb565")));
        let socket = TcpStream::connect(address).unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        let (mut client, _) =
            tungstenite::client(format!("ws://{address}/test/frames"), socket).unwrap();
        let first = client.read().unwrap().into_data();
        assert_eq!(&first[16..], include_bytes!("stream-logo.rgb565"));
        let acknowledge = |client: &mut tungstenite::WebSocket<TcpStream>, packet: &[u8]| {
            client
                .send(Message::Binary(packet[4..8].to_vec().into()))
                .unwrap();
        };
        {
            let mut value = latest.lock().unwrap();
            let mut performance = frame(0);
            performance.pixels = crate::stream_privacy::tests::performance().into();
            value.publish(performance, Duration::ZERO);
        }
        acknowledge(&mut client, &first);
        let performance = client.read().unwrap().into_data();
        assert_eq!(
            &performance[16..],
            crate::stream_privacy::tests::performance()
        );
        latest.lock().unwrap().publish(frame(0x55), Duration::ZERO);
        acknowledge(&mut client, &performance);
        let browsing = client.read().unwrap().into_data();
        assert_eq!(&browsing[16..], include_bytes!("stream-logo.rgb565"));
        {
            let mut value = latest.lock().unwrap();
            value.privacy = false;
            value.revision += 1;
        }
        acknowledge(&mut client, &browsing);
        let disabled = client.read().unwrap().into_data();
        assert_eq!(&disabled[16..], &vec![0x55; PIXELS]);
        stop.store(true, Ordering::Release);
        worker.join().unwrap();
    }
    #[test]
    fn slow_connected_capture_does_not_flash_disconnected() {
        assert!(PAGE.contains(&format!("const MAX_FRAME_AGE_MS={MAX_FRAME_AGE_MS};")));
        let mut latest = Latest::default();
        latest.publish(frame(1), Duration::from_millis(1700));
        latest.at = Some(Instant::now() - Duration::from_millis(1800));
        assert!(
            latest.fresh(),
            "A capture slower than 1 fps is still connected"
        );
        assert_eq!(latest.status()["phase"], "live");
        latest.at = Some(Instant::now() - Duration::from_millis(3500));
        assert!(
            latest.fresh(),
            "Slow controller work is bounded by the receive budget"
        );
        latest.at = Some(Instant::now() - Duration::from_millis(5100));
        assert!(!latest.fresh(), "A stalled stream must still expire");
        latest.disconnected(CaptureError {
            reason: "stream",
            message: "socket closed".into(),
            legacy_fallback: false,
        });
        assert!(
            latest.frame.is_none(),
            "Real disconnects clear the picture immediately"
        );
    }
    #[test]
    fn rgb565_bitmap_is_exact() {
        let pixels = vec![0x12; PIXELS];
        let bmp = bitmap(&pixels);
        assert_eq!(&bmp[..2], b"BM");
        assert!(&bmp[66..] == pixels);
        assert_eq!(i32::from_le_bytes(bmp[22..26].try_into().unwrap()), -480);
    }
    #[test]
    fn http_handles_fragments_expiry_and_stop() {
        let (address, stop, latest, worker) = local_server();
        let result = get(address, "/test/frame.bmp", true);
        assert!(result.starts_with(b"HTTP/1.1 200"));
        assert!(result.ends_with(&vec![0x55; PIXELS]));
        latest.lock().unwrap().at = Some(Instant::now() - Duration::from_secs(6));
        assert!(get(address, "/test/frame.bmp", false).starts_with(b"HTTP/1.1 503"));
        assert!(get(address, "/other", false).starts_with(b"HTTP/1.1 404"));
        stop.store(true, Ordering::Release);
        worker.join().unwrap();
        assert!(TcpStream::connect(address).is_err());
    }
    #[test]
    fn websocket_uses_ack_and_skips_queued_frames() {
        let (address, stop, latest, worker) = local_server();
        let socket = TcpStream::connect(address).unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        let request = format!("ws://{address}/test/frames")
            .into_client_request()
            .unwrap();
        let (mut client, _) = tungstenite::client(request, socket).unwrap();
        let first = client.read().unwrap().into_data();
        assert_eq!(&first[..4], b"XZPV");
        assert_eq!(first.len(), PIXELS + 16);
        assert_eq!(u32::from_le_bytes(first[4..8].try_into().unwrap()), 1);
        {
            let mut value = latest.lock().unwrap();
            for color in 1..=5 {
                value.publish(frame(color), Duration::ZERO);
            }
        }
        client
            .send(Message::Binary(1u32.to_le_bytes().to_vec().into()))
            .unwrap();
        let last = client.read().unwrap().into_data();
        assert_eq!(u32::from_le_bytes(last[4..8].try_into().unwrap()), 6);
        assert_eq!(last[16], 5);
        client
            .send(Message::Binary(6u32.to_le_bytes().to_vec().into()))
            .unwrap();
        stop.store(true, Ordering::Release);
        worker.join().unwrap();
    }
    #[test]
    fn websocket_rejects_foreign_browser_origins() {
        let (address, stop, _, worker) = local_server();
        let socket = TcpStream::connect(address).unwrap();
        let mut request = format!("ws://{address}/test/frames")
            .into_client_request()
            .unwrap();
        request
            .headers_mut()
            .insert("Origin", "https://example.com".parse().unwrap());
        assert!(tungstenite::client(request, socket).is_err());
        stop.store(true, Ordering::Release);
        worker.join().unwrap();
    }
    #[test]
    fn rate_is_measured_and_cleared_on_disconnect() {
        let mut value = Latest::default();
        value.publish(frame(1), Duration::ZERO);
        assert!(value.status()["fps"].is_null());
        let now = Instant::now();
        value.samples = VecDeque::from([
            (now - Duration::from_secs(2), 64, true),
            (now - Duration::from_secs(1), 64, true),
            (now, 64, true),
        ]);
        assert_eq!(value.status()["fps"], 1.0);
        value.disconnected(CaptureError {
            message: "test".into(),
            reason: "stream",
            legacy_fallback: false,
        });
        assert!(value.status()["fps"].is_null());
        assert!(!value.fresh());
    }
    #[test]
    fn negotiates_old_loader_without_losing_its_working_picture() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            let mut request = [0u8; 8];
            socket.read_exact(&mut request).unwrap();
            assert_eq!(&request, b"XZSC\x02\0\0\0");
            drop(socket);
            let (mut socket, _) = listener.accept().unwrap();
            socket.read_exact(&mut request).unwrap();
            assert_eq!(&request, b"XZSC\x01\0\0\0");
            for byte in b"XZSF\x01\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0\x01\0\0\0" {
                socket.write_all(&[*byte]).unwrap();
            }
            socket.write_all(&vec![0x22; PIXELS]).unwrap();
        });
        let (_, _, frame, _) = negotiate(address).unwrap();
        assert_eq!(frame.protocol, Protocol::Legacy);
        assert_eq!(frame.pixels[0], 0x22);
        server.join().unwrap();
    }
    #[test]
    fn interrupted_capture_clears_pixels_and_reconnects() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let stop = Arc::new(AtomicBool::new(false));
        let latest = Arc::new(Mutex::new(Latest::default()));
        let (s, l) = (stop.clone(), latest.clone());
        let worker = thread::spawn(move || run_capture(address, s, l));
        let send = |socket: &mut TcpStream, color: u8| {
            let mut request = [0u8; 8];
            socket.read_exact(&mut request).unwrap();
            assert_eq!(&request, b"XZSC\x02\0\0\0");
            let mut header = vec![0u8; 32];
            header[..20].copy_from_slice(b"XZSF\x02\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0\x01\0\0\0");
            socket.write_all(&header).unwrap();
            socket.write_all(&vec![color; PIXELS]).unwrap();
        };
        let (mut first, _) = listener.accept().unwrap();
        send(&mut first, 0x11);
        let until = Instant::now() + Duration::from_secs(2);
        while latest.lock().unwrap().frames < 1 && Instant::now() < until {
            thread::sleep(Duration::from_millis(5));
        }
        first.shutdown(std::net::Shutdown::Both).unwrap();
        drop(first);
        let until = Instant::now() + Duration::from_secs(2);
        while latest.lock().unwrap().at.is_some() && Instant::now() < until {
            thread::sleep(Duration::from_millis(5));
        }
        {
            let value = latest.lock().unwrap();
            assert!(value.frame.is_none() && !value.fresh() && value.reason == "stream");
        }
        let (mut second, _) = listener.accept().unwrap();
        send(&mut second, 0x22);
        let until = Instant::now() + Duration::from_secs(2);
        while latest.lock().unwrap().frames < 2 && Instant::now() < until {
            thread::sleep(Duration::from_millis(5));
        }
        {
            let value = latest.lock().unwrap();
            assert_eq!(value.frame.as_ref().unwrap().pixels[0], 0x22);
            assert!(value.status()["fps"].is_null());
        }
        stop.store(true, Ordering::Release);
        second.shutdown(std::net::Shutdown::Both).unwrap();
        worker.join().unwrap();
    }
    #[test]
    #[ignore = "bounded PC-only native-handler/browser integration fixture"]
    fn browser_fixture() {
        let address: SocketAddr = std::env::var("XZ_SCREEN_FIXTURE_ADDRESS")
            .unwrap()
            .parse()
            .unwrap();
        assert!(address.ip().is_loopback());
        let receipt = std::path::PathBuf::from(std::env::var("XZ_SCREEN_FIXTURE_RECEIPT").unwrap());
        assert!(!receipt.exists());
        let session = ScreenSession::start(address).unwrap();
        std::fs::write(&receipt, session.connection_info().to_string()).unwrap();
        let mut observations =
            std::fs::File::create(receipt.with_extension("samples.jsonl")).unwrap();
        let mut last_sample = Instant::now() - Duration::from_secs(1);
        let until = Instant::now() + Duration::from_secs(180);
        while Instant::now() < until && !receipt.with_extension("stop").exists() {
            if last_sample.elapsed() >= Duration::from_secs(1) {
                writeln!(observations, "{}", session.status()).unwrap();
                observations.flush().unwrap();
                last_sample = Instant::now();
            }
            thread::sleep(Duration::from_millis(100));
        }
        std::fs::write(
            receipt.with_extension("result.json"),
            session.status().to_string(),
        )
        .unwrap();
    }
}
