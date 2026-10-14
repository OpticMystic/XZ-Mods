use std::{
    io::{Read, Write},
    net::TcpStream,
    sync::Arc,
};

pub const PIXELS: usize = 800 * 480 * 2;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Protocol {
    Legacy,
    Compressed,
}
impl Protocol {
    pub fn version(self) -> u16 {
        match self {
            Self::Legacy => 1,
            Self::Compressed => 2,
        }
    }
    pub fn target_fps(self) -> u32 {
        match self {
            Self::Legacy => 1,
            Self::Compressed => 30,
        }
    }
}
#[derive(Debug)]
pub struct CaptureError {
    pub message: String,
    pub reason: &'static str,
    pub legacy_fallback: bool,
}
impl From<std::io::Error> for CaptureError {
    fn from(error: std::io::Error) -> Self {
        Self {
            message: format!("XZ screen connection interrupted: {error}"),
            reason: "stream",
            legacy_fallback: false,
        }
    }
}
fn invalid(message: &str) -> CaptureError {
    CaptureError {
        message: message.into(),
        reason: "protocol",
        legacy_fallback: false,
    }
}

#[derive(Clone, Debug)]
pub struct Frame {
    pub pixels: Arc<[u8]>,
    pub sequence: u32,
    pub protocol: Protocol,
    pub device_time_us: u32,
    pub copy_us: u32,
    pub encode_us: u32,
    pub decode_us: u32,
    pub encoded_bytes: usize,
    pub changed: bool,
}
pub struct Decoder {
    protocol: Protocol,
    previous: Option<Arc<[u8]>>,
    sequence: Option<u32>,
}
pub enum Event {
    Frame(Frame),
    Diagnostics(serde_json::Value),
}
impl Decoder {
    pub fn new(protocol: Protocol) -> Self {
        Self {
            protocol,
            previous: None,
            sequence: None,
        }
    }
    pub fn capture(&mut self, socket: &mut TcpStream) -> Result<Frame, CaptureError> {
        let mut request = *b"XZSC\x01\0\0\0";
        request[4] = self.protocol.version() as u8;
        socket.write_all(&request)?;
        self.read_frame(socket)
    }
    pub fn next_event(&mut self, source: &mut impl Read) -> Result<Event, CaptureError> {
        let mut magic = [0u8; 4];
        source.read_exact(&mut magic)?;
        match &magic {
            b"XZSF" => self
                .read_frame(&mut magic.as_slice().chain(source))
                .map(Event::Frame),
            b"XZSD" => {
                let mut length = [0u8; 4];
                source.read_exact(&mut length)?;
                let size = u32::from_le_bytes(length) as usize;
                if size == 0 || size > 16384 {
                    return Err(invalid("Invalid XZ diagnostic response"));
                }
                let mut data = vec![0u8; size];
                source.read_exact(&mut data)?;
                serde_json::from_slice(&data)
                    .map(Event::Diagnostics)
                    .map_err(|_| invalid("Invalid XZ diagnostic JSON"))
            }
            _ => Err(invalid("Unknown XZ screen packet")),
        }
    }
    fn read_frame(&mut self, source: &mut impl Read) -> Result<Frame, CaptureError> {
        let mut header = [0u8; 32];
        let header_size = if self.protocol == Protocol::Legacy {
            20
        } else {
            32
        };
        let mut received = 0;
        while received < header_size {
            match source.read(&mut header[received..header_size]) {
                Ok(0) => {
                    return Err(CaptureError {
                        message: "XZ closed the screen response".into(),
                        reason: "stream",
                        legacy_fallback: received == 0 && self.protocol == Protocol::Compressed,
                    })
                }
                Ok(bytes) => received += bytes,
                Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(error) => {
                    let fallback = received == 0
                        && self.protocol == Protocol::Compressed
                        && error.kind() == std::io::ErrorKind::ConnectionReset;
                    let mut error = CaptureError::from(error);
                    error.legacy_fallback = fallback;
                    return Err(error);
                }
            }
        }
        let word = |at| u16::from_le_bytes([header[at], header[at + 1]]);
        let number = |at: usize| u32::from_le_bytes(header[at..at + 4].try_into().unwrap());
        if &header[..4] != b"XZSF"
            || word(4) != self.protocol.version()
            || word(6) != 800
            || word(8) != 480
        {
            return Err(invalid(
                "The XZ returned an unsupported screen format. Update its USB loader.",
            ));
        }
        let codec = word(10);
        let size = number(12) as usize;
        let sequence = number(16);
        if self.protocol == Protocol::Legacy {
            if codec != 0 || size != PIXELS {
                return Err(invalid("Invalid legacy screen frame size"));
            }
        } else {
            if let Some(previous) = self.sequence {
                if sequence != previous.wrapping_add(1) {
                    return Err(invalid("Screen frame sequence changed; reconnecting"));
                }
            } else if sequence != 1 {
                return Err(invalid("Screen stream did not start with its first frame"));
            }
            match codec {
                0 if size == PIXELS => {}
                1 | 2 if size > 0 && size <= PIXELS && size % 2 == 0 => {}
                3 if size == 0 => {}
                _ => return Err(invalid("Invalid compressed screen frame size or codec")),
            }
            if (codec == 2 || codec == 3) && self.previous.is_none() {
                return Err(invalid("Screen delta has no initial frame"));
            }
        }
        let mut decode_us = 0;
        let pixels: Arc<[u8]> = if codec == 3 {
            self.previous
                .clone()
                .ok_or_else(|| invalid("Screen repeat has no initial frame"))?
        } else {
            let mut payload = vec![0u8; size];
            source.read_exact(&mut payload)?;
            let started = std::time::Instant::now();
            let decoded = if codec == 0 {
                payload.into()
            } else {
                decode_rle(
                    &payload,
                    if codec == 2 {
                        self.previous.as_deref()
                    } else {
                        None
                    },
                )?
                .into()
            };
            decode_us = started.elapsed().as_micros().min(u32::MAX as u128) as u32;
            decoded
        };
        let changed = self.previous.as_ref().is_none_or(|previous| {
            !Arc::ptr_eq(previous, &pixels) && previous.as_ref() != pixels.as_ref()
        });
        self.previous = Some(pixels.clone());
        self.sequence = Some(sequence);
        Ok(Frame {
            pixels,
            sequence,
            protocol: self.protocol,
            device_time_us: if header_size == 32 { number(20) } else { 0 },
            copy_us: if header_size == 32 { number(24) } else { 0 },
            encode_us: if header_size == 32 { number(28) } else { 0 },
            decode_us,
            encoded_bytes: size + header_size,
            changed,
        })
    }
}

