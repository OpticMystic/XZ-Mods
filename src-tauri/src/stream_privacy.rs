use crate::screen_protocol::{rgb332_to_rgb565, rgb565_to_rgb332, Frame, Protocol, PIXELS};
use std::sync::{Arc, OnceLock};

// Stock 1.26 performance-screen landmarks, shared with mods/ui/key_strip.c.
// Unknown layouts and themes stay covered. Never infer performance from audio.
fn performance_screen(pixels: &[u8], quantized: bool) -> bool {
    if pixels.len() != PIXELS {
        return false;
    }
    let color = |pixel| {
        if quantized {
            rgb332_to_rgb565(rgb565_to_rgb332(pixel))
        } else {
            pixel
        }
    };
    let at = |x: usize, y: usize| {
        let offset = (y * 800 + x) * 2;
        u16::from_le_bytes([pixels[offset], pixels[offset + 1]])
    };
    if at(21, 31) != 0xffff || at(50, 43) != 0xffff {
        return false;
    }
    for deck in 0..2 {
        for x in [
            if deck == 0 { 95 } else { 497 },
            if deck == 0 { 219 } else { 621 },
            if deck == 0 { 395 } else { 797 },
        ] {
            if at(x, 314) != color(0x118c) || at(x, 345) != color(0x0861) {
                return false;
            }
        }
        for x in [4, 112] {
            for y in [116, 155] {
                if at(x, y + 143 * deck) != 0 {
                    return false;
                }
            }
        }
    }
    true
}

pub fn covered(frame: &Frame, enabled: bool) -> bool {
    enabled && !performance_screen(&frame.pixels, frame.protocol == Protocol::Rgb332)
}

pub fn output(mut frame: Frame, enabled: bool) -> Frame {
    if covered(&frame, enabled) {
        static LOGO: OnceLock<Arc<[u8]>> = OnceLock::new();
        frame.pixels = LOGO
            .get_or_init(|| Arc::from(include_bytes!("stream-logo.rgb565").as_slice()))
            .clone();
    }
    frame
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::screen_protocol::Protocol;

    pub fn performance() -> Vec<u8> {
        let mut pixels = vec![0; PIXELS];
        let mut set = |x: usize, y: usize, value: u16| {
            let offset = (y * 800 + x) * 2;
            pixels[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        };
        set(21, 31, 0xffff);
        set(50, 43, 0xffff);
        for x in [95, 219, 395, 497, 621, 797] {
            set(x, 314, 0x118c);
            set(x, 345, 0x0861);
        }
        pixels
    }
    #[test]
    fn eight_bit_palette_still_recognizes_performance_and_covers_browsing() {
        let pixels: Vec<u8> = performance()
            .chunks_exact(2)
            .flat_map(|pixel| {
                rgb332_to_rgb565(rgb565_to_rgb332(u16::from_le_bytes(
                    pixel.try_into().unwrap(),
                )))
                .to_le_bytes()
            })
            .collect();
        assert!(performance_screen(&pixels, true));
        assert!(!performance_screen(&pixels, false));
        let mut browse = pixels;
        browse[(31 * 800 + 21) * 2] = 0;
        assert!(!performance_screen(&browse, true));
    }
    #[test]
    fn unknown_browser_and_partial_screens_are_covered() {
        let mut frame = Frame {
            pixels: performance().into(),
            sequence: 1,
            protocol: Protocol::Compressed,
            device_time_us: 0,
            copy_us: 0,
            encode_us: 0,
            decode_us: 0,
            encoded_bytes: PIXELS,
            changed: true,
        };
        assert!(!covered(&frame, true));
        assert_eq!(output(frame.clone(), true).pixels, frame.pixels);
        let mut browser = performance();
        browser[(31 * 800 + 21) * 2] = 0;
        frame.pixels = browser.into();
        assert!(covered(&frame, true));
        assert_ne!(output(frame.clone(), true).pixels, frame.pixels);
        assert_eq!(output(frame.clone(), false).pixels, frame.pixels);
        frame.pixels = vec![0; PIXELS].into();
        assert!(covered(&frame, true));
        assert_eq!(output(frame, true).pixels.len(), PIXELS);
    }
}