fn decode_rle(payload: &[u8], previous: Option<&[u8]>) -> Result<Vec<u8>, CaptureError> {
    let mut pixels = if let Some(previous) = previous {
        if previous.len() != PIXELS {
            return Err(invalid("Invalid previous screen frame"));
        }
        previous.to_vec()
    } else {
        vec![0; PIXELS]
    };
    let mut input = 0;
    let mut output = 0;
    while input < payload.len() {
        if payload.len() - input < 2 {
            return Err(invalid("Truncated screen RLE command"));
        }
        let command = u16::from_le_bytes([payload[input], payload[input + 1]]);
        input += 2;
        let bytes = ((command as usize & 32767) + 1) * 2;
        if bytes > PIXELS - output {
            return Err(invalid("Screen RLE exceeds frame bounds"));
        }
        let repeated = command & 32768 != 0;
        let needed = if repeated { 2 } else { bytes };
        if needed > payload.len() - input {
            return Err(invalid("Truncated screen RLE pixels"));
        }
        let target = &mut pixels[output..output + bytes];
        if repeated {
            let color = &payload[input..input + 2];
            if previous.is_some() {
                if color != [0, 0] {
                    for pixel in target.chunks_exact_mut(2) {
                        pixel[0] ^= color[0];
                        pixel[1] ^= color[1];
                    }
                }
            } else {
                for pixel in target.chunks_exact_mut(2) {
                    pixel.copy_from_slice(color);
                }
            }
        } else if previous.is_some() {
            for (value, delta) in target.iter_mut().zip(&payload[input..input + bytes]) {
                *value ^= delta;
            }
        } else {
            target.copy_from_slice(&payload[input..input + bytes]);
        }
        input += needed;
        output += bytes;
    }
    if output != PIXELS {
        return Err(invalid("Screen RLE ended before the full frame"));
    }
    Ok(pixels)
}

pub fn acknowledge(socket: &mut TcpStream, sequence: u32) -> Result<(), CaptureError> {
    let mut packet = *b"XZAK\0\0\0\0";
    packet[4..].copy_from_slice(&sequence.to_le_bytes());
    socket.write_all(&packet)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn repeated(color: u16) -> Vec<u8> {
        let mut out = Vec::new();
        let mut remaining = PIXELS / 2;
        while remaining > 0 {
            let count = remaining.min(32768);
            out.extend_from_slice(&(0x8000 | (count as u16 - 1)).to_le_bytes());
            out.extend_from_slice(&color.to_le_bytes());
            remaining -= count;
        }
        out
    }
    fn packet(codec: u16, sequence: u32, data: &[u8]) -> Vec<u8> {
        let mut out = vec![0u8; 32];
        out[..4].copy_from_slice(b"XZSF");
        for (at, value) in [(4, 2u16), (6, 800), (8, 480), (10, codec)] {
            out[at..at + 2].copy_from_slice(&value.to_le_bytes());
        }
        out[12..16].copy_from_slice(&(data.len() as u32).to_le_bytes());
        out[16..20].copy_from_slice(&sequence.to_le_bytes());
        out.extend_from_slice(data);
        out
    }
    #[test]
    fn pushed_frames_and_diagnostics_share_the_stream() {
        let mut data = packet(1, 1, &repeated(0x1234));
        data.extend_from_slice(b"XZSD\x0d\0\0\0{\"test\":true}");
        data.extend_from_slice(&packet(3, 2, &[]));
        let mut source = data.as_slice();
        let mut decoder = Decoder::new(Protocol::Compressed);
        assert!(matches!(
            decoder.next_event(&mut source).unwrap(),
            Event::Frame(Frame { sequence: 1, .. })
        ));
        match decoder.next_event(&mut source).unwrap() {
            Event::Diagnostics(value) => assert_eq!(value["test"], true),
            _ => panic!("Expected diagnostics"),
        }
        assert!(matches!(
            decoder.next_event(&mut source).unwrap(),
            Event::Frame(Frame { sequence: 2, .. })
        ));
        assert!(source.is_empty());
    }
    #[test]
    fn key_delta_repeat_are_bit_exact() {
        let mut decoder = Decoder::new(Protocol::Compressed);
        let key = decoder
            .read_frame(&mut packet(1, 1, &repeated(0x1234)).as_slice())
            .unwrap();
        assert!(key
            .pixels
            .chunks_exact(2)
            .all(|pixel| pixel == [0x34, 0x12]));
        let delta = decoder
            .read_frame(&mut packet(2, 2, &repeated(0x00ff)).as_slice())
            .unwrap();
        assert!(delta
            .pixels
            .chunks_exact(2)
            .all(|pixel| pixel == [0xcb, 0x12]));
        let same = decoder
            .read_frame(&mut packet(3, 3, &[]).as_slice())
            .unwrap();
        assert!(Arc::ptr_eq(&same.pixels, &delta.pixels));
    }
    #[test]
    fn literals_and_xor_literals_are_exact() {
        let expected: Vec<u8> = (0..PIXELS).map(|i| (i % 251) as u8).collect();
        let mut encoded = Vec::new();
        for chunk in expected.chunks(65536) {
            encoded.extend_from_slice(&((chunk.len() / 2 - 1) as u16).to_le_bytes());
            encoded.extend_from_slice(chunk);
        }
        assert_eq!(decode_rle(&encoded, None).unwrap(), expected);
        assert_eq!(
            decode_rle(&encoded, Some(&expected)).unwrap(),
            vec![0; PIXELS]
        );
    }
    #[test]
    fn rejects_bad_stream_before_reading_payload() {
        let cases = [
            packet(2, 1, &repeated(0)),
            packet(3, 1, &[]),
            packet(4, 1, &[]),
            packet(1, 1, &[0]),
            packet(0, 1, &[]),
            packet(1, 9, &repeated(0)),
        ];
        for bad in cases {
            assert!(Decoder::new(Protocol::Compressed)
                .read_frame(&mut bad.as_slice())
                .is_err());
        }
        let mut decoder = Decoder::new(Protocol::Compressed);
        decoder
            .read_frame(&mut packet(1, 1, &repeated(0)).as_slice())
            .unwrap();
        assert!(decoder
            .read_frame(&mut packet(3, 3, &[]).as_slice())
            .is_err());
    }
    #[test]
    fn malformed_rle_never_grows_a_frame() {
        assert!(decode_rle(&[0, 0, 1, 2], None).is_err());
        assert!(decode_rle(&[255, 255, 0], None).is_err());
        let mut overflow = repeated(0);
        overflow.extend_from_slice(&[0, 128, 0, 0]);
        assert!(decode_rle(&overflow, None).is_err());
        for size in 0..48 {
            assert!(decode_rle(&repeated(0)[..size], None).is_err());
        }
    }
}
